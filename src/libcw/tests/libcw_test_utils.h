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




typedef struct {
	int successes;
	int failures;
} cw_test_stats_t;




struct cw_test_t;
typedef struct cw_test_t {
	char msg_prefix[32];
	cw_test_stats_t * stats;
	FILE * stdout;
	FILE * stderr;

	/* Limit of characters that can be printed to console in one row. */
	int console_n_cols;

	bool (* expect_eq_int)(struct cw_test_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
	bool (* expect_eq_int_errors_only)(struct cw_test_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
	void (* print_test_header)(struct cw_test_t * self, const char * text);
	void (* print_test_footer)(struct cw_test_t * self, const char * text);
} cw_test_t;
void cw_test_init(cw_test_t * self, FILE * stdout, FILE * stderr, const char * msg_prefix);




int cw_test_args(int argc, char *const argv[], char *sound_systems, size_t systems_max, char *modules, size_t modules_max);
void cw_test_print_help(const char *progname);




#endif /* #ifndef _LIBCW_TEST_UTILS_H_ */
