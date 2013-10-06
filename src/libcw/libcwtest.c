/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2013  Kamil Ignacak (acerion@wp.pl)
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
#include <getopt.h>
#include <assert.h>


#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "libcw.h"
#include "libcw_debug.h"


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
static void cw_test_print_help(const char *progname);

static int  cw_test_args(int argc, char *const argv[], char *sound_systems, size_t systems_max);

typedef struct {
	const char character;
	const char *const representation;
	const int usecs[9];
} cw_test_receive_data_t;

static void cw_test_helper_receive_tests(bool adaptive, const cw_test_receive_data_t *data, cw_test_stats_t *stats);
static void cw_test_helper_tq_callback(void *data);

static void cw_test_version_license(cw_test_stats_t *stats);
static void cw_test_debug_flags(cw_test_stats_t *stats);
static void cw_test_limits(cw_test_stats_t *stats);
static void cw_test_ranges(cw_test_stats_t *stats);
static void cw_test_tone_parameters(cw_test_stats_t *stats);
static void cw_test_tone_queue_1(cw_test_stats_t *stats);
static void cw_test_tone_queue_2(cw_test_stats_t *stats);
static void cw_test_tone_queue_3(cw_test_stats_t *stats);
static void cw_test_tone_queue_callback(cw_test_stats_t *stats);
static void cw_test_volumes(cw_test_stats_t *stats);
static void cw_test_lookups(cw_test_stats_t *stats);
static void cw_test_prosign_lookups(cw_test_stats_t *stats);
static void cw_test_phonetic_lookups(cw_test_stats_t *stats);
static void cw_test_send_primitives(cw_test_stats_t *stats);
static void cw_test_representations(cw_test_stats_t *stats);
static void cw_test_validate_characters_and_string(cw_test_stats_t *stats);
static void cw_test_send_characters_and_string(cw_test_stats_t *stats);
static void cw_test_fixed_receive(cw_test_stats_t *stats);
static void cw_test_adaptive_receive(cw_test_stats_t *stats);
static void cw_test_keyer(cw_test_stats_t *stats);
static void cw_test_straight_key(cw_test_stats_t *stats);
// static void cw_test_delayed_release(cw_test_stats_t *stats);


/*---------------------------------------------------------------------*/
/*  Unit tests                                                         */
/*---------------------------------------------------------------------*/





void cw_test_version_license(__attribute__((unused)) cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Test the cw_version and cw_license functions. */
	fprintf(stderr, "libcw: version %d.%d\n", cw_version() >> 16, cw_version() & 0xff);
	cw_license();

	printf("libcw: %s(): completed\n\n", __func__);

	return;
}





extern cw_debug_t cw_debug_object;


/**
   \brief Test getting and setting of debug flags.
*/
void cw_test_debug_flags(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	uint32_t flags = cw_debug_get_flags(&cw_debug_object);

	bool failure = false;
	for (uint32_t i = 0; i <= CW_DEBUG_MASK; i++) {
		cw_debug_set_flags(&cw_debug_object, i);
		if (cw_debug_get_flags(&cw_debug_object) != i) {
			failure = true;
			break;
		}
	}
	if (failure) {
		printf("libcw: cw_debug_set/get_flags():   failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_debug_set/get_flags():   success\n");
		stats->successes++;
	}

	cw_debug_set_flags(&cw_debug_object, flags);

	printf("libcw: %s(): completed\n\n", __func__);

	return;
}





/**
   \brief Ensure that we can obtain correct values of main parameter limits

   \return zero
*/
void cw_test_limits(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int cw_min_speed = -1, cw_max_speed = -1;
	cw_get_speed_limits(&cw_min_speed, &cw_max_speed);

	if (cw_min_speed != CW_SPEED_MIN || cw_max_speed != CW_SPEED_MAX) {
		printf("libcw: cw_get_speed_limits(): %d,%d:       failure\n", cw_min_speed, cw_max_speed);
		stats->failures++;
	} else {
		printf("libcw: cw_get_speed_limits(): %d,%d:       success\n",
		       cw_min_speed, cw_max_speed);
		stats->successes++;
	}


	int cw_min_frequency = -1, cw_max_frequency = -1;
	cw_get_frequency_limits(&cw_min_frequency, &cw_max_frequency);

	if (cw_min_frequency != CW_FREQUENCY_MIN || cw_max_frequency != CW_FREQUENCY_MAX) {
		printf("libcw: cw_get_frequency_limits(): %d,%d: failure\n",
		       cw_min_frequency, cw_max_frequency);
		stats->failures++;
	} else {
		printf("libcw: cw_get_frequency_limits(): %d,%d: success\n",
		       cw_min_frequency, cw_max_frequency);
		stats->successes++;
	}


	int cw_min_volume = -1, cw_max_volume = -1;
	cw_get_volume_limits(&cw_min_volume, &cw_max_volume);

	if (cw_min_volume != CW_VOLUME_MIN || cw_max_volume != CW_VOLUME_MAX) {
		printf("libcw: cw_get_volume_limits(): %d,%d:     failure\n",
		       cw_min_volume, cw_max_volume);
		stats->failures++;
	} else {
		printf("libcw: cw_get_volume_limits(): %d,%d:     success\n",
		       cw_min_volume, cw_max_volume);
		stats->successes++;
	}


	int cw_min_gap = -1, cw_max_gap = -1;
	cw_get_gap_limits(&cw_min_gap, &cw_max_gap);

	if (cw_min_gap != CW_GAP_MIN || cw_max_gap != CW_GAP_MAX) {
		printf("libcw: cw_get_gap_limits(): %d,%d:         failure\n",
		       cw_min_gap, cw_max_gap);
		stats->failures++;
	} else {
		printf("libcw: cw_get_gap_limits(): %d,%d:         success\n",
		       cw_min_gap, cw_max_gap);
		stats->successes++;
	}


	int cw_min_tolerance = -1, cw_max_tolerance = -1;
	cw_get_tolerance_limits(&cw_min_tolerance, &cw_max_tolerance);

	if (cw_min_tolerance != CW_TOLERANCE_MIN || cw_max_tolerance != CW_TOLERANCE_MAX) {
		printf("libcw: cw_get_tolerance_limits(): %d,%d:   failure\n",
		       cw_min_tolerance, cw_max_tolerance);
		stats->failures++;
	} else {
		printf("libcw: cw_get_tolerance_limits(): %d,%d:   success\n",
		       cw_min_tolerance, cw_max_tolerance);
		stats->successes++;
	}



	int cw_min_weighting = -1, cw_max_weighting = -1;
	cw_get_weighting_limits(&cw_min_weighting, &cw_max_weighting);

	if (cw_min_weighting != CW_WEIGHTING_MIN || cw_max_weighting != CW_WEIGHTING_MAX) {
		printf("libcw: cw_get_weighting_limits(): %d,%d:  failure\n",
		       cw_min_weighting, cw_max_weighting);
		stats->failures++;
	} else {
		printf("libcw: cw_get_weighting_limits(): %d,%d:  success\n",
		       cw_min_weighting, cw_max_weighting);
		stats->successes++;
	}

	printf("libcw: %s(): completed\n\n", __func__);

	return;
}






