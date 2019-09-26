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
   \file libcw_rec.c

   \brief Receiver. Receive a series of marks and spaces. Interpret
   them as characters.


   There are two ways of feeding marks and spaces to receiver.

   First of them is to notify receiver about "begin of mark" and "end
   of mark" events. Receiver then tries to figure out how long a mark
   or space is, what type of mark (dot/dash) or space (inter-mark,
   inter-character, inter-word) it is, and when a full character has
   been received.

   This is done with cw_rec_mark_begin() and cw_rec_mark_end()
   functions.

   The second method is to inform receiver not about start and stop of
   marks (dots/dashes), but about full marks themselves.  This is done
   with cw_rec_add_mark(): a function that is one level of abstraction
   above functions from first method.


   Currently there is only one method of passing received data
   (characters) from receiver to client code. This is done by client
   code cyclically polling the receiver with
   cw_rec_poll_representation() or cw_rec_poll_character() (which
   itself is built on top of cw_rec_poll_representation()).


   Duration (length) of marks, spaces and few other things is in
   microseconds [us], unless specified otherwise.
*/




#include "config.h"


#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h>  /* sqrt(), cosf() */
#include <limits.h> /* INT_MAX, for clang. */


#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include <sys/time.h> /* struct timeval */




#include "libcw.h"
#include "libcw_rec.h"
#include "libcw_rec_internal.h"
#include "libcw_key.h"
#include "libcw_data.h"
#include "libcw_utils.h"
#include "libcw_debug.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/rec: "




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




/* See also enum of int values, declared in libcw_rec.h. */
static const char *cw_receiver_states[] = {
	"RS_IDLE",
	"RS_MARK",
	"RS_IMARK_SPACE",
	"RS_EOC_GAP",
	"RS_EOW_GAP",
	"RS_EOC_GAP_ERR",
	"RS_EOW_GAP_ERR"
};




/* Functions handling averaging data structure in adaptive receiving
   mode. */
static void cw_rec_update_average_internal(cw_rec_averaging_t * avg, int mark_len);
static void cw_rec_update_averages_internal(cw_rec_t * rec, int mark_len, char mark);
static void cw_rec_reset_average_internal(cw_rec_averaging_t * avg, int initial);




/**
   \brief Allocate and initialize new receiver variable

   Before returning, the function calls
   cw_rec_sync_parameters_internal() for the receiver.

   Function may return NULL on malloc() failure.

   \return freshly allocated, initialized and synchronized receiver on success
   \return NULL pointer on failure
*/
cw_rec_t * cw_rec_new(void)
{
	cw_rec_t *rec = (cw_rec_t *) malloc(sizeof (cw_rec_t));
	if (!rec) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      MSG_PREFIX "new: malloc()");
		return (cw_rec_t *) NULL;
	}

	memset(rec, 0, sizeof (cw_rec_t));

	rec->state = RS_IDLE;

	rec->speed                      = CW_SPEED_INITIAL;
	rec->tolerance                  = CW_TOLERANCE_INITIAL;
	rec->gap                        = CW_GAP_INITIAL;
	rec->is_adaptive_receive_mode   = CW_REC_ADAPTIVE_MODE_INITIAL;
	rec->noise_spike_threshold      = CW_REC_NOISE_THRESHOLD_INITIAL;

	/* TODO: this variable is not set in
	   cw_rec_reset_parameters_internal().  Why is it separated
	   from the four main variables? Is it because it is a
	   derivative of speed? But speed is a derivative of this
	   variable in adaptive speed mode. */
	rec->adaptive_speed_threshold = CW_REC_SPEED_THRESHOLD_INITIAL;


	rec->mark_start.tv_sec = 0;
	rec->mark_start.tv_usec = 0;

	rec->mark_end.tv_sec = 0;
	rec->mark_end.tv_usec = 0;

	memset(rec->representation, 0, sizeof (rec->representation));
	rec->representation_ind = 0;


	rec->dot_len_ideal = 0;
	rec->dot_len_min = 0;
	rec->dot_len_max = 0;

	rec->dash_len_ideal = 0;
	rec->dash_len_min = 0;
	rec->dash_len_max = 0;

	rec->eom_len_ideal = 0;
	rec->eom_len_min = 0;
	rec->eom_len_max = 0;

	rec->eoc_len_ideal = 0;
	rec->eoc_len_min = 0;
	rec->eoc_len_max = 0;

	rec->additional_delay = 0;
	rec->adjustment_delay = 0;


	rec->parameters_in_sync = false;


	rec->statistics[0].type = 0;
	rec->statistics[0].delta = 0;
	rec->statistics_ind = 0;


	rec->dot_averaging.cursor = 0;
	rec->dot_averaging.sum = 0;
	rec->dot_averaging.average = 0;

	rec->dash_averaging.cursor = 0;
	rec->dash_averaging.sum = 0;
	rec->dash_averaging.average = 0;


	cw_rec_sync_parameters_internal(rec);

#ifdef WITH_EXPERIMENTAL_RECEIVER
	rec->push_callback = NULL;
#endif

	return rec;
}




/**
   \brief Delete a generator

   Deallocate all memory and free all resources associated with given
   receiver.

   \reviewed on 2017-02-02

   \param rec - pointer to receiver
*/
void cw_rec_delete(cw_rec_t ** rec)
{
	cw_assert (rec, MSG_PREFIX "delete: 'rec' argument can't be NULL\n");

	if (!*rec) {
		return;
	}

	free(*rec);
	*rec = (cw_rec_t *) NULL;

	return;
}




/**
   \brief Set receiver's receiving speed

   See documentation of cw_set_send_speed() for more information.

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of receive speed.

   \errno EINVAL - \p new_value is out of range.
   \errno EPERM - adaptive receive speed tracking is enabled.

   Notice that internally the speed is saved as float.

   \param rec - receiver
   \param new_value - new value of receive speed to be set in receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_rec_set_speed(cw_rec_t * rec, int new_value)
{
	if (rec->is_adaptive_receive_mode) {
		errno = EPERM;
		return CW_FAILURE;
	}

	if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* TODO: verify this comparison. */
	float diff = abs((1.0 * new_value) - rec->speed);
	if (diff >= 0.5) {
		rec->speed = new_value;

		/* Changes of receive speed require resynchronization. */
		rec->parameters_in_sync = false;
		cw_rec_sync_parameters_internal(rec);
	}

	return CW_SUCCESS;
}




/**
   \brief Get receiver's speed

   \reviewed on 2017-02-02

   \param rec - receiver

   \return current value of receiver's speed
*/
float cw_rec_get_speed(const cw_rec_t * rec)
{
	return rec->speed;
}




/**
   \brief Set receiver's tolerance

   See libcw.h/CW_TOLERANCE_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of tolerance.

   \errno EINVAL - \p new_value is out of range.

   \param rec - receiver
   \param new_value - new value of tolerance to be set in receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_rec_set_tolerance(cw_rec_t * rec, int new_value)
{
	if (new_value < CW_TOLERANCE_MIN || new_value > CW_TOLERANCE_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != rec->tolerance) {
		rec->tolerance = new_value;

		/* Changes of tolerance require resynchronization. */
		rec->parameters_in_sync = false;
		cw_rec_sync_parameters_internal(rec);
	}

	return CW_SUCCESS;
}




