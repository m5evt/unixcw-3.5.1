/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef H_LIBCW_TEST
#define H_LIBCW_TEST




#include <stddef.h> /* size_t */

#define out_file stdout

typedef struct {
	int successes;
	int failures;
} cw_test_stats_t;




/* Total width of test name + test status printed in console. Remember
   that some consoles have width = 80. Not everyone works in X. */
static const int cw_test_print_width = 75;


/* Notice that failure status string ("FAIL!") is visually very
   different than "success". This makes finding failed tests
   easier. */
#define CW_TEST_PRINT_TEST_RESULT(m_failure, m_n) {			\
		printf("%*s\n", (cw_test_print_width - m_n), m_failure ? "\x1B[7m FAIL! \x1B[0m" : "success"); \
	}

#define CW_TEST_PRINT_FUNCTION_COMPLETED(m_func_name) {			\
		int m = printf("libcw: %s(): ", m_func_name);		\
		printf("%*s\n\n", cw_test_print_width - m, "completed");	\
	}




#endif /* #ifndef H_LIBCW_TEST */
