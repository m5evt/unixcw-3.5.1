/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2014  Kamil Ignacak (acerion@wp.pl)

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


#include "config.h"


#include <unistd.h>
#include <stdlib.h>

#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>  /* sqrt() */



#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif





#include "libcw.h"
#include "libcw_internal.h"
#include "libcw_data.h"
#include "libcw_utils.h"
#include "libcw_gen.h"
#include "libcw_debug.h"
#include "libcw_rec.h"
#include "libcw_test.h"





extern cw_gen_t *cw_generator;
extern bool cw_is_in_sync;

extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;





/* "RS" stands for "Receiver State" */
enum {
	RS_IDLE,          /* Representation buffer is empty and ready to accept data. */
	RS_IN_TONE,       /* Mark */
	RS_AFTER_TONE,    /* Space */
	RS_END_CHAR,      /* After receiving a character, without error. */
	RS_END_WORD,      /* After receiving a word, without error. */
	RS_ERR_CHAR,      /* TODO: shouldn't it be RS_END_WORD_CHAR, as in, "After receiving a character, but with error"? */
	RS_ERR_WORD       /* TODO: shouldn't it be RS_END_WORD_ERR, as in, "After receiving a word, but with error"? */
};


static const char *cw_receiver_states[] = {
	"RS_IDLE",
	"RS_IN_TONE",
	"RS_AFTER_TONE",
	"RS_END_CHAR",
	"RS_END_WORD",
	"RS_ERR_CHAR",
	"RS_ERR_WORD"
};





cw_rec_t cw_receiver = { .state = RS_IDLE,

			 .speed = CW_SPEED_INITIAL,

			 .noise_spike_threshold = CW_REC_INITIAL_NOISE_THRESHOLD,
			 .is_adaptive_receive_enabled = CW_REC_ADAPTIVE_INITIAL,
			 .adaptive_receive_threshold = CW_REC_INITIAL_THRESHOLD,
			 .tolerance = CW_TOLERANCE_INITIAL,

			 .tone_start = { 0, 0 },
			 .tone_end =   { 0, 0 },

			 .representation_ind = 0,

			 .dot_length = 0,
			 .dash_length = 0,
			 .dot_range_minimum = 0,
			 .dot_range_maximum = 0,
			 .dash_range_minimum = 0,
			 .dash_range_maximum = 0,
			 .eoe_range_minimum = 0,
			 .eoe_range_maximum = 0,
			 .eoe_range_ideal = 0,
			 .eoc_range_minimum = 0,
			 .eoc_range_maximum = 0,
			 .eoc_range_ideal = 0,

			 .statistics_ind = 0,
			 .statistics = { {0, 0} },

			 .dot_tracking  = { {0}, 0, 0 },
			 .dash_tracking = { {0}, 0, 0 }
};





static void cw_receiver_set_adaptive_internal(cw_rec_t *rec, bool flag);
static void cw_receiver_update_adaptive_tracking_internal(cw_rec_t *rec, int mark_len_usecs, char mark);
static int  cw_receiver_add_mark_internal(cw_rec_t *rec, const struct timeval *timestamp, char mark);


static int cw_rec_mark_begin(cw_rec_t *rec, const struct timeval *timestamp);
static int cw_rec_mark_end(cw_rec_t *rec, const struct timeval *timestamp);
static int cw_rec_mark_identify_internal(cw_rec_t *rec, int mark_len_usecs, char *representation);





/* Receive tracking and statistics helpers */
static void   cw_receiver_add_statistic_internal(cw_rec_t *rec, stat_type_t type, int usecs);
static double cw_receiver_get_statistic_internal(cw_rec_t *rec, stat_type_t type);
static void   cw_reset_adaptive_average_internal(cw_tracking_t *tracking, int initial);
static void   cw_update_adaptive_average_internal(cw_tracking_t *tracking, int mark_len_usecs);
static int    cw_get_adaptive_average_internal(cw_tracking_t *tracking);





/**
   \brief Set receiving speed of receiver

   See documentation of cw_set_send_speed() for more information.

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of receive speed.
   errno is set to EINVAL if \p new_value is out of range.
   errno is set to EPERM if adaptive receive speed tracking is enabled.

   testedin::test_parameter_ranges()

   \param new_value - new value of receive speed to be assigned to receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_receive_speed(int new_value)
{
	if (cw_receiver.is_adaptive_receive_enabled) {
		errno = EPERM;
		return CW_FAILURE;
	} else {
		if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX) {
			errno = EINVAL;
			return CW_FAILURE;
		}
	}

	if (new_value != cw_receiver.speed) {
		cw_receiver.speed = new_value;

		/* Changes of receive speed require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, &cw_receiver);
	}

	return CW_SUCCESS;
}





/**
   \brief Get receiving speed from receiver

   testedin::test_parameter_ranges()

   \return current value of the receiver's receive speed
*/
int cw_get_receive_speed(void)
{
	return cw_receiver.speed;
}





/**
   \brief Set tolerance for receiver

   See libcw.h/CW_TOLERANCE_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of tolerance.
   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param new_value - new value of tolerance to be assigned to receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_tolerance(int new_value)
{
	if (new_value < CW_TOLERANCE_MIN || new_value > CW_TOLERANCE_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != cw_receiver.tolerance) {
		cw_receiver.tolerance = new_value;

		/* Changes of tolerance require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, &cw_receiver);
	}

	return CW_SUCCESS;
}





/**
   \brief Get tolerance from receiver

   testedin::test_parameter_ranges()

   \return current value of receiver's tolerance
*/
int cw_get_tolerance(void)
{
	return cw_receiver.tolerance;
}





