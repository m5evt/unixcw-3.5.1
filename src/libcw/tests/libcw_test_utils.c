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


/**
   \file libcw_test.c

   \brief Utility functions for test executables.
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
#include <stdarg.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif


#include "libcw.h"
#include "libcw_debug.h"
#include "libcw_test_utils.h"




static bool cw_test_expect_eq_int(struct cw_test_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
static bool cw_test_expect_eq_int_errors_only(struct cw_test_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
static void cw_test_print_test_header(cw_test_t * self, const char * text);
static void cw_test_print_test_footer(cw_test_t * self, const char * text);
static void cw_test_append_status_string(cw_test_t * self, char * msg_buf, int n, const char * status_string);




int cw_test_args(int argc, char *const argv[],
		 char *sound_systems, size_t systems_max,
		 char *modules, size_t modules_max)
{
	strncpy(sound_systems, "ncoap", systems_max);
	sound_systems[systems_max] = '\0';

	strncpy(modules, "gtko", modules_max);
	modules[modules_max] = '\0';

	if (argc == 1) {
		fprintf(stderr, "sound systems = \"%s\"\n", sound_systems);
		fprintf(stderr, "modules = \"%s\"\n", modules);
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

	fprintf(stderr, "Sound systems = \"%s\"\n", sound_systems);
	fprintf(stderr, "Modules = \"%s\"\n", modules);
	return CW_SUCCESS;
}





void cw_test_print_help(const char *progname)
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
	fprintf(stderr, "       o - other\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "       If no argument is provided, the program will attempt to test all audio systems and all modules\n");

	return;
}




bool cw_test_expect_eq_int(struct cw_test_t * self, int expected_value, int received_value, const char * fmt, ...)
{
	bool as_expected = false;
	char va_buf[128] = { 0 };

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	char msg_buf[1024] = { 0 };
	int n = snprintf(msg_buf, sizeof (msg_buf), "%s", self->msg_prefix);
	const int message_len = n + snprintf(msg_buf + n, sizeof (msg_buf) - n, "%s", va_buf);
	n += snprintf(msg_buf + n, sizeof (msg_buf) - n, "%-*s", (int) (self->console_n_cols - n), va_buf);


	if (expected_value == received_value) {
		self->stats->successes++;

		cw_test_append_status_string(self, msg_buf, message_len, "[ OK ]");
		fprintf(self->stderr, "%s\n", msg_buf);

		as_expected = true;
	} else {
		self->stats->failures++;

		cw_test_append_status_string(self, msg_buf, message_len, "[FAIL]");
		fprintf(self->stderr, "%s\n", msg_buf);
		fprintf(self->stderr, "   ***   expected %d, got %d   ***\n", expected_value, received_value);

		as_expected = false;
	}


	return as_expected;
}




/* Append given status string at the end of buffer, but within cw_test::console_n_cols limit. */
void cw_test_append_status_string(cw_test_t * self, char * msg_buf, int n, const char * status_string)
{
	const char * separator = " "; /* Separator between test message and test status string, for better visibility of status string. */
	const size_t space_left = self->console_n_cols - n;

	if (space_left > strlen(separator) + strlen(status_string)) {
		sprintf(msg_buf + self->console_n_cols - strlen(separator) - strlen(status_string), "%s%s", separator, status_string);
	} else {
		sprintf(msg_buf + self->console_n_cols - strlen("...") - strlen(separator) - strlen(status_string), "...%s%s", separator, status_string);
	}
}




bool cw_test_expect_eq_int_errors_only(struct cw_test_t * self, int expected_value, int received_value, const char * fmt, ...)
{
	bool as_expected = true;
	char buf[128] = { 0 };

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	if (expected_value == received_value) {
		as_expected = true;
	} else {
		const int n = fprintf(self->stderr, "%s%s", self->msg_prefix, buf);
		self->stats->failures++;
		fprintf(self->stderr, "%*s", self->console_n_cols - n, "failure: ");
		fprintf(self->stderr, "expected %d, got %d\n", expected_value, received_value);
		as_expected = false;
	}

	return as_expected;
}




void cw_test_print_test_header(cw_test_t * self, const char * text)
{
	fprintf(self->stderr, "\n%sbeginning of test: %s:\n", self->msg_prefix, text);
}




void cw_test_print_test_footer(cw_test_t * self, const char * text)
{
	const int n = fprintf(self->stderr, "%send of test: %s: ", self->msg_prefix, text);
	fprintf(self->stderr, "%*s\n", self->console_n_cols - n, "completed\n");
}




void cw_test_init(cw_test_t * self, FILE * stdout, FILE * stderr, const char * msg_prefix)
{
	self->stdout = stdout;
	self->stderr = stderr;
	self->expect_eq_int = cw_test_expect_eq_int;
	self->expect_eq_int_errors_only = cw_test_expect_eq_int_errors_only;
	self->print_test_header = cw_test_print_test_header;
	self->print_test_footer = cw_test_print_test_footer;

	self->console_n_cols = default_cw_test_print_n_chars;

	snprintf(self->msg_prefix, sizeof (self->msg_prefix), "%s", msg_prefix);

	const time_t seed = time(0);
	srand(seed);
	fprintf(self->stdout, "Random seed = %ld\n", seed);
}
