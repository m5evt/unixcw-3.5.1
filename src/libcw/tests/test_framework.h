/*
  This file is a part of unixcw project.  unixcw project is covered by
  GNU General Public License, version 2 or later.
*/

#ifndef _LIBCW_TEST_FRAMEWORK_H_
#define _LIBCW_TEST_FRAMEWORK_H_




#include <stdio.h>
#include <stdbool.h>




#include <sys/time.h>




#include <libcw.h>




#define out_file stdout

/* Total width of test name + test status printed in console (without
   ending '\n'). Remember that some consoles have width = 80. Not
   everyone works in X. */
#define default_cw_test_print_n_chars 75

#define LIBCW_TEST_ALL_TOPICS           "tgkrdo"   /* generator, tone queue, key, receiver, data, other. */
#define LIBCW_TEST_ALL_SOUND_SYSTEMS    "ncoap"   /* Null, console, OSS, ALSA, PulseAudio. */




enum {
	/* Explicitly stated values in this enum shall never
	   change. */
	LIBCW_TEST_TOPIC_TQ      = 0,
	LIBCW_TEST_TOPIC_GEN     = 1,
	LIBCW_TEST_TOPIC_KEY     = 2,
	LIBCW_TEST_TOPIC_REC     = 3,
	LIBCW_TEST_TOPIC_DATA    = 4,

	LIBCW_TEST_TOPIC_OTHER,

	LIBCW_TEST_TOPIC_MAX
};




/*
  NONE = 0, NULL = 1, CONSOLE = 2, OSS = 3, ALSA = 4, PA = 5;
  everything else after PA we right now don't test, so MAX = 6.
*/
#define LIBCW_TEST_SOUND_SYSTEM_MAX 6




typedef struct {
	int successes;
	int failures;
} cw_test_stats_t;



struct cw_test_set_t;



