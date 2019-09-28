#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>




#include "tests/libcw_test_framework.h"
#include "libcw_key.h"
#include "libcw_key_tests.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/key: "


static void key_setup(cw_test_executor_t * cte, cw_key_t ** key, cw_gen_t ** gen);
static void key_destroy(cw_key_t ** key, cw_gen_t ** gen);



static void key_setup(cw_test_executor_t * cte, cw_key_t ** key, cw_gen_t ** gen)
{
	*key = cw_key_new();
	if (!*key) {
		cte->log_err(cte, "Can't create key, stopping the test\n");
		return;
	}
	cw_key_register_generator(*key, *gen);

	*gen = cw_gen_new(cte->current_sound_system, NULL);

	if (CW_SUCCESS != cw_gen_start(*gen)) {
		cte->log_err(cte, "Can't start generator, stopping the test\n");
		cw_gen_delete(gen);
		if (*key) {
			cw_key_delete(key);
		}
		return;
	}

	cw_gen_reset_parameters_internal(*gen);
	cw_gen_sync_parameters_internal(*gen);
	cw_gen_set_speed(*gen, 30);
}




void key_destroy(cw_key_t ** key, cw_gen_t ** gen)
{
	if (NULL == key) {
		return;
	}
	if (NULL == *key) {
		return;
	}
	sleep(1);
	cw_key_delete(key);
	cw_gen_delete(gen);
}



/**
   tests::cw_key_ik_notify_paddle_event()
   tests::cw_key_ik_wait_for_element()
   tests::cw_key_ik_get_paddles()
*/
int test_keyer(cw_test_executor_t * cte)
{
	cw_key_t * key = NULL;
	cw_gen_t * gen = NULL;
	key_setup(cte, &key, &gen);

	/* Perform some tests on the iambic keyer.  The latch finer
	   timing points are not tested here, just the basics - dots,
	   dashes, and alternating dots and dashes. */

	int dot_paddle, dash_paddle;

	/* Test: keying dot. */
	{
		/* Seems like this function calls means "keyer pressed
		   until further notice". First argument is true, so
		   this is a dot. */
		int cwret = cw_key_ik_notify_paddle_event(key, true, false);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "cw_key_ik_notify_paddle_event(key, true, false):");


		bool failure = false;
		/* Since a "dot" paddle is pressed, get 30 "dot"
		   events from the keyer. */
		fprintf(out_file, MSG_PREFIX "testing iambic keyer dots   ");
		fflush(out_file);
		for (int i = 0; i < 30; i++) {
			cwret = cw_key_ik_wait_for_element(key);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "wait for iambic key element (dot), #%d", i)) {
				failure = true;
				break;
			}
			putchar('.');
			fflush(out_file);
		}
		putchar('\n');

		cte->expect_eq_int(cte, false, failure, "wait for iambic key elements (dots)");
	}



	/* Test: preserving of paddle states. */
	{
		cw_key_ik_get_paddles(key, &dot_paddle, &dash_paddle);
		const bool failure = !dot_paddle || dash_paddle;
		cte->expect_eq_int_errors_only(cte, false, failure, "cw_keyer_get_keyer_paddles():");
	}



	/* Test: keying dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Second
		   argument is true, so this is a dash. */

		int cwret = cw_key_ik_notify_paddle_event(key, false, true);
		cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_key_ik_notify_paddle_event(key, false, true):");


		bool failure = false;
		/* Since a "dash" paddle is pressed, get 30 "dash"
		   events from the keyer. */
		fprintf(out_file, MSG_PREFIX "testing iambic keyer dashes ");
		fflush(out_file);
		for (int i = 0; i < 30; i++) {
			int cwret = cw_key_ik_wait_for_element(key);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "wait for iambic key element (dash), #%d", i)) {
				failure = true;
				break;
			}
			putchar('-');
			fflush(out_file);
		}
		putchar('\n');

		cte->expect_eq_int_errors_only(cte, false, failure, "wait for iambic key elements (dashes)");
	}



	/* Test: preserving of paddle states. */
	{
		cw_key_ik_get_paddles(key, &dot_paddle, &dash_paddle);
		bool failure = dot_paddle || !dash_paddle;
		cte->expect_eq_int_errors_only(cte, false, failure, "cw_key_ik_get_paddles():");
	}



	/* Test: keying alternate dit/dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Both
		   arguments are true, so both paddles are pressed at
		   the same time.*/
		int cwret = cw_key_ik_notify_paddle_event(key, true, true);
		cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_key_ik_notify_paddle_event(true, true):");


		bool failure = false;
		fprintf(out_file, MSG_PREFIX "testing iambic alternating  ");
		fflush(out_file);
		for (int i = 0; i < 30; i++) {
			cwret = cw_key_ik_wait_for_element(key);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "wait for iambic key element (alternating), #%d", i)) {
				failure = true;
				break;
			}
			fflush(out_file);
		}
		putchar('\n');

		cte->expect_eq_int_errors_only(cte, false, failure, "wait for iambic key elements (alternating)");
	}



	/* Test: preserving of paddle states. */
	{
		cw_key_ik_get_paddles(key, &dot_paddle, &dash_paddle);
		const bool failure = !dot_paddle || !dash_paddle;
		cte->expect_eq_int_errors_only(cte, false, failure, "cw_key_ik_get_paddles():");
	}



	/* Test: set new state of paddles: no paddle pressed. */
	{
		bool failure = !cw_key_ik_notify_paddle_event(key, false, false);
		cte->expect_eq_int_errors_only(cte, false, failure, "cw_key_ik_notify_paddle_event(false, false)");
	}

	cw_key_ik_wait_for_keyer(key);

	key_destroy(&key, &gen);

	return 0;
}





