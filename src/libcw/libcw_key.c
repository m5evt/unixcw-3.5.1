/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2014  Kamil Ignacak (acerion@wp.pl)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/



/**
   \file libcw_key.c


*/

#include <stdbool.h>
#include <inttypes.h> /* uint32_t */

#include "libcw_debug.h"
#include "libcw_key.h"
#include "libcw_tq.h"



extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;

extern cw_rec_t receiver;
extern cw_gen_t **cw_generator;





/* ******************************************************************** */
/*                       Section:Keying control                         */
/* ******************************************************************** */
/* Code maintaining state of a key, and handling changes of key state.
   A key can be in two states:
   \li open - a physical key with electric contacts open, no sound or
   continuous wave is generated;
   \li closed - a physical key with electric contacts closed, a sound
   or continuous wave is generated;

   Key type is not specified. This code maintains state of any type
   of key: straight key, cootie key, iambic key. All that matters is
   state of contacts (open/closed).

   The concept of "key" is extended to a software generator (provided
   by this library) that generates Morse code wave from text input.
   This means that key is closed when a tone (element) is generated,
   and key is open when there is inter-tone (inter-element) space.

   Client code can register - using cw_register_keying_callback() -
   a client callback function. The function will be called every time the
   state of a key changes. */
static void cw_key_straight_key_generate_internal(cw_gen_t *gen, int key_state);
static void cw_key_iambic_keyer_generate_internal(cw_gen_t *gen, int key_state, int usecs);





/* ******************************************************************** */
/*                        Section:Iambic keyer                          */
/* ******************************************************************** */




/*
 * The CW keyer functions implement the following state graph:
 *
 *        +-----------------------------------------------------+
 *        |          (all latches clear)                        |
 *        |                                     (dot latch)     |
 *        |                          +--------------------------+
 *        |                          |                          |
 *        |                          v                          |
 *        |      +-------------> KS_IN_DOT_[A|B] -------> KS_AFTER_DOT_[A|B]
 *        |      |(dot paddle)       ^            (delay)       |
 *        |      |                   |                          |(dash latch/
 *        |      |                   +------------+             | _B)
 *        v      |                                |             |
 * --> KS_IDLE --+                   +--------------------------+
 *        ^      |                   |            |
 *        |      |                   |            +-------------+(dot latch/
 *        |      |                   |                          | _B)
 *        |      |(dash paddle)      v            (delay)       |
 *        |      +-------------> KS_IN_DASH_[A|B] -------> KS_AFTER_DASH_[A|B]
 *        |                          ^                          |
 *        |                          |                          |
 *        |                          +--------------------------+
 *        |                                     (dash latch)    |
 *        |          (all latches clear)                        |
 *        +-----------------------------------------------------+
 */



static const char *cw_iambic_keyer_states[] = {
	"KS_IDLE",
	"KS_IN_DOT_A",
	"KS_IN_DASH_A",
	"KS_AFTER_DOT_A",
	"KS_AFTER_DASH_A",
	"KS_IN_DOT_B",
	"KS_IN_DASH_B",
	"KS_AFTER_DOT_B",
	"KS_AFTER_DASH_B"
};





cw_iambic_keyer_t cw_iambic_keyer = {
	.state = KS_IDLE,

	.dot_paddle = false,
	.dash_paddle = false,

	.dot_latch = false,
	.dash_latch = false,

	.curtis_mode_b = false,
	.curtis_b_latch = false,

	.lock = false,

	.timer = NULL
};





static void cw_iambic_keyer_update_initial_internal(cw_iambic_keyer_t *keyer, cw_gen_t *gen);





/* ******************************************************************** */
/*                       Section:Keying control                         */
/* ******************************************************************** */





/* External "on key state change" callback function and its argument.

   It may be useful for a client to have this library control an external
   keying device, for example, an oscillator, or a transmitter.
   Here is where we keep the address of a function that is passed to us
   for this purpose, and a void* argument for it. */
