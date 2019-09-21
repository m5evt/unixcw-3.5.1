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

#include "config.h"

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h> /* gettimeofday() */
#include <signal.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>

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




#define _XOPEN_SOURCE 600 /* signaction() + SA_RESTART */




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_dev;



static char const * prefix = "libcw unit tests";


enum {
	CW_MODULE_TQ,
	CW_MODULE_GEN,
	CW_MODULE_KEY,
	CW_MODULE_REC,
	CW_MODULE_OTHER,

	CW_MODULE_MAX
};





static cw_test_stats_t unit_test_statistics[CW_AUDIO_SOUNDCARD][CW_MODULE_MAX];

static void cw_test_print_stats(void);
static void cw_test_print_help(char const * progname);
static int cw_test_args(int argc, char * const argv[], char * sound_systems, size_t systems_max, char * modules, size_t modules_max);

static int cw_test_run(char const * audio_systems, char const * modules);
static int cw_test_run_with_audio(int audio_system, char const * modules);
static void cw_test_setup(cw_gen_t *gen);
static void signal_handler(int signal_number);
static void register_signal_handler(void);



/* ******************************************************************** */
/*                 Unit tests for internal functions                    */
/* ******************************************************************** */





/* Unit tests for internal functions (and also some public functions)
   defined in libcw.c.

   See also libcw_test_public.c and libcw_test_simple_gen.c. */

#include <stdio.h>
#include <assert.h>





typedef unsigned int (*cw_test_function_stats_t)(cw_test_stats_t * stats);
typedef unsigned int (*cw_test_function_stats_key_t)(cw_key_t * key, cw_test_stats_t * stats);
typedef unsigned int (*cw_test_function_stats_gen_t)(cw_gen_t * gen, cw_test_stats_t * stats);
typedef unsigned int (*cw_test_function_stats_tq_t)(cw_gen_t * gen, cw_test_stats_t * stats);



static cw_test_function_stats_t cw_unit_tests_other_s[] = {

	/* cw_utils module */
	test_cw_timestamp_compare_internal,
	test_cw_timestamp_validate_internal,
	test_cw_usecs_to_timespec_internal,
	test_cw_version_internal,
	test_cw_license_internal,
	test_cw_get_x_limits_internal,

	/* cw_data module */
	test_cw_representation_to_hash_internal,
	test_cw_representation_to_character_internal,
	test_cw_representation_to_character_internal_speed,
	test_character_lookups_internal,
	test_prosign_lookups_internal,
	test_phonetic_lookups_internal,
	test_validate_character_and_string_internal,
	test_validate_representation_internal,


	/* cw_debug module */
	test_cw_debug_flags_internal,

	NULL
};



/* Tests that are dependent on a sound system being configured.
   Tone queue module functions */
static cw_test_function_stats_tq_t cw_unit_tests_tq[] = {
	test_cw_tq_test_capacity_1,
	test_cw_tq_test_capacity_2,
	test_cw_tq_wait_for_level_internal,
	test_cw_tq_is_full_internal,
	test_cw_tq_enqueue_dequeue_internal,
	test_cw_tq_enqueue_args_internal,
	test_cw_tq_new_delete_internal,
	test_cw_tq_get_capacity_internal,
	test_cw_tq_length_internal,
	test_cw_tq_prev_index_internal,
	test_cw_tq_next_index_internal,
	test_cw_tq_callback,
	test_cw_tq_operations_1,
	test_cw_tq_operations_2,
	test_cw_tq_operations_3,

	NULL
};




/* Tests that are dependent on a sound system being configured.
   Generator module functions. */
static cw_test_function_stats_gen_t cw_unit_tests_gen[] = {

	test_cw_gen_set_tone_slope,
	test_cw_gen_tone_slope_shape_enums,
	test_cw_gen_new_delete,
	test_cw_gen_get_timing_parameters_internal,
	test_cw_gen_parameter_getters_setters,
	test_cw_gen_volume_functions,
	test_cw_gen_enqueue_primitives,
	test_cw_gen_enqueue_representations,
	test_cw_gen_enqueue_character_and_string,
	test_cw_gen_forever_internal,
	NULL
};




