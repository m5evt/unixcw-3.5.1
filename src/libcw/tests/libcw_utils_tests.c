#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>




#include "tests/libcw_test_framework.h"
#include "libcw_utils.h"
#include "libcw_utils_tests.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_key.h"
#include "libcw.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/utils: "




/**
   tests::cw_timestamp_compare_internal()
*/
int test_cw_timestamp_compare_internal(cw_test_executor_t * cte)
{
	struct timeval earlier_timestamp;
	struct timeval later_timestamp;

	/* TODO: I think that there may be more tests to perform for
	   the function, testing handling of overflow. */

	int expected_deltas[] = { 0,
				  1,
				  1001,
				  CW_USECS_PER_SEC - 1,
				  CW_USECS_PER_SEC,
				  CW_USECS_PER_SEC + 1,
				  2 * CW_USECS_PER_SEC - 1,
				  2 * CW_USECS_PER_SEC,
				  2 * CW_USECS_PER_SEC + 1,
				  -1 }; /* Guard. */


	earlier_timestamp.tv_sec = 3;
	earlier_timestamp.tv_usec = 567;

	bool failure = true;

	int i = 0;
	while (expected_deltas[i] != -1) {

		later_timestamp.tv_sec = earlier_timestamp.tv_sec + (expected_deltas[i] / CW_USECS_PER_SEC);
		later_timestamp.tv_usec = earlier_timestamp.tv_usec + (expected_deltas[i] % CW_USECS_PER_SEC);

		const int delta = cw_timestamp_compare_internal(&earlier_timestamp, &later_timestamp);
		if (!cte->expect_eq_int_errors_only(cte, expected_deltas[i], delta, "libcw:utils:compare timestamp: test #%d: unexpected delta: %d != %d\n", i, delta, expected_deltas[i])) {
			failure = true;
			break;
		}

		i++;
	}


	cte->expect_eq_int(cte, false, failure, "libcw:utils:compare timestamp:");

	return 0;
}





/**
   tests::cw_timestamp_validate_internal()
*/
int test_cw_timestamp_validate_internal(cw_test_executor_t * cte)
{
	struct timeval out_timestamp;
	struct timeval in_timestamp;
	struct timeval ref_timestamp; /* Reference timestamp. */
	int cwret = CW_FAILURE;



	/* Test 1 - get current time. */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;

	cw_assert (!gettimeofday(&ref_timestamp, NULL), "libcw:utils:validate timestamp 1: failed to get reference time");

	cwret = cw_timestamp_validate_internal(&out_timestamp, NULL);
	cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "libcw:utils:validate timestamp:current timestamp:");

#if 0
	fprintf(stderr, "\nINFO: delay in getting timestamp is %d microseconds\n",
		cw_timestamp_compare_internal(&ref_timestamp, &out_timestamp));
#endif



	/* Test 2 - validate valid input timestamp and copy it to
	   output timestamp. */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = 1234;
	in_timestamp.tv_usec = 987;

	cwret = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "libcw:utils:validate timestamp:validate and copy (cwret):");
	cte->expect_eq_int_errors_only(cte, in_timestamp.tv_sec, out_timestamp.tv_sec, "libcw:utils:validate timestamp:validate and copy (copy sec):");
	cte->expect_eq_int_errors_only(cte, in_timestamp.tv_usec, out_timestamp.tv_usec, "libcw:utils:validate timestamp:validate and copy (copy usec):");




	/* Test 3 - detect invalid seconds in input timestamp. */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = -1;
	in_timestamp.tv_usec = 987;

	cwret = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	cte->expect_eq_int_errors_only(cte, CW_FAILURE, cwret, "libcw:utils:validate timestamp:invalid seconds (cwret)");
	cte->expect_eq_int_errors_only(cte, EINVAL, errno, "libcw:utils:validate timestamp:invalid seconds (errno)");



	/* Test 4 - detect invalid microseconds in input timestamp (microseconds too large). */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = 123;
	in_timestamp.tv_usec = CW_USECS_PER_SEC + 1;

	cwret = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	cte->expect_eq_int_errors_only(cte, CW_FAILURE, cwret, "libcw:utils:validate timestamp:invalid milliseconds (cwret)");
	cte->expect_eq_int_errors_only(cte, EINVAL, errno, "libcw:utils:validate timestamp:invalid milliseconds (cwret)");



	/* Test 5 - detect invalid microseconds in input timestamp (microseconds negative). */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = 123;
	in_timestamp.tv_usec = -1;

	cwret = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	cte->expect_eq_int_errors_only(cte, CW_FAILURE, cwret, "libcw:utils:validate timestamp:negative milliseconds (cwret)");
	cte->expect_eq_int_errors_only(cte, EINVAL, errno, "libcw:utils:validate timestamp:negative milliseconds (cwret)");


	return 0;
}





