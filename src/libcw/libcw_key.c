/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)

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

   \brief Straight key and iambic keyer.
*/





#include <inttypes.h> /* uint32_t */
#include <errno.h>
#include <unistd.h>   /* usleep() */
#include <stdbool.h>
#include <sys/time.h>
#include <stdlib.h>




#include "libcw_debug.h"
#include "libcw_key.h"
#include "libcw_gen.h"
#include "libcw_rec.h"
#include "libcw_signal.h"
#include "libcw_utils.h"
#include "libcw2.h"




#define MSG_PREFIX_QK "libcw/qk: "
#define MSG_PREFIX_SK "libcw/sk: "
#define MSG_PREFIX_IK "libcw/ik: "
#define MSG_PREFIX "libcw/key: "




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




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




static int cw_key_ik_update_state_initial_internal(volatile cw_key_t * key);
static int cw_key_ik_set_value_internal(volatile cw_key_t * key, int key_state, char symbol);
static int cw_key_sk_set_value_internal(volatile cw_key_t * key, int key_state);




/* ******************************************************************** */
/*                       Section:Keying control                         */
/* ******************************************************************** */




/**
   \brief Register external callback function for keying

   Register a \p callback_func function that should be called when a
   state of a \p key changes from "key open" to "key closed", or
   vice-versa.

   The first argument passed to the registered callback is key's timer.

   The second argument passed to the registered callback function is
   the supplied \p callback_arg, if any.

   The third argument passed to registered callback function is the
   key state: CW_KEY_STATE_CLOSED (one/true) for "key closed", and
   CW_KEY_STATE_OPEN (zero/false) for "key open".

   Calling this routine with a NULL function address disables keying
   callbacks.  Any callback supplied will be called in signal handler
   context (TODO ??).

   \param key
   \param callback_func - callback function to be called on key state changes
   \param callback_arg - first argument to callback_func
*/
void cw_key_register_keying_callback(volatile cw_key_t * key, cw_key_callback_t callback_func, void * callback_arg)
{
	key->key_callback_func = callback_func;
	key->key_callback_arg = callback_arg;

	return;
}




/**
   \brief Register external callback function for keying

   Function to be used in implementation of legacy API.

   Register a \p callback_func function that should be called when a
   state of a \p key changes from "key open" to "key closed", or
   vice-versa.

   The first argument passed to the registered callback function is
   the supplied \p callback_arg, if any.

   The second argument passed to registered callback function is the
   key state: CW_KEY_STATE_CLOSED (one/true) for "key closed", and
   CW_KEY_STATE_OPEN (zero/false) for "key open".

   Calling this routine with a NULL function address disables keying
   callbacks.

   \param key
   \param callback_func - callback function to be called on key state changes
   \param callback_arg - first argument to callback_func
*/
void cw_key_register_legacy_keying_callback_internal(volatile cw_key_t * key, cw_key_legacy_callback_t callback_func, void * callback_arg)
{
	key->key_legacy_callback_func = callback_func;
	key->key_legacy_callback_arg = callback_arg;

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
   \param key_state - key state to be set
*/
void cw_key_tk_set_value_internal(volatile cw_key_t *key, int key_state)
{
	cw_assert (key, MSG_PREFIX "tk set value: key is NULL");

	if (key->tk.key_value == key_state) {
		/* This is not an error. This may happen when
		   dequeueing 'forever' tone multiple times in a
		   row. */
		return;
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYING, CW_DEBUG_INFO,
		      MSG_PREFIX "tk set value: %d->%d", key->tk.key_value, key_state);

	/* Remember the new key value. */
	key->tk.key_value = key_state;

	/* In theory client code should register either a receiver (so
	   events from key are passed to receiver directly), or a
	   callback (so events from key are passed to receiver through
	   callback).

	   So *in theory* only one of these "if" blocks will be
	   executed. */

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
			      MSG_PREFIX "tk set value: about to call callback, key state = %d\n", key->tk.key_value);

		(*key->key_callback_func)(&key->timer, key->tk.key_value, key->key_callback_arg);
	}
	if (key->key_legacy_callback_func) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      MSG_PREFIX "tk set value: about to call legacy callback, key state = %d\n", key->tk.key_value);

		(*key->key_legacy_callback_func)(key->key_legacy_callback_arg, key->tk.key_value);
	}

	return;
}




