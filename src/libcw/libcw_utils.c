/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)

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


/**
   \file libcw_utils.c

   \brief Utility functions that should be put in a common place.

   One of the utilities is cw_dlopen_internal() - a function that
   allowed me to drop compile-time dependency on ALSA libs and
   PulseAudio libs, and replace it with run-time dependency.

   You will find calls to dlclose() in libcw_alsa.c and libcw_pa.c.
*/





#include "config.h"


#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h> /* strtol() */

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif


#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif


#include "libcw2.h"
#include "libcw_gen.h"
#include "libcw_test.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "cw_copyright.h"




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




/* Human-readable labels of audio systems.
   Indexed by values of "enum cw_audio_systems". */
static const char * cw_audio_system_labels[] = {
	"None",
	"Null",
	"Console",
	"OSS",
	"ALSA",
	"PulseAudio",
	"Soundcard" };





/**
   \brief Return version number of libcw library

   Return version number of the library, split into \p current, \p
   revision, \p age. These three properties are described here:
   http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html

   \reviewed on 2017-02-04

   testedin::test_cw_version()
*/
void cw_version(int * current, int * revision, int * age)
{
	char *endptr = NULL;

	/* LIBCW_VERSION: "current:revision:age", libtool notation. */

	long int c = strtol(LIBCW_VERSION, &endptr, 10);
	if (current) {
		*current = (int) c;
	}

	long int r = strtol(endptr + 1, &endptr, 10);
	if (revision) {
		*revision = (int) r;
	}

	long int a = strtol(endptr + 1, &endptr, 10);
	if (age) {
		*age = (int) a;
	}

	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_INTERNAL, CW_DEBUG_INFO,
		      "libcw/utils: current:revision:age: %ld:%ld:%ld\n", c, r, a);

	return;
}




/**
   \brief Print libcw's license text to stdout

   testedin::test_cw_license()

   \reviewed on 2017-02-04

   Function prints to stdout information about libcw version, followed
   by short text presenting libcw's copyright and license notice.
*/
void cw_license(void)
{
	int current, revision, age;
	cw_version(&current, &revision, &age);

	printf("libcw version %d.%d.%d\n", current, revision, age);
	printf("%s\n", CW_COPYRIGHT);

	return;
}




/**
   \brief Get a readable label of given audio system

   The function returns one of following strings:
   None, Null, Console, OSS, ALSA, PulseAudio, Soundcard

   Returned pointer is owned and managed by the library.

   TODO: change the declaration to "const char *const cw_get_audio_system_label(...)"?

   \reviewed on 2017-02-04

   \param audio_system - ID of audio system

   \return audio system's label
*/
const char * cw_get_audio_system_label(int audio_system)
{
	return cw_audio_system_labels[audio_system];
}




/**
   \brief Convert microseconds to struct timespec

   Function fills fields of struct timespec \p t (seconds and nanoseconds)
   based on value of \p usecs.
   \p usecs should be non-negative.

   This function is just a simple wrapper for few lines of code.

   testedin::test_cw_usecs_to_timespec_internal()

   \reviewed on 2017-02-04

   \param t - pointer to existing struct to be filled with data
   \param usecs - value to convert to timespec
*/
void cw_usecs_to_timespec_internal(struct timespec * t, int usecs)
{
	assert (usecs >= 0);
	assert (t);

	int sec = usecs / CW_USECS_PER_SEC;
	int usec = usecs % CW_USECS_PER_SEC;

	t->tv_sec = sec;
	t->tv_nsec = usec * 1000;

	return;
}




