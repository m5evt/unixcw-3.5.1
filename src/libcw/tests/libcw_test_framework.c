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
#include "libcw_test_framework.h"




static bool cw_test_expect_eq_int(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
static bool cw_test_expect_eq_int_errors_only(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));

static bool cw_test_expect_null_pointer(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));
static bool cw_test_expect_null_pointer_errors_only(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));

static bool cw_test_expect_valid_pointer(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));
static bool cw_test_expect_valid_pointer_errors_only(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));


static void cw_test_print_test_header(cw_test_executor_t * self, const char * text);
static void cw_test_print_test_footer(cw_test_executor_t * self, const char * text);
static void cw_test_append_status_string(cw_test_executor_t * self, char * msg_buf, int n, const char * status_string);

static int cw_test_process_args(cw_test_executor_t * self, int argc, char * const argv[]);
static void cw_test_print_args_summary(cw_test_executor_t * self);

static bool cw_test_test_topic_was_requested(cw_test_executor_t * self, int libcw_test_topic);
static bool cw_test_sound_system_was_requested(cw_test_executor_t * self, int sound_system);

static void cw_test_set_current_sound_system(cw_test_executor_t * self, int sound_system);
static const char * cw_test_get_current_sound_system_label(cw_test_executor_t * self);
static void cw_test_print_test_stats(cw_test_executor_t * self);

