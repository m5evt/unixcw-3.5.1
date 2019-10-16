/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */




#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>




#include "test_framework.h"

#include "libcw_key.h"
#include "libcw_key_tests.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw.h"
#include "libcw2.h"




static int key_setup(cw_test_executor_t * cte, cw_key_t ** key, cw_gen_t ** gen);
static void key_destroy(cw_key_t ** key, cw_gen_t ** gen);
static int test_keyer_helper(cw_test_executor_t * cte, cw_key_t * key, int intended_dot_paddle, int intended_dash_paddle, char mark_representation, const char * marks_name, int max);
static int test_straight_key_helper(cw_test_executor_t * cte, cw_key_t * key, int intended_key_state, const char * state_name, int max);




/**
   @reviewed on 2019-10-12
*/
static int key_setup(cw_test_executor_t * cte, cw_key_t ** key, cw_gen_t ** gen)
{
	*key = cw_key_new();
	if (!*key) {
		cte->log_error(cte, "Can't create key, stopping the test\n");
		return -1;
	}


	*gen = cw_gen_new(cte->current_sound_system, NULL);
	if (!*gen) {
		cte->log_error(cte, "Can't create gen, stopping the test\n");
		return -1;
	}


	if (CW_SUCCESS != cw_gen_start(*gen)) {
		cte->log_error(cte, "Can't start generator, stopping the test\n");
		cw_gen_delete(gen);
		cw_key_delete(key);
		return -1;
	}

	cw_key_register_generator(*key, *gen);
	cw_gen_reset_parameters_internal(*gen);
	cw_gen_sync_parameters_internal(*gen);
	cw_gen_set_speed(*gen, 30);

	return 0;
}




/**
   @reviewed on 2019-10-12
*/
void key_destroy(cw_key_t ** key, cw_gen_t ** gen)
{
	if (NULL != key) {
		if (NULL != *key) {
			cw_key_delete(key);
		}
	}

	if (NULL != gen) {
		if (NULL != *gen) {
			cw_gen_delete(gen);
		}
	}
}




/**
   @reviewed on 2019-10-12
*/
int test_keyer_helper(cw_test_executor_t * cte, cw_key_t * key, int intended_dot_paddle, int intended_dash_paddle, char mark_representation, const char * marks_name, int max)
{
	/* Test: keying dot. */
	{
		/* Seems like this function calls means "keyer pressed
		   until further notice". First argument is true, so
		   this is a dot. */
		int cwret = cw_key_ik_notify_paddle_event(key, intended_dot_paddle, intended_dash_paddle);
		cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 0, "cw_key_ik_notify_paddle_event(key, %d, %d)", intended_dot_paddle, intended_dash_paddle);


		bool failure = false;
		/* Since a X paddle is pressed, get "max" X marks from
		   the keyer. Notice that they aren't enqueued - we
		   won't run out of marks. Iambic keyer can produce
		   them indefinitely, as long as a paddle is
		   pressed. We just want to get N marks. */
		cte->log_info(cte, "%s: ", marks_name);
		for (int i = 0; i < max; i++) {
			cwret = cw_key_ik_wait_for_element(key);
			if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "wait for iambic key element (%s), #%d", marks_name, i)) {
				failure = true;
				break;
			}
			cte->log_info_cont(cte, "%c", mark_representation);
		}
		cte->log_info_cont(cte, "\n");

		cte->expect_op_int(cte, false, "==", failure, 0, "wait for iambic key elements (%s)", marks_name);
	}



	/* Test: preserving of paddle states. */
	{
		int readback_dot_paddle;
		int readback_dash_paddle;

		cw_key_ik_get_paddles(key, &readback_dot_paddle, &readback_dash_paddle);
		cte->expect_op_int(cte, intended_dot_paddle, "==", readback_dot_paddle, 0, "cw_keyer_get_keyer_paddles(): preserving dot paddle (%s)", marks_name);
		cte->expect_op_int(cte, intended_dash_paddle, "==", readback_dash_paddle, 0, "cw_keyer_get_keyer_paddles(): preserving dash paddle (%s)", marks_name);
	}

	return 0;
}