/**
   tests::cw_key_sk_notify_event()
   tests::cw_key_sk_get_value()
   tests::cw_key_sk_is_busy()
*/
int test_straight_key(cw_test_executor_t * cte)
{
	cw_key_t * key = NULL;
	cw_gen_t * gen = NULL;
	key_setup(cte, &key, &gen);

	/* See what happens when we tell the library N times in a row that key is open. */
	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		for (int i = 0; i < 10; i++) {
			const int cwret = cw_key_sk_notify_event(key, CW_KEY_STATE_OPEN);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "KEY STATE OPEN")) {
				event_failure = true;
				break;
			}

			const int readback_state = cw_key_sk_get_value(key);
			if (!cte->expect_eq_int_errors_only(cte, CW_KEY_STATE_OPEN, readback_state, "key state readback (OPEN)")) {
				state_failure = true;
				break;
			}

			/* not busy == up == opened. */
			const bool is_busy = cw_key_sk_is_busy(key);
			if (!cte->expect_eq_int_errors_only(cte, false, is_busy, "key business readback (OPEN)")) {
				busy_failure = true;
				break;
			}
		}

		cte->expect_eq_int(cte, false, event_failure, "cw_key_sk_notify_event(<key open>)");
		cte->expect_eq_int(cte, false, state_failure, "cw_key_sk_get_value(<key open>):");
		cte->expect_eq_int(cte, false, busy_failure, "cw_straight_key_busy(<key open>):");
	}



	/* See what happens when we tell the library N times in a row that key is closed. */
	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		/* Again not sure why we have N identical calls in a
		   row. TODO: why? */
		for (int i = 0; i < 10; i++) {
			const int cwret = cw_key_sk_notify_event(key, CW_KEY_STATE_CLOSED);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "KEY STATE CLOSED")) {
				event_failure = true;
				break;
			}

			const int readback_state = cw_key_sk_get_value(key);
			if (!cte->expect_eq_int_errors_only(cte, CW_KEY_STATE_CLOSED, readback_state, "key state readback (CLOSED)")) {
				state_failure = true;
				break;
			}

			/* busy == down == closed. */
			const bool is_busy = cw_key_sk_is_busy(key);
			if (!cte->expect_eq_int_errors_only(cte, true, is_busy, "key business readback (CLOSED)")) {
				busy_failure = true;
				break;
			}
		}


		cte->expect_eq_int(cte, false, event_failure, "cw_key_sk_notify_event(<key closed>):");
		cte->expect_eq_int(cte, false, state_failure, "cw_key_sk_get_value(<key closed>):");
		cte->expect_eq_int(cte, false, busy_failure, "cw_straight_key_busy(<key closed>):");
	}



	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		struct timespec t;
		int usecs = CW_USECS_PER_SEC;
		cw_usecs_to_timespec_internal(&t, usecs);


		/* Alternate between open and closed. */
		for (int i = 0; i < 25; i++) {
			const int intended_key_state = (i % 2) ? CW_KEY_STATE_OPEN : CW_KEY_STATE_CLOSED;
			const int cwret = cw_key_sk_notify_event(key, intended_key_state);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "alternating key state, notification, iteration %d, value %d", i, intended_key_state)) {
				event_failure = true;
				break;
			}

			const int readback_key_state = cw_key_sk_get_value(key);
			if (cte->expect_eq_int_errors_only(cte, intended_key_state, readback_key_state, "alternating key state, value readback, iteration %d, value %d", i, intended_key_state)) {
				state_failure = true;
				break;
			}

			const bool is_busy = cw_key_sk_is_busy(key);
			if (!cte->expect_eq_int_errors_only(cte, intended_key_state, is_busy, "alternating key state, busy readback, iteration %d, value %d", i, intended_key_state)) {
				busy_failure = true;
				break;
			}

			fprintf(out_file, "%d", intended_key_state);
			fflush(out_file);
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

		cte->expect_eq_int(cte, false, event_failure, "cw_key_sk_notify_event(<key open/closed>):");
		cte->expect_eq_int(cte, false, state_failure, "cw_key_sk_get_value(<key open/closed>):");
		cte->expect_eq_int(cte, false, busy_failure, "cw_straight_key_busy(<key open/closed>):");
	}

	fflush(out_file);

	key_destroy(&key, &gen);

	return 0;
}