/**
   \brief Get timing parameters for receiving, and adaptive threshold

   Return the low-level timing parameters calculated from the speed, gap,
   tolerance, and weighting set.  Parameter values are returned in
   microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   \param dot_usecs
   \param dash_usecs
   \param dot_min_usecs
   \param dot_max_usecs
   \param dash_min_usecs
   \param dash_max_usecs
   \param end_of_element_min_usecs
   \param end_of_element_max_usecs
   \param end_of_element_ideal_usecs
   \param end_of_character_min_usecs
   \param end_of_character_max_usecs
   \param end_of_character_ideal_usecs
   \param adaptive_threshold
*/
void cw_get_receive_parameters(int *dot_usecs, int *dash_usecs,
			       int *dot_min_usecs, int *dot_max_usecs,
			       int *dash_min_usecs, int *dash_max_usecs,
			       int *end_of_element_min_usecs,
			       int *end_of_element_max_usecs,
			       int *end_of_element_ideal_usecs,
			       int *end_of_character_min_usecs,
			       int *end_of_character_max_usecs,
			       int *end_of_character_ideal_usecs,
			       int *adaptive_threshold)
{
	cw_sync_parameters_internal(cw_generator, &cw_receiver);

	if (dot_usecs)      *dot_usecs = cw_receiver.dot_length;
	if (dash_usecs)     *dash_usecs = cw_receiver.dash_length;
	if (dot_min_usecs)  *dot_min_usecs = cw_receiver.dot_range_minimum;
	if (dot_max_usecs)  *dot_max_usecs = cw_receiver.dot_range_maximum;
	if (dash_min_usecs) *dash_min_usecs = cw_receiver.dash_range_minimum;
	if (dash_max_usecs) *dash_max_usecs = cw_receiver.dash_range_maximum;

	if (end_of_element_min_usecs)     *end_of_element_min_usecs = cw_receiver.eoe_range_minimum;
	if (end_of_element_max_usecs)     *end_of_element_max_usecs = cw_receiver.eoe_range_maximum;
	if (end_of_element_ideal_usecs)   *end_of_element_ideal_usecs = cw_receiver.eoe_range_ideal;
	if (end_of_character_min_usecs)   *end_of_character_min_usecs = cw_receiver.eoc_range_minimum;
	if (end_of_character_max_usecs)   *end_of_character_max_usecs = cw_receiver.eoc_range_maximum;
	if (end_of_character_ideal_usecs) *end_of_character_ideal_usecs = cw_receiver.eoc_range_ideal;

	if (adaptive_threshold) *adaptive_threshold = cw_receiver.adaptive_receive_threshold;

	return;
}





/**
   \brief Set noise spike threshold for receiver

   Set the period shorter than which, on receive, received tones are ignored.
   This allows the receive tone functions to apply noise canceling for very
   short apparent tones.
   For useful results the value should never exceed the dot length of a dot at
   maximum speed: 20000 microseconds (the dot length at 60WPM).
   Setting a noise threshold of zero turns off receive tone noise canceling.

   The default noise spike threshold is 10000 microseconds.

   errno is set to EINVAL if \p new_value is out of range.

   \param new_value - new value of noise spike threshold to be assigned to receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_noise_spike_threshold(int new_value)
{
	if (new_value < 0) {
		errno = EINVAL;
		return CW_FAILURE;
	}
	cw_receiver.noise_spike_threshold = new_value;

	return CW_SUCCESS;
}





/**
   \brief Get noise spike threshold from receiver

   See documentation of cw_set_noise_spike_threshold() for more information

   \return current value of receiver's threshold
*/
int cw_get_noise_spike_threshold(void)
{
	return cw_receiver.noise_spike_threshold;
}






/* ******************************************************************** */
/*            Section:Receive tracking and statistics helpers           */
/* ******************************************************************** */





/**
   \brief Reset tracking data structure

   Moving average function for smoothed tracking of dot and dash lengths.

   \param tracking - tracking data structure
   \param initial - initial value to be put in table of tracking data structure
*/
void cw_reset_adaptive_average_internal(cw_tracking_t *tracking, int initial)
{
	for (int i = 0; i < CW_REC_AVERAGE_ARRAY_LENGTH; i++) {
		tracking->buffer[i] = initial;
	}

	tracking->sum = initial * CW_REC_AVERAGE_ARRAY_LENGTH;
	tracking->cursor = 0;

	return;
}





/**
   \brief Add new "length of mark" value to tracking data structure

   Moving average function for smoothed tracking of dot and dash lengths.

   \param tracking - tracking data structure
   \param mark_len_usec - new "length of mark" value to add to tracking data
*/
void cw_update_adaptive_average_internal(cw_tracking_t *tracking, int mark_len_usecs)
{
	tracking->sum += mark_len_usecs - tracking->buffer[tracking->cursor];
	tracking->buffer[tracking->cursor++] = mark_len_usecs;
	tracking->cursor %= CW_REC_AVERAGE_ARRAY_LENGTH;

	return;
}





/**
   \brief Get average sum from tracking data structure

   \param tracking - tracking data structure

   \return average sum
*/
int cw_get_adaptive_average_internal(cw_tracking_t *tracking)
{
	return tracking->sum / CW_REC_AVERAGE_ARRAY_LENGTH;
}





/**
   \brief Add a mark timing to statistics

   Add a mark timing with a given statistic type to the circular
   statistics buffer.  The buffer stores only the delta from the ideal
   value; the ideal is inferred from the type passed in.

   \p type may be: STAT_DOT or STAT_DASH or STAT_END_ELEMENT or STAT_END_CHARACTER

   \param rec - receiver
   \param type - mark type
   \param usecs - timing of a mark
*/
void cw_receiver_add_statistic_internal(cw_rec_t *rec, stat_type_t type, int usecs)
{
	/* Synchronize low-level timings if required. */
	cw_sync_parameters_internal(cw_generator, rec);

	/* Calculate delta as difference between usec and the ideal value. */
	int delta = usecs - ((type == STAT_DOT) ? rec->dot_length
			     : (type == STAT_DASH) ? rec->dash_length
			     : (type == STAT_END_ELEMENT) ? rec->eoe_range_ideal
			     : (type == STAT_END_CHARACTER) ? rec->eoc_range_ideal : usecs);

	/* Add this statistic to the buffer. */
	rec->statistics[rec->statistics_ind].type = type;
	rec->statistics[rec->statistics_ind++].delta = delta;
	rec->statistics_ind %= CW_REC_STATISTICS_CAPACITY;

	return;
}





