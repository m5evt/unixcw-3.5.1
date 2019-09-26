/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)

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
#include <stdbool.h>
#include <dlfcn.h> /* dlopen() and related symbols */
#include <stdlib.h> /* strtol() */
#include <limits.h> /* INT_MAX, for clang. */

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif


#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif




#include "libcw.h"
#include "libcw_gen.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_signal.h"
#include "cw_copyright.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/utils: "




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




/* Human-readable labels of audio systems.
   Indexed by values of "enum cw_audio_systems". */
static const char *cw_audio_system_labels[] = {
	"None",
	"Null",
	"Console",
	"OSS",
	"ALSA",
	"PulseAudio",
	"Soundcard" };





/* Finalization and cleanup. */
static void cw_finalization_clock_internal(void);





/**
   \brief Return version number of libcw library

   Return the version number of the library.
   Version numbers (major and minor) are returned as an int,
   composed of major_version << 16 | minor_version.

   \return library's major and minor version number encoded as single int
*/
int cw_version(void)
{
	char *endptr = NULL;

	/* LIBCW_VERSION: "current:revision:age", libtool notation. */
	long int current = strtol(LIBCW_VERSION, &endptr, 10);
	long int revision = strtol(endptr + 1, &endptr, 10);
	__attribute__((unused)) long int age = strtol(endptr + 1, &endptr, 10);

	// fprintf(stderr, "current:revision:age: %ld:%ld:%ld\n", current, revision, age);

	/* TODO: Return all three parts of library version. */
	return ((int) current) << 16 | ((int) revision);
}




/**
   \brief Return version number of libcw library

   Return version number of the library, split into \p current, \p
   revision, \p age. These three properties are described here:
   http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
*/
cw_ret cw_get_lib_version(int * current, int * revision, int * age)
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
		      MSG_PREFIX "current:revision:age: %ld:%ld:%ld\n", c, r, a);

	return CW_SUCCESS;
}




cw_ret cw_get_package_version(__attribute__((unused)) int * major, __attribute__((unused)) int * minor, __attribute__((unused)) int * maintenance)
{
	/* TODO: implement. */
	return CW_FAILURE;
}