/* 'key' module. */
static cw_test_function_stats_key_t cw_unit_tests_key[] = {
	test_keyer,
	test_straight_key,
	NULL
};


static cw_test_function_stats_t cw_unit_tests_rec1[] = {
	test_cw_rec_get_parameters,
	test_cw_rec_parameter_getters_setters_1,
	test_cw_rec_parameter_getters_setters_2,
	test_cw_rec_identify_mark_internal,
	test_cw_rec_test_with_base_constant,
	test_cw_rec_test_with_random_constant,
	test_cw_rec_test_with_random_varying,

	NULL
};




/**
   \return EXIT_SUCCESS if all tests complete successfully,
   \return EXIT_FAILURE otherwise
*/
int main(int argc, char *const argv[])
{
	fprintf(stderr, "%s\n\n", prefix);

	//cw_debug_set_flags(&cw_debug_object, CW_DEBUG_RECEIVE_STATES | CW_DEBUG_TONE_QUEUE | CW_DEBUG_GENERATOR | CW_DEBUG_KEYING);
	//cw_debug_object.level = CW_DEBUG_ERROR;

	//cw_debug_set_flags(&cw_debug_object_dev, CW_DEBUG_RECEIVE_STATES | CW_DEBUG_TONE_QUEUE | CW_DEBUG_GENERATOR | CW_DEBUG_KEYING);
	//cw_debug_object_dev.level = CW_DEBUG_DEBUG;


	struct timeval seed;
	gettimeofday(&seed, NULL);
	fprintf(stderr, "%s: seed: %lu\n", prefix, (long unsigned) seed.tv_sec);
	srand(seed.tv_usec);


	/* Obtain a bitmask of the tests to run from the command line
	   arguments. If none, then default to ~0, which effectively
	   requests all tests. */
	unsigned int testset = 0;
	if (argc > 1) {
		testset = 0;
		for (int arg = 1; arg < argc; arg++) {
			unsigned int test = strtoul(argv[arg], NULL, 0);
			testset |= 1 << test;
		}
	} else {
		testset = ~0;
	}


#define CW_SYSTEMS_MAX (sizeof ("ncoap"))
	char sound_systems[CW_SYSTEMS_MAX + 1];
#define CW_MODULES_MAX (sizeof ("gtkro"))
	char modules[CW_MODULES_MAX + 1];
	modules[0] = '\0';

	if (!cw_test_args(argc, argv, sound_systems, CW_SYSTEMS_MAX, modules, CW_MODULES_MAX)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}


	atexit(cw_test_print_stats);
	register_signal_handler();

	int rv = cw_test_run(sound_systems, modules);

	/* "make check" facility requires this message to be
	   printed on stdout; don't localize it */
	fprintf(stdout, "\n%s: test result: success\n\n", prefix);

	return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}