/**
   \brief Calculate and return one given timing statistic type

   \p type may be: STAT_DOT or STAT_DASH or STAT_END_ELEMENT or STAT_END_CHARACTER

   \param rec - receiver
   \param type - type of statistics

   \return 0.0 if no record of given type were found
   \return timing statistics otherwise
*/
double cw_receiver_get_statistic_internal(cw_rec_t *rec, stat_type_t type)
{
	/* Sum and count marks matching the given type.  A cleared
	   buffer always begins refilling at zeroth mark, so to optimize
	   we can stop on the first unoccupied slot in the circular buffer. */
	double sum_of_squares = 0.0;
	int count = 0;
	for (int i = 0; i < CW_REC_STATISTICS_CAPACITY; i++) {
		if (rec->statistics[i].type == type) {
			int delta = rec->statistics[i].delta;
			sum_of_squares += (double) delta * (double) delta;
			count++;
		} else if (rec->statistics[i].type == STAT_NONE) {
			break;
		}
	}

	/* Return the standard deviation, or zero if no matching mark. */
	return count > 0 ? sqrt (sum_of_squares / (double) count) : 0.0;
}





/**
   \brief Calculate and return receive timing statistics

   These statistics may be used to obtain a measure of the accuracy of
   received CW.  The values \p dot_sd and \p dash_sd contain the standard
   deviation of dot and dash lengths from the ideal values, and
   \p element_end_sd and \p character_end_sd the deviations for inter
   element and inter character spacing.  Statistics are held for all
   timings in a 256 element circular buffer.  If any statistic cannot
   be calculated, because no records for it exist, the returned value
   is 0.0.  Use NULL for the pointer argument to any statistic not required.

   \param dot_sd
   \param dash_sd
   \param element_end_sd
   \param character_end_sd
*/
void cw_get_receive_statistics(double *dot_sd, double *dash_sd,
			       double *element_end_sd, double *character_end_sd)
{
	if (dot_sd) {
		*dot_sd = cw_receiver_get_statistic_internal(&cw_receiver, STAT_DOT);
	}
	if (dash_sd) {
		*dash_sd = cw_receiver_get_statistic_internal(&cw_receiver, STAT_DASH);
	}
	if (element_end_sd) {
		*element_end_sd = cw_receiver_get_statistic_internal(&cw_receiver, STAT_END_ELEMENT);
	}
	if (character_end_sd) {
		*character_end_sd = cw_receiver_get_statistic_internal(&cw_receiver, STAT_END_CHARACTER);
	}
	return;
}





/**
   \brief Clear the receive statistics buffer

   Clear the receive statistics buffer by removing all records from it and
   returning it to its initial default state.
*/
void cw_reset_receive_statistics(void)
{
	for (int i = 0; i < CW_REC_STATISTICS_CAPACITY; i++) {
		cw_receiver.statistics[i].type = STAT_NONE;
		cw_receiver.statistics[i].delta = 0;
	}
	cw_receiver.statistics_ind = 0;

	return;
}





/* ******************************************************************** */
/*                           Section:Receiving                          */
/* ******************************************************************** */





/*
 * The CW receive functions implement the following state graph:
 *
 *        +----------------- RS_ERR_WORD <-----------------------+
 *        |(clear)                ^                              |
 *        |           (delay=long)|                              |
 *        |                       |                              |
 *        +----------------- RS_ERR_CHAR <-------------+         |
 *        |(clear)                ^  |                 |         |
 *        |                       |  +-----------------+         |(error,
 *        |                       |   (delay=short)              | delay=long)
 *        |    (error,delay=short)|                              |
 *        |                       |  +---------------------------+
 *        |                       |  |
 *        +--------------------+  |  |
 *        |             (noise)|  |  |
 *        |                    |  |  |
 *        v    (start tone)    |  |  |  (end tone,noise)
 * --> RS_IDLE ------------> RS_IN_TONE ----------------> RS_AFTER_TONE <------- +
 *     |  ^                           ^                   | |    | ^ |           |
 *     |  |                           |                   | |    | | |           |
 *     |  |          (delay=short)    +-------------------+ |    | | +-----------+
 *     |  |        +--------------+     (start tone)        |    | |   (not ready,
 *     |  |        |              |                         |    | |    buffer dot,
 *     |  |        +-------> RS_END_CHAR <------------------+    | |    buffer dash)
 *     |  |                   |   |       (delay=short)          | |
 *     |  +-------------------+   |                              | |
 *     |  |(clear)                |                              | |
 *     |  |           (delay=long)|                              | |
 *     |  |                       v                              | |
 *     |  +----------------- RS_END_WORD <-----------------------+ |
 *     |   (clear)                        (delay=long)             |(buffer dot,
 *     |                                                           | buffer dash)
 *     +-----------------------------------------------------------+
 */





/**
   \brief Set value of "adaptive receive enabled" flag for a receiver

   Set the value of the flag of \p receiver that controls whether, on
   receive, the receive functions do fixed speed receive, or track the
   speed of the received Morse code by adapting to the input stream.

   \param rec - receiver for which to set the flag
   \param flag - flag value to set
*/
void cw_receiver_set_adaptive_internal(cw_rec_t *rec, bool flag)
{
	/* Look for change of adaptive receive state. */
	if (rec->is_adaptive_receive_enabled != flag) {

		rec->is_adaptive_receive_enabled = flag;

		/* Changing the flag forces a change in low-level parameters. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, rec);

		/* If we have just switched to adaptive mode, (re-)initialize
		   the averages array to the current dot/dash lengths, so
		   that initial averages match the current speed. */
		if (rec->is_adaptive_receive_enabled) {
			cw_reset_adaptive_average_internal(&rec->dot_tracking, rec->dot_length);
			cw_reset_adaptive_average_internal(&rec->dash_tracking, rec->dash_length);
		}
	}

	return;
}





/**
   \brief Enable adaptive receive speed tracking

   If adaptive speed tracking is enabled, the receive functions will
   attempt to automatically adjust the receive speed setting to match
   the speed of the incoming Morse code. If it is disabled, the receive
   functions will use fixed speed settings, and reject incoming Morse
   which is not at the expected speed.

   Adaptive speed tracking uses a moving average of the past four elements
   as its baseline for tracking speeds.  The default state is adaptive speed
   tracking disabled.
*/
void cw_enable_adaptive_receive(void)
{
	cw_receiver_set_adaptive_internal(&cw_receiver, true);
	return;
}