/**
   \brief Get receiver's tolerance

   \reviewed on 2017-02-02

   \param rec - receiver

   \return current value of receiver's tolerance
*/
int cw_rec_get_tolerance(const cw_rec_t * rec)
{
	return rec->tolerance;
}




/**
   \brief Get receiver's timing parameters and adaptive threshold

   Return the low-level timing parameters calculated from the speed,
   gap, tolerance, and weighting set.  Units of returned parameter
   values are microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   TODO: reconsider order of these function arguments.

   \reviewed on 2017-02-02

   \param *rec
   \param dot_len_ideal
   \param dash_len_ideal
   \param dot_len_min
   \param dot_len_max
   \param dash_len_min
   \param dash_len_max
   \param eom_len_min
   \param eom_len_max
   \param eom_len_ideal
   \param eoc_len_min
   \param eoc_len_max
   \param eoc_len_ideal
*/
void cw_rec_get_parameters_internal(cw_rec_t *rec,
				    int *dot_len_ideal, int *dash_len_ideal,
				    int *dot_len_min,   int *dot_len_max,
				    int *dash_len_min,  int *dash_len_max,
				    int *eom_len_min,
				    int *eom_len_max,
				    int *eom_len_ideal,
				    int *eoc_len_min,
				    int *eoc_len_max,
				    int *eoc_len_ideal,
				    int *adaptive_threshold)
{
	cw_rec_sync_parameters_internal(rec);

	/* Dot mark. */
	if (dot_len_min)   *dot_len_min   = rec->dot_len_min;
	if (dot_len_max)   *dot_len_max   = rec->dot_len_max;
	if (dot_len_ideal) *dot_len_ideal = rec->dot_len_ideal;

	/* Dash mark. */
	if (dash_len_min)   *dash_len_min   = rec->dash_len_min;
	if (dash_len_max)   *dash_len_max   = rec->dash_len_max;
	if (dash_len_ideal) *dash_len_ideal = rec->dash_len_ideal;

	/* End-of-mark. */
	if (eom_len_min)   *eom_len_min   = rec->eom_len_min;
	if (eom_len_max)   *eom_len_max   = rec->eom_len_max;
	if (eom_len_ideal) *eom_len_ideal = rec->eom_len_ideal;

	/* End-of-character. */
	if (eoc_len_min)   *eoc_len_min   = rec->eoc_len_min;
	if (eoc_len_max)   *eoc_len_max   = rec->eoc_len_max;
	if (eoc_len_ideal) *eoc_len_ideal = rec->eoc_len_ideal;

	if (adaptive_threshold) *adaptive_threshold = rec->adaptive_speed_threshold;

	return;
}




/**
   \brief Set receiver's noise spike threshold

   Set the period shorter than which, on receive, received marks are ignored.
   This allows the "receive mark" functions to apply noise canceling for very
   short apparent marks.
   For useful results the value should never exceed the dot length of a dot at
   maximum speed: 20000 microseconds (the dot length at 60WPM).
   Setting a noise threshold of zero turns off receive mark noise canceling.

   The default noise spike threshold is 10000 microseconds.

   \errno EINVAL - \p new_value is out of range.

   \param rec - receiver
   \param new_value - new value of noise spike threshold to be set in receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_rec_set_noise_spike_threshold(cw_rec_t * rec, int new_value)
{
	if (new_value < 0) {
		errno = EINVAL;
		return CW_FAILURE;
	}
	rec->noise_spike_threshold = new_value;

	return CW_SUCCESS;
}




/**
   \brief Get receiver's noise spike threshold

   See documentation of cw_set_noise_spike_threshold() for more information

   \reviewed on 2017-02-02

   \param rec - receiver

   \return current value of receiver's threshold
*/
int cw_rec_get_noise_spike_threshold(const cw_rec_t * rec)
{
	return rec->noise_spike_threshold;
}





/* TODO: this function probably should have its old-style version in
   libcw.h as well. */
/**
   \brief Set receiver's gap

   TODO: this function probably should have its old-style version in
   libcw.h as well.

   \errno EINVAL - \p new_value is out of range.

   \reviewed on 2017-02-02

   \param rec - receiver
   \param new_value - new value of gap to be set in receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_rec_set_gap(cw_rec_t * rec, int new_value)
{
	if (new_value < CW_GAP_MIN || new_value > CW_GAP_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != rec->gap) {
		rec->gap = new_value;

		/* Changes of gap require resynchronization. */
		rec->parameters_in_sync = false;
		cw_rec_sync_parameters_internal(rec);
	}

	return CW_SUCCESS;
}





/* Functions handling average lengths of dot and dashes in adaptive
   receiving mode. */





/**
   \brief Reset averaging data structure

   Reset averaging data structure to initial state.
   To be used in adaptive receiving mode.

   \reviewed on 2017-02-02

   \param avg - averaging data structure (for Dot or for Dash)
   \param initial - initial value to be put in table of averaging data structure
*/
void cw_rec_reset_average_internal(cw_rec_averaging_t *avg, int initial)
{
	for (int i = 0; i < CW_REC_AVERAGING_ARRAY_LENGTH; i++) {
		avg->buffer[i] = initial;
	}

	avg->sum = initial * CW_REC_AVERAGING_ARRAY_LENGTH;
	avg->cursor = 0;

	return;
}




/**
   \brief Update value of average "length of mark"

   To be used in adaptive receiving mode.

   Update table of values used to calculate averaged "length of
   mark". The averaged length of a mark is calculated with moving
   average function.

   The new \p mark_len is added to \p avg, and the oldest is
   discarded. New averaged sum is calculated using updated data.

   \reviewed on 2017-02-02

   \param avg - averaging data structure (for Dot or for Dash)
   \param mark_len - new "length of mark" value to be added to averaging data \p avg
*/
void cw_rec_update_average_internal(cw_rec_averaging_t *avg, int mark_len)
{
	/* Oldest mark length goes out, new goes in. */
	avg->sum -= avg->buffer[avg->cursor];
	avg->sum += mark_len;

	avg->average = avg->sum / CW_REC_AVERAGING_ARRAY_LENGTH;

	avg->buffer[avg->cursor++] = mark_len;
	avg->cursor %= CW_REC_AVERAGING_ARRAY_LENGTH;

	return;
}





/* Functions handling receiver statistics. */





