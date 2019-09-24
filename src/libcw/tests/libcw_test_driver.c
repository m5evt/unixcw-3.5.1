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
#include "libcw_test_utils.h"
#include "libcw_debug.h"
#include "libcw_tq.h"
#include "libcw_utils.h"
#include "libcw_gen.h"




#define MSG_PREFIX "libcw/legacy: "



extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;



extern void (*const libcw_test_set_tq_with_audio[])(cw_test_t *);
extern void (*const libcw_test_set_gen_with_audio[])(cw_test_t *);
extern void (*const libcw_test_set_key_with_audio[])(cw_test_t *);
extern void (*const libcw_test_set_other_with_audio[])(cw_test_t *);




static cw_test_stats_t cw_stats_indep   = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_null    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_console = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_oss     = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_alsa    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_pa      = { .successes = 0, .failures = 0 };


static void cw_test_setup(void);
static int  cw_test_modules_with_sound_systems(const char * modules, const char * sound_systems);
static int  cw_test_modules_with_one_sound_system(const char * modules, int audio_system, cw_test_t * tests);
static void cw_test_print_stats(void);




/* This variable will be used in "forever" test. This test function
   needs to open generator itself, so it needs to know the current
   audio system to be used. _NONE is just an initial value, to be
   changed in test setup. */
int test_audio_system = CW_AUDIO_NONE;




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
int cw_test_modules_with_one_sound_system(const char * modules, int audio_system, cw_test_t * tests)
{
	test_audio_system = audio_system;

	int rv = cw_generator_new(audio_system, NULL);
	if (rv != 1) {
		fprintf(stderr, MSG_PREFIX "can't create generator, stopping the test\n");
		return -1;
	}
	rv = cw_generator_start();
	if (rv != 1) {
		fprintf(stderr, MSG_PREFIX "can't start generator, stopping the test\n");
		cw_generator_delete();
		return -1;
	}


	if (strstr(modules, "t")) {
		for (int test = 0; libcw_test_set_tq_with_audio[test]; test++) {
			cw_test_setup();
			(*libcw_test_set_tq_with_audio[test])(tests);
		}
	}


	if (strstr(modules, "g")) {
		for (int test = 0; libcw_test_set_gen_with_audio[test]; test++) {
			cw_test_setup();
			(*libcw_test_set_gen_with_audio[test])(tests);
		}
	}


	if (strstr(modules, "k")) {
		for (int test = 0; libcw_test_set_gen_with_audio[test]; test++) {
			cw_test_setup();
			(*libcw_test_set_key_with_audio[test])(tests);
		}
	}


	if (strstr(modules, "o")) {
		for (int test = 0; libcw_test_set_other_with_audio[test]; test++) {
			cw_test_setup();
			(*libcw_test_set_other_with_audio[test])(tests);
		}
	}


	sleep(1);
	cw_generator_stop();
	sleep(1);
	cw_generator_delete();

	/* All tests done; return success if no failures,
	   otherwise return an error status code. */
	return tests->stats->failures ? 1 : 0;
}





/**
   \brief Run a series of tests for specified audio systems

   Function attempts to run a set of testcases for every audio system
   specified in \p sound_systems. These testcases require some kind of
   audio system configured. The function calls
   cw_test_modules_with_one_sound_system() to do the configuration and
   run the tests.

   \p sound_systems is a list of audio systems to be tested: "ncoap".
   Pass NULL pointer to attempt to test all of audio systems supported
   by libcw.

   \param sound_systems - list of audio systems to be tested
*/
int cw_test_modules_with_sound_systems(const char * modules, const char * sound_systems)
{
	int n = 0, c = 0, o = 0, a = 0, p = 0;

	cw_test_t test_set;
	cw_test_init(&test_set, stdout, stderr, MSG_PREFIX);


	if (!sound_systems || strstr(sound_systems, "n")) {
		if (cw_is_null_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with null output\n");
			test_set.stats = &cw_stats_null;
			n = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_NULL, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "null output not available\n");
		}
	}

	if (!sound_systems || strstr(sound_systems, "c")) {
		if (cw_is_console_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with console output\n");
			test_set.stats = &cw_stats_console;
			c = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_CONSOLE, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "console output not available\n");
		}
	}

	if (!sound_systems || strstr(sound_systems, "o")) {
		if (cw_is_oss_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with OSS output\n");
			test_set.stats = &cw_stats_oss;
			o = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_OSS, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "OSS output not available\n");
		}
	}

	if (!sound_systems || strstr(sound_systems, "a")) {
		if (cw_is_alsa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with ALSA output\n");
			test_set.stats = &cw_stats_alsa;
			a = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_ALSA, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "Alsa output not available\n");
		}
	}

	if (!sound_systems || strstr(sound_systems, "p")) {
		if (cw_is_pa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with PulseAudio output\n");
			test_set.stats = &cw_stats_pa;
			p = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_PA, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "PulseAudio output not available\n");
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
	printf("\n\n"MSG_PREFIX "Statistics of tests:\n\n");

	printf(MSG_PREFIX "Tests not requiring any audio system:            ");
	if (cw_stats_indep.failures + cw_stats_indep.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_indep.failures, cw_stats_indep.failures + cw_stats_indep.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with NULL audio system:          ");
	if (cw_stats_null.failures + cw_stats_null.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_null.failures, cw_stats_null.failures + cw_stats_null.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with console audio system:       ");
	if (cw_stats_console.failures + cw_stats_console.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_console.failures, cw_stats_console.failures + cw_stats_console.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with OSS audio system:           ");
	if (cw_stats_oss.failures + cw_stats_oss.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_oss.failures, cw_stats_oss.failures + cw_stats_oss.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with ALSA audio system:          ");
	if (cw_stats_alsa.failures + cw_stats_alsa.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_alsa.failures, cw_stats_alsa.failures + cw_stats_alsa.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with PulseAudio audio system:    ");
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

	static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };

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
#define CW_MODULES_MAX 4  /* g, t, k, o */
	char modules[CW_MODULES_MAX + 1];
	modules[0] = '\0';

	if (!cw_test_args(argc, argv, sound_systems, CW_SYSTEMS_MAX, modules, CW_MODULES_MAX)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}

	atexit(cw_test_print_stats);

	/* Arrange for the test to exit on a range of signals. */
	for (int i = 0; SIGNALS[i] != 0; i++) {
		if (!cw_register_signal_handler(SIGNALS[i], SIG_DFL)) {
			fprintf(stderr, MSG_PREFIX "ERROR: cw_register_signal_handler\n");
			exit(EXIT_FAILURE);
		}
	}

	rv = cw_test_modules_with_sound_systems(modules, sound_systems);

	return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