/*
 * cw_test_ranges()
 */
void cw_test_ranges(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int status, cw_min, cw_max;
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


	/* Set the main parameters to out-of-range values, and through
	   their complete valid ranges.  */
	cw_get_speed_limits(&cw_min, &cw_max);
	errno = 0;
	status = cw_set_send_speed(cw_min - 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_send_speed(cw_min_speed-1):     failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_send_speed(cw_min_speed-1):     success\n");
		stats->successes++;
	}


	errno = 0;
	status = cw_set_send_speed(cw_max + 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_send_speed(cw_max_speed+1):     failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_send_speed(cw_max_speed+1):     success\n");
		stats->successes++;
	}


	bool failure = false;
	for (int i = cw_min; i <= cw_max; i++) {
		cw_set_send_speed(i);
		if (cw_get_send_speed() != i) {
			failure = true;
			break;
		}
	}
	if (failure) {
		printf("libcw: cw_get/set_send_speed():               failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get/set_send_speed():               success\n");
		stats->successes++;
	}
	failure = false;


	errno = 0;
	status = cw_set_receive_speed(cw_min - 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_receive_speed(cw_min_speed-1):  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_receive_speed(cw_min_speed-1):  success\n");
		stats->successes++;
	}


	errno = 0;
	status = cw_set_receive_speed(cw_max + 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_receive_speed(cw_max_speed+1):  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_receive_speed(cw_max_speed+1):  success\n");
		stats->successes++;
	}


	for (int i = cw_min; i <= cw_max; i++) {
		cw_set_receive_speed(i);
		if (cw_get_receive_speed() != i) {
			failure = true;
			break;
		}
	}
	if (failure) {
		printf("libcw: cw_get/set_receive_speed():            failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get/set_receive_speed():            success\n");
		stats->successes++;
	}
	failure = false;


	cw_get_frequency_limits(&cw_min, &cw_max);
	errno = 0;
	status = cw_set_frequency(cw_min - 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_frequency(cw_min_frequency-1):  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_frequency(cw_min_frequency-1):  success\n");
		stats->successes++;
	}


	errno = 0;
	status = cw_set_frequency(cw_max + 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_frequency(cw_max_frequency+1):  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_frequency(cw_max_frequency+1):  success\n");
		stats->successes++;
	}


	for (int i = cw_min; i <= cw_max; i++) {
		cw_set_frequency(i);
		if (cw_get_frequency() != i) {
			failure = true;
			break;
		}
	}
	if (failure) {
		printf("libcw: cw_get/set_frequency():                failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get/set_frequency():                success\n");
		stats->successes++;
	}
	failure = false;


	cw_get_volume_limits(&cw_min, &cw_max);
	errno = 0;
	status = cw_set_volume(cw_min - 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_volume(cw_min_volume-1):        failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_volume(cw_min_volume-1):        success\n");
		stats->successes++;
	}


	errno = 0;
	status = cw_set_volume(cw_max + 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_volume(cw_max_volume+1):        failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_volume(cw_max_volume+1):        success\n");
		stats->successes++;
	}


	for (int i = cw_min; i <= cw_max; i++) {
		cw_set_volume(i);
		if (cw_get_volume() != i) {
			failure = true;
			break;
		}
	}
	if (failure) {
		printf("libcw: cw_get/set_volume():                   failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get/set_volume():                   success\n");
		stats->successes++;
	}
	failure = false;


	cw_get_gap_limits(&cw_min, &cw_max);
	errno = 0;
	status = cw_set_gap(cw_min - 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_gap(cw_min_gap-1):              failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_gap(cw_min_gap-1):              success\n");
		stats->successes++;
	}


	errno = 0;
	status = cw_set_gap(cw_max + 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_gap(cw_max_gap+1):              failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_gap(cw_max_gap+1):              success\n");
		stats->successes++;
	}


	for (int i = cw_min; i <= cw_max; i++) {
		cw_set_gap(i);
		if (cw_get_gap() != i) {
			failure = true;
			break;
		}
	}
	if (failure) {
		printf("libcw: cw_get/set_gap():                      failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get/set_gap():                      success\n");
		stats->successes++;
	}
	failure = false;


	cw_get_tolerance_limits(&cw_min, &cw_max);
	errno = 0;
	status = cw_set_tolerance(cw_min - 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_tolerance(cw_min_tolerance-1):  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_tolerance(cw_min_tolerance-1):  success\n");
		stats->successes++;
	}


	errno = 0;
	status = cw_set_tolerance(cw_max + 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_tolerance(cw_max_tolerance+1):  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_tolerance(cw_max_tolerance+1):  success\n");
		stats->successes++;
	}


	for (int i = cw_min; i <= cw_max; i++) {
		cw_set_tolerance(i);
		if (cw_get_tolerance() != i) {
			failure = true;
			break;
		}
	}
	if (failure) {
		printf("libcw: cw_get/set_tolerance():                failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get/set_tolerance():                success\n");
		stats->successes++;
	}
	failure = false;


	cw_get_weighting_limits(&cw_min, &cw_max);
	errno = 0;
	status = cw_set_weighting(cw_min - 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_weighting(cw_min_weighting-1):  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_weighting(cw_min_weighting-1):  success\n");
		stats->successes++;
	}


	errno = 0;
	status = cw_set_weighting(cw_max + 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_set_weighting(cw_max_weighting+1):  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_set_weighting(cw_max_weighting+1):  success\n");
		stats->successes++;
	}


	for (int i = cw_min; i <= cw_max; i++) {
		cw_set_weighting(i);
		if (cw_get_weighting() != i) {
			failure = true;
			break;
		}
	}
	if (failure) {
		printf("libcw: cw_get/set_weighting():                failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get/set_weighting():                success\n");
		stats->successes++;
	}
	failure = false;

	printf("libcw: %s(): completed\n\n", __func__);
	return;
}