/**
   \brief Add a mark or space length to statistics

   Add a mark or space length \p len (type of mark or space is
   indicated by \p type) to receiver's circular statistics buffer.
   The buffer stores only the delta from the ideal value; the ideal is
   inferred from the type \p type passed in.


   \param rec - receiver
   \param type - type of statistics: CW_REC_STAT_DOT or CW_REC_STAT_DASH or CW_REC_STAT_IMARK_SPACE or CW_REC_STAT_ICHAR_SPACE
   \param len - length of a mark or space
*/
void cw_rec_update_stats_internal(cw_rec_t *rec, stat_type_t type, int len)
{
	/* Synchronize parameters if required. */
	cw_rec_sync_parameters_internal(rec);

	/* Calculate delta as difference between given length (len)
	   and the ideal length value. */
	int delta = len - ((type == CW_REC_STAT_DOT)           ? rec->dot_len_ideal
			   : (type == CW_REC_STAT_DASH)        ? rec->dash_len_ideal
			   : (type == CW_REC_STAT_IMARK_SPACE) ? rec->eom_len_ideal
			   : (type == CW_REC_STAT_ICHAR_SPACE) ? rec->eoc_len_ideal
			   : len);

	/* Add this statistic to the buffer. */
	rec->statistics[rec->statistics_ind].type = type;
	rec->statistics[rec->statistics_ind].delta = delta;

	rec->statistics_ind++;
	rec->statistics_ind %= CW_REC_STATISTICS_CAPACITY;

	return;
}




/**
   \brief Calculate and return length statistics for given type of mark or space

   \param rec - receiver
   \param type - type of statistics: CW_REC_STAT_DOT or CW_REC_STAT_DASH or CW_REC_STAT_IMARK_SPACE or CW_REC_STAT_ICHAR_SPACE

   \return 0.0 if no record of given type were found
   \return statistics of length otherwise
*/
double cw_rec_get_stats_internal(cw_rec_t *rec, stat_type_t type)
{
	/* Sum and count values for marks/spaces matching the given
	   type.  A cleared buffer always begins refilling at zeroth
	   mark, so to optimize we can stop on the first unoccupied
	   slot in the circular buffer. */
	double sum_of_squares = 0.0;
	int count = 0;
	for (int i = 0; i < CW_REC_STATISTICS_CAPACITY; i++) {
		if (rec->statistics[i].type == type) {
			int delta = rec->statistics[i].delta;
			sum_of_squares += (double) delta * (double) delta;
			count++;
		} else if (rec->statistics[i].type == CW_REC_STAT_NONE) {
			break;
		} else {
			; /* A type of statistics that we are not interested in. Continue. */
		}
	}

	/* Return the standard deviation, or zero if no matching mark. */
	return count > 0 ? sqrt (sum_of_squares / (double) count) : 0.0;
}




/**
   \brief Calculate and return receiver's timing statistics

   These statistics may be used to obtain a measure of the accuracy of
   received Morse code.

   The values \p dot_sd and \p dash_sd contain the standard deviation
   of dot and dash lengths from the ideal values, and \p
   element_end_sd and \p character_end_sd the deviations for inter
   element and inter character spacing.

   Statistics are held for all timings in a 256 element circular
   buffer.  If any statistic cannot be calculated, because no records
   for it exist, the returned value is 0.0.  Use NULL for the pointer
   argument to any statistic not required.

   \reviewed on 2017-02-02

   \param rec - receiver
   \param dot_sd
   \param dash_sd
   \param element_end_sd
   \param character_end_sd
*/
void cw_rec_get_statistics_internal(cw_rec_t *rec, double *dot_sd, double *dash_sd,
				    double *element_end_sd, double *character_end_sd)
{
	if (dot_sd) {
		*dot_sd = cw_rec_get_stats_internal(rec, CW_REC_STAT_DOT);
	}
	if (dash_sd) {
		*dash_sd = cw_rec_get_stats_internal(rec, CW_REC_STAT_DASH);
	}
	if (element_end_sd) {
		*element_end_sd = cw_rec_get_stats_internal(rec, CW_REC_STAT_IMARK_SPACE);
	}
	if (character_end_sd) {
		*character_end_sd = cw_rec_get_stats_internal(rec, CW_REC_STAT_ICHAR_SPACE);
	}
	return;
}




/**
   \brief Clear the receive statistics buffer

   Function handling receiver statistics.

   Clear the receive statistics buffer by removing all records from it and
   returning it to its initial default state.

   \reviewed on 2017-02-02

   \param rec - receiver
*/
void cw_rec_reset_statistics(cw_rec_t * rec)
{
	for (int i = 0; i < CW_REC_STATISTICS_CAPACITY; i++) {
		rec->statistics[i].type = CW_REC_STAT_NONE;
		rec->statistics[i].delta = 0;
	}
	rec->statistics_ind = 0;

	return;
}




/* ******************************************************************** */
/*                           Section:Receiving                          */
/* ******************************************************************** */




/*
 * The CW receive functions implement the following state graph:
 *
 *        +-----------<------- RS_EOW_GAP_ERR ------------<--------------+
 *        |(clear)                    ^                                  |
 *        |                (pull() +  |                                  |
 *        |       space len > eoc len)|                                  |
 *        |                           |                                  |
 *        +-----------<-------- RS_EOC_GAP_ERR <---------------+         |
 *        |(clear)                    ^  |                     |         |
 *        |                           |  +---------------------+         |(error,
 *        |                           |    (pull() +                     |space len > eoc len)
 *        |                           |    space len = eoc len)          |
 *        v                    (error,|                                  |
 *        |       space len = eoc len)|  +------------->-----------------+
 *        |                           |  |
 *        +-----------<------------+  |  |
 *        |                        |  |  |
 *        |              (is noise)|  |  |
 *        |                        |  |  |
 *        v        (begin mark)    |  |  |    (end mark,noise)
 * --> RS_IDLE ------->----------- RS_MARK ------------>-------> RS_IMARK_SPACE <---------- +
 *     v  ^                              ^                          v v v ^ |               |
 *     |  |                              |    (begin mark)          | | | | |               |
 *     |  |     (pull() +                +-------------<------------+ | | | +---------------+
 *     |  |     space len = eoc len)                                  | | |      (not ready,
 *     |  |     +-----<------------+          (pull() +               | | |      buffer dot,
 *     |  |     |                  |          space len = eoc len)    | | |      buffer dash)
 *     |  |     +-----------> RS_EOC_GAP <-------------<--------------+ | |
 *     |  |                     |  |                                    | |
 *     |  |(clear)              |  |                                    | |
 *     |  +-----------<---------+  |                                    | |
 *     |  |                        |                                    | |
 *     |  |              (pull() + |                                    | |
 *     |  |    space len > eoc len)|                                    | |
 *     |  |                        |          (pull() +                 | |
 *     |  |(clear)                 v          space len > eoc len)      | |
 *     |  +-----------<------ RS_EOW_GAP <-------------<----------------+ |
 *     |                                                                  |
 *     |                                                                  |
 *     |               (buffer dot,                                       |
 *     |               buffer dash)                                       |
 *     +------------------------------->----------------------------------+
 */




#define CW_REC_SET_STATE(m_rec, m_new_state, m_debug_object)		\
	{								\
		cw_debug_msg ((m_debug_object),				\
			      CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,	\
			      MSG_PREFIX "state: %s -> %s @ %s:%d",	\
			      cw_receiver_states[m_rec->state], cw_receiver_states[m_new_state], __func__, __LINE__); \
		m_rec->state = m_new_state;				\
	}




