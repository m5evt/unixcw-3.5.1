#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>




#include "tests/libcw_test_utils.h"
#include "libcw_key.h"
#include "libcw_key_tests.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/key: "




/**
   tests::cw_key_ik_notify_paddle_event()
   tests::cw_key_ik_wait_for_element()
   tests::cw_key_ik_get_paddles()
*/
unsigned int test_keyer(cw_key_t * key, cw_test_stats_t * stats)
{
	int p = fprintf(out_file, MSG_PREFIX "iambic keyer operation:\n");
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
		int n = fprintf(out_file, MSG_PREFIX "cw_key_ik_notify_paddle_event(key, true, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dot" paddle is pressed, get 30 "dot"
		   events from the keyer. */
		fprintf(out_file, MSG_PREFIX "testing iambic keyer dots   ");
		fflush(out_file);
		for (int i = 0; i < 30; i++) {
			success = success && cw_key_ik_wait_for_element(key);
			putchar('.');
			fflush(out_file);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_key_ik_wait_for_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_key_ik_get_paddles(key, &dot_paddle, &dash_paddle);
		bool failure = !dot_paddle || dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_keyer_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: keying dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Second
		   argument is true, so this is a dash. */

		bool failure = !cw_key_ik_notify_paddle_event(key, false, true);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_key_ik_notify_paddle_event(key, false, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dash" paddle is pressed, get 30 "dash"
		   events from the keyer. */
		fprintf(out_file, MSG_PREFIX "testing iambic keyer dashes ");
		fflush(out_file);
		for (int i = 0; i < 30; i++) {
			success = success && cw_key_ik_wait_for_element(key);
			putchar('-');
			fflush(out_file);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_key_ik_wait_for_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_key_ik_get_paddles(key, &dot_paddle, &dash_paddle);
		bool failure = dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_key_ik_get_paddles():");
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
		int n = fprintf(out_file, MSG_PREFIX "cw_key_ik_notify_paddle_event(true, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		bool success = true;
		fprintf(out_file, MSG_PREFIX "testing iambic alternating  ");
		fflush(out_file);
		for (int i = 0; i < 30; i++) {
			success = success && cw_key_ik_wait_for_element(key);
			putchar('#');
			fflush(out_file);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_key_ik_wait_for_element:");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_key_ik_get_paddles(key, &dot_paddle, &dash_paddle);
		bool failure = !dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_key_ik_get_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: set new state of paddles: no paddle pressed. */
	{
		bool failure = !cw_key_ik_notify_paddle_event(key, false, false);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_key_ik_notify_paddle_event(false, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_key_ik_wait_for_keyer(key);

	p = fprintf(out_file, MSG_PREFIX "iambic keyer operation:");
	CW_TEST_PRINT_TEST_RESULT(false, p);
	fflush(out_file);

	return 0;
}





/**
   tests::cw_key_sk_notify_event()
   tests::cw_key_sk_get_value()
   tests::cw_key_sk_is_busy()
*/
unsigned int test_straight_key(cw_key_t * key, cw_test_stats_t * stats)
{
	int p = fprintf(out_file, MSG_PREFIX "straight key operation:\n");
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

			if (CW_KEY_STATE_OPEN != cw_key_sk_get_value(key)) {
				state_failure = true;
				break;
			}

			if (cw_key_sk_is_busy(key)) {
				busy_failure = true;
				break;
			}
		}

		event_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_key_sk_notify_event(<key open>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_key_sk_get_value(<key open>):");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_straight_key_busy(<key open>):");
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

			if (CW_KEY_STATE_CLOSED != cw_key_sk_get_value(key)) {
				state_failure = true;
				break;
			}

			if (!cw_key_sk_is_busy(key)) {
				busy_failure = true;
				break;
			}
		}


		event_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_key_sk_notify_event(<key closed>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_key_sk_get_value(<key closed>):");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_straight_key_busy(<key closed>):");
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

			if (CW_KEY_STATE_OPEN != cw_key_sk_get_value(key)) {
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

			if (CW_KEY_STATE_CLOSED != cw_key_sk_get_value(key)) {
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
		n = fprintf(out_file, "libcw: cw_key_sk_get_value(<key open/closed>):");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw: cw_straight_key_busy(<key open/closed>):");
		CW_TEST_PRINT_TEST_RESULT (busy_failure, n);
	}


	p = fprintf(out_file, MSG_PREFIX "straight key operation:");
	CW_TEST_PRINT_TEST_RESULT(false, p);
	fflush(out_file);

	return 0;
}