/**
   \brief Sleep for period of time specified by given timespec

   Function sleeps for given amount of seconds and nanoseconds, as
   specified by \p n.

   The function uses nanosleep(), and can handle incoming SIGALRM signals
   that cause regular nanosleep() to return. The function calls nanosleep()
   until all time specified by \p n has elapsed.

   The function may sleep a little longer than specified by \p n if it needs
   to spend some time handling SIGALRM signal. Other restrictions from
   nanosleep()'s man page also apply.

   \reviewed on 2017-02-04

   \param n - period of time to sleep
*/
void cw_nanosleep_internal(const struct timespec * n)
{
	struct timespec rem = { .tv_sec = n->tv_sec, .tv_nsec = n->tv_nsec };

	int rv = 0;
	do {
		struct timespec req = { .tv_sec = rem.tv_sec, .tv_nsec = rem.tv_nsec };
		//fprintf(stderr, " -- sleeping for %ld s, %ld ns\n", req.tv_sec, req.tv_nsec);
		rv = nanosleep(&req, &rem);
		if (rv) {
			//fprintf(stderr, " -- remains %ld s, %ld ns\n", rem.tv_sec, rem.tv_nsec);
		}
	} while (rv);

	return;
}




#if (defined(LIBCW_WITH_ALSA) || defined(LIBCW_WITH_PULSEAUDIO))

#include <dlfcn.h> /* dlopen() and related symbols */

/**
   \brief Try to dynamically open shared library

   Function tries to open a shared library specified by \p name using
   dlopen() system function. On sucess, handle to open library is
   returned via \p handle.

   Name of the library should contain ".so" suffix, e.g.: "libasound.so.2",
   or "libpulse-simple.so".

   \reviewed on 2017-02-04

   \param name - name of library to test
   \param handle - output argument, handle to open library

   \return true on success
   \return false otherwise
*/
bool cw_dlopen_internal(const char * name, void ** handle)
{
	assert (name);

	dlerror();
	void * h = dlopen(name, RTLD_LAZY);
	char * e = dlerror();

	if (e) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw/utils: dlopen() fails for %s with error: %s", name, e);
		return false;
	} else {
		*handle = h;

		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_STDLIB, CW_DEBUG_DEBUG,
			      "libcw/utils: dlopen() succeeds for %s", name);
		return true;
	}
}
#endif




/**
   \brief Validate and return timestamp

   If an input timestamp \p in_timestamp is given (non-NULL pointer),
   validate it for correctness, and if valid, copy contents of
   \p in_timestamp into \p out_timestamp and return CW_SUCCESS.

   If \p in_timestamp is non-NULL and the timestamp is invalid, return
   CW_FAILURE with errno set to EINVAL.

   If \p in_timestamp is not given (NULL), get current time (with
   gettimeofday()), put it in \p out_timestamp and return
   CW_SUCCESS. If call to gettimeofday() fails, return
   CW_FAILURE. gettimeofday() sets its own errno.

   \p out_timestamp cannot be NULL.

   testedin::test_cw_timestamp_validate_internal()

   \reviewed on 2017-02-04

   \param out_timestamp - timestamp to be used by client code after the function call
   \param in_timestamp - timestamp to be validated

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_timestamp_validate_internal(struct timeval * out_timestamp, const struct timeval * in_timestamp)
{
	cw_assert (out_timestamp, "libcw/utils: validate timestamp: pointer to output timestamp is NULL");

	if (in_timestamp) {
		if (in_timestamp->tv_sec < 0
		    || in_timestamp->tv_usec < 0
		    || in_timestamp->tv_usec >= CW_USECS_PER_SEC) {

			errno = EINVAL;
			return CW_FAILURE;
		} else {
			*out_timestamp = *in_timestamp;
			return CW_SUCCESS;
		}
	} else {
		if (gettimeofday(out_timestamp, NULL)) {
			if (out_timestamp->tv_usec < 0) {
				// fprintf(stderr, "Negative usecs in %s\n", __func__);
			}

			perror("libcw/utils: validate timestamp: gettimeofday");
			return CW_FAILURE;
		} else {
			return CW_SUCCESS;
		}
	}
}




/**
   \brief Compare two timestamps

   Compare two timestamps and return the difference between them in
   microseconds, taking care to clamp values which would overflow an int.

   This routine always returns a positive integer in the range 0 to INT_MAX.

   testedin::test_cw_timestamp_compare_internal()

   \reviewed on 2017-02-04

   \param earlier - timestamp to compare
   \param later - timestamp to compare

   \return difference between timestamps (in microseconds)
*/
int cw_timestamp_compare_internal(const struct timeval * earlier, const struct timeval * later)
{
	/* Compare the timestamps, taking care on overflows.

	   At 4 WPM, the dash length is 3*(1200000/4)=900,000 usecs, and
	   the word gap is 2,100,000 usecs.  With the maximum Farnsworth
	   additional delay, the word gap extends to 20,100,000 usecs.
	   This fits into an int with a lot of room to spare, in fact, an
	   int can represent 2,147,483,647 usecs, or around 33 minutes.
	   This is way, way longer than we'd ever want to differentiate,
	   so if by some chance we see timestamps farther apart than this,
	   and it ought to be very, very unlikely, then we'll clamp the
	   return value to INT_MAX with a clear conscience.

	   Note: passing nonsensical or bogus timevals in may result in
	   unpredictable results.  Nonsensical includes timevals with
	   -ve tv_usec, -ve tv_sec, tv_usec >= 1,000,000, etc.
	   To help in this, we check all incoming timestamps for
	   "well-formedness".  However, we assume the  gettimeofday()
	   call always returns good timevals.  All in all, timeval could
	   probably be a better thought-out structure. */

	/* Calculate an initial delta, possibly with overflow. */
	int delta_usec = (later->tv_sec - earlier->tv_sec) * CW_USECS_PER_SEC
		+ later->tv_usec - earlier->tv_usec;

	/* Check specifically for overflow, and clamp if it did. */
	if ((later->tv_sec - earlier->tv_sec) > (INT_MAX / CW_USECS_PER_SEC) + 1
	    || delta_usec < 0) {

		delta_usec = INT_MAX;
		// fprintf(stderr, "earlier =           %10ld : %10ld\n", earlier->tv_sec, earlier->tv_usec);
		// fprintf(stderr, "later   =           %10ld : %10ld\n", later->tv_sec, later->tv_usec);
	}

	/* TODO: add somewhere a debug message informing that we are
	   returning INT_MAX. */

	return delta_usec;
}