/**
   \brief Enable or disable receiver's "adaptive receiving" mode

   Set the mode of a receiver \p rec to fixed or adaptive receiving
   mode.

   In adaptive receiving mode the receiver tracks the speed of the
   received Morse code by adapting to the input stream.

   \reviewed on 2017-02-04

   \param rec - receiver for which to set the mode
   \param adaptive - value of receiver's "adaptive mode" to be set
*/
void cw_rec_set_adaptive_mode_internal(cw_rec_t *rec, bool adaptive)
{
	/* Look for change of adaptive receive state. */
	if (rec->is_adaptive_receive_mode != adaptive) {

		rec->is_adaptive_receive_mode = adaptive;

		/* Changing the flag forces a change in low-level parameters. */
		rec->parameters_in_sync = false;
		cw_rec_sync_parameters_internal(rec);

		/* If we have just switched to adaptive mode, (re-)initialize
		   the averages array to the current Dot/Dash lengths, so
		   that initial averages match the current speed. */
		if (rec->is_adaptive_receive_mode) {
			cw_rec_reset_average_internal(&rec->dot_averaging, rec->dot_len_ideal);
			cw_rec_reset_average_internal(&rec->dash_averaging, rec->dash_len_ideal);
		}
	}

	return;
}




/**
   \brief Enable receiver's "adaptive receiving" mode

   In adaptive receiving mode the receiver tracks the speed of the
   received Morse code by adapting to the input stream.

   \reviewed on 2017-02-04

   \param rec - receiver for which to enable the mode
*/
void cw_rec_enable_adaptive_mode(cw_rec_t * rec)
{
	cw_rec_set_adaptive_mode_internal(rec, true);
	return;
}




/**
   \brief Disable receiver's "adaptive receiving" mode

   \reviewed on 2017-02-04

   \param rec - receiver for which to disable the mode
*/
void cw_rec_disable_adaptive_mode(cw_rec_t * rec)
{
	cw_rec_set_adaptive_mode_internal(rec, false);
	return;
}




/**
   \brief Get adaptive receive speed tracking flag

   The function returns state of "adaptive receive enabled" flag.
   See documentation of cw_enable_adaptive_receive() for more information.

   \reviewed on 2017-02-04

   \return true if adaptive speed tracking is enabled
   \return false otherwise
*/
bool cw_rec_get_adaptive_mode(const cw_rec_t * rec)
{
	return rec->is_adaptive_receive_mode;
}




/**
   \errno ERANGE - invalid state of receiver was discovered.
   \errno EINVAL - errors while processing or getting \p timestamp

   \reviewed on 2017-02-04

   \param rec - receiver
   \param timestamp - timestamp of "beginning of mark" event. May be NULL, then current time will be used.

   \return CW_SUCCESS when no errors occurred
   \return CW_FAILURE otherwise

*/
int cw_rec_mark_begin(cw_rec_t * rec, const volatile struct timeval * timestamp)
{
	if (rec->is_pending_inter_word_space) {

		/* Beginning of mark in this situation means that
		   we're seeing the next incoming character within the
		   same word, so no inter-word space will be received
		   at this point in time. The space that we were
		   observing/waiting for, was just inter-character
		   space.

		   Reset state of rec and cancel the waiting for
		   inter-word space. */
		cw_rec_reset_state(rec);
	}

	if (rec->state != RS_IDLE && rec->state != RS_IMARK_SPACE) {
		/* A start of mark can only happen while we are idle,
		   or in inter-mark-space of a current character. */

		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "mark_begin: receive state not idle and not inter-mark-space: %s", cw_receiver_states[rec->state]);

		/*
		  ->state should be RS_IDLE at the beginning of new character;
		  ->state should be RS_IMARK_SPACE in the middle of character (between marks).
		*/

		errno = ERANGE;
		return CW_FAILURE;
	}
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      MSG_PREFIX "mark_begin: receive state: %s", cw_receiver_states[rec->state]);

	/* Validate and save the timestamp, or get one and then save
	   it.  This is a beginning of mark. */
	if (!cw_timestamp_validate_internal(&rec->mark_start, timestamp)) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (rec->state == RS_IMARK_SPACE) {
		/* Measure inter-mark space (just for statistics).

		   rec->mark_end is timestamp of end of previous
		   mark. It is set when receiver goes into
		   inter-mark space state by cw_end_receive tone() or
		   by cw_rec_add_mark(). */
		int space_len = cw_timestamp_compare_internal(&rec->mark_end,
							      &rec->mark_start);
		cw_rec_update_stats_internal(rec, CW_REC_STAT_IMARK_SPACE, space_len);

		/* TODO: this may have been a very long space. Should
		   we accept a very long space inside a character? */
	}

	/* Set state to indicate we are inside a mark. We don't know
	   yet if it will be recognized as valid mark (it may be
	   shorter than a threshold). */
	CW_REC_SET_STATE (rec, RS_MARK, (&cw_debug_object));

	return CW_SUCCESS;
}




/**
   \errno ERANGE - invalid state of receiver was discovered
   \errno EINVAL - errors while processing or getting \p timestamp
   \errno ECANCELED - the mark has been classified as noise spike and rejected
   \errno EBADMSG - this function can't recognize the mark
   \errno ENOMEM - space for representation of character has been exhausted

   \reviewed on 2017-02-04

   \param rec - receiver
   \param timestamp - timestamp of "end of mark" event. May be NULL, then current time will be used.

   \return CW_SUCCESS when no errors occurred
   \return CW_FAILURE otherwise
*/
int cw_rec_mark_end(cw_rec_t * rec, const volatile struct timeval * timestamp)
{
	/* The receive state is expected to be inside of a mark. */
	if (rec->state != RS_MARK) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "mark_end: receive state not RS_MARK: %s", cw_receiver_states[rec->state]);
		errno = ERANGE;
		return CW_FAILURE;
	}
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      MSG_PREFIX "mark_end: receive state: %s", cw_receiver_states[rec->state]);

	/* Take a safe copy of the current end timestamp, in case we need
	   to put it back if we decide this mark is really just noise. */
	struct timeval saved_end_timestamp = rec->mark_end;

	/* Save the timestamp passed in, or get one. */
	if (!cw_timestamp_validate_internal(&rec->mark_end, timestamp)) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Compare the timestamps to determine the length of the mark. */
	int mark_len = cw_timestamp_compare_internal(&rec->mark_start,
						     &rec->mark_end);

#if 0
	fprintf(stderr, "------- %d.%d - %d.%d = %d (%d)\n",
		rec->mark_end.tv_sec, rec->mark_end.tv_usec,
		rec->mark_start.tv_sec, rec->mark_start.tv_usec,
		mark_len, cw_timestamp_compare_internal(&rec->mark_start, &rec->mark_end));