/* Show the signal caught, and exit. */
void signal_handler(int signal_number)
{
	fprintf(stderr, "\n%s: caught signal %d, exiting...\n", prefix, signal_number);
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
			fprintf(stderr, "%s: can't register signal: %s\n", prefix, strerror(errno));
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
int cw_test_run_with_audio(int audio_system, char const * modules)
{
	cw_gen_t * gen = NULL;
	cw_key_t * key = NULL;

	if (strstr(modules, "k") || strstr(modules, "g") || strstr(modules, "t")) {
		gen = cw_gen_new(audio_system, NULL);
		if (!gen) {
			fprintf(stderr, "%s: can't create generator, stopping the test\n", prefix);
			return -1;
		}

		if (strstr(modules, "k")) {
			key = cw_key_new();
			if (!key) {
				fprintf(stderr, "%s: can't create key, stopping the test\n", prefix);
				return -1;
			}
			cw_key_register_generator(key, gen);
		}

		if (CW_SUCCESS != cw_gen_start(gen)) {
			fprintf(stderr, "%s: can't start generator, stopping the test\n", prefix);
			cw_gen_delete(&gen);
			if (key) {
				cw_key_delete(&key);
			}
			return -1;
		}
	}



	if (strstr(modules, "t")) {
		int i = 0;
		while (cw_unit_tests_tq[i]) {
			cw_test_setup(gen);
			(*cw_unit_tests_tq[i])(gen, &unit_test_statistics[audio_system][CW_MODULE_TQ]);
			i++;
		}
		fprintf(out_file, "\n");
	}

	if (strstr(modules, "g")) {
		int i = 0;
		while (cw_unit_tests_gen[i]) {
			cw_test_setup(gen);
			(*cw_unit_tests_gen[i])(gen, &unit_test_statistics[audio_system][CW_MODULE_GEN]);
			i++;
		}
		fprintf(out_file, "\n");
	}

	if (strstr(modules, "k")) {
		int i = 0;
		while (cw_unit_tests_key[i]) {
			cw_test_setup(gen);
	                (*cw_unit_tests_key[i])(key, &unit_test_statistics[audio_system][CW_MODULE_KEY]);
			i++;
		}
		fprintf(out_file, "\n");
	}

	if (strstr(modules, "r")) {
		int i = 0;
		while (cw_unit_tests_rec1[i]) {
	                (*cw_unit_tests_rec1[i])(&unit_test_statistics[audio_system][CW_MODULE_REC]);
			i++;
		}
		fprintf(out_file, "\n");
	}

	if (strstr(modules, "o")) {
		int i = 0;
		while (cw_unit_tests_other_s[i]) {
	                (*cw_unit_tests_other_s[i])(&unit_test_statistics[audio_system][CW_MODULE_OTHER]);
			i++;
		}
		fprintf(out_file, "\n");
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
   function calls cw_test_run_with_audio() to do the configuration and
   run the tests.

   \p audio_systems is a list of audio systems to be tested: "ncoap".
   Pass NULL pointer to attempt to test all of audio systems supported
   by libcw.

   \param modules is a list of libcw modules to be tested.

   \param audio_systems - list of audio systems to be tested
   \param modules - list of modules systems to be tested
*/
int cw_test_run(char const * audio_systems, char const * modules)
{
	int n = 0, c = 0, o = 0, a = 0, p = 0;


	if (!audio_systems || strstr(audio_systems, "n")) {
		if (cw_is_null_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "%s: testing with null output\n", prefix);
			n = cw_test_run_with_audio(CW_AUDIO_NULL, modules);
		} else {
			fprintf(stderr, "%s: null output not available\n", prefix);
		}
	}

	if (!audio_systems || strstr(audio_systems, "c")) {
		if (cw_is_console_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "%s: testing with console output\n", prefix);
			c = cw_test_run_with_audio(CW_AUDIO_CONSOLE, modules);
		} else {
			fprintf(stderr, "%s: console output not available\n", prefix);
		}
	}

	if (!audio_systems || strstr(audio_systems, "o")) {
		if (cw_is_oss_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "%s: testing with OSS output\n", prefix);
			o = cw_test_run_with_audio(CW_AUDIO_OSS, modules);
		} else {
			fprintf(stderr, "%s: OSS output not available\n", prefix);
		}
	}

	if (!audio_systems || strstr(audio_systems, "a")) {
		if (cw_is_alsa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "%s: testing with ALSA output\n", prefix);
			a = cw_test_run_with_audio(CW_AUDIO_ALSA, modules);
		} else {
			fprintf(stderr, "%s: Alsa output not available\n", prefix);
		}
	}

	if (!audio_systems || strstr(audio_systems, "p")) {
		if (cw_is_pa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "%s: testing with PulseAudio output\n", prefix);
			p = cw_test_run_with_audio(CW_AUDIO_PA, modules);
		} else {
			fprintf(stderr, "%s: PulseAudio output not available\n", prefix);
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
	fprintf(stderr, "\n\nlibcw: Statistics of tests: (total/failures)\n\n");

        //                     123 12345678901234 12345678901234 12345678901234 12345678901234 12345678901234
	fprintf(stderr,       "   | tone queue   | generator    | key          | receiver     | other        |\n");
	fprintf(stderr,       " -----------------------------------------------------------------------------|\n");
	#define LINE_FORMAT   " %c |% 10d/% 3d|% 10d/% 3d|% 10d/% 3d|% 10d/% 3d|% 10d/% 3d|\n"


	char audio_systems[] = " NCOAP";

	for (int i = CW_AUDIO_NULL; i <= CW_AUDIO_PA; i++) {
		fprintf(stderr, LINE_FORMAT,
			audio_systems[i],
			unit_test_statistics[i][CW_MODULE_TQ].failures    + unit_test_statistics[i][CW_MODULE_TQ].successes,    unit_test_statistics[i][CW_MODULE_TQ].failures,
			unit_test_statistics[i][CW_MODULE_GEN].failures   + unit_test_statistics[i][CW_MODULE_GEN].successes,   unit_test_statistics[i][CW_MODULE_GEN].failures,
			unit_test_statistics[i][CW_MODULE_KEY].failures   + unit_test_statistics[i][CW_MODULE_KEY].successes,   unit_test_statistics[i][CW_MODULE_KEY].failures,
			unit_test_statistics[i][CW_MODULE_REC].failures   + unit_test_statistics[i][CW_MODULE_REC].successes,   unit_test_statistics[i][CW_MODULE_REC].failures,
			unit_test_statistics[i][CW_MODULE_OTHER].failures + unit_test_statistics[i][CW_MODULE_OTHER].successes, unit_test_statistics[i][CW_MODULE_OTHER].failures);
	}

	return;
}