/**
   \brief Get speed limits

   Get (through function's arguments) limits on speed of Morse code that
   are supported by libcw.

   See CW_SPEED_MIN and CW_SPEED_MAX in libcw2.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   testedin::test_cw_get_x_limits_internal()
   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-02-04

   \param min_speed - minimal allowed speed
   \param max_speed - maximal allowed speed
*/
void cw_get_speed_limits(int * min_speed, int * max_speed)
{
	if (min_speed) {
		*min_speed = CW_SPEED_MIN;
	}

	if (max_speed) {
		*max_speed = CW_SPEED_MAX;
	}
	return;
}




/**
   \brief Get frequency limits

   Get (through function's arguments) limits on frequency that are
   supported by libcw.

   See CW_FREQUENCY_MIN and CW_FREQUENCY_MAX in libcw2.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   testedin::test_cw_get_x_limits_internal()
   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-02-04

   \param min_frequency - minimal allowed frequency
   \param max_frequency - maximal allowed frequency
*/
void cw_get_frequency_limits(int * min_frequency, int * max_frequency)
{
	if (min_frequency) {
		*min_frequency = CW_FREQUENCY_MIN;
	}

	if (max_frequency) {
		*max_frequency = CW_FREQUENCY_MAX;
	}
	return;
}




/**
   \brief Get volume limits

   Get (through function's arguments) limits on volume of sound
   supported by libcw and generated by generator.

   See CW_VOLUME_MIN and CW_VOLUME_MAX in libcw2.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   testedin::test_cw_get_x_limits_internal()
   testedin::test_cw_gen_volume_functions()
   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-02-04

   \param min_volume - minimal allowed volume
   \param max_volume - maximal allowed volume
*/
void cw_get_volume_limits(int * min_volume, int * max_volume)
{
	if (min_volume) {
		*min_volume = CW_VOLUME_MIN;
	}
	if (max_volume) {
		*max_volume = CW_VOLUME_MAX;
	}
	return;
}




