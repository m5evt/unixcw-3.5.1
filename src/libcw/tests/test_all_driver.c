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

#define _XOPEN_SOURCE 600 /* signaction() + SA_RESTART */
#include "config.h"




#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>




#include "libcw_gen.h"
#include "libcw_debug.h"
#include "libcw2.h"

#include "libcw_test_framework.h"
#include "test_all_sets.h"




#define MSG_PREFIX  "libcw modern API"




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_dev;

extern cw_test_function_stats_t cw_unit_tests_other_s[];
extern cw_test_function_stats_tq_t cw_unit_tests_tq[];
extern cw_test_function_stats_gen_t cw_unit_tests_gen[];
extern cw_test_function_stats_key_t cw_unit_tests_key[];
extern cw_test_function_stats_t cw_unit_tests_rec1[];




static cw_test_executor_t g_tests_executor;




static void cw_test_print_stats_wrapper(void);
static int cw_test_modules_with_current_sound_system(cw_test_executor_t * executor);
static void cw_test_setup(cw_gen_t *gen);
static void signal_handler(int signal_number);
static void register_signal_handler(void);




/**
   \return EXIT_SUCCESS if all tests complete successfully,
   \return EXIT_FAILURE otherwise
*/
int main(int argc, char *const argv[])
{
	fprintf(stderr, "%s\n\n", MSG_PREFIX);

	//cw_debug_set_flags(&cw_debug_object, CW_DEBUG_RECEIVE_STATES | CW_DEBUG_TONE_QUEUE | CW_DEBUG_GENERATOR | CW_DEBUG_KEYING);
	//cw_debug_object.level = CW_DEBUG_ERROR;

	//cw_debug_set_flags(&cw_debug_object_dev, CW_DEBUG_RECEIVE_STATES | CW_DEBUG_TONE_QUEUE | CW_DEBUG_GENERATOR | CW_DEBUG_KEYING);
	//cw_debug_object_dev.level = CW_DEBUG_DEBUG;


	cw_test_init(&g_tests_executor, stdout, stderr, MSG_PREFIX);

	if (!g_tests_executor.process_args(&g_tests_executor, argc, argv)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}


	atexit(cw_test_print_stats_wrapper);
	register_signal_handler();

	int rv = cw_test_modules_with_sound_systems(&g_tests_executor, cw_test_modules_with_current_sound_system);

	/* "make check" facility requires this message to be
	   printed on stdout; don't localize it */
	fprintf(stdout, "\n%s: test result: success\n\n", g_tests_executor.msg_prefix);

	return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}




/* Show the signal caught, and exit. */
void signal_handler(int signal_number)
{
	fprintf(stderr, "\n%s: caught signal %d, exiting...\n", MSG_PREFIX, signal_number);
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
			fprintf(stderr, "%s: can't register signal: %s\n", MSG_PREFIX, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	return;
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
   audio system specified with \p audio_system.  Tests should be
   performed on modules specified with \p modules.

   \param audio_system - audio system to use for tests
   \param modules - libcw modules to be tested

   \return 0
*/
int cw_test_modules_with_current_sound_system(cw_test_executor_t * executor)
{
	cw_gen_t * gen = NULL;
	cw_key_t * key = NULL;

	fprintf(executor->stderr, "%sTesting with %s sound system\n", executor->msg_prefix, executor->get_current_sound_system_label(executor));

	if (strstr(executor->tested_modules, "k") || strstr(executor->tested_modules, "g") || strstr(executor->tested_modules, "t")) {
		gen = cw_gen_new(executor->current_sound_system, NULL);
		if (!gen) {
			fprintf(stderr, "%s: can't create generator, stopping the test\n", executor->msg_prefix);
			return -1;
		}

		if (strstr(executor->tested_modules, "k")) {
			key = cw_key_new();
			if (!key) {
				fprintf(stderr, "%s: can't create key, stopping the test\n", executor->msg_prefix);
				return -1;
			}
			cw_key_register_generator(key, gen);
		}

		if (CW_SUCCESS != cw_gen_start(gen)) {
			fprintf(stderr, "%s: can't start generator, stopping the test\n", executor->msg_prefix);
			cw_gen_delete(&gen);
			if (key) {
				cw_key_delete(&key);
			}
			return -1;
		}
	}



	if (executor->should_test_module(executor, "t")) {
		int i = 0;
		while (cw_unit_tests_tq[i]) {
			cw_test_setup(gen);
			(*cw_unit_tests_tq[i])(gen, &executor->stats2[executor->current_sound_system][LIBCW_MODULE_TQ]);
			i++;
		}
		fprintf(executor->stderr, "\n");
	}

	if (executor->should_test_module(executor, "g")) {
		int i = 0;
		while (cw_unit_tests_gen[i]) {
			cw_test_setup(gen);
			(*cw_unit_tests_gen[i])(gen, &executor->stats2[executor->current_sound_system][LIBCW_MODULE_GEN]);
			i++;
		}
		fprintf(executor->stderr, "\n");
	}

	if (executor->should_test_module(executor, "k")) {
		int i = 0;
		while (cw_unit_tests_key[i]) {
			cw_test_setup(gen);
	                (*cw_unit_tests_key[i])(key, &executor->stats2[executor->current_sound_system][LIBCW_MODULE_KEY]);
			i++;
		}
		fprintf(executor->stderr, "\n");
	}

	if (executor->should_test_module(executor, "r")) {
		int i = 0;
		while (cw_unit_tests_rec1[i]) {
	                (*cw_unit_tests_rec1[i])(&executor->stats2[executor->current_sound_system][LIBCW_MODULE_REC]);
			i++;
		}
		fprintf(executor->stderr, "\n");
	}

	if (executor->should_test_module(executor, "o")) {
		int i = 0;
		while (cw_unit_tests_other_s[i]) {
	                (*cw_unit_tests_other_s[i])(&executor->stats2[executor->current_sound_system][LIBCW_MODULE_OTHER]);
			i++;
		}
		fprintf(executor->stderr, "\n");
	}



	if (key) {
		sleep(1);
		cw_key_delete(&key);
	}

	if (gen) {
		sleep(1);
		cw_gen_stop(gen);
		sleep(1);
		cw_gen_delete(&gen);
	}

	/* All tests done; return success if no failures,
	   otherwise return an error status code. */
	return 0;
}




/**
   \brief Run a series of tests for specified audio systems and modules

   Function attempts to run a set of testcases for every audio system
   specified in \p audio_systems and for every module specified in \p modules.

   These testcases require some kind of audio system configured. The
   function calls cw_test_modules_with_current_sound_system() to do the configuration and
   run the tests.

   \p audio_systems is a list of audio systems to be tested: "ncoap".
   Pass NULL pointer to attempt to test all of audio systems supported
   by libcw.

   \param modules is a list of libcw modules to be tested.

   \param audio_systems - list of audio systems to be tested
   \param modules - list of modules systems to be tested
*/
void cw_test_print_stats_wrapper(void)
{
	g_tests_executor.print_test_stats(&g_tests_executor);
}
