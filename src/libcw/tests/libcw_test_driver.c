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




#define MSG_PREFIX "libcw/legacy"



extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;



extern void (*const libcw_test_set_tq_with_audio[])(cw_test_t *);
extern void (*const libcw_test_set_gen_with_audio[])(cw_test_t *);
extern void (*const libcw_test_set_key_with_audio[])(cw_test_t *);
extern void (*const libcw_test_set_other_with_audio[])(cw_test_t *);




static void cw_test_setup(void);
//static int  cw_test_modules_with_sound_systems(cw_test_t * tests);
static int  cw_test_modules_with_current_sound_system(cw_test_t * tests);
static void cw_test_print_stats_wrapper(void);
static void cw_test_print_stats(cw_test_t * tests);




/* This variable will be used in "forever" test. This test function
   needs to open generator itself, so it needs to know the current
   audio system to be used. _NONE is just an initial value, to be
   changed in test setup. */
int test_audio_system = CW_AUDIO_NONE;



static cw_test_t g_tests;


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
   \param stats - test statistics

   \return -1 on failure to set up tests
   \return 0 if tests were run, and no errors occurred
   \return 1 if tests were run, and some errors occurred
*/
int cw_test_modules_with_current_sound_system(cw_test_t * tests)
{
	test_audio_system = tests->current_sound_system;

	fprintf(tests->stderr, "%sTesting with %s sound system\n", tests->msg_prefix, tests->get_current_sound_system_label(tests));

	int rv = cw_generator_new(tests->current_sound_system, NULL);
	if (rv != 1) {
		fprintf(tests->stderr, "%scan't create generator, stopping the test\n", tests->msg_prefix);
		return -1;
	}
	rv = cw_generator_start();
	if (rv != 1) {
		fprintf(tests->stderr, "%scan't start generator, stopping the test\n", tests->msg_prefix);
		cw_generator_delete();
		return -1;
	}

	if (tests->should_test_module(tests, "t")) {
		for (int test = 0; libcw_test_set_tq_with_audio[test]; test++) {
			cw_test_setup();
			(*libcw_test_set_tq_with_audio[test])(tests);
		}
	}


	if (tests->should_test_module(tests, "g")) {
		for (int test = 0; libcw_test_set_gen_with_audio[test]; test++) {
			cw_test_setup();
			(*libcw_test_set_gen_with_audio[test])(tests);
		}
	}


	if (tests->should_test_module(tests, "k")) {
		for (int test = 0; libcw_test_set_key_with_audio[test]; test++) {
			cw_test_setup();
			(*libcw_test_set_key_with_audio[test])(tests);
		}
	}


	if (tests->should_test_module(tests, "o")) {
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









void cw_test_print_stats_wrapper(void)
{
	cw_test_print_stats(&g_tests);
}




void cw_test_print_stats(cw_test_t * tests)
{
	fprintf(tests->stderr, "\n\n%sStatistics of tests:\n\n", tests->msg_prefix);

	fprintf(tests->stderr, "%sTests not requiring any audio system:            ", tests->msg_prefix);
	if (tests->stats_indep.failures + tests->stats_indep.successes) {
		fprintf(tests->stderr, "errors: %03d, total: %03d\n",
		       tests->stats_indep.failures, tests->stats_indep.failures + tests->stats_indep.successes);
	} else {
		fprintf(tests->stderr, "no tests were performed\n");
	}

	fprintf(tests->stderr, "%sTests performed with NULL audio system:          ", tests->msg_prefix);
	if (tests->stats_null.failures + tests->stats_null.successes) {
		fprintf(tests->stderr, "errors: %03d, total: %03d\n",
		       tests->stats_null.failures, tests->stats_null.failures + tests->stats_null.successes);
	} else {
		fprintf(tests->stderr, "no tests were performed\n");
	}

	fprintf(tests->stderr, "%sTests performed with console audio system:       ", tests->msg_prefix);
	if (tests->stats_console.failures + tests->stats_console.successes) {
		fprintf(tests->stderr, "errors: %03d, total: %03d\n",
		       tests->stats_console.failures, tests->stats_console.failures + tests->stats_console.successes);
	} else {
		fprintf(tests->stderr, "no tests were performed\n");
	}

	fprintf(tests->stderr, "%sTests performed with OSS audio system:           ", tests->msg_prefix);
	if (tests->stats_oss.failures + tests->stats_oss.successes) {
		fprintf(tests->stderr, "errors: %03d, total: %03d\n",
			tests->stats_oss.failures, tests->stats_oss.failures + tests->stats_oss.successes);
	} else {
		fprintf(tests->stderr, "no tests were performed\n");
	}

	fprintf(tests->stderr, "%sTests performed with ALSA audio system:          ", tests->msg_prefix);
	if (tests->stats_alsa.failures + tests->stats_alsa.successes) {
		fprintf(tests->stderr, "errors: %03d, total: %03d\n",
		       tests->stats_alsa.failures, tests->stats_alsa.failures + tests->stats_alsa.successes);
	} else {
		fprintf(tests->stderr, "no tests were performed\n");
	}

	fprintf(tests->stderr, "%sTests performed with PulseAudio audio system:    ", tests->msg_prefix);
	if (tests->stats_pa.failures + tests->stats_pa.successes) {
		fprintf(tests->stderr, "errors: %03d, total: %03d\n",
		       tests->stats_pa.failures, tests->stats_pa.failures + tests->stats_pa.successes);
	} else {
		fprintf(tests->stderr, "no tests were performed\n");
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

	cw_test_init(&g_tests, stdout, stderr, MSG_PREFIX);

	if (!g_tests.process_args(&g_tests, argc, argv)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}

	atexit(cw_test_print_stats_wrapper);

	/* Arrange for the test to exit on a range of signals. */
	for (int i = 0; SIGNALS[i] != 0; i++) {
		if (!cw_register_signal_handler(SIGNALS[i], SIG_DFL)) {
			fprintf(stderr, MSG_PREFIX ": ERROR: cw_register_signal_handler\n");
			exit(EXIT_FAILURE);
		}
	}

	rv = cw_test_modules_with_sound_systems(&g_tests, cw_test_modules_with_current_sound_system);

	return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