/**
   \brief Test the limits of the parameters to the tone queue routine.

   \return zero
*/
void cw_test_tone_parameters(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int f_min, f_max;
	cw_get_frequency_limits(&f_min, &f_max);

	/* Test 1: invalid duration of tone. */
	errno = 0;
	int status = cw_queue_tone(-1, f_min);
	if (status || errno != EINVAL)  {
		printf("libcw: cw_queue_tone(-1, cw_min_frequency):    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_queue_tone(-1, cw_min_frequency):    success\n");
		stats->successes++;
	}

	/* Test 2: tone's frequency too low. */
	errno = 0;
	status = cw_queue_tone(1, f_min - 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_queue_tone(1, cw_min_frequency - 1): failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_queue_tone(1, cw_min_frequency - 1): success\n");
		stats->successes++;
	}


	/* Test 3: tone's frequency too high. */
	errno = 0;
	status = cw_queue_tone(1, f_max + 1);
	if (status || errno != EINVAL) {
		printf("libcw: cw_queue_tone(1, cw_max_frequency + 1): failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_queue_tone(1, cw_max_frequency + 1): success\n");
		stats->successes++;
	}

	printf("libcw: %s(): completed\n\n", __func__);

	return;
}





/**
   \brief Simple tests of queueing and dequeueing of tones

   Ensure we can generate a few simple tones, and wait for them to end.
*/
void cw_test_tone_queue_1(cw_test_stats_t *stats)
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
	if (!cw_queue_tone(duration, f)) {
		printf("libcw: cw_queue_tone():                  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_queue_tone():                  success\n");
		stats->successes++;
	}


	/* This is to make sure that rest of tones is enqueued when
	   the first tone is being dequeued. */
	usleep(duration / 4);

	/* Enqueue rest of N tones. It is now safe to check length of
	   tone queue before and after queueing each tone: length of
	   the tone queue should increase (there won't be any decrease
	   due to dequeueing of first tone). */
	printf("libcw: enqueueing (1): \n");
	for (int i = 1; i < N; i++) {
		/* Monitor length of a queue as it is filled - before adding a new tone. */
		l = cw_get_tone_queue_length();
		expected = (i - 1);
		if (l != expected) {
			printf("libcw: cw_get_tone_queue_length(): pre:  failure\n");
			//printf("libcw: cw_get_tone_queue_length(): pre-queue: expected %d != result %d: failure\n", expected, l);
			stats->failures++;
		} else {
			printf("libcw: cw_get_tone_queue_length(): pre:  success\n");
			stats->successes++;
		}

		f = cw_min + i * delta_f;
		if (!cw_queue_tone(duration, f)) {
			printf("libcw: cw_queue_tone():                  failure\n");
			stats->failures++;
		} else {
			printf("libcw: cw_queue_tone():                  success\n");
			stats->successes++;
		}

		/* Monitor length of a queue as it is filled - after adding a new tone. */
		l = cw_get_tone_queue_length();
		expected = (i - 1) + 1;
		if (l != expected) {
			printf("libcw: cw_get_tone_queue_length(): post: failure\n");
			//printf("libcw: cw_get_tone_queue_length(): post-queue: expected %d != result %d: failure\n", expected, l);
			stats->failures++;
		} else {
			printf("libcw: cw_get_tone_queue_length(): post: success\n");
			stats->successes++;
		}
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
		if (l != expected) {
			printf("libcw: cw_get_tone_queue_length(): pre:  failure\n");
			//printf("libcw: cw_get_tone_queue_length(): pre-dequeue:  expected %d != result %d: failure\n", expected, l);
			stats->failures++;
		} else {
			printf("libcw: cw_get_tone_queue_length(): pre:  success\n");
			//printf("libcw: cw_get_tone_queue_length(): pre-dequeue:  expected %d == result %d: success\n", expected, l);
			stats->successes++;
		}


		/* Wait for each of N tones to be dequeued. */
		if (!cw_wait_for_tone()) {
			printf("libcw: cw_wait_for_tone():               failure\n");
			stats->failures++;
		} else {
			printf("libcw: cw_wait_for_tone():               success\n");
			stats->successes++;
		}

		/* Monitor length of a queue as it is emptied - after dequeueing. */
		l = cw_get_tone_queue_length();
		expected = N - i - 1;
		//printf("libcw: cw_get_tone_queue_length(): post: l = %d\n", l);
		if (l != expected) {
			printf("libcw: cw_get_tone_queue_length(): post: failure\n");
			// printf("libcw: cw_get_tone_queue_length(): post-dequeue: expected %d != result %d: failure\n", expected, l);
			stats->failures++;
		} else {
			printf("libcw: cw_get_tone_queue_length(): post: success\n");
			//printf("libcw: cw_get_tone_queue_length(): post-dequeue: expected %d == result %d: success\n", expected, l);
			stats->successes++;
		}
	}



	/* Test 2: fill a queue, but this time don't wait for each
	   tone separately, but wait for a whole queue to become
	   empty. */
	bool failure = false;
	printf("libcw: enqueueing (2):\n");
	f = 0;
	for (int i = 0; i < N; i++) {
		f = cw_min + i * delta_f;
		if (!cw_queue_tone(duration, f)) {
			failure = true;
			break;
		}
	}
	if (failure) {
		printf("libcw: cw_queue_tone(%08d, %04d):    failure\n", duration, f);
		stats->failures++;
	} else {
		printf("libcw: cw_queue_tone(%08d, %04d):    success\n", duration, f);
		stats->successes++;
	}
	failure = false;


	printf("libcw: dequeueing (2):\n");

	if (!cw_wait_for_tone_queue()) {
		printf("libcw: cw_wait_for_tone_queue():         failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_wait_for_tone_queue():         success\n");
		stats->successes++;
	}

	printf("libcw: %s():         completed\n\n", __func__);

	return;
}