#endif

	if (rec->noise_spike_threshold > 0
	    && mark_len <= rec->noise_spike_threshold) {

		/* This pair of start()/stop() calls is just a noise,
		   ignore it.

		   Revert to state of receiver as it was before
		   complementary cw_rec_mark_begin(). After
		   call to mark_begin() the state was changed to
		   mark, but what state it was before call to
		   start()?

		   Check position in representation buffer (how many
		   marks are in the buffer) to see in which state the
		   receiver was *before* mark_begin() function call,
		   and restore this state. */
		CW_REC_SET_STATE (rec, (rec->representation_ind == 0 ? RS_IDLE : RS_IMARK_SPACE), (&cw_debug_object));

		/* Put the end-of-mark timestamp back to how it was when we
		   came in to the routine. */
		rec->mark_end = saved_end_timestamp;

		cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      MSG_PREFIX "mark_end: '%d [us]' mark identified as spike noise (threshold = '%d [us]')",
			      mark_len, rec->noise_spike_threshold);

		errno = ECANCELED;
		return CW_FAILURE;
	}


	/* This was not a noise. At this point, we have to make a
	   decision about the mark just received.  We'll use a routine
	   that compares length of a mark against pre-calculated Dot
	   and Dash length ranges to tell us what it thinks this mark
	   is (Dot or Dash).  If the routine can't decide, it will
	   hand us back an error which we return to the caller.
	   Otherwise, it returns a mark (Dot or Dash), for us to put
	   in representation buffer. */
	char mark;
	int status = cw_rec_identify_mark_internal(rec, mark_len, &mark);
	if (!status) {
		errno = EBADMSG;
		return CW_FAILURE;
	}

	if (rec->is_adaptive_receive_mode) {
		/* Update the averaging buffers so that the adaptive
		   tracking of received Morse speed stays up to
		   date. */
		cw_rec_update_averages_internal(rec, mark_len, mark);
	} else {
		/* Do nothing. Don't fiddle about trying to track for
		   fixed speed receive. */
	}

	/* Update Dot and Dash length statistics.  It may seem odd to do
	   this after calling cw_rec_update_averages_internal(),
	   rather than before, as this function changes the ideal values we're
	   measuring against.  But if we're on a speed change slope, the
	   adaptive tracking smoothing will cause the ideals to lag the
	   observed speeds.  So by doing this here, we can at least
	   ameliorate this effect, if not eliminate it. */
	if (mark == CW_DOT_REPRESENTATION) {
		cw_rec_update_stats_internal(rec, CW_REC_STAT_DOT, mark_len);
	} else {
		cw_rec_update_stats_internal(rec, CW_REC_STAT_DASH, mark_len);
	}

	/* Add the mark to the receiver's representation buffer. */
	rec->representation[rec->representation_ind++] = mark;
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      MSG_PREFIX "mark_end: recognized representation is '%s'", rec->representation);

	/* We just added a mark to the receive buffer.  If it's full,
	   then we have to do something, even though it's unlikely.
	   What we'll do is make a unilateral declaration that if we
	   get this far, we go to end-of-char error state
	   automatically. */
	if (rec->representation_ind == CW_REC_REPRESENTATION_CAPACITY - 1) {

		CW_REC_SET_STATE (rec, RS_EOC_GAP_ERR, (&cw_debug_object));

		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "mark_end: receiver's representation buffer is full");

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* All is well.  Move to the more normal inter-mark-space
	   state. */
	CW_REC_SET_STATE (rec, RS_IMARK_SPACE, (&cw_debug_object));

	return CW_SUCCESS;
}




/**
   \brief Analyze a mark and identify it as a Dot or Dash

   Identify a mark (Dot/Dash) represented by a duration of mark: \p
   mark_len.

   Identification is done using the length ranges provided by the low
   level timing parameters.

   On success function returns CW_SUCCESS and sends back either a Dot
   or a Dash through \p mark.

   On failure it returns CW_FAILURE if the mark is not recognizable as
   either a Dot or a Dash, and sets the receiver state to one of the
   error states, depending on the length of mark passed in.

   Note: for adaptive timing, the mark should _always_ be recognized
   as a Dot or a Dash, because the length ranges will have been set to
   cover 0 to INT_MAX.

   \param rec - receiver
   \param mark_len - length of mark to analyze
   \param mark - variable to store identified mark (output variable)

   \return CW_SUCCESS if a mark has been identified as either Dot or Dash
   \return CW_FAILURE otherwise
*/
int cw_rec_identify_mark_internal(cw_rec_t *rec, int mark_len, /* out */ char *mark)
{
	cw_assert (mark, MSG_PREFIX "output argument is NULL");

	/* Synchronize parameters if required */
	cw_rec_sync_parameters_internal(rec);

	/* If the length was, within tolerance, a Dot, return Dot to
	   the caller.  */
	if (mark_len >= rec->dot_len_min
	    && mark_len <= rec->dot_len_max) {

		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO, MSG_PREFIX "identify: mark '%d [us]' recognized as DOT (limits: %d - %d [us])", mark_len, rec->dot_len_min, rec->dot_len_max);

		*mark = CW_DOT_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* Do the same for a dash. */
	if (mark_len >= rec->dash_len_min
	    && mark_len <= rec->dash_len_max) {

		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO, MSG_PREFIX "identify: mark '%d [us]' recognized as DASH (limits: %d - %d [us])", mark_len, rec->dash_len_min, rec->dash_len_max);

		*mark = CW_DASH_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* This mark is not a Dot or a Dash, so we have an error
	   case. */
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      MSG_PREFIX "identify: unrecognized mark, len = %d [us]", mark_len);
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      MSG_PREFIX "identify: dot limits: %d - %d [us]", rec->dot_len_min, rec->dot_len_max);
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      MSG_PREFIX "identify: dash limits: %d - %d [us]", rec->dash_len_min, rec->dash_len_max);

	/* We should never reach here when in adaptive timing receive
	   mode - a mark should be always recognized as Dot or Dash,
	   and function should have returned before reaching this
	   point. */
	if (rec->is_adaptive_receive_mode) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "identify: unrecognized mark in adaptive receive");
	} else {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "identify: unrecognized mark in non-adaptive receive");
	}



	/* TODO: making decision about current state of receiver is
	   out of scope of this function. Move the part below to
	   separate function. */

	/* If we can't send back any result through \p mark,
	   let's move to either "end-of-character, in error" or
	   "end-of-word, in error" state.

	   We will treat \p mark_len as length of space.

	   Depending on the length of space, we pick which of the
	   error states to move to, and move to it.  The comparison is
	   against the expected end-of-char delay.  If it's larger,
	   then fix at word error, otherwise settle on char error.

	   TODO: reconsider this for a moment: the function has been
	   called because client code has received a *mark*, not a
	   space. Are we sure that we now want to treat the
	   mark_len as length of *space*? And do we want to
	   move to either RS_EOW_GAP_ERR or RS_EOC_GAP_ERR pretending that
	   this is a length of *space*? */
	CW_REC_SET_STATE (rec, (mark_len > rec->eoc_len_max ? RS_EOW_GAP_ERR : RS_EOC_GAP_ERR), (&cw_debug_object));

	return CW_FAILURE;
}




