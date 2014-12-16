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




/**
   \file libcw_rec.c

   Receiver. Receive a series of marks and spaces. Interpret them as
   characters.


   There are two ways of adding marks and spaces to receiver.

   First of them is to notify receiver about "begin of mark" and "end
   of mark" events. Receiver then tries to figure out how long a mark
   or space is, what type of mark (dot/dash) or space (inter-mark,
   inter-character, inter-word) it is, and when a full character has
   been received.

   This is done with cw_start_receive_tone() and cw_end_receive_tone()
   functions.

   The second method is to inform receiver not about start and stop of
   marks (dots/dashes), but about full marks themselves.  This is done
   with cw_receive_buffer_dot() and cw_receive_buffer_dash() - two
   functions that are one level of abstraction above functions from
   first method.


   Currently there is only one method of passing received data
   (characters) between receiver and client code. This is done by
   client code cyclically polling the receiver with
   cw_receive_representation() or with cw_receive_character() which is
   built on top of cw_receive_representation().


   Duration (length) of marks, spaces and few other things is in
   microseconds [us], unless specified otherwise.
*/





#include "config.h"


#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h>  /* sqrt(), cosf() */


#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif


#include "libcw.h"
#include "libcw_rec.h"
#include "libcw_data.h"
#include "libcw_utils.h"
#include "libcw_debug.h"
#include "libcw_test.h"





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;





/* "RS" stands for "Receiver State" */
enum {
	RS_IDLE,          /* Representation buffer is empty and ready to accept data. */
	RS_MARK,          /* Mark. */
	RS_SPACE,         /* Space (inter-mark-space). */
	RS_EOC_GAP,       /* Gap after a character, without error (EOC = end-of-character). */
	RS_EOW_GAP,       /* Gap after a word, without error (EOW = end-of-word). */
	RS_EOC_GAP_ERR,   /* Gap after a character, with error. */
	RS_EOW_GAP_ERR    /* Gap after a word, with error. */
};


static const char *cw_receiver_states[] = {
	"RS_IDLE",
	"RS_MARK",
	"RS_SPACE",
	"RS_EOC_GAP",
	"RS_EOW_GAP",
	"RS_EOC_GAP_ERR",
	"RS_EOW_GAP_ERR"
};





/* Default initial values for library controls. */
enum { CW_REC_ADAPTIVE_MODE_INITIAL = false };                                            /* Initial adaptive receive setting. */


/* TODO: it would be interesting to track (in debug mode) relationship
   between "speed threshold" and "noise threshold" parameters. */
enum { CW_REC_SPEED_THRESHOLD_INITIAL = (CW_DOT_CALIBRATION / CW_SPEED_INITIAL) * 2 };    /* Initial adaptive speed threshold. [us] */
enum { CW_REC_NOISE_THRESHOLD_INITIAL = (CW_DOT_CALIBRATION / CW_SPEED_MAX) / 2 };        /* Initial noise filter threshold. */


/* TODO: what is the relationship between this constant and CW_REC_REPRESENTATION_CAPACITY?
   Both have value of 256. Coincidence? I don't think so. */
enum { CW_REC_STATISTICS_CAPACITY = 256 };


/* Length of array used to calculate average length of a mark. Average
   length of a mark is used in adaptive receiving mode to track speed
   of incoming Morse data. */
enum { CW_REC_AVERAGING_ARRAY_LENGTH = 4 };





/* Types of receiver's timing statistics.
   CW_REC_STAT_NONE must be zero so that the statistics buffer is initially empty. */
typedef enum {
	CW_REC_STAT_NONE = 0,
	CW_REC_STAT_DOT,           /* Dot mark. */
	CW_REC_STAT_DASH,          /* Dash mark. */
	CW_REC_STAT_IMARK_SPACE,   /* Inter-mark space. */
	CW_REC_STAT_ICHAR_SPACE    /* Inter-character space. */
} stat_type_t;


typedef struct {
	stat_type_t type;  /* Record type */
	int delta;         /* Difference between actual and ideal length of mark or space. [us] */
} cw_rec_statistics_t;





/* A moving averages structure - circular buffer. Used for calculating
   averaged length ([us]) of dots and dashes. */
typedef struct {
	int buffer[CW_REC_AVERAGING_ARRAY_LENGTH];  /* Buffered mark lengths. */
	int cursor;                                 /* Circular buffer cursor. */
	int sum;                                    /* Running sum of lengths of marks. [us] */
	int average;                                /* Averaged length of a mark. [us] */
} cw_rec_averaging_t;





struct cw_rec_struct {

	/* State of receiver state machine. */
	int state;



	/* Essential parameters. */
	/* Changing values of speed, tolerance, gap or
	   is_adaptive_receive_mode will trigger a recalculation of
	   low level timing parameters. */
	float speed;       /* [wpm] */
	int tolerance;
	int gap;         /* Inter-character-gap, similar as in generator. */
	bool is_adaptive_receive_mode;
	int noise_spike_threshold;
	/* Library variable which is automatically adjusted based on
	   incoming Morse data stream, rather than being settable by
	   the user.

	   Not exactly a *speed* threshold, but for a lack of a better
	   name...

	   When the library changes internally value of this variable,
	   it recalculates low level timing parameters too. */
	int adaptive_speed_threshold; /* [microseconds]/[us] */



	/* Retained timestamps of mark's begin and end. */
	struct timeval mark_start;
	struct timeval mark_end;

	/* Buffer for received representation (dots/dashes). This is a
	   fixed-length buffer, filled in as tone on/off timings are
	   taken. The buffer is vastly longer than any practical
	   representation.

	   Along with it we maintain a cursor indicating the current
	   write position. */
	char representation[CW_REC_REPRESENTATION_CAPACITY + 1];
	int representation_ind;



	/* Receiver's low-level timing parameters */

	/* These are basic timing parameters which should be
	   recalculated each time client code demands changing some
	   higher-level parameter of receiver.  How these values are
	   calculated depends on receiving mode (fixed/adaptive). */
	int dot_len_ideal;        /* Length of an ideal dot. [microseconds]/[us] */
	int dot_len_min;          /* Minimal length of mark that will be identified as dot. [us] */
	int dot_len_max;          /* Maximal length of mark that will be identified as dot. [us] */

	int dash_len_ideal;       /* Length of an ideal dash. [us] */
	int dash_len_min;         /* Minimal length of mark that will be identified as dash. [us] */
	int dash_len_max;         /* Maximal length of mark that will be identified as dash. [us] */

	int eom_len_ideal;        /* Ideal end of mark, for stats */
	int eom_len_min;          /* Shortest end of mark allowable */
	int eom_len_max;          /* Longest end of mark allowable */

	int eoc_len_ideal;        /* Ideal end of char, for stats */
	int eoc_len_min;          /* Shortest end of char allowable */
	int eoc_len_max;          /* Longest end of char allowable */

	/* These two fields have the same function as in
	   cw_gen_t. They are needed in function re-synchronizing
	   parameters. */
	int additional_delay;     /* More delay at the end of a char */
	int adjustment_delay;     /* More delay at the end of a word */



	/* Are receiver's parameters in sync?
	   After changing receiver's essential parameters, its
	   low-level timing parameters need to be re-calculated. This
	   is a flag that shows when this needs to be done. */
	bool parameters_in_sync;



	/* Receiver statistics.
	   A circular buffer of entries indicating the difference
	   between the actual and the ideal length of received mark or
	   space, tagged with the type of statistic held, and a
	   circular buffer pointer. */
	cw_rec_statistics_t statistics[CW_REC_STATISTICS_CAPACITY];
	int statistics_ind;



	/* Data structures for calculating averaged length of dots and
	   dashes. The averaged lengths are used for adaptive tracking
	   of receiver's speed (tracking of speed of incoming data). */
	cw_rec_averaging_t dot_averaging;
	cw_rec_averaging_t dash_averaging;
};





cw_rec_t cw_receiver = { .state = RS_IDLE,

			 .gap = CW_GAP_INITIAL,



			 .speed                      = CW_SPEED_INITIAL,
			 .tolerance                  = CW_TOLERANCE_INITIAL,
			 .is_adaptive_receive_mode   = CW_REC_ADAPTIVE_MODE_INITIAL,
			 .noise_spike_threshold      = CW_REC_NOISE_THRESHOLD_INITIAL,



			 /* TODO: this variable is not set in
			    cw_rec_reset_receive_parameters_internal(). Why
			    is it separated from the four main
			    variables? Is it because it is a
			    derivative of speed? But speed is a
			    derivative of this variable in adaptive
			    speed mode. */
			 .adaptive_speed_threshold = CW_REC_SPEED_THRESHOLD_INITIAL,



			 .mark_start = { 0, 0 },
			 .mark_end   = { 0, 0 },

			 .representation_ind = 0,



			 .dot_len_ideal = 0,
			 .dot_len_min = 0,
			 .dot_len_max = 0,

			 .dash_len_ideal = 0,
			 .dash_len_min = 0,
			 .dash_len_max = 0,

			 .eom_len_ideal = 0,
			 .eom_len_min = 0,
			 .eom_len_max = 0,

			 .eoc_len_ideal = 0,
			 .eoc_len_min = 0,
			 .eoc_len_max = 0,

			 .additional_delay = 0,
			 .adjustment_delay = 0,



			 .parameters_in_sync = false,



			 .statistics_ind = 0,
			 .statistics = { {0, 0} },

			 .dot_averaging  = { {0}, 0, 0, 0 },
			 .dash_averaging = { {0}, 0, 0, 0 },
};





