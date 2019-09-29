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

#include "config.h"

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> /* sleep() */

#include "libcw.h"

#include "libcw_test_framework.h"

#include "libcw_debug.h"
#include "libcw_data.h"
#include "libcw_tq.h"

#include "libcw_null.h"
#include "libcw_console.h"
#include "libcw_oss.h"





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;


typedef bool (*predicate_t)(const char *device);
static void main_helper(int audio_system, const char *name, const char *device, predicate_t predicate);





int main(int argc, char *const argv[])
{
	// cw_debug_set_flags(&cw_debug_object, CW_DEBUG_RECEIVE_STATES | CW_DEBUG_TONE_QUEUE | CW_DEBUG_GENERATOR | CW_DEBUG_KEYING);
	// cw_debug_object.level = CW_DEBUG_DEBUG;

	cw_test_executor_t cte;
	cw_test_init(&cte, stdout, stderr, "simple gen test");

	if (!cte.process_args(&cte, argc, argv)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}

	if (strstr(cte.tested_sound_systems, "n")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with null output\n");
		main_helper(CW_AUDIO_NULL,    "Null",        CW_DEFAULT_NULL_DEVICE,      cw_is_null_possible);
	}

	if (strstr(cte.tested_sound_systems, "c")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with console output\n");
		main_helper(CW_AUDIO_CONSOLE, "console",     CW_DEFAULT_CONSOLE_DEVICE,   cw_is_console_possible);
	}

	if (strstr(cte.tested_sound_systems, "o")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with OSS output\n");
		main_helper(CW_AUDIO_OSS,     "OSS",         CW_DEFAULT_OSS_DEVICE,       cw_is_oss_possible);
	}

	if (strstr(cte.tested_sound_systems, "a")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with ALSA output\n");
		main_helper(CW_AUDIO_ALSA,    "ALSA",        CW_DEFAULT_ALSA_DEVICE,      cw_is_alsa_possible);
	}

	if (strstr(cte.tested_sound_systems, "p")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with PulseAudio output\n");
		main_helper(CW_AUDIO_PA,      "PulseAudio",  CW_DEFAULT_PA_DEVICE,        cw_is_pa_possible);
	}

	sleep(2);

	return 0;
}





void main_helper(int audio_system, const char *name, const char *device, predicate_t predicate)
{
	int rv = CW_FAILURE;

	rv = predicate(device);
	if (rv == CW_SUCCESS) {
		} else {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
				      "libcw: can't create %s generator", name);
		}
	} else {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: %s output is not available", name);
	}
}