static void (*cw_kk_key_callback)(void*, int) = NULL;
static void *cw_kk_key_callback_arg = NULL;





/**
   \brief Register external callback function for keying

   Register a \p callback_func function that should be called when a state
   of a key changes from "key open" to "key closed", or vice-versa.

   The first argument passed to the registered callback function is the
   supplied \p callback_arg, if any.  The second argument passed to
   registered callback function is the key state: CW_KEY_STATE_CLOSED
   (one/true) for "key closed", and CW_KEY_STATE_OPEN (zero/false) for
   "key open".

   Calling this routine with a NULL function address disables keying
   callbacks.  Any callback supplied will be called in signal handler
   context (??).

   \param callback_func - callback function to be called on key state changes
   \param callback_arg - first argument to callback_func
*/
void cw_register_keying_callback(void (*callback_func)(void*, int),
				 void *callback_arg)
{
	cw_kk_key_callback = callback_func;
	cw_kk_key_callback_arg = callback_arg;

	return;
}





/*
  Most of the time libcw just passes around cw_kk_key_callback_arg,
  not caring of what type it is, and not attempting to do any
  operations on it. On one occasion however, it needs to know whether
  cw_kk_key_callback_arg is of type 'struct timeval', and if so, it
  must do some operation on it. I could pass struct with ID as
  cw_kk_key_callback_arg, but that may break some old client
  code. Instead I've created this function that has only one, very
  specific purpose: to pass to libcw a pointer to timer.

  The timer is owned by client code, and is used to measure and clock
  iambic keyer.
*/
void cw_iambic_keyer_register_timer(struct timeval *timer)
{
	cw_iambic_keyer.timer = timer;

	return;
}





/**
   \brief Set new key state

   Set new state of a key. Filter successive key-down or key-up
   actions into a single action (successive calls with the same value
   of \p key_state don't change internally registered state of key).

   If and only if the function registers change of key state, an
   external callback function for keying (if configured) is called.

   Notice that the function is used only in
   cw_tone_queue_dequeue_internal(). A generator which owns a tone
   queue is treated as a key, and dequeued tones are treated as key
   states. Dequeueing tones is treated as manipulating a key.

   \param key_state - key state to be set
*/
void cw_key_set_state_internal(int key_state)
{
	static int current_key_state = CW_KEY_STATE_OPEN;  /* Maintained key control state */

	if (current_key_state != key_state) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw: keying state %d->%d", current_key_state, key_state);

		/* Set the new keying state, and call any requested callback. */
		current_key_state = key_state;
		if (cw_kk_key_callback) {
			(*cw_kk_key_callback)(cw_kk_key_callback_arg, current_key_state);
		}
	}

	return;
}