static void cw_rec_set_adaptive_internal(cw_rec_t *rec, bool adaptive);




/* Receive and identify a mark. */
static int cw_rec_mark_begin_internal(cw_rec_t *rec, const struct timeval *timestamp);
static int cw_rec_mark_end_internal(cw_rec_t *rec, const struct timeval *timestamp);
static int cw_rec_identify_mark_internal(cw_rec_t *rec, int mark_len, char *representation);
static int cw_rec_add_mark_internal(cw_rec_t *rec, const struct timeval *timestamp, char mark);


/* Functions handling receiver statistics. */
static void   cw_rec_update_stats_internal(cw_rec_t *rec, stat_type_t type, int len);
static double cw_rec_get_stats_internal(cw_rec_t *rec, stat_type_t type);


/* Functions handling averaging data structure in adaptive receiving
   mode. */
static void cw_rec_update_average_internal(cw_rec_averaging_t *avg, int mark_len);
static void cw_rec_update_averages_internal(cw_rec_t *rec, int mark_len, char mark);
static void cw_rec_reset_average_internal(cw_rec_averaging_t *avg, int initial);



static int  cw_rec_poll_representation_internal(cw_rec_t *rec, const struct timeval *timestamp, char *representation, bool *is_end_of_word, bool *is_error);
static void cw_rec_poll_representation_eoc_internal(cw_rec_t *rec, int space_len, char *representation, bool *is_end_of_word, bool *is_error);
static void cw_rec_poll_representation_eow_internal(cw_rec_t *rec, char *representation, bool *is_end_of_word, bool *is_error);
static int  cw_rec_poll_character_internal(cw_rec_t *rec, const struct timeval *timestamp, char *c, bool *is_end_of_word, bool *is_error);





static int   cw_rec_get_buffer_length_internal(cw_rec_t *rec);
static void  cw_rec_clear_buffer_internal(cw_rec_t *rec);
static float cw_rec_get_speed_internal(cw_rec_t *rec);




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
	if (cw_receiver.is_adaptive_receive_mode) {
		errno = EPERM;
		return CW_FAILURE;
	}

	if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* TODO: verify this comparison. */
	float diff = abs((1.0 * new_value) - cw_receiver.speed);
	if (diff >= 0.5) {
		cw_receiver.speed = new_value;

		/* Changes of receive speed require resynchronization. */
		cw_receiver.parameters_in_sync = false;
		cw_rec_sync_parameters_internal(&cw_receiver);
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
	int rv = (int) cw_rec_get_speed_internal(&cw_receiver);
	return rv;
}





float cw_rec_get_speed_internal(cw_rec_t *rec)
{
	return rec->speed;
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
		cw_receiver.parameters_in_sync = false;
		cw_rec_sync_parameters_internal(&cw_receiver);
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
	cw_rec_sync_parameters_internal(&cw_receiver);

	if (dot_usecs)      *dot_usecs = cw_receiver.dot_len_ideal;
	if (dash_usecs)     *dash_usecs = cw_receiver.dash_len_ideal;
	if (dot_min_usecs)  *dot_min_usecs = cw_receiver.dot_len_min;
	if (dot_max_usecs)  *dot_max_usecs = cw_receiver.dot_len_max;
	if (dash_min_usecs) *dash_min_usecs = cw_receiver.dash_len_min;
	if (dash_max_usecs) *dash_max_usecs = cw_receiver.dash_len_max;

	/* End-of-mark. */
	if (end_of_element_min_usecs)     *end_of_element_min_usecs = cw_receiver.eom_len_min;
	if (end_of_element_max_usecs)     *end_of_element_max_usecs = cw_receiver.eom_len_max;
	if (end_of_element_ideal_usecs)   *end_of_element_ideal_usecs = cw_receiver.eom_len_ideal;

	/* End-of-character. */
	if (end_of_character_min_usecs)   *end_of_character_min_usecs = cw_receiver.eoc_len_min;
	if (end_of_character_max_usecs)   *end_of_character_max_usecs = cw_receiver.eoc_len_max;
	if (end_of_character_ideal_usecs) *end_of_character_ideal_usecs = cw_receiver.eoc_len_ideal;

	if (adaptive_threshold) *adaptive_threshold = cw_receiver.adaptive_speed_threshold;

	return;
}





/**
   \brief Set noise spike threshold for receiver

   Set the period shorter than which, on receive, received marks are ignored.
   This allows the "receive mark" functions to apply noise canceling for very
   short apparent marks.
   For useful results the value should never exceed the dot length of a dot at
   maximum speed: 20000 microseconds (the dot length at 60WPM).
   Setting a noise threshold of zero turns off receive mark noise canceling.

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





/* TODO: this function probably should have its old-style version in
   libcw.h as well. */
int cw_rec_set_gap_internal(cw_rec_t *rec, int new_value)
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

   \param avg - averaging data structure (for dot or for dash)
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

   Update table of values used to calculate averaged "length of
   mark". The averaged length of a mark is calculated with moving
   average function.

   The new \p mark_len is added to \p avg, and the oldest is
   discarded. New averaged sum is calculated using updated data.

   \param avg - averaging data structure (for dot or for dash)
   \param mark_len - new "length of mark" value to add to averaging data
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

   \p type may be: CW_REC_STAT_DOT or CW_REC_STAT_DASH or CW_REC_STAT_IMARK_SPACE or CW_REC_STAT_ICHAR_SPACE

   \param rec - receiver
   \param type - mark type
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
	rec->statistics[rec->statistics_ind++].delta = delta;
	rec->statistics_ind %= CW_REC_STATISTICS_CAPACITY;

	return;
}





/**
   \brief Calculate and return length statistics for given type of mark or space

   \p type may be: CW_REC_STAT_DOT or CW_REC_STAT_DASH or CW_REC_STAT_IMARK_SPACE or CW_REC_STAT_ICHAR_SPACE

   \param rec - receiver
   \param type - type of mark or space for which to return statistics

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
		}
	}

	/* Return the standard deviation, or zero if no matching mark. */
	return count > 0 ? sqrt (sum_of_squares / (double) count) : 0.0;
}





