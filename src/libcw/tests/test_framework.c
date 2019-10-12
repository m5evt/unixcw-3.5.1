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
   \file test_framework.c

   \brief Test framework for libcw test code
*/




#include "config.h"


#define _XOPEN_SOURCE 600 /* signaction() + SA_RESTART */


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

#include "test_framework.h"




static bool cw_test_expect_eq_int(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
static bool cw_test_expect_eq_int_errors_only(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));

static bool cw_test_expect_op_int(struct cw_test_executor_t * self, int expected_value, const char * operator, int received_value, bool errors_only, const char * fmt, ...) __attribute__ ((format (printf, 6, 7)));
static bool cw_test_expect_op_int2(struct cw_test_executor_t * self, int expected_value, const char * operator, int received_value, bool errors_only, const char * va_buf);

static bool cw_test_expect_between_int(struct cw_test_executor_t * self, int expected_lower, int received_value, int expected_higher, const char * fmt, ...) __attribute__ ((format (printf, 5, 6)));
static bool cw_test_expect_between_int_errors_only(struct cw_test_executor_t * self, int expected_lower, int received_value, int expected_higher, const char * fmt, ...) __attribute__ ((format (printf, 5, 6)));

static bool cw_test_expect_null_pointer(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));
static bool cw_test_expect_null_pointer_errors_only(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));

static bool cw_test_expect_valid_pointer(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));
static bool cw_test_expect_valid_pointer_errors_only(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));

static void cw_assert2(struct cw_test_executor_t * self, bool condition, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));


static void cw_test_print_test_header(cw_test_executor_t * self, const char * fmt, ...);
static void cw_test_print_test_footer(cw_test_executor_t * self, const char * function_name);
static void cw_test_append_status_string(cw_test_executor_t * self, char * msg_buf, int n, const char * status_string);

static int cw_test_process_args(cw_test_executor_t * self, int argc, char * const argv[]);
static int cw_test_fill_default_sound_systems_and_topics(cw_test_executor_t * self);

static void cw_test_print_test_options(cw_test_executor_t * self);

static bool cw_test_test_topic_was_requested(cw_test_executor_t * self, int libcw_test_topic);
static bool cw_test_sound_system_was_requested(cw_test_executor_t * self, int sound_system);

static const char * cw_test_get_current_topic_label(cw_test_executor_t * self);
static const char * cw_test_get_current_sound_system_label(cw_test_executor_t * self);

static void cw_test_set_current_topic_and_sound_system(cw_test_executor_t * self, int topic, int sound_system);

static void cw_test_print_test_stats(cw_test_executor_t * self);