/**
   \brief Set new key state, generate appropriate tone

   Set new state of a key. Filter successive key-down or key-up
   actions into a single action (successive calls with the same value
   of \p key_state don't change internally registered state of key).

   If and only if the function registers change of key state, an
   external callback function for keying (if configured) is called.

   If and only if the function registers change of key state, a state
   of related generator \p gen is changed accordingly (a tone is
   started or stopped).

   \param gen - generator to be used to emit tones as state of key changes
   \param key_state - key state to be set
*/
void cw_key_straight_key_generate_internal(cw_gen_t *gen, int key_state)
{
	static int current_key_state = CW_KEY_STATE_OPEN;  /* Maintained key control state */

	if (current_key_state != key_state) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw: straight key: keying state %d->%d", current_key_state, key_state);

		/* Set the new keying state, and call any requested callback. */
		current_key_state = key_state;
		if (cw_kk_key_callback) {
			(*cw_kk_key_callback)(cw_kk_key_callback_arg, current_key_state);
		}

		if (current_key_state == CW_KEY_STATE_CLOSED) {

			/* First a transient state of rising slope,
			   then a constant tone. The constant tone
			   will be played until function receives
			   CW_KEY_STATE_OPEN key state. */

			cw_tone_t tone;
			tone.usecs = gen->tone_slope.length_usecs;
			tone.frequency = gen->frequency;
			tone.slope_mode = CW_SLOPE_MODE_RISING_SLOPE;
			cw_tone_queue_enqueue_internal(gen->tq, &tone);

			tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
			tone.usecs = CW_AUDIO_FOREVER_USECS;
			tone.frequency = gen->frequency;
			cw_tone_queue_enqueue_internal(gen->tq, &tone);

			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
				      "libcw: tone queue: len = %"PRIu32"", cw_tone_queue_length_internal(gen->tq));
		} else {
			if (gen->audio_system == CW_AUDIO_CONSOLE) {
				/* Play just a bit of silence, just to switch
				   buzzer from playing a sound to being silent. */
				cw_tone_t tone;
				tone.usecs = CW_AUDIO_QUANTUM_USECS;
				tone.frequency = 0;
				tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
				cw_tone_queue_enqueue_internal(gen->tq, &tone);
			} else {
				/* For soundcards a falling slope with
				   volume from max to zero should be
				   enough, but... */
				cw_tone_t tone;
				tone.usecs = gen->tone_slope.length_usecs;
				tone.frequency = gen->frequency;
				tone.slope_mode = CW_SLOPE_MODE_FALLING_SLOPE;
				cw_tone_queue_enqueue_internal(gen->tq, &tone);

				/* On some occasions, on some platforms, some
				   sound systems may need to constantly play
				   "silent" tone. These four lines of code are
				   just for them.

				   It would be better to avoid queueing silent
				   "forever" tone because this increases CPU
				   usage. It would be better to simply not to
				   queue any new tones after "falling slope"
				   tone. Silence after the last falling slope
				   would simply last on itself until there is
				   new tone on queue to play. */
				tone.usecs = CW_AUDIO_FOREVER_USECS;
				tone.frequency = 0;
				tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
				cw_tone_queue_enqueue_internal(gen->tq, &tone);
			}
		}
	}

	return;
}





/**
   \brief Call external callback function for keying

   Control function that calls any requested keying callback only when there
   is a change of keying state.  This function filters successive key-down
   or key-up actions into a single action.

   \param gen - generator
   \param key_state - key state to be set
   \param usecs - length of tone to be generated
*/
void cw_key_iambic_keyer_generate_internal(cw_gen_t *gen, int key_state, int usecs)
{
	static int current_key_state = CW_KEY_STATE_OPEN;  /* Maintained key control state */

	if (current_key_state != key_state) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw: iambic keyer: keying state %d->%d", current_key_state, key_state);

		/* Set the new keying state, and call any requested callback. */
		current_key_state = key_state;
		if (cw_kk_key_callback) {
			(*cw_kk_key_callback)(cw_kk_key_callback_arg, current_key_state);
		}

		cw_tone_t tone;
		if (current_key_state == CW_KEY_STATE_CLOSED) {
			tone.slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES;
			tone.usecs = usecs;
			tone.frequency = gen->frequency;
			cw_tone_queue_enqueue_internal(gen->tq, &tone);
		} else {
			tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
			tone.usecs = usecs;
			tone.frequency = 0;
			cw_tone_queue_enqueue_internal(gen->tq, &tone);
		}
	}

	return;
}





/* ******************************************************************** */
/*                        Section:Iambic keyer                          */
/* ******************************************************************** */





/**
   \brief Enable iambic Curtis mode B

   Normally, the iambic keying functions will emulate Curtis 8044 Keyer
   mode A.  In this mode, when both paddles are pressed together, the
   last dot or dash being sent on release is completed, and nothing else
   is sent. In mode B, when both paddles are pressed together, the last
   dot or dash being sent on release is completed, then an opposite
   element is also sent. Some operators prefer mode B, but timing is
   more critical in this mode. The default mode is Curtis mode A.
*/
void cw_enable_iambic_curtis_mode_b(void)
{
	cw_iambic_keyer.curtis_mode_b = true;
	return;
}





