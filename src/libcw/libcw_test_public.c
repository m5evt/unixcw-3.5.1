/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2014  Kamil Ignacak (acerion@wp.pl)
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


#include "config.h"


#define _XOPEN_SOURCE 600 /* signaction() + SA_RESTART */


#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>


#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "libcw.h"
#include "libcw_test.h"
#include "libcw_debug.h"
#include "libcw_internal.h"
#include "libcw_tq.h"
#include "libcw_utils.h"


typedef struct {
	int successes;
	int failures;
} cw_test_stats_t;


static cw_test_stats_t cw_stats_indep   = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_null    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_console = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_oss     = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_alsa    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_pa      = { .successes = 0, .failures = 0 };


static void cw_test_setup(void);
static int  cw_test_independent(void);
static int  cw_test_dependent(const char *audio_systems);
static int  cw_test_dependent_with(int audio_system, cw_test_stats_t *stats);
static void cw_test_print_stats(void);




typedef struct {
	const char character;
	const char *const representation;
	const int usecs[15];
} cw_test_receive_data_t;

static void test_helper_receive_tests(bool adaptive, const cw_test_receive_data_t *data, cw_test_stats_t *stats, bool fixed_speed);
static void cw_test_helper_tq_callback(void *data);

static void test_cw_version(cw_test_stats_t *stats);
static void test_cw_license(cw_test_stats_t *stats);
static void test_cw_debug_flags(cw_test_stats_t *stats);
static void test_cw_get_x_limits(cw_test_stats_t *stats);
static void test_parameter_ranges(cw_test_stats_t *stats);
static void test_tone_queue_0(cw_test_stats_t *stats);
static void test_tone_queue_1(cw_test_stats_t *stats);
static void test_tone_queue_2(cw_test_stats_t *stats);
static void test_tone_queue_3(cw_test_stats_t *stats);
static void test_tone_queue_callback(cw_test_stats_t *stats);
static void test_volume_functions(cw_test_stats_t *stats);
static void test_character_lookups(cw_test_stats_t *stats);
static void test_prosign_lookups(cw_test_stats_t *stats);
static void test_phonetic_lookups(cw_test_stats_t *stats);
static void test_send_primitives(cw_test_stats_t *stats);
static void test_representations(cw_test_stats_t *stats);
static void test_validate_character_and_string(cw_test_stats_t *stats);
static void test_send_character_and_string(cw_test_stats_t *stats);
static void test_fixed_receive(cw_test_stats_t *stats);
static int  test_fixed_receive_add_jitter(const int usecs, bool is_space);
static void test_adaptive_receive(cw_test_stats_t *stats);
static int  test_adaptive_receive_scale(const int usecs, float factor);
static void test_keyer(cw_test_stats_t *stats);
static void test_straight_key(cw_test_stats_t *stats);
// static void cw_test_delayed_release(cw_test_stats_t *stats);







/*---------------------------------------------------------------------*/
/*  Unit tests                                                         */
/*---------------------------------------------------------------------*/





