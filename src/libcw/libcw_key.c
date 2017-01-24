/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)

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

   \brief Straight key and keyer.
*/





#include <stdbool.h>
#include <inttypes.h> /* uint32_t */
#include <errno.h>
#include <unistd.h>   /* usleep() */
#include <sys/time.h>


#include "libcw_debug.h"
#include "libcw_key.h"
#include "libcw_gen.h"
#include "libcw_rec.h"
#include "libcw_utils.h"





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;





/* KS stands for Keyer State. */
enum {
	KS_IDLE,
	KS_IN_DOT_A,
	KS_IN_DASH_A,
	KS_AFTER_DOT_A,
	KS_AFTER_DASH_A,
	KS_IN_DOT_B,
	KS_IN_DASH_B,
	KS_AFTER_DOT_B,
	KS_AFTER_DASH_B
};





struct cw_key_struct {
	/* Straight key and iambic keyer needs a generator to produce
	   a sound on "Key Down" events. Maybe we don't always need a
	   sound, but sometimes we do want to have it.

	   Additionally iambic keyer needs a generator for timing
	   purposes. Even if we came up with a different mechanism for
	   timing the key, we still would need to use generator to
	   produce a sound - then we would have a kind of
	   duplication. So let's always use a generator. Sometimes for
	   timing iambic keyer, sometimes for generating sound, but
	   always the same generator.

	   In any case - a key needs to have access to a generator
	   (but a generator doesn't need a key). This is why the key
	   data type has a "generator" field, not the other way
	   around. */
	cw_gen_t *gen;


	/* There should be a binding between key and a receiver.

	   The receiver can get it's properly formed input data (key
	   down/key up events) from any source, so it's independent
	   from key. On the other hand the key without receiver is
	   rather useless. Therefore I think that the key should
	   contain reference to a receiver, not the other way
	   around.

	   There may be one purpose of having a key without libcw
	   receiver: iambic keyer mechanism may be used to ensure a
	   functioning iambic keyer, but there may be a
	   different/external/3-rd party receiver that is
	   controlled/fed by cw_key_t->key_callback_func function. */
	cw_rec_t *rec;

	/* External "on key state change" callback function and its
	   argument.

	   It may be useful for a client to have this library control
	   an external keying device, for example, an oscillator, or a
	   transmitter.  Here is where we keep the address of a
	   function that is passed to us for this purpose, and a void*
	   argument for it. */
	cw_key_callback_t key_callback_func;
	void *key_callback_arg;

	/* Straight key. */
	struct {
		int key_value;    /* Open/Closed, Space/Mark, NoSound/Sound. */
	} sk;


	/* Iambic keyer.  The keyer functions maintain the current
	   known state of the paddles, and latch false-to-true
	   transitions while busy, to form the iambic effect.  For
	   Curtis mode B, the keyer also latches any point where both
	   paddle states are true at the same time. */
	struct {
		int graph_state;       /* State of iambic keyer state machine. */
		int key_value;         /* Open/Closed, Space/Mark, NoSound/Sound. */

		bool dot_paddle;       /* Dot paddle state */
		bool dash_paddle;      /* Dash paddle state */

		bool dot_latch;        /* Dot false->true latch */
		bool dash_latch;       /* Dash false->true latch */

		/* Iambic keyer "Curtis" mode A/B selector.  Mode A and mode B timings
		   differ slightly, and some people have a preference for one or the other.
		   Mode A is a bit less timing-critical, so we'll make that the default. */
		bool curtis_mode_b;

		bool curtis_b_latch;   /* Curtis Dot&Dash latch */

		bool lock;             /* FIXME: describe why we need this flag. */

		/* Generator associated with the keyer. Should never
		   be NULL as iambic keyer *needs* a generator to
		   function properly (and to generate audible tones).
		   Set using
		   cw_key_register_generator_internal(). */
		/* No separate generator, use cw_key_t->gen. */
	} ik;

	/* Tone-queue key. */
	struct {
		int key_value;    /* Open/Closed, Space/Mark, NoSound/Sound. */
	} tk;

	/* Every key event needs to have a timestamp. */
	struct timeval timer;
};





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





/* See also enum of int values, declared in libcw_key.h. */
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





static int cw_key_ik_update_state_initial_internal(cw_key_t *key);

static int cw_key_ik_enqueue_symbol_internal(cw_key_t *key, int key_value, char symbol);
static int cw_key_sk_enqueue_symbol_internal(cw_key_t *key, int key_value);





/* ******************************************************************** */
/*                       Section:Keying control                         */
/* ******************************************************************** */





/**
   \brief Register external callback function for keying

   Register a \p callback_func function that should be called when a
   state of a \p key changes from "key open" to "key closed", or
   vice-versa.

   The first argument passed to the registered callback function is the
   supplied \p callback_arg, if any.  The second argument passed to
   registered callback function is the key state: CW_KEY_STATE_CLOSED
   (one/true) for "key closed", and CW_KEY_STATE_OPEN (zero/false) for
   "key open".

   Calling this routine with a NULL function address disables keying
   callbacks.

   \param key
   \param callback_func - callback function to be called on key state changes
   \param callback_arg - first argument to callback_func
*/
void cw_key_register_keying_callback(cw_key_t *key,
				     cw_key_callback_t callback_func,
				     void *callback_arg)
{
	key->key_callback_func = callback_func;
	key->key_callback_arg = callback_arg;

	return;
}