struct cw_test_executor_t;
typedef struct cw_test_executor_t {
	char msg_prefix[32];
	FILE * stdout;
	FILE * stderr;

	suseconds_t random_seed;

	/* Sound system and test topic currently tested.
	   Should be set right before calling a specific test function. */
	int current_sound_system;
	int current_topic;

	/* We can ask to execute one specific function by function's
	   name (command line argument "-n"). */
	char single_test_function_name[128];

	/* Limit of characters that can be printed to console in one row. */
	int console_n_cols;

	cw_test_stats_t all_stats[LIBCW_TEST_SOUND_SYSTEM_MAX][LIBCW_TEST_TOPIC_MAX];
	cw_test_stats_t * stats; /* Pointer to current stats (one of members of ::all_stats[][]). */

	int tested_sound_systems[LIBCW_TEST_SOUND_SYSTEM_MAX + 1];
	int tested_topics[LIBCW_TEST_TOPIC_MAX + 1];




	/**
	   Verify that @param received_value is equal to @param expected_value

	   Use the function to verify that a successful behaviour has
	   occurred and some result of calculation (@param
	   received_value) is equal to what was expected (@param
	   expected_value).

	   Print log specified by @param fmt and following args
	   regardless of results of the verification or (in case of
	   _errors_only() variant) only if the comparison has shown
	   that the values are NOT equal, which is regarded as failure
	   of expectation, i.e. an error.

	   @return true if this comparison shows that the values are equal
	   @return false otherwise
	*/
	bool (* expect_eq_int)(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
	bool (* expect_eq_int_errors_only)(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));

	/**
	   Verify that operator @param operator is satisfied for
	   @param received_value and @param expected_value

	   Use the function to verify that a successful behaviour has
	   occurred and @param operator applied to value of
	   calculation (@param received_value) and expected value
	   (@param expected_value) returns true.

	   Print log specified by @param fmt and following args
	   regardless of results of the verification or (if @param
	   errors_only is true) only if the application of operator
	   has shown that the values DON'T satisfy the operator, which
	   is regarded as failure of expectation, i.e. an error.

	   @return true if this comparison shows that the values satisfy the operator
	   @return false otherwise
	*/
	bool (* expect_op_int)(struct cw_test_executor_t * self, int expected_value, const char * operator, int received_value, bool errors_only, const char * fmt, ...) __attribute__ ((format (printf, 6, 7)));

	/**
	   Verify that @param received_value is between @param
	   expected_lower and @param expect_higher (inclusive)

	   Use the function to verify that a successful behaviour has
	   occurred and some result of calculation (@param
	   received_value) is larger than or equal to @param
	   expected_lower, and at the same time is smaller than or
	   equal to @param expected_higher

	   Print log specified by @param fmt and following args
	   regardless of results of the verification or (in case of
	   _errors_only() variant) only if the comparison has shown
	   that the tested value is NOT within specified range, which
	   is regarded as failure of expectation, i.e. an error.

	   @return true if this comparison shows that the given value is within specified range
	   @return false otherwise
	*/
	bool (* expect_between_int)(struct cw_test_executor_t * self, int expected_lower, int received_value, int expected_higher, const char * fmt, ...) __attribute__ ((format (printf, 5, 6)));
	bool (* expect_between_int_errors_only)(struct cw_test_executor_t * self, int expected_lower, int received_value, int expected_higher, const char * fmt, ...) __attribute__ ((format (printf, 5, 6)));

	/**
	   Verify that @param pointer is NULL pointer

	   Print log specified by @param fmt and following args
	   regardless of results of the verification or (in case of
	   _errors_only() variant) only if the check has shown that
	   pointer is *NOT* NULL pointer, which is regarded as failure
	   of expectation, i.e. an error.

	   @return true if this comparison shows that the pointer is truly a NULL pointer
	   @return false otherwise
	*/
	bool (* expect_null_pointer)(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));
	bool (* expect_null_pointer_errors_only)(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));

	/**
	   Verify that @param pointer is valid (non-NULL) pointer

	   Print log specified by @param fmt and following args
	   regardless of results of the verification or (in case of
	   _errors_only() variant) only if the check has shown that
	   pointer *IS* NULL pointer, which is regarded as failure
	   of expectation, i.e. an error.

	   Right now there are no additional checks of validity of the
	   pointer. Function only checks if it is non-NULL pointer.

	   @return true if this comparison shows that the pointer is a non-NULL pointer
	   @return false otherwise
	*/
	bool (* expect_valid_pointer)(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));
	bool (* expect_valid_pointer_errors_only)(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));




	/**
	   An assert - not much to explain
	*/
	void (* assert2)(struct cw_test_executor_t * self, bool condition, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));




	/**
	   @brief Print an informative header with information about current test

	   Call this function on top of a test function to display
	   some basic information about test: current test topic,
	   current sound system and name of test function. This should
	   get a basic overview of what is about to be tested now.

	   Name of the function is usually passed through @param fmt
	   argument. @param fmt can be also printf()-like format
	   string, followed by additional arguments
	*/
	void (* print_test_header)(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));

	/**
	   @brief Print a not-so-informative test footer

	   Call the function at the very end of test function to
	   display indication that test function with name @param
	   function_name has completed its work.
	*/
	void (* print_test_footer)(struct cw_test_executor_t * self, const char * function_name);




	/**
	   \brief Process command line arguments of test executable

	   @return 0 on success: program can continue after arguments
	   have been processes (or no arguments were provided),

	   Function exits with EXIT_FAILURE status when function
	   encountered errors during processing of command line
	   args. Help text is printed before the exit.

	   Function exits with EXIT_SUCCESS status when "help" option
	   was requested. Help text is printed before the exit.
	*/
	int (* process_args)(struct cw_test_executor_t * self, int argc, char * const argv[]);




	/**
	   @brief Print summary of program's arguments and options that are in effect

	   Print names of test topics that will be tested and of sound
	   systems that will be used in tests. Call this function
	   after processing command line arguments with
	   ::process_args().
	*/
	void (* print_test_options)(struct cw_test_executor_t * self);




	/**
	   @brief Print a table with summary of test statistics

	   The statistics are presented as a table. There are columns
	   with test topics and rows with sound modules. Each table
	   cell contains a total number of tests in given category and
	   number of errors (failed test functions) in that category.
	*/
	void (* print_test_stats)(struct cw_test_executor_t * self);




	/**
	   @brief Get label of currently tested test topic

	   Return pointer to static string buffer with label of
	   currently tested test topic. Notice: if the topic is not
	   set, function returns "unknown" text label.
	*/
	const char * (* get_current_topic_label)(struct cw_test_executor_t * self);

	/**
	   @brief Get label of currently used sound system

	   Return pointer to static string buffer with label of
	   currently used sound system  Notice: if the sound system is not
	   set, function returns "None" text label.
	*/
	const char * (* get_current_sound_system_label)(struct cw_test_executor_t * self);




	/**
	   Log information to cw_test_executor_t::stdout file (if it is set).
	   Add "[II]" mark at the beginning.
	   Add message prefix at the beginning.
	   Don't add newline character at the end.

	   @return number of characters printed
	*/
        int (* log_info)(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));

	/**
	   Log text to cw_test_executor_t::stdout file (if it is set).
	   Don't add "[II]" mark at the beginning.
	   Don't add message prefix at the beginning.
	   Don't add newline character at the end.
	*/
	void (* log_info_cont)(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));

	/**
	   Flush file descriptor used to log info messages
	*/
	void (* flush_info)(struct cw_test_executor_t * self);

	/**
	   Log error to cw_test_executor_t::stdout file (if it is set).
	   Add "[EE]" mark at the beginning.
	   Add message prefix at the beginning.
	   Don't add newline character at the end.
	*/
	void (* log_error)(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));

	/**
	   See whether or not a given test topic was requested from
	   command line. By default, if not specified in command line,
	   all test topics are requested.
	*/
	bool (* test_topic_was_requested)(struct cw_test_executor_t * self, int libcw_test_topic);

	/**
	   See whether or not a given sound system was requested from
	   command line. By default, if not specified in command line,
	   all sound systems are requested.

	   However, if a host machine does not support some sound
	   system (e.g. because a library is missing), such sound
	   system is excluded from list of requested sound systems.
	*/
	bool (* sound_system_was_requested)(struct cw_test_executor_t * self, int sound_system);




	/**
	   @brief Main test loop that walks through given @param
	   @test_sets and executes all test function specified in
	   @param test_sets
	*/
	int (* main_test_loop)(struct cw_test_executor_t * cte, struct cw_test_set_t * test_sets);

} cw_test_executor_t;