/**
   \brief Disable adaptive receive speed tracking

   See documentation of cw_enable_adaptive_receive() for more information
*/
void cw_disable_adaptive_receive(void)
{
	cw_receiver_set_adaptive_internal(&cw_receiver, false);
	return;
}





/**
   \brief Get adaptive receive speed tracking flag

   The function returns state of "adaptive receive enabled" flag.
   See documentation of cw_enable_adaptive_receive() for more information

   \return true if adaptive speed tracking is enabled
   \return false otherwise
*/
bool cw_get_adaptive_receive_state(void)
{
	return cw_receiver.is_adaptive_receive_enabled;
}





/**
   \brief Mark beginning of receive tone

   Called on the start of a receive tone.  If the \p timestamp is NULL, the
   current timestamp is used as beginning of tone.

   The function should be called by client application when pressing a
   key down (closing a circuit) has been detected by client
   application.

   On error the function returns CW_FAILURE, with errno set to ERANGE if
   the call is directly after another cw_start_receive_tone() call or if
   an existing received character has not been cleared from the buffer,
   or EINVAL if the timestamp passed in is invalid.

   \param timestamp - time stamp of "key down" event

   \return CW_SUCCESS on success
   \return CW_FAILURE otherwise (with errno set)
*/
int cw_start_receive_tone(const struct timeval *timestamp)
{
	int rv = cw_rec_mark_begin(&cw_receiver, timestamp);
	return rv;
}





/* For top-level comment see cw_start_receive_tone(). */
int cw_rec_mark_begin(cw_rec_t *rec, const struct timeval *timestamp)
{
	/* If the receive state is not idle or after a tone, this is
	   a state error.  A receive tone start can only happen while
	   we are idle, or between marks of a current character. */
	if (rec->state != RS_IDLE && rec->state != RS_AFTER_TONE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Validate and save the timestamp, or get one and then save
	   it.  This is a beginning of mark.*/
	if (!cw_timestamp_validate_internal(&(rec->tone_start), timestamp)) {
		return CW_FAILURE;
	}

	/* If this function has been called while received is in "after
	   tone" state, we can measure the inter-mark gap (between
	   previous tone and this tone) by comparing the start
	   timestamp with the last end one, guaranteed set by getting
	   to the "after tone" state via cw_end_receive tone(), or in
	   extreme cases, by cw_receiver_add_element_internal().

	   Do that, then, and update the relevant statistics. */
	if (rec->state == RS_AFTER_TONE) {
		int space_len_usec = cw_timestamp_compare_internal(&(rec->tone_end),
								   &(rec->tone_start));
		cw_receiver_add_statistic_internal(&cw_receiver, STAT_END_ELEMENT, space_len_usec);

		/* TODO: this may have been a very long space. Should
		   we accept a very long space inside a character? */
	}

	/* Set state to indicate we are inside a tone. We don't know
	   yet if it will be recognized as valid tone (it may be
	   shorter than a threshold). */
	rec->state = RS_IN_TONE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[rec->state]);

	return CW_SUCCESS;
}





/**
   \brief Analyze a mark and identify it as a dot or dash

   Identify a mark (dot/dash) represented by a duration of mark.
   The duration is provided in \p mark_len_usecs.

   Identification is done using the ranges provided by the low level
   timing parameters.

   On success function returns CW_SUCCESS and sends back either a dot
   or a dash through \p representation.

   On failure it returns CW_FAILURE with errno set to ENOENT if the
   mark is not recognizable as either a dot or a dash, and sets the
   receiver state to one of the error states, depending on the length
   of mark passed in.

   Note: for adaptive timing, the mark should _always_ be
   recognized as a dot or a dash, because the ranges will have been
   set to cover 0 to INT_MAX.

   testedin::test_cw_rec_mark_identify_internal()

   \param rec - receiver
   \param mark_len_usecs - length of mark to analyze
   \param representation - variable to store identified mark (output variable)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_rec_mark_identify_internal(cw_rec_t *rec, int mark_len_usecs, /* out */ char *representation)
{
	cw_assert (representation, "Output parameter is NULL");

	/* Synchronize low level timings if required */
	cw_sync_parameters_internal(cw_generator, rec);

	/* If the timing was, within tolerance, a dot, return dot to
	   the caller.  */
	if (mark_len_usecs >= rec->dot_range_minimum
	    && mark_len_usecs <= rec->dot_range_maximum) {

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: mark '%d [us]' recognized as DOT (limits: %d - %d [us])",
			      mark_len_usecs, rec->dot_range_minimum, rec->dot_range_maximum);

		*representation = CW_DOT_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* Do the same for a dash. */
	if (mark_len_usecs >= rec->dash_range_minimum
	    && mark_len_usecs <= rec->dash_range_maximum) {

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: mark '%d [us]' recognized as DASH (limits: %d - %d [us])",
			      mark_len_usecs, rec->dash_range_minimum, rec->dash_range_maximum);

		*representation = CW_DASH_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* This mark is not a dot or a dash, so we have an error
	   case. */
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw: unrecognized mark, len = %d [us]", mark_len_usecs);
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw: dot limits: %d - %d [us]", rec->dot_range_minimum, rec->dot_range_maximum);
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw: dash limits: %d - %d [us]", rec->dash_range_minimum, rec->dash_range_maximum);

	/* We should never reach here when in adaptive timing receive
	   mode. */
	if (rec->is_adaptive_receive_enabled) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: unrecognized mark in adaptive receive");
	}



	/* TODO: making decision about current state of receiver is
	   out of scope of this function. Move the part below to
	   separate function. */

	/* If we cannot send back through \p representation any result,
	   let's move to either "in error after character" or "in
	   error after word" state, which is an "in space" state.

	   We will treat \p mark_len_usecs as length of space.

	   Depending on the length of space, we pick which of the
	   error states to move to, and move to it.  The comparison is
	   against the expected end-of-char delay.  If it's larger,
	   then fix at word error, otherwise settle on char error.

	   TODO: reconsider this for a moment: the function has been
	   called because client code has received a *mark*, not a
	   space. Are we sure that we now want to treat the
	   mark_len_usecs as length of *space*? And do we want to
	   move to either RS_ERR_WORD or RS_ERR_CHAR pretending that
	   this is a length of *space*? */
	rec->state = mark_len_usecs > rec->eoc_range_maximum
		? RS_ERR_WORD : RS_ERR_CHAR;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[rec->state]);



	/* Return ENOENT to the caller. */
	errno = ENOENT;
	return CW_FAILURE;
}