/**
   tests::cw_version()
*/
void test_cw_version(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Test the cw_version() function. */

	bool failure = false;
	int rv = cw_version();
	int major = rv >> 16;
	int minor = rv & 0xff;

	/* Library's version is defined in LIBCW_VERSION. cw_version()
	   uses three calls to strtol() to get three parts of the
	   library version.

	   Let's use a different approach to convert LIBCW_VERSION
	   into numbers. */


	int current = 0, revision = 0;
	__attribute__((unused)) int age = 0;

	char *str = strdup(LIBCW_VERSION);

	for (int i = 0; ; i++, str = NULL) {

		char *token = strtok(str, ":");
		if (token == NULL) {
			break;
		}

		if (i == 0) {
			current = atoi(token);
		} else if (i == 1) {
			revision = atoi(token);
		} else if (i == 2) {
			age = atoi(token);
		} else {
			cw_assert (0, "too many tokens in \"%s\"\n", LIBCW_VERSION);
		}
	}

	free(str);
	str = NULL;

	if (major == current) {
		stats->successes++;
	} else {
		stats->failures++;
		failure = true;
	}

	if (minor == revision) {
		stats->successes++;
	} else {
		stats->failures++;
		failure = true;
	}

	int n = fprintf(stderr, "libcw: version %d.%d:", major, minor);
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   tests::cw_license()
*/
void test_cw_license(__attribute__((unused)) cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Test the cw_license() function. */
	cw_license();

	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





extern cw_debug_t cw_debug_object;


/**
   \brief Test getting and setting of debug flags.

   tests::cw_debug_set_flags()
   tests::cw_debug_get_flags()
*/
void test_cw_debug_flags(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Store current flags for period of tests. */
	uint32_t flags_backup = cw_debug_get_flags(&cw_debug_object);

	bool failure = false;
	for (uint32_t i = 0; i <= CW_DEBUG_MASK; i++) {
		cw_debug_set_flags(&cw_debug_object, i);
		if (cw_debug_get_flags(&cw_debug_object) != i) {
			failure = true;
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	int n = printf("libcw: cw_debug_set/get_flags():");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	/* Restore original flags. */
	cw_debug_set_flags(&cw_debug_object, flags_backup);

	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   \brief Ensure that we can obtain correct values of main parameter limits

   tests::cw_get_speed_limits()
   tests::cw_get_frequency_limits()
   tests::cw_get_volume_limits()
   tests::cw_get_gap_limits()
   tests::cw_get_tolerance_limits()
   tests::cw_get_weighting_limits()
*/
void test_cw_get_x_limits(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	struct {
		void (* get_limits)(int *min, int *max);
		int min;     /* Minimum hardwired in library. */
		int max;     /* Maximum hardwired in library. */
		int get_min; /* Minimum received in function call. */
		int get_max; /* Maximum received in function call. */

		const char *name;
	} test_data[] = {
		{ cw_get_speed_limits,      CW_SPEED_MIN,      CW_SPEED_MAX,      10000,  -10000,  "speed"     },
		{ cw_get_frequency_limits,  CW_FREQUENCY_MIN,  CW_FREQUENCY_MAX,  10000,  -10000,  "frequency" },
		{ cw_get_volume_limits,     CW_VOLUME_MIN,     CW_VOLUME_MAX,     10000,  -10000,  "volume"    },
		{ cw_get_gap_limits,        CW_GAP_MIN,        CW_GAP_MAX,        10000,  -10000,  "gap"       },
		{ cw_get_tolerance_limits,  CW_TOLERANCE_MIN,  CW_TOLERANCE_MAX,  10000,  -10000,  "tolerance" },
		{ cw_get_weighting_limits,  CW_WEIGHTING_MIN,  CW_WEIGHTING_MAX,  10000,  -10000,  "weighting" },
		{ NULL,                     0,                 0,                      0,      0,  NULL        }

	};

	for (int i = 0; test_data[i].get_limits; i++) {

		/* Get limits of a parameter. */
		test_data[i].get_limits(&test_data[i].get_min, &test_data[i].get_max);

		/* Test that limits are as expected (values received
		   by function call match those defined in library's
		   header file). */
		bool failure = test_data[i].get_min != test_data[i].min
			|| test_data[i].get_max != test_data[i].max;

		/* Act upon result of test. */
		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_%s_limits(): %d,%d:",
			       test_data[i].name, test_data[i].get_min, test_data[i].get_max);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}






/**
   Notice that getters of parameter limits are tested in test_cw_get_x_limits()

   tests::cw_set_send_speed()
   tests::cw_get_send_speed()
   tests::cw_set_receive_speed()
   tests::cw_get_receive_speed()
   tests::cw_set_frequency()
   tests::cw_get_frequency()
   tests::cw_set_volume()
   tests::cw_get_volume()
   tests::cw_set_gap()
   tests::cw_get_gap()
   tests::cw_set_tolerance()
   tests::cw_get_tolerance()
   tests::cw_set_weighting()
   tests::cw_get_weighting()
*/
void test_parameter_ranges(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int status;
	int txdot_usecs, txdash_usecs, end_of_element_usecs, end_of_character_usecs,
		end_of_word_usecs, additional_usecs, adjustment_usecs,
		rxdot_usecs, rxdash_usecs, dot_min_usecs, dot_max_usecs, dash_min_usecs,
		dash_max_usecs, end_of_element_min_usecs, end_of_element_max_usecs,
		end_of_element_ideal_usecs, end_of_character_min_usecs,
		end_of_character_max_usecs, end_of_character_ideal_usecs,
		adaptive_threshold;

	/* Print default low level timing values. */
	cw_reset_send_receive_parameters();
	cw_get_send_parameters(&txdot_usecs, &txdash_usecs,
			       &end_of_element_usecs, &end_of_character_usecs,
			       &end_of_word_usecs, &additional_usecs,
			       &adjustment_usecs);
	printf("libcw: cw_get_send_parameters():\n"
	       "libcw:     %d, %d, %d, %d, %d, %d, %d\n",
	       txdot_usecs, txdash_usecs, end_of_element_usecs,
	       end_of_character_usecs,end_of_word_usecs, additional_usecs,
	       adjustment_usecs);

	cw_get_receive_parameters(&rxdot_usecs, &rxdash_usecs,
				  &dot_min_usecs, &dot_max_usecs,
				  &dash_min_usecs, &dash_max_usecs,
				  &end_of_element_min_usecs,
				  &end_of_element_max_usecs,
				  &end_of_element_ideal_usecs,
				  &end_of_character_min_usecs,
				  &end_of_character_max_usecs,
				  &end_of_character_ideal_usecs,
				  &adaptive_threshold);
	printf("libcw: cw_get_receive_parameters():\n"
	       "libcw:     %d, %d, %d, %d, %d, %d, %d, %d\n"
	       "libcw:     %d, %d, %d, %d, %d\n",
	       rxdot_usecs, rxdash_usecs, dot_min_usecs, dot_max_usecs,
	       dash_min_usecs, dash_max_usecs, end_of_element_min_usecs,
	       end_of_element_max_usecs, end_of_element_ideal_usecs,
	       end_of_character_min_usecs, end_of_character_max_usecs,
	       end_of_character_ideal_usecs, adaptive_threshold);



	/* Test setting and getting of some basic parameters. */

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int *min, int *max);
		int (* set_new_value)(int new_value);
		int (* get_value)(void);

		int min; /* Minimal acceptable value of parameter. */
		int max; /* Maximal acceptable value of parameter. */

		const char *name;
	} test_data[] = {
		{ cw_get_speed_limits,      cw_set_send_speed,     cw_get_send_speed,     10000,  -10000,  "send_speed"    },
		{ cw_get_speed_limits,      cw_set_receive_speed,  cw_get_receive_speed,  10000,  -10000,  "receive_speed" },
		{ cw_get_frequency_limits,  cw_set_frequency,      cw_get_frequency,      10000,  -10000,  "frequency"     },
		{ cw_get_volume_limits,     cw_set_volume,         cw_get_volume,         10000,  -10000,  "volume"        },
		{ cw_get_gap_limits,        cw_set_gap,            cw_get_gap,            10000,  -10000,  "gap"           },
		{ cw_get_tolerance_limits,  cw_set_tolerance,      cw_get_tolerance,      10000,  -10000,  "tolerance"     },
		{ cw_get_weighting_limits,  cw_set_weighting,      cw_get_weighting,      10000,  -10000,  "weighting"     },
		{ NULL,                     NULL,                  NULL,                      0,       0,  NULL            }
	};


	bool failure = false;
	for (int i = 0; test_data[i].get_limits; i++) {

		/* Get limits of values to be tested. */
		/* Notice that getters of parameter limits are tested
		   in test_cw_get_x_limits(). */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);



		/* Test out-of-range value lower than minimum. */
		errno = 0;
		status = test_data[i].set_new_value(test_data[i].min - 1);
		failure = status || errno != EINVAL;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_set_%s(min - 1):", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test out-of-range value higher than maximum. */
		errno = 0;
		status = test_data[i].set_new_value(test_data[i].max + 1);
		failure = status || errno != EINVAL;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_set_%s(max + 1):", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test in-range values. */
		failure = false;
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			test_data[i].set_new_value(j);
			if (test_data[i].get_value() != j) {
				failure = true;
				break;
			}
		}

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get/set_%s():", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		failure = false;
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   \brief Test the limits of the parameters to the tone queue routine.

   tests::cw_queue_tone()
*/
void test_tone_queue_0(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int f_min, f_max;
	cw_get_frequency_limits(&f_min, &f_max);


	/* Test 1: invalid duration of tone. */
	errno = 0;
	int status = cw_queue_tone(-1, f_min);
	bool failure = status || errno != EINVAL;

	failure ? stats->failures++ : stats->successes++;
	int n = printf("libcw: cw_queue_tone(-1, cw_min_frequency):");
	CW_TEST_PRINT_TEST_RESULT (failure, n);



	/* Test 2: tone's frequency too low. */
	errno = 0;
	status = cw_queue_tone(1, f_min - 1);
	failure = status || errno != EINVAL;

	failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_queue_tone(1, cw_min_frequency - 1):");
	CW_TEST_PRINT_TEST_RESULT (failure, n);



	/* Test 3: tone's frequency too high. */
	errno = 0;
	status = cw_queue_tone(1, f_max + 1);
	failure = status || errno != EINVAL;

	failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_queue_tone(1, cw_max_frequency + 1):");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   \brief Simple tests of queueing and dequeueing of tones

   Ensure we can generate a few simple tones, and wait for them to end.

   tests::cw_queue_tone()
   tests::cw_get_tone_queue_length()
   tests::cw_wait_for_tone()
   tests::cw_wait_for_tone_queue()
*/
void test_tone_queue_1(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int l = 0;         /* Measured length of tone queue. */
	int expected = 0;  /* Expected length of tone queue. */

	int cw_min, cw_max;

	cw_set_volume(70);
	cw_get_frequency_limits(&cw_min, &cw_max);

	int N = 6;              /* Number of test tones put in queue. */
	int duration = 100000;  /* Duration of tone. */
	int delta_f = ((cw_max - cw_min) / (N - 1));      /* Delta of frequency in loops. */


	/* Test 1: enqueue N tones, and wait for each of them
	   separately. Control length of tone queue in the process. */

	/* Enqueue first tone. Don't check queue length yet.

	   The first tone is being dequeued right after enqueueing, so
	   checking the queue length would yield incorrect result.
	   Instead, enqueue the first tone, and during the process of
	   dequeueing it, enqueue rest of the tones in the loop,
	   together with checking length of the tone queue. */
	int f = cw_min;
	bool failure = !cw_queue_tone(duration, f);
	failure ? stats->failures++ : stats->successes++;
	int n = printf("libcw: cw_queue_tone():");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* This is to make sure that rest of tones is enqueued when
	   the first tone is being dequeued. */
	usleep(duration / 4);

	/* Enqueue rest of N tones. It is now safe to check length of
	   tone queue before and after queueing each tone: length of
	   the tone queue should increase (there won't be any decrease
	   due to dequeueing of first tone). */
	printf("libcw: enqueueing (1): \n");
	for (int i = 1; i < N; i++) {

		/* Monitor length of a queue as it is filled - before
		   adding a new tone. */
		l = cw_get_tone_queue_length();
		expected = (i - 1);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length(): pre:");
		// n = printf("libcw: cw_get_tone_queue_length(): pre-queue: expected %d != result %d:", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Add a tone to queue. All frequencies should be
		   within allowed range, so there should be no
		   error. */
		f = cw_min + i * delta_f;
		failure = !cw_queue_tone(duration, f);

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_queue_tone():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Monitor length of a queue as it is filled - after
		   adding a new tone. */
		l = cw_get_tone_queue_length();
		expected = (i - 1) + 1;
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length(): post:");
		// n = printf("libcw: cw_get_tone_queue_length(): post-queue: expected %d != result %d:", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Above we have queued N tones. libcw starts dequeueing first
	   of them before the last one is enqueued. This is why below
	   we should only check for N-1 of them. Additionally, let's
	   wait a moment till dequeueing of the first tone is without
	   a question in progress. */

	usleep(duration / 4);

	/* And this is the proper test - waiting for dequeueing tones. */
	printf("libcw: dequeueing (1):\n");
	for (int i = 1; i < N; i++) {

		/* Monitor length of a queue as it is emptied - before dequeueing. */
		l = cw_get_tone_queue_length();
		expected = N - i;
		//printf("libcw: cw_get_tone_queue_length(): pre:  l = %d\n", l);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length(): pre:");
		// n = printf("libcw: cw_get_tone_queue_length(): pre-dequeue:  expected %d != result %d: failure\n", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Wait for each of N tones to be dequeued. */
		failure = !cw_wait_for_tone();

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_wait_for_tone():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Monitor length of a queue as it is emptied - after dequeueing. */
		l = cw_get_tone_queue_length();
		expected = N - i - 1;
		//printf("libcw: cw_get_tone_queue_length(): post: l = %d\n", l);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length(): post:");
		// n = printf("libcw: cw_get_tone_queue_length(): post-dequeue: expected %d != result %d: failure\n", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test 2: fill a queue, but this time don't wait for each
	   tone separately, but wait for a whole queue to become
	   empty. */
	failure = false;
	printf("libcw: enqueueing (2):\n");
	f = 0;
	for (int i = 0; i < N; i++) {
		f = cw_min + i * delta_f;
		if (!cw_queue_tone(duration, f)) {
			failure = true;
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_queue_tone(%08d, %04d):", duration, f);
	CW_TEST_PRINT_TEST_RESULT (failure, n);



	printf("libcw: dequeueing (2):\n");

	failure = !cw_wait_for_tone_queue();

	failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_wait_for_tone_queue():");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   Run the complete range of tone generation, at 100Hz intervals,
   first up the octaves, and then down.  If the queue fills, though it
   shouldn't with this amount of data, then pause until it isn't so
   full.

   tests::cw_wait_for_tone()
   tests::cw_queue_tone()
   tests::cw_wait_for_tone_queue()
*/
void test_tone_queue_2(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	cw_set_volume(70);
	int duration = 40000;

	int cw_min, cw_max;
	cw_get_frequency_limits(&cw_min, &cw_max);

	bool wait_failure = false;
	bool queue_failure = false;

	for (int i = cw_min; i < cw_max; i += 100) {
		while (cw_is_tone_queue_full()) {
			if (!cw_wait_for_tone()) {
				wait_failure = true;
				break;
			}
		}

		if (!cw_queue_tone(duration, i)) {
			queue_failure = true;
			break;
		}
	}

	for (int i = cw_max; i > cw_min; i -= 100) {
		while (cw_is_tone_queue_full()) {
			if (!cw_wait_for_tone()) {
				wait_failure = true;
				break;
			}
		}
		if (!cw_queue_tone(duration, i)) {
			queue_failure = true;
			break;
		}
	}


	queue_failure ? stats->failures++ : stats->successes++;
	int n = printf("libcw: cw_queue_tone():");
	CW_TEST_PRINT_TEST_RESULT (queue_failure, n);


	wait_failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_wait_for_tone():");
	CW_TEST_PRINT_TEST_RESULT (wait_failure, n);


	bool wait_tq_failure = !cw_wait_for_tone_queue();
	wait_tq_failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_wait_for_tone_queue():");
	CW_TEST_PRINT_TEST_RESULT (wait_tq_failure, n);


	cw_queue_tone(0, 0);
	cw_wait_for_tone_queue();


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   Test the tone queue manipulations, ensuring that we can fill the
   queue, that it looks full when it is, and that we can flush it all
   again afterwards, and recover.

   tests::cw_get_tone_queue_capacity()
   tests::cw_get_tone_queue_length()
   tests::cw_queue_tone()
   tests::cw_wait_for_tone_queue()
*/
void test_tone_queue_3(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Small setup. */
	cw_set_volume(70);



	/* Test: properties (capacity and length) of empty tq. */
	{
		fprintf(stderr, "libcw:  --  initial test on empty tq:\n");

		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_flush_tone_queue();
		cw_wait_for_tone_queue();

		int capacity = cw_get_tone_queue_capacity();
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_tone_queue_capacity(): %d %s %d:",
			       capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		int len_empty = cw_get_tone_queue_length();
		failure = len_empty > 0;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length() when tq empty: %d %s 0:", len_empty, failure ? "!=" : "==");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: properties (capacity and length) of full tq. */

	/* FIXME: we call cw_queue_tone() until tq is full, and then
	   expect the tq to be full while we perform tests. Doesn't
	   the tq start dequeuing tones right away? Can we expect the
	   tq to be full for some time after adding last tone?
	   Hint: check when a length of tq is decreased. Probably
	   after playing first tone on tq, which - in this test - is
	   pretty long. Or perhaps not. */
	{
		fprintf(stderr, "libcw:  --  test on full tq:\n");

		int i = 0;
		/* FIXME: cw_is_tone_queue_full() is not tested */
		while (!cw_is_tone_queue_full()) {
			cw_queue_tone(1000000, 100 + (i++ & 1) * 100);
		}

		int capacity = cw_get_tone_queue_capacity();
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_tone_queue_capacity(): %d %s %d:",
			       capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		int len_full = cw_get_tone_queue_length();
		failure = len_full != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length() when tq full: %d %s %d:",
			   len_full, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: attempt to add tone to full queue. */
	{
		errno = 0;
		int status = cw_queue_tone(1000000, 100);
		bool failure = status || errno != EAGAIN;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_queue_tone() for full tq:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: check again properties (capacity and length) of empty
	   tq after it has been in use.

	   Empty the tq, ensure that it is empty, and do the test. */
	{
		fprintf(stderr, "libcw:  --  final test on empty tq:\n");

		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_flush_tone_queue();
		cw_wait_for_tone_queue();

		int capacity = cw_get_tone_queue_capacity();
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_tone_queue_capacity(): %d %s %d:",
			       capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test that the tq is really empty after
		   cw_wait_for_tone_queue() has returned. */
		int len_empty = cw_get_tone_queue_length();
		failure = len_empty > 0;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length() when tq empty: %d %s 0:", len_empty, failure ? "!=" : "==");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





static int cw_test_tone_queue_callback_data = 999999;
static int cw_test_helper_tq_callback_capture = false;


/**
   tests::cw_register_tone_queue_low_callback()
*/
void test_tone_queue_callback(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	for (int i = 1; i < 10; i++) {
		/* Test the callback mechanism for very small values,
		   but for a bit larger as well. */
		int level = i <= 5 ? i : 10 * i;

		int rv = cw_register_tone_queue_low_callback(cw_test_helper_tq_callback, (void *) &cw_test_tone_queue_callback_data, level);
		bool failure = rv == CW_FAILURE;
		sleep(1);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_register_tone_queue_low_callback(): %d:", level);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Add a lot of tones to tone queue. "a lot" means three times more than a value of trigger level. */
		for (int j = 0; j < 3 * level; j++) {
			int duration = 10000;
			int f = 440;
			rv = cw_queue_tone(duration, f);
			assert (rv);
		}

		/* Allow the callback to work only after initial
		   filling of queue. */
		cw_test_helper_tq_callback_capture = true;

		/* Wait for the queue to be drained to zero. While the
		   tq is drained, and level of tq reaches trigger
		   level, a callback will be called. Its only task is
		   to copy the current level (tq level at time of
		   calling the callback) value into
		   cw_test_tone_queue_callback_data.

		   Since the value of trigger level is different in
		   consecutive iterations of loop, we can test the
		   callback for different values of trigger level. */
		cw_wait_for_tone_queue();

		/* Because of order of calling callback and decreasing
		   length of queue, I think that it's safe to assume
		   that there may be a difference of 1 between these
		   two values. */
		int diff = level - cw_test_tone_queue_callback_data;
		failure = diff > 1;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: tone queue callback: %d", level);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_reset_tone_queue();
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





static void cw_test_helper_tq_callback(void *data)
{
	if (cw_test_helper_tq_callback_capture) {
	int *d = (int *) data;
	*d = cw_get_tone_queue_length();

		cw_test_helper_tq_callback_capture = false;
	}

	return;
}





/**
   \brief Test control of volume

   Fill tone queue with short tones, then check that we can move the
   volume through its entire range.  Flush the queue when complete.

   tests::cw_get_volume_limits()
   tests::cw_set_volume()
   tests::cw_get_volume()
*/
void test_volume_functions(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int cw_min = -1, cw_max = -1;

	/* Test: get range of allowed volumes. */
	{
		cw_get_volume_limits(&cw_min, &cw_max);

		bool failure = cw_min != CW_VOLUME_MIN
			|| cw_max != CW_VOLUME_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(stderr, "libcw: cw_get_volume_limits(): %d, %d", cw_min, cw_max);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: decrease volume from max to low. */
	{
		/* Fill the tone queue with valid tones. */
		while (!cw_is_tone_queue_full()) {
			cw_queue_tone(100000, 440);
		}

		bool set_failure = false;
		bool get_failure = false;

		/* TODO: why call the cw_wait_for_tone() at the
		   beginning and end of loop's body? */
		for (int i = cw_max; i >= cw_min; i -= 10) {
			cw_wait_for_tone();
			if (!cw_set_volume(i)) {
				set_failure = true;
				break;
			}

			if (cw_get_volume() != i) {
				get_failure = true;
				break;
			}

			cw_wait_for_tone();
		}

		set_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(stderr, "libcw: cw_set_volume() (down):");
		CW_TEST_PRINT_TEST_RESULT (set_failure, n);

		get_failure ? stats->failures++ : stats->successes++;
		n = fprintf(stderr, "libcw: cw_get_volume() (down):");
		CW_TEST_PRINT_TEST_RESULT (get_failure, n);
	}



	/* Test: increase volume from zero to high. */
	{
		/* Fill tone queue with valid tones. */
		while (!cw_is_tone_queue_full()) {
			cw_queue_tone(100000, 440);
		}

		bool set_failure = false;
		bool get_failure = false;

		/* TODO: why call the cw_wait_for_tone() at the
		   beginning and end of loop's body? */
		for (int i = cw_min; i <= cw_max; i += 10) {
			cw_wait_for_tone();
			if (!cw_set_volume(i)) {
				set_failure = true;
				break;
			}

			if (cw_get_volume() != i) {
				get_failure = true;
				break;
			}
			cw_wait_for_tone();
		}

		set_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(stderr, "libcw: cw_set_volume() (up):");
		CW_TEST_PRINT_TEST_RESULT (set_failure, n);

		get_failure ? stats->failures++ : stats->successes++;
		n = fprintf(stderr, "libcw: cw_get_volume() (up):");
		CW_TEST_PRINT_TEST_RESULT (get_failure, n);
	}

	cw_wait_for_tone();
	cw_flush_tone_queue();


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   \brief Test functions looking up characters and their representation.

   tests::cw_get_character_count()
   tests::cw_list_characters()
   tests::cw_get_maximum_representation_length()
   tests::cw_character_to_representation()
   tests::cw_representation_to_character()
*/
void test_character_lookups(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);


	int count = 0; /* Number of characters. */

	/* Test: get number of characters known to libcw. */
	{
		/* libcw doesn't define a constant describing the
		   number of known/supported/recognized characters,
		   but there is a function calculating the number. One
		   thing is certain: the number is larger than
		   zero. */
		count = cw_get_character_count();
		bool failure = count <= 0;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_character_count(): %d:", count);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	char charlist[UCHAR_MAX + 1];
	/* Test: get list of characters supported by libcw. */
	{
		/* Of course length of the list must match the
		   character count discovered above. */

		cw_list_characters(charlist);
		printf("libcw: cw_list_characters():\n"
		       "libcw:     %s\n", charlist);
		size_t len = strlen(charlist);
		bool failure = count != (int) len;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: character count - character list len: %d - %d", count, (int) len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: get maximum length of a representation (a string of dots/dashes). */
	{
		/* This test is rather not related to any other, but
		   since we are doing tests of other functions related
		   to representations, let's do this as well. */

		int rep_len = cw_get_maximum_representation_length();
		bool failure = rep_len <= 0;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_maximum_representation_length(): %d:", rep_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: character <--> representation lookup. */
	{
		/* For each character, look up its representation, the
		   look up each representation in the opposite
		   direction. */

		bool c2r_failure = false;
		bool r2c_failure = false;
		bool two_way_failure = false;

		for (int i = 0; charlist[i] != '\0'; i++) {
			char *representation = cw_character_to_representation(charlist[i]);

			/* Here we get a representation of an input char 'charlist[i]'. */
			if (!representation) {
				c2r_failure = true;
				break;
			}

			/* Here we convert the representation into an output char 'c'. */
			char c = cw_representation_to_character(representation);
			if (!c) {
				r2c_failure = true;
				break;
			}

			/* Compare output char with input char. */
			if (charlist[i] != c) {
				two_way_failure = true;
				break;
			}

			if (representation) {
				free(representation);
				representation = NULL;
			}
		}

		c2r_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_character_to_representation():");
		CW_TEST_PRINT_TEST_RESULT (c2r_failure, n);


		r2c_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_representation_to_character():");
		CW_TEST_PRINT_TEST_RESULT (r2c_failure, n);


		two_way_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: two-way lookup:");
		CW_TEST_PRINT_TEST_RESULT (two_way_failure, n);

	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   \brief Test functions looking up procedural characters and their representation.

   tests::cw_get_procedural_character_count()
   tests::cw_list_procedural_characters()
   tests::cw_get_maximum_procedural_expansion_length()
   tests::cw_lookup_procedural_character()
*/
void test_prosign_lookups(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Collect and print out a list of characters in the
	   procedural signals expansion table. */

	int count = 0; /* Number of prosigns. */

	/* Test: get number of prosigns known to libcw. */
	{
		count = cw_get_procedural_character_count();
		bool failure = count <= 0;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_procedural_character_count(): %d", count);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	char charlist[UCHAR_MAX + 1];
	/* Test: get list of characters supported by libcw. */
	{
		cw_list_procedural_characters(charlist);
		printf("libcw: cw_list_procedural_characters():\n"
		       "libcw:     %s\n", charlist);
		size_t len = strlen(charlist);
		bool failure = count != (int) len;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: character count - character list len: %d - %d", count, (int) len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: expansion length. */
	{
		int exp_len = cw_get_maximum_procedural_expansion_length();
		bool failure = exp_len <= 0;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_maximum_procedural_expansion_length(): %d", exp_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: lookup. */
	{
		/* For each procedural character, look up its
		   expansion and check for two or three characters,
		   and a true/false assignment to the display hint. */

		bool lookup_failure = false;
		bool len_failure = false;

		for (int i = 0; charlist[i] != '\0'; i++) {
			char expansion[256];
			int is_usually_expanded = -1;

			if (!cw_lookup_procedural_character(charlist[i],
							    expansion,
							    &is_usually_expanded)) {
				lookup_failure = true;
				break;
			}

			/* TODO: comment, please. */
			if ((strlen(expansion) != 2 && strlen(expansion) != 3)
			    || is_usually_expanded == -1) {

				len_failure = true;
				break;
			}
		}

		lookup_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_lookup_procedural_character():");
		CW_TEST_PRINT_TEST_RESULT (lookup_failure, n);

		len_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_lookup_procedural_() mapping:");
		CW_TEST_PRINT_TEST_RESULT (len_failure, n);

	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   tests::cw_get_maximum_phonetic_length()
   tests::cw_lookup_phonetic()
*/
void test_phonetic_lookups(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* For each ASCII character, look up its phonetic and check
	   for a string that start with this character, if alphabetic,
	   and false otherwise. */

	/* Test: check that maximum phonetic length is larger than
	   zero. */
	{
		int len = cw_get_maximum_phonetic_length();
		bool failure = len <= 0;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_maximum_phonetic_length(): %d", len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: lookup of phonetic + reverse lookup. */
	{
		bool lookup_failure = false;
		bool reverse_lookup_failure = false;

		for (int i = 0; i < UCHAR_MAX; i++) {
			char phonetic[256];

			int status = cw_lookup_phonetic((char) i, phonetic);
			if (status != (bool) isalpha(i)) {
				/* cw_lookup_phonetic() returns CW_SUCCESS
				   only for letters from ASCII set. */
				lookup_failure = true;
				break;
			}

			if (status) {
				/* We have looked up a letter, it has a
				   phonetic.  Almost by definition, the first
				   letter of phonetic should be the same as
				   the looked up letter. */
				if (phonetic[0] != toupper(i)) {
					reverse_lookup_failure = true;
					break;
				}
			}
		}

		lookup_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_lookup_phonetic():");
		CW_TEST_PRINT_TEST_RESULT (lookup_failure, n);

		reverse_lookup_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: reverse lookup:");
		CW_TEST_PRINT_TEST_RESULT (reverse_lookup_failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   \brief Test enqueueing and playing most basic elements of Morse code

   tests::cw_send_dot()
   tests::cw_send_dash()
   tests::cw_send_character_space()
   tests::cw_send_word_space()
*/
void test_send_primitives(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int N = 20;

	/* Test: sending dot. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (!cw_send_dot()) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_dot():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending dash. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (!cw_send_dash()) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_dash():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending character space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (!cw_send_character_space()) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_character_space():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending word space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (!cw_send_word_space()) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_word_space():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   \brief Testing and playing representations of characters

   tests::cw_representation_is_valid()
   tests::cw_send_representation()
   tests::cw_send_representation_partial()
*/
void test_representations(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Test: validating valid representations. */
	{
		bool failure = !cw_representation_is_valid(".-.-.-")
			|| !cw_representation_is_valid(".-")
			|| !cw_representation_is_valid("---")
			|| !cw_representation_is_valid("...-");

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_representation_is_valid(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: validating invalid representations. */
	{
		bool failure = cw_representation_is_valid("INVALID")
			|| cw_representation_is_valid("_._")
			|| cw_representation_is_valid("-_-");

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_representation_is_valid(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending valid representations. */
	{
		bool failure = !cw_send_representation(".-.-.-")
			|| !cw_send_representation(".-")
			|| !cw_send_representation("---")
			|| !cw_send_representation("...-");

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_representation(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending invalid representations. */
	{
		bool failure = cw_send_representation("INVALID")
			|| cw_send_representation("_._")
			|| cw_send_representation("-_-");

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_representation(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending partial representation of a valid string. */
	{
		bool failure = !cw_send_representation_partial(".-.-.-");

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_representation_partial():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	cw_wait_for_tone_queue();


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   Validate all supported characters, first each characters individually, then as a string.

   tests::cw_character_is_valid()
   tests::cw_string_is_valid()
*/
void test_validate_character_and_string(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Test: validation of individual characters. */
	{
		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);

		bool valid_failure = false;
		bool invalid_failure = false;
		for (int i = 0; i < UCHAR_MAX; i++) {
			if (i == ' '
			    || (i != 0 && strchr(charlist, toupper(i)) != NULL)) {

				/* Here we have a valid character, that is
				   recognized/supported as 'sendable' by
				   libcw.  cw_character_is_valid() should
				   confirm it. */
				if (!cw_character_is_valid(i)) {
					valid_failure = true;
					break;
				}
			} else {
				/* The 'i' character is not
				   recognized/supported by libcw.
				   cw_character_is_valid() should return false
				   to signify that the char is invalid. */
				if (cw_character_is_valid(i)) {
					invalid_failure = true;
					break;
				}
			}
		}

		valid_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_character_is_valid(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (valid_failure, n);

		invalid_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_character_is_valid(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (invalid_failure, n);
	}



	/* Test: validation of string as a whole. */
	{
		/* Check the whole charlist item as a single string,
		   then check a known invalid string. */

		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);
		bool failure = !cw_string_is_valid(charlist);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_string_is_valid(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		/* Test invalid string. */
		failure = cw_string_is_valid("%INVALID%");

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_string_is_valid(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   Send all supported characters: first as individual characters, and then as a string.

   tests::cw_send_character()
   tests::cw_send_string()
*/
void test_send_character_and_string(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Test: sending all supported characters as individual characters. */
	{
		char charlist[UCHAR_MAX + 1];
		bool failure = false;

		/* Send all the characters from the charlist individually. */
		cw_list_characters(charlist);
		printf("libcw: cw_send_character(<valid>):\n"
		       "libcw:     ");
		for (int i = 0; charlist[i] != '\0'; i++) {
			putchar(charlist[i]);
			fflush(stdout);
			if (!cw_send_character(charlist[i])) {
				failure = true;
				break;
			}
			cw_wait_for_tone_queue();
		}

		putchar('\n');

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_character(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending invalid character. */
	{
		bool failure = cw_send_character(0);
		int n = printf("libcw: cw_send_character(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending all supported characters as single string. */
	{
		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);

		/* Send the complete charlist as a single string. */
		printf("libcw: cw_send_string(<valid>):\n"
		       "libcw:     %s\n", charlist);
		bool failure = !cw_send_string(charlist);

		while (cw_get_tone_queue_length() > 0) {
			printf("libcw: tone queue length %-6d\r", cw_get_tone_queue_length());
			fflush(stdout);
			cw_wait_for_tone();
		}
		printf("libcw: tone queue length %-6d\n", cw_get_tone_queue_length());
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_string(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending invalid string. */
	{
		bool failure = cw_send_string("%INVALID%");
		int n = printf("libcw: cw_send_string(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/* The time values are incremental. First event occurs at time
   t1, second at time t1+t2, third at t1+t2+t3 and so on. That
   way we don't have to worry at which time starts e.g. a
   third dot in 'S', we just need to know lengths of two
   previous dots and lengths of separating spaces.

   Times are for 60 WPM (at least that was what original
   comment said. TODO: verify lengths of elements at 60 WPM.

   Notice that this test data is "raw" data: no jitter
   included in the timing values.  The jitter should be
   applied in separate step, by function call. TODO: apply
   jitter.*/
static const cw_test_receive_data_t TEST_DATA_RAW[] = {
	/*                  ./-    ' '     ./-    ' '     ./-    ' '     ./-    ' '     ./-    ' '     ./-    ' '     ./-       ending space, guard */
	/* ASCII 7bit letters */
	{ 'A', ".-",       { 20000, 20000,  60000,                                                                               60000, 0 }},
	{ 'B', "-...",     { 60000, 20000,  20000, 20000,  20000, 20000,  20000,                                                 60000, 0 }},
	{ 'C', "-.-.",     { 60000, 20000,  20000, 20000,  60000, 20000,  20000,                                                 60000, 0 }},
	{ 'D', "-..",      { 60000, 20000,  20000, 20000,  20000,                                                                60000, 0 }},
	{ 'E', ".",        { 20000,                                                                                              60000, 0 }},
	{ 'F', "..-.",     { 20000, 20000,  20000, 20000,  60000, 20000,  20000,                                                 60000, 0 }},
	{ 'G', "--.",      { 60000, 20000,  60000, 20000,  20000,                                                                60000, 0 }},
	{ 'H', "....",     { 20000, 20000,  20000, 20000,  20000, 20000,  20000,                                                 60000, 0 }},
	{ 'I', "..",       { 20000, 20000,  20000,                                                                               60000, 0 }},
	{ 'J', ".---",     { 20000, 20000,  60000, 20000,  60000, 20000,  60000,                                                 60000, 0 }},
	{ 'K', "-.-",      { 60000, 20000,  20000, 20000,  60000,                                                                60000, 0 }},
	{ 'L', ".-..",     { 20000, 20000,  60000, 20000,  20000, 20000,  20000,                                                 60000, 0 }},
	{ 'M', "--",       { 60000, 20000,  60000,                                                                               60000, 0 }},
	{ 'N', "-.",       { 60000, 20000,  20000,                                                                               60000, 0 }},
	{ 'O', "---",      { 60000, 20000,  60000, 20000,  60000,                                                                60000, 0 }},
	{ 'P', ".--.",     { 20000, 20000,  60000, 20000,  60000, 20000,  20000,                                                 60000, 0 }},
	{ 'Q', "--.-",     { 60000, 20000,  60000, 20000,  20000, 20000,  60000,                                                 60000, 0 }},
	{ 'R', ".-.",      { 20000, 20000,  60000, 20000,  20000,                                                                60000, 0 }},
	{ 'S', "...",      { 20000, 20000,  20000, 20000,  20000,                                                                60000, 0 }},
	{ 'T', "-",        { 60000,                                                                                              60000, 0 }},
	{ 'U', "..-",      { 20000, 20000,  20000, 20000,  60000,                                                                60000, 0 }},
	{ 'V', "...-",     { 20000, 20000,  20000, 20000,  20000, 20000,  60000,                                                 60000, 0 }},
	{ 'W', ".--",      { 20000, 20000,  60000, 20000,  60000,                                                                60000, 0 }},
	{ 'X', "-..-",     { 60000, 20000,  20000, 20000,  20000, 20000,  60000,                                                 60000, 0 }},
	{ 'Y', "-.--",     { 60000, 20000,  20000, 20000,  60000, 20000,  60000,                                                 60000, 0 }},
	{ 'Z', "--..",     { 60000, 20000,  60000, 20000,  20000, 20000,  20000,                                                 60000, 0 }},

	/* Numerals */
	{ '0', "-----",    { 60000, 20000,  60000, 20000,  60000, 20000,  60000, 20000,  60000,                                  60000, 0 }},
	{ '1', ".----",    { 20000, 20000,  60000, 20000,  60000, 20000,  60000, 20000,  60000,                                  60000, 0 }},
	{ '2', "..---",    { 20000, 20000,  20000, 20000,  60000, 20000,  60000, 20000,  60000,                                  60000, 0 }},
	{ '3', "...--",    { 20000, 20000,  20000, 20000,  20000, 20000,  60000, 20000,  60000,                                  60000, 0 }},
	{ '4', "....-",    { 20000, 20000,  20000, 20000,  20000, 20000,  20000, 20000,  60000,                                  60000, 0 }},
	{ '5', ".....",    { 20000, 20000,  20000, 20000,  20000, 20000,  20000, 20000,  20000,                                  60000, 0 }},
	{ '6', "-....",    { 60000, 20000,  20000, 20000,  20000, 20000,  20000, 20000,  20000,                                  60000, 0 }},
	{ '7', "--...",    { 60000, 20000,  60000, 20000,  20000, 20000,  20000, 20000,  20000,                                  60000, 0 }},
	{ '8', "---..",    { 60000, 20000,  60000, 20000,  60000, 20000,  20000, 20000,  20000,                                  60000, 0 }},
	{ '9', "----.",    { 60000, 20000,  60000, 20000,  60000, 20000,  60000, 20000,  20000,                                  60000, 0 }},

	/* Punctuation */
	{ '"', ".-..-.",   { 20000, 20000,  60000, 20000,  20000, 20000,  20000, 20000,  60000, 20000,  20000,                   60000, 0 }},
	{ '\'', ".----.",  { 20000, 20000,  60000, 20000,  60000, 20000,  60000, 20000,  60000, 20000,  20000,                   60000, 0 }},
	{ '$', "...-..-",  { 20000, 20000,  20000, 20000,  20000, 20000,  60000, 20000,  20000, 20000,  20000, 20000,  60000,    60000, 0 }},
	{ '(', "-.--.",    { 60000, 20000,  20000, 20000,  60000, 20000,  60000, 20000,  20000,                                  60000, 0 }},
	{ ')', "-.--.-",   { 60000, 20000,  20000, 20000,  60000, 20000,  60000, 20000,  20000, 20000,  60000,                   60000, 0 }},
	{ '+', ".-.-." ,   { 20000, 20000,  60000, 20000,  20000, 20000,  60000, 20000,  20000,                                  60000, 0 }},
	{ ',', "--..--",   { 60000, 20000,  60000, 20000,  20000, 20000,  20000, 20000,  60000, 20000,  60000,                   60000, 0 }},
	{ '-', "-....-",   { 60000, 20000,  20000, 20000,  20000, 20000,  20000, 20000,  20000, 20000,  60000,                   60000, 0 }},
	{ '.', ".-.-.-",   { 20000, 20000,  60000, 20000,  20000, 20000,  60000, 20000,  20000, 20000,  60000,                   60000, 0 }},
	{ '/', "-..-.",    { 60000, 20000,  20000, 20000,  20000, 20000,  60000, 20000,  20000,                                  60000, 0 }},
	{ ':', "---...",   { 60000, 20000,  60000, 20000,  60000, 20000,  20000, 20000,  20000, 20000,  20000,                   60000, 0 }},
	{ ';', "-.-.-.",   { 60000, 20000,  20000, 20000,  60000, 20000,  20000, 20000,  60000, 20000,  20000,                   60000, 0 }},
	{ '=', "-...-",    { 60000, 20000,  20000, 20000,  20000, 20000,  20000, 20000,  60000,                                  60000, 0 }},
	{ '?', "..--..",   { 20000, 20000,  20000, 20000,  60000, 20000,  60000, 20000,  20000, 20000,  20000,                   60000, 0 }},
	{ '_', "..--.-",   { 20000, 20000,  20000, 20000,  60000, 20000,  60000, 20000,  20000, 20000,  60000,                   60000, 0 }},
	{ '@', ".--.-.",   { 20000, 20000,  60000, 20000,  60000, 20000,  20000, 20000,  60000, 20000,  20000,                   60000, 0 }},

	/* ISO 8859-1 accented characters */
	{ '\334', "..--",  { 20000, 20000,  20000, 20000,  60000, 20000,  60000,                                                 60000, 0 }},   /* U with diaeresis */
	{ '\304', ".-.-",  { 20000, 20000,  60000, 20000,  20000, 20000,  60000,                                                 60000, 0 }},   /* A with diaeresis */
	{ '\307', "-.-..", { 60000, 20000,  20000, 20000,  60000, 20000,  20000, 20000,  20000,                                  60000, 0 }},   /* C with cedilla */
	{ '\326', "---.",  { 60000, 20000,  60000, 20000,  60000, 20000,  20000,                                                 60000, 0 }},   /* O with diaeresis */
	{ '\311', "..-..", { 20000, 20000,  20000, 20000,  60000, 20000,  20000, 20000,  20000,                                  60000, 0 }},   /* E with acute */
	{ '\310', ".-..-", { 20000, 20000,  60000, 20000,  20000, 20000,  20000, 20000,  60000,                                  60000, 0 }},   /* E with grave */
	{ '\300', ".--.-", { 20000, 20000,  60000, 20000,  60000, 20000,  20000, 20000,  60000,                                  60000, 0 }},   /* A with grave */
	{ '\321', "--.--", { 60000, 20000,  60000, 20000,  20000, 20000,  60000, 20000,  60000,                                  60000, 0 }},   /* N with tilde */

	/* ISO 8859-2 accented characters */
	{ '\252', "----",  { 60000, 20000,  60000, 20000,  60000, 20000,  60000,                                                 60000, 0 }},   /* S with cedilla */
	{ '\256', "--..-", { 60000, 20000,  60000, 20000,  20000, 20000,  20000, 20000,  60000,                                  60000, 0 }},   /* Z with dot above */

	/* Non-standard procedural signal extensions to standard CW characters. */
	{ '<', "...-.-",   { 20000, 20000,  20000, 20000,  20000, 20000,  60000, 20000,  20000, 20000,  60000,                   60000, 0 }},    /* VA/SK, end of work */
	{ '>', "-...-.-",  { 60000, 20000,  20000, 20000,  20000, 20000,  20000, 20000,  60000, 20000,  20000, 20000,  60000,    60000, 0 }},    /* BK, break */
	{ '!', "...-." ,   { 20000, 20000,  20000, 20000,  20000, 20000,  60000, 20000,  20000,                                  60000, 0 }},    /* SN, understood */
	{ '&', ".-..." ,   { 20000, 20000,  60000, 20000,  20000, 20000,  20000, 20000,  20000,                                  60000, 0 }},    /* AS, wait */
	{ '^', "-.-.-" ,   { 60000, 20000,  20000, 20000,  60000, 20000,  20000, 20000,  60000,                                  60000, 0 }},    /* KA, starting signal */
	{ '~', ".-.-..",   { 20000, 20000,  60000, 20000,  20000, 20000,  60000, 20000,  20000, 20000,  20000,                   60000, 0 }},    /* AL, paragraph */

	/* This line is exclusively for adaptive receiving speed tracking. */
	{ 'P', ".--.",     { 20000, 20000,  60000, 20000,  60000, 20000,  20000,                                               1200000, -1 }},  /* Includes word end delay (120000 > 5 * 20000), -1 indicator */


#if 0 /* Legacy test units, now we have test data for full set of supported characters defined above. */
	/* Values from old code:
	   Jitter for "space": 111, 222, 333.
	   Jitter for "dot" and "dash": 2346, 3456.

	   The exact values of jitter in old code seem to be
	   random, non-meaningful. I think that for now, until
	   more tests and calculations are performed, the upper values for jitter would be:

	   Jitter for "space": 350
	   Jitter for "dot" and "dash": 3500 */
	{ 'Q', "--.-", { 63456, 20111, 63456, 20111, 23456, 20111, 63456, 60111, 0 } },
	{ 'R', ".-.",  { 17654, 20222, 57654, 20222, 17654, 60222,     0 } },
	{ 'P', ".--.", { 23456, 20333, 63456, 20333, 63456, 20333, 23456, 60333, 0 } },
#endif

#if 0
	{ 'Q', "--.-", { 60000, 20000,  60000, 20000,  20000, 20000, 60000, 60000, 0    } },
	{ 'R', ".-.",  { 30000, 30000,  90000, 30000,  30000, 90000,     0 } },
	{ 'P', ".--.", { 40000, 40000, 120000, 40000, 120000, 40000, 40000,  280000, -1 } },  /* Includes word end delay, -1 indicator */
#endif

	{ ' ', NULL,   { 0 } }
};



#define TEST_ADAPTIVE_RECEIVE_FACTORS_MAX 10

/* Input values of timing parameters are calculated for 60
   WPM. Scaling should produce values no larger than 60 WPM. */
float test_adaptive_receive_factors[TEST_ADAPTIVE_RECEIVE_FACTORS_MAX] =
	{
		60.0 / 60.0,
		60.0 / 60.0,
		60.0 / 60.0,
		55.0 / 60.0,
		55.0 / 60.0,
		55.0 / 60.0,
		50.0 / 60.0,
		50.0 / 60.0,
		45.0 / 60.0,
		45.0 / 60.0
	};





/**
   \brief Test functions related to receiving with fixed speed.
*/
void test_fixed_receive(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);


	/* Test receive functions by spoofing them with a timestamp.
	   Getting the test suite to generate reliable timing events
	   is a little too much work.  Add just a little jitter to the
	   timestamps.  This is a _very_ minimal test, omitting all
	   error states. */
	printf("libcw: cw_get_receive_buffer_capacity(): %d\n", cw_get_receive_buffer_capacity());

	cw_set_receive_speed(60);
	cw_set_tolerance(35);
	cw_disable_adaptive_receive();

	test_helper_receive_tests(false, TEST_DATA_RAW, stats, true);


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   Add jitter to timing parameter

   Add random jitter to parameters marking beginning or end of mark
   (dot/dash) or space. The jitter can be positive or negative.

   Old code added jitter no larger than 350 for space, and no larger
   than for mark. I'm keeping this for now, although one could imagine
   better algorithm for calculating the jitter.

   \param usecs - raw value of timing parameter, without jitter
   \param is_space - tells if value of \p usecs describes space or mark

   \return usecs with added random jitter
*/
int test_fixed_receive_add_jitter(const int usecs, bool is_space)
{
	int r = (rand() % (is_space ? 350 : 3500));   /* Random. */
	r *= ((rand() & 1) ? (-1) : (1));             /* Positive/negative. */

	int jittered = usecs + r;

	//fprintf(stderr, "%s: %d\n", is_space? "SPACE" : "MARK", jittered);

	return jittered;
}





/**
   Scale timing parameters

   Scale values of timing parameters for purposes of testing of adaptive receiving.

   \param usecs - raw value of timing parameter
   \param factor - scaling factor

   \return scaled value of usecs
*/
int test_adaptive_receive_scale(const int usecs, float factor)
{
	int out = usecs * factor;
	//fprintf(stderr, "factor = %f, in usecs = %d, out usecs = %d\n", factor, usecs, out);

	return out;
}





/**
   test_adaptive_receive()
*/
void test_adaptive_receive(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Test adaptive receive functions in much the same sort of
	   way.  Again, this is a _very_ minimal test, omitting all
	   error states. */
	cw_set_receive_speed(45);
	cw_set_tolerance(35);
	cw_enable_adaptive_receive();

	test_helper_receive_tests(true, TEST_DATA_RAW, stats, false);


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   Wrapper for code that is common for both test_fixed_receive() and
   test_adaptive_receive().

   tests::cw_get_receive_buffer_length()
   tests::cw_receive_representation()
   tests::cw_receive_character()
*/
void test_helper_receive_tests(bool adaptive, const cw_test_receive_data_t *data, cw_test_stats_t *stats, bool fixed_speed)
{
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	for (int i = 0; data[i].representation; i++) {

		printf("\nlibcw: testing character #%d:\n", i);

		/* Start sending every character at the beginning of a
		   new second.

		   TODO: here we make an assumption that every
		   character is sent in less than a second. Which is a
		   good assumption when we have a speed of tens of
		   WPM. If the speed will be lower, the assumption
		   will be false. */
		tv.tv_sec++;
		tv.tv_usec = 0;

		/* This loop simulates "key down" and "key up" events
		   in specific moments, and in specific time
		   intervals.

		   key down -> call to cw_start_receive_tone()
		   key up -> call to cw_end_receive_tone()

		   First moment is at 0 seconds 0 microseconds. Time
		   of every following event is calculated by iterating
		   over times specified in data table. */
		int entry;
		for (entry = 0; data[i].usecs[entry] > 0; entry++) {
			entry & 1 ? cw_end_receive_tone(&tv) : cw_start_receive_tone(&tv);
			if (fixed_speed) {
				tv.tv_usec += test_fixed_receive_add_jitter(data[i].usecs[entry], (bool) (entry & 1));
			} else {
				float factor = test_adaptive_receive_factors[i % TEST_ADAPTIVE_RECEIVE_FACTORS_MAX];
				tv.tv_usec += test_adaptive_receive_scale(data[i].usecs[entry], factor);
			}

			if (tv.tv_usec > CW_USECS_PER_SEC) {
				tv.tv_usec %= CW_USECS_PER_SEC;
				tv.tv_sec++;
			}
		}





		/* Test: length of receiver's buffer after adding a
		   representation to receiver's buffer. */
		{
			/* Check number of dots and dashes accumulated in receiver. */
			bool failure = (cw_get_receive_buffer_length()
					!= (int) strlen(data[i].representation));

			failure ? stats->failures++ : stats->successes++;
			int n = printf("libcw: cw_get_receive_buffer_length() <nonempty>:  %d %s %zd",
				       cw_get_receive_buffer_length(),
				       failure ? "!=" : "==",
				       strlen(data[i].representation));
			CW_TEST_PRINT_TEST_RESULT (failure, n);
			if (failure) break;
		}




		/* Test: getting representation from receiver's buffer. */
		char representation[CW_REC_REPRESENTATION_CAPACITY];
		{
			/* Get representation (dots and dashes)
			   accumulated by receiver. Check for
			   errors. */

			bool is_word, is_error;

			/* Notice that we call the function with last
			   timestamp (tv) from input data. The last timestamp
			   in the input data represents end of final space - a
			   space ending a character.

			   With this final passing of "end of space" timestamp
			   to libcw we make a statement, informing libcw about
			   ??? (TODO: about what?).

			   The space length in input data is (3 x dot +
			   jitter). In libcw maximum recognizable length of
			   "end of character" space is 5 x dot. */
			if (!cw_receive_representation(&tv, representation, &is_word, &is_error)) {
				stats->failures++;
				int n = printf("libcw: cw_receive_representation() (1):");
				CW_TEST_PRINT_TEST_RESULT (true, n);
				break;
			}

			if (strcmp(representation, data[i].representation) != 0) {
				stats->failures++;
				fprintf(stderr, "\"%s\"   !=   \"%s\"\n",
					representation, data[i].representation);
				int n = printf("libcw: cw_receive_representation() (2):");
				CW_TEST_PRINT_TEST_RESULT (true, n);
				break;
			}

			if (is_error) {
				stats->failures++;
				int n = printf("libcw: cw_receive_representation() (3):");
				CW_TEST_PRINT_TEST_RESULT (true, n);
				break;
			}

			if (adaptive
			    || data[i].usecs[entry] == -1) { /* The test data row that is exclusively for adaptive speed tracking. */

				if ((data[i].usecs[entry] == 0 && is_word)
				    || (data[i].usecs[entry] < 0 && !is_word)) {

					stats->failures++;
					int n = printf("libcw: cw_receive_representation(): not a %s: ", is_word ? "char" : "word");
					CW_TEST_PRINT_TEST_RESULT (true, n);
					break;
				}
			} else {
				if (is_word) {
					stats->failures++;
					int n = printf("libcw: cw_receive_representation() (4):");
					CW_TEST_PRINT_TEST_RESULT (true, n);
					break;
				}
			}

			stats->successes++;
			int n = printf("libcw: cw_receive_representation():");
			CW_TEST_PRINT_TEST_RESULT (false, n);
		}





		char c;
		/* Test: getting character from receiver's buffer. */
		{
			bool is_word, is_error;

			/* The representation is still held in
			   receiver. Ask receiver for converting the
			   representation to character. */
			if (!cw_receive_character(&tv, &c, &is_word, &is_error)) {
				stats->failures++;
				int n = printf("libcw: cw_receive_character():");
				CW_TEST_PRINT_TEST_RESULT (true, n);
				break;
			}

			if (c != data[i].character) {
				stats->failures++;
				int n = printf("libcw: cw_receive_character():");
				CW_TEST_PRINT_TEST_RESULT (true, n);
				break;
			}

			stats->successes++;
			int n = printf("libcw: cw_receive_character():");
			CW_TEST_PRINT_TEST_RESULT (false, n);
		}







		/* Test: getting length of receiver's representation
		   buffer after cleaning the buffer. */
		{
			/* We have a copy of received representation,
			   we have a copy of character. The receiver
			   no longer needs to store the
			   representation. If I understand this
			   correctly, the call to clear() is necessary
			   to prepare the receiver for receiving next
			   character. */
			cw_clear_receive_buffer();
			bool failure = cw_get_receive_buffer_length() != 0;

			failure ? stats->failures++ : stats->successes++;
			int n = printf("libcw: cw_get_receive_buffer_length() <empty>:");
			CW_TEST_PRINT_TEST_RESULT (failure, n);
			if (failure) break;
		}


		printf("libcw: cw_receive_representation(): <%s>\n", representation);
		printf("libcw: cw_receive_character(): <%c>\n", c);

		if (adaptive) {
			printf("libcw: adaptive speed tracking reports %d wpm\n",
			       cw_get_receive_speed());
		}
	}

	double dot_sd, dash_sd, element_end_sd, character_end_sd;
	cw_get_receive_statistics(&dot_sd, &dash_sd,
				  &element_end_sd, &character_end_sd);
	printf("\n");
	printf("libcw: cw_receive_statistics(): standard deviations:\n");
	printf("                           dot: %.2f\n", dot_sd);
	printf("                          dash: %.2f\n", dash_sd);
	printf("         inter-element spacing: %.2f\n", element_end_sd);
	printf("       inter-character spacing: %.2f\n", character_end_sd);

	cw_reset_receive_statistics();

	return;
}





/**
   tests::cw_notify_keyer_paddle_event()
   tests::cw_wait_for_keyer_element()
   tests::cw_get_keyer_paddles()
*/
void test_keyer(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Perform some tests on the iambic keyer.  The latch finer
	   timing points are not tested here, just the basics - dots,
	   dashes, and alternating dots and dashes. */

	int dot_paddle, dash_paddle;

	/* Test: keying dot. */
	{
		/* Seems like this function calls means "keyer pressed
		   until further notice". First argument is true, so
		   this is a dot. */
		bool failure = !cw_notify_keyer_paddle_event(true, false);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_keyer_paddle_event(true, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dot" paddle is pressed, get 30 "dot"
		   events from the keyer. */
		printf("libcw: testing iambic keyer dots   ");
		fflush(stdout);
		for (int i = 0; i < 30; i++) {
			success = success && cw_wait_for_keyer_element();
			putchar('.');
			fflush(stdout);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_wait_for_keyer_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
		bool failure = !dot_paddle || dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_keyer_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: keying dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Second
		   argument is true, so this is a dash. */

		bool failure = !cw_notify_keyer_paddle_event(false, true);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_keyer_paddle_event(false, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dash" paddle is pressed, get 30 "dash"
		   events from the keyer. */
		printf("libcw: testing iambic keyer dashes ");
		fflush(stdout);
		for (int i = 0; i < 30; i++) {
			success = success && cw_wait_for_keyer_element();
			putchar('-');
			fflush(stdout);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_wait_for_keyer_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
		bool failure = dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: keying alternate dit/dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Both
		   arguments are true, so both paddles are pressed at
		   the same time.*/
		bool failure = !cw_notify_keyer_paddle_event(true, true);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_keyer_paddle_event(true, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		bool success = true;
		printf("libcw: testing iambic alternating  ");
		fflush(stdout);
		for (int i = 0; i < 30; i++) {
			success = success && cw_wait_for_keyer_element();
			putchar('#');
			fflush(stdout);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_wait_for_keyer_element:");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
		bool failure = !dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: set new state of paddles: no paddle pressed. */
	{
		bool failure = !cw_notify_keyer_paddle_event(false, false);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_keyer_paddle_event(false, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_wait_for_keyer();


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   tests::cw_notify_straight_key_event()
   tests::cw_get_straight_key_state()
   tests::cw_is_straight_key_busy()
*/
void test_straight_key(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		/* Not sure why, but we have N calls informing the
		   library that the key is not pressed.  TODO: why we
		   have N identical calls in a row? */
		for (int i = 0; i < 10; i++) {
			if (!cw_notify_straight_key_event(CW_KEY_STATE_OPEN)) {
				event_failure = true;
				break;
			}

			if (cw_get_straight_key_state()) {
				state_failure = true;
				break;
			}

			if (cw_is_straight_key_busy()) {
				busy_failure = true;
				break;
			}
		}

		event_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_straight_key_event(<key open>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_straight_key_state():");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_straight_key_busy():");
		CW_TEST_PRINT_TEST_RESULT (busy_failure, n);
	}



	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		/* Again not sure why we have N identical calls in a
		   row. TODO: why? */
		for (int i = 0; i < 10; i++) {
			if (!cw_notify_straight_key_event(CW_KEY_STATE_CLOSED)) {
				event_failure = true;
				break;
			}

			if (!cw_get_straight_key_state()) {
				state_failure = true;
				break;
			}

			if (!cw_is_straight_key_busy()) {
				busy_failure = true;
				break;
			}
		}


		event_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_straight_key_event(<key closed>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_straight_key_state():");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_straight_key_busy():");
		CW_TEST_PRINT_TEST_RESULT (busy_failure, n);
	}


	sleep(1);


	{
		bool event_failure = false;
		bool state_failure = false;

		/* Even more identical calls. TODO: why? */
		for (int i = 0; i < 10; i++) {
			if (!cw_notify_straight_key_event(CW_KEY_STATE_OPEN)) {
				event_failure = true;
				break;
			}
		}

		event_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_straight_key_event(<key open>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);


		/* The key should be open, the function should return false. */
		int state = cw_get_straight_key_state();
		state_failure = state != CW_KEY_STATE_OPEN;

		state_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_straight_key_state():");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





# if 0
/*
 * cw_test_delayed_release()
 */
void cw_test_delayed_release(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);
	int failures = 0;
	struct timeval start, finish;
	int is_released, delay;

	/* This is slightly tricky to detect, but circumstantial
	   evidence is provided by SIGALRM disposition returning to SIG_DFL. */
	if (!cw_send_character_space()) {
		printf("libcw: ERROR: cw_send_character_space()\n");
		failures++;
	}

	if (gettimeofday(&start, NULL) != 0) {
		printf("libcw: WARNING: gettimeofday failed, test incomplete\n");
		return;
	}
	printf("libcw: waiting for cw_finalization delayed release");
	fflush(stdout);
	do {
		struct sigaction disposition;

		sleep(1);
		if (sigaction(SIGALRM, NULL, &disposition) != 0) {
			printf("libcw: WARNING: sigaction failed, test incomplete\n");
			return;
		}
		is_released = disposition.sa_handler == SIG_DFL;

		if (gettimeofday(&finish, NULL) != 0) {
			printf("libcw: WARNING: gettimeofday failed, test incomplete\n");
			return;
		}

		delay = (finish.tv_sec - start.tv_sec) * 1000000 + finish.tv_usec
			- start.tv_usec;
		putchar('.');
		fflush(stdout);
	}
	while (!is_released && delay < 20000000);
	putchar('\n');

	/* The release should be around 10 seconds after the end of
	   the sent space.  A timeout or two might leak in, reducing
	   it by a bit; we'll be ecstatic with more than five
	   seconds. */
	if (is_released) {
		printf("libcw: cw_finalization delayed release after %d usecs\n", delay);
		if (delay < 5000000) {
			printf("libcw: ERROR: cw_finalization release too quick\n");
			failures++;
		}
	} else {
		printf("libcw: ERROR: cw_finalization release wait timed out\n");
		failures++;
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/*
 * cw_test_signal_handling_callback()
 * cw_test_signal_handling()
 */
static int cw_test_signal_handling_callback_called = false;
void cw_test_signal_handling_callback(int signal_number)
{
	signal_number = 0;
	cw_test_signal_handling_callback_called = true;
}





void cw_test_signal_handling(cw_test_stats_t *stats)
{
	int failures = 0;
	struct sigaction action, disposition;

	/* Test registering, unregistering, and raising SIGUSR1.
	   SIG_IGN and handlers are tested, but not SIG_DFL, because
	   that stops the process. */
	if (cw_unregister_signal_handler(SIGUSR1)) {
		printf("libcw: ERROR: cw_unregister_signal_handler invalid\n");
		failures++;
	}

	if (!cw_register_signal_handler(SIGUSR1,
                                   cw_test_signal_handling_callback)) {
		printf("libcw: ERROR: cw_register_signal_handler failed\n");
		failures++;
	}

	cw_test_signal_handling_callback_called = false;
	raise(SIGUSR1);
	sleep(1);
	if (!cw_test_signal_handling_callback_called) {
		printf("libcw: ERROR: cw_test_signal_handling_callback missed\n");
		failures++;
	}

	if (!cw_register_signal_handler(SIGUSR1, SIG_IGN)) {
		printf("libcw: ERROR: cw_register_signal_handler (overwrite) failed\n");
		failures++;
	}

	cw_test_signal_handling_callback_called = false;
	raise(SIGUSR1);
	sleep(1);
	if (cw_test_signal_handling_callback_called) {
		printf("libcw: ERROR: cw_test_signal_handling_callback called\n");
		failures++;
	}

	if (!cw_unregister_signal_handler(SIGUSR1)) {
		printf("libcw: ERROR: cw_unregister_signal_handler failed\n");
		failures++;
	}

	if (cw_unregister_signal_handler(SIGUSR1)) {
		printf("libcw: ERROR: cw_unregister_signal_handler invalid\n");
		failures++;
	}

	action.sa_handler = cw_test_signal_handling_callback;
	action.sa_flags = SA_RESTART;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGUSR1, &action, &disposition) != 0) {
		printf("libcw: WARNING: sigaction failed, test incomplete\n");
		return failures;
	}
	if (cw_register_signal_handler(SIGUSR1, SIG_IGN)) {
		printf("libcw: ERROR: cw_register_signal_handler clobbered\n");
		failures++;
	}
	if (sigaction(SIGUSR1, &disposition, NULL) != 0) {
		printf("libcw: WARNING: sigaction failed, test incomplete\n");
		return failures;
	}

	printf("libcw: cw_[un]register_signal_handler tests complete\n");
	return;
}
#endif





/*---------------------------------------------------------------------*/
/*  Unit tests drivers                                                 */
/*---------------------------------------------------------------------*/





/**
   \brief Set up common test conditions

   Run before each individual test, to handle setup of common test conditions.
*/
void cw_test_setup(void)
{
	cw_reset_send_receive_parameters();
	cw_set_send_speed(30);
	cw_set_receive_speed(30);
	cw_disable_adaptive_receive();
	cw_reset_receive_statistics();
	cw_unregister_signal_handler(SIGUSR1);
	errno = 0;

	return;
}




/* Tests that don't depend on any audio system being open. */
static void (*const CW_TEST_FUNCTIONS_INDEP[])(cw_test_stats_t *) = {
	test_cw_version,
	test_cw_license,
	test_cw_debug_flags,
	test_cw_get_x_limits,
	test_character_lookups,
	test_prosign_lookups,
	test_phonetic_lookups,
	NULL
};




/* Tests that are dependent on a sound system being configured. */
static void (*const CW_TEST_FUNCTIONS_DEP[])(cw_test_stats_t *) = {
	test_parameter_ranges,
	test_tone_queue_0,
	test_tone_queue_1,
	test_tone_queue_2,
	test_tone_queue_3,
	test_tone_queue_callback,
	test_volume_functions,
	test_send_primitives,
	test_representations,
	test_validate_character_and_string,
	test_send_character_and_string,
	test_fixed_receive,
	test_adaptive_receive,
	test_keyer,
	test_straight_key,
	//cw_test_delayed_release,
	//cw_test_signal_handling, /* FIXME - not sure why this test fails :( */
	NULL
};





/**
   \brief Run tests for given audio system.

   Perform a series of self-tests on library public interfaces, using
   audio system specified with \p audio_system. Range of tests is specified
   with \p testset.

   \param audio_system - audio system to use for tests
   \param testset - set of tests to be performed

   \return -1 on failure to set up tests
   \return 0 if tests were run, and no errors occurred
   \return 1 if tests were run, and some errors occurred
*/
int cw_test_dependent_with(int audio_system, cw_test_stats_t *stats)
{
	int rv = cw_generator_new(audio_system, NULL);
	if (rv != 1) {
		fprintf(stderr, "libcw: can't create generator, stopping the test\n");
		return -1;
	}
	rv = cw_generator_start();
	if (rv != 1) {
		fprintf(stderr, "libcw: can't start generator, stopping the test\n");
		cw_generator_delete();
		return -1;
	}

	for (int test = 0; CW_TEST_FUNCTIONS_DEP[test]; test++) {
		cw_test_setup();
		(*CW_TEST_FUNCTIONS_DEP[test])(stats);
	}

	sleep(1);
	cw_generator_stop();
	sleep(1);
	cw_generator_delete();

	/* All tests done; return success if no failures,
	   otherwise return an error status code. */
	return stats->failures ? 1 : 0;
}





int cw_test_independent(void)
{
	fprintf(stderr, "========================================\n");
	fprintf(stderr, "libcw: testing functions independent from audio system\n");

	for (int test = 0; CW_TEST_FUNCTIONS_INDEP[test]; test++) {
		(*CW_TEST_FUNCTIONS_INDEP[test])(&cw_stats_indep);
	}

	sleep(1);

	return cw_stats_indep.failures ? 1 : 0;
}





/**
   \brief Run a series of tests for specified audio systems

   Function attempts to run a set of testcases for every audio system
   specified in \p audio_systems. These testcases require some kind
   of audio system configured. The function calls cw_test_dependent_with()
   to do the configuration and run the tests.

   \p audio_systems is a list of audio systems to be tested: "ncoap".
   Pass NULL pointer to attempt to test all of audio systems supported
   by libcw.

   \param audio_systems - list of audio systems to be tested
*/
int cw_test_dependent(const char *audio_systems)
{
	int n = 0, c = 0, o = 0, a = 0, p = 0;


	if (!audio_systems || strstr(audio_systems, "n")) {
		if (cw_is_null_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with null output\n");
			n = cw_test_dependent_with(CW_AUDIO_NULL, &cw_stats_null);
		} else {
			fprintf(stderr, "libcw: null output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "c")) {
		if (cw_is_console_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with console output\n");
			c = cw_test_dependent_with(CW_AUDIO_CONSOLE, &cw_stats_console);
		} else {
			fprintf(stderr, "libcw: console output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "o")) {
		if (cw_is_oss_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with OSS output\n");
			o = cw_test_dependent_with(CW_AUDIO_OSS, &cw_stats_oss);
		} else {
			fprintf(stderr, "libcw: OSS output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "a")) {
		if (cw_is_alsa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with ALSA output\n");
			a = cw_test_dependent_with(CW_AUDIO_ALSA, &cw_stats_alsa);
		} else {
			fprintf(stderr, "libcw: Alsa output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "p")) {
		if (cw_is_pa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with PulseAudio output\n");
			p = cw_test_dependent_with(CW_AUDIO_PA, &cw_stats_pa);
		} else {
			fprintf(stderr, "libcw: PulseAudio output not available\n");
		}
	}

	if (!n && !c && !o && !a && !p) {
		return 0;
	} else {
		return -1;
	}
}





/**
   \return EXIT_SUCCESS if all tests complete successfully,
   \return EXIT_FAILURE otherwise
*/
int main(int argc, char *const argv[])
{
	static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };

	unsigned int testset = 0;

	struct timeval seed;
	gettimeofday(&seed, NULL);
	fprintf(stderr, "seed: %d\n", (int) seed.tv_usec);
	srand(seed.tv_usec);

	/* Obtain a bitmask of the tests to run from the command line
	   arguments. If none, then default to ~0, which effectively
	   requests all tests. */
	if (argc > 1) {
		testset = 0;
		for (int arg = 1; arg < argc; arg++) {
			unsigned int test = strtoul(argv[arg], NULL, 0);
			testset |= 1 << test;
		}
	} else {
		testset = ~0;
	}

#define CW_SYSTEMS_MAX 5
	char sound_systems[CW_SYSTEMS_MAX + 1];

	if (!cw_test_args(argc, argv, sound_systems, CW_SYSTEMS_MAX)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}

	atexit(cw_test_print_stats);

	/* Arrange for the test to exit on a range of signals. */
	for (int i = 0; SIGNALS[i] != 0; i++) {
		if (!cw_register_signal_handler(SIGNALS[i], SIG_DFL)) {
			fprintf(stderr, "libcw: ERROR: cw_register_signal_handler\n");
			exit(EXIT_FAILURE);
		}
	}

	int rv1 = cw_test_independent();
	int rv2 = cw_test_dependent(sound_systems);

	return rv1 == 0 && rv2 == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}





void cw_test_print_stats(void)
{
	printf("\n\nlibcw: Statistics of tests:\n\n");

	printf("libcw: Tests not requiring any audio system:            ");
	if (cw_stats_indep.failures + cw_stats_indep.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_indep.failures, cw_stats_indep.failures + cw_stats_indep.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with NULL audio system:          ");
	if (cw_stats_null.failures + cw_stats_null.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_null.failures, cw_stats_null.failures + cw_stats_null.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with console audio system:       ");
	if (cw_stats_console.failures + cw_stats_console.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_console.failures, cw_stats_console.failures + cw_stats_console.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with OSS audio system:           ");
	if (cw_stats_oss.failures + cw_stats_oss.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_oss.failures, cw_stats_oss.failures + cw_stats_oss.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with ALSA audio system:          ");
	if (cw_stats_alsa.failures + cw_stats_alsa.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_alsa.failures, cw_stats_alsa.failures + cw_stats_alsa.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with PulseAudio audio system:    ");
	if (cw_stats_pa.failures + cw_stats_pa.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_pa.failures, cw_stats_pa.failures + cw_stats_pa.successes);
	} else {
		printf("no tests were performed\n");
	}

	return;
}