/**
   \brief Set new value of key

   Set new value of a key. Filter successive key-down or key-up
   actions into a single action (successive calls with the same value
   of \p key_state don't change internally registered value of key).

   If and only if the function registers change of key value, an
   external callback function for keying (if configured) is called.

   Notice that the function is used only in
   cw_tq_dequeue_internal(). A generator which owns a tone
   queue is treated as a key, and dequeued tones are treated as key
   values. Dequeueing tones is treated as manipulating a key.

   \param key - key to use
   \param key_value - key value to be set
*/
void cw_key_tk_set_value_internal(cw_key_t *key, int key_value)
{
	cw_assert (key, "key is NULL");

	if (key->tk.key_value != key_value) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw/qk: key value: %d->%d", key->tk.key_value, key_value);

		/* Remember the new key value. */
		key->tk.key_value = key_value;

		/* In theory client code should register either a
		   receiver (so events from key are passed to receiver
		   directly), or a callback (so events from key are
		   passed to receiver through callback).

		   So *in theory* only one of these "if" blocks will
		   be executed. */

		if (key->rec) {
			if (key->tk.key_value) {
				/* Key down. */
				cw_rec_mark_begin(key->rec, &key->timer);
			} else {
				/* Key up. */
				cw_rec_mark_end(key->rec, &key->timer);
			}
		}

		if (key->key_callback_func) {
			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYING, CW_DEBUG_INFO,
				      "libcw/qk: ====== about to call callback, key value = %d\n", key->tk.key_value);

			(*(key->key_callback_func))(&key->timer, key->tk.key_value, key->key_callback_arg);
		}

		return;
	} else {
		/* This is not an error. This may happen when
		   dequeueing 'forever' tone multiple times in a
		   row. */
		return;
	}
}





/*
  Comment for key used as iambic keyer:
  Iambic keyer cannot function without an associated generator. A
  keyer has to have some generator to function correctly. Generator
  doesn't care if it has any key registered or not. Thus a function
  binding a keyer and generator belongs to "iambic keyer" module.

  Remember that a generator can exist without a keyer. In applications
  that do nothing related to keying with iambic keyer, having just a
  generator is a valid situation.



  \param key - key that needs to have a generator associated with it
  \param gen - generator to be used with given keyer
*/
void cw_key_register_generator(cw_key_t *key, cw_gen_t *gen)
{
	/* General key. */
	key->gen = gen;
	gen->key = key;

	return;
}





/**
   Receiver should somehow receive key events from physical or logical
   key. This can be done in one of two ways:

   1. key events -> key variable -> cw_key_tk_set_value_internal() ->
      -> cw_rec_mark_{begin|end}(key->rec, ...)
   2. key events -> key variable -> cw_key_tk_set_value_internal() ->
      -> key->key_callback_function() -> cw_rec_mark_{begin|end}(rec, ...)


   There should be a binding between key and a receiver.

   The receiver can get it's properly formed input data (key down/key
   up events) from any source, so it's independent from key. On the
   other hand the key without receiver is rather useless. Therefore I
   think that the key should contain reference to a receiver, not the
   other way around.

  \param key - key that needs to have a receiver associated with it
  \param rec - receiver to be used with given key
*/
void cw_key_register_receiver(cw_key_t *key, cw_rec_t *rec)
{
	key->rec = rec;

	return;
}





/**
   \brief Set new key value, generate appropriate tone (Mark/Space)

   Set new value of a key. Filter successive key-down or key-up
   actions into a single action (successive calls with the same value
   of \p key_value don't change internally registered value of key).

   If and only if the function registers change of key value, an
   external callback function for keying (if configured) is called.

   If and only if the function registers change of key value, a state
   of related generator \p gen is changed accordingly (a tone is
   started or stopped).

   \param key - key in use
   \param key_state - key state to be set

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_sk_enqueue_symbol_internal(cw_key_t *key, int key_value)
{
	cw_assert (key, "key is NULL");
	cw_assert (key->gen, "generator is NULL");

	gettimeofday(&key->timer, NULL);

	if (key->sk.key_value != key_value) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw/sk: key value %d->%d", key->sk.key_value, key_value);

		/* Remember the new key value. */
		key->sk.key_value = key_value;

		/* Call a registered callback. */
		if (key->key_callback_func) {
			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYING, CW_DEBUG_INFO,
				      "libcw/sk: ++++++ about to call callback, key value = %d\n", key_value);

			(*(key->key_callback_func))(&key->timer, key->sk.key_value, key->key_callback_arg);
		}

		int rv;
		if (key->sk.key_value == CW_KEY_STATE_CLOSED) {
			/* In case of straight key we don't know at
			   all how long the tone should be (we don't
			   know for how long the key will be closed.

			   Let's enqueue a beginning of mark. A
			   constant tone will be generated until function
			   receives CW_KEY_STATE_OPEN key state. */
			rv = cw_gen_enqueue_begin_mark_internal(key->gen);
		} else {
			/* CW_KEY_STATE_OPEN, time to go from Mark
			   (audible tone) to Space (silence). */
			rv = cw_gen_enqueue_begin_space_internal(key->gen);
		}
		cw_assert (rv, "failed to key key value %d", key->sk.key_value);
		return rv;
	} else {
		/* This may happen when dequeueing 'forever' tone
		   multiple times in a row. */
		return CW_SUCCESS;
	}
}