/**
   \brief Update adaptive tracking data

   Function updates the averages of dot and dash lengths, and recalculates
   the adaptive threshold for the next receive tone.

   \param rec - receiver
   \param mark_len_usecs
   \param mark
*/
void cw_receiver_update_adaptive_tracking_internal(cw_rec_t *rec, int mark_len_usecs, char mark)
{
	/* We are not going to tolerate being called in fixed speed mode. */
	if (!rec->is_adaptive_receive_enabled) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_WARNING,
			      "Called \"adaptive\" function when receiver is not in adaptive mode\n");
		return;
	}

	/* We will update the information held for either dots or dashes.
	   Which we pick depends only on what the representation of the
	   character was identified as earlier. */
	if (mark == CW_DOT_REPRESENTATION) {
		cw_update_adaptive_average_internal(&rec->dot_tracking, mark_len_usecs);
	} else if (mark == CW_DASH_REPRESENTATION) {
		cw_update_adaptive_average_internal(&rec->dash_tracking, mark_len_usecs);
	} else {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "Unknown mark %d\n", mark);
		return;
	}

	/* Recalculate the adaptive threshold from the values currently
	   held in the moving averages.  The threshold is calculated as
	   (avg dash length - avg dot length) / 2 + avg dot_length. */
	int average_dot = cw_get_adaptive_average_internal(&rec->dot_tracking);
	int average_dash = cw_get_adaptive_average_internal(&rec->dash_tracking);
	rec->adaptive_receive_threshold = (average_dash - average_dot) / 2
		+ average_dot;

	/* Resynchronize the low level timing data following recalculation.
	   If the resultant recalculated speed is outside the limits,
	   clamp the speed to the limit value and recalculate again.

	   Resetting the speed directly really means unsetting adaptive mode,
	   resyncing to calculate the new threshold, which unfortunately
	   recalculates everything else according to fixed speed; so, we
	   then have to reset adaptive and resyncing one more time, to get
	   all other timing parameters back to where they should be. */
	cw_is_in_sync = false;
	cw_sync_parameters_internal(cw_generator, rec);
	if (rec->speed < CW_SPEED_MIN || rec->speed > CW_SPEED_MAX) {
		rec->speed = rec->speed < CW_SPEED_MIN
			? CW_SPEED_MIN : CW_SPEED_MAX;
		rec->is_adaptive_receive_enabled = false;
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, rec);

		rec->is_adaptive_receive_enabled = true;
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, rec);
	}

	return;
}





/**
   \brief Mark end of tone

   The function should be called by client application when releasing
   a key (opening a circuit) has been detected by client application.

   If the \p timestamp is NULL, the current time is used as timestamp
   of end of tone.

   On success, the routine adds a dot or dash to the receiver's
   representation buffer, and returns CW_SUCCESS.

   On failure, it returns CW_FAIURE, with errno set to:
   ERANGE if the call was not preceded by a cw_start_receive_tone() call,
   EINVAL if the timestamp passed in is not valid,
   ENOENT if the tone length was out of bounds for the permissible
   dot and dash lengths and fixed speed receiving is selected,
   ENOMEM if the receiver's representation buffer is full,
   EAGAIN if the tone was shorter than the threshold for noise and was
   therefore ignored.

   \param timestamp - time stamp of "key up" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_end_receive_tone(const struct timeval *timestamp)
{
	int rv = cw_rec_mark_end(&cw_receiver, timestamp);
	return rv;
}





/* For top-level comment see cw_end_receive_tone(). */
int cw_rec_mark_end(cw_rec_t *rec, const struct timeval *timestamp)
{
	/* The receive state is expected to be inside of a mark. */
	if (rec->state != RS_IN_TONE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Take a safe copy of the current end timestamp, in case we need
	   to put it back if we decide this tone is really just noise. */
	struct timeval saved_end_timestamp = rec->tone_end;

	/* Save the timestamp passed in, or get one. */
	if (!cw_timestamp_validate_internal(&(rec->tone_end), timestamp)) {
		return CW_FAILURE;
	}

	/* Compare the timestamps to determine the length of the mark. */
	int mark_len_usecs = cw_timestamp_compare_internal(&(rec->tone_start),
							   &(rec->tone_end));


	if (rec->noise_spike_threshold > 0
	    && mark_len_usecs <= rec->noise_spike_threshold) {

		/* This pair of start()/stop() calls is just a noise,
		   ignore it.

		   Revert to state of receiver as it was before
		   complementary cw_start_receive_tone(). After call
		   to start() the state was changed to RS_IN_TONE, but
		   what state it was before call to start()?

		   Check position in representation buffer (how many
		   marks are in the buffer) to see in which state the
		   receiver was *before* start() function call, and
		   restore this state. */
		rec->state = rec->representation_ind == 0 ? RS_IDLE : RS_AFTER_TONE;

		/* Put the end tone timestamp back to how it was when we
		   came in to the routine. */
		rec->tone_end = saved_end_timestamp;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw: '%d [us]' mark identified as spike noise (threshold = '%d [us]')",
			      mark_len_usecs, rec->noise_spike_threshold);
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[rec->state]);

		errno = EAGAIN;
		return CW_FAILURE;
	}


	/* This was not a noise. At this point, we have to make a
	   decision about the mark just received.  We'll use a
	   routine that compares ranges to tell us what it thinks this
	   mark is.  If it can't decide, it will hand us back an
	   error which we return to the caller.  Otherwise, it returns
	   a mark (dot or dash), for us to buffer. */
	char representation;
	int status = cw_rec_mark_identify_internal(rec, mark_len_usecs, &representation);
	if (!status) {
		return CW_FAILURE;
	}

	/* Update the averaging buffers so that the adaptive tracking of
	   received Morse speed stays up to date.  But only do this if we
	   have set adaptive receiving; don't fiddle about trying to track
	   for fixed speed receive. */
	if (rec->is_adaptive_receive_enabled) {
		cw_receiver_update_adaptive_tracking_internal(rec, mark_len_usecs, representation);
	}

	/* Update dot and dash timing statistics.  It may seem odd to do
	   this after calling cw_receiver_update_adaptive_tracking_internal(),
	   rather than before, as this function changes the ideal values we're
	   measuring against.  But if we're on a speed change slope, the
	   adaptive tracking smoothing will cause the ideals to lag the
	   observed speeds.  So by doing this here, we can at least
	   ameliorate this effect, if not eliminate it. */
	if (representation == CW_DOT_REPRESENTATION) {
		cw_receiver_add_statistic_internal(rec, STAT_DOT, mark_len_usecs);
	} else {
		cw_receiver_add_statistic_internal(rec, STAT_DASH, mark_len_usecs);
	}

	/* Add the representation character to the receiver's buffer. */
	rec->representation[rec->representation_ind++] = representation;

	/* We just added a representation to the receive buffer.  If it's
	   full, then we have to do something, even though it's unlikely.
	   What we'll do is make a unilateral declaration that if we get
	   this far, we go to end-of-char error state automatically. */
	if (rec->representation_ind == CW_REC_REPRESENTATION_CAPACITY - 1) {
		rec->state = RS_ERR_CHAR;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: receiver's representation buffer is full");

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[rec->state]);

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* All is well.  Move to the more normal after-tone state. */
	rec->state = RS_AFTER_TONE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[rec->state]);

	return CW_SUCCESS;
}