/**
   \brief Calculate and return receiver's timing statistics

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
		*dot_sd = cw_rec_get_stats_internal(&cw_receiver, CW_REC_STAT_DOT);
	}
	if (dash_sd) {
		*dash_sd = cw_rec_get_stats_internal(&cw_receiver, CW_REC_STAT_DASH);
	}
	if (element_end_sd) {
		*element_end_sd = cw_rec_get_stats_internal(&cw_receiver, CW_REC_STAT_IMARK_SPACE);
	}
	if (character_end_sd) {
		*character_end_sd = cw_rec_get_stats_internal(&cw_receiver, CW_REC_STAT_ICHAR_SPACE);
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
		cw_receiver.statistics[i].type = CW_REC_STAT_NONE;
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
 * --> RS_IDLE ------->----------- RS_MARK ------------>----------> RS_SPACE <------------- +
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
			      "libcw: receive state %s -> %s",		\
			      cw_receiver_states[m_rec->state], cw_receiver_states[m_new_state]); \
		m_rec->state = m_new_state;				\
	}





/**
   \brief Enable or disable receiver's "adaptive receiving" mode

   Set the mode of a receiver (\p rec) to fixed or adaptive receiving
   mode.

   In adaptive receiving mode the receiver tracks the speed of the
   received Morse code by adapting to the input stream.

   \param rec - receiver for which to set the mode
   \param adaptive - value of receiver's "adaptive mode" to be set
*/
void cw_rec_set_adaptive_internal(cw_rec_t *rec, bool adaptive)
{
	/* Look for change of adaptive receive state. */
	if (rec->is_adaptive_receive_mode != adaptive) {

		rec->is_adaptive_receive_mode = adaptive;

		/* Changing the flag forces a change in low-level parameters. */
		rec->parameters_in_sync = false;
		cw_rec_sync_parameters_internal(rec);

		/* If we have just switched to adaptive mode, (re-)initialize
		   the averages array to the current dot/dash lengths, so
		   that initial averages match the current speed. */
		if (rec->is_adaptive_receive_mode) {
			cw_rec_reset_average_internal(&rec->dot_averaging, rec->dot_len_ideal);
			cw_rec_reset_average_internal(&rec->dash_averaging, rec->dash_len_ideal);
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

   Adaptive speed tracking uses a moving average length of the past N marks
   as its baseline for tracking speeds.  The default state is adaptive speed
   tracking disabled.
*/
void cw_enable_adaptive_receive(void)
{
	cw_rec_set_adaptive_internal(&cw_receiver, true);
	return;
}





/**
   \brief Disable adaptive receive speed tracking

   See documentation of cw_enable_adaptive_receive() for more information
*/
void cw_disable_adaptive_receive(void)
{
	cw_rec_set_adaptive_internal(&cw_receiver, false);
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
	return cw_receiver.is_adaptive_receive_mode;
}





/**
   \brief Signal beginning of receive mark

   Called on the start of a receive mark.  If the \p timestamp is NULL, the
   current timestamp is used as beginning of mark.

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
	int rv = cw_rec_mark_begin_internal(&cw_receiver, timestamp);
	return rv;
}





/* For top-level comment see cw_start_receive_tone(). */
int cw_rec_mark_begin_internal(cw_rec_t *rec, const struct timeval *timestamp)
{
	/* If the receive state is not idle or inter-mark-space, this is a
	   state error.  A start of mark can only happen while we are
	   idle, or in inter-mark-space of a current character. */
	if (rec->state != RS_IDLE && rec->state != RS_SPACE) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: receive state not idle and not inter-mark-space: %s", cw_receiver_states[rec->state]);

		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Validate and save the timestamp, or get one and then save
	   it.  This is a beginning of mark. */
	if (!cw_timestamp_validate_internal(&(rec->mark_start), timestamp)) {
		return CW_FAILURE;
	}

	if (rec->state == RS_SPACE) {
		/* Measure inter-mark space (just for statistics).

		   rec->mark_end is timestamp of end of previous
		   mark. It is set at going to the inter-mark-space
		   state by cw_end_receive tone() or by
		   cw_rec_add_mark_internal(). */
		int space_len = cw_timestamp_compare_internal(&(rec->mark_end),
							      &(rec->mark_start));
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
   \brief Signal end of mark

   The function should be called by client application when releasing
   a key (opening a circuit) has been detected by client application.

   If the \p timestamp is NULL, the current time is used as timestamp
   of end of mark.

   On success, the routine adds a dot or dash to the receiver's
   representation buffer, and returns CW_SUCCESS.

   On failure, it returns CW_FAIURE, with errno set to:
   ERANGE if the call was not preceded by a cw_start_receive_tone() call,
   EINVAL if the timestamp passed in is not valid,
   ENOENT if the mark length was out of bounds for the permissible
   dot and dash lengths and fixed speed receiving is selected,
   ENOMEM if the receiver's representation buffer is full,
   EAGAIN if the mark was shorter than the threshold for noise and was
   therefore ignored.

   \param timestamp - time stamp of "key up" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_end_receive_tone(const struct timeval *timestamp)
{
	int rv = cw_rec_mark_end_internal(&cw_receiver, timestamp);
	return rv;
}





/* For top-level comment see cw_end_receive_tone(). */
int cw_rec_mark_end_internal(cw_rec_t *rec, const struct timeval *timestamp)
{
	/* The receive state is expected to be inside of a mark. */
	if (rec->state != RS_MARK) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Take a safe copy of the current end timestamp, in case we need
	   to put it back if we decide this mark is really just noise. */
	struct timeval saved_end_timestamp = rec->mark_end;

	/* Save the timestamp passed in, or get one. */
	if (!cw_timestamp_validate_internal(&(rec->mark_end), timestamp)) {
		return CW_FAILURE;
	}

	/* Compare the timestamps to determine the length of the mark. */
	int mark_len = cw_timestamp_compare_internal(&(rec->mark_start),
						     &(rec->mark_end));

#if 0
	fprintf(stderr, "------- %d.%d - %d.%d = %d (%d)\n",
		rec->mark_end.tv_sec, rec->mark_end.tv_usec,
		rec->mark_start.tv_sec, rec->mark_start.tv_usec,
		mark_len, cw_timestamp_compare_internal(&(rec->mark_start), &(rec->mark_end)));
#endif

	if (rec->noise_spike_threshold > 0
	    && mark_len <= rec->noise_spike_threshold) {

		/* This pair of start()/stop() calls is just a noise,
		   ignore it.

		   Revert to state of receiver as it was before
		   complementary cw_rec_mark_begin_internal(). After
		   call to mark_begin() the state was changed to
		   mark, but what state it was before call to
		   start()?

		   Check position in representation buffer (how many
		   marks are in the buffer) to see in which state the
		   receiver was *before* mark_begin() function call,
		   and restore this state. */
		CW_REC_SET_STATE (rec, (rec->representation_ind == 0 ? RS_IDLE : RS_SPACE), (&cw_debug_object));

		/* Put the end-of-mark timestamp back to how it was when we
		   came in to the routine. */
		rec->mark_end = saved_end_timestamp;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw: '%d [us]' mark identified as spike noise (threshold = '%d [us]')",
			      mark_len, rec->noise_spike_threshold);

		errno = EAGAIN;
		return CW_FAILURE;
	}


	/* This was not a noise. At this point, we have to make a
	   decision about the mark just received.  We'll use a routine
	   that compares length of a mark against pre-calculated dot
	   and dash length ranges to tell us what it thinks this mark
	   is (dot or dash).  If the routing can't decide, it will
	   hand us back an error which we return to the caller.
	   Otherwise, it returns a mark (dot or dash), for us to put
	   in representation buffer. */
	char mark;
	int status = cw_rec_identify_mark_internal(rec, mark_len, &mark);
	if (!status) {
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

	/* Update dot and dash length statistics.  It may seem odd to do
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

	/* We just added a mark to the receive buffer.  If it's full,
	   then we have to do something, even though it's unlikely.
	   What we'll do is make a unilateral declaration that if we
	   get this far, we go to end-of-char error state
	   automatically. */
	if (rec->representation_ind == CW_REC_REPRESENTATION_CAPACITY - 1) {

		CW_REC_SET_STATE (rec, RS_EOC_GAP_ERR, (&cw_debug_object));

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: receiver's representation buffer is full");

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* All is well.  Move to the more normal inter-mark-space
	   state. */
	CW_REC_SET_STATE (rec, RS_SPACE, (&cw_debug_object));

	return CW_SUCCESS;
}





/**
   \brief Analyze a mark and identify it as a dot or dash

   Identify a mark (dot/dash) represented by a duration of mark.
   The duration is provided in \p mark_len.

   Identification is done using the length ranges provided by the low
   level timing parameters.

   On success function returns CW_SUCCESS and sends back either a dot
   or a dash through \p mark.

   On failure it returns CW_FAILURE with errno set to ENOENT if the
   mark is not recognizable as either a dot or a dash, and sets the
   receiver state to one of the error states, depending on the length
   of mark passed in.

   Note: for adaptive timing, the mark should _always_ be recognized
   as a dot or a dash, because the length ranges will have been set to
   cover 0 to INT_MAX.

   testedin::test_cw_rec_identify_mark_internal()

   \param rec - receiver
   \param mark_len - length of mark to analyze
   \param mark - variable to store identified mark (output variable)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_rec_identify_mark_internal(cw_rec_t *rec, int mark_len, /* out */ char *mark)
{
	cw_assert (mark, "Output parameter is NULL");

	/* Synchronize parameters if required */
	cw_rec_sync_parameters_internal(rec);

	/* If the length was, within tolerance, a dot, return dot to
	   the caller.  */
	if (mark_len >= rec->dot_len_min
	    && mark_len <= rec->dot_len_max) {

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: mark '%d [us]' recognized as DOT (limits: %d - %d [us])",
			      mark_len, rec->dot_len_min, rec->dot_len_max);

		*mark = CW_DOT_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* Do the same for a dash. */
	if (mark_len >= rec->dash_len_min
	    && mark_len <= rec->dash_len_max) {

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: mark '%d [us]' recognized as DASH (limits: %d - %d [us])",
			      mark_len, rec->dash_len_min, rec->dash_len_max);

		*mark = CW_DASH_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* This mark is not a dot or a dash, so we have an error
	   case. */
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw: unrecognized mark, len = %d [us]", mark_len);
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw: dot limits: %d - %d [us]", rec->dot_len_min, rec->dot_len_max);
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw: dash limits: %d - %d [us]", rec->dash_len_min, rec->dash_len_max);

	/* We should never reach here when in adaptive timing receive
	   mode - a mark should be always recognized as dot or dash,
	   and function should have returned before reaching this
	   point. */
	if (rec->is_adaptive_receive_mode) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: unrecognized mark in adaptive receive");
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


	/* Return ENOENT to the caller. */
	errno = ENOENT;
	return CW_FAILURE;
}





/**
   \brief Update receiver's averaging data structures with most recent data

   When in adaptive receiving mode, function updates the averages of
   dot and dash lengths with given \p mark_len, and recalculates the
   adaptive threshold for the next receive mark.

   \param rec - receiver
   \param mark_len - length of a mark (dot or dash)
   \param mark - CW_DOT_REPRESENTATION or CW_DASH_REPRESENTATION
*/
void cw_rec_update_averages_internal(cw_rec_t *rec, int mark_len, char mark)
{
	/* We are not going to tolerate being called in fixed speed mode. */
	if (!rec->is_adaptive_receive_mode) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_WARNING,
			      "Called \"adaptive\" function when receiver is not in adaptive mode\n");
		return;
	}

	/* Update moving averages for dots or dashes. */
	if (mark == CW_DOT_REPRESENTATION) {
		cw_rec_update_average_internal(&rec->dot_averaging, mark_len);
	} else if (mark == CW_DASH_REPRESENTATION) {
		cw_rec_update_average_internal(&rec->dash_averaging, mark_len);
	} else {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "Unknown mark %d\n", mark);
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
int cw_rec_add_mark_internal(cw_rec_t *rec, const struct timeval *timestamp, char mark)
{
	/* The receiver's state is expected to be idle or
	   inter-mark-space in order to use this routine. */
	if (rec->state != RS_IDLE && rec->state != RS_SPACE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* This routine functions as if we have just seen a mark end,
	   yet without really seeing a mark start.

	   It doesn't matter that we don't know timestamp of start of
	   this mark: start timestamp would be needed only to
	   determine mark length (and from the mark length to
	   determine mark type (dot/dash)). But since the mark type
	   has been determined by \p mark, we don't need timestamp for
	   beginning of mark.

	   What does matter is timestamp of end of this mark. This is
	   because the receiver representation routines that may be
	   called later look at the time since the last end of mark
	   to determine whether we are at the end of a word, or just
	   at the end of a character. */
	if (!cw_timestamp_validate_internal(&rec->mark_end, timestamp)) {
		return CW_FAILURE;
	}

	/* Add the mark to the receiver's representation buffer. */
	rec->representation[rec->representation_ind++] = mark;

	/* We just added a mark to the receiver's buffer.  As
	   above, if it's full, then we have to do something, even
	   though it's unlikely to actually be full. */
	if (rec->representation_ind == CW_REC_REPRESENTATION_CAPACITY - 1) {

		CW_REC_SET_STATE (rec, RS_EOC_GAP_ERR, (&cw_debug_object));

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: receiver's representation buffer is full");

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* Since we effectively just saw the end of a mark, move to
	   the inter-mark-space state. */
	CW_REC_SET_STATE (rec, RS_SPACE, (&cw_debug_object));

	return CW_SUCCESS;
}





/**
   \brief Add a dot to the receiver's representation buffer

   Documentation for both cw_receive_buffer_dot() and cw_receive_buffer_dash():

   Since we can't add a mark to the buffer without any
   accompanying timing information, the functions accepts \p timestamp
   of the "end of mark" event.  If the \p timestamp is NULL, the
   current timestamp is used.

   These routines are for client code that has already determined
   whether a dot or dash was received by a method other than calling
   the routines cw_start_receive_tone() and cw_end_receive_tone().

   On success, the relevant mark is added to the receiver's
   representation buffer.

   On failure, the routines return CW_FAILURE, with errno set to
   ERANGE if preceded by a cw_start_receive_tone() call with no matching
   cw_end_receive_tone() or if an error condition currently exists
   within the receiver's buffer, or ENOMEM if the receiver's
   representation buffer is full.

   \param timestamp - timestamp of "end of dot" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_buffer_dot(const struct timeval *timestamp)
{
	return cw_rec_add_mark_internal(&cw_receiver, timestamp, CW_DOT_REPRESENTATION);
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
	return cw_rec_add_mark_internal(&cw_receiver, timestamp, CW_DASH_REPRESENTATION);
}





/**
   \brief Get the current buffered representation from the receiver's representation buffer

   On success the function fills in \p representation with the
   contents of the current representation buffer and returns
   CW_SUCCESS.

   On failure, it returns CW_FAILURE and sets errno to:
   ERANGE if not preceded by a cw_end_receive_tone() call, a prior
   successful cw_receive_representation call, or a prior
   cw_receive_buffer_dot or cw_receive_buffer_dash,
   EINVAL if the timestamp passed in is invalid,
   EAGAIN if the call is made too early to determine whether a
   complete representation has yet been placed in the buffer (that is,
   less than the end-of-character gap period elapsed since the last
   cw_end_receive_tone() or cw_receive_buffer_dot/dash call). This is
   not a *hard* error, just an information that the caller should try
   to get the representation later.

   \p is_end_of_word indicates that the space after the last mark
   received is longer that the end-of-character gap, so it must be
   qualified as end-of-word gap.

   \p is_error indicates that the representation was terminated by an
   error condition.

   TODO: the function should be called cw_receiver_poll_representation().

   The function is called periodically (poll()-like function) by
   client code in hope that at some attempt receiver will be ready to
   pass \p representation. The attempt succeeds only if data stream is
   in "space" state. To mark end of the space, client code has to
   provide a timestamp (or pass NULL timestamp, the function will get
   time stamp at function call). The receiver needs to know the "end
   of space" event - thus the \p timestamp parameter.

   testedin::test_helper_receive_tests()

   \param timestamp - timestamp of event that ends "end-of-character" gap or "end-of-word" gap
   \param representation - buffer for representation (output parameter)
   \param is_end_of_word - buffer for "is end of word" state (output parameter)
   \param is_error - buffer for "error" state (output parameter)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_representation(const struct timeval *timestamp,
			      /* out */ char *representation,
			      /* out */ bool *is_end_of_word,
			      /* out */ bool *is_error)
{
	int rv = cw_rec_poll_representation_internal(&cw_receiver,
						     timestamp,
						     representation,
						     is_end_of_word,
						     is_error);

	return rv;
}





int cw_rec_poll_representation_internal(cw_rec_t *rec,
					const struct timeval *timestamp,
					/* out */ char *representation,
					/* out */ bool *is_end_of_word,
					/* out */ bool *is_error)
{
	if (rec->state == RS_EOW_GAP
	    || rec->state == RS_EOW_GAP_ERR) {

		/* Until receiver is notified about new mark, its
		   state won't change, and representation stored by
		   receiver's buffer won't change.

		   Repeated calls of the cw_receive_representation()
		   function when receiver is in this state will simply
		   return the same representation over and over again.

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
	cw_assert (rec->state == RS_SPACE
		   || rec->state == RS_EOC_GAP
		   || rec->state == RS_EOC_GAP_ERR,

		   "Unknown receiver state %d", rec->state);

	/* Stream of data is in one of these states
	   - inter-mark space, or
	   - end-of-character gap, or
	   - end-of-word gap.
	   To see which case is true, calculate length of this space
	   by comparing current/given timestamp with end of last
	   mark. */
	struct timeval now_timestamp;
	if (!cw_timestamp_validate_internal(&now_timestamp, timestamp)) {

		return CW_FAILURE;
	}

	int space_len = cw_timestamp_compare_internal(&rec->mark_end, &now_timestamp);
	if (space_len == INT_MAX) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: space len == INT_MAX");

		errno = EAGAIN;
		return CW_FAILURE;
	}

	/* Synchronize parameters if required */
	cw_rec_sync_parameters_internal(rec);

	if (space_len >= rec->eoc_len_min
	    && space_len <= rec->eoc_len_max) {

		fprintf(stderr, "EOC: space len = %d (%d - %d)\n", space_len, rec->eoc_len_min, rec->eoc_len_max);

		/* The space is, within tolerance, an end-of-character
		   gap.

		   We have a complete character representation in
		   receiver's buffer and we can return it. */
		cw_rec_poll_representation_eoc_internal(rec, space_len, representation, is_end_of_word, is_error);
		return CW_SUCCESS;

	} else if (space_len > rec->eoc_len_max) {

		fprintf(stderr, "EOW: space len = %d (> %d) ------------- \n", space_len, rec->eoc_len_max);

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





void cw_rec_poll_representation_eoc_internal(cw_rec_t *rec, int space_len,
					     /* out */ char *representation,
					     /* out */ bool *is_end_of_word,
					     /* out */ bool *is_error)
{
	if (rec->state == RS_SPACE) {
		/* State of receiver is inter-mark-space, but real
		   length of current space turned out to be a bit
		   longer than acceptable inter-mark-space. Update
		   length statistics for space identified as
		   end-of-character gap. */
		cw_rec_update_stats_internal(rec, CW_REC_STAT_ICHAR_SPACE, space_len);

		/* Transition of state of receiver. */
		CW_REC_SET_STATE (rec, RS_EOC_GAP, (&cw_debug_object));
	} else {
		/* We are already in RS_EOC_GAP or
		   RS_EOC_GAP_ERR, so nothing to do. */

		cw_assert (rec->state == RS_EOC_GAP || rec->state == RS_EOC_GAP_ERR,
			   "unexpected state of receiver: %d / %s",
			   rec->state, cw_receiver_states[rec->state]);
	}

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[rec->state]);

	/* Return the representation from receiver's buffer. */
	if (is_end_of_word) {
		fprintf(stderr, "1 eow = false\n");
		*is_end_of_word = false;
	}
	if (is_error) {
		*is_error = (rec->state == RS_EOC_GAP_ERR);
	}
	*representation = '\0'; /* TODO: why do this? */
	strncat(representation, rec->representation, rec->representation_ind);
	rec->representation[rec->representation_ind] = '\0';

	return;
}





void cw_rec_poll_representation_eow_internal(cw_rec_t *rec,
					     /* out */ char *representation,
					     /* out */ bool *is_end_of_word,
					     /* out */ bool *is_error)
{
	if (rec->state == RS_EOC_GAP || rec->state == RS_SPACE) {
		CW_REC_SET_STATE (rec, RS_EOW_GAP, (&cw_debug_object)); /* Transition of state. */

	} else if (rec->state == RS_EOC_GAP_ERR) {
		CW_REC_SET_STATE (rec, RS_EOW_GAP_ERR, (&cw_debug_object)); /* Transition of state with preserving error. */

	} else if (rec->state == RS_EOW_GAP_ERR || rec->state == RS_EOW_GAP) {
		; /* No need to change state. */

	} else {
		cw_assert (0, "unexpected receiver state %d / %s", rec->state, cw_receiver_states[rec->state]);
	}

	/* Return the representation from receiver's buffer. */
	if (is_end_of_word) {
		fprintf(stderr, "2 eow = true\n");
		*is_end_of_word = true;
	}
	if (is_error) {
		*is_error = (rec->state == RS_EOW_GAP_ERR);
	}
	*representation = '\0'; /* TODO: why do this? */
	strncat(representation, rec->representation, rec->representation_ind);
	rec->representation[rec->representation_ind] = '\0';

	return;
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
   than the end-of-character gap period elapsed since the last
   cw_end_receive_tone() or cw_receive_buffer_dot/dash call).
   ENOENT if character stored in receiver cannot be recognized as valid

   \p is_end_of_word indicates that the space after the last mark
   received is longer that the end-of-character gap, so it must be
   qualified as end-of-word gap.

   \p is_error indicates that the character was terminated by an error
   condition.

   testedin::test_helper_receive_tests()

   \param timestamp - timestamp of event that ends end-of-character gap or end-of-word gap
   \param c - buffer for character (output parameter)
   \param is_end_of_word - buffer for "is end of word" state (output parameter)
   \param is_error - buffer for "error" state (output parameter)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_character(const struct timeval *timestamp,
			 /* out */ char *c,
			 /* out */ bool *is_end_of_word,
			 /* out */ bool *is_error)
{
	int rv = cw_rec_poll_character_internal(&cw_receiver, timestamp, c, is_end_of_word, is_error);
	return rv;
}





int cw_rec_poll_character_internal(cw_rec_t *rec,
				   const struct timeval *timestamp,
				   /* out */ char *c,
				   /* out */ bool *is_end_of_word,
				   /* out */ bool *is_error)
{
	/* TODO: in theory we don't need these intermediate bool
	   variables, since is_end_of_word and is_error won't be
	   modified by any function on !success. */
	bool end_of_word, error;

	char representation[CW_REC_REPRESENTATION_CAPACITY + 1];

	/* See if we can obtain a representation from receiver. */
	int status = cw_rec_poll_representation_internal(rec, timestamp,
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
   internal state. This prepares the receiver to receive marks and
   spaces again.

   This routine must be called after successful, or terminating,
   cw_receive_representation() or cw_receive_character() calls, to
   clear the states and prepare the buffer to receive more marks and spaces.
*/
void cw_clear_receive_buffer(void)
{
	cw_rec_clear_buffer_internal(&cw_receiver);

	return;
}





void cw_rec_clear_buffer_internal(cw_rec_t *rec)
{
	rec->representation_ind = 0;
	CW_REC_SET_STATE (rec, RS_IDLE, (&cw_debug_object));

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
	int rv = cw_rec_get_buffer_length_internal(&cw_receiver);
	return rv;
}





int cw_rec_get_buffer_length_internal(cw_rec_t *rec)
{
	return rec->representation_ind;
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
	CW_REC_SET_STATE ((&cw_receiver), RS_IDLE, (&cw_debug_object));

	cw_reset_receive_statistics();

	return;
}





/**
  \brief Reset essential receive parameters to their initial values
*/
void cw_rec_reset_receive_parameters_internal(cw_rec_t *rec)
{
	cw_assert (rec, "receiver is NULL");

	rec->speed = CW_SPEED_INITIAL;
	rec->tolerance = CW_TOLERANCE_INITIAL;
	rec->is_adaptive_receive_mode = CW_REC_ADAPTIVE_MODE_INITIAL;
	rec->noise_spike_threshold = CW_REC_NOISE_THRESHOLD_INITIAL;

	/* FIXME: consider resetting ->gap as well. */

	rec->parameters_in_sync = false;

	return;
}





void cw_rec_sync_parameters_internal(cw_rec_t *rec)
{
	cw_assert (rec, "receiver is NULL");

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



	/* Calculate the basic receiver's dot and dash lengths. */
	rec->dot_len_ideal = unit_len;
	rec->dash_len_ideal = 3 * unit_len;
	/* For statistical purposes, calculate the ideal "end of mark"
	   and "end of character" lengths, too. */
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

		/* Any mark longer than dot is a dash in adaptive
		   receiving mode. */

		/* FIXME: shouldn't this be '= rec->dot_len_max + 1'?
		   now the length ranges for dot and dash overlap. */
		rec->dash_len_min = rec->dot_len_max;
		rec->dash_len_max = INT_MAX;

		int debug_eoc_len_max = rec->eoc_len_max;

		/* Make the inter-mark space be anything up to the
		   adaptive threshold lengths - that is two dots.  And
		   the end-of-character gap is anything longer than
		   that, and shorter than five dots. */
		rec->eom_len_min = rec->dot_len_min;
		rec->eom_len_max = rec->dot_len_max;
		rec->eoc_len_min = rec->eom_len_max;
		rec->eoc_len_max = 5 * rec->dot_len_ideal;

		if (debug_eoc_len_max != rec->eoc_len_max) {
			fprintf(stderr, "eoc_len_max changed from %d to %d --------\n", debug_eoc_len_max, rec->eoc_len_max);
		}

	} else {
		/* Fixed speed receiving mode. */

		/* 'int tolerance' is in [%]. */
		int tolerance = (rec->dot_len_ideal * rec->tolerance) / 100;
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

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_PARAMETERS, CW_DEBUG_INFO,
		      "libcw: receive usec timings <%.2f [wpm]>: dot: %d-%d [ms], dash: %d-%d [ms], %d-%d[%d], %d-%d[%d], thres: %d [us]",
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





#ifdef LIBCW_UNIT_TESTS





#include "libcw_gen.h"





#define TEST_CW_REC_DATA_LEN_MAX 30 /* There is no character that would have data that long. */
struct cw_rec_test_data {
	char c;                           /* Character. */
	char *r;                          /* Character's representation (dots and dashes). */
	float s;                          /* Send speed (speed at which the character is incoming). */
	int d[TEST_CW_REC_DATA_LEN_MAX];  /* Data - time information for marks and spaces. */
	int nd;                           /* Length of data. */

	bool is_last_in_word;              /* Is this character a last character in a word? (is it followed by end-of-word space?) */
};





static struct cw_rec_test_data *test_cw_rec_new_data(const char *characters, float speeds[], int fuzz_percent);
static struct cw_rec_test_data *test_cw_rec_new_base_data_fixed(int speed, int fuzz_percent);
static struct cw_rec_test_data *test_cw_rec_new_random_data_fixed(int speed, int fuzz_percent);
static struct cw_rec_test_data *test_cw_rec_new_random_data_adaptive(int speed_min, int speed_max, int fuzz_percent);

static void                     test_cw_rec_delete_data(struct cw_rec_test_data **data);
static void                     test_cw_rec_print_data(struct cw_rec_test_data *data);
static void                     test_cw_rec_test_begin_end(cw_rec_t *rec, struct cw_rec_test_data *data);

/* Functions creating tables of test values: characters and speeds.
   Characters and speeds will be combined into test (timing) data. */
static char  *test_cw_rec_new_base_characters(void);
static char  *test_cw_rec_new_random_characters(int n);
static float *test_cw_rec_new_speeds_fixed(int speed, size_t n);
static float *test_cw_rec_new_speeds_adaptive(int speed_min, int speed_max, size_t n);





/**
   tests::cw_rec_identify_mark_internal()

   Test if function correctly recognizes dots and dashes for a range
   of receive speeds.  This test function also checks if marks of
   lengths longer or shorter than certain limits (dictated by
   receiver) are handled properly (i.e. if they are recognized as
   invalid marks).

   Currently the function only works for non-adaptive receiving.
*/
unsigned int test_cw_rec_identify_mark_internal(void)
{
	int p = fprintf(stderr, "libcw: cw_rec_identify_mark_internal() (non-adaptive):");

	cw_disable_adaptive_receive();

	cw_generator_new(CW_AUDIO_NULL, "null");

	int speed_step = (CW_SPEED_MAX - CW_SPEED_MIN) / 10;

	for (int i = CW_SPEED_MIN; i < CW_SPEED_MAX; i += speed_step) {
		cw_set_receive_speed(i);

		char representation;
		int rv;


		/* Test marks of length within allowed lengths of dots. */
		int len_step = (cw_receiver.dot_len_max - cw_receiver.dot_len_min) / 10;
		for (int j = cw_receiver.dot_len_min; j < cw_receiver.dot_len_max; j += len_step) {
			rv = cw_rec_identify_mark_internal(&cw_receiver, j, &representation);
			cw_assert (rv, "failed to identify dot for speed = %d [wpm], len = %d [us]", i, j);

			cw_assert (representation == CW_DOT_REPRESENTATION, "got something else than dot for speed = %d [wpm], len = %d [us]", i, j);
		}

		/* Test mark shorter than minimal length of dot. */
		rv = cw_rec_identify_mark_internal(&cw_receiver, cw_receiver.dot_len_min - 1, &representation);
		cw_assert (!rv, "incorrectly identified short mark as a dot for speed = %d [wpm]", i);

		/* Test mark longer than maximal length of dot (but shorter than minimal length of dash). */
		rv = cw_rec_identify_mark_internal(&cw_receiver, cw_receiver.dot_len_max + 1, &representation);
		cw_assert (!rv, "incorrectly identified long mark as a dot for speed = %d [wpm]", i);




		/* Test marks of length within allowed lengths of dashes. */
		len_step = (cw_receiver.dash_len_max - cw_receiver.dash_len_min) / 10;
		for (int j = cw_receiver.dash_len_min; j < cw_receiver.dash_len_max; j += len_step) {
			rv = cw_rec_identify_mark_internal(&cw_receiver, j, &representation);
			cw_assert (rv, "failed to identify dash for speed = %d [wpm], len = %d [us]", i, j);

			cw_assert (representation == CW_DASH_REPRESENTATION, "got something else than dash for speed = %d [wpm], len = %d [us]", i, j);
		}

		/* Test mark shorter than minimal length of dash (but longer than maximal length of dot). */
		rv = cw_rec_identify_mark_internal(&cw_receiver, cw_receiver.dash_len_min - 1, &representation);
		cw_assert (!rv, "incorrectly identified short mark as a dash for speed = %d [wpm]", i);

		/* Test mark longer than maximal length of dash. */
		rv = cw_rec_identify_mark_internal(&cw_receiver, cw_receiver.dash_len_max + 1, &representation);
		cw_assert (!rv, "incorrectly identified long mark as a dash for speed = %d [wpm]", i);
	}



	cw_generator_delete();

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/* Test a receiver with small and simple set of all characters
   supported by libcw. The test is done with fixed speed. */
unsigned int test_cw_rec_with_base_data_fixed(void)
{
	for (int speed = CW_SPEED_MIN; speed <= CW_SPEED_MAX; speed++) {
		struct cw_rec_test_data *data = test_cw_rec_new_base_data_fixed(speed, 0);
		//test_cw_rec_print_data(data);

		/* Reset. */
		cw_reset_receive();
		cw_clear_receive_buffer();

		cw_set_receive_speed(speed);
		cw_disable_adaptive_receive();

		cw_assert (cw_get_receive_speed() == speed, "incorrect receive speed: %d != %d", cw_get_receive_speed(), speed);

		/* Actual tests of receiver functions are here. */
		test_cw_rec_test_begin_end(&cw_receiver, data);


		test_cw_rec_delete_data(&data);
	}

	return 0;
}





/**
   \brief The core test function, testing receiver's "begin" and "end" functions

   As mentioned in file's top-level comment, there are two main
   methods to add data to receiver. This function tests first method:
   using cw_start_receive_tone() and cw_end_receive_tone() functions
   (or cw_rec_mark_begin_internal() and cw_rec_mark_end_internal()
   functions that are used to implement them).

   Other helper functions are used/tested here as well, because adding
   marks and spaces to receiver is just half of the job necessary to
   receive Morse code. You have to interpret the marks and spaces,
   too.

   \param rec - receiver variable used during tests
   \param data - table with timings, used to test the receiver
*/
void test_cw_rec_test_begin_end(cw_rec_t *rec, struct cw_rec_test_data *data)
{
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	for (int i = 0; data[i].r; i++) {

		printf("\nlibcw: input test data #%d: <%c> / <%s> @ %.2f [wpm] (%d time values)\n",
		       i, data[i].c, data[i].r, data[i].s, data[i].nd);

		/* Start sending every character at the beginning of a
		   new second.

		   TODO: here we make an assumption that every
		   character is sent in less than a second. Which is a
		   good assumption when we have a speed of tens of
		   WPM. If the speed will be lower, the assumption
		   will be false. */
		//tv.tv_sec = 0;
		//tv.tv_usec = 0;

		/* This loop simulates "key down" and "key up" events
		   in specific moments, and in specific time
		   intervals.

		   key down -> call to cw_start_receive_tone()
		   key up -> call to cw_end_receive_tone().

		   First "key down" event is at 0 seconds 0
		   microseconds. Time of every following event is
		   calculated by iterating over tone lengths specified
		   in data table. */
		int tone;
		for (tone = 0; data[i].d[tone] > 0; tone++) {

			/* Here we just assume that
			   cw_rec_mark_bein{start|end}_receive_tone() functions just
			   work. No checking of return values. */
			if (tone % 2) {
				cw_assert (cw_rec_mark_end_internal(rec, &tv),
					   "cw_rec_mark_end_internal(): %d.%d", (int) tv.tv_sec, (int) tv.tv_usec);
			} else {
				cw_assert (cw_rec_mark_begin_internal(rec, &tv),
					   "cw_rec_mark_begin_internal(): %d.%d", (int) tv.tv_sec, (int) tv.tv_usec);
			}

			tv.tv_usec += data[i].d[tone];
			if (tv.tv_usec >= CW_USECS_PER_SEC) {
				/* Moving event to next second. */
				tv.tv_sec += tv.tv_usec / CW_USECS_PER_SEC;
				tv.tv_usec %= CW_USECS_PER_SEC;

			}
			/* If we exit the loop at this point, the last
			   'tv' with length of end-of-character space
			   will be used below in
			   cw_receive_representation(). */
		}





		/* Test: length of receiver's buffer (only marks!)
		   after adding a representation of a single character
		   to receiver's buffer. */
		{
			int n = cw_rec_get_buffer_length_internal(rec);
			cw_assert (n == (int) strlen(data[i].r),
				   "cw_rec_get_buffer_length_internal() <nonempty>:  %d != %zd",
				   n, strlen(data[i].r));
		}




		/* Test: getting representation from receiver's buffer. */
		char representation[CW_REC_REPRESENTATION_CAPACITY + 1];
		{
			/* Get representation (dots and dashes)
			   accumulated by receiver. Check for
			   errors. */

			bool is_word, is_error;

			/* Notice that we call the function with last
			   timestamp (tv) from input data. The last
			   timestamp in the input data represents end
			   of final space (end-of-character space).

			   With this final passing of "end of space"
			   timestamp to libcw the test code informs
			   receiver, that end-of-character space has
			   occurred, i.e. a full character has been
			   passed to receiver.

			   The space length in input data is (3 x dot
			   + jitter). In libcw maximum recognizable
			   length of "end of character" space is 5 x
			   dot. */
			cw_assert (cw_rec_poll_representation_internal(rec, &tv, representation, &is_word, &is_error),
				   "cw_rec_poll_representation_internal() returns false");

			cw_assert (strcmp(representation, data[i].r) == 0,
				   "cw_rec_poll_representation_internal(): polled representation does not match test representation:" \
				   "\"%s\"   !=   \"%s\"", representation, data[i].r);

			cw_assert (!is_error,
				   "cw_rec_poll_representation_internal() sets is_error to true");

			/* If the last space in character's data is
			   end-of-word space (which is indicated by
			   is_last_in_word), then is_word should be
			   set by poll() to true. Otherwise both
			   values should be false. */
			cw_assert (is_word == data[i].is_last_in_word,
				   "'is_word' flag error: function returns '%d', data is tagged with '%d'\n" \
				   "'%c'  '%c'  '%c'  '%c'  '%c'",
				   is_word, data[i].is_last_in_word,
				   data[i - 2].c, data[i - 1].c, data[i].c, data[i + 1].c, data[i + 2].c );

#if 0
			/* Debug code. Print times of character with
			   end-of-word space to verify length of the
			   space. */
			if (data[i].is_last_in_word) {
				fprintf(stderr, "------- character '%c' is last in word\n", data[i].c);
				for (int m = 0; m < data[i].nd; m++) {
					fprintf(stderr, "#%d: %d\n", m, data[i].d[m]);
				}
			}
#endif

		}





		char c;
		/* Test: getting character from receiver's buffer. */
		{
			bool is_word, is_error;

			/* The representation is still held in
			   receiver. Ask receiver for converting the
			   representation to character. */
			cw_assert (cw_rec_poll_character_internal(rec, &tv, &c, &is_word, &is_error),
				   "cw_rec_poll_character_internal() returns false");

			cw_assert (c == data[i].c,
				   "cw_rec_poll_character_internal(): polled character does not match test character:" \
				   "'%c' != '%c':", c, data[i].c);
		}







		/* Test: getting length of receiver's representation
		   buffer after cleaning the buffer. */
		{
			/* We have a copy of received representation,
			   we have a copy of character. The receiver
			   no longer needs to store the
			   representation. If I understand this
			   correctly, the call to clear() is necessary
			   to prepare the receiver for receiving next
			   character. */
			cw_rec_clear_buffer_internal(rec);
			int length = cw_rec_get_buffer_length_internal(rec);
			cw_assert (length == 0,
				   "cw_get_receive_buffer_length(): length of cleared buffer is non zero (is %d)",
				   length);
		}


		float speed = cw_rec_get_speed_internal(rec);


		printf("libcw: received data #%d:   <%c> / <%s> @ %.2f [wpm]\n",
		       i, c, representation, speed);

#if 0
		if (adaptive) {
			printf("libcw: adaptive speed tracking reports %d wpm\n",
			       );
		}
#endif
	}


	return;
}





/* Generate small test data set with all characters supported by libcw
   and a fixed speed. */
struct cw_rec_test_data *test_cw_rec_new_base_data_fixed(int speed, int fuzz_percent)
{
	/* All characters supported by libcw.  Don't use
	   get_characters_random(): for this test get a small table of
	   all characters supported by libcw. This should be a quick
	   test, and it should cover all characters. */
	char *base_characters = test_cw_rec_new_base_characters();
	cw_assert (base_characters, "test_cw_rec_new_base_characters() failed");


	size_t n = strlen(base_characters);


	/* Fixed speed receive mode - speed is constant for all
	   characters. */
	float *speeds = test_cw_rec_new_speeds_fixed(speed, n);
	cw_assert (speeds, "test_cw_rec_new_speeds_fixed() failed");


	/* Generate timing data for given set of characters, each
	   character is sent with speed dictated by speeds[]. */
	struct cw_rec_test_data *data = test_cw_rec_new_data(base_characters, speeds, fuzz_percent);
	cw_assert (data, "failed to get test data");


	free(base_characters);
	base_characters = NULL;

	free(speeds);
	speeds = NULL;

	return data;
}





/* Test a receiver with large set of random data. The test is done
   with fixed speed. */
unsigned int test_cw_rec_with_random_data_fixed(void)
{
	for (int speed = CW_SPEED_MIN; speed <= CW_SPEED_MAX; speed++) {
		struct cw_rec_test_data *data = test_cw_rec_new_random_data_fixed(speed, 0);
		//test_cw_rec_print_data(data);

		/* Reset. */
		cw_reset_receive();
		cw_clear_receive_buffer();

		cw_set_receive_speed(speed);
		cw_disable_adaptive_receive();

		cw_assert (cw_get_receive_speed() == speed, "incorrect receive speed: %d != %d", cw_get_receive_speed(), speed);

		/* Actual tests of receiver functions are here. */
		test_cw_rec_test_begin_end(&cw_receiver, data);


		test_cw_rec_delete_data(&data);
	}

	return 0;
}





/* Test a receiver with large set of random data. The test is done
   with varying speed. */
unsigned int test_cw_rec_with_random_data_adaptive(void)
{
	struct cw_rec_test_data *data = test_cw_rec_new_random_data_adaptive(CW_SPEED_MIN, CW_SPEED_MAX, 0);
	//test_cw_rec_print_data(data);

	/* Reset. */
	cw_reset_receive();
	cw_clear_receive_buffer();

	cw_set_receive_speed(CW_SPEED_MAX);
	cw_enable_adaptive_receive();

	/* Actual tests of receiver functions are here. */
	test_cw_rec_test_begin_end(&cw_receiver, data);


	test_cw_rec_delete_data(&data);

	return 0;
}





/* This function generates a large set of data using characters from
   base set.  The characters in data are randomized and space
   characters are added.  Size of data set is tens of times larger
   than for base data. Speed of data is constant for all
   characters. */
struct cw_rec_test_data *test_cw_rec_new_random_data_fixed(int speed, int fuzz_percent)
{
	int n = cw_get_character_count() * 30;

	char *characters = test_cw_rec_new_random_characters(n);
	cw_assert (characters, "test_cw_rec_new_random_characters() failed");

	/* Fixed speed receive mode - speed is constant for all
	   characters. */
	float *speeds = test_cw_rec_new_speeds_fixed(speed, n);
	cw_assert (speeds, "test_cw_rec_new_speeds_fixed() failed");


	/* Generate timing data for given set of characters, each
	   character is sent with speed dictated by speeds[]. */
	struct cw_rec_test_data *data = test_cw_rec_new_data(characters, speeds, fuzz_percent);
	cw_assert (data, "failed to get test data");


	free(characters);
	characters = NULL;

	free(speeds);
	speeds = NULL;

	return data;
}





/* This function generates a large set of data using characters from
   base set.  The characters in data are randomized and space
   characters are added.  Size of data set is tens of times larger
   than for base data.

   Speed of data is varying. */
struct cw_rec_test_data *test_cw_rec_new_random_data_adaptive(int speed_min, int speed_max, int fuzz_percent)
{
	int n = cw_get_character_count() * 30;

	char *characters = test_cw_rec_new_random_characters(n);
	cw_assert (characters, "test_cw_rec_new_random_characters() failed");

	/* Adaptive speed receive mode - speed varies for all
	   characters. */
	float *speeds = test_cw_rec_new_speeds_adaptive(speed_min, speed_max, n);
	cw_assert (speeds, "test_cw_rec_new_speeds_adaptive() failed");


	/* Generate timing data for given set of characters, each
	   character is sent with speed dictated by speeds[]. */
	struct cw_rec_test_data *data = test_cw_rec_new_data(characters, speeds, fuzz_percent);
	cw_assert (data, "failed to get test data");


	free(characters);
	characters = NULL;

	free(speeds);
	speeds = NULL;

	return data;
}





/**
   \brief Get a string with all characters supported by libcw

   Function allocates and returns a string with all characters that are supported/recognized by libcw.

   \return allocated string.
*/
char *test_cw_rec_new_base_characters(void)
{
	int n = cw_get_character_count();
	char *base_characters = (char *) malloc((n + 1) * sizeof (char));
	cw_assert (base_characters, "malloc() failed");
	cw_list_characters(base_characters);

	return base_characters;
}





/**
   \brief Generate a set of characters of size \p n.

   Function allocates and returns a string of \p n characters. The
   characters are randomly drawn from set of all characters supported
   by libcw.

   Spaces are added to the string in random places to mimic a regular
   text. Function makes sure that there are no consecutive spaces (two
   or more) in the string.

   \param n - number of characters in output string

   \return string of random characters (including spaces)
*/
char *test_cw_rec_new_random_characters(int n)
{
	/* All characters supported by libcw - this will be an input
	   set of all characters. */
	char *base_characters = test_cw_rec_new_base_characters();
	cw_assert (base_characters, "test_cw_rec_new_base_characters() failed");
	size_t length = strlen(base_characters);


	char *characters = (char *) malloc ((n + 1) * sizeof (char));
	cw_assert (characters, "malloc() failed");
	for (int i = 0; i < n; i++) {
		int r = rand() % length;
		if (!(r % 3)) {
			characters[i] = ' ';

			/* To prevent two consecutive spaces. */
			i++;
			characters[i] = base_characters[r];
		} else {
			characters[i] = base_characters[r];
		}
	}

	/* First character in input data can't be a space - we can't
	   start a receiver's state machine with space. Also when a
	   end-of-word space appears in input character set, it is
	   added as last time value at the end of time values table
	   for "previous char". We couldn't do this for -1st char. */
	characters[0] = 'K'; /* Use capital letter. libcw uses capital letters internally. */

	characters[n] = '\0';

	fprintf(stderr, "%s\n", characters);


	free(base_characters);
	base_characters = NULL;


	return characters;
}





/**
   \brief Generate a table of fixed speeds

   Function allocates and returns a table of speeds of constant value
   specified by \p speed. There are \p n valid (non-negative) values
   in the table. After the last valid value there is a small negative
   value that acts as a guard.

   \param speed - a constant value to be used as initializer of the table
   \param n - size of table (+1 for guard)

   \return table of speeds of constant value
*/
float *test_cw_rec_new_speeds_fixed(int speed, size_t n)
{
	cw_assert (speed > 0, "speed must be larger than zero");

	/* Fixed speed receive mode - speed is constant for all
	   characters. */
	float *speeds = (float *) malloc((n + 1) * sizeof (float));
	cw_assert (speeds, "malloc() failed");
	for (size_t i = 0; i < n; i++) {
		speeds[i] = (float) speed;
	}

	speeds[n] = -1.0;

	return speeds;
}





/**
   \brief Generate a table of varying speeds

   Function allocates and returns a table of speeds of varying values,
   changing between \p speed_min and \p speed_max. There are \p n
   valid (non-negative) values in the table. After the last valid
   value there is a small negative value that acts as a guard.

   \param speed_min - minimal speed
   \param speed_max - maximal speed
   \param n - size of table (+1 for guard)

   \return table of speeds
*/
float *test_cw_rec_new_speeds_adaptive(int speed_min, int speed_max, size_t n)
{
	cw_assert (speed_min > 0, "speed_min must be larger than zero");
	cw_assert (speed_max > 0, "speed_max must be larger than zero");
	cw_assert (speed_min <= speed_max, "speed_min can't be larger than speed_max");

	/* Adaptive speed receive mode - speed is constant for all
	   characters. */
	float *speeds = (float *) malloc((n + 1) * sizeof (float));
	cw_assert (speeds, "malloc() failed");

	for (size_t i = 0; i < n; i++) {
		float t = (1.0 * i) / n;

		speeds[i] = (1 + cosf(2 * 3.1415 * t)) / 2.0; /* 0.0 -  1.0 */
		speeds[i] *= (speed_max - speed_min);         /* 0.0 - 56.0 */
		speeds[i] += speed_min;                       /* 4.0 - 60.0 */

		// fprintf(stderr, "%f\n", speeds[i]);
	}

	speeds[n] = -1.0;

	return speeds;
}





/**
   \brief Create timing data used for testing a receiver

   This is a generic function that can generate different sets of data
   depending on input parameters. It is to be used by wrapper
   functions that first specify parameters of test data, and then pass
   the parameters to this function.

   The function allocates a table with timing data (and some other
   data as well) that can be used to test receiver's functions that
   accept timestamp argument.

   All characters in \p characters must be valid (i.e. they must be
   accepted by cw_character_is_valid()).

   All values in \p speeds must be valid (i.e. must be between
   CW_SPEED_MIN and CW_SPEED_MAX, inclusive).

   Size of \p characters and \p speeds must be equal.

   The data is valid and represents valid Morse representations.  If
   you want to generate invalid data or to generate data based on
   invalid representations, you have to use some other function.

   For each character the last timing parameter represents
   end-of-character space or end-of-word space. The next timing
   parameter after the space is zero. For character 'A' that would
   look like this:

   .-    ==   40000 (dot); 40000 (space); 120000 (dash); 240000 (end-of-word space); 0 (guard, zero timing)

   Last element in the created table (a guard "pseudo-character") has
   'r' field set to NULL.

   Use test_cw_rec_delete_data() to deallocate the timing data table.

   \brief characters - list of characters for which to generate table with timing data
   \brief speeds - list of speeds (per-character)

   \return table of timing data sets
*/
struct cw_rec_test_data *test_cw_rec_new_data(const char *characters, float speeds[], int fuzz_percent)
{
	size_t n = strlen(characters);
	/* +1 for guard. */
	struct cw_rec_test_data *test_data = (struct cw_rec_test_data *) malloc((n + 1) * sizeof(struct cw_rec_test_data));
	cw_assert (test_data, "malloc() failed");

	size_t j = 0; /* For indexing output data table. */
	for (size_t i = 0; i < n; i++) {

		int unit_len = CW_DOT_CALIBRATION / speeds[i]; /* Dot length, [us]. Used as basis for other elements. */
		// fprintf(stderr, "unit_len = %d [us] for speed = %d [wpm]\n", unit_len, speed);


		/* First handle a special case: end-of-word
		   space. This long space will be put at the end of
		   table of time values for previous
		   representation. */
		if (characters[i] == ' ') {
			test_data[j - 1].d[test_data[j - 1].nd - 1] = unit_len * 6; /* unit_len * 5 is the minimal end-of-word space. */

			test_data[j - 1].is_last_in_word = true;

			continue;
		} else {
			/* A regular character, handled below. */
		}


		test_data[j].c = characters[i];
		test_data[j].r = cw_character_to_representation(test_data[j].c);
		cw_assert (test_data[j].r,
			   "cw_character_to_representation() failed for input char #%zd: '%c'\n",
			   i, characters[i]);
		test_data[j].s = speeds[i];


		/* Build table of times for given representation. */


		size_t nd = 0;

		size_t rep_length = strlen(test_data[j].r);
		for (size_t k = 0; k < rep_length; k++) {

			/* Length of mark. */
			if (test_data[j].r[k] == CW_DOT_REPRESENTATION) {
				test_data[j].d[nd] = unit_len;

			} else if (test_data[j].r[k] == CW_DASH_REPRESENTATION) {
				test_data[j].d[nd] = unit_len * 3;

			} else {
				cw_assert (0, "unknown char in representation: '%c'\n", test_data[j].r[k]);
			}
			nd++;


			/* Length of space (inter-mark space). Mark
			   and space always go in pair. */
			test_data[j].d[nd] = unit_len;
			nd++;
		}

		cw_assert (nd > 0, "number of times is %zd for representation '%s'\n", nd, test_data[j].r);

		test_data[j].d[nd - 1] = (unit_len * 3) + (unit_len / 2);  /* end-of-character space. */
		test_data[j].d[nd] = 0; /* Guard. */


		/* Mark and space always go in pair. */
		cw_assert (! (nd % 2), "number of times is not even");
		/* Mark/space pair per each dot or dash. */
		cw_assert (nd == 2 * rep_length, "number of times incorrect: %zd != 2 * %zd\n", nd, rep_length);


		test_data[j].nd = (size_t ) nd;

		/* This may be overwritten by this function when a
		   space is encountered in input string. */
		test_data[j].is_last_in_word = false;

		j++;
	}


	/* Guard. */
	test_data[n].r = (char *) NULL;


	return test_data;
}





/**
   \brief Deallocate timing data used for testing a receiver

   \param data - pointer to data to be deallocated
*/
void test_cw_rec_delete_data(struct cw_rec_test_data **data)
{
	int i = 0;
	while ((*data)[i].r) {
		free((*data)[i].r);
		(*data)[i].r = (char *) NULL;

		i++;
	}

	free(*data);
	*data = NULL;

	return;
}





/**
   \brief Pretty-print timing data used for testing a receiver

   \param data timing data to be printed
*/
void test_cw_rec_print_data(struct cw_rec_test_data *data)
{
	int i = 0;

	fprintf(stderr, "---------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	while (data[i].r) {
		/* Debug output. */
		if (!(i % 10)) {
			/* Print header. */
			fprintf(stderr, "char  repr      [wpm]    mark     space      mark     space      mark     space      mark     space      mark     space      mark     space      mark     space\n");
		}
		fprintf(stderr, "%c     %-7s  %02.2f", data[i].c, data[i].r, data[i].s);
		for (int j = 0; j < data[i].nd; j++) {
			fprintf(stderr, "%9d ", data[i].d[j]);
		}
		fprintf(stderr, "\n");

		i++;
	}

	return;
}





#endif /* #ifdef LIBCW_UNIT_TESTS */