/**
   \brief Enqueue a symbol (Mark/Space) in generator's queue

   This function is called when keyer enters new graph state (as
   described by keyer's state graph). The keyer needs some mechanism
   to control itself, to control when to move out of current graph
   state into next graph state. The movement between graph states must
   be done in specific time periods. Iambic keyer needs to be notified
   whenever a specific time period has elapsed.

   Lengths of the enqueued periods are determined by type of \p symbol
   (Space, Dot, Dash).

   Generator and its tone queue is used to implement this mechanism.
   The function enqueues a tone/symbol (Mark or Space) of specific
   length - this is the beginning of period when keyer is in new graph
   state. Then generator dequeues the tone/symbol, counts the time
   period, and (at the end of the tone/period) notifies keyer about
   end of period. (Keyer then needs to evaluate state of paddles and
   decide what's next, but that is a different story).

   As a side effect of using generator, a sound is generated (if
   generator's sound system is not Null).

   Function also calls external callback function for keying on every
   change of key's value (if the callback has been registered by
   client code). Key's value (Open/Closed) is passed to the callback
   as argument. Callback is called by this function only when there is
   a change of key value - this function filters successive key-down
   or key-up actions into a single action.

   TODO: explain difference and relation between key's value and
   keyer's graph state.

   \param key - current key
   \param key_value - key value to be set (Mark/Space)
   \param symbol - symbol to enqueue (Space, Dot, Dash)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_enqueue_symbol_internal(cw_key_t *key, int key_value, char symbol)
{
	cw_assert (key, "keyer is NULL");
	cw_assert (key->gen, "generator is NULL");

	if (key->ik.key_value != key_value) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw/ik: key value %d->%d", key->ik.key_value, key_value);

		/* Remember the new key value. */
		key->ik.key_value = key_value;

		/* Call a registered callback. */
		if (key->key_callback_func) {
			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYING, CW_DEBUG_INFO,
				      "libcw/ik: ------ about to call callback, key value = %d\n", key_value);

			(*(key->key_callback_func))(&key->timer, key->ik.key_value, key->key_callback_arg);
		}

		/* 'Pure' means without any end-of-mark spaces. */
		int rv = cw_gen_enqueue_partial_symbol_internal(key->gen, symbol);
		cw_assert (rv, "failed to key symbol '%c'", symbol);
		return rv;
	} else {
		/* This is not an error. This may happen when
		   dequeueing 'forever' tone multiple times in a
		   row. */
		return CW_SUCCESS;
	}
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

   \param key
*/
void cw_key_ik_enable_curtis_mode_b(cw_key_t *key)
{
	key->ik.curtis_mode_b = true;
	return;
}





/**
   See documentation of cw_key_ik_enable_curtis_mode_b() for more information

   \param key
*/
void cw_key_ik_disable_curtis_mode_b(cw_key_t *key)
{
	key->ik.curtis_mode_b = false;
	return;
}





/**
   See documentation of cw_enable_iambic_curtis_mode_b() for more information

   \param key

   \return true if Curtis mode is enabled for the key
   \return false otherwise
*/
bool cw_key_ik_get_curtis_mode_b(cw_key_t *key)
{
	return key->ik.curtis_mode_b;
}