/**
   \brief Get gap limits

   Get (through function's arguments) limits on gap in cw signal
   supported by libcw.

   See CW_GAP_MIN and CW_GAP_MAX in libcw2.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   testedin::test_cw_get_x_limits_internal()
   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-02-04

   \param min_gap - minimal allowed gap
   \param max_gap - maximal allowed gap
*/
void cw_get_gap_limits(int * min_gap, int * max_gap)
{
	if (min_gap) {
		*min_gap = CW_GAP_MIN;
	}
	if (max_gap) {
		*max_gap = CW_GAP_MAX;
	}
	return;
}




/**
   \brief Get tolerance limits

   Get (through function's arguments) limits on "tolerance" parameter
   supported by libcw.

   See CW_TOLERANCE_MIN and CW_TOLERANCE_MAX in libcw2.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   testedin::test_cw_get_x_limits_internal()

   \reviewed on 2017-02-04

   \param min_tolerance - minimal allowed tolerance
   \param max_tolerance - maximal allowed tolerance
*/
void cw_get_tolerance_limits(int * min_tolerance, int * max_tolerance)
{
	if (min_tolerance) {
		*min_tolerance = CW_TOLERANCE_MIN;
	}
	if (max_tolerance) {
		*max_tolerance = CW_TOLERANCE_MAX;
	}
	return;
}




/**
   \brief Get weighting limits

   Get (through function's arguments) limits on "weighting" parameter
   supported by libcw.

   See CW_WEIGHTING_MIN and CW_WEIGHTING_MAX in libcw2.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   testedin::test_cw_get_x_limits_internal()
   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-02-04

   \param min_weighting - minimal allowed weighting
   \param max_weighting - maximal allowed weighting
*/
void cw_get_weighting_limits(int * min_weighting, int * max_weighting)
{
	if (min_weighting) {
		*min_weighting = CW_WEIGHTING_MIN;
	}
	if (max_weighting) {
		*max_weighting = CW_WEIGHTING_MAX;
	}
	return;
}




#ifdef LIBCW_UNIT_TESTS




#include <stdbool.h>




/**
   tests::cw_timestamp_compare_internal()
*/
unsigned int test_cw_timestamp_compare_internal(cw_test_stats_t * stats)
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

		int delta = cw_timestamp_compare_internal(&earlier_timestamp, &later_timestamp);
		failure = (delta != expected_deltas[i]);
		if (failure) {
			fprintf(out_file, "libcw:utils:compare timestamp: test #%d: unexpected delta: %d != %d\n", i, delta, expected_deltas[i]);
			break;
		}

		i++;
	}

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw:utils:compare timestamp:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	return 0;
}





/**
   tests::cw_timestamp_validate_internal()
*/
unsigned int test_cw_timestamp_validate_internal(cw_test_stats_t * stats)
{
	struct timeval out_timestamp;
	struct timeval in_timestamp;
	struct timeval ref_timestamp; /* Reference timestamp. */
	int rv = 0;

	bool failure = true;
	int n = 0;




	/* Test 1 - get current time. */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;

	cw_assert (!gettimeofday(&ref_timestamp, NULL), "libcw:utils:validate timestamp 1: failed to get reference time");

	rv = cw_timestamp_validate_internal(&out_timestamp, NULL);
	failure = (CW_SUCCESS != rv);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:current timestamp:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

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

	rv = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	failure = (CW_SUCCESS != rv)
		|| (out_timestamp.tv_sec != in_timestamp.tv_sec)
		|| (out_timestamp.tv_usec != in_timestamp.tv_usec);

	if (failure) {
		fprintf(out_file, "libcw:utils:validate timestamp:validate and copy:"
			"rv = %d,"
			"%d / %d,"
			"%d / %d",
			rv,
			(int) out_timestamp.tv_sec, (int) in_timestamp.tv_sec,
			(int) out_timestamp.tv_usec, (int) in_timestamp.tv_usec);
	}
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:validate and copy:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* Test 3 - detect invalid seconds in input timestamp. */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = -1;
	in_timestamp.tv_usec = 987;

	rv = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	failure = (rv == CW_SUCCESS) || (errno != EINVAL);
	if (failure) {
		fprintf(out_file, "libcw:utils:validate timestamp:invalid seconds: rv==CW_FAILURE = %d, errno==EINVAL = %d\n", rv == CW_FAILURE, errno == EINVAL);
	}
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:invalid seconds:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* Test 4 - detect invalid microseconds in input timestamp (microseconds too large). */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = 123;
	in_timestamp.tv_usec = CW_USECS_PER_SEC + 1;

	rv = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	failure = (rv == CW_SUCCESS)
		|| (errno != EINVAL);
	if (failure) {
		fprintf(out_file, "libcw:utils:validate timestamp:invalid milliseconds: rv==CW_FAILURE = %d, errno==EINVAL = %d\n", rv == CW_FAILURE, errno == EINVAL);
	}
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:invalid milliseconds:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* Test 5 - detect invalid microseconds in input timestamp (microseconds negative). */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = 123;
	in_timestamp.tv_usec = -1;

	rv = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	failure = (rv == CW_SUCCESS) || (errno != EINVAL);
	if (failure) {
		fprintf(out_file, "libcw:utils:validate timestamp:negative milliseconds: rv==CW_FAILURE = %d, errno==EINVAL = %d\n", rv == CW_FAILURE, errno == EINVAL);
	}
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:negative milliseconds:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	return 0;
}