/**
   tests::cw_usecs_to_timespec_internal()
*/
int test_cw_usecs_to_timespec_internal(cw_test_executor_t * cte)
{
	struct {
		int input;
		struct timespec t;
	} input_data[] = {
		/* input in ms    /   expected output seconds : milliseconds */
		{           0,    {   0,             0 }},
		{     1000000,    {   1,             0 }},
		{     1000004,    {   1,          4000 }},
		{    15000350,    {  15,        350000 }},
		{          73,    {   0,         73000 }},
		{          -1,    {   0,             0 }},
	};

	bool failure = true;

	int i = 0;
	while (input_data[i].input != -1) {
		struct timespec result = { .tv_sec = 0, .tv_nsec = 0 };
		cw_usecs_to_timespec_internal(&result, input_data[i].input);
#if 0
		fprintf(stderr, "input = %d usecs, output = %ld.%ld\n",
			input_data[i].input, (long) result.tv_sec, (long) result.tv_nsec);
#endif
		if (cte->expect_eq_int_errors_only(cte, input_data[i].t.tv_sec, result.tv_sec, "libcw:utils:usecs to timespec: test %d: %ld [s] != %ld [s]\n", i, result.tv_sec, input_data[i].t.tv_sec)) {
			failure = true;
			break;
		}
		if (!cte->expect_eq_int_errors_only(cte, input_data[i].t.tv_nsec, result.tv_nsec, "libcw:utils:usecs to timespec: test %d: %ld [ns] != %ld [ns]\n", i, result.tv_nsec, input_data[i].t.tv_nsec)) {
			failure = true;
			break;
		}

		i++;
	}

	cte->expect_eq_int(cte, false, failure, "libcw:utils:usecs to timespec:");

	return 0;
}






/**
   tests::cw_version()
*/
int test_cw_version_internal(cw_test_executor_t * cte)
{
	int current = 77, revision = 88, age = 99; /* Dummy values. */
	cw_get_lib_version(&current, &revision, &age);

	/* Library's version is defined in LIBCW_VERSION. cw_version()
	   uses three calls to strtol() to get three parts of the
	   library version.

	   Let's use a different approach to convert LIBCW_VERSION
	   into numbers. */

#define VERSION_LEN_MAX 30
	cw_assert (strlen(LIBCW_VERSION) <= VERSION_LEN_MAX, "LIBCW_VERSION longer than expected!\n");

	char buffer[VERSION_LEN_MAX + 1];
	strncpy(buffer, LIBCW_VERSION, VERSION_LEN_MAX);
	buffer[VERSION_LEN_MAX] = '\0';
#undef VERSION_LEN_MAX

	char *str = buffer;
	int c = 0, r = 0, a = 0;

	bool failure = true;

	for (int i = 0; ; i++, str = NULL) {

		char * token = strtok(str, ":");
		if (token == NULL) {
			/* We should end tokenizing process after 3 valid tokens, no more and no less. */
			cte->expect_eq_int(cte, 3, i, "libcw:utils:version: stopping at token %d\n", i);
			break;
		}

		if (i == 0) {
			c = atoi(token);
		} else if (i == 1) {
			r = atoi(token);
		} else if (i == 2) {
			a = atoi(token);
		} else {
			failure = true;
			cte->expect_eq_int_errors_only(cte, false, failure, "libcw:utils:version: too many tokens in '%s\': %d\n", LIBCW_VERSION, i);
		}
	}

	cte->expect_eq_int(cte, current, c, "libcw:utils:version: current: %d / %dn", current, c);
	cte->expect_eq_int(cte, revision, r, "libcw:utils:version: revision: %d / %d", revision, r);
	cte->expect_eq_int(cte, age, a, "libcw:utils:version: age: %d / %d", age, a);

	return 0;
}





/**
   tests::cw_license()
*/
int test_cw_license_internal(cw_test_executor_t * cte)
{
	/* Well, there isn't much to test here. The function just
	   prints the license to stdout, and that's it. */

	cw_license();
	cte->expect_eq_int(cte, false, false, "libcw:utils:license:");

	return 0;
}





/**
   \brief Ensure that we can obtain correct values of main parameter limits

   tests::cw_get_speed_limits()
   tests::cw_get_frequency_limits()
   tests::cw_get_volume_limits()
   tests::cw_get_gap_limits()
   tests::cw_get_tolerance_limits()
   tests::cw_get_weighting_limits()
*/
int test_cw_get_x_limits_internal(cw_test_executor_t * cte)
{
	struct {
		void (* getter)(int *min, int *max);
		int min;     /* Minimum hardwired in library. */
		int max;     /* Maximum hardwired in library. */
		int get_min; /* Minimum received in function call. */
		int get_max; /* Maximum received in function call. */

		const char *name;
	} test_data[] = {
		/*                                                                initial values                */
		{ cw_get_speed_limits,      CW_SPEED_MIN,      CW_SPEED_MAX,      10000,  -10000,  "speed"     },
		{ cw_get_frequency_limits,  CW_FREQUENCY_MIN,  CW_FREQUENCY_MAX,  10000,  -10000,  "frequency" },
		{ cw_get_volume_limits,     CW_VOLUME_MIN,     CW_VOLUME_MAX,     10000,  -10000,  "volume"    },
		{ cw_get_gap_limits,        CW_GAP_MIN,        CW_GAP_MAX,        10000,  -10000,  "gap"       },
		{ cw_get_tolerance_limits,  CW_TOLERANCE_MIN,  CW_TOLERANCE_MAX,  10000,  -10000,  "tolerance" },
		{ cw_get_weighting_limits,  CW_WEIGHTING_MIN,  CW_WEIGHTING_MAX,  10000,  -10000,  "weighting" },
		{ NULL,                     0,                 0,                      0,      0,  NULL        }

	};

	for (int i = 0; test_data[i].getter; i++) {

		/* Get limits of a parameter. */
		test_data[i].getter(&test_data[i].get_min, &test_data[i].get_max);

		/* Test that limits are as expected (values received
		   by function call match those defined in library's
		   header file). */
		cte->expect_eq_int(cte, test_data[i].get_min, test_data[i].min, "libcw:utils:get min %s:", test_data[i].name);
		cte->expect_eq_int(cte, test_data[i].get_max, test_data[i].max, "libcw:utils:get max %s:", test_data[i].name);
	}


	return 0;
}