/**
   Comment for key used as iambic keyer:
   Iambic keyer cannot function without an associated generator. A
   keyer has to have some generator to function correctly. Generator
   doesn't care if it has any key registered or not. Thus a function
   binding a keyer and generator belongs to "iambic keyer" module.

   Remember that a generator can exist without a keyer. In applications
   that do nothing related to keying with iambic keyer, having just a
   generator is a valid situation.

   \reviewed on 2017-01-31

   \param key - key that needs to have a generator associated with it
   \param gen - generator to be used with given keyer
*/
void cw_key_register_generator(volatile cw_key_t * key, cw_gen_t * gen)
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
      -> registered receiver -> cw_rec_mark_{begin|end}(key->rec, ...)
   2. key events -> key variable -> cw_key_tk_set_value_internal() ->
      -> registered callback function-> key->key_callback_function()
      -> cw_rec_mark_{begin|end}(rec, ...)

   When using the first way, there should be a binding between key and
   a receiver.

   The receiver can get it's properly formed input data (key down/key
   up events) from any source, so it's independent from key. On the
   other hand the key without receiver is rather useless. Therefore I
   think that the key should contain reference to a receiver, not the
   other way around.

   \reviewed on 2017-01-31

   \param key - key that needs to have a receiver associated with it
   \param rec - receiver to be used with given key
*/
void cw_key_register_receiver(volatile cw_key_t * key, cw_rec_t * rec)
{
	key->rec = rec;

	return;
}




