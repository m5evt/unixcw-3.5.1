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


#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h> /* gettimeofday() */

#include "libcw2.h"

#include "libcw_gen.h"
#include "libcw_rec.h"
#include "libcw_debug.h"
#include "libcw_data.h"
#include "libcw_tq.h"
#include "libcw_utils.h"
#include "libcw_key.h"

#include "libcw_null.h"
#include "libcw_console.h"
#include "libcw_oss.h"
#include "libcw_test.h"




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_dev;



static cw_test_stats_t cw_stats_null    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_console = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_oss     = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_alsa    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_pa      = { .successes = 0, .failures = 0 };



static int cw_test_dependent_with(int audio_system, const char *modules, cw_test_stats_t *stats);
static int cw_test_dependent(const char *audio_systems, const char *modules);
static void cw_test_setup(cw_gen_t *gen);


/* This variable will be used in "forever" test. This test function
   needs to open generator itself, so it needs to know the current
   audio system to be used. _NONE is just an initial value, to be
   changed in test setup. */
static int test_audio_system = CW_AUDIO_NONE;




/* ******************************************************************** */
/*                 Unit tests for internal functions                    */
/* ******************************************************************** */





/* Unit tests for internal functions (and also some public functions)
   defined in libcw.c.

   See also libcw_test_public.c and libcw_test_simple_gen.c. */

#include <stdio.h>
#include <assert.h>





typedef unsigned int (*cw_test_function_t)(void);
typedef unsigned int (*cw_key_test_function_t)(cw_key_t * key, cw_test_stats_t * stats);

static cw_test_function_t cw_unit_tests[] = {

	/* cw_data module */
	test_cw_representation_to_hash_internal,
	test_cw_representation_to_character_internal,
	test_cw_representation_to_character_internal_speed,
	test_character_lookups_internal,
	test_prosign_lookups_internal,
	test_phonetic_lookups_internal,
	test_validate_character_and_string_internal,
	test_validate_representation_internal,


	/* cw_tq module */
	test_cw_tq_new_delete_internal,
	test_cw_tq_get_capacity_internal,
	test_cw_tq_prev_index_internal,
	test_cw_tq_next_index_internal,
	test_cw_tq_length_internal,
	test_cw_tq_enqueue_dequeue_internal,
	test_cw_tq_enqueue_args_internal,
	test_cw_tq_is_full_internal,
	test_cw_tq_test_capacity_1,
	test_cw_tq_test_capacity_2,
	test_cw_tq_wait_for_level_internal,


	/* cw_gen module */
	test_cw_gen_set_tone_slope,
	test_cw_gen_tone_slope_shape_enums,
	test_cw_gen_new_delete,
	test_cw_gen_forever_internal,
	test_cw_gen_get_timing_parameters_internal,
	test_cw_gen_parameter_getters_setters,


	/* cw_utils module */
	test_cw_timestamp_compare_internal,
	test_cw_timestamp_validate_internal,
	test_cw_usecs_to_timespec_internal,
	test_cw_version_internal,
	test_cw_license_internal,
	test_cw_get_x_limits_internal,


	/* cw_rec module */
	test_cw_rec_identify_mark_internal,
	test_cw_rec_with_base_data_fixed,
	test_cw_rec_with_random_data_fixed,
	test_cw_rec_with_random_data_adaptive,
	test_cw_get_receive_parameters,
	test_cw_rec_parameter_getters_setters,


	/* cw_debug module */
	test_cw_debug_flags_internal,

	NULL
};


/* 'key' module. */
static cw_key_test_function_t cw_unit_tests_key[] = {
	test_keyer,
	test_straight_key,
	NULL
};




int main(void)
{
	fprintf(stderr, "libcw unit tests\n\n");


	struct timeval tv;
	gettimeofday(&tv, NULL);
	srand((int) tv.tv_usec);

	//cw_debug_set_flags(&cw_debug_object, CW_DEBUG_RECEIVE_STATES);
	//cw_debug_object.level = CW_DEBUG_INFO;

	cw_debug_set_flags(&cw_debug_object_dev, CW_DEBUG_RECEIVE_STATES | CW_DEBUG_TONE_QUEUE | CW_DEBUG_GENERATOR | CW_DEBUG_KEYING);
	cw_debug_object_dev.level = CW_DEBUG_DEBUG;

	int i = 0;
	while (cw_unit_tests[i]) {
		cw_unit_tests[i]();
		i++;
	}

	cw_test_dependent("a", "k");

	/* "make check" facility requires this message to be
	   printed on stdout; don't localize it */
	fprintf(stdout, "\nlibcw: test result: success\n\n");


	return 0;
}