/**
   See documentation of cw_enable_iambic_curtis_mode_b() for more information
*/
void cw_disable_iambic_curtis_mode_b(void)
{
	cw_iambic_keyer.curtis_mode_b = false;
	return;
}





/**
   See documentation of cw_enable_iambic_curtis_mode_b() for more information
*/
int cw_get_iambic_curtis_mode_b_state(void)
{
	return cw_iambic_keyer.curtis_mode_b;
}





/**
   \brief Update state of iambic keyer, queue tone representing state of the iambic keyer

   It seems that the function is called when a client code informs
   about change of state of one of paddles. So I think what the
   function does is that it takes the new state of paddles and
   re-evaluate internal state of iambic keyer.

   The function is also called in generator's thread function
   cw_generator_dequeue_and_play_internal() each time a tone is
   dequeued and pushed to audio system. I don't know why make the call
   in that place for iambic keyer, but not for straight key.

   \param keyer - iambic keyer
   \param gen - current generator

   \return CW_FAILURE if there is a lock and the function cannot proceed
   \return CW_SUCCESS otherwise
*/
int cw_iambic_keyer_update_internal(cw_iambic_keyer_t *keyer, cw_gen_t *gen)
{
	if (keyer->lock) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_INTERNAL, CW_DEBUG_ERROR,
			      "libcw: lock in thread %ld", (long) pthread_self());
		return CW_FAILURE;
	}
	keyer->lock = true;

	/* Synchronize low level timing parameters if required. */
	cw_sync_parameters_internal(gen, &receiver);

	int cw_iambic_keyer_state_old = keyer->state;

	/* Decide what to do based on the current state. */
	switch (keyer->state) {
		/* Ignore calls if our state is idle. */
	case KS_IDLE:
		keyer->lock = false;
		return CW_SUCCESS;

		/* If we were in a dot, turn off tones and begin the
		   after-dot delay.  Do much the same if we are in a dash.
		   No routine status checks are made since we are in a
		   signal handler, and can't readily return error codes
		   to the client. */
	case KS_IN_DOT_A:
	case KS_IN_DOT_B:
		cw_key_iambic_keyer_generate_internal(gen, CW_KEY_STATE_OPEN, gen->eoe_delay);
		keyer->state = keyer->state == KS_IN_DOT_A
			? KS_AFTER_DOT_A : KS_AFTER_DOT_B;
		break;

	case KS_IN_DASH_A:
	case KS_IN_DASH_B:
		cw_key_iambic_keyer_generate_internal(gen, CW_KEY_STATE_OPEN, gen->eoe_delay);
		keyer->state = keyer->state == KS_IN_DASH_A
			? KS_AFTER_DASH_A : KS_AFTER_DASH_B;

		break;

		/* If we have just finished a dot or a dash and its
		   post-element delay, then reset the latches as
		   appropriate.  Next, if in a _B state, go straight to
		   the opposite element state.  If in an _A state, check
		   the latch states; if the opposite latch is set true,
		   then do the iambic thing and alternate dots and dashes.
		   If the same latch is true, repeat.  And if nothing is
		   true, then revert to idling. */
	case KS_AFTER_DOT_A:
	case KS_AFTER_DOT_B:
		if (!keyer->dot_paddle) {
			keyer->dot_latch = false;
		}

		if (keyer->state == KS_AFTER_DOT_B) {
			cw_key_iambic_keyer_generate_internal(gen, CW_KEY_STATE_CLOSED, gen->dash_length);
			keyer->state = KS_IN_DASH_A;
		} else if (keyer->dash_latch) {
			cw_key_iambic_keyer_generate_internal(gen, CW_KEY_STATE_CLOSED, gen->dash_length);
			if (keyer->curtis_b_latch){
				keyer->curtis_b_latch = false;
				keyer->state = KS_IN_DASH_B;
			} else {
				keyer->state = KS_IN_DASH_A;
			}
		} else if (keyer->dot_latch) {
			cw_key_iambic_keyer_generate_internal(gen, CW_KEY_STATE_CLOSED, gen->dot_length);
			keyer->state = KS_IN_DOT_A;
		} else {
			keyer->state = KS_IDLE;
			//cw_finalization_schedule_internal();
		}

		break;

	case KS_AFTER_DASH_A:
	case KS_AFTER_DASH_B:
		if (!keyer->dash_paddle) {
			keyer->dash_latch = false;
		}
		if (keyer->state == KS_AFTER_DASH_B) {
			cw_key_iambic_keyer_generate_internal(gen, CW_KEY_STATE_CLOSED, gen->dot_length);
			keyer->state = KS_IN_DOT_A;
		} else if (keyer->dot_latch) {
			cw_key_iambic_keyer_generate_internal(gen, CW_KEY_STATE_CLOSED, gen->dot_length);
			if (keyer->curtis_b_latch) {
				keyer->curtis_b_latch = false;
				keyer->state = KS_IN_DOT_B;
			} else {
				keyer->state = KS_IN_DOT_A;
			}
		} else if (keyer->dash_latch) {
			cw_key_iambic_keyer_generate_internal(gen, CW_KEY_STATE_CLOSED, gen->dash_length);
			keyer->state = KS_IN_DASH_A;
		} else {
			keyer->state = KS_IDLE;
			//cw_finalization_schedule_internal();
		}

		break;
	}


	cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
		      "libcw: cw_keyer_state: %s -> %s",
		      cw_iambic_keyer_states[cw_iambic_keyer_state_old], cw_iambic_keyer_states[keyer->state]);

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      "libcw: keyer %s -> %s",
		      cw_iambic_keyer_states[cw_iambic_keyer_state_old], cw_iambic_keyer_states[keyer->state]);

	keyer->lock = false;
	return CW_SUCCESS;
}