/**

   Run the complete range of tone generation, at 100Hz intervals,
   first up the octaves, and then down.  If the queue fills, though it
   shouldn't with this amount of data, then pause until it isn't so
   full.
*/
void cw_test_tone_queue_2(cw_test_stats_t *stats)
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

	if (queue_failure) {
		printf("libcw: cw_queue_tone():          failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_queue_tone():          success\n");
		stats->successes++;
	}

	if (wait_failure) {
		printf("libcw: cw_wait_for_tone():       failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_wait_for_tone():       success\n");
		stats->successes++;
	}

	if (!cw_wait_for_tone_queue()) {
		printf("libcw: cw_wait_for_tone_queue(): failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_wait_for_tone_queue(): success\n");
		stats->successes++;
	}

	cw_queue_tone(0, 0);
	cw_wait_for_tone_queue();

	printf("libcw: cw_wait_for_tone_queue(): success\n");
	printf("libcw: %s():  completed\n\n", __func__);

	return;
}





/**
   Test the tone queue manipulations, ensuring that we can fill the
   queue, that it looks full when it is, and that we can flush it all
   again afterwards, and recover.
*/
void cw_test_tone_queue_3(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	cw_set_volume(70);
	int capacity = cw_get_tone_queue_capacity();
	if (capacity == 0) {
		printf("libcw: cw_get_tone_queue_capacity(): %d == 0:                   failure\n", capacity);
		stats->failures++;
	} else {
		printf("libcw: cw_get_tone_queue_capacity(): %d != 0:                   success\n", capacity);
		stats->successes++;
	}

	/* Empty tone queue and make sure that it is really empty
	   (wait for info from libcw). */
	cw_flush_tone_queue();
	cw_wait_for_tone_queue();

	int len_empty = cw_get_tone_queue_length();
	if (len_empty > 0) {
		printf("libcw: cw_get_tone_queue_length() when tq empty: %d != 0:          failure\n", len_empty);
		stats->failures++;
	} else {
		printf("libcw: cw_get_tone_queue_length() when tq empty: %d == 0:          success\n", len_empty);
		stats->successes++;
	}


	int i = 0;
	/* FIXME: cw_is_tone_queue_full() is not tested */
	while (!cw_is_tone_queue_full()) {
		cw_queue_tone(1000000, 100 + (i++ & 1) * 100);
	}

	int len_full = cw_get_tone_queue_length();
	if (len_full != capacity) {
		printf("libcw: cw_get_tone_queue_length() when tq full: %d != capacity: failure\n", len_full);
		stats->failures++;
	} else {
		printf("libcw: cw_get_tone_queue_length() when tq full: %d == capacity: success\n", len_full);
		stats->successes++;
	}

	errno = 0;
	int status = cw_queue_tone(1000000, 100);
	if (status || errno != EAGAIN) {
		printf("libcw: cw_queue_tone() for full tq:                               failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_queue_tone() for full tq:                               success\n");
		stats->successes++;
	}

	/* Empty tone queue and make sure that it is really empty
	   (wait for info from libcw). */
	cw_flush_tone_queue();
	cw_wait_for_tone_queue();

	len_empty = cw_get_tone_queue_length();
	if (len_empty > 0) {
		printf("libcw: cw_get_tone_queue_length() for empty tq: %d:                failure\n", len_empty);
		stats->failures++;
	} else {
		printf("libcw: cw_get_tone_queue_length() for empty tq: %d:                success\n", len_empty);
		stats->successes++;
	}

	printf("libcw: %s(): completed\n\n", __func__);

	return;
}





static int cw_test_tone_queue_callback_data = 999999;



void cw_test_tone_queue_callback(cw_test_stats_t *stats)
{
	for (int i = 1; i < 5; i++) {
		int level = i;
		int rv = cw_register_tone_queue_low_callback(cw_test_helper_tq_callback, (void *) &cw_test_tone_queue_callback_data, level);
		sleep(1);

		if (rv == CW_FAILURE) {
			printf("libcw: cw_register_tone_queue_low_callback():        failure (%d)\n", level);
			stats->failures++;
		} else {
			printf("libcw: cw_register_tone_queue_low_callback():        success (%d)\n", level);
			stats->successes++;
		}

		int duration = 100000;
		int f = 440;

		for (int i = 0; i < 3 * level; i++) {
			rv = cw_queue_tone(duration, f);
			assert (rv);
		}

		cw_wait_for_tone_queue();

		if (level != cw_test_tone_queue_callback_data) {
			printf("libcw: tone queue callback:                          failure (%d)\n", level);
			stats->failures++;
		} else {
			printf("libcw: tone queue callback:                          success (%d)\n", level);
			stats->successes++;
		}

		cw_reset_tone_queue();
	}

	return;
}





static void cw_test_helper_tq_callback(void *data)
{
	int *d = (int *) data;
	*d = cw_get_tone_queue_length();

	return;
}





/**
   \brief Test control of volume

   Fill tone queue with short tones, then check that we can move the
   volume through its entire range.  Flush the queue when complete.
*/
void cw_test_volumes(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int cw_min = -1, cw_max = -1;
	cw_get_volume_limits(&cw_min, &cw_max);

	if (cw_min != CW_VOLUME_MIN || cw_max != CW_VOLUME_MAX) {
		fprintf(stderr, "libcw: cw_get_volume_limits():   failure (%d, %d)\n", cw_min, cw_max);
		stats->failures++;
	} else {
		fprintf(stderr, "libcw: cw_get_volume_limits():   success\n");
		stats->successes++;
	}

	while (!cw_is_tone_queue_full()) {
		cw_queue_tone(100000, 440);
	}


	bool set_failure = false;
	bool get_failure = false;

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


	if (set_failure) {
		fprintf(stderr, "libcw: cw_set_volume() (down):   failure\n");
		stats->failures++;
	} else {
		fprintf(stderr, "libcw: cw_set_volume() (down):   success\n");
		stats->successes++;
	}
	if (get_failure) {
		fprintf(stderr, "libcw: cw_get_volume() (down):   failure\n");
		stats->failures++;
	} else {
		fprintf(stderr, "libcw: cw_get_volume() (down):   success\n");
		stats->successes++;
	}


	set_failure = false;
	get_failure = false;

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
		cw_wait_for_tone ();
	}


	if (set_failure) {
		fprintf(stderr, "libcw: cw_set_volume() (up):     failure\n");
		stats->failures++;
	} else {
		fprintf(stderr, "libcw: cw_set_volume() (up):     success\n");
		stats->successes++;
	}
	if (get_failure) {
		fprintf(stderr, "libcw: cw_get_volume() (up):     failure\n");
		stats->failures++;
	} else {
		fprintf(stderr, "libcw: cw_get_volume() (up):     success\n");
		stats->successes++;
	}


	cw_wait_for_tone();
	cw_flush_tone_queue();

	printf("libcw: %s():      completed\n\n", __func__);
	return;
}