/**
   \brief Set new key value, generate appropriate tone (Mark/Space)

   Set new value of a key. Filter successive key-down or key-up
   actions into a single action (successive calls with the same value
   of \p key_state don't change internally registered value of key).

   If and only if the function registers change of key value, an
   external callback function for keying (if configured) is called.

   If and only if the function registers change of key value, a state
   of related generator \p gen is changed accordingly (a tone is
   started or stopped).

   \param key - key in use
   \param key_state - key value to be set

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_sk_set_value_internal(volatile cw_key_t *key, int key_state)
{
	cw_assert (key, MSG_PREFIX "sk set value: key is NULL");
	cw_assert (key->gen, MSG_PREFIX "sk set value: generator is NULL");

	struct timeval t;
	gettimeofday(&t, NULL);
	key->timer.tv_sec = t.tv_sec;
	key->timer.tv_usec = t.tv_usec;

	if (key->sk.key_value == key_state) {
		/* This may happen when dequeueing 'forever' tone
		   multiple times in a row. */
		return CW_SUCCESS;
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYING, CW_DEBUG_INFO,
		      MSG_PREFIX "sk set value %d->%d", key->sk.key_value, key_state);

	/* Remember the new key value. */
	key->sk.key_value = key_state;

	/* Call a registered callback. */
	if (key->key_callback_func) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      MSG_PREFIX "sk set value: about to call callback, key state = %d\n", key->sk.key_value);

		(*key->key_callback_func)(&key->timer, key->sk.key_value, key->key_callback_arg);
	}
	if (key->key_legacy_callback_func) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      MSG_PREFIX "sk set value: about to call legacy callback, key state = %d\n", key->sk.key_value);

		(*key->key_legacy_callback_func)(key->key_legacy_callback_arg, key->sk.key_value);
	}

	int rv;
	if (key->sk.key_value == CW_KEY_STATE_CLOSED) {
		/* In case of straight key we don't know at
		   all how long the tone should be (we don't
		   know for how long the key will be closed).

		   Let's enqueue a beginning of mark. A
		   constant tone will be generated until function
		   receives CW_KEY_STATE_OPEN key state. */
		rv = cw_gen_enqueue_begin_mark_internal(key->gen);
	} else {
		/* CW_KEY_STATE_OPEN, time to go from Mark
		   (audible tone) to Space (silence). */
		rv = cw_gen_enqueue_begin_space_internal(key->gen);
	}
	cw_assert (CW_SUCCESS == rv, MSG_PREFIX "sk set value: failed to enqueue key value %d", key->sk.key_value);
	return rv;
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
   client code). Key's value (CW_KEY_STATE_OPEN/CW_KEY_STATE_CLOSED)
   is passed to the callback as argument. Callback is called by this
   function only when there is a change of key value - this function
   filters successive key-down or key-up actions into a single action.

   TODO: explain difference and relation between key's value and
   keyer's graph state.

   \param key - current key
   \param key_state - key value to be set (CW_KEY_STATE_OPEN/CW_KEY_STATE_CLOSED)
   \param symbol - symbol to enqueue (Space, Dot, Dash)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_set_value_internal(volatile cw_key_t *key, int key_state, char symbol)
{
	cw_assert (key, MSG_PREFIX "ik set value: keyer is NULL");
	cw_assert (key->gen, MSG_PREFIX "ik set value: generator is NULL");

	if (key->ik.key_value == key_state) {
		/* This is not an error. This may happen when
		   dequeueing 'forever' tone multiple times in a
		   row. */
		return CW_SUCCESS;
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYING, CW_DEBUG_INFO,
		      MSG_PREFIX "ik set value %d->%d", key->ik.key_value, key_state);

	/* Remember the new key value. */
	key->ik.key_value = key_state;

	/* Call a registered callback. */
	if (key->key_callback_func) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      MSG_PREFIX "ik set value: about to call callback, key state = %d\n", key->ik.key_value);

		(*key->key_callback_func)(&key->timer, key->ik.key_value, key->key_callback_arg);
	}
	if (key->key_legacy_callback_func) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      MSG_PREFIX "ik set value: about to call legacy callback, key state = %d\n", key->ik.key_value);

		(*key->key_legacy_callback_func)(key->key_legacy_callback_arg, key->ik.key_value);
	}

	/* 'Partial' means without any end-of-mark spaces. */
	int rv = cw_gen_enqueue_partial_symbol_internal(key->gen, symbol);
	cw_assert (CW_SUCCESS == rv, MSG_PREFIX "ik set value: failed to key symbol '%c'", symbol);
	return rv;
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

   \reviewed on 2017-01-31

   \param key
*/
void cw_key_ik_enable_curtis_mode_b(volatile cw_key_t *key)
{
	key->ik.curtis_mode_b = true;
	return;
}




/**
   See documentation of cw_key_ik_enable_curtis_mode_b() for more information

   \reviewed on 2017-01-31

   \param key
*/
void cw_key_ik_disable_curtis_mode_b(volatile cw_key_t * key)
{
	key->ik.curtis_mode_b = false;
	return;
}




/**
   See documentation of cw_enable_iambic_curtis_mode_b() for more information

   \reviewed on 2017-01-31

   \param key

   \return true if Curtis mode is enabled for the key
   \return false otherwise
*/
bool cw_key_ik_get_curtis_mode_b(const volatile cw_key_t *key)
{
	return key->ik.curtis_mode_b;
}