/**
   \brief Set up common test conditions

   Run before each individual test, to handle setup of common test conditions.
*/
void cw_test_setup(cw_gen_t *gen)
{
	cw_gen_reset_parameters_internal(gen);
	//cw_rec_reset_receive_parameters_internal(&cw_receiver);
	/* Reset requires resynchronization. */
	cw_gen_sync_parameters_internal(gen);
	//cw_rec_sync_parameters_internal(&cw_receiver);

	cw_gen_set_speed(gen, 30);
	// cw_rec_set_speed(rec, 30);
	// cw_rec_disable_adaptive_mode(rec);
	// cw_rec_reset_statistics(rec);
	errno = 0;

	return;
}





/**
   \brief Run tests for given audio system.

   Perform a series of self-tests on library public interfaces, using
   audio system specified with \p audio_system. Range of tests is specified
   with \p testset.

   \param audio_system - audio system to use for tests
   \param modules - libcw modules to be tested
   \param stats - test statistics

   \return -1 on failure to set up tests
   \return 0 if tests were run, and no errors occurred
   \return 1 if tests were run, and some errors occurred
*/
int cw_test_dependent_with(int audio_system, const char *modules, cw_test_stats_t *stats)
{
	test_audio_system = audio_system;

	cw_gen_t *gen = cw_gen_new(audio_system, NULL);
	if (!gen) {
		fprintf(stderr, "libcw: can't create generator, stopping the test\n");
		return -1;
	}

	cw_key_t *key = cw_key_new();
	if (!key) {
		fprintf(stderr, "libcw: can't create key, stopping the test\n");
		return -1;
	}
	cw_key_register_generator(key, gen);

	int rv = cw_gen_start(gen);
	if (rv != 1) {
		fprintf(stderr, "libcw: can't start generator, stopping the test\n");
		cw_gen_delete(&gen);
		return -1;
	}

#if 0

	if (strstr(modules, "t")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_T[test]; test++) {
			cw_test_setup(gen);
	                (*CW_TEST_FUNCTIONS_DEP_T[test])(gen, stats);
		}
	}


	if (strstr(modules, "g")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_G[test]; test++) {
			cw_test_setup(gen);
			(*CW_TEST_FUNCTIONS_DEP_G[test])(gen, stats);
		}
	}

#endif

	if (strstr(modules, "k")) {
		int i = 0;
		while (cw_unit_tests_key[i]) {
			cw_test_setup(gen);
	                (*cw_unit_tests_key[i])(key, stats);
			i++;
		}
	}



	sleep(1);
	cw_key_delete(&key);

	sleep(1);
	cw_gen_stop(gen);
	sleep(1);
	cw_gen_delete(&gen);

	/* All tests done; return success if no failures,
	   otherwise return an error status code. */
	return stats->failures ? 1 : 0;
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
int cw_test_dependent(const char *audio_systems, const char *modules)
{
	int n = 0, c = 0, o = 0, a = 0, p = 0;


	if (!audio_systems || strstr(audio_systems, "n")) {
		if (cw_is_null_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with null output\n");
			n = cw_test_dependent_with(CW_AUDIO_NULL, modules, &cw_stats_null);
		} else {
			fprintf(stderr, "libcw: null output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "c")) {
		if (cw_is_console_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with console output\n");
			c = cw_test_dependent_with(CW_AUDIO_CONSOLE, modules, &cw_stats_console);
		} else {
			fprintf(stderr, "libcw: console output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "o")) {
		if (cw_is_oss_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with OSS output\n");
			o = cw_test_dependent_with(CW_AUDIO_OSS, modules, &cw_stats_oss);
		} else {
			fprintf(stderr, "libcw: OSS output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "a")) {
		if (cw_is_alsa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with ALSA output\n");
			a = cw_test_dependent_with(CW_AUDIO_ALSA, modules, &cw_stats_alsa);
		} else {
			fprintf(stderr, "libcw: Alsa output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "p")) {
		if (cw_is_pa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with PulseAudio output\n");
			p = cw_test_dependent_with(CW_AUDIO_PA, modules, &cw_stats_pa);
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





void cw_test_print_stats(void)
{
	printf("\n\nlibcw: Statistics of tests:\n\n");


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