/**
   tests::cw_usecs_to_timespec_internal()
*/
unsigned int test_cw_usecs_to_timespec_internal(cw_test_stats_t * stats)
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
		failure = (result.tv_sec != input_data[i].t.tv_sec);
		if (failure) {
			fprintf(out_file, "libcw:utils:usecs to timespec: test %d: %d [s] != %d [s]\n", result.tv_sec, input_data[i].t.tv_sec);
			break;
		}

		failure = (result.tv_nsec != input_data[i].t.tv_nsec);
		if (failure) {
			fprintf(out_file, "libcw:utils:usecs to timespec: test %d: %d [ns] != %d [ns]\n", result.tv_nsec, input_data[i].t.tv_nsec);
			break;
		}

		i++;
	}

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw:utils:usecs to timespec:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}






/**
   tests::cw_version()
*/
unsigned int test_cw_version_internal(cw_test_stats_t * stats)
{
	int current = 77, revision = 88, age = 99; /* Dummy values. */
	cw_version(&current, &revision, &age);

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
			failure = (i != 3);
			if (failure) {
				fprintf(out_file, "libcw:utils:version: stopping at token %d\n", i);
			}
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
			fprintf(out_file, "libcw:utils:version: too many tokens in \"%s\"\n", LIBCW_VERSION);
		}
	}


	failure = (current != c) || (revision != r) || (age != a);
	if (failure) {
		fprintf(out_file, "libcw:utils:version: current: %d / %d; revision: %d / %d; age: %d / %d\n", current, c, revision, r, age, a);
	}

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw:utils:version: %d:%d:%d:", c, r, a);
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   tests::cw_license()
*/
unsigned int test_cw_license_internal(cw_test_stats_t * stats)
{
	/* Well, there isn't much to test here. The function just
	   prints the license to stdout, and that's it. */

	cw_license();

	false ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw:utils:license:");
	CW_TEST_PRINT_TEST_RESULT (false, n);


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
unsigned int test_cw_get_x_limits_internal(cw_test_stats_t * stats)
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

		bool min_failure = true;
		bool max_failure = true;
		int n = 0;

		/* Get limits of a parameter. */
		test_data[i].getter(&test_data[i].get_min, &test_data[i].get_max);

		/* Test that limits are as expected (values received
		   by function call match those defined in library's
		   header file). */
		min_failure = (test_data[i].get_min != test_data[i].min);
		if (min_failure) {
			fprintf(out_file, "libcw:utils:limits: failed to get correct minimum of %s\n", test_data[i].name);
		}

		max_failure = (test_data[i].get_max != test_data[i].max);
		if (max_failure) {
			fprintf(out_file, "libcw:utils:limits: failed to get correct maximum of %s\n", test_data[i].name);
		}

		min_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:utils:get min %s:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (min_failure, n);

		max_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:utils:get max %s:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (max_failure, n);
	}


	return 0;
}





#endif /* #ifdef LIBCW_UNIT_TESTS */