static void cw_test_log_info(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void cw_test_log_info_cont(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void cw_test_log_err(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));

static void cw_test_print_sound_systems(cw_test_executor_t * self, int * sound_systems);
static void cw_test_print_topics(cw_test_executor_t * self, int * topics);

static bool cw_test_test_topic_is_member(cw_test_executor_t * cte, int topic, int * topics);
static bool cw_test_sound_system_is_member(cw_test_executor_t * cte, int sound_system, int * sound_systems);





int cw_test_process_args(cw_test_executor_t * self, int argc, char * const argv[])
{
	self->tested_sound_systems[0] = CW_AUDIO_NULL;
	self->tested_sound_systems[1] = CW_AUDIO_CONSOLE;
	self->tested_sound_systems[2] = CW_AUDIO_OSS;
	self->tested_sound_systems[3] = CW_AUDIO_ALSA;
	self->tested_sound_systems[4] = CW_AUDIO_PA;
	self->tested_sound_systems[5] = LIBCW_TEST_SOUND_SYSTEM_MAX;

	self->tested_topics[0] = LIBCW_TEST_TOPIC_TQ;
	self->tested_topics[1] = LIBCW_TEST_TOPIC_GEN;
	self->tested_topics[2] = LIBCW_TEST_TOPIC_KEY;
	self->tested_topics[3] = LIBCW_TEST_TOPIC_REC;
	self->tested_topics[4] = LIBCW_TEST_TOPIC_DATA;
	self->tested_topics[5] = LIBCW_TEST_TOPIC_OTHER;
	self->tested_topics[6] = LIBCW_TEST_TOPIC_MAX;

	if (argc == 1) {
		return CW_SUCCESS;
	}

	size_t optarg_len = 0;
	int dest_idx = 0;

	int opt;
	while ((opt = getopt(argc, argv, "m:s:")) != -1) {
		switch (opt) {
		case 's':
			optarg_len = strlen(optarg);
			if (optarg_len > strlen(LIBCW_TEST_ALL_SOUND_SYSTEMS)) {
				fprintf(stderr, "Too many values for 'sound system' option: '%s'\n", optarg);
				return CW_FAILURE;
			}

			dest_idx = 0;
			for (size_t i = 0; i < optarg_len; i++) {
				const int val = optarg[i];
				if (NULL == strchr(LIBCW_TEST_ALL_SOUND_SYSTEMS, val)) {
					fprintf(stderr, "Unsupported sound system '%c'\n", val);
					return CW_FAILURE;
				}
				switch (val) {
				case 'n':
					self->tested_sound_systems[dest_idx] = CW_AUDIO_NULL;
					break;
				case 'c':
					self->tested_sound_systems[dest_idx] = CW_AUDIO_CONSOLE;
					break;
				case 'o':
					self->tested_sound_systems[dest_idx] = CW_AUDIO_OSS;
					break;
				case 'a':
					self->tested_sound_systems[dest_idx] = CW_AUDIO_ALSA;
					break;
				case 'p':
					self->tested_sound_systems[dest_idx] = CW_AUDIO_PA;
					break;
				default:
					fprintf(stderr, "Unsupported sound system '%c'\n", val);
					return CW_FAILURE;
				}
				dest_idx++;
			}
			self->tested_sound_systems[dest_idx] = LIBCW_TEST_SOUND_SYSTEM_MAX;
			break;

		case 'm':
			optarg_len = strlen(optarg);
			if (optarg_len > strlen(LIBCW_TEST_ALL_TOPICS)) {
				fprintf(stderr, "Too many values for 'topics' option: '%s'\n", optarg);
				return CW_FAILURE;
			}

			dest_idx = 0;
			for (size_t i = 0; i < optarg_len; i++) {
				const int val = optarg[i];
				if (NULL == strchr(LIBCW_TEST_ALL_TOPICS, val)) {
					fprintf(stderr, "Unsupported topic '%c'\n", val);
					return CW_FAILURE;
				}
				switch (val) {
				case 't':
					self->tested_topics[dest_idx] = LIBCW_TEST_TOPIC_TQ;
					break;
				case 'g':
					self->tested_topics[dest_idx] = LIBCW_TEST_TOPIC_GEN;
					break;
				case 'k':
					self->tested_topics[dest_idx] = LIBCW_TEST_TOPIC_KEY;
					break;
				case 'r':
					self->tested_topics[dest_idx] = LIBCW_TEST_TOPIC_REC;
					break;
				case 'd':
					self->tested_topics[dest_idx] = LIBCW_TEST_TOPIC_DATA;
					break;
				case 'o':
					self->tested_topics[dest_idx] = LIBCW_TEST_TOPIC_OTHER;
					break;
				default:
					fprintf(stderr, "Unsupported topic: '%c'\n", val);
					return CW_FAILURE;
				}
				dest_idx++;
			}
			self->tested_topics[dest_idx] = LIBCW_TEST_TOPIC_MAX;
			break;

		default: /* '?' */
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}




void cw_test_print_help(const char * progname)
{
	fprintf(stderr, "Usage: %s [-s <sound systems>] [-t <topics>]\n\n", progname);
	fprintf(stderr, "       <sound system> is one or more of those:\n");
	fprintf(stderr, "       n - null\n");
	fprintf(stderr, "       c - console\n");
	fprintf(stderr, "       o - OSS\n");
	fprintf(stderr, "       a - ALSA\n");
	fprintf(stderr, "       p - PulseAudio\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "       <topics> is one or more of those:\n");
	fprintf(stderr, "       g - generator\n");
	fprintf(stderr, "       t - tone queue\n");
	fprintf(stderr, "       k - Morse key\n");
	fprintf(stderr, "       r - receiver\n");
	fprintf(stderr, "       o - other\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "       If no argument is provided, the program will attempt to test all audio systems and all topics\n");

	return;
}




bool cw_test_expect_eq_int(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...)
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
void cw_test_append_status_string(cw_test_executor_t * self, char * msg_buf, int n, const char * status_string)
{
	const char * separator = " "; /* Separator between test message and test status string, for better visibility of status string. */
	const size_t space_left = self->console_n_cols - n;

	if (space_left > strlen(separator) + strlen(status_string)) {
		sprintf(msg_buf + self->console_n_cols - strlen(separator) - strlen(status_string), "%s%s", separator, status_string);
	} else {
		sprintf(msg_buf + self->console_n_cols - strlen("...") - strlen(separator) - strlen(status_string), "...%s%s", separator, status_string);
	}
}




bool cw_test_expect_eq_int_errors_only(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...)
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




bool cw_test_expect_null_pointer(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...)
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


	if (NULL == pointer) {
		self->stats->successes++;

		cw_test_append_status_string(self, msg_buf, message_len, "[ OK ]");
		fprintf(self->stderr, "%s\n", msg_buf);

		as_expected = true;
	} else {
		self->stats->failures++;

		cw_test_append_status_string(self, msg_buf, message_len, "[FAIL]");
		fprintf(self->stderr, "%s\n", msg_buf);
		fprintf(self->stderr, "   ***   expected NULL, got %p   ***\n", pointer);

		as_expected = false;
	}


	return as_expected;
}




bool cw_test_expect_null_pointer_errors_only(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...)
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


	if (NULL == pointer) {
		as_expected = true;
	} else {
		self->stats->failures++;

		cw_test_append_status_string(self, msg_buf, message_len, "[FAIL]");
		fprintf(self->stderr, "%s\n", msg_buf);
		fprintf(self->stderr, "   ***   expected NULL, got %p   ***\n", pointer);

		as_expected = false;
	}


	return as_expected;
}




bool cw_test_expect_valid_pointer(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...)
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


	if (NULL != pointer) {
		self->stats->successes++;

		cw_test_append_status_string(self, msg_buf, message_len, "[ OK ]");
		fprintf(self->stderr, "%s\n", msg_buf);

		as_expected = true;
	} else {
		self->stats->failures++;

		cw_test_append_status_string(self, msg_buf, message_len, "[FAIL]");
		fprintf(self->stderr, "%s\n", msg_buf);
		fprintf(self->stderr, "   ***   expected valid pointer, got NULL   ***\n");

		as_expected = false;
	}


	return as_expected;
}




bool cw_test_expect_valid_pointer_errors_only(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...)
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


	if (NULL != pointer) {
		as_expected = true;
	} else {
		self->stats->failures++;

		cw_test_append_status_string(self, msg_buf, message_len, "[FAIL]");
		fprintf(self->stderr, "%s\n", msg_buf);
		fprintf(self->stderr, "   ***   expected valid pointer, got NULL   ***\n");

		as_expected = false;
	}


	return as_expected;
}




bool cw_test_test_topic_was_requested(cw_test_executor_t * self, int libcw_test_topic)
{
	const int n = sizeof (self->tested_topics) / sizeof (self->tested_topics[0]);

	switch (libcw_test_topic) {
	case LIBCW_TEST_TOPIC_TQ:
	case LIBCW_TEST_TOPIC_GEN:
	case LIBCW_TEST_TOPIC_KEY:
	case LIBCW_TEST_TOPIC_REC:
	case LIBCW_TEST_TOPIC_DATA:
	case LIBCW_TEST_TOPIC_OTHER:
		for (int i = 0; i < n; i++) {
			if (LIBCW_TEST_TOPIC_MAX == self->tested_topics[i]) {
				/* Found guard element. */
				return false;
			}
			if (libcw_test_topic == self->tested_topics[i]) {
				return true;
			}
		}
		return false;

	case LIBCW_TEST_TOPIC_MAX:
	default:
		fprintf(stderr, "Unexpected test topic %d\n", libcw_test_topic);
		exit(EXIT_FAILURE);
	}
}




bool cw_test_sound_system_was_requested(cw_test_executor_t * self, int sound_system)
{
	const int n = sizeof (self->tested_sound_systems) / sizeof (self->tested_sound_systems[0]);

	switch (sound_system) {
	case CW_AUDIO_NULL:
	case CW_AUDIO_CONSOLE:
	case CW_AUDIO_OSS:
	case CW_AUDIO_ALSA:
	case CW_AUDIO_PA:
		for (int i = 0; i < n; i++) {
			if (LIBCW_TEST_SOUND_SYSTEM_MAX == self->tested_sound_systems[i]) {
				/* Found guard element. */
				return false;
			}
			if (sound_system == self->tested_sound_systems[i]) {
				return true;
			}
		}
		return false;

	case CW_AUDIO_NONE:
	case CW_AUDIO_SOUNDCARD:
	default:
		fprintf(stderr, "Unexpected sound system %d\n", sound_system);
		exit(EXIT_FAILURE);
	}
}




void cw_test_print_test_header(cw_test_executor_t * self, const char * text)
{
	fprintf(self->stderr, "\n%sbeginning of test: %s:\n", self->msg_prefix, text);
}




void cw_test_print_test_footer(cw_test_executor_t * self, const char * text)
{
	const int n = fprintf(self->stderr, "%send of test: %s: ", self->msg_prefix, text);
	fprintf(self->stderr, "%*s\n", self->console_n_cols - n, "completed\n");
}



const char * cw_test_get_current_sound_system_label(cw_test_executor_t * self)
{
	return cw_get_audio_system_label(self->current_sound_system);
}




void cw_test_set_current_sound_system(cw_test_executor_t * self, int sound_system)
{
	self->current_sound_system = sound_system;
	switch (sound_system) {


	case CW_AUDIO_NULL:
		self->stats = &self->stats_null;
		break;
	case CW_AUDIO_CONSOLE:
		self->stats = &self->stats_console;
		break;
	case CW_AUDIO_OSS:
		self->stats = &self->stats_oss;
		break;
	case CW_AUDIO_ALSA:
		self->stats = &self->stats_alsa;
		break;
	case CW_AUDIO_PA:
		self->stats = &self->stats_pa;
		break;
	default:
	case CW_AUDIO_NONE:
	case CW_AUDIO_SOUNDCARD:
		fprintf(self->stderr, "Unexpected sound system %d\n", sound_system);
		exit(EXIT_FAILURE);
	}
}




void cw_test_print_test_stats(cw_test_executor_t * self)
{
	fprintf(self->stderr, "\n\n%s: Statistics of tests: (total/failures)\n\n", self->msg_prefix);

        //                     123 12345678901234 12345678901234 12345678901234 12345678901234 12345678901234
	fprintf(self->stderr,       "   | tone queue   | generator    | key          | receiver     | other        |\n");
	fprintf(self->stderr,       " -----------------------------------------------------------------------------|\n");
	#define LINE_FORMAT   " %c |% 10d/% 3d|% 10d/% 3d|% 10d/% 3d|% 10d/% 3d|% 10d/% 3d|\n"


	char audio_systems[] = " NCOAP";

	for (int i = CW_AUDIO_NULL; i <= CW_AUDIO_PA; i++) {
		fprintf(self->stderr, LINE_FORMAT,
			audio_systems[i], /* TODO: global variable. */
			self->stats2[i][LIBCW_TEST_TOPIC_TQ].failures    + self->stats2[i][LIBCW_TEST_TOPIC_TQ].successes,    self->stats2[i][LIBCW_TEST_TOPIC_TQ].failures,
			self->stats2[i][LIBCW_TEST_TOPIC_GEN].failures   + self->stats2[i][LIBCW_TEST_TOPIC_GEN].successes,   self->stats2[i][LIBCW_TEST_TOPIC_GEN].failures,
			self->stats2[i][LIBCW_TEST_TOPIC_KEY].failures   + self->stats2[i][LIBCW_TEST_TOPIC_KEY].successes,   self->stats2[i][LIBCW_TEST_TOPIC_KEY].failures,
			self->stats2[i][LIBCW_TEST_TOPIC_REC].failures   + self->stats2[i][LIBCW_TEST_TOPIC_REC].successes,   self->stats2[i][LIBCW_TEST_TOPIC_REC].failures,
			self->stats2[i][LIBCW_TEST_TOPIC_OTHER].failures + self->stats2[i][LIBCW_TEST_TOPIC_OTHER].successes, self->stats2[i][LIBCW_TEST_TOPIC_OTHER].failures);
	}

	return;
}




void cw_test_init(cw_test_executor_t * self, FILE * stdout, FILE * stderr, const char * msg_prefix)
{
	memset(self, 0, sizeof (cw_test_executor_t));

	self->stdout = stdout;
	self->stderr = stderr;

	self->expect_eq_int = cw_test_expect_eq_int;
	self->expect_eq_int_errors_only = cw_test_expect_eq_int_errors_only;

	self->expect_null_pointer = cw_test_expect_null_pointer;
	self->expect_null_pointer_errors_only = cw_test_expect_null_pointer_errors_only;

	self->expect_valid_pointer = cw_test_expect_valid_pointer;
	self->expect_valid_pointer_errors_only = cw_test_expect_valid_pointer_errors_only;

	self->print_test_header = cw_test_print_test_header;
	self->print_test_footer = cw_test_print_test_footer;

	self->process_args = cw_test_process_args;
	self->print_args_summary = cw_test_print_args_summary;

	self->test_topic_was_requested = cw_test_test_topic_was_requested;
	self->sound_system_was_requested = cw_test_sound_system_was_requested;

	self->get_current_sound_system_label = cw_test_get_current_sound_system_label;
	self->set_current_sound_system = cw_test_set_current_sound_system;
	self->print_test_stats = cw_test_print_test_stats;

	self->log_info = cw_test_log_info;
	self->log_info_cont = cw_test_log_info_cont;
	self->log_err = cw_test_log_err;

	self->print_sound_systems = cw_test_print_sound_systems;
	self->print_topics = cw_test_print_topics;

	self->test_topic_is_member = cw_test_test_topic_is_member;
	self->sound_system_is_member = cw_test_sound_system_is_member;

	self->console_n_cols = default_cw_test_print_n_chars;

	self->current_sound_system = CW_AUDIO_NONE;

	snprintf(self->msg_prefix, sizeof (self->msg_prefix), "%s: ", msg_prefix);

	struct timeval tv;
	gettimeofday(&tv, NULL);
	const suseconds_t seed = tv.tv_usec;
	fprintf(self->stdout, "%sRandom seed = %lu\n", self->msg_prefix, seed);
	fflush(self->stdout);
	srand(seed);
}




/**
   \brief Run a series of tests for specified audio systems

   Function attempts to run a set of testcases for every audio system
   specified in \p sound_systems. These testcases require some kind of
   audio system configured. The function calls
   cw_test_topics_with_current_sound_system() to do the configuration and
   run the tests.

   \p sound_systems is a list of audio systems to be tested: "ncoap".
   Pass NULL pointer to attempt to test all of audio systems supported
   by libcw.

   \param sound_systems - list of audio systems to be tested
*/
int cw_test_topics_with_sound_systems(cw_test_executor_t * self, tester_fn test_topics_with_current_sound_system)
{
	int n = 0, c = 0, o = 0, a = 0, p = 0;

	if (self->sound_system_was_requested(self, CW_AUDIO_NULL)) {
		if (cw_is_null_possible(NULL)) {
			fprintf(self->stderr, "========================================\n");
			self->set_current_sound_system(self, CW_AUDIO_NULL);
			n = (*test_topics_with_current_sound_system)(self);
		} else {
			fprintf(self->stderr, "%snull output not available\n", self->msg_prefix);
		}
	}

	if (self->sound_system_was_requested(self, CW_AUDIO_CONSOLE)) {
		if (cw_is_console_possible(NULL)) {
			fprintf(self->stderr, "========================================\n");
			self->set_current_sound_system(self, CW_AUDIO_CONSOLE);
			c = (*test_topics_with_current_sound_system)(self);
		} else {
			fprintf(self->stderr, "%sconsole output not available\n", self->msg_prefix);
		}
	}

	if (self->sound_system_was_requested(self, CW_AUDIO_OSS)) {
		if (cw_is_oss_possible(NULL)) {
			fprintf(self->stderr, "========================================\n");
			self->set_current_sound_system(self, CW_AUDIO_OSS);
			o = (*test_topics_with_current_sound_system)(self);
		} else {
			fprintf(self->stderr, "%sOSS output not available\n", self->msg_prefix);
		}
	}

	if (self->sound_system_was_requested(self, CW_AUDIO_ALSA)) {
		if (cw_is_alsa_possible(NULL)) {
			fprintf(self->stderr, "========================================\n");
			self->set_current_sound_system(self, CW_AUDIO_ALSA);
			a = (*test_topics_with_current_sound_system)(self);
		} else {
			fprintf(self->stderr, "%sAlsa output not available\n", self->msg_prefix);
		}
	}

	if (self->sound_system_was_requested(self, CW_AUDIO_PA)) {
		if (cw_is_pa_possible(NULL)) {
			fprintf(self->stderr, "========================================\n");
			self->set_current_sound_system(self, CW_AUDIO_PA);
			p = (*test_topics_with_current_sound_system)(self);
		} else {
			fprintf(self->stderr, "%sPulseAudio output not available\n", self->msg_prefix);
		}
	}

	if (!n && !c && !o && !a && !p) {
		return 0;
	} else {
		return -1;
	}
}




void cw_test_log_info(struct cw_test_executor_t * self, const char * fmt, ...)
{
	if (NULL == self->stdout) {
		return;
	}

	char va_buf[256] = { 0 };

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	fprintf(self->stdout, "[II] %s: %s", self->msg_prefix, va_buf);
	fflush(self->stdout);

	return;
}




void cw_test_log_info_cont(struct cw_test_executor_t * self, const char * fmt, ...)
{
	if (NULL == self->stdout) {
		return;
	}

	char va_buf[256] = { 0 };

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	fprintf(self->stdout, "%s", va_buf);
	fflush(self->stdout);

	return;
}




void cw_test_log_err(struct cw_test_executor_t * self, const char * fmt, ...)
{
	if (NULL == self->stdout) {
		return;
	}

	char va_buf[256] = { 0 };

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	fprintf(self->stdout, "[EE] %s: %s", self->msg_prefix, va_buf);
	fflush(self->stdout);

	return;
}





void cw_test_print_sound_systems(cw_test_executor_t * self, int * sound_systems)
{
	for (int i = 0; i < LIBCW_TEST_SOUND_SYSTEM_MAX; i++) {
		if (LIBCW_TEST_SOUND_SYSTEM_MAX == sound_systems[i]) {
			/* Found guard element. */
			return;
		}

		switch (sound_systems[i]) {
		case CW_AUDIO_NULL:
			self->log_info_cont(self, "null ");
			break;
		case CW_AUDIO_CONSOLE:
			self->log_info_cont(self, "console ");
			break;
		case CW_AUDIO_OSS:
			self->log_info_cont(self, "OSS ");
			break;
		case CW_AUDIO_ALSA:
			self->log_info_cont(self, "ALSA ");
			break;
		case CW_AUDIO_PA:
			self->log_info_cont(self, "PulseAudio ");
			break;
		default:
			self->log_info_cont(self, "unknown! ");
			break;
		}
	}

	return;
}




void cw_test_print_topics(cw_test_executor_t * self, int * topics)
{
	for (int i = 0; i < LIBCW_TEST_TOPIC_MAX; i++) {
		if (LIBCW_TEST_TOPIC_MAX == topics[i]) {
			/* Found guard element. */
			return;
		}

		switch (topics[i]) {
		case LIBCW_TEST_TOPIC_TQ:
			self->log_info_cont(self, "tq ");
			break;
		case LIBCW_TEST_TOPIC_GEN:
			self->log_info_cont(self, "gen ");
			break;
		case LIBCW_TEST_TOPIC_KEY:
			self->log_info_cont(self, "key ");
			break;
		case LIBCW_TEST_TOPIC_REC:
			self->log_info_cont(self, "rec ");
			break;
		case LIBCW_TEST_TOPIC_DATA:
			self->log_info_cont(self, "data ");
			break;
		case LIBCW_TEST_TOPIC_OTHER:
			self->log_info_cont(self, "other ");
			break;
		default:
			self->log_info_cont(self, "unknown! ");
			break;
		}
	}
	self->log_info_cont(self, "\n");

	return;
}




void cw_test_print_args_summary(cw_test_executor_t * self)
{
	self->log_info(self, "tested sound systems: ");
	self->print_sound_systems(self, self->tested_sound_systems);
	self->log_info_cont(self, "\n");

	self->log_info(self, "tested topics: ");
	self->print_topics(self, self->tested_topics);
	self->log_info_cont(self, "\n");
}





bool cw_test_test_topic_is_member(cw_test_executor_t * cte, int topic, int * topics)
{
	for (int i = 0; i < LIBCW_TEST_TOPIC_MAX; i++) {
		if (LIBCW_TEST_TOPIC_MAX == topics[i]) {
			/* Found guard element. */
			return false;
		}
		if (topic == topics[i]) {
			return true;
		}
	}
	return false;
}




bool cw_test_sound_system_is_member(cw_test_executor_t * cte, int sound_system, int * sound_systems)
{
	for (int i = 0; i < LIBCW_TEST_SOUND_SYSTEM_MAX; i++) {
		if (LIBCW_TEST_SOUND_SYSTEM_MAX == sound_systems[i]) {
			/* Found guard element. */
			return false;
		}
		if (sound_system == sound_systems[i]) {
			return true;
		}
	}
	return false;
}