/**
   \brief Add dot or dash to receiver's representation buffer

   Function adds a \p mark (either a dot or a dash) to the
   receiver's representation buffer.

   Since we can't add a mark to the buffer without any
   accompanying timing information, the function also accepts
   \p timestamp of the "end of mark" event.  If the \p timestamp
   is NULL, the timestamp for current time is used.

   The receiver's state is updated as if we had just received a call
   to cw_end_receive_tone().

   \param rec - receiver
   \param timestamp - timestamp of "end of mark" event
   \param mark - mark to be inserted into receiver's representation buffer

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receiver_add_mark_internal(cw_rec_t *rec, const struct timeval *timestamp, char mark)
{
	/* The receiver's state is expected to be idle or after a tone in
	   order to use this routine. */
	if (rec->state != RS_IDLE && rec->state != RS_AFTER_TONE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* This routine functions as if we have just seen a tone end,
	   yet without really seeing a tone start.

	   It doesn't matter that we don't know timestamp of start of
	   this tone: start timestamp would be needed only to
	   determine tone length and mark type (dot/dash). But
	   since the mark type has been determined by \p mark,
	   we don't need timestamp for beginning of mark.

	   What does matter is timestamp of end of this tone. This is
	   because the receiver representation routines that may be
	   called later look at the time since the last end of tone
	   to determine whether we are at the end of a word, or just
	   at the end of a character. */
	if (!cw_timestamp_validate_internal(&rec->tone_end, timestamp)) {
		return CW_FAILURE;
	}

	/* Add the mark to the receiver's representation buffer. */
	rec->representation[rec->representation_ind++] = mark;

	/* We just added a mark to the receiver's buffer.  As
	   above, if it's full, then we have to do something, even
	   though it's unlikely to actually be full. */
	if (rec->representation_ind == CW_REC_REPRESENTATION_CAPACITY - 1) {
		rec->state = RS_ERR_CHAR;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: receiver's representation buffer is full");

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[rec->state]);

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* Since we effectively just saw the end of a tone, move to
	   the after-tone state. */
	rec->state = RS_AFTER_TONE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[rec->state]);

	return CW_SUCCESS;
}