/**
   \brief Update receiver's averaging data structures with most recent data

   When in adaptive receiving mode, function updates the averages of
   Dot or Dash lengths with given \p mark_len, and recalculates the
   adaptive threshold for the next receive mark.

   \reviewed on 2017-02-04

   \param rec - receiver
   \param mark_len - length of a mark (Dot or Dash)
   \param mark - CW_DOT_REPRESENTATION or CW_DASH_REPRESENTATION
*/
void cw_rec_update_averages_internal(cw_rec_t *rec, int mark_len, char mark)
{
	/* We are not going to tolerate being called in fixed speed mode. */
	if (!rec->is_adaptive_receive_mode) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_WARNING,
			      MSG_PREFIX "called 'adaptive' function when receiver is not in adaptive mode\n");
		return;
	}

	/* Update moving averages for dots or dashes. */
	if (mark == CW_DOT_REPRESENTATION) {
		cw_rec_update_average_internal(&rec->dot_averaging, mark_len);
	} else if (mark == CW_DASH_REPRESENTATION) {
		cw_rec_update_average_internal(&rec->dash_averaging, mark_len);
	} else {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "unknown mark '%c' / '0x%x'\n", mark, mark);
		return;
	}

	/* Recalculate the adaptive threshold. */
	int avg_dot_len = rec->dot_averaging.average;
	int avg_dash_len = rec->dash_averaging.average;
	rec->adaptive_speed_threshold = (avg_dash_len - avg_dot_len) / 2 + avg_dot_len;

	/* We are in adaptive mode. Since ->adaptive_speed_threshold
	   has changed, we need to calculate new ->speed with sync().
	   Low-level parameters will also be re-synchronized to new
	   threshold/speed. */
	rec->parameters_in_sync = false;
	cw_rec_sync_parameters_internal(rec);

	if (rec->speed < CW_SPEED_MIN || rec->speed > CW_SPEED_MAX) {

		/* Clamp the speed. */
		rec->speed = rec->speed < CW_SPEED_MIN ? CW_SPEED_MIN : CW_SPEED_MAX;

		/* Direct manipulation of speed in line above
		   (clamping) requires resetting adaptive mode and
		   re-synchronizing to calculate the new threshold,
		   which unfortunately recalculates everything else
		   according to fixed speed.

		   So, we then have to reset adaptive mode and
		   re-synchronize one more time, to get all other
		   parameters back to where they should be. */

		rec->is_adaptive_receive_mode = false;
		rec->parameters_in_sync = false;
		cw_rec_sync_parameters_internal(rec);

		rec->is_adaptive_receive_mode = true;
		rec->parameters_in_sync = false;
		cw_rec_sync_parameters_internal(rec);
	}

	return;
}




/**
   \brief Add Dot or Dash to receiver's representation buffer

   Function adds a \p mark (either a Dot or a Dash) to the
   receiver's representation buffer.

   Since we can't add a mark to the buffer without any
   accompanying timing information, the function also accepts
   \p timestamp of the "end of mark" event.  If the \p timestamp
   is NULL, the timestamp for current time is used.

   The receiver's state is updated as if we had just received a call
   to cw_rec_mark_end().

   \errno ERANGE - invalid state of receiver was discovered.
   \errno EINVAL - errors while processing or getting \p timestamp
   \errno ENOMEM - space for representation of character has been exhausted

   \param rec - receiver
   \param timestamp - timestamp of "end of mark" event. May be NULL, then current time will be used.
   \param mark - mark to be inserted into receiver's representation buffer

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_rec_add_mark(cw_rec_t * rec, const volatile struct timeval * timestamp, char mark)
{
	/* The receiver's state is expected to be idle or
	   inter-mark-space in order to use this routine. */
	if (rec->state != RS_IDLE && rec->state != RS_IMARK_SPACE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* This routine functions as if we have just seen a mark end,
	   yet without really seeing a mark start.

	   It doesn't matter that we don't know timestamp of start of
	   this mark: start timestamp would be needed only to
	   determine mark length (and from the mark length to
	   determine mark type (Dot/Dash)). But since the mark type
	   has been determined by \p mark, we don't need timestamp for
	   beginning of mark.

	   What does matter is timestamp of end of this mark. This is
	   because the receiver representation routines that may be
	   called later look at the time since the last end of mark
	   to determine whether we are at the end of a word, or just
	   at the end of a character. */
	if (!cw_timestamp_validate_internal(&rec->mark_end, timestamp)) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Add the mark to the receiver's representation buffer. */
	rec->representation[rec->representation_ind++] = mark;

	/* We just added a mark to the receiver's buffer.  As in
	   cw_rec_mark_end(): if it's full, then we have to do
	   something, even though it's unlikely to actually be
	   full. */
	if (rec->representation_ind == CW_REC_REPRESENTATION_CAPACITY - 1) {

		CW_REC_SET_STATE (rec, RS_EOC_GAP_ERR, (&cw_debug_object));

		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "add_mark: receiver's representation buffer is full");

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* Since we effectively just saw the end of a mark, move to
	   the inter-mark-space state. */
	CW_REC_SET_STATE (rec, RS_IMARK_SPACE, (&cw_debug_object));

	return CW_SUCCESS;
}




/**
   \brief Try to poll representation from receiver

   Try to get representation of received character and receiver's
   state flags. The representation is appended to end of \p
   representation.

   \errno ERANGE - invalid state of receiver was discovered.
   \errno EINVAL - errors while processing or getting \p timestamp
   \errno EAGAIN - function called too early, representation not ready yet

   \param rec - receiver
   \param timestamp
   \param representation - output variable, representation of character from receiver's buffer
   \param is_end_of_word - output variable,
   \param is_error - output variable

   \return CW_SUCCESS if a correct representation has been returned through \p representation
   \return CW_FAILURE otherwise
*/
int cw_rec_poll_representation(cw_rec_t * rec,
			       const struct timeval * timestamp,
			       /* out */ char * representation,
			       /* out */ bool * is_end_of_word,
			       /* out */ bool * is_error)
{
	if (rec->state == RS_EOW_GAP
	    || rec->state == RS_EOW_GAP_ERR) {

		/* Until receiver is notified about new mark, its
		   state won't change, and representation stored by
		   receiver's buffer won't change.

		   Repeated calls of this function when receiver is in
		   this state will simply return the same
		   representation over and over again.

		   Because the state of receiver is settled, \p
		   timestamp is uninteresting. We don't expect it to
		   hold any useful information that could influence
		   receiver's state or representation buffer. */

		cw_rec_poll_representation_eow_internal(rec, representation, is_end_of_word, is_error);
		return CW_SUCCESS;

	} else if (rec->state == RS_IDLE
		   || rec->state == RS_MARK) {

		/* Not a good time/state to call this get()
		   function. */
		errno = ERANGE;
		return CW_FAILURE;

	} else {
		/* Pass to handling other states. */
	}



	/* Four receiver states were covered above, so we are left
	   with these three: */
	cw_assert (rec->state == RS_IMARK_SPACE
		   || rec->state == RS_EOC_GAP
		   || rec->state == RS_EOC_GAP_ERR,

		   MSG_PREFIX "poll: unexpected receiver state %d / %s", rec->state, cw_receiver_states[rec->state]);

	/* Stream of data is in one of these states
	   - inter-mark space, or
	   - end-of-character gap, or
	   - end-of-word gap.
	   To see which case is true, calculate length of this space
	   by comparing current/given timestamp with end of last
	   mark. */
	struct timeval now_timestamp;
	if (!cw_timestamp_validate_internal(&now_timestamp, timestamp)) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	int space_len = cw_timestamp_compare_internal(&rec->mark_end, &now_timestamp);
	if (space_len == INT_MAX) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      MSG_PREFIX "poll: space len == INT_MAX");

		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Synchronize parameters if required */
	cw_rec_sync_parameters_internal(rec);

	if (space_len >= rec->eoc_len_min
	    && space_len <= rec->eoc_len_max) {

		//fprintf(stderr, "EOC: space len = %d (%d - %d)\n", space_len, rec->eoc_len_min, rec->eoc_len_max);

		/* The space is, within tolerance, an end-of-character
		   gap.

		   We have a complete character representation in
		   receiver's buffer and we can return it. */
		cw_rec_poll_representation_eoc_internal(rec, space_len, representation, is_end_of_word, is_error);
		return CW_SUCCESS;

	} else if (space_len > rec->eoc_len_max) {

		// fprintf(stderr, "EOW: space len = %d (> %d) ------------- \n", space_len, rec->eoc_len_max);

		/* The space is too long for end-of-character
		   state. This should be end-of-word state. We have
		   to inform client code about this, too.

		   We have a complete character representation in
		   receiver's buffer and we can return it. */
		cw_rec_poll_representation_eow_internal(rec, representation, is_end_of_word, is_error);
		return CW_SUCCESS;

	} else { /* space_len < rec->eoc_len_min */
		/* We are still inside a character (inside an
		   inter-mark space, to be precise). The receiver
		   can't return a representation, because building a
		   representation is not finished yet.

		   So it is too early to return a representation,
		   because it's not complete yet. */

		errno = EAGAIN;
		return CW_FAILURE;
	}
}




