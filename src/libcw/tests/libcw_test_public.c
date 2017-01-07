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