/**
   \brief Add a dot to the receiver's representation buffer

   Documentation for both cw_receive_buffer_dot() and cw_receive_buffer_dash():

   Since we can't add an element to the buffer without any
   accompanying timing information, the functions accepts \p timestamp
   of the "end of element" event.  If the \p timestamp is NULL, the
   current timestamp is used.

   These routines are for client code that has already determined
   whether a dot or dash was received by a method other than calling
   the routines cw_start_receive_tone() and cw_end_receive_tone().

   On success, the relevant element is added to the receiver's
   representation buffer.

   On failure, the routines return CW_FAILURE, with errno set to
   ERANGE if preceded by a cw_start_receive_tone call with no matching
   cw_end_receive_tone or if an error condition currently exists
   within the receiver's buffer, or ENOMEM if the receiver's
   representation buffer is full.

   \param timestamp - timestamp of "end of dot" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_buffer_dot(const struct timeval *timestamp)
{
	return cw_receiver_add_mark_internal(&cw_receiver, timestamp, CW_DOT_REPRESENTATION);
}





/**
   \brief Add a dash to the receiver's representation buffer

   See documentation of cw_receive_buffer_dot() for more information.

   \param timestamp - timestamp of "end of dash" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_buffer_dash(const struct timeval *timestamp)
{
	return cw_receiver_add_mark_internal(&cw_receiver, timestamp, CW_DASH_REPRESENTATION);
}





/**
   \brief Get the current buffered representation from the receiver's representation buffer

   On success the function fills in \p representation with the
   contents of the current representation buffer and returns
   CW_SUCCESS.

   On failure, it returns CW_FAILURE and sets errno to:
   ERANGE if not preceded by a cw_end_receive_tone call, a prior
   successful cw_receive_representation call, or a prior
   cw_receive_buffer_dot or cw_receive_buffer_dash,
   EINVAL if the timestamp passed in is invalid,
   EAGAIN if the call is made too early to determine whether a
   complete representation has yet been placed in the buffer (that is,
   less than the inter-character gap period elapsed since the last
   cw_end_receive_tone or cw_receive_buffer_dot/dash call). This is
   not a *hard* error, just an information that the caller should try
   to get the representation later.

   \p is_end_of_word indicates that the delay after the last tone
   received is longer that the inter-word gap.

   \p is_error indicates that the representation was terminated by an
   error condition.

   TODO: the function should be called cw_receiver_get_representation().

   TODO: why we pass \p timestamp to the function when all we want
   from it is a representation from representation buffer? It seems
   that the function can be called with timestamp for last event,
   i.e. on end of last space. "last space" may mean space after last
   element (dot/dash) in a character, or a space between two
   characters, or (a bit different case) a space between
   words. Timestamp for end of space would be the same timestamp as
   for beginning of new tone (?).

   testedin::test_helper_receive_tests()

   \param timestamp - timestamp of event that ends "end-of-character" space or "end-of-word" space
   \param representation - buffer for representation (output parameter)
   \param is_end_of_word - buffer for "is end of word" state (output parameter)
   \param is_error - buffer for "error" state (output parameter)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_representation(const struct timeval *timestamp,
			      /* out */ char *representation,
			      /* out */ bool *is_end_of_word, /* out */ bool *is_error)
{

	/* If the receiver's state indicates that receiver's
	   representation buffer stores a completed representation at
	   the end of word, just return the representation via \p
	   representation.

	   Repeated calls of the function when receiver is in this
	   state would simply return the same representation over and
	   over again.

	   Notice that the state of receiver at this point is settled,
	   so \p timestamp is uninteresting. We don't expect it to
	   hold any useful information that could influence state of
	   receiver or content of representation buffer. */
	if (cw_receiver.state == RS_END_WORD
	    || cw_receiver.state == RS_ERR_WORD) {

		if (is_end_of_word) {
			*is_end_of_word = true;
		}
		if (is_error) {
			*is_error = (cw_receiver.state == RS_ERR_WORD);
		}
		*representation = '\0'; /* TODO: why do this? */
		strncat(representation, cw_receiver.representation, cw_receiver.representation_ind);
		return CW_SUCCESS;
	}


	if (cw_receiver.state == RS_IDLE
	    || cw_receiver.state == RS_IN_TONE) {

		/* Not a good time to call this function. */
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Four receiver states were covered above, so we are left
	   with these three: */
	cw_assert (cw_receiver.state == RS_AFTER_TONE
		   || cw_receiver.state == RS_END_CHAR
		   || cw_receiver.state == RS_ERR_CHAR,

		   "Unknown receiver state %d", cw_receiver.state);

	/* We now know the state is after a tone, or end-of-char,
	   perhaps with error.  For all three of these cases, we're
	   going to [re-]compare the \p timestamp with the tone_end
	   timestamp saved in receiver.

	   This could mean that in the case of end-of-char, we revise
	   our opinion on later calls to end-of-word. This is correct,
	   since it models reality. */

	/* If we weren't supplied with one, get the current timestamp
	   for comparison against the tone_end timestamp saved in
	   receiver. */
	struct timeval now_timestamp;
	if (!cw_timestamp_validate_internal(&now_timestamp, timestamp)) {
		return CW_FAILURE;
	}

	/* Now we need to compare the timestamps to determine the length
	   of the inter-tone gap. */
	int space_len_usecs = cw_timestamp_compare_internal(&cw_receiver.tone_end,
							    &now_timestamp);

	if (space_len_usecs == INT_MAX) {
		// fprintf(stderr, "space len == INT_MAX\n");
		errno = EAGAIN;
		return CW_FAILURE;
	}

	/* Synchronize low level timings if required */
	cw_sync_parameters_internal(cw_generator, &cw_receiver);


	if (space_len_usecs >= cw_receiver.eoc_range_minimum
	    && space_len_usecs <= cw_receiver.eoc_range_maximum) {

		/* The space is, within tolerance, a character
		   space. A representation of complete character is
		   now in representation buffer, we can return the
		   representation via parameter. */

		if (cw_receiver.state == RS_AFTER_TONE) {
			/* A character space after a tone means end of
			   character. Update receiver state. On
			   updating the state, update timing
			   statistics for an identified end of
			   character as well. */
			cw_receiver_add_statistic_internal(&cw_receiver, STAT_END_CHARACTER, space_len_usecs);
			cw_receiver.state = RS_END_CHAR;
		} else {
			/* We are already in RS_END_CHAR or
			   RS_ERR_CHAR, so nothing to do. */
		}

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

		/* Return the representation from receiver's buffer. */
		if (is_end_of_word) {
			*is_end_of_word = false;
		}
		if (is_error) {
			*is_error = (cw_receiver.state == RS_ERR_CHAR);
		}
		*representation = '\0'; /* TODO: why do this? */
		strncat(representation, cw_receiver.representation, cw_receiver.representation_ind);
		return CW_SUCCESS;
	}

	/* If the length of space indicated a word space, again we
	   have a complete representation and can return it.  In this
	   case, we also need to inform the client that this looked
	   like the end of a word, not just a character.

	   Any space length longer than eoc_range_maximum is, almost
	   by definition, an "end of word" space. */
	if (space_len_usecs > cw_receiver.eoc_range_maximum) {

		/* The space is a word space. Update receiver state,
		   remember to preserve error state (if any). */
		cw_receiver.state = cw_receiver.state == RS_ERR_CHAR
			? RS_ERR_WORD : RS_END_WORD;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

		/* Return the representation from receiver's buffer. */
		if (is_end_of_word) {
			*is_end_of_word = true;
		}
		if (is_error) {
			*is_error = (cw_receiver.state == RS_ERR_WORD);
		}
		*representation = '\0'; /* TODO: why do this? */
		strncat(representation, cw_receiver.representation, cw_receiver.representation_ind);
		return CW_SUCCESS;
	}

	/* The space - judging by \p timestamp - is neither an
	   inter-character space, nor inter-word space. If none of
	   these conditions holds, then we cannot *yet* make a
	   judgement on what we have in the buffer, so return
	   EAGAIN. */
	errno = EAGAIN;
	return CW_FAILURE;
}





/**
   \brief Get a current character

   Function returns the character currently stored in receiver's
   representation buffer.

   On success the function returns CW_SUCCESS, and fills \p c with the
   contents of the current representation buffer, translated into a
   character.

   On failure the function returns CW_FAILURE, with errno set to:

   ERANGE if not preceded by a cw_end_receive_tone() call, a prior
   successful cw_receive_character() call, or a
   cw_receive_buffer_dot() or cw_receive_buffer_dash() call,
   EINVAL if the timestamp passed in is invalid, or
   EAGAIN if the call is made too early to determine whether a
   complete character has yet been placed in the buffer (that is, less
   than the inter-character gap period elapsed since the last
   cw_end_receive_tone() or cw_receive_buffer_dot/dash call).
   ENOENT if character stored in receiver cannot be recognized as valid

   \p is_end_of_word indicates that the delay after the last tone
   received is longer that the inter-word gap.

   \p is_error indicates that the character was terminated by an error
   condition.

   testedin::test_helper_receive_tests()

   \param timestamp - timestamp of event that ends "end-of-character" space or "end-of-word" space
   \param c - buffer for character (output parameter)
   \param is_end_of_word - buffer for "is end of word" state (output parameter)
   \param is_error - buffer for "error" state (output parameter)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_character(const struct timeval *timestamp,
			 /* out */ char *c,
			 /* out */ bool *is_end_of_word, /* out */ bool *is_error)
{
	bool end_of_word, error;
	char representation[CW_REC_REPRESENTATION_CAPACITY + 1];

	/* See if we can obtain a representation from receiver. */
	int status = cw_receive_representation(timestamp, representation,
					       &end_of_word, &error);
	if (!status) {
		return CW_FAILURE;
	}

	/* Look up the representation using the lookup functions. */
	char character = cw_representation_to_character_internal(representation);
	if (!character) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	/* If we got this far, all is well, so return what we received. */
	if (c) {
		*c = character;
	}
	if (is_end_of_word) {
		*is_end_of_word = end_of_word;
	}
	if (is_error) {
		*is_error = error;
	}
	return CW_SUCCESS;
}