/**
   \brief Print libcw's license text to stdout

   Function prints to stdout information about libcw version, followed
   by short text presenting libcw's copyright and license notice.
*/
void cw_license(void)
{
	int current, revision, age;
	cw_get_lib_version(&current, &revision, &age);

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
const char *cw_get_audio_system_label(int audio_system)
{
	return cw_audio_system_labels[audio_system];
}




/**
   \brief Convert microseconds to struct timespec

   Function fills fields of struct timespec \p t (seconds and nanoseconds)
   based on value of \p usecs.
   \p usecs should be non-negative.

   This function is just a simple wrapper for few lines of code.

   \reviewed on 2017-02-04

   \param t - pointer to existing struct to be filled with data
   \param usecs - value to convert to timespec
*/
void cw_usecs_to_timespec_internal(struct timespec *t, int usecs)
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
void cw_nanosleep_internal(const struct timespec *n)
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
bool cw_dlopen_internal(const char *name, void **handle)
{
	assert (name);

	dlerror();
	void *h = dlopen(name, RTLD_LAZY);
	char *e = dlerror();

	if (e) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      MSG_PREFIX "dlopen() fails for %s with error: %s", name, e);
		return false;
	} else {
		*handle = h;

		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_STDLIB, CW_DEBUG_DEBUG,
			      MSG_PREFIX "dlopen() succeeds for %s", name);
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

   \reviewed on 2017-02-04

   \param out_timestamp - timestamp to be used by client code after the function call
   \param in_timestamp - timestamp to be validated

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_timestamp_validate_internal(struct timeval *out_timestamp, const volatile struct timeval *in_timestamp)
{
	cw_assert (out_timestamp, MSG_PREFIX "validate timestamp: pointer to output timestamp is NULL");

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

			perror(MSG_PREFIX "validate timestamp: gettimeofday");
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





/* Morse code controls and timing parameters. */





/**
   \brief Get speed limits

   Get (through function's arguments) limits on speed of Morse code that
   are supported by libcw.

   See CW_SPEED_MIN and CW_SPEED_MAX in libcw.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   \reviewed on 2017-02-04

   \param min_speed - minimal allowed speed
   \param max_speed - maximal allowed speed
*/
void cw_get_speed_limits(int *min_speed, int *max_speed)
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

   See CW_FREQUENCY_MIN and CW_FREQUENCY_MAX in libcw.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   \reviewed on 2017-02-04

   \param min_frequency - minimal allowed frequency
   \param max_frequency - maximal allowed frequency
*/
void cw_get_frequency_limits(int *min_frequency, int *max_frequency)
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

   See CW_VOLUME_MIN and CW_VOLUME_MAX in libcw.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   testedin::test_volume_functions()

   \reviewed on 2017-02-04

   \param min_volume - minimal allowed volume
   \param max_volume - maximal allowed volume
*/
void cw_get_volume_limits(int *min_volume, int *max_volume)
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

   See CW_GAP_MIN and CW_GAP_MAX in libcw.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   \reviewed on 2017-02-04

   \param min_gap - minimal allowed gap
   \param max_gap - maximal allowed gap
*/
void cw_get_gap_limits(int *min_gap, int *max_gap)
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

   See CW_TOLERANCE_MIN and CW_TOLERANCE_MAX in libcw.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   \reviewed on 2017-02-04

   \param min_tolerance - minimal allowed tolerance
   \param max_tolerance - maximal allowed tolerance
*/
void cw_get_tolerance_limits(int *min_tolerance, int *max_tolerance)
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

   See CW_WEIGHTING_MIN and CW_WEIGHTING_MAX in libcw.h for values.

   Any of functions two arguments can be NULL - function won't update
   value of that argument.

   \reviewed on 2017-02-04

   \param min_weighting - minimal allowed weighting
   \param max_weighting - maximal allowed weighting
*/
void cw_get_weighting_limits(int *min_weighting, int *max_weighting)
{
	if (min_weighting) {
		*min_weighting = CW_WEIGHTING_MIN;
	}
	if (max_weighting) {
		*max_weighting = CW_WEIGHTING_MAX;
	}
	return;
}





/* Finalization and cleanup. */





/* We prefer to close the soundcard after a period of library inactivity,
   so that other applications can use it.  Ten seconds seems about right.
   We do it in one-second timeouts so that any leaked pending timeouts from
   other facilities don't cause premature finalization. */
static const int CW_AUDIO_FINALIZATION_DELAY = 10000000;

 /* Counter counting down the number of clock calls before we finalize. */
static volatile bool cw_is_finalization_pending = false;
static volatile int cw_finalization_countdown = 0;

/* Use a mutex to suppress delayed finalizations on complete resets. */
static volatile bool cw_is_finalization_locked_out = false;





/**
   \brief Tick a finalization clock

   If finalization is pending, decrement the countdown, and if this reaches
   zero, we've waited long enough to release sound and timeouts.
*/
void cw_finalization_clock_internal(void)
{
	if (cw_is_finalization_pending) {
		/* Decrement the timeout countdown, and finalize if we reach zero. */
		cw_finalization_countdown--;
		if (cw_finalization_countdown <= 0) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
				      MSG_PREFIX "finalization timeout, closing down");

			cw_sigalrm_restore_internal();
			// cw_gen_release_internal(&cw_generator);

			cw_is_finalization_pending = false;
			cw_finalization_countdown = 0;
		} else {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
				      MSG_PREFIX "finalization countdown %d", cw_finalization_countdown);

			/* Request another timeout.  This results in a call to our
			   cw_finalization_cancel_internal below; to ensure that it doesn't
			   really cancel finalization, unset the pending flag, then set it
			   back again after reqesting the timeout. */
			cw_is_finalization_pending = false;
			cw_timer_run_with_handler_internal(CW_USECS_PER_SEC, NULL);
			cw_is_finalization_pending = true;
		}
	}

	return;
}





/**
  Set the finalization pending flag, and request a timeout to call the
  finalization function after a delay of a few seconds.
*/
void cw_finalization_schedule_internal(void)
{
	if (!cw_is_finalization_locked_out && !cw_is_finalization_pending) {
		cw_timer_run_with_handler_internal(CW_USECS_PER_SEC,
						   cw_finalization_clock_internal);

		/* Set the flag and countdown last; calling cw_timer_run_with_handler()
		 * above results in a call to our cw_finalization_cancel_internal(),
		 which clears the flag and countdown if we set them early. */
		cw_is_finalization_pending = true;
		cw_finalization_countdown = CW_AUDIO_FINALIZATION_DELAY / CW_USECS_PER_SEC;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
			      MSG_PREFIX "finalization scheduled");
	}

	return;
}





/**
   Cancel any pending finalization on noting other library activity,
   indicated by a call from the timeout request function telling us
   that it is setting a timeout.
*/
void cw_finalization_cancel_internal(void)
{
	if (cw_is_finalization_pending)  {
		/* Cancel pending finalization and return to doing nothing. */
		cw_is_finalization_pending = false;
		cw_finalization_countdown = 0;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
			      MSG_PREFIX "finalization canceled");
	}

	return;
}





/**
   \brief Reset all library features to their default states

   Clears the tone queue, receive buffers and retained state information,
   any current keyer activity, and any straight key activity, returns to
   silence, and closes soundcard and console devices.  This function is
   suitable for calling from an application exit handler.
*/
void cw_complete_reset(void)
{
	/* If the finalizer thinks it's pending, stop it, then temporarily
	   lock out finalizations. */
	cw_finalization_cancel_internal();
	cw_is_finalization_locked_out = true;

	cw_generator_stop();

	/* Call the reset functions for each subsystem. */
	cw_reset_tone_queue();
	cw_reset_receive();
	cw_reset_keyer();
	cw_reset_straight_key();

	cw_generator_delete_internal();
	cw_sigalrm_restore_internal();

	/* Now we can re-enable delayed finalizations. */
	cw_is_finalization_locked_out = false;

	return;
}