/**
   \brief Prepare return values at end-of-character

   Return representation of received character and receiver's state
   flags after receiver has encountered end-of-character gap. The
   representation is appended to end of \p representation.

   Update receiver's state (\p rec) so that it matches end-of-character state.

   Since this is _eoc_ function, \p is_end_of_word is set to false.

   \p rec - receiver
   \p representation - representation of character from receiver's buffer
   \p is_end_of_word - end-of-word flag
   \p is_error - error flag
*/
void cw_rec_poll_representation_eoc_internal(cw_rec_t *rec, int space_len,
					     /* out */ char *representation,
					     /* out */ bool *is_end_of_word,
					     /* out */ bool *is_error)
{
	if (rec->state == RS_IMARK_SPACE) {
		/* State of receiver is inter-mark space, but real
		   length of current space turned out to be a bit
		   longer than acceptable inter-mark space (length of
		   space indicates that it's inter-character
		   space). Update length statistics for space
		   identified as inter-character space. */
		cw_rec_update_stats_internal(rec, CW_REC_STAT_ICHAR_SPACE, space_len);

		/* Transition of state of receiver. */
		CW_REC_SET_STATE (rec, RS_EOC_GAP, (&cw_debug_object));
	} else {
		cw_assert (rec->state == RS_EOC_GAP || rec->state == RS_EOC_GAP_ERR,
			   MSG_PREFIX "poll eoc: unexpected state of receiver: %d / %s",
			   rec->state, cw_receiver_states[rec->state]);

		/* We are already in RS_EOC_GAP or RS_EOC_GAP_ERR, so nothing to do. */
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO, MSG_PREFIX "poll eoc: state: %s", cw_receiver_states[rec->state]);

	/* Return receiver's state. */
	if (is_end_of_word) {
		*is_end_of_word = false;
	}
	if (is_error) {
		*is_error = (rec->state == RS_EOC_GAP_ERR);
	}

	/* Append representation from receiver's buffer to caller's buffer. */
	*representation = '\0';
	strncat(representation, rec->representation, rec->representation_ind);

	/* Since we are in eoc state, there will be no more Dots or Dashes added to current representation. */
	rec->representation[rec->representation_ind] = '\0';

	return;
}




/**
   \brief Prepare return values at end-of-word

   Return representation of received character and receiver's state
   flags after receiver has encountered end-of-word gap. The
   representation is appended to end of \p representation.

   Update receiver's state (\p rec) so that it matches end-of-word state.

   Since this is _eow_ function, \p is_end_of_word is set to true.

   \param rec - receiver
   \param representation - representation of character from receiver's buffer
   \param is_end_of_word - end-of-word flag
   \param is_error - error flag
*/
void cw_rec_poll_representation_eow_internal(cw_rec_t *rec,
					     /* out */ char *representation,
					     /* out */ bool *is_end_of_word,
					     /* out */ bool *is_error)
{
	if (rec->state == RS_EOC_GAP || rec->state == RS_IMARK_SPACE) {
		CW_REC_SET_STATE (rec, RS_EOW_GAP, (&cw_debug_object)); /* Transition of state. */

	} else if (rec->state == RS_EOC_GAP_ERR) {
		CW_REC_SET_STATE (rec, RS_EOW_GAP_ERR, (&cw_debug_object)); /* Transition of state with preserving error. */

	} else if (rec->state == RS_EOW_GAP_ERR || rec->state == RS_EOW_GAP) {
		; /* No need to change state. */

	} else {
		cw_assert (0, MSG_PREFIX "poll eow: unexpected receiver state %d / %s", rec->state, cw_receiver_states[rec->state]);
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO, MSG_PREFIX "poll eow: state: %s", cw_receiver_states[rec->state]);

	/* Return receiver's state. */
	if (is_end_of_word) {
		*is_end_of_word = true;
	}
	if (is_error) {
		*is_error = (rec->state == RS_EOW_GAP_ERR);
	}

	/* Append representation from receiver's buffer to caller's buffer. */
	*representation = '\0';
	strncat(representation, rec->representation, rec->representation_ind);

	/* Since we are in eoc state, there will be no more Dots or Dashes added to current representation. */
	rec->representation[rec->representation_ind] = '\0';

	return;
}