/**
   \brief Update state of iambic keyer, queue tone representing state of the iambic keyer

   It seems that the function is called when a client code informs
   about change of state of one of paddles. So I think what the
   function does is that it takes the new state of paddles and
   re-evaluate internal state of iambic keyer.

   The function is also called in generator's thread function
   cw_generator_dequeue_and_generate_internal() each time a tone is
   dequeued and pushed to audio system. I don't know why make the call
   in that place for iambic keyer, but not for straight key.

   \param key - iambic key

   \return CW_FAILURE if there is a lock and the function cannot proceed
   \return CW_SUCCESS otherwise
*/
int cw_key_ik_update_graph_state_internal(cw_key_t *key)
{
	if (!key) {
		/* This function is called from generator thread. It
		   is perfectly valid situation that for some
		   applications a generator exists, but a keyer does
		   not exist.  Silently accept this situation.

		   TODO: move this check earlier in call stack, so
		   that less functions are called before silently
		   discovering that key doesn't exist.. */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_INTERNAL, CW_DEBUG_DEBUG,
			      "libcw/ik: NULL key, silently accepting");
		return CW_SUCCESS;
	}

	/* This function is called from generator thread function, so
	   the generator must exist. Be paranoid and check it, just in
	   case :) */
	cw_assert (key->gen, "generator is NULL");


	if (key->ik.lock) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_INTERNAL, CW_DEBUG_ERROR,
			      "libcw/ik: lock in thread %ld", (long) pthread_self());
		return CW_FAILURE;
	}
	key->ik.lock = true;

	/* Synchronize low level timing parameters if required. */
	if (key->gen) {
		cw_gen_sync_parameters_internal(key->gen);
	}
	if (key->rec) {
		cw_rec_sync_parameters_internal(key->rec);
	}

	int old_state = key->ik.graph_state;

	/* Decide what to do based on the current state. */
	switch (key->ik.graph_state) {
		/* Ignore calls if our state is idle. */
	case KS_IDLE:
		key->ik.lock = false;
		return CW_SUCCESS;


	case KS_IN_DOT_A:
	case KS_IN_DOT_B:
		/* Verify that key value and keyer graph state are in
		   sync.  We are *at the end* of Mark, so key should
		   be (still) closed. */
		cw_assert (key->ik.key_value == CW_KEY_STATE_CLOSED,
			   "inconsistency between keyer state (%s) ad key value (%d)",
			   cw_iambic_keyer_states[key->ik.graph_state], key->ik.key_value);

		/* We are ending a dot, so turn off tone and begin the
		   after-dot delay.
		   No routine status checks are made! (TODO) */
		cw_key_ik_enqueue_symbol_internal(key, CW_KEY_STATE_OPEN, CW_SYMBOL_SPACE);
		key->ik.graph_state = key->ik.graph_state == KS_IN_DOT_A
			? KS_AFTER_DOT_A : KS_AFTER_DOT_B;
		break;

	case KS_IN_DASH_A:
	case KS_IN_DASH_B:
		/* Verify that key value and keyer graph state are in
		   sync.  We are *at the end* of Mark, so key should
		   be (still) closed. */
		cw_assert (key->ik.key_value == CW_KEY_STATE_CLOSED,
			   "inconsistency between keyer state (%s) ad key value (%d)",
			   cw_iambic_keyer_states[key->ik.graph_state], key->ik.key_value);

		/* We are ending a dash, so turn off tone and begin
		   the after-dash delay.
		   No routine status checks are made! (TODO) */
		cw_key_ik_enqueue_symbol_internal(key, CW_KEY_STATE_OPEN, CW_SYMBOL_SPACE);
		key->ik.graph_state = key->ik.graph_state == KS_IN_DASH_A
			? KS_AFTER_DASH_A : KS_AFTER_DASH_B;

		break;

	case KS_AFTER_DOT_A:
	case KS_AFTER_DOT_B:
		/* Verify that key value and keyer graph state are in
		   sync.  We are *at the end* of Space, so key should
		   be (still) open. */
		cw_assert (key->ik.key_value == CW_KEY_STATE_OPEN,
			   "inconsistency between keyer state (%s) ad key value (%d)",
			   cw_iambic_keyer_states[key->ik.graph_state], key->ik.key_value);

		/* If we have just finished a dot or a dash and its
		   post-mark delay, then reset the latches as
		   appropriate.  Next, if in a _B state, go straight
		   to the opposite element state.  If in an _A state,
		   check the latch states; if the opposite latch is
		   set true, then do the iambic thing and alternate
		   dots and dashes.  If the same latch is true,
		   repeat.  And if nothing is true, then revert to
		   idling. */

		if (!key->ik.dot_paddle) {
			/* Client has informed us that dot paddle has
			   been released. Clear the paddle state
			   memory. */
			key->ik.dot_latch = false;
		}

		if (key->ik.graph_state == KS_AFTER_DOT_B) {
			cw_key_ik_enqueue_symbol_internal(key, CW_KEY_STATE_CLOSED, CW_DASH_REPRESENTATION);
			key->ik.graph_state = KS_IN_DASH_A;
		} else if (key->ik.dash_latch) {
			cw_key_ik_enqueue_symbol_internal(key, CW_KEY_STATE_CLOSED, CW_DASH_REPRESENTATION);
			if (key->ik.curtis_b_latch){
				key->ik.curtis_b_latch = false;
				key->ik.graph_state = KS_IN_DASH_B;
			} else {
				key->ik.graph_state = KS_IN_DASH_A;
			}
		} else if (key->ik.dot_latch) {
			cw_key_ik_enqueue_symbol_internal(key, CW_KEY_STATE_CLOSED, CW_DOT_REPRESENTATION);
			key->ik.graph_state = KS_IN_DOT_A;
		} else {
			key->ik.graph_state = KS_IDLE;
		}

		break;

	case KS_AFTER_DASH_A:
	case KS_AFTER_DASH_B:
		/* Verify that key value and keyer graph state are in
		   sync.  We are *at the end* of Space, so key should
		   be (still) open. */
		cw_assert (key->ik.key_value == CW_KEY_STATE_OPEN,
			   "inconsistency between keyer state (%s) ad key value (%d)",
			   cw_iambic_keyer_states[key->ik.graph_state], key->ik.key_value);

		if (!key->ik.dash_paddle) {
			/* Client has informed us that dash paddle has
			   been released. Clear the paddle state
			   memory. */
			key->ik.dash_latch = false;
		}

		/* If we have just finished a dot or a dash and its
		   post-mark delay, then reset the latches as
		   appropriate.  Next, if in a _B state, go straight
		   to the opposite element state.  If in an _A state,
		   check the latch states; if the opposite latch is
		   set true, then do the iambic thing and alternate
		   dots and dashes.  If the same latch is true,
		   repeat.  And if nothing is true, then revert to
		   idling. */

		if (key->ik.graph_state == KS_AFTER_DASH_B) {
			cw_key_ik_enqueue_symbol_internal(key, CW_KEY_STATE_CLOSED, CW_DOT_REPRESENTATION);
			key->ik.graph_state = KS_IN_DOT_A;
		} else if (key->ik.dot_latch) {
			cw_key_ik_enqueue_symbol_internal(key, CW_KEY_STATE_CLOSED, CW_DOT_REPRESENTATION);
			if (key->ik.curtis_b_latch) {
				key->ik.curtis_b_latch = false;
				key->ik.graph_state = KS_IN_DOT_B;
			} else {
				key->ik.graph_state = KS_IN_DOT_A;
			}
		} else if (key->ik.dash_latch) {
			cw_key_ik_enqueue_symbol_internal(key, CW_KEY_STATE_CLOSED, CW_DASH_REPRESENTATION);
			key->ik.graph_state = KS_IN_DASH_A;
		} else {
			key->ik.graph_state = KS_IDLE;
		}

		break;
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      "libcw/ik: keyer state: %s -> %s",
		      cw_iambic_keyer_states[old_state], cw_iambic_keyer_states[key->ik.graph_state]);

	key->ik.lock = false;
	return CW_SUCCESS;
}






/**
   \brief Inform iambic keyer logic about changed state of iambic keyer's paddles

   Function informs the library that the iambic keyer paddles have
   changed state.  The new paddle states are recorded, and if either
   transition from false to true, paddle latches (for iambic
   functions) are also set.

   On success, the routine returns CW_SUCCESS.
   On failure, it returns CW_FAILURE, with errno set to EBUSY if the
   tone queue or straight key are using the sound card, console
   speaker, or keying system.

   If appropriate, this routine starts the keyer functions sending the
   relevant element.  Element send and timing occurs in the background,
   so this routine returns almost immediately.  See cw_keyer_element_wait()
   and cw_keyer_wait() for details about how to check the current status of
   iambic keyer background processing.


   \param key
   \param dot_paddle_state
   \param dash_paddle_state

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_notify_paddle_event(cw_key_t *key, int dot_paddle_state, int dash_paddle_state)
{
#if 0 /* This is disabled, but I'm not sure why. */
	/* If the tone queue or the straight key are busy, this is going to
	   conflict with our use of the sound card, console sounder, and
	   keying system.  So return an error status in this case. */
	if (cw_tq_is_busy_internal(key->gen->tq) || cw_key_sk_is_busy_internal(key)) {
		errno = EBUSY;
		return CW_FAILURE;
	}