/**
   \brief Test functions looking up characters and their representation.
*/
void cw_test_lookups(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);


	/* Collect and print out a list of characters in the main CW table. */
	int count = cw_get_character_count();
	if (count <= 0) {
		/* There is no constant defining number of characters
		   known to libcw, but surely it is not a non-positive
		   number. */
		printf("libcw: cw_get_character_count():                 failure: %d\n", count);
		stats->failures++;
	} else {
		printf("libcw: cw_get_character_count():                 success: %d\n", count);
		stats->successes++;
	}


	char charlist[UCHAR_MAX + 1];
	cw_list_characters(charlist);
	printf("libcw: cw_list_characters():\n"
	       "libcw:     %s\n", charlist);
	size_t len = strlen(charlist);
	if (count != (int) len) {
		printf("libcw: character count != character list len:    failure: %d != %d\n", count, (int) len);
		stats->failures++;
	} else {
		printf("libcw: character count == character list len:    success: %d == %d\n", count, (int) len);
		stats->successes++;
	}

	/* For each character, look up its representation, the look up
	   each representation in the opposite direction. */
	int rep_len = cw_get_maximum_representation_length();
	if (rep_len <= 0) {
		printf("libcw: cw_get_maximum_representation_length():   failure: %d\n", rep_len);
		stats->failures++;
	} else {
		printf("libcw: cw_get_maximum_representation_length():   success: %d\n", rep_len);
		stats->successes++;
	}

	bool c2r_failure = false;
	bool r2c_failure = false;
	bool compare_failure = false;
	for (int i = 0; charlist[i] != '\0'; i++) {
		char *representation = cw_character_to_representation(charlist[i]);

		/* Here we get a representation of an input char 'charlist[i]'. */
		if (!representation) {
			c2r_failure = true;
			break;
		}

		char c = cw_representation_to_character(representation);
		/* Here we convert the representation into an output char 'c'. */
		if (!c) {
			r2c_failure = true;
			break;
		}

		/* Compare output char with input char. */
		if (charlist[i] != c) {
			compare_failure = true;
			break;
		}

		if (representation) {
			free(representation);
			representation = NULL;
		}
	}

	if (c2r_failure) {
		printf("libcw: cw_character_to_representation():         failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_character_to_representation():         success\n");
		stats->successes++;
	}
	if (r2c_failure) {
		printf("libcw: cw_representation_to_character():         failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_representation_to_character():         success\n");
		stats->successes++;
	}
	if (compare_failure) {
		printf("libcw: two-way lookup:                           failure\n");
		stats->failures++;
	} else {
		printf("libcw: two-way lookup:                           success\n");
		stats->successes++;
	}

	printf("libcw: %s(): completed\n\n", __func__);

	return;
}





/**
   \brief Test functions looking up procedural characters and their representation.
*/
void cw_test_prosign_lookups(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Collect and print out a list of characters in the
	   procedural signals expansion table. */
	int count = cw_get_procedural_character_count();
	if (count <= 0) {
		printf("libcw: cw_get_procedural_character_count():                failure: %d\n", count);
		stats->failures++;
	} else {
		printf("libcw: cw_get_procedural_character_count():                success: %d\n", count);
		stats->successes++;
	}


	char charlist[UCHAR_MAX + 1];
	cw_list_procedural_characters(charlist);
	printf("libcw: cw_list_procedural_characters():\n"
	       "libcw:     %s\n", charlist);
  	size_t len = strlen(charlist);
	if (count != (int) len) {
		printf("libcw: character count != character list len:              failure: %d != %d\n", count, (int) len);
		stats->failures++;
	} else {
		printf("libcw: character count == character list len:              success: %d == %d\n", count, (int) len);
		stats->successes++;
	}

	/* For each character, look up its expansion and check for two or three
	   characters, and a true/false assignment to the display hint. */
	int exp_len = cw_get_maximum_procedural_expansion_length();
	if (exp_len <= 0) {
		printf("libcw: cw_get_maximum_procedural_expansion_length():       failure: %d\n", exp_len);
		stats->failures++;
	} else {
		printf("libcw: cw_get_maximum_procedural_expansion_length():       success: %d\n", exp_len);
		stats->successes++;
	}


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

	if (lookup_failure) {
		printf("libcw: cw_lookup_procedural_character():                   failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_lookup_procedural_character():                   success\n");
		stats->successes++;
	}

	if (len_failure) {
		printf("libcw: cw_lookup_procedural_() mapping:                    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_lookup_procedural_() mapping:                    success\n");
		stats->successes++;
	}

	printf("libcw: %s(): completed\n\n", __func__);
	return;
}





void cw_test_phonetic_lookups(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* For each ASCII character, look up its phonetic and check
	   for a string that start with this character, if alphabetic,
	   and false otherwise. */
	int len = cw_get_maximum_phonetic_length();
	if (len <= 0) {
		printf("libcw: cw_get_maximum_phonetic_length():   failure: %d\n", len);
		stats->failures++;
	} else {
		printf("libcw: cw_get_maximum_phonetic_length():   success: %d\n", len);
		stats->successes++;
	}


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

	if (lookup_failure) {
		printf("libcw: cw_lookup_phonetic():               failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_lookup_phonetic():               success\n");
		stats->successes++;
	}

	if (reverse_lookup_failure) {
		printf("libcw: reverse lookup:                     failure\n");
		stats->failures++;
	} else {
		printf("libcw: reverse lookup:                     success\n");
		stats->successes++;
	}

	printf("libcw: %s(): completed\n\n", __func__);
	return;
}