static int cw_test_log_info(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void cw_test_log_info_cont(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void cw_test_flush_info(struct cw_test_executor_t * self);
static void cw_test_log_error(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));

static void cw_test_print_sound_systems(cw_test_executor_t * self, int * sound_systems, int max);
static void cw_test_print_topics(cw_test_executor_t * self, int * topics, int max);

static bool cw_test_test_topic_is_member(cw_test_executor_t * cte, int topic, int * topics, int max);
static bool cw_test_sound_system_is_member(cw_test_executor_t * cte, int sound_system, int * sound_systems, int max);

static int cw_test_main_test_loop(cw_test_executor_t * cte, cw_test_set_t * test_sets);

static void cw_test_print_help(cw_test_executor_t * self, const char * program_name);




/**
   \brief Set default contents of
   cw_test_executor_t::tested_sound_systems[] and
   cw_test_executor_t::tested_topics[]

   One or both sets of defaults will be used if related argument was
   not used in command line.

   When during preparation of default set of sound system we detect
   that some sound set is not available, we will not include it in set
   of default sound systems.

   This is a private function so it is not put into cw_test_executor_t
   class.
*/
int cw_test_fill_default_sound_systems_and_topics(cw_test_executor_t * self)
{
	/* NULL means "use default device" for every sound system */
	const char * default_device = NULL;

	int dest_idx = 0;
	if (cw_is_null_possible(default_device)) {
		self->tested_sound_systems[dest_idx] = CW_AUDIO_NULL;
		dest_idx++;
	} else {
		self->log_info(self, "Null sound system is not available on this machine - will skip it\n");
	}

	if (cw_is_console_possible(default_device)) {
		self->tested_sound_systems[dest_idx] = CW_AUDIO_CONSOLE;
		dest_idx++;
	} else {
		self->log_info(self, "Console sound system is not available on this machine - will skip it\n");
	}

	if (cw_is_oss_possible(default_device)) {
		self->tested_sound_systems[dest_idx] = CW_AUDIO_OSS;
		dest_idx++;
	} else {
		self->log_info(self, "OSS sound system is not available on this machine - will skip it\n");
	}

	if (cw_is_alsa_possible(default_device)) {
		self->tested_sound_systems[dest_idx] = CW_AUDIO_ALSA;
		dest_idx++;
	} else {
		self->log_info(self, "ALSA sound system is not available on this machine - will skip it\n");
	}

	if (cw_is_pa_possible(default_device)) {
		self->tested_sound_systems[dest_idx] = CW_AUDIO_PA;
		dest_idx++;
	} else {
		self->log_info(self, "PulseAudio sound system is not available on this machine - will skip it\n");
	}
	self->tested_sound_systems[dest_idx] = LIBCW_TEST_SOUND_SYSTEM_MAX; /* Guard element. */



	self->tested_topics[0] = LIBCW_TEST_TOPIC_TQ;
	self->tested_topics[1] = LIBCW_TEST_TOPIC_GEN;
	self->tested_topics[2] = LIBCW_TEST_TOPIC_KEY;
	self->tested_topics[3] = LIBCW_TEST_TOPIC_REC;
	self->tested_topics[4] = LIBCW_TEST_TOPIC_DATA;
	self->tested_topics[5] = LIBCW_TEST_TOPIC_OTHER;
	self->tested_topics[6] = LIBCW_TEST_TOPIC_MAX; /* Guard element. */

	return 0;
}




int cw_test_process_args(cw_test_executor_t * self, int argc, char * const argv[])
{
	cw_test_fill_default_sound_systems_and_topics(self);
	if (argc == 1) {
		/* Use defaults configured by
		   cw_test_fill_default_sound_systems_and_topics(). */
		return 0;
	}

	size_t optarg_len = 0;
	int dest_idx = 0;

	int opt;
	while ((opt = getopt(argc, argv, "t:s:n:h")) != -1) {
		switch (opt) {
		case 's':
			optarg_len = strlen(optarg);
			if (optarg_len > strlen(LIBCW_TEST_ALL_SOUND_SYSTEMS)) {
				fprintf(stderr, "Too many values for 'sound system' option: '%s'\n", optarg);
				goto help_and_error;
			}

			dest_idx = 0;
			for (size_t i = 0; i < optarg_len; i++) {
				const int val = optarg[i];
				if (NULL == strchr(LIBCW_TEST_ALL_SOUND_SYSTEMS, val)) {
					fprintf(stderr, "Unsupported sound system '%c'\n", val);
					goto help_and_error;
				}

				/* If user has explicitly requested a sound system,
				   then we have to fail if the system is not available.
				   Otherwise we may mislead the user. */
				switch (val) {
				case 'n':
					if (cw_is_null_possible(NULL)) {
						self->tested_sound_systems[dest_idx] = CW_AUDIO_NULL;
						dest_idx++;
					} else {
						fprintf(stderr, "Requested null sound system is not available on this machine\n");
						goto help_and_error;
					}
					break;
				case 'c':
					if (cw_is_console_possible(NULL)) {
						self->tested_sound_systems[dest_idx] = CW_AUDIO_CONSOLE;
						dest_idx++;
					} else {
						fprintf(stderr, "Requested console sound system is not available on this machine\n");
						goto help_and_error;

					}
					break;
				case 'o':
					if (cw_is_oss_possible(NULL)) {
						self->tested_sound_systems[dest_idx] = CW_AUDIO_OSS;
						dest_idx++;
					} else {
						fprintf(stderr, "Requested OSS sound system is not available on this machine\n");
						goto help_and_error;

					}
					break;
				case 'a':
					if (cw_is_alsa_possible(NULL)) {
						self->tested_sound_systems[dest_idx] = CW_AUDIO_ALSA;
						dest_idx++;
					} else {
						fprintf(stderr, "Requested ALSA sound system is not available on this machine\n");
						goto help_and_error;

					}
					break;
				case 'p':
					if (cw_is_pa_possible(NULL)) {
						self->tested_sound_systems[dest_idx] = CW_AUDIO_PA;
						dest_idx++;
					} else {
						fprintf(stderr, "Requested PulseAudio sound system is not available on this machine\n");
						goto help_and_error;

					}
					break;
				default:
					fprintf(stderr, "Unsupported sound system '%c'\n", val);
					goto help_and_error;
				}
			}
			self->tested_sound_systems[dest_idx] = LIBCW_TEST_SOUND_SYSTEM_MAX; /* Guard element. */
			break;

		case 't':
			optarg_len = strlen(optarg);
			if (optarg_len > strlen(LIBCW_TEST_ALL_TOPICS)) {
				fprintf(stderr, "Too many values for 'topics' option: '%s'\n", optarg);
				return -1;
			}

			dest_idx = 0;
			for (size_t i = 0; i < optarg_len; i++) {
				const int val = optarg[i];
				if (NULL == strchr(LIBCW_TEST_ALL_TOPICS, val)) {
					fprintf(stderr, "Unsupported topic '%c'\n", val);
					goto help_and_error;
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
					goto help_and_error;
				}
				dest_idx++;
			}
			self->tested_topics[dest_idx] = LIBCW_TEST_TOPIC_MAX; /* Guard element. */
			break;

		case 'n':
			strncpy(self->single_test_function_name, optarg, sizeof (self->single_test_function_name));
			self->single_test_function_name[sizeof (self->single_test_function_name) - 1] = '\0';
			break;

		case 'h':
			cw_test_print_help(self, argv[0]);
			exit(EXIT_SUCCESS);
		default:
			goto help_and_error;
		}
	}

	return 0;

 help_and_error:
	cw_test_print_help(self, argv[0]);
	exit(EXIT_FAILURE);
}




/**
   @brief Print help text with summary of available command line
   options and their possible values

   Pass argv[0] as the second argument of the function.
*/
void cw_test_print_help(__attribute__((unused)) cw_test_executor_t * self, const char * program_name)
{
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage: %s [-s <sound systems>] [-t <topics>] [-n <test function name>]\n\n", program_name);
	fprintf(stderr, "    <sound system> is one or more of those:\n");
	fprintf(stderr, "    n - Null\n");
	fprintf(stderr, "    c - console\n");
	fprintf(stderr, "    o - OSS\n");
	fprintf(stderr, "    a - ALSA\n");
	fprintf(stderr, "    p - PulseAudio\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    <topics> is one or more of those:\n"); /* TODO: add missing test topics. */
	fprintf(stderr, "    g - generator\n");
	fprintf(stderr, "    t - tone queue\n");
	fprintf(stderr, "    k - Morse key\n");
	fprintf(stderr, "    r - receiver\n");
	fprintf(stderr, "    o - other\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -n argument is used to specify one (and only one) test function to be executed.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    If no argument is provided, the program will attempt to test all sound systems available on the machine and all topics\n");

	return;
}




bool cw_test_expect_op_int(struct cw_test_executor_t * self, int expected_value, const char * operator, int received_value, bool errors_only, const char * fmt, ...)
{
	char va_buf[128] = { 0 };
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	return cw_test_expect_op_int2(self, expected_value, operator, received_value, errors_only, va_buf);
}




bool cw_test_expect_op_int2(struct cw_test_executor_t * self, int expected_value, const char * operator, int received_value, bool errors_only, const char * va_buf)
{
	bool as_expected = false;

	char msg_buf[1024] = { 0 };
	int n = snprintf(msg_buf, sizeof (msg_buf), "%s", self->msg_prefix);
	const int message_len = n + snprintf(msg_buf + n, sizeof (msg_buf) - n, "%s", va_buf);
	n += snprintf(msg_buf + n, sizeof (msg_buf) - n, "%-*s", (int) (self->console_n_cols - n), va_buf);


	bool success = false;
	if (operator[0] == '=' && operator[1] == '=') {
		success = expected_value == received_value;

	} else if (operator[0] == '<' && operator[1] == '=') {
		success = expected_value <= received_value;

	} else if (operator[0] == '>' && operator[1] == '=') {
		success = expected_value >= received_value;

	} else if (operator[0] == '!' && operator[1] == '=') {
		success = expected_value != received_value;

	} else if (operator[0] == '<' && operator[1] == '\0') {
		success = expected_value < received_value;

	} else if (operator[0] == '>' && operator[1] == '\0') {
		success = expected_value > received_value;

	} else {
		self->log_error(self, "Unhandled operator '%s'\n", operator);
		assert(0);
	}


	if (success) {
		if (!errors_only) {
			self->stats->successes++;

			cw_test_append_status_string(self, msg_buf, message_len, "[ OK ]");
			self->log_info(self, "%s\n", msg_buf);
		}
		as_expected = true;
	} else {
		self->stats->failures++;

		cw_test_append_status_string(self, msg_buf, message_len, "[FAIL]");
		self->log_error(self, "%s\n", msg_buf);
		self->log_error(self, "   ***   expected %d, got %d   ***\n", expected_value, received_value);

		as_expected = false;
	}

	return as_expected;
}



bool cw_test_expect_eq_int(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...)
{
	char va_buf[128] = { 0 };
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	return cw_test_expect_op_int2(self, expected_value, "==", received_value, false, va_buf);
}




bool cw_test_expect_eq_int_errors_only(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...)
{
	char va_buf[128] = { 0 };
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	return cw_test_expect_op_int2(self, expected_value, "==", received_value, true, va_buf);
}




/**
   @brief Append given status string at the end of buffer, but within cw_test::console_n_cols limit

   This is a private function so it is not put into cw_test_executor_t
   class.
*/
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




bool cw_test_expect_between_int(struct cw_test_executor_t * self, int expected_lower, int received_value, int expected_higher, const char * fmt, ...)
{
	bool as_expected = true;
	char va_buf[128] = { 0 };

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	char msg_buf[1024] = { 0 };
	int n = snprintf(msg_buf, sizeof (msg_buf), "%s", self->msg_prefix);
	const int message_len = n + snprintf(msg_buf + n, sizeof (msg_buf) - n, "%s", va_buf);
	n += snprintf(msg_buf + n, sizeof (msg_buf) - n, "%-*s", (int) (self->console_n_cols - n), va_buf);

	if (expected_lower <= received_value && received_value <= expected_higher) {
		self->stats->successes++;

		cw_test_append_status_string(self, msg_buf, message_len, "[ OK ]");
		//self->log_info(self, "%s\n", msg_buf);
		self->log_info(self, "%s %d %d %d\n", msg_buf, expected_lower, received_value, expected_higher);

		as_expected = true;
	} else {
		self->stats->failures++;

		cw_test_append_status_string(self, msg_buf, message_len, "[FAIL]");
		self->log_error(self, "%s\n", msg_buf);
		self->log_error(self, "   ***   expected within %d-%d, got %d   ***\n", expected_lower, expected_higher, received_value);

		as_expected = false;
	}

	return as_expected;
}




bool cw_test_expect_between_int_errors_only(struct cw_test_executor_t * self, int expected_lower, int received_value, int expected_higher, const char * fmt, ...)
{
	bool as_expected = true;
	char buf[128] = { 0 };

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	if (expected_lower <= received_value && received_value <= expected_higher) {
		as_expected = true;
	} else {
		const int n = fprintf(self->stderr, "%s%s", self->msg_prefix, buf);
		self->stats->failures++;
		self->log_error(self, "%*s", self->console_n_cols - n, "failure: ");
		self->log_error(self, "expected value within %d-%d, got %d\n", expected_lower, expected_higher, received_value);
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
		self->log_info(self, "%s\n", msg_buf);

		as_expected = true;
	} else {
		self->stats->failures++;

		cw_test_append_status_string(self, msg_buf, message_len, "[FAIL]");
		self->log_error(self, "%s\n", msg_buf);
		self->log_error(self, "   ***   expected NULL, got %p   ***\n", pointer);

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
		self->log_error(self, "%s\n", msg_buf);
		self->log_error(self, "   ***   expected NULL, got %p   ***\n", pointer);

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
		self->log_info(self, "%s\n", msg_buf);

		as_expected = true;
	} else {
		self->stats->failures++;

		cw_test_append_status_string(self, msg_buf, message_len, "[FAIL]");
		self->log_error(self, "%s\n", msg_buf);
		self->log_error(self, "   ***   expected valid pointer, got NULL   ***\n");

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
		self->log_error(self, "%s\n", msg_buf);
		self->log_error(self, "   ***   expected valid pointer, got NULL   ***\n");

		as_expected = false;
	}


	return as_expected;
}




void cw_assert2(struct cw_test_executor_t * self, bool condition, const char * fmt, ...)
{
	if (!condition) {

		char va_buf[128] = { 0 };

		va_list ap;
		va_start(ap, fmt);
		vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
		va_end(ap);

		self->log_error(self, "Assertion failed: %s\n", va_buf);

		exit(EXIT_FAILURE);
	}

	return;
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




void cw_test_print_test_header(cw_test_executor_t * self, const char * fmt, ...)
{
	self->log_info_cont(self, "\n");

	self->log_info(self, "Beginning of test\n");

	{
		self->log_info(self, " ");
		for (size_t i = 0; i < self->console_n_cols - (strlen ("[II]  ")); i++) {
			self->log_info_cont(self, "-");
		}
		self->log_info_cont(self, "\n");
	}


	char va_buf[256] = { 0 };
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	self->log_info(self, "Test name: %s\n", va_buf);
	self->log_info(self, "Current test topic: %s\n", self->get_current_topic_label(self));
	self->log_info(self, "Current sound system: %s\n", self->get_current_sound_system_label(self));

	{
		self->log_info(self, " ");
		for (size_t i = 0; i < self->console_n_cols - (strlen ("[II]  ")); i++) {
			self->log_info_cont(self, "-");
		}
		self->log_info_cont(self, "\n");
	}
}




void cw_test_print_test_footer(cw_test_executor_t * self, const char * text)
{
	self->log_info(self, "End of test: %s\n", text);
}




const char * cw_test_get_current_sound_system_label(cw_test_executor_t * self)
{
	return cw_get_audio_system_label(self->current_sound_system);
}




const char * cw_test_get_current_topic_label(cw_test_executor_t * self)
{
	switch (self->current_topic) {
	case LIBCW_TEST_TOPIC_TQ:
		return "tq";
	case LIBCW_TEST_TOPIC_GEN:
		return "gen";
	case LIBCW_TEST_TOPIC_KEY:
		return "key";
	case LIBCW_TEST_TOPIC_REC:
		return "rec";
	case LIBCW_TEST_TOPIC_DATA:
		return "data";
	case LIBCW_TEST_TOPIC_OTHER:
		return "other";
	default:
		return "*** unknown ***";
	}
}




/**
   @brief Set a test topic and sound system that is about to be tested

   This is a private function so it is not put into cw_test_executor_t
   class.

   Call this function before calling each test function. Topic and
   sound system values to be passed to this function should be taken
   from the same test set that the test function is taken.
*/
void cw_test_set_current_topic_and_sound_system(cw_test_executor_t * self, int topic, int sound_system)
{
	self->current_topic = topic;
	self->current_sound_system = sound_system;
	self->stats = &self->all_stats[sound_system][topic];
}




void cw_test_print_test_stats(cw_test_executor_t * self)
{
	const char sound_systems[] = " NCOAP";

	fprintf(self->stderr, "\n\nlibcw tests: Statistics of tests (failures/total)\n\n");

	//                           12345 123456789012 123456789012 123456789012 123456789012 123456789012 123456789012
	#define SEPARATOR_LINE      "   --+------------+------------+------------+------------+------------+------------+\n"
	#define FRONT_FORMAT        "%s %c |"
	#define BACK_FORMAT         "%s\n"
	#define CELL_FORMAT_D       "% 11d |"
	#define CELL_FORMAT_S       "%11s |"

	fprintf(self->stderr,       "     | tone queue | generator  |    key     |  receiver  |    data    |    other   |\n");
	fprintf(self->stderr,       "%s", SEPARATOR_LINE);

	for (int sound = CW_AUDIO_NULL; sound <= CW_AUDIO_PA; sound++) {

		/* If a row with error counter has non-zero values,
		   use arrows at the beginning and end of the row to
		   highlight/indicate row that has non-zero error
		   counters. We want the errors to be visible and
		   stand out. */
		char error_indicator_empty[3] = "  ";
		char error_indicator_front[3] = "  ";
		char error_indicator_back[3] = "  ";
		{
			bool has_errors = false;
			for (int topic = 0; topic < LIBCW_TEST_TOPIC_MAX; topic++) {
				if (self->all_stats[sound][topic].failures) {
					has_errors = true;
					break;
				}
			}

			if (has_errors) {
				snprintf(error_indicator_front, sizeof (error_indicator_front), "%s", "->");
				snprintf(error_indicator_back, sizeof (error_indicator_back), "%s", "<-");
			}
		}



		/* Print line with errors. Print numeric values only
		   if some tests for given combination of sound
		   system/topic were performed. */
		fprintf(self->stderr, FRONT_FORMAT, error_indicator_front, sound_systems[sound]);
		for (int topic = 0; topic < LIBCW_TEST_TOPIC_MAX; topic++) {
			int total = self->all_stats[sound][topic].failures + self->all_stats[sound][topic].successes;
			int failures = self->all_stats[sound][topic].failures;

			if (0 == total && 0 == failures) {
				fprintf(self->stderr, CELL_FORMAT_S, " ");
			} else {
				fprintf(self->stderr, CELL_FORMAT_D, failures);
			}
		}
		fprintf(self->stderr, BACK_FORMAT, error_indicator_back);



		/* Print line with totals. Print numeric values only
		   if some tests for given combination of sound
		   system/topic were performed. */
		fprintf(self->stderr, FRONT_FORMAT, error_indicator_empty, sound_systems[sound]);
		for (int topic = 0; topic < LIBCW_TEST_TOPIC_MAX; topic++) {
			int total = self->all_stats[sound][topic].failures + self->all_stats[sound][topic].successes;
			int failures = self->all_stats[sound][topic].failures;

			if (0 == total && 0 == failures) {
				fprintf(self->stderr, CELL_FORMAT_S, " ");
			} else {
				fprintf(self->stderr, CELL_FORMAT_D, total);
			}
		}
		fprintf(self->stderr, BACK_FORMAT, error_indicator_empty);



		fprintf(self->stderr,       "%s", SEPARATOR_LINE);
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

	self->expect_op_int = cw_test_expect_op_int;

	self->expect_between_int = cw_test_expect_between_int;
	self->expect_between_int_errors_only = cw_test_expect_between_int_errors_only;

	self->expect_null_pointer = cw_test_expect_null_pointer;
	self->expect_null_pointer_errors_only = cw_test_expect_null_pointer_errors_only;

	self->expect_valid_pointer = cw_test_expect_valid_pointer;
	self->expect_valid_pointer_errors_only = cw_test_expect_valid_pointer_errors_only;

	self->assert2 = cw_assert2;

	self->print_test_header = cw_test_print_test_header;
	self->print_test_footer = cw_test_print_test_footer;

	self->process_args = cw_test_process_args;

	self->print_test_options = cw_test_print_test_options;

	self->test_topic_was_requested = cw_test_test_topic_was_requested;
	self->sound_system_was_requested = cw_test_sound_system_was_requested;

	self->get_current_topic_label = cw_test_get_current_topic_label;
	self->get_current_sound_system_label = cw_test_get_current_sound_system_label;

	self->print_test_stats = cw_test_print_test_stats;

	self->log_info = cw_test_log_info;
	self->log_info_cont = cw_test_log_info_cont;
	self->flush_info = cw_test_flush_info;
	self->log_error = cw_test_log_error;

	self->main_test_loop = cw_test_main_test_loop;




	self->console_n_cols = default_cw_test_print_n_chars;

	self->current_sound_system = CW_AUDIO_NONE;

	snprintf(self->msg_prefix, sizeof (self->msg_prefix), "%s: ", msg_prefix);

	struct timeval tv;
	gettimeofday(&tv, NULL);
	self->random_seed = tv.tv_sec;
	srand(self->random_seed);
}




int cw_test_log_info(struct cw_test_executor_t * self, const char * fmt, ...)
{
	if (NULL == self->stdout) {
		return 0;
	}

	char va_buf[256] = { 0 };

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	const int n = fprintf(self->stdout, "[II] %s", va_buf);
	fflush(self->stdout);

	return n;
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




void cw_test_flush_info(struct cw_test_executor_t * self)
{
	if (NULL == self->stdout) {
		return;
	}
	fflush(self->stdout);
	return;
}




void cw_test_log_error(struct cw_test_executor_t * self, const char * fmt, ...)
{
	if (NULL == self->stdout) {
		return;
	}

	char va_buf[256] = { 0 };

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(va_buf, sizeof (va_buf), fmt, ap);
	va_end(ap);

	fprintf(self->stdout, "[EE] %s", va_buf);
	fflush(self->stdout);

	return;
}




/**
   @brief Print labels of sound systems specified by @param sound_systems array

   There are no more than @param max items in @param sound_systems
   vector. LIBCW_TEST_SOUND_SYSTEM_MAX is considered a guard
   element. Function stops either after processing @param max
   elements, or at guard element (without printing label for the guard
   element) - whichever comes first.
*/
void cw_test_print_sound_systems(cw_test_executor_t * self, int * sound_systems, int max)
{
	for (int i = 0; i < max; i++) {
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




/**
   @brief Print labels of test topics specified by @param topics array

   There are no more than @param max items in @param topics
   vector. LIBCW_TEST_TOPIC_MAX is considered a guard
   element. Function stops either after processing @param max
   elements, or at guard element (without printing label for the guard
   element) - whichever comes first.
*/
void cw_test_print_topics(cw_test_executor_t * self, int * topics, int max)
{
	for (int i = 0; i < max; i++) {
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




void cw_test_print_test_options(cw_test_executor_t * self)
{
	self->log_info(self, "Sound systems that will be tested: ");
	cw_test_print_sound_systems(self, self->tested_sound_systems, sizeof (self->tested_sound_systems) / sizeof (self->tested_sound_systems[0]));
	self->log_info_cont(self, "\n");

	self->log_info(self, "Topics that will be tested: ");
	cw_test_print_topics(self, self->tested_topics, sizeof (self->tested_topics) / sizeof (self->tested_topics[0]));
	self->log_info_cont(self, "\n");

	self->log_info(self, "Random seed = %lu\n", self->random_seed);

	if (strlen(self->single_test_function_name)) {
		self->log_info(self, "Single function to be tested: '%s'\n", self->single_test_function_name);
	}

	fflush(self->stdout);
}




/**
   @brief See if given @param topic is a member of given list of test topics @param topics

   The size of @param topics is specified by @param max.
*/
bool cw_test_test_topic_is_member(__attribute__((unused)) cw_test_executor_t * cte, int topic, int * topics, int max)
{
	for (int i = 0; i < max; i++) {
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




/**
   @brief See if given @param sound_system is a member of given list of test topics @param sound_system

   The size of @param sound_system is specified by @param max.
*/
bool cw_test_sound_system_is_member(__attribute__((unused)) cw_test_executor_t * cte, int sound_system, int * sound_systems, int max)
{
	for (int i = 0; i < max; i++) {
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




int cw_test_main_test_loop(cw_test_executor_t * cte, cw_test_set_t * test_sets)
{
	int set = 0;
	while (CW_TEST_SET_VALID == test_sets[set].set_valid) {

		cw_test_set_t * test_set = &test_sets[set];

		for (int topic = LIBCW_TEST_TOPIC_TQ; topic < LIBCW_TEST_TOPIC_MAX; topic++) {
			if (!cte->test_topic_was_requested(cte, topic)) {
				continue;
			}
			const int topics_max = sizeof (test_set->topics) / sizeof (test_set->topics[0]);
			if (!cw_test_test_topic_is_member(cte, topic, test_set->topics, topics_max)) {
				continue;
			}


			for (int sound_system = CW_AUDIO_NULL; sound_system < LIBCW_TEST_SOUND_SYSTEM_MAX; sound_system++) {
				if (!cte->sound_system_was_requested(cte, sound_system)) {
					continue;
				}
				const int systems_max = sizeof (test_set->sound_systems) / sizeof (test_set->sound_systems[0]);
				if (!cw_test_sound_system_is_member(cte, sound_system, test_set->sound_systems, systems_max)) {
					continue;
				}


				int f = 0;
				while (test_set->test_functions[f].fn) {
					bool execute = true;
					if (0 != strlen(cte->single_test_function_name)) {
						if (0 != strcmp(cte->single_test_function_name, test_set->test_functions[f].name)) {
							execute = false;
						}
					}

					if (execute) {
						cw_test_set_current_topic_and_sound_system(cte, topic, sound_system);
						//fprintf(stderr, "+++ %s +++\n", test_set->test_functions[f].name);
						(*test_set->test_functions[f].fn)(cte);
					}

					f++;
				}
			}
		}
		set++;
	}

	return 0;
}