#endif

	/* Clean up and save the paddle states passed in. */
	key->ik.dot_paddle = (dot_paddle_state != 0);
	key->ik.dash_paddle = (dash_paddle_state != 0);

	/* Update the paddle latches if either paddle goes true.
	   The latches are checked in the signal handler, so if the paddles
	   go back to false during this element, the item still gets
	   actioned.  The signal handler is also responsible for clearing
	   down the latches. TODO: verify the comment. */
	if (key->ik.dot_paddle) {
		key->ik.dot_latch = true;
	}
	if (key->ik.dash_paddle) {
		key->ik.dash_latch = true;
	}

	/* If in Curtis mode B, make a special check for both paddles true
	   at the same time.  This flag is checked by the signal handler,
	   to determine whether to add mode B trailing timing elements. TODO: verify this comment. */
	if (key->ik.curtis_mode_b && key->ik.dot_paddle && key->ik.dash_paddle) {
		key->ik.curtis_b_latch = true;
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      "libcw/ik: keyer paddles %d,%d, latches %d,%d, curtis_b %d",
		      key->ik.dot_paddle, key->ik.dash_paddle,
		      key->ik.dot_latch, key->ik.dash_latch, key->ik.curtis_b_latch);


	if (key->ik.graph_state == KS_IDLE) {
		gettimeofday(&key->timer, NULL);

		/* If the current state is idle, give the state
		   process an initial impulse. */
		return cw_key_ik_update_state_initial_internal(key);
	} else {
		/* The state machine for iambic keyer is already in
		   motion, no need to do anything more.

		   Current paddle states have been recorded in this
		   function. Any future changes of paddle states will
		   be also recorded by this function.

		   In both cases the main action upon states of
		   paddles and paddle latches is taken in
		   cw_key_ik_update_graph_state_internal(). */
		return CW_SUCCESS;
	}
}





/**
   \brief Initiate work of iambic keyer state machine

   State machine for iambic keyer must be pushed from KS_IDLE
   state. Call this function to do this.

   \param key

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_update_state_initial_internal(cw_key_t *key)
{
	cw_assert (key, "keyer is NULL");
	cw_assert (key->gen, "generator is NULL");

	if (!key->ik.dot_paddle && !key->ik.dash_paddle) {
		/* Both paddles are open/up. We certainly don't want
		   to start any process upon "both paddles open"
		   event. But the function shouldn't have been called
		   in that situation. */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYER_STATES, CW_DEBUG_ERROR,
			      "libcw/ik: called update_state_initial() function when both paddles are up");

		/* Silently accept.
		   TODO: maybe it's a good idea, or maybe bad one to
		   return CW_SUCCESS here. */
		return CW_SUCCESS;
	}

	int old_state = key->ik.graph_state;

	if (key->ik.dot_paddle) {
		/* "Dot" paddle pressed. Pretend that we are in "after
		   dash" space, so that keyer will have to transit
		   into "dot" mark state. */
		key->ik.graph_state = key->ik.curtis_b_latch
			? KS_AFTER_DASH_B : KS_AFTER_DASH_A;

	} else { /* key->ik.dash_paddle */
		/* "Dash" paddle pressed. Pretend that we are in
		   "after dot" space, so that keyer will have to
		   transit into "dash" mark state. */

		key->ik.graph_state = key->ik.curtis_b_latch
			? KS_AFTER_DOT_B : KS_AFTER_DOT_A;
	}

	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
		      "libcw/ik: keyer state (init): %s -> %s",
		      cw_iambic_keyer_states[old_state], cw_iambic_keyer_states[key->ik.graph_state]);


	/* Here comes the "real" initial transition - this is why we
	   called this function. */
	int rv = cw_key_ik_update_graph_state_internal(key);
	if (rv == CW_FAILURE) {
		/* Just try again, once. */
		usleep(1000);
		rv = cw_key_ik_update_graph_state_internal(key);
		if (!rv) {
			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYER_STATES, CW_DEBUG_ERROR,
				      "libcw/ik: call to update_state_initial() failed");
		}
	}

	return rv;
}





/**
   \brief Change state of dot paddle

   Alter the state of just one of the two iambic keyer paddles.
   The other paddle state of the paddle pair remains unchanged.

   See cw_key_ik_notify_paddle_event() for details of iambic
   keyer background processing, and how to check its status.

   \param key
   \param dot_paddle_state

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_notify_dot_paddle_event(cw_key_t *key, int dot_paddle_state)
{
	return cw_key_ik_notify_paddle_event(key, dot_paddle_state, key->ik.dash_paddle);
}





/**
   See documentation of cw_notify_keyer_dot_paddle_event() for more information

   \param key
   \param dash_paddle_state

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_notify_dash_paddle_event(cw_key_t *key, int dash_paddle_state)
{
	return cw_key_ik_notify_paddle_event(key, key->ik.dot_paddle, dash_paddle_state);
}






/**
   \brief Get the current saved states of the two paddles

   \param key
   \param dot_paddle_state
   \param dash_paddle_state
*/
void cw_key_ik_get_paddles(cw_key_t *key, int *dot_paddle_state, int *dash_paddle_state)
{
	if (dot_paddle_state) {
		*dot_paddle_state = key->ik.dot_paddle;
	}
	if (dash_paddle_state) {
		*dash_paddle_state = key->ik.dash_paddle;
	}
	return;
}






