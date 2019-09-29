/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef _LIBCW_TEST_UTILS_H_
#define _LIBCW_TEST_UTILS_H_




#include <stddef.h> /* size_t */
#include <stdio.h>
#include <stdbool.h>


#include <libcw.h>


#define out_file stdout

/* Total width of test name + test status printed in console (without
   ending '\n'). Remember that some consoles have width = 80. Not
   everyone works in X. */
#define default_cw_test_print_n_chars 75

#define LIBCW_TEST_ALL_TOPICS           "tgkro"   /* generator, tone queue, key, receiver, other. */
#define LIBCW_TEST_ALL_SOUND_SYSTEMS    "ncoap"   /* null, console, oss, alsa, pulseaudio. */




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
	cw_test_stats_t stats2[LIBCW_TEST_SOUND_SYSTEM_MAX][LIBCW_TEST_TOPIC_MAX];

	int tested_sound_systems[LIBCW_TEST_SOUND_SYSTEM_MAX + 1];
	int tested_topics[LIBCW_TEST_TOPIC_MAX + 1];


	bool (* expect_eq_int)(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));
	bool (* expect_eq_int_errors_only)(struct cw_test_executor_t * self, int expected_value, int received_value, const char * fmt, ...) __attribute__ ((format (printf, 4, 5)));

	bool (* expect_null_pointer)(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));
	bool (* expect_null_pointer_errors_only)(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));

	bool (* expect_valid_pointer)(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));
	bool (* expect_valid_pointer_errors_only)(struct cw_test_executor_t * self, const void * pointer, const char * fmt, ...) __attribute__ ((format (printf, 3, 4)));


	void (* print_test_header)(struct cw_test_executor_t * self, const char * text);
	void (* print_test_footer)(struct cw_test_executor_t * self, const char * text);

	int (* process_args)(struct cw_test_executor_t * self, int argc, char * const argv[]);
	void (* print_args_summary)(struct cw_test_executor_t * self);
	int (* fill_default_sound_systems_and_topics)(struct cw_test_executor_t * self);

	void (* print_test_stats)(struct cw_test_executor_t * self);

	const char * (* get_current_sound_system_label)(struct cw_test_executor_t * self);
	void (* set_current_sound_system)(struct cw_test_executor_t * self, int sound_system);

	/**
	   Log information to cw_test_executor_t::stdout file (if it is set).
	   Add "[II]" mark at the beginning.
	   Add message prefix at the beginning.
	   Don't add newline character at the end.
	*/
	void (* log_info)(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));
	/**
	   Log information to cw_test_executor_t::stdout file (if it is set).
	   Don't add "[II]" mark at the beginning.
	   Don't add message prefix at the beginning.
	   Don't add newline character at the end.
	*/
	void (* log_info_cont)(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));
	/**
	   Log error to cw_test_executor_t::stdout file (if it is set).
	   Add "[EE]" mark at the beginning.
	   Add message prefix at the beginning.
	   Don't add newline character at the end.
	*/
	void (* log_err)(struct cw_test_executor_t * self, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));

	/**
	   See whether or not a given test topic or sound system was
	   requested from command line. By default, if not specified
	   in command line, all test topics and all sound systems are
	   requested.

	   If a host machine does not support some sound system
	   (e.g. because a library is missing), such sound system is
	   excluded from list of requested sound systems.
	*/
	bool (* test_topic_was_requested)(struct cw_test_executor_t * self, int libcw_test_topic);
	bool (* sound_system_was_requested)(struct cw_test_executor_t * self, int sound_system);

	void (* print_sound_systems)(struct cw_test_executor_t * self, int * sound_systems);
	void (* print_topics)(struct cw_test_executor_t * self, int * topics);

	bool (* test_topic_is_member)(struct cw_test_executor_t * cte, int topic, int * topics);
	bool (* sound_system_is_member)(struct cw_test_executor_t * cte, int sound_system, int * sound_systems);

	int (* main_test_loop)(struct cw_test_executor_t * cte, struct cw_test_set_t * test_sets);

} cw_test_executor_t;



void cw_test_init(cw_test_executor_t * self, FILE * stdout, FILE * stderr, const char * msg_prefix);

void cw_test_print_help(const char *progname);



typedef int (* cw_test_function_t)(cw_test_executor_t * cte);
typedef int (* tester_fn)(cw_test_executor_t * cte);



typedef enum cw_test_set_valid {
	CW_TEST_SET_INVALID,
	CW_TEST_SET_VALID,
} cw_test_set_valid;

typedef enum cw_test_api_tested {
	CW_TEST_API_LEGACY,
	CW_TEST_API_MODERN,
} cw_test_api_tested;



typedef struct cw_test_set_t {
	cw_test_set_valid set_valid;
	cw_test_api_tested api_tested;

	int topics[LIBCW_TEST_TOPIC_MAX];
	int sound_systems[LIBCW_TEST_SOUND_SYSTEM_MAX];
	cw_test_function_t test_functions[100];
} cw_test_set_t;





int cw_test_topics_with_sound_systems(cw_test_executor_t * cte, tester_fn test_topics_with_current_sound_system);



#endif /* #ifndef _LIBCW_TEST_UTILS_H_ */