/**
   \brief Update state of iambic keyer, enqueue tone representing state of the iambic keyer

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
int cw_key_ik_update_graph_state_internal(volatile cw_key_t *key)
{
	if (!key) {
		/* This function is called from generator thread. It
		   is perfectly valid situation that for some
		   applications a generator exists, but a keyer does
		   not exist.  Silently accept this situation.

		   TODO: move this check earlier in call stack, so
		   that less functions are called before silently
		   discovering that key doesn't exist. */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_INTERNAL, CW_DEBUG_DEBUG,
			      MSG_PREFIX "ik update: NULL key, silently accepting");
		return CW_SUCCESS;
	}

	/* This function is called from generator thread function, so
	   the generator must exist. Be paranoid and check it, just in
	   case. */
	cw_assert (key->gen, MSG_PREFIX "ik update: generator is NULL");


	if (key->ik.lock) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_INTERNAL, CW_DEBUG_ERROR,
			      MSG_PREFIX "ik update: lock in thread %ld", (long) pthread_self());
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
		   sync.  We are *at the end* of Mark (Dot), so key should
		   be (still) closed. */
		cw_assert (key->ik.key_value == CW_KEY_STATE_CLOSED,
			   MSG_PREFIX "ik update: inconsistency between keyer state (%s) ad key value (%d)",
			   cw_iambic_keyer_states[key->ik.graph_state], key->ik.key_value);

		/* We are ending a Dot, so turn off tone and begin the
		   after-dot Space.
		   No routine status checks are made! (TODO) */
		cw_key_ik_set_value_internal(key, CW_KEY_STATE_OPEN, CW_SYMBOL_SPACE);
		key->ik.graph_state = key->ik.graph_state == KS_IN_DOT_A ? KS_AFTER_DOT_A : KS_AFTER_DOT_B;
		break;

	case KS_IN_DASH_A:
	case KS_IN_DASH_B:
		/* Verify that key value and keyer graph state are in
		   sync.  We are *at the end* of Mark (Dash), so key should
		   be (still) closed. */
		cw_assert (key->ik.key_value == CW_KEY_STATE_CLOSED,
			   MSG_PREFIX "ik update: inconsistency between keyer state (%s) ad key value (%d)",
			   cw_iambic_keyer_states[key->ik.graph_state], key->ik.key_value);

		/* We are ending a Dash, so turn off tone and begin
		   the after-dash Space.
		   No routine status checks are made! (TODO) */
		cw_key_ik_set_value_internal(key, CW_KEY_STATE_OPEN, CW_SYMBOL_SPACE);
		key->ik.graph_state = key->ik.graph_state == KS_IN_DASH_A ? KS_AFTER_DASH_A : KS_AFTER_DASH_B;
		break;

	case KS_AFTER_DOT_A:
	case KS_AFTER_DOT_B:
		/* Verify that key value and keyer graph state are in
		   sync.  We are *at the end* of Space, so key should
		   be (still) open. */
		cw_assert (key->ik.key_value == CW_KEY_STATE_OPEN,
			   MSG_PREFIX "ik update: inconsistency between keyer state (%s) ad key value (%d)",
			   cw_iambic_keyer_states[key->ik.graph_state], key->ik.key_value);

		/* If we have just finished a Dot or a Dash and its
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
			cw_key_ik_set_value_internal(key, CW_KEY_STATE_CLOSED, CW_DASH_REPRESENTATION);
			key->ik.graph_state = KS_IN_DASH_A;

		} else if (key->ik.dash_latch) {
			cw_key_ik_set_value_internal(key, CW_KEY_STATE_CLOSED, CW_DASH_REPRESENTATION);
			if (key->ik.curtis_b_latch) {
				key->ik.curtis_b_latch = false;
				key->ik.graph_state = KS_IN_DASH_B;
			} else {
				key->ik.graph_state = KS_IN_DASH_A;
			}
		} else if (key->ik.dot_latch) {
			cw_key_ik_set_value_internal(key, CW_KEY_STATE_CLOSED, CW_DOT_REPRESENTATION);
			key->ik.graph_state = KS_IN_DOT_A;
		} else {
			key->ik.graph_state = KS_IDLE;
			//cw_finalization_schedule_internal();
		}

		break;

	case KS_AFTER_DASH_A:
	case KS_AFTER_DASH_B:
		/* Verify that key value and keyer graph state are in
		   sync.  We are *at the end* of Space, so key should
		   be (still) open. */
		cw_assert (key->ik.key_value == CW_KEY_STATE_OPEN,
			   MSG_PREFIX "ik update: inconsistency between keyer state (%s) ad key value (%d)",
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
			cw_key_ik_set_value_internal(key, CW_KEY_STATE_CLOSED, CW_DOT_REPRESENTATION);
			key->ik.graph_state = KS_IN_DOT_A;

		} else if (key->ik.dot_latch) {
			cw_key_ik_set_value_internal(key, CW_KEY_STATE_CLOSED, CW_DOT_REPRESENTATION);
			if (key->ik.curtis_b_latch) {
				key->ik.curtis_b_latch = false;
				key->ik.graph_state = KS_IN_DOT_B;
			} else {
				key->ik.graph_state = KS_IN_DOT_A;
			}
		} else if (key->ik.dash_latch) {
			cw_key_ik_set_value_internal(key, CW_KEY_STATE_CLOSED, CW_DASH_REPRESENTATION);
			key->ik.graph_state = KS_IN_DASH_A;
		} else {
			key->ik.graph_state = KS_IDLE;
			//cw_finalization_schedule_internal();
		}

		break;
	default:
		cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYER_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "ik update: invalid keyer state %d",
			      key->ik.graph_state);
		return CW_FAILURE;
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      MSG_PREFIX "ik update: keyer state: %s -> %s",
		      cw_iambic_keyer_states[old_state], cw_iambic_keyer_states[key->ik.graph_state]);

	key->ik.lock = false;
	return CW_SUCCESS;
}