/**
   \brief Get the current states of paddle latches

   Function returns the current saved states of the two paddle latches.
   A paddle latches is set to true when the paddle state becomes true,
   and is cleared if the paddle state is false when the element finishes
   sending.

   \param key
   \param dot_paddle_latch_state
   \param dash_paddle_latch_state
*/
void cw_key_ik_get_paddle_latches_internal(cw_key_t *key, int *dot_paddle_latch_state, int *dash_paddle_latch_state)
{
	if (dot_paddle_latch_state) {
		*dot_paddle_latch_state = key->ik.dot_latch;
	}
	if (dash_paddle_latch_state) {
		*dash_paddle_latch_state = key->ik.dash_latch;
	}
	return;
}





/**
   \brief Check if a keyer is busy

   \param key

   \return true if keyer is busy
   \return false if keyer is not busy
*/
bool cw_key_ik_is_busy_internal(cw_key_t *key)
{
	return key->ik.graph_state != KS_IDLE;
}





/**
   \brief Wait for end of element from the keyer

   Waits until the end of the current element, dot or dash, from the keyer.

   The function always returns CW_SUCCESS/

   testedin::test_keyer()

   \param key

   \return CW_SUCCESS
*/
int cw_key_ik_wait_for_element(cw_key_t *key)
{
	/* First wait for the state to move to idle (or just do nothing
	   if it's not), or to one of the after- states. */
	pthread_mutex_lock(&key->gen->tq->wait_mutex);
	while (key->ik.graph_state != KS_IDLE
	       && key->ik.graph_state != KS_AFTER_DOT_A
	       && key->ik.graph_state != KS_AFTER_DOT_B
	       && key->ik.graph_state != KS_AFTER_DASH_A
	       && key->ik.graph_state != KS_AFTER_DASH_B) {

		pthread_cond_wait(&key->gen->tq->wait_var, &key->gen->tq->wait_mutex);
		/* cw_signal_wait_internal(); */ /* Old implementation was using signals. */
	}
	pthread_mutex_unlock(&key->gen->tq->wait_mutex);


	/* Now wait for the state to move to idle (unless it is, or was,
	   already), or one of the in- states, at which point we know
	   we're actually at the end of the element we were in when we
	   entered this routine. */
	pthread_mutex_lock(&key->gen->tq->wait_mutex);
	while (key->ik.graph_state != KS_IDLE
	       && key->ik.graph_state != KS_IN_DOT_A
	       && key->ik.graph_state != KS_IN_DOT_B
	       && key->ik.graph_state != KS_IN_DASH_A
	       && key->ik.graph_state != KS_IN_DASH_B) {

		pthread_cond_wait(&key->gen->tq->wait_var, &key->gen->tq->wait_mutex);
		/* cw_signal_wait_internal(); */ /* Old implementation was using signals. */
	}
	pthread_mutex_unlock(&key->gen->tq->wait_mutex);

	return CW_SUCCESS;
}





/**
   \brief Wait for the current keyer cycle to complete

   The routine returns CW_SUCCESS on success.

   It returns CW_FAILURE (with errno set to EDEADLK) if either paddle
   state is true.

   \param key

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_wait_for_keyer(cw_key_t *key)
{
	/* Check that neither paddle is true; if either is, then the signal
	   cycle is going to continue forever, and we'll never return from
	   this routine. TODO: verify this comment. */
	if (key->ik.dot_paddle || key->ik.dash_paddle) {
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait for the keyer state to go idle. */
	pthread_mutex_lock(&key->gen->tq->wait_mutex);
	while (key->ik.graph_state != KS_IDLE) {
		pthread_cond_wait(&key->gen->tq->wait_var, &key->gen->tq->wait_mutex);
		/* cw_signal_wait_internal(); */ /* Old implementation was using signals. */
	}
	pthread_mutex_unlock(&key->gen->tq->wait_mutex);

	return CW_SUCCESS;
}





/**
   \brief Reset iambic keyer data

   Clear all latches and paddle states of iambic keyer, return to
   Curtis 8044 Keyer mode A, and return to silence.  This function is
   suitable for calling from an application exit handler.

   \param key
*/
void cw_key_ik_reset_internal(cw_key_t *key)
{
	key->ik.dot_paddle = false;
	key->ik.dash_paddle = false;
	key->ik.dot_latch = false;
	key->ik.dash_latch = false;
	key->ik.curtis_b_latch = false;
	key->ik.curtis_mode_b = false;

	cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
		      "libcw/ik: keyer state %s -> KS_IDLE", cw_iambic_keyer_states[key->ik.graph_state]);
	key->ik.graph_state = KS_IDLE;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_gen_silence_internal(key->gen);

	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
		      "libcw/ik: keyer state -> %s (reset)", cw_iambic_keyer_states[key->ik.graph_state]);

	return;
}





/**
   Iambic keyer has an internal timer variable. On some occasions the
   variable needs to be updated.

   I thought that it needs to be updated by client application on key
   paddle events, but it turns out that it should be also updated in
   generator dequeue code. Not sure why.

   \param key - keyer with timer to be updated
   \param usecs - amount of increase (usually length of a tone)
*/
void cw_key_ik_increment_timer_internal(cw_key_t *key, int usecs)
{
	if (!key) {
		/* This function is called from generator thread. It
		   is perfectly valid situation that for some
		   applications a generator exists, but a keyer does
		   not exist.  Silently accept this situation. */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_INTERNAL, CW_DEBUG_DEBUG,
			      "libcw/ik: NULL key, silently accepting");
		return;
	}

	if (key->ik.graph_state != KS_IDLE) {
		/* Update timestamp that clocks iambic keyer
		   with current time interval. This must be
		   done only when iambic keyer is in
		   use. Calling the code when straight key is
		   in use will cause problems, so don't clock
		   a straight key with this. */

		cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw/ik: incrementing timer by %d [us]\n", usecs);

		key->timer.tv_usec += usecs % CW_USECS_PER_SEC;
		key->timer.tv_sec  += usecs / CW_USECS_PER_SEC + key->timer.tv_usec / CW_USECS_PER_SEC;
		key->timer.tv_usec %= CW_USECS_PER_SEC;
	}

	return;
}