/**
   \brief Inform about changed state of iambic keyer's paddles

   Function informs the library that the iambic keyer paddles have
   changed state.  The new paddle states are recorded, and if either
   transition from false to true, paddle latches, for iambic functions,
   are also set.

   On success, the routine returns CW_SUCCESS.
   On failure, it returns CW_FAILURE, with errno set to EBUSY if the
   tone queue or straight key are using the sound card, console
   speaker, or keying system.

   If appropriate, this routine starts the keyer functions sending the
   relevant element.  Element send and timing occurs in the background,
   so this routine returns almost immediately.  See cw_keyer_element_wait()
   and cw_keyer_wait() for details about how to check the current status of
   iambic keyer background processing.

   testedin::test_keyer()

   \param dot_paddle_state
   \param dash_paddle_state

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_notify_keyer_paddle_event(int dot_paddle_state,
				 int dash_paddle_state)
{
	/* If the tone queue or the straight key are busy, this is going to
	   conflict with our use of the sound card, console sounder, and
	   keying system.  So return an error status in this case. */
	// if (cw_is_straight_key_busy() || cw_is_tone_busy()) {
	if (0) {
		errno = EBUSY;
		return CW_FAILURE;
	}

	/* Clean up and save the paddle states passed in. */
	cw_iambic_keyer.dot_paddle = (dot_paddle_state != 0);
	cw_iambic_keyer.dash_paddle = (dash_paddle_state != 0);

	/* Update the paddle latches if either paddle goes true.
	   The latches are checked in the signal handler, so if the paddles
	   go back to false during this element, the item still gets
	   actioned.  The signal handler is also responsible for clearing
	   down the latches. */
	if (cw_iambic_keyer.dot_paddle) {
		cw_iambic_keyer.dot_latch = true;
	}
	if (cw_iambic_keyer.dash_paddle) {
		cw_iambic_keyer.dash_latch = true;
	}

	/* If in Curtis mode B, make a special check for both paddles true
	   at the same time.  This flag is checked by the signal handler,
	   to determine whether to add mode B trailing timing elements. */
	if (cw_iambic_keyer.curtis_mode_b && cw_iambic_keyer.dot_paddle && cw_iambic_keyer.dash_paddle) {
		cw_iambic_keyer.curtis_b_latch = true;
	}

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      "libcw: keyer paddles %d,%d, latches %d,%d, curtis_b %d",
		      cw_iambic_keyer.dot_paddle, cw_iambic_keyer.dash_paddle,
		      cw_iambic_keyer.dot_latch, cw_iambic_keyer.dash_latch, cw_iambic_keyer.curtis_b_latch);



	if (cw_iambic_keyer.state == KS_IDLE) {
		/* If the current state is idle, give the state
		   process a nudge. */
		cw_iambic_keyer_update_initial_internal(&cw_iambic_keyer, (*cw_generator));
	} else {
		/* The state machine for iambic keyer is already in
		   motion, no need to do anything more.

		   Current paddle states have been recorded in this
		   function. Any future changes of paddle states will
		   be also recorded by this function. In both cases
		   the main action upon the states is taken somewhere
		   else. */
		;
	}

	return CW_SUCCESS;
}