/**
   \brief Test enqueueing and playing most basic elements of Morse code
*/
void cw_test_send_primitives(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	bool failure = false;
	int N = 20;

	for (int i = 0; i < N; i++) {
		if (!cw_send_dot()) {
			failure = true;
			break;
		}
	}
	cw_wait_for_tone_queue();
	if (failure) {
		printf("libcw: cw_send_dot():               failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_dot():               success\n");
		stats->successes++;
	}
	failure = false;

	for (int i = 0; i < N; i++) {
		if (!cw_send_dash()) {
			failure = true;
			break;
		}
	}
	cw_wait_for_tone_queue();
	if (failure) {
		printf("libcw: cw_send_dash():              failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_dash():              success\n");
		stats->successes++;
	}
	failure = false;


	for (int i = 0; i < N; i++) {
		if (!cw_send_character_space()) {
			failure = true;
			break;
		}
	}
	cw_wait_for_tone_queue();
	if (failure) {
		printf("libcw: cw_send_character_space():   failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_character_space():   success\n");
		stats->successes++;
	}
	failure = false;


	for (int i = 0; i < N; i++) {
		if (!cw_send_word_space()) {
			failure = true;
			break;
		}
	}
	cw_wait_for_tone_queue();
	if (failure) {
		printf("libcw: cw_send_word_space():        failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_word_space():        success\n");
		stats->successes++;
	}
	failure = false;


	printf("libcw: %s():  completed\n\n", __func__);

	return;
}





/**
   \brief Testing and playing representations of characters
*/
void cw_test_representations(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Test some valid representations. */
	if (!cw_representation_is_valid(".-.-.-")
	    || !cw_representation_is_valid(".-")
	    || !cw_representation_is_valid("---")
	    || !cw_representation_is_valid("...-")) {

		printf("libcw: cw_representation_is_valid(<valid>):    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_representation_is_valid(<valid>):    success\n");
		stats->successes++;
	}


	/* Test some invalid representations. */
	if (cw_representation_is_valid("INVALID")
	    || cw_representation_is_valid("_._")
	    || cw_representation_is_valid("-_-")) {

		printf("libcw: cw_representation_is_valid(<invalid>):  failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_representation_is_valid(<invalid>):  success\n");
		stats->successes++;
	}


	/* Send some valid representations. */
	if (!cw_send_representation(".-.-.-")
	    || !cw_send_representation(".-")
	    || !cw_send_representation("---")
	    || !cw_send_representation("...-")) {

		printf("libcw: cw_send_representation(<valid>):        failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_representation(<valid>):        success\n");
		stats->successes++;
	}


	/* Send some invalid representations. */
	if (cw_send_representation("INVALID")
	    || cw_send_representation("_._")
	    || cw_send_representation("-_-")) {

		printf("libcw: cw_send_representation(<invalid>):      failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_representation(<invalid>):      success\n");
		stats->successes++;
	}


	/* Test sending partial representation of a valid string. */
	if (!cw_send_representation_partial(".-.-.-")) {
		printf("libcw: cw_send_representation_partial():       failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_representation_partial():       success\n");
		stats->successes++;
	}


	cw_wait_for_tone_queue();


	printf("libcw: %s():            completed\n\n", __func__);
	return;
}






/**
   Validate all supported characters, first each characters individually, then as a string.
*/
void cw_test_validate_characters_and_string(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

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
	if (valid_failure) {
		printf("libcw: cw_character_is_valid(<valid>):      failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_character_is_valid(<valid>):      success\n");
		stats->successes++;
	}
	if (invalid_failure) {
		printf("libcw: cw_character_is_valid(<invalid>):    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_character_is_valid(<invalid>):    success\n");
		stats->successes++;
	}


	/* Check the whole charlist item as a single string, then
	   check a known invalid string. */
	cw_list_characters(charlist);
	if (!cw_string_is_valid(charlist)) {
		printf("libcw: cw_string_is_valid(<valid>):         failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_string_is_valid(<valid>):         success\n");
		stats->successes++;
	}

	if (cw_string_is_valid("%INVALID%")) {
		printf("libcw: cw_string_is_valid(<invalid>):       failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_string_is_valid(<invalid>):       success\n");
		stats->successes++;
	}


	printf("libcw: %s(): completed\n\n", __func__);
	return;
}





/**
   Send all supported characters: first as individual characters, and then as a string.
*/
void cw_test_send_characters_and_string(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

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

	if (failure) {
		printf("libcw: cw_send_character(<valid>):        failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_character(<valid>):        success\n");
		stats->successes++;
	}

	if (cw_send_character(0)) {
		printf("libcw: cw_send_character(<invalid>):      failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_character(<invalid>):      success\n");
		stats->successes++;
	}


	/* Now send the complete charlist as a single string. */
	printf("libcw: cw_send_string(<valid>):\n"
	       "libcw:     %s\n", charlist);
	int send = cw_send_string(charlist);

	while (cw_get_tone_queue_length() > 0) {
		printf("libcw: tone queue length %-6d\r", cw_get_tone_queue_length ());
		fflush(stdout);
		cw_wait_for_tone();
	}
	printf("libcw: tone queue length %-6d\n", cw_get_tone_queue_length());
	cw_wait_for_tone_queue();

	if (!send) {
		printf("libcw: cw_send_string(<valid>):             failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_string(<valid>):             success\n");
		stats->successes++;
	}


	if (cw_send_string("%INVALID%")) {
		printf("libcw: cw_send_string(<invalid>):           failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_send_string(<invalid>):           success\n");
		stats->successes++;
	}

	printf("libcw: %s(): completed\n\n", __func__);
	return;
}





/*
 * cw_test_fixed_receive()
 */
void cw_test_fixed_receive(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	const cw_test_receive_data_t TEST_DATA[] = {  /* 60 WPM characters with jitter */
		{ 'Q', "--.-", { 63456, 20111, 63456, 20111, 23456, 20111, 63456, 60111, 0 } },
		{ 'R', ".-.",  { 17654, 20222, 57654, 20222, 17654, 60222, 0 } },
		{ 'P', ".--.", { 23456, 20333, 63456, 20333, 63456, 20333, 23456, 60333, 0 } },
		{ ' ', NULL,   { 0 } }
	};

	/* Test receive functions by spoofing them with a timestamp.
	   Getting the test suite to generate reliable timing events
	   is a little too much work.  Add just a little jitter to the
	   timestamps.  This is a _very_ minimal test, omitting all
	   error states. */
	printf("libcw: cw_get_receive_buffer_capacity(): %d\n", cw_get_receive_buffer_capacity());

	cw_set_receive_speed(60);
	cw_set_tolerance(35);
	cw_disable_adaptive_receive();

	cw_test_helper_receive_tests(false, TEST_DATA, stats);

	printf("libcw: %s(): completed\n\n", __func__);
	return;
}