/**
   tests::cw_key_ik_notify_paddle_event()
   tests::cw_key_ik_wait_for_element()
   tests::cw_key_ik_get_paddles()

   @reviewed on 2019-10-12
*/
int test_keyer(cw_test_executor_t * cte)
{
	const int max = (rand() % 30) + 20;

	cte->print_test_header(cte, "%s (%d)", __func__, max);

	cw_key_t * key = NULL;
	cw_gen_t * gen = NULL;
	if (0 != key_setup(cte, &key, &gen)) {
		return -1;
	}

	/* Perform some tests on the iambic keyer.  The latch finer
	   timing points are not tested here, just the basics - dots,
	   dashes, and alternating dots and dashes. */



	/* Test: keying dot. */
	test_keyer_helper(cte, key, CW_KEY_STATE_CLOSED, CW_KEY_STATE_OPEN, CW_DOT_REPRESENTATION, "dots", max);

	/* Test: keying dash. */
	test_keyer_helper(cte, key, CW_KEY_STATE_OPEN, CW_KEY_STATE_CLOSED, CW_DASH_REPRESENTATION, "dashes", max);

	/* Test: keying alternate dit/dash. */
	test_keyer_helper(cte, key, CW_KEY_STATE_CLOSED, CW_KEY_STATE_CLOSED, '#', "alternating", max);


	/* Test: set new state of paddles: no paddle pressed. */
	{
		bool failure = !cw_key_ik_notify_paddle_event(key, CW_KEY_STATE_OPEN, CW_KEY_STATE_OPEN);
		cte->expect_op_int(cte, false, "==", failure, 0, "cw_key_ik_notify_paddle_event(%d, %d)", CW_KEY_STATE_OPEN, CW_KEY_STATE_OPEN);
	}

	cw_key_ik_wait_for_keyer(key);

	key_destroy(&key, &gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   @reviewed on 2019-10-12
*/
int test_straight_key_helper(cw_test_executor_t * cte, cw_key_t * key, int intended_key_state, const char * state_name, int max)
{
	bool event_failure = false;
	bool state_failure = false;
	bool busy_failure = false;

	for (int i = 0; i < max; i++) {
		const int cwret = cw_key_sk_notify_event(key, intended_key_state);
		if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "key state %d", intended_key_state)) {
			event_failure = true;
			break;
		}

		const int readback_state = cw_key_sk_get_value(key);
		if (!cte->expect_op_int(cte, intended_key_state, "==", readback_state, 1, "key state readback (%d)", intended_key_state)) {
			state_failure = true;
			break;
		}

		/* not busy == up == opened
		   busy == down == closed. */
		const bool is_busy = cw_key_sk_is_busy(key);
		const bool expected_is_busy = intended_key_state == CW_KEY_STATE_CLOSED;
		if (!cte->expect_op_int(cte, expected_is_busy, "==", is_busy, 1, "key business readback (%d)", intended_key_state)) {
			busy_failure = true;
			break;
		}
	}

	cte->expect_op_int(cte, false, "==", event_failure, 0, "cw_key_sk_notify_event(<key %s>)", state_name);
	cte->expect_op_int(cte, false, "==", state_failure, 0, "cw_key_sk_get_value(<key %s)", state_name);
	cte->expect_op_int(cte, false, "==", busy_failure, 0, "cw_straight_key_busy(<key %s>)", state_name);

	return 0;
}




/**
   tests::cw_key_sk_notify_event()
   tests::cw_key_sk_get_value()
   tests::cw_key_sk_is_busy()

   @reviewed on 2019-10-12
*/
int test_straight_key(cw_test_executor_t * cte)
{
	const int max = (rand() % 30) + 20;

	cte->print_test_header(cte, "%s (%d)", __func__, max);

	cw_key_t * key = NULL;
	cw_gen_t * gen = NULL;
	if (0 != key_setup(cte, &key, &gen)) {
		return -1;
	}

	/* See what happens when we tell the library 'max' times in a
	   row that key is open. */
	test_straight_key_helper(cte, key, CW_KEY_STATE_OPEN, "open", max);

	/* See what happens when we tell the library 'max' times in a
	   row that key is closed. */
	test_straight_key_helper(cte, key, CW_KEY_STATE_CLOSED, "closed", max);


	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		struct timespec t;
		int usecs = CW_USECS_PER_SEC;
		cw_usecs_to_timespec_internal(&t, usecs);


		/* Alternate between open and closed. */
		for (int i = 0; i < max; i++) {
			const int intended_key_state = (i % 2) ? CW_KEY_STATE_OPEN : CW_KEY_STATE_CLOSED;
			const int cwret = cw_key_sk_notify_event(key, intended_key_state);
			if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "alternating key state, notification, iteration %d, value %d", i, intended_key_state)) {
				event_failure = true;
				break;
			}

			const int readback_key_state = cw_key_sk_get_value(key);
			if (!cte->expect_op_int(cte, intended_key_state, "==", readback_key_state, 1, "alternating key state, value readback, iteration %d, value %d", i, intended_key_state)) {
				state_failure = true;
				break;
			}

			const bool is_busy = cw_key_sk_is_busy(key);
			const bool expected_is_busy = intended_key_state == CW_KEY_STATE_CLOSED;
			if (!cte->expect_op_int(cte, expected_is_busy, "==", is_busy, 1, "alternating key state, busy readback, iteration %d, value %d", i, intended_key_state)) {
				busy_failure = true;
				break;
			}

			cte->log_info_cont(cte, "%d", intended_key_state);
#ifdef __FreeBSD__
			/* There is a problem with nanosleep() and
			   signals on FreeBSD. TODO: see if the
			   problem still persists after moving from
			   signals to conditional variables. */
			sleep(1);
#else
			cw_nanosleep_internal(&t);
#endif
		}
		cte->log_info_cont(cte, "\n");

		/* Never leave the key closed. */
		cw_key_sk_notify_event(key, CW_KEY_STATE_OPEN);

		cte->expect_op_int(cte, false, "==", event_failure, 0, "cw_key_sk_notify_event(<key open/closed>)");
		cte->expect_op_int(cte, false, "==", state_failure, 0, "cw_key_sk_get_value(<key open/closed>)");
		cte->expect_op_int(cte, false, "==", busy_failure, 0, "cw_straight_key_busy(<key open/closed>)");
	}

	key_destroy(&key, &gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}