/**
   \brief Inform iambic keyer logic about changed state of iambic keyer's paddles

   Function informs the library that the iambic keyer paddles have
   changed state.  The new paddle states are recorded, and if either
   transition from CW_KEY_STATE_OPEN to CW_KEY_STATE_CLOSED, paddle
   latches (for iambic functions) are also set.

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
   \param dot_paddle_state: CW_KEY_STATE_CLOSED or CW_KEY_STATE_OPEN
   \param dash_paddle_state: CW_KEY_STATE_CLOSED or CW_KEY_STATE_OPEN

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_notify_paddle_event(volatile cw_key_t *key, int dot_paddle_state, int dash_paddle_state)
{
#if 0 /* This is disabled, but I'm not sure why. */  /* This code has been disabled some time before 2017-01-31. */
	/* If the tone queue or the straight key are busy, this is going to
	   conflict with our use of the sound card, console sounder, and
	   keying system.  So return an error status in this case. */
	if (cw_tq_is_busy_internal(key->gen->tq) || cw_key_sk_is_busy(key)) {
		errno = EBUSY;
		return CW_FAILURE;
	}
#endif

	/* Clean up and save the paddle states passed in. */
#if 0    /* This code has been disabled on 2017-01-31. */
	key->ik.dot_paddle = (dot_paddle_state != 0);
	key->ik.dash_paddle = (dash_paddle_state != 0);
#else
	key->ik.dot_paddle = dot_paddle_state;
	key->ik.dash_paddle = dash_paddle_state;