/*
 * cw_test_adaptive_receive()
 */
void cw_test_adaptive_receive(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	const cw_test_receive_data_t TEST_DATA[] = {  /* 60, 40, and 30 WPM (mixed speed) characters */
		{ 'Q', "--.-", { 60000, 20000,  60000, 20000,  20000, 20000, 60000, 60000, 0    } },
		{ 'R', ".-.",  { 30000, 30000,  90000, 30000,  30000, 90000,     0 } },
		{ 'P', ".--.", { 40000, 40000, 120000, 40000, 120000, 40000, 40000,  280000, -1 } },  /* Includes word end delay, -1 indicator */
		{ ' ', NULL,   { 0 } }
	};

	/* Test adaptive receive functions in much the same sort of
	   way.  Again, this is a _very_ minimal test, omitting all
	   error states. */
	cw_set_receive_speed(45);
	cw_set_tolerance(35);
	cw_enable_adaptive_receive();

	cw_test_helper_receive_tests(true, TEST_DATA, stats);

	printf("libcw: %s(): completed\n\n", __func__);
	return;
}





/*
  Wrapper for code that is common for both cw_test_fixed_receive()
  and cw_test_adaptive_receive().
*/
void cw_test_helper_receive_tests(bool adaptive, const cw_test_receive_data_t *data, cw_test_stats_t *stats)
{
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	for (int i = 0; data[i].representation; i++) {
		int entry;
		bool is_word, is_error;
		char c, representation[256];

		tv.tv_sec++;
		tv.tv_usec = 0;
		for (entry = 0; data[i].usecs[entry] > 0; entry++) {
			entry & 1 ? cw_end_receive_tone(&tv) : cw_start_receive_tone(&tv);
			tv.tv_usec += data[i].usecs[entry];
		}

		if (cw_get_receive_buffer_length()
		    != (int) strlen(data[i].representation)) {

			printf("libcw: cw_get_receive_buffer_length():  failure\n");
			stats->failures++;
			break;
		} else {
			printf("libcw: cw_get_receive_buffer_length():  success\n");
			stats->successes++;
		}

		if (!cw_receive_representation(&tv, representation, &is_word, &is_error)) {
			printf("libcw: cw_receive_representation():     failure\n");
			stats->failures++;
			break;
		} else {
			printf("libcw: cw_receive_representation():     success\n");
			stats->successes++;
		}

		if (strcmp(representation, data[i].representation) != 0) {
			printf("libcw: cw_receive_representation():     failure\n");
			stats->failures++;
			break;
		} else {
			printf("libcw: cw_receive_representation():     success\n");
			stats->successes++;
		}

		if (adaptive) {
			if ((data[i].usecs[entry] == 0 && is_word)
			    || (data[i].usecs[entry] < 0 && !is_word)) {

				printf("libcw: cw_receive_representation():     failure (not a %s)\n", is_word ? "char" : "word");
				stats->failures++;
				break;
			} else {
				printf("libcw: cw_receive_representation():     success\n");
				stats->successes++;
			}
		} else {
			if (is_word) {
				printf("libcw: cw_receive_representation():     failure\n");
				stats->failures++;
				break;
			} else {
				printf("libcw: cw_receive_representation():     success\n");
				stats->successes++;
			}
		}

		if (is_error) {
			printf("libcw: cw_receive_representation():     failure\n");
			stats->failures++;
			break;
		} else {
			printf("libcw: cw_receive_representation():     success\n");
			stats->successes++;
		}

		if (!cw_receive_character(&tv, &c, &is_word, &is_error)) {
			printf("libcw: cw_receive_character():          failure\n");
			stats->failures++;
			break;
		} else {
			printf("libcw: cw_receive_character():          success\n");
			stats->successes++;
		}

		if (c != data[i].character) {
			printf("libcw: cw_receive_character():          failure\n");
			stats->failures++;
			break;
		} else {
			printf("libcw: cw_receive_character():          success\n");
			stats->successes++;
		}

		if (adaptive) {
			printf("libcw: adaptive speed tracking reports %d wpm\n",
			       cw_get_receive_speed());
		}

		printf("libcw: cw_receive_representation(): <%s>\n", representation);
		printf("libcw: cw_receive_character(): <%c>\n", c);

		cw_clear_receive_buffer();
		if (cw_get_receive_buffer_length() != 0) {
			printf("libcw: receive_buffer_length():         failure\n");
			stats->failures++;
			break;
		} else {
			printf("libcw: receive_buffer_length():         success\n");
			stats->successes++;
		}
	}

	double dot_sd, dash_sd, element_end_sd, character_end_sd;
	cw_get_receive_statistics(&dot_sd, &dash_sd,
				  &element_end_sd, &character_end_sd);
	printf("libcw: cw_receive_statistics(): standard deviations:\n");
	printf("                           dot: %.2f\n", dot_sd);
	printf("                          dash: %.2f\n", dash_sd);
	printf("         inter-element spacing: %.2f\n", element_end_sd);
	printf("       inter-character spacing: %.2f\n", character_end_sd);

	cw_reset_receive_statistics();

	return;
}





/*
 * cw_test_keyer()
 */