int cw_test_args(int argc, char * const argv[], char * sound_systems, size_t systems_max, char * modules, size_t modules_max)
{
	strncpy(sound_systems, "ncoap", systems_max);
	sound_systems[systems_max] = '\0';

	strncpy(modules, "gtkro", modules_max);
	modules[modules_max] = '\0';

	if (argc == 1) {
		fprintf(stderr, "%s: sound systems = \"%s\"\n", prefix, sound_systems);
		fprintf(stderr, "%s: modules = \"%s\"\n", prefix, modules);
		return CW_SUCCESS;
	}

	int opt;
	while ((opt = getopt(argc, argv, "m:s:")) != -1) {
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
		case 'm':
			{
				size_t len = strlen(optarg);
				if (!len || len > modules_max) {
					return CW_FAILURE;
				}

				int j = 0;
				for (size_t i = 0; i < len; i++) {
					if (optarg[i] != 'g'       /* Generator. */
					    && optarg[i] != 't'    /* Tone queue. */
					    && optarg[i] != 'k'    /* Morse key. */
					    && optarg[i] != 'r'    /* Receiver. */
					    && optarg[i] != 'o') { /* Other. */

						return CW_FAILURE;
					} else {
						modules[j] = optarg[i];
						j++;
					}
				}
				modules[j] = '\0';

			}

			break;
		default: /* '?' */
			return CW_FAILURE;
		}
	}

	fprintf(stderr, "%s: sound systems = \"%s\"\n", prefix, sound_systems);
	fprintf(stderr, "%s: modules = \"%s\"\n", prefix, modules);
	return CW_SUCCESS;
}





void cw_test_print_help(char const * progname)
{
	fprintf(stderr, "Usage: %s [-s <sound systems>] [-m <modules>]\n\n", progname);
	fprintf(stderr, "       <sound system> is one or more of those:\n");
	fprintf(stderr, "       n - null\n");
	fprintf(stderr, "       c - console\n");
	fprintf(stderr, "       o - OSS\n");
	fprintf(stderr, "       a - ALSA\n");
	fprintf(stderr, "       p - PulseAudio\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "       <modules> is one or more of those:\n");
	fprintf(stderr, "       g - generator\n");
	fprintf(stderr, "       t - tone queue\n");
	fprintf(stderr, "       k - Morse key\n");
	fprintf(stderr, "       r - receiver\n");
	fprintf(stderr, "       o - other\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "       If no argument is provided, the program will attempt to test all audio systems and all modules\n");

	return;
}
