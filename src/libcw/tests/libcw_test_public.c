/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)
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

#include "libcw2.h"
#include "libcw_test.h"
#include "libcw_debug.h"
#include "libcw_tq.h"
#include "libcw_utils.h"
#include "libcw_gen.h"





typedef struct {
	int successes;
	int failures;
} cw_test_stats_t;


extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;



static cw_test_stats_t cw_stats_null    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_console = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_oss     = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_alsa    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_pa      = { .successes = 0, .failures = 0 };


static void cw_test_setup(cw_gen_t *gen);
static int  cw_test_dependent(const char *audio_systems, const char *modules);
static int  cw_test_dependent_with(int audio_system, const char *modules, cw_test_stats_t *stats);
static void cw_test_print_stats(void);

static void register_signal_handler(void);
static void signal_handler(int signal_number);


static void cw_test_helper_tq_callback(void *data);





/* This variable will be used in "forever" test. This test function
   needs to open generator itself, so it needs to know the current
   audio system to be used. _NONE is just an initial value, to be
   changed in test setup. */
static int test_audio_system = CW_AUDIO_NONE;







/*---------------------------------------------------------------------*/
/*  Unit tests drivers                                                 */
/*---------------------------------------------------------------------*/





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

	if (strstr(modules, "k")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_K[test]; test++) {
			cw_test_setup(gen);
	                (*CW_TEST_FUNCTIONS_DEP_K[test])(key, stats);
		}
	}
#endif


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





/**
   \return EXIT_SUCCESS if all tests complete successfully,
   \return EXIT_FAILURE otherwise
*/
int main(int argc, char *const argv[])
{
	int rv = 0;

	//cw_debug_set_flags(&cw_debug_object_dev, CW_DEBUG_RECEIVE_STATES | CW_DEBUG_TONE_QUEUE | CW_DEBUG_GENERATOR | CW_DEBUG_KEYING);
	//cw_debug_object_dev.level = CW_DEBUG_DEBUG;

	unsigned int testset = 0;

	struct timeval seed;
	gettimeofday(&seed, NULL);
	// fprintf(stderr, "seed: %d\n", (int) seed.tv_usec);
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
#define CW_MODULES_MAX 3  /* g, t, k */
	char modules[CW_MODULES_MAX + 1];
	modules[0] = '\0';

	if (!cw_test_args(argc, argv, sound_systems, CW_SYSTEMS_MAX, modules, CW_MODULES_MAX)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}

	atexit(cw_test_print_stats);

	register_signal_handler();

	rv = cw_test_dependent(sound_systems, modules);

	return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}





/* Show the signal caught, and exit. */
void signal_handler(int signal_number)
{
	fprintf(stderr, "\nCaught signal %d, exiting...\n", signal_number);
	exit(EXIT_SUCCESS);
}





void register_signal_handler(void)
{
	/* Set up signal handler to exit on a range of signals. */
	const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };
	for (int i = 0; SIGNALS[i]; i++) {

		struct sigaction action;
		memset(&action, 0, sizeof(action));
		action.sa_handler = signal_handler;
		action.sa_flags = 0;
		int rv = sigaction(SIGNALS[i], &action, (struct sigaction *) NULL);
		if (rv == -1) {
			fprintf(stderr, "can't register signal: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	return;
}