/* ******************************************************************** */
/*                        Section:Straight key                          */
/* ******************************************************************** */





/**
   \brief Inform the library that the straight key has changed state

   This routine returns CW_SUCCESS on success.  On error, it returns CW_FAILURE,
   with errno set to EBUSY if the tone queue or iambic keyer are using
   the sound card, console speaker, or keying control system.  If
   \p key_state indicates no change of state, the call is ignored.

   \p key_state may be either CW_KEY_STATE_OPEN (false) or CW_KEY_STATE_CLOSED (true).

   \param key
   \param key_state - state of straight key

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_sk_notify_event(cw_key_t *key, int key_state)
{
#if 0 /* This is disabled, but I'm not sure why. */
	/* If the tone queue or the keyer are busy, we can't use the
	   sound card, console sounder, or the key control system. */
	if (cw_tq_is_busy_internal(key->gen->tq) || cw_key_ik_is_busy_internal(key)) {
		errno = EBUSY;
		return CW_FAILURE;
	}
#endif

	/* Do tones and keying, and set up timeouts and soundcard
	   activities to match the new key state. */
	int rv = cw_key_sk_enqueue_symbol_internal(key, key_state);

	return rv;
}





/**
   \brief Get saved state of straight key

   Returns the current saved state of the straight key.

   \param key

   \return CW_KEY_STATE_CLOSED (true) if the key is down
   \return CW_KEY_STATE_OPEN (false) if the key up
*/
int cw_key_sk_get_state(cw_key_t *key)
{
	return key->sk.key_value;
}





/**
   \brief Check if the straight key is busy

   This routine is just a pseudonym for
   cw_key_sk_get_state(), and exists to fill a hole in the
   API naming conventions. TODO: verify if this function is needed in
   new API.

   \param key

   \return true if the straight key is busy
   \return false if the straight key is not busy
*/
bool cw_key_sk_is_busy(cw_key_t *key)
{
	return key->sk.key_value;
}





/**
   \brief Clear the straight key state, and return to silence

   This function is suitable for calling from an application exit handler.

   \param key
*/
void cw_key_sk_reset_internal(cw_key_t *key)
{
	key->sk.key_value = CW_KEY_STATE_OPEN;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_gen_silence_internal(key->gen);

	cw_debug_msg (&cw_debug_object, CW_DEBUG_STRAIGHT_KEY_STATES, CW_DEBUG_INFO,
		      "libcw/sk: key state ->UP (reset)");

	return;
}





cw_key_t *cw_key_new(void)
{
	cw_key_t *key = (cw_key_t *) malloc(sizeof (cw_key_t));
	if (!key) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: malloc()");
		return (cw_key_t *) NULL;
	}

	key->gen = (cw_gen_t *) NULL;
	key->rec = (cw_rec_t *) NULL;

	key->key_callback_func = NULL;
	key->key_callback_arg = NULL;

	key->sk.key_value = CW_KEY_STATE_OPEN;

	key->ik.graph_state = KS_IDLE;
	key->ik.key_value = 0;

	key->ik.dot_paddle = false;
	key->ik.dash_paddle = false;
	key->ik.dot_latch = false;
	key->ik.dash_latch = false;

	key->ik.curtis_mode_b = false;
	key->ik.curtis_b_latch = false;

	key->ik.lock = false;

	key->timer.tv_sec = 0;
	key->timer.tv_usec = 0;

	key->tk.key_value = CW_KEY_STATE_OPEN;

	return key;
}






void cw_key_delete(cw_key_t **key)
{
	cw_assert (key, "key is NULL");

	if (!*key) {
		return;
	}

	if ((*key)->gen) {
		/* Unregister. */
		(*key)->gen->key = NULL;
	}

	free(*key);
	*key = (cw_key_t *) NULL;

	return;
}




/* *** Unit tests *** */




#ifdef LIBCW_UNIT_TESTS

#include "libcw_test.h"