/**
   @brief Initialize cw_text_executor_t object @param cte

   Some messages printed by logger function of cw_text_executor_t
   object will be prefixed with @param msg_prefix.

   No resources are allocated in @param cte during the call, so there
   is no "deinit" function.
*/
void cw_test_init(cw_test_executor_t * cte, FILE * stdout, FILE * stderr, const char * msg_prefix);




typedef int (* cw_test_function_t)(cw_test_executor_t * cte);

typedef enum cw_test_set_valid {
	CW_TEST_SET_INVALID,
	CW_TEST_SET_VALID,
} cw_test_set_valid;

typedef enum cw_test_api_tested {
	CW_TEST_API_LEGACY, /* Tests of functions from libcw.h. Legacy API that does not allow using multiple gen/key/rec objects. */
	CW_TEST_API_MODERN, /* Tests of internal functions that operate on explicit gen/key/rec objects (functions that accept such objects as arguments). */
} cw_test_api_tested;




typedef	struct cw_test_function_wrapper_t {
	cw_test_function_t fn;
	const char * name; /* Unique label/name of test function, used to execute only one test function from whole set. Can be empty/NULL. */
} cw_test_function_wrapper_t;




typedef struct cw_test_set_t {
	cw_test_set_valid set_valid; /* Invalid test set is a guard element in array of test sets. */
	cw_test_api_tested api_tested;

	int topics[LIBCW_TEST_TOPIC_MAX]; /* Test topics tested by given test set. */
	int sound_systems[LIBCW_TEST_SOUND_SYSTEM_MAX]; /* Sound systems that need to be configured to test given test set. */
	cw_test_function_wrapper_t test_functions[100]; /* Right now my test sets have only a few test functions. For now 100 is a safe limit. */
} cw_test_set_t;




#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CW_TEST_FUNCTION_INSERT(function_pointer) { .fn = function_pointer, .name = TOSTRING(function_pointer) }




#endif /* #ifndef _LIBCW_TEST_FRAMEWORK_H_ */
