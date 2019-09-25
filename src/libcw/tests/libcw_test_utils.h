/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef _LIBCW_TEST_UTILS_H_
#define _LIBCW_TEST_UTILS_H_




#include <stddef.h> /* size_t */
#include <stdio.h>
#include <stdbool.h>




#define out_file stdout

/* Total width of test name + test status printed in console (without
   ending '\n'). Remember that some consoles have width = 80. Not
   everyone works in X. */
#define default_cw_test_print_n_chars 75

/* Notice that failure status string ("FAIL!") is visually very
   different than "success". This makes finding failed tests
   easier. */
#define CW_TEST_PRINT_TEST_RESULT(m_failure, m_n) {			\
		printf("%*s\n", (default_cw_test_print_n_chars - m_n), m_failure ? "\x1B[7m FAIL! \x1B[0m" : "success"); \
	}

#define CW_TEST_PRINT_FUNCTION_COMPLETED(m_func_name) {			\
		int m = printf("libcw: %s(): ", m_func_name);		\
		printf("%*s\n\n", default_cw_test_print_n_chars - m, "completed");	\
	}




#define LIBCW_TEST_ALL_MODULES          "gtkro"   /* generator, tone queue, key, receiver, other. */
#define LIBCW_TEST_ALL_SOUND_SYSTEMS    "ncoap"   /* null, console, oss, alsa, pulseaudio. */




typedef struct {
	int successes;
	int failures;
} cw_test_stats_t;




struct cw_test_t;
typedef struct cw_test_t {
	char msg_prefix[32];
	FILE * stdout;
	FILE * stderr;

	int current_sound_system;

	/* Limit of characters that can be printed to console in one row. */
	int console_n_cols;

	cw_test_stats_t stats_indep;
	cw_test_stats_t stats_null;
	cw_test_stats_t stats_console;
	cw_test_stats_t stats_oss;
	cw_test_stats_t stats_alsa;
	cw_test_stats_t stats_pa;
	cw_test_stats_t * stats; /* Pointer to current stats. */

	char tested_sound_systems[sizeof (LIBCW_TEST_ALL_SOUND_SYSTEMS)];
	char tested_modules[sizeof (LIBCW_TEST_ALL_MODULES)];

	bool (* expect_eq_int)(struct cw_test_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
	bool (* expect_eq_int_errors_only)(struct cw_test_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
	void (* print_test_header)(struct cw_test_t * self, const char * text);
	void (* print_test_footer)(struct cw_test_t * self, const char * text);
	int (* process_args)(struct cw_test_t * self, int argc, char * const argv[]);

	const char * (* get_current_sound_system_label)(struct cw_test_t * self);
	void (* set_current_sound_system)(struct cw_test_t * self, int sound_system);

	bool (* should_test_module)(struct cw_test_t * self, const char * module);
	bool (* should_test_sound_system)(struct cw_test_t * self, const char * sound_system);
} cw_test_t;



void cw_test_init(cw_test_t * self, FILE * stdout, FILE * stderr, const char * msg_prefix);

void cw_test_print_help(const char *progname);




#endif /* #ifndef _LIBCW_TEST_UTILS_H_ */