/**
   \brief Clear receiver's representation buffer

   Clears the receiver's representation buffer, resets receiver's
   internal state. This prepares the receiver to receive tones again.

   This routine must be called after successful, or terminating,
   cw_receive_representation() or cw_receive_character() calls, to
   clear the states and prepare the buffer to receive more tones.
*/
void cw_clear_receive_buffer(void)
{
	cw_receiver.representation_ind = 0;
	cw_receiver.state = RS_IDLE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

	return;
}





/**
   \brief Get the number of elements (dots/dashes) the receiver's buffer can accommodate

   The maximum number of elements written out by cw_receive_representation()
   is the capacity + 1, the extra character being used for the terminating
   NUL.

   \return number of elements that can be stored in receiver's representation buffer
*/
int cw_get_receive_buffer_capacity(void)
{
	return CW_REC_REPRESENTATION_CAPACITY;
}





/**
   \brief Get the number of elements (dots/dashes) currently pending in the cw_receiver's representation buffer

   testedin::test_helper_receive_tests()

   \return number of elements in receiver's representation buffer
*/
int cw_get_receive_buffer_length(void)
{
	return cw_receiver.representation_ind;
}





/**
   \brief Clear receive data

   Clear the receiver's representation buffer, statistics, and any
   retained receiver's state.  This function is suitable for calling
   from an application exit handler.
*/
void cw_reset_receive(void)
{
	cw_receiver.representation_ind = 0;
	cw_receiver.state = RS_IDLE;

	cw_reset_receive_statistics();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s (reset)", cw_receiver_states[cw_receiver.state]);

	return;
}





#ifdef LIBCW_UNIT_TESTS





/**
   tests::cw_rec_mark_identify_internal()
*/
unsigned int test_cw_rec_mark_identify_internal(void)
{
	int p = fprintf(stderr, "libcw: cw_rec_mark_identify_internal():");

	cw_disable_adaptive_receive();

	cw_generator_new(CW_AUDIO_NULL, "null");

	int speed_step = CW_SPEED_MAX - CW_SPEED_MIN;

	for (int i = CW_SPEED_MIN; i < CW_SPEED_MAX; i += speed_step)
	{
		cw_set_receive_speed(i);

		char representation;
		int rv;


		/* Test marks of length within allowed lengths of dots. */
		int len_step = (cw_receiver.dot_range_maximum - cw_receiver.dot_range_minimum) / 10;
		for (int j = cw_receiver.dot_range_minimum; j < cw_receiver.dot_range_maximum; j += len_step) {
			rv = cw_rec_mark_identify_internal(&cw_receiver, j, &representation);
			cw_assert (rv, "failed to identify dot for speed = %d [wpm], len = %d [us]", i, j);

			cw_assert (representation == CW_DOT_REPRESENTATION, "got something else than dot for speed = %d [wpm], len = %d [us]", i, j);
		}

		/* Test mark shorter than minimal length of dot. */
		rv = cw_rec_mark_identify_internal(&cw_receiver, cw_receiver.dot_range_minimum - 1, &representation);
		cw_assert (!rv, "incorrectly identified short mark as a dot for speed = %d [wpm]", i);

		/* Test mark longer than maximal length of dot (but shorter than minimal length of dash). */
		rv = cw_rec_mark_identify_internal(&cw_receiver, cw_receiver.dot_range_maximum + 1, &representation);
		cw_assert (!rv, "incorrectly identified long mark as a dot for speed = %d [wpm]", i);




		/* Test marks of length within allowed lengths of dashes. */
		len_step = (cw_receiver.dash_range_maximum - cw_receiver.dash_range_minimum) / 10;
		for (int j = cw_receiver.dash_range_minimum; j < cw_receiver.dash_range_maximum; j += len_step) {
			rv = cw_rec_mark_identify_internal(&cw_receiver, j, &representation);
			cw_assert (rv, "failed to identify dash for speed = %d [wpm], len = %d [us]", i, j);

			cw_assert (representation == CW_DASH_REPRESENTATION, "got something else than dash for speed = %d [wpm], len = %d [us]", i, j);
		}

		/* Test mark shorter than minimal length of dash (but longer than maximal length of dot). */
		rv = cw_rec_mark_identify_internal(&cw_receiver, cw_receiver.dash_range_minimum - 1, &representation);
		cw_assert (!rv, "incorrectly identified short mark as a dash for speed = %d [wpm]", i);

		/* Test mark longer than maximal length of dash. */
		rv = cw_rec_mark_identify_internal(&cw_receiver, cw_receiver.dash_range_maximum + 1, &representation);
		cw_assert (!rv, "incorrectly identified long mark as a dash for speed = %d [wpm]", i);
	}



	cw_generator_delete();

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}


#endif /* #ifdef LIBCW_UNIT_TESTS */