/**
   \brief Initiate work of iambic keyer state machine

   State machine for iambic keyer must be pushed from KS_IDLE
   state. Call this function to do this.
*/
void cw_iambic_keyer_update_initial_internal(cw_iambic_keyer_t *keyer, cw_gen_t *gen)
{
	if (keyer->dot_paddle) {
		/* Pretend we just finished a dash. */
		keyer->state = keyer->curtis_b_latch
			? KS_AFTER_DASH_B : KS_AFTER_DASH_A;

		if (!cw_iambic_keyer_update_internal(keyer, gen)) {
			/* just try again, once */
			usleep(1000);
			cw_iambic_keyer_update_internal(keyer, gen);
		}
	} else if (keyer->dash_paddle) {
		/* Pretend we just finished a dot. */
		keyer->state = keyer->curtis_b_latch
			? KS_AFTER_DOT_B : KS_AFTER_DOT_A;

		if (!cw_iambic_keyer_update_internal(keyer, gen)) {
			/* just try again, once */
			usleep(1000);
			cw_iambic_keyer_update_internal(keyer, gen);
		}
	} else {
		/* Both paddles are open/up. We certainly don't want
		   to start any process upon "both paddles open"
		   event. */
		;
	}

	return;
}





/**
   \brief Change state of dot paddle

   Alter the state of just one of the two iambic keyer paddles.
   The other paddle state of the paddle pair remains unchanged.

   See cw_keyer_paddle_event() for details of iambic keyer background
   processing, and how to check its status.

   \param dot_paddle_state
*/
int cw_notify_keyer_dot_paddle_event(int dot_paddle_state)
{
	return cw_notify_keyer_paddle_event(dot_paddle_state, cw_iambic_keyer.dash_paddle);
}





/**
   See documentation of cw_notify_keyer_dot_paddle_event() for more information
*/
int cw_notify_keyer_dash_paddle_event(int dash_paddle_state)
{
	return cw_notify_keyer_paddle_event(cw_iambic_keyer.dot_paddle, dash_paddle_state);
}





/**
   \brief Get the current saved states of the two paddles

   testedin::test_keyer()

   \param dot_paddle_state
   \param dash_paddle_state
*/
void cw_get_keyer_paddles(int *dot_paddle_state, int *dash_paddle_state)
{
	if (dot_paddle_state) {
		*dot_paddle_state = cw_iambic_keyer.dot_paddle;
	}
	if (dash_paddle_state) {
		*dash_paddle_state = cw_iambic_keyer.dash_paddle;
	}
	return;
}