void cw_test_keyer(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int dot_paddle, dash_paddle;

	/* Perform some tests on the iambic keyer.  The latch finer
	   timing points are not tested here, just the basics - dots,
	   dashes, and alternating dots and dashes. */
	if (!cw_notify_keyer_paddle_event(true, false)) {
		printf("libcw: cw_notify_keyer_paddle_event():    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_notify_keyer_paddle_event():    success\n");
		stats->successes++;
	}


	printf("libcw: testing iambic keyer dots   ");
	fflush(stdout);
	for (int i = 0; i < 30; i++) {
		cw_wait_for_keyer_element();
		putchar('.');
		fflush(stdout);
	}
	putchar('\n');


	cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
	if (!dot_paddle || dash_paddle) {
		printf("libcw: cw_keyer_get_keyer_paddles():      failure (mismatch)\n");
		stats->failures++;
	} else {
		printf("libcw: cw_keyer_get_keyer_paddles():      success\n");
		stats->successes++;
	}


	if (!cw_notify_keyer_paddle_event(false, true)) {
		printf("libcw: cw_notify_keyer_paddle_event():    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_notify_keyer_paddle_event():    success\n");
		stats->successes++;
	}


	printf("libcw: testing iambic keyer dashes ");
	fflush(stdout);
	for (int i = 0; i < 30; i++) {
		cw_wait_for_keyer_element();
		putchar('-');
		fflush(stdout);
	}
	putchar('\n');


	cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
	if (dot_paddle || !dash_paddle) {
		printf("libcw: cw_get_keyer_paddles():            failure (mismatch)\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get_keyer_paddles():            success\n");
		stats->successes++;
	}


	if (!cw_notify_keyer_paddle_event(true, true)) {
		printf("libcw: cw_notify_keyer_paddle_event():    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_notify_keyer_paddle_event():    success\n");
		stats->successes++;
	}


	printf("libcw: testing iambic alternating  ");
	fflush(stdout);
	for (int i = 0; i < 30; i++) {
		cw_wait_for_keyer_element();
		putchar('#');
		fflush(stdout);
	}
	putchar('\n');


	cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
	if (!dot_paddle || !dash_paddle) {
		printf("libcw: cw_get_keyer_paddles():            failure (mismatch)\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get_keyer_paddles():            success\n");
		stats->successes++;
	}


	if (!cw_notify_keyer_paddle_event(false, false)) {
		printf("libcw: cw_notify_keyer_paddle_event():    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_notify_keyer_paddle_event():    success\n");
		stats->successes++;
	}


	cw_wait_for_keyer();


	printf("libcw: %s(): completed\n\n", __func__);
	return;
}





/*
 * cw_test_straight_key()
 */
void cw_test_straight_key(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Unusually, a nice simple set of tests. */

	bool event_failure = false;
	bool state_failure = false;
	bool busy_failure = false;

	for (int i = 0; i < 10; i++) {
		if (!cw_notify_straight_key_event(false)) {
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

	if (event_failure) {
		printf("libcw: cw_notify_straight_key_event(false):   failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_notify_straight_key_event(false):   success\n");
		stats->successes++;
	}
	if (state_failure) {
		printf("libcw: cw_get_straight_key_state():           failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get_straight_key_state():           success\n");
		stats->successes++;
	}
	if (busy_failure) {
		printf("libcw: cw_straight_key_busy():                failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_straight_key_busy():                success\n");
		stats->successes++;
	}


	event_failure = false;
	state_failure = false;
	busy_failure = false;

	for (int i = 0; i < 10; i++) {
		if (!cw_notify_straight_key_event(true)) {
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

	if (event_failure) {
		printf("libcw: cw_notify_straight_key_event(true):    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_notify_straight_key_event(true):    success\n");
		stats->successes++;
	}
	if (state_failure) {
		printf("libcw: cw_get_straight_key_state():           failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get_straight_key_state():           success\n");
		stats->successes++;
	}
	if (busy_failure) {
		printf("libcw: cw_straight_key_busy():                failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_straight_key_busy():                success\n");
		stats->successes++;
	}
	event_failure = false;


	sleep(1);


	for (int i = 0; i < 10; i++) {
		if (!cw_notify_straight_key_event(false)) {
			event_failure = true;
			break;
		}
	}
	if (event_failure) {
		printf("libcw: cw_notify_straight_key_event(true):    failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_notify_straight_key_event(true):    success\n");
		stats->successes++;
	}


	if (cw_get_straight_key_state()) {
		printf("libcw: cw_get_straight_key_state():           failure\n");
		stats->failures++;
	} else {
		printf("libcw: cw_get_straight_key_state():           success\n");
		stats->successes++;
	}

	printf("libcw: %s(): completed\n\n", __func__);
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

	printf("libcw: %s(): completed\n\n", __func__);
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
	cw_test_version_license,
	cw_test_debug_flags,
	cw_test_limits,
	cw_test_lookups,
	cw_test_prosign_lookups,
	cw_test_phonetic_lookups,
	NULL
};




/* Tests that are dependent on a sound system being configured. */
static void (*const CW_TEST_FUNCTIONS_DEP[])(cw_test_stats_t *) = {
	cw_test_ranges,
	cw_test_tone_parameters,
	cw_test_tone_queue_1,
	cw_test_tone_queue_2,
	cw_test_tone_queue_3,
	cw_test_tone_queue_callback,
	cw_test_volumes,
	cw_test_send_primitives,
	cw_test_representations,
	cw_test_validate_characters_and_string,
	cw_test_send_characters_and_string,
	cw_test_fixed_receive,
	cw_test_adaptive_receive,
	cw_test_keyer,
	cw_test_straight_key,
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





int cw_test_args(int argc, char *const argv[], char *sound_systems, size_t systems_max)
{
	if (argc == 1) {
		strncpy(sound_systems, "ncoap", systems_max);
		sound_systems[systems_max] = '\0';

		fprintf(stderr, "sound systems = \"%s\"\n", sound_systems);
		return CW_SUCCESS;
	}

	int opt;
	while ((opt = getopt(argc, argv, "s:")) != -1) {
		switch (opt) {
		case 's':
			{
				size_t len = strlen(optarg);
				if (!len || len > systems_max) {
					return CW_FAILURE;
				}

				int j = 0;
				for (size_t i = 0; i < len; i++) {
					if (optarg[i] != 'n'
					    && optarg[i] != 'c'
					    && optarg[i] != 'o'
					    && optarg[i] != 'a'
					    && optarg[i] != 'p') {

						return CW_FAILURE;
					} else {
						sound_systems[j] = optarg[i];
						j++;
					}
				}
				sound_systems[j] = '\0';
			}

			break;
		default: /* '?' */
			return CW_FAILURE;
		}
	}

	fprintf(stderr, "sound systems = \"%s\"\n", sound_systems);
	return CW_SUCCESS;
}





void cw_test_print_help(const char *progname)
{
	fprintf(stderr, "Usage: %s [-s <sound systems>]\n\n", progname);
	fprintf(stderr, "       <sound system> is one or more of those:\n");
	fprintf(stderr, "       n - null\n");
	fprintf(stderr, "       c - console\n");
	fprintf(stderr, "       o - OSS\n");
	fprintf(stderr, "       a - ALSA\n");
	fprintf(stderr, "       p - PulseAudio\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "       If no argument is provided, the program will attempt to test all audio systems\n");

	return;
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