/**
   \brief Try to poll character from receiver

   Try to get received character and receiver's state flags. The
   representation is appended to end of \p representation.

   \errno ERANGE - invalid state of receiver was discovered.
   \errno EINVAL - errors while processing or getting \p timestamp
   \errno EAGAIN - function called too early, character not ready yet
   \errno ENOENT - function can't convert representation retrieved from receiver into a character

   \param rec - receiver
   \param timestamp
   \param c - output variable, character received by receiver
   \param is_end_of_word - output variable,
   \param is_error - output variable

   \return CW_SUCCESS if a correct representation has been returned through \p representation
   \return CW_FAILURE otherwise
*/
int cw_rec_poll_character(cw_rec_t * rec,
			  const struct timeval * timestamp,
			  /* out */ char * c,
			  /* out */ bool * is_end_of_word,
			  /* out */ bool * is_error)
{
	/* TODO: in theory we don't need these intermediate bool
	   variables, since is_end_of_word and is_error won't be
	   modified by any function on !success. */
	bool end_of_word, error;

	char representation[CW_REC_REPRESENTATION_CAPACITY + 1];

	/* See if we can obtain a representation from receiver. */
	int status = cw_rec_poll_representation(rec, timestamp,
						representation,
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

	/* A full character has been received. Directly after
	   it comes a space. Either a short inter-character
	   space followed by another character (in this case
	   we won't display the inter-character space), or
	   longer inter-word space - this space we would like
	   to catch and display.

	   Set a flag indicating that next poll may result in
	   inter-word space. */
	if (!end_of_word) {
		rec->is_pending_inter_word_space = true;
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
   \brief Reset state of receiver

   The function doesn't reset parameters or statistics.
*/
void cw_rec_reset_state(cw_rec_t * rec)
{
	memset(rec->representation, 0, sizeof (rec->representation));
	rec->representation_ind = 0;

	rec->is_pending_inter_word_space = false;

	CW_REC_SET_STATE (rec, RS_IDLE, (&cw_debug_object));

	return;
}




/**
   \brief Get the number of elements (Dots/Dashes) the receiver's buffer can accommodate

   The maximum number of elements written out by cw_rec_poll_representation()
   is the capacity + 1, the extra character being used for the terminating
   NUL.

   \return number of elements that can be stored in receiver's representation buffer
*/
int cw_rec_get_receive_buffer_capacity_internal(void)
{
	return CW_REC_REPRESENTATION_CAPACITY;
}




/**
   \brief Get number of symbols (Dots and Dashes) in receiver's representation buffer

   \reviewed on 2017-02-04

   \param rec - receiver
*/
int cw_rec_get_buffer_length_internal(const cw_rec_t *rec)
{
	return rec->representation_ind;
}




/**
   \brief Reset essential receive parameters to their initial values

   \param rec - receiver
*/
void cw_rec_reset_parameters_internal(cw_rec_t *rec)
{
	cw_assert (rec, MSG_PREFIX "reset parameters: receiver is NULL");

	rec->speed = CW_SPEED_INITIAL;
	rec->tolerance = CW_TOLERANCE_INITIAL;
	rec->is_adaptive_receive_mode = CW_REC_ADAPTIVE_MODE_INITIAL;
	rec->noise_spike_threshold = CW_REC_NOISE_THRESHOLD_INITIAL;

	/* FIXME: consider resetting ->gap as well. */

	rec->parameters_in_sync = false;

	return;
}




/**
   \brief Synchronize receivers' parameters

   \reviewed on 2017-02-04

   \param rec - receiver
*/
void cw_rec_sync_parameters_internal(cw_rec_t *rec)
{
	cw_assert (rec, MSG_PREFIX "sync parameters: receiver is NULL");

	/* Do nothing if we are already synchronized. */
	if (rec->parameters_in_sync) {
		return;
	}

	/* First, depending on whether we are set for fixed speed or
	   adaptive speed, calculate either the threshold from the
	   receive speed, or the receive speed from the threshold,
	   knowing that the threshold is always, effectively, two dot
	   lengths.  Weighting is ignored for receive parameters,
	   although the core unit length is recalculated for the
	   receive speed, which may differ from the send speed. */

	/* FIXME: shouldn't we move the calculation of unit_len (that
	   depends on rec->speed) after the calculation of
	   rec->speed? */
	int unit_len = CW_DOT_CALIBRATION / rec->speed;

	if (rec->is_adaptive_receive_mode) {
		rec->speed = CW_DOT_CALIBRATION	/ (rec->adaptive_speed_threshold / 2.0);
	} else {
		rec->adaptive_speed_threshold = 2 * unit_len;
	}



	rec->dot_len_ideal = unit_len;
	rec->dash_len_ideal = 3 * unit_len;
	rec->eom_len_ideal = unit_len;
	rec->eoc_len_ideal = 3 * unit_len;

	/* These two lines mimic calculations done in
	   cw_gen_sync_parameters_internal().  See the function for
	   more comments. */
	rec->additional_delay = rec->gap * unit_len;
	rec->adjustment_delay = (7 * rec->additional_delay) / 3;

	/* Set length ranges of low level parameters. The length
	   ranges depend on whether we are required to adapt to the
	   incoming Morse code speeds. */
	if (rec->is_adaptive_receive_mode) {
		/* Adaptive receiving mode. */
		rec->dot_len_min = 0;
		rec->dot_len_max = 2 * rec->dot_len_ideal;

		/* Any mark longer than Dot is a Dash in adaptive
		   receiving mode. */

		/* FIXME: shouldn't this be '= rec->dot_len_max + 1'?
		   now the length ranges for Dot and Dash overlap. */
		rec->dash_len_min = rec->dot_len_max;
		rec->dash_len_max = INT_MAX;

#if 0
		int debug_eoc_len_max = rec->eoc_len_max;
#endif

		/* Make the inter-mark space be anything up to the
		   adaptive threshold lengths - that is two Dots.  And
		   the end-of-character gap is anything longer than
		   that, and shorter than five Dots. */
		rec->eom_len_min = rec->dot_len_min;
		rec->eom_len_max = rec->dot_len_max;
		rec->eoc_len_min = rec->eom_len_max;
		rec->eoc_len_max = 5 * rec->dot_len_ideal;

#if 0
		if (debug_eoc_len_max != rec->eoc_len_max) {
			fprintf(stderr, "eoc_len_max changed from %d to %d --------\n", debug_eoc_len_max, rec->eoc_len_max);
		}
#endif

	} else {
		/* Fixed speed receiving mode. */

		int tolerance = (rec->dot_len_ideal * rec->tolerance) / 100; /* [%] */
		rec->dot_len_min = rec->dot_len_ideal - tolerance;
		rec->dot_len_max = rec->dot_len_ideal + tolerance;
		rec->dash_len_min = rec->dash_len_ideal - tolerance;
		rec->dash_len_max = rec->dash_len_ideal + tolerance;

		/* Make the inter-mark space the same as the dot
		   length range. */
		rec->eom_len_min = rec->dot_len_min;
		rec->eom_len_max = rec->dot_len_max;

		/* Make the end-of-character gap, expected to be
		   three dots, the same as dash length range at the
		   lower end, but make it the same as the dash length
		   range _plus_ the "Farnsworth" delay at the top of
		   the length range. */
		rec->eoc_len_min = rec->dash_len_min;
		rec->eoc_len_max = rec->dash_len_max
			+ rec->additional_delay + rec->adjustment_delay;

		/* Any gap longer than eoc_len_max is by implication
		   end-of-word gap. */
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_PARAMETERS, CW_DEBUG_INFO,
		      MSG_PREFIX "sync parameters: receive usec timings <%.2f [wpm]>: dot: %d-%d [ms], dash: %d-%d [ms], %d-%d[%d], %d-%d[%d], thres: %d [us]",
		      rec->speed,
		      rec->dot_len_min, rec->dot_len_max,
		      rec->dash_len_min, rec->dash_len_max,
		      rec->eom_len_min, rec->eom_len_max, rec->eom_len_ideal,
		      rec->eoc_len_min, rec->eoc_len_max, rec->eoc_len_ideal,
		      rec->adaptive_speed_threshold);

	/* Receiver parameters are now in sync. */
	rec->parameters_in_sync = true;

	return;
}




/**
   \param rec - receiver
*/
bool cw_rec_poll_is_pending_inter_word_space(cw_rec_t const * rec)
{
	return rec->is_pending_inter_word_space;
}




#ifdef WITH_EXPERIMENTAL_RECEIVER

/**
   \param rec - receiver
   \param callback
*/
void cw_rec_register_push_callback(cw_rec_t * rec, cw_rec_push_callback_t * callback)
{
	rec->push_callback = callback;

	return;
}

#endif