#endif

	/* Update the paddle latches if either paddle goes CLOSED.
	   The latches are checked in the signal handler, so if the
	   paddles go back to OPEN during this element, the item still
	   gets actioned.  The signal handler is also responsible for
	   clearing down the latches. TODO: verify the comment. */
	if (key->ik.dot_paddle == CW_KEY_STATE_CLOSED) {
		key->ik.dot_latch = true;
	}
	if (key->ik.dash_paddle == CW_KEY_STATE_CLOSED) {
		key->ik.dash_latch = true;
	}


	if (key->ik.curtis_mode_b
	    && key->ik.dot_paddle == CW_KEY_STATE_CLOSED
	    && key->ik.dash_paddle == CW_KEY_STATE_CLOSED) {

		/* Both paddles closed at the same time in Curtis mode B.

		   This flag is checked by the signal handler, to
		   determine whether to add mode B trailing timing
		   elements. TODO: verify this comment. */
		key->ik.curtis_b_latch = true;
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      MSG_PREFIX "ik notify: paddles %d,%d, latches %d,%d, curtis_b %d",
		      key->ik.dot_paddle, key->ik.dash_paddle,
		      key->ik.dot_latch, key->ik.dash_latch, key->ik.curtis_b_latch);


	if (key->ik.graph_state == KS_IDLE) {
		struct timeval t;
		gettimeofday(&t, NULL);
		key->timer.tv_sec = t.tv_sec;
		key->timer.tv_usec = t.tv_usec;

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
int cw_key_ik_update_state_initial_internal(volatile cw_key_t *key)
{
	cw_assert (key, MSG_PREFIX "ik update initial: keyer is NULL");
	cw_assert (key->gen, MSG_PREFIX "ik update initial: generator is NULL");

	if (key->ik.dot_paddle == CW_KEY_STATE_OPEN && key->ik.dash_paddle == CW_KEY_STATE_OPEN) {
		/* Both paddles are open/up. We certainly don't want
		   to start any process upon "both paddles open"
		   event. But the function shouldn't have been called
		   in that situation. */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYER_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "ik update initial: called the function when both paddles are open");

		/* Silently accept.
		   TODO: maybe it's a good idea, or maybe bad one to
		   return CW_SUCCESS here. */
		return CW_SUCCESS;
	}

	int old_state = key->ik.graph_state;

	if (key->ik.dot_paddle == CW_KEY_STATE_CLOSED) {
		/* "Dot" paddle pressed. Pretend that we are in "after
		   dash" space, so that keyer will have to transit
		   into "dot" mark state. */
		key->ik.graph_state = key->ik.curtis_b_latch
			? KS_AFTER_DASH_B : KS_AFTER_DASH_A;

	} else { /* key->ik.dash_paddle == CW_KEY_STATE_CLOSED */
		/* "Dash" paddle pressed. Pretend that we are in
		   "after dot" space, so that keyer will have to
		   transit into "dash" mark state. */

		key->ik.graph_state = key->ik.curtis_b_latch
			? KS_AFTER_DOT_B : KS_AFTER_DOT_A;
	}

	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
		      MSG_PREFIX "ik update initial: keyer state: %s -> %s",
		      cw_iambic_keyer_states[old_state], cw_iambic_keyer_states[key->ik.graph_state]);


	/* Here comes the "real" initial transition - this is why we
	   called this function. We will transition from state set
	   above into "real" state, reflecting state of paddles. */
	int rv = cw_key_ik_update_graph_state_internal(key);
	if (rv == CW_FAILURE) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYER_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "ik update initial: call to update_state_initial() failed first time");
		/* Just try again, once. */
		usleep(1000);
		rv = cw_key_ik_update_graph_state_internal(key);
		if (rv == CW_FAILURE) {
			cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYER_STATES, CW_DEBUG_ERROR,
				      MSG_PREFIX "ik update initial: call to update_state_initial() failed twice");
		}
	}

	return rv;
}




/**
   \brief Change state of Dot paddle

   Alter the state of Dot paddle. State of Dash paddle remains unchanged.

   See cw_key_ik_notify_paddle_event() for details of iambic
   keyer background processing, and how to check its status.

   \param key
   \param dot_paddle_state: CW_KEY_STATE_CLOSED or CW_KEY_STATE_OPEN

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_notify_dot_paddle_event(volatile cw_key_t * key, int dot_paddle_state)
{
	return cw_key_ik_notify_paddle_event(key, dot_paddle_state, key->ik.dash_paddle);
}




/**
   \brief Change state of Dash paddle

   Alter the state of Dash paddle. State of Dot paddle remains unchanged.

   See documentation of cw_notify_keyer_dot_paddle_event() for more information

   \param key
   \param dash_paddle_state: CW_KEY_STATE_CLOSED or CW_KEY_STATE_OPEN

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_notify_dash_paddle_event(volatile cw_key_t * key, int dash_paddle_state)
{
	return cw_key_ik_notify_paddle_event(key, key->ik.dot_paddle, dash_paddle_state);
}




/**
   \brief Get the current saved states of the two paddles

   Either of the last two arguments can be NULL - it won't be updated then.

   \param key
   \param dot_paddle_state: will be updated with CW_KEY_STATE_CLOSED or CW_KEY_STATE_OPEN value
   \param dash_paddle_state: will be updated with CW_KEY_STATE_CLOSED or CW_KEY_STATE_OPEN value
*/
void cw_key_ik_get_paddles(const volatile cw_key_t * key, int * dot_paddle_state, int * dash_paddle_state)
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
   A paddle latch is set to true when the paddle state becomes CLOSED,
   and is cleared if the paddle state is OPEN when the element finishes
   sending.

   Either of the last two arguments can be NULL - it won't be updated then.

   \param key
   \param dot_paddle_latch_state: will be updated with true or false: TODO: true/false or OPEN/CLOSED?
   \param dash_paddle_latch_state: will be updated with true or false