/**
   \brief Get the current states of paddle latches

   Function returns the current saved states of the two paddle latches.
   A paddle latches is set to true when the paddle state becomes true,
   and is cleared if the paddle state is false when the element finishes
   sending.

   \param dot_paddle_latch_state
   \param dash_paddle_latch_state
*/
void cw_get_keyer_paddle_latches(int *dot_paddle_latch_state,
				 int *dash_paddle_latch_state)
{
	if (dot_paddle_latch_state) {
		*dot_paddle_latch_state = cw_iambic_keyer.dot_latch;
	}
	if (dash_paddle_latch_state) {
		*dash_paddle_latch_state = cw_iambic_keyer.dash_latch;
	}
	return;
}





/**
   \brief Check if a keyer is busy

   \return true if keyer is busy
   \return false if keyer is not busy
*/
bool cw_is_keyer_busy(void)
{
	return cw_iambic_keyer.state != KS_IDLE;
}





/**
   \brief Wait for end of element from the keyer

   Waits until the end of the current element, dot or dash, from the keyer.

   On error the function returns CW_FAILURE, with errno set to
   EDEADLK if SIGALRM is blocked.

   testedin::test_keyer()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_keyer_element(void)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* First wait for the state to move to idle (or just do nothing
	   if it's not), or to one of the after- states. */
	while (cw_iambic_keyer.state != KS_IDLE
	       && cw_iambic_keyer.state != KS_AFTER_DOT_A
	       && cw_iambic_keyer.state != KS_AFTER_DOT_B
	       && cw_iambic_keyer.state != KS_AFTER_DASH_A
	       && cw_iambic_keyer.state != KS_AFTER_DASH_B) {

		cw_signal_wait_internal();
	}

	/* Now wait for the state to move to idle (unless it is, or was,
	   already), or one of the in- states, at which point we know
	   we're actually at the end of the element we were in when we
	   entered this routine. */
	while (cw_iambic_keyer.state != KS_IDLE
	       && cw_iambic_keyer.state != KS_IN_DOT_A
	       && cw_iambic_keyer.state != KS_IN_DOT_B
	       && cw_iambic_keyer.state != KS_IN_DASH_A
	       && cw_iambic_keyer.state != KS_IN_DASH_B) {

		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the current keyer cycle to complete

   The routine returns CW_SUCCESS on success.  On error, it returns
   CW_FAILURE, with errno set to EDEADLK if SIGALRM is blocked or if
   either paddle state is true.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_keyer(void)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Check that neither paddle is true; if either is, then the signal
	   cycle is going to continue forever, and we'll never return from
	   this routine. */
	if (cw_iambic_keyer.dot_paddle || cw_iambic_keyer.dash_paddle) {
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait for the keyer state to go idle. */
	while (cw_iambic_keyer.state != KS_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Reset iambic keyer data

   Clear all latches and paddle states of iambic keyer, return to
   Curtis 8044 Keyer mode A, and return to silence.  This function is
   suitable for calling from an application exit handler.
*/
void cw_reset_keyer(void)
{
	cw_iambic_keyer.dot_paddle = false;
	cw_iambic_keyer.dash_paddle = false;
	cw_iambic_keyer.dot_latch = false;
	cw_iambic_keyer.dash_latch = false;
	cw_iambic_keyer.curtis_b_latch = false;
	cw_iambic_keyer.curtis_mode_b = false;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      "libcw: assigning to cw_keyer_state %s -> KS_IDLE", cw_iambic_keyer_states[cw_iambic_keyer.state]);
	cw_iambic_keyer.state = KS_IDLE;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_generator_silence_internal((*cw_generator));
	cw_finalization_schedule_internal();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      "libcw: keyer -> %s (reset)", cw_iambic_keyer_states[cw_iambic_keyer.state]);

	return;
}





/* ******************************************************************** */
/*                        Section:Straight key                          */
/* ******************************************************************** */





/* Straight key status; just a key-up or key-down indication. */
static volatile bool cw_sk_key_state = CW_KEY_STATE_OPEN;


#if 0 /* unused */
/* Period of constant tone generation after which we need another timeout,
   to ensure that the soundcard doesn't run out of data. */
static const int STRAIGHT_KEY_TIMEOUT = 500000;





/**
   \brief Generate a tone while straight key is down

   Soundcard tone data is only buffered to last about a second on each
   cw_generate_sound_internal() call, and holding down the straight key
   for longer than this could cause a soundcard data underrun.  To guard
   against this, a timeout is generated every half-second or so while the
   straight key is down.  The timeout generates a chunk of sound data for
   the soundcard.
*/
void cw_straight_key_clock_internal(void)
{
	if (cw_sk_key_state == CW_KEY_STATE_CLOSED) {
		/* Generate a quantum of tone data, and request another
		   timeout. */
		// cw_generate_sound_internal();
		cw_timer_run_with_handler_internal(STRAIGHT_KEY_TIMEOUT, NULL);
	}

	return;
}
#endif





/**
   \brief Inform the library that the straight key has changed state

   This routine returns CW_SUCCESS on success.  On error, it returns CW_FAILURE,
   with errno set to EBUSY if the tone queue or iambic keyer are using
   the sound card, console speaker, or keying control system.  If
   \p key_state indicates no change of state, the call is ignored.

   \p key_state may be either CW_KEY_STATE_OPEN (false) or CW_KEY_STATE_CLOSED (true).

   testedin::test_straight_key()

   \param key_state - state of straight key
*/
int cw_notify_straight_key_event(int key_state)
{
	/* If the tone queue or the keyer are busy, we can't use the
	   sound card, console sounder, or the key control system. */
	// if (cw_is_tone_busy() || cw_is_keyer_busy()) {
	if (0) {
		errno = EBUSY;
		return CW_FAILURE;
	}

	/* If the key state did not change, ignore the call. */
	if (cw_sk_key_state != key_state) {

		/* Save the new key state. */
		cw_sk_key_state = key_state;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STRAIGHT_KEY_STATES, CW_DEBUG_INFO,
			      "libcw: straight key state -> %s", cw_sk_key_state == CW_KEY_STATE_CLOSED ? "DOWN" : "UP");

		/* Do tones and keying, and set up timeouts and soundcard
		   activities to match the new key state. */
		if (cw_sk_key_state == CW_KEY_STATE_CLOSED) {
			cw_key_straight_key_generate_internal((*cw_generator), CW_KEY_STATE_CLOSED);
		} else {
			cw_key_straight_key_generate_internal((*cw_generator), CW_KEY_STATE_OPEN);

			/* Indicate that we have finished with timeouts,
			   and also with the soundcard too.  There's no way
			   of knowing when straight keying is completed,
			   so the only thing we can do here is to schedule
			   release on each key up event.   */
			//cw_finalization_schedule_internal();
		}
	}

	return CW_SUCCESS;
}





/**
   \brief Get saved state of straight key

   Returns the current saved state of the straight key.

   testedin::test_straight_key()

   \return CW_KEY_STATE_CLOSED (true) if the key is down
   \return CW_KEY_STATE_OPEN (false) if the key up
*/
int cw_get_straight_key_state(void)
{
	return cw_sk_key_state;
}





/**
   \brief Check if the straight key is busy

   This routine is just a pseudonym for cw_get_straight_key_state(),
   and exists to fill a hole in the API naming conventions.

   testedin::test_straight_key()

   \return true if the straight key is busy
   \return false if the straight key is not busy
*/
bool cw_is_straight_key_busy(void)
{
	return cw_sk_key_state;
}





/**
   \brief Clear the straight key state, and return to silence

   This function is suitable for calling from an application exit handler.
*/
void cw_reset_straight_key(void)
{
	cw_sk_key_state = CW_KEY_STATE_OPEN;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_generator_silence_internal((*cw_generator));
	//cw_finalization_schedule_internal();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_STRAIGHT_KEY_STATES, CW_DEBUG_INFO,
		      "libcw: straight key state ->UP (reset)");

	return;
}