/**
   tests::cw_key_ik_notify_paddle_event()
   tests::cw_key_ik_wait_for_element()
   tests::cw_key_ik_get_paddles()
*/
unsigned int test_keyer(cw_key_t * key, cw_test_stats_t * stats)
{
	int p = fprintf(out_file, "libcw:key: iambic keyer operation:\n");
	fflush(out_file);

	/* Perform some tests on the iambic keyer.  The latch finer
	   timing points are not tested here, just the basics - dots,
	   dashes, and alternating dots and dashes. */

	int dot_paddle, dash_paddle;

	/* Test: keying dot. */
	{
		/* Seems like this function calls means "keyer pressed
		   until further notice". First argument is true, so
		   this is a dot. */
		bool failure = !cw_key_ik_notify_paddle_event(key, true, false);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:key: cw_key_ik_notify_paddle_event(key, true, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dot" paddle is pressed, get 30 "dot"
		   events from the keyer. */
		fprintf(out_file, "libcw:key: testing iambic keyer dots   ");
		fflush(out_file);
		for (int i = 0; i < 30; i++) {
			success = success && cw_key_ik_wait_for_element(key);
			putchar('.');
			fflush(out_file);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:key: cw_key_ik_wait_for_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_key_ik_get_paddles(key, &dot_paddle, &dash_paddle);
		bool failure = !dot_paddle || dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:key: cw_keyer_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: keying dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Second
		   argument is true, so this is a dash. */

		bool failure = !cw_key_ik_notify_paddle_event(key, false, true);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:key: cw_key_ik_notify_paddle_event(key, false, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dash" paddle is pressed, get 30 "dash"
		   events from the keyer. */
		fprintf(out_file, "libcw:key: testing iambic keyer dashes ");
		fflush(out_file);
		for (int i = 0; i < 30; i++) {
			success = success && cw_key_ik_wait_for_element(key);
			putchar('-');
			fflush(out_file);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:key: cw_key_ik_wait_for_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_key_ik_get_paddles(key, &dot_paddle, &dash_paddle);
		bool failure = dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:key: cw_key_ik_get_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: keying alternate dit/dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Both
		   arguments are true, so both paddles are pressed at
		   the same time.*/
		bool failure = !cw_key_ik_notify_paddle_event(key, true, true);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:key: cw_key_ik_notify_paddle_event(true, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		bool success = true;
		fprintf(out_file, "libcw:key: testing iambic alternating  ");
		fflush(out_file);
		for (int i = 0; i < 30; i++) {
			success = success && cw_key_ik_wait_for_element(key);
			putchar('#');
			fflush(out_file);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:key: cw_key_ik_wait_for_element:");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_key_ik_get_paddles(key, &dot_paddle, &dash_paddle);
		bool failure = !dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:key: cw_key_ik_get_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: set new state of paddles: no paddle pressed. */
	{
		bool failure = !cw_key_ik_notify_paddle_event(key, false, false);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:key: cw_key_ik_notify_paddle_event(false, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_key_ik_wait_for_keyer(key);

	p = fprintf(out_file, "libcw:key: iambic keyer operation:");
	CW_TEST_PRINT_TEST_RESULT(false, p);
	fflush(out_file);

	return 0;
}





/**
   tests::cw_key_sk_notify_event()
   tests::cw_key_sk_get_state()
   tests::cw_key_sk_is_busy()
*/
unsigned int test_straight_key(cw_key_t * key, cw_test_stats_t * stats)
{
	int p = fprintf(out_file, "libcw:key: straight key operation:\n");
	fflush(out_file);

	/* See what happens when we tell the library N times in a row that key is open. */
	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		for (int i = 0; i < 10; i++) {
			if (CW_SUCCESS != cw_key_sk_notify_event(key, CW_KEY_STATE_OPEN)) {
				event_failure = true;
				break;
			}

			if (CW_KEY_STATE_OPEN != cw_key_sk_get_state(key)) {
				state_failure = true;
				break;
			}

			if (cw_key_sk_is_busy(key)) {
				busy_failure = true;
				break;
			}
		}

		event_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:key: cw_key_sk_notify_event(<key open>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:key: cw_key_sk_get_state(<key open>):");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:key: cw_straight_key_busy(<key open>):");
		CW_TEST_PRINT_TEST_RESULT (busy_failure, n);
	}



	/* See what happens when we tell the library N times in a row that key is closed. */
	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		/* Again not sure why we have N identical calls in a
		   row. TODO: why? */
		for (int i = 0; i < 10; i++) {
			if (CW_SUCCESS != cw_key_sk_notify_event(key, CW_KEY_STATE_CLOSED)) {
				event_failure = true;
				break;
			}

			if (CW_KEY_STATE_CLOSED != cw_key_sk_get_state(key)) {
				state_failure = true;
				break;
			}

			if (!cw_key_sk_is_busy(key)) {
				busy_failure = true;
				break;
			}
		}


		event_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:key: cw_key_sk_notify_event(<key closed>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:key: cw_key_sk_get_state(<key closed>):");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:key: cw_straight_key_busy(<key closed>):");
		CW_TEST_PRINT_TEST_RESULT (busy_failure, n);
	}



	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		struct timespec t;
		int usecs = CW_USECS_PER_SEC;
		cw_usecs_to_timespec_internal(&t, usecs);


		/* Alternate between open and closed. */
		for (int i = 0; i < 5; i++) {
			if (CW_SUCCESS != cw_key_sk_notify_event(key, CW_KEY_STATE_OPEN)) {
				event_failure = true;
				break;
			}

			if (CW_KEY_STATE_OPEN != cw_key_sk_get_state(key)) {
				state_failure = true;
				break;
			}

			if (cw_key_sk_is_busy(key)) {
				busy_failure = true;
				break;
			}

			fprintf(out_file, "%d", CW_KEY_STATE_OPEN);
			fflush(out_file);
#ifdef __FreeBSD__
			/* There is a problem with nanosleep() and
			   signals on FreeBSD. */
			sleep(1);
#else
			cw_nanosleep_internal(&t);
#endif

			if (CW_SUCCESS != cw_key_sk_notify_event(key, CW_KEY_STATE_CLOSED)) {
				event_failure = true;
				break;
			}

			if (CW_KEY_STATE_CLOSED != cw_key_sk_get_state(key)) {
				state_failure = true;
				break;
			}

			if (!cw_key_sk_is_busy(key)) {
				busy_failure = true;
				break;
			}

			fprintf(out_file, "%d", CW_KEY_STATE_CLOSED);
			fflush(stdout);
#ifdef __FreeBSD__
			/* There is a problem with nanosleep() and
			   signals on FreeBSD. */
			sleep(1);
#else
			cw_nanosleep_internal(&t);
#endif
		}

		/* Whatever happens, don't leave the key closed. */
		cw_key_sk_notify_event(key, CW_KEY_STATE_OPEN);

		fprintf(out_file, "\n");
		fflush(out_file);

		event_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw: cw_key_sk_notify_event(<key open/closed>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw: cw_key_sk_get_state(<key open/closed>):");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw: cw_straight_key_busy(<key open/closed>):");
		CW_TEST_PRINT_TEST_RESULT (busy_failure, n);
	}


	p = fprintf(out_file, "libcw:key: straight key operation:");
	CW_TEST_PRINT_TEST_RESULT(false, p);
	fflush(out_file);

	return 0;
}




#endif /* #ifdef LIBCW_UNIT_TESTS */