*/
void cw_key_ik_get_paddle_latches_internal(volatile cw_key_t * key, /* out */ int * dot_paddle_latch_state, /* out */ int * dash_paddle_latch_state)
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

   \return true if keyer is busy (keyer's state is other than IDLE)
   \return false if keyer is not busy (keyer's state is IDLE)
*/
bool cw_key_ik_is_busy_internal(const volatile cw_key_t *key)
{
	return key->ik.graph_state != KS_IDLE;
}





/**
   \brief Wait for end of element from the keyer

   Waits until the end of the current element, Dot or Dash, from the keyer.

   The function always returns CW_SUCCESS.

   \param key

   \return CW_SUCCESS
*/
int cw_key_ik_wait_for_element(const volatile cw_key_t * key)
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
		/* cw_signal_wait_internal(); */ /* Old implementation was using signals. */ /* This code has been disabled some time before 2017-01-31. */
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
		/* cw_signal_wait_internal(); */ /* Old implementation was using signals. */ /* This code has been disabled some time before 2017-01-31. */
	}
	pthread_mutex_unlock(&key->gen->tq->wait_mutex);

	return CW_SUCCESS;
}




/**
   \brief Wait for the current keyer cycle to complete

   The routine returns CW_SUCCESS on success.

   It returns CW_FAILURE (with errno set to EDEADLK) if either paddle
   state is CLOSED.

   \param key

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_ik_wait_for_keyer(volatile cw_key_t * key)
{
	/* Check that neither paddle is CLOSED; if either is, then the
	   signal cycle is going to continue forever, and we'll never
	   return from this routine. TODO: verify this comment. */
	if (key->ik.dot_paddle == CW_KEY_STATE_CLOSED || key->ik.dash_paddle == CW_KEY_STATE_CLOSED) {
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait for the keyer state to go idle. */
	pthread_mutex_lock(&key->gen->tq->wait_mutex);
	while (key->ik.graph_state != KS_IDLE) {
		pthread_cond_wait(&key->gen->tq->wait_var, &key->gen->tq->wait_mutex);
		/* cw_signal_wait_internal(); */ /* Old implementation was using signals. */ /* This code has been disabled some time before 2017-01-31. */
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
void cw_key_ik_reset_internal(volatile cw_key_t *key)
{
	cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
		      MSG_PREFIX "ik reset: keyer state %s -> KS_IDLE", cw_iambic_keyer_states[key->ik.graph_state]);
	key->ik.graph_state = KS_IDLE;

	key->ik.key_value = CW_KEY_STATE_OPEN;

	key->ik.dot_paddle = CW_KEY_STATE_OPEN;
	key->ik.dash_paddle = CW_KEY_STATE_OPEN;
	key->ik.dot_latch = false;
	key->ik.dash_latch = false;
	key->ik.curtis_mode_b = false;
	key->ik.curtis_b_latch = false;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_gen_silence_internal(key->gen);
	cw_finalization_schedule_internal(); /* TODO: do we still need this? */

	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
		      MSG_PREFIX "ik reset: keyer state -> %s (reset)", cw_iambic_keyer_states[key->ik.graph_state]);

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
void cw_key_ik_increment_timer_internal(volatile cw_key_t *key, int usecs)
{
	if (!key) {
		/* This function is called from generator thread. It
		   is perfectly valid situation that for some
		   applications a generator exists, but a keyer does
		   not exist.  Silently accept this situation. */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_INTERNAL, CW_DEBUG_DEBUG,
			      MSG_PREFIX "ik increment: NULL key, silently accepting");
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
			      MSG_PREFIX "ik increment: incrementing timer by %d [us]\n", usecs);

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
   \brief Set new value of straight key

   If \p key_state indicates no change of state, the call is ignored.


   \param key - straight key to update
   \param key_state - new state of straight key (CW_KEY_STATE_OPEN / CW_KEY_STATE_CLOSED)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_key_sk_notify_event(volatile cw_key_t * key, int key_state)
{
#if 0 /* This is disabled, but I'm not sure why. */  /* This code has been disabled some time before 2017-01-31. */
	/* If the tone queue or the keyer are busy, we can't use the
	   sound card, console sounder, or the key control system. */
	if (cw_tq_is_busy_internal(key->gen->tq) || cw_key_ik_is_busy_internal(key)) {
		errno = EBUSY;
		return CW_FAILURE;
	}
#endif

	/* Do tones and keying, and set up timeouts and soundcard
	   activities to match the new key state. */
	return cw_key_sk_set_value_internal(key, key_state);
}




/**
   \brief Get current value of straight key

   Returns the current value of the straight key.

   \reviewed on 2017-01-31

   \param key

   \return CW_KEY_STATE_CLOSED if the key is down
   \return CW_KEY_STATE_OPEN if the key up
*/
int cw_key_sk_get_value(const volatile cw_key_t * key)
{
	return key->sk.key_value;
}




/**
   \brief Check if the straight key is busy

   This routine is just a pseudonym for
   cw_key_sk_get_value(), and exists to fill a hole in the
   API naming conventions. TODO: verify if this function is needed in
   new API.

   TODO: "busy" is a misleading term. Either remove the function, or
   rename it.

   \reviewed on 2017-01-31

   \param key

   \return true if the straight key is busy
   \return false if the straight key is not busy
*/
bool cw_key_sk_is_busy(volatile cw_key_t * key)
{
	return key->sk.key_value;
}




/**
   \brief Clear the straight key state, and return to silence

   This function is suitable for calling from an application exit handler.

   \param key
*/
void cw_key_sk_reset_internal(volatile cw_key_t *key)
{
	key->sk.key_value = CW_KEY_STATE_OPEN;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_gen_silence_internal(key->gen);
	//cw_finalization_schedule_internal();

	cw_debug_msg (&cw_debug_object, CW_DEBUG_STRAIGHT_KEY_STATES, CW_DEBUG_INFO,
		      MSG_PREFIX "sk: key state ->OPEN (reset)");

	return;
}




/**
   \brief Create new key

   \reviewed on 2017-01-31

   \return pointer to new key on success
   \return NULL on failure
*/
cw_key_t * cw_key_new(void)
{
	cw_key_t * key = (cw_key_t *) malloc(sizeof (cw_key_t));
	if (!key) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      MSG_PREFIX "new: malloc()");
		return (cw_key_t *) NULL;
	}

	key->gen = (cw_gen_t *) NULL;
	key->rec = (cw_rec_t *) NULL;

	key->key_callback_func = NULL;
	key->key_callback_arg = NULL;
	key->key_legacy_callback_func = NULL;
	key->key_legacy_callback_arg = NULL;

	key->sk.key_value = CW_KEY_STATE_OPEN;

	key->ik.graph_state = KS_IDLE;
	key->ik.key_value = CW_KEY_STATE_OPEN;

	key->ik.dot_paddle = CW_KEY_STATE_OPEN;
	key->ik.dash_paddle = CW_KEY_STATE_OPEN;
	key->ik.dot_latch = false;
	key->ik.dash_latch = false;

	key->ik.curtis_mode_b = false;
	key->ik.curtis_b_latch = false;

	key->ik.lock = false;

	key->tk.key_value = CW_KEY_STATE_OPEN;

	key->timer.tv_sec = 0;
	key->timer.tv_usec = 0;

	return key;
}




/**
   \brief Delete key

   \p key is deallocated. Pointer to \p key is set to NULL.

   \reviewed on 2017-01-31

   \param key - pointer to key
*/
void cw_key_delete(cw_key_t ** key)
{
	cw_assert (key, MSG_PREFIX "delete: key is NULL");

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
