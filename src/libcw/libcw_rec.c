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




#include "libcw_rec.h"
#include "libcw_data.h"
#include "libcw_utils.h"
#include "libcw_debug.h"
#include "libcw_test.h"




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




/* Does receiver initially adapt to varying speed of input data? */
enum { CW_REC_ADAPTIVE_MODE_INITIAL = false };


/* TODO: it would be interesting to track (in debug mode) relationship
   between "speed threshold" and "noise threshold" parameters. */
enum { CW_REC_SPEED_THRESHOLD_INITIAL = (CW_DOT_CALIBRATION / CW_SPEED_INITIAL) * 2 };    /* Initial adaptive speed threshold. [us] */
enum { CW_REC_NOISE_THRESHOLD_INITIAL = (CW_DOT_CALIBRATION / CW_SPEED_MAX) / 2 };        /* Initial noise filter threshold. */


/* Receiver contains a fixed-length buffer for representation of
   received data.  Capacity of the buffer is vastly longer than any
   practical representation.  Don't know why, a legacy thing.

   Representation can be presented as a char string. This is the
   maximal length of the string. This value does not include string's
   ending NULL. */
enum { CW_REC_REPRESENTATION_CAPACITY = 256 };


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

static const char * cw_receiver_states[] = {
	"RS_IDLE",
	"RS_MARK",
	"RS_SPACE",
	"RS_EOC_GAP",
	"RS_EOW_GAP",
	"RS_EOC_GAP_ERR",
	"RS_EOW_GAP_ERR"
};




/* A moving averages structure - circular buffer. Used for calculating
   averaged length ([us]) of Dots and Dashes. */
typedef struct {
	int buffer[CW_REC_AVERAGING_ARRAY_LENGTH];  /* Buffered mark lengths. */
	int cursor;                                 /* Circular buffer cursor. */
	int sum;                                    /* Running sum of lengths of marks. [us] */
	int average;                                /* Averaged length of a mark. [us] */
} cw_rec_averaging_t;




typedef struct {
	stat_type_t type;  /* Record type */
	int delta;         /* Difference between actual and ideal length of mark or space. [us] */
} cw_rec_statistics_t;




struct cw_rec_struct {

	/* State of receiver state machine. */
	int state;



	/* Essential parameters. */
	/* Changing values of speed, tolerance, gap or
	   is_adaptive_receive_mode will trigger a recalculation of
	   low level timing parameters. */

	/* 'speed' is float instead of being 'int' on purpose.  It
	   makes adaptation to varying speed of incoming data more
	   smooth. This is especially important at low speeds, where
	   change/adaptation from (int) 5wpm to (int) 4wpm would
	   mean a sharp decrease by 20%. With 'float' data type the
	   adjustment of receive speeds is more gradual. */
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
	int adaptive_speed_threshold; /* [us] */



	/* Retained timestamps of mark's begin and end. */
	struct timeval mark_start;
	struct timeval mark_end;

	/* Buffer for received representation (Dots/Dashes). This is a
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
	int dot_len_ideal;        /* Length of an ideal dot. [us] */
	int dot_len_min;          /* Minimal length of mark that will be identified as dot. [us] */
	int dot_len_max;          /* Maximal length of mark that will be identified as dot. [us] */

	int dash_len_ideal;       /* Length of an ideal dash. [us] */
	int dash_len_min;         /* Minimal length of mark that will be identified as dash. [us] */
	int dash_len_max;         /* Maximal length of mark that will be identified as dash. [us] */

	int eom_len_ideal;        /* Ideal end of mark, for stats. [us] */
	int eom_len_min;          /* Shortest end of mark allowable. [us] */
	int eom_len_max;          /* Longest end of mark allowable. [us] */

	int eoc_len_ideal;        /* Ideal end of char, for stats. [us] */
	int eoc_len_min;          /* Shortest end of char allowable. [us] */
	int eoc_len_max;          /* Longest end of char allowable. [us] */

	/* These two fields have the same function as in
	   cw_gen_t. They are needed in function re-synchronizing
	   parameters. */
	int additional_delay;     /* More delay at the end of a char. [us] */
	int adjustment_delay;     /* More delay at the end of a word. [us] */



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



	/* Data structures for calculating averaged length of Dots and
	   Dashes. The averaged lengths are used for adaptive tracking
	   of receiver's speed (tracking of speed of incoming data). */
	cw_rec_averaging_t dot_averaging;
	cw_rec_averaging_t dash_averaging;

#ifdef WITH_EXPERIMENTAL_RECEIVER
	cw_rec_push_callback_t * push_callback;
#endif

	/* Flag indicating if receive polling has received a
	   character, and may need to augment it with a word
	   space on a later poll. */
	bool is_pending_inter_word_space;
};




/* Receive and identify a mark. */
static int cw_rec_identify_mark_internal(cw_rec_t * rec, int mark_len, char * representation);



/* Functions handling receiver statistics. */
static void   cw_rec_update_stats_internal(cw_rec_t * rec, stat_type_t type, int len);
static double cw_rec_get_stats_internal(cw_rec_t * rec, stat_type_t type);


/* Functions handling averaging data structure in adaptive receiving
   mode. */
static void cw_rec_update_average_internal(cw_rec_averaging_t * avg, int mark_len);
static void cw_rec_update_averages_internal(cw_rec_t * rec, int mark_len, char mark);
static void cw_rec_reset_average_internal(cw_rec_averaging_t * avg, int initial);


static int  cw_rec_set_gap(cw_rec_t * rec, int new_value);
static void cw_rec_set_adaptive_mode_internal(cw_rec_t * rec, bool adaptive);
static void cw_rec_poll_representation_eoc_internal(cw_rec_t * rec, int space_len, char * representation, bool * is_end_of_word, bool * is_error);
static void cw_rec_poll_representation_eow_internal(cw_rec_t * rec, char *representation, bool * is_end_of_word, bool * is_error);




/**
   \brief Allocate and initialize new receiver variable

   Before returning, the function calls
   cw_rec_sync_parameters_internal() for the receiver.

   Function may return NULL on malloc() failure.

   testedin::test_cw_rec_identify_mark_internal()

   \reviewed on 2017-02-02

   \return freshly allocated, initialized and synchronized receiver on success
   \return NULL pointer on failure
*/
cw_rec_t * cw_rec_new(void)
{
	cw_rec_t * rec = (cw_rec_t *) malloc(sizeof (cw_rec_t));
	if (!rec) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw/rec: new: malloc()");
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
	cw_assert (rec, "libcw/rec: delete: 'rec' argument can't be NULL\n");

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

   See libcw2.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of receive speed.

   \errno EINVAL - \p new_value is out of range.
   \errno EPERM - adaptive receive speed tracking is enabled.

   Notice that internally the speed is saved as float.

   testedin::test_cw_rec_identify_mark_internal()

   \reviewed on 2017-02-02

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

   See libcw2.h/CW_TOLERANCE_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of tolerance.

   \reviewed on 2017-02-02

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
void cw_rec_get_parameters_internal(cw_rec_t * rec,
				    int * dot_len_ideal, int * dash_len_ideal,
				    int * dot_len_min,   int * dot_len_max,
				    int * dash_len_min,  int * dash_len_max,
				    int * eom_len_min,
				    int * eom_len_max,
				    int * eom_len_ideal,
				    int * eoc_len_min,
				    int * eoc_len_max,
				    int * eoc_len_ideal,
				    int * adaptive_threshold)
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

   \reviewed on 2017-02-02

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




/**
   \brief Reset averaging data structure

   Reset averaging data structure to initial state.
   To be used in adaptive receiving mode.

   \reviewed on 2017-02-02

   \param avg - averaging data structure (for Dot or for Dash)
   \param initial - initial value to be put in table of averaging data structure
*/
void cw_rec_reset_average_internal(cw_rec_averaging_t * avg, int initial)
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
void cw_rec_update_average_internal(cw_rec_averaging_t * avg, int mark_len)
{
	/* Oldest mark length goes out, new goes in. */
	avg->sum -= avg->buffer[avg->cursor];
	avg->sum += mark_len;

	avg->average = avg->sum / CW_REC_AVERAGING_ARRAY_LENGTH;

	avg->buffer[avg->cursor++] = mark_len;
	avg->cursor %= CW_REC_AVERAGING_ARRAY_LENGTH;

	return;
}




/**
   \brief Add a mark or space length to statistics

   Add a mark or space length \p len (type of mark or space is
   indicated by \p type) to receiver's circular statistics buffer.
   The buffer stores only the delta from the ideal value; the ideal is
   inferred from the type \p type passed in.

   \reviewed on 2017-02-02

   \param rec - receiver
   \param type - type of statistics: CW_REC_STAT_DOT or CW_REC_STAT_DASH or CW_REC_STAT_IMARK_SPACE or CW_REC_STAT_ICHAR_SPACE
   \param len - length of a mark or space
*/
void cw_rec_update_stats_internal(cw_rec_t * rec, stat_type_t type, int len)
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

   \reviewed on 2017-02-02

   \return 0.0 if no record of given type were found
   \return statistics of length otherwise
*/
double cw_rec_get_stats_internal(cw_rec_t * rec, stat_type_t type)
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
void cw_rec_get_statistics_internal(cw_rec_t * rec, double * dot_sd, double * dash_sd,
				    double * element_end_sd, double * character_end_sd)
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
			      "libcw:rec:state: %s -> %s @ %s:%d",	\
			      cw_receiver_states[m_rec->state], cw_receiver_states[m_new_state], __FUNCTION__, __LINE__); \
		m_rec->state = m_new_state;				\
	}





/**
   \brief Enable or disable receiver's "adaptive receiving" mode

   Set the mode of a receiver (\p rec) to fixed or adaptive receiving
   mode.

   In adaptive receiving mode the receiver tracks the speed of the
   received Morse code by adapting to the input stream.

   testedin::test_cw_rec_identify_mark_internal()

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
		   the averages array to the current dot/dash lengths, so
		   that initial averages match the current speed. */
		if (rec->is_adaptive_receive_mode) {
			cw_rec_reset_average_internal(&rec->dot_averaging, rec->dot_len_ideal);
			cw_rec_reset_average_internal(&rec->dash_averaging, rec->dash_len_ideal);
		}
	}

	return;
}





void cw_rec_enable_adaptive_mode(cw_rec_t *rec)
{
	cw_rec_set_adaptive_mode_internal(rec, true);
	return;
}





void cw_rec_disable_adaptive_mode(cw_rec_t *rec)
{
	cw_rec_set_adaptive_mode_internal(rec, false);
	return;
}





/**
   \brief Get adaptive receive speed tracking flag

   The function returns state of "adaptive receive enabled" flag.
   See documentation of cw_enable_adaptive_receive() for more information

   \return true if adaptive speed tracking is enabled
   \return false otherwise
*/
bool cw_rec_get_adaptive_mode(const cw_rec_t * rec)
{
	return rec->is_adaptive_receive_mode;
}




int cw_rec_mark_begin(cw_rec_t *rec, const struct timeval *timestamp)
{
	/* If this is a tone start and we're awaiting an inter-word
	   space, cancel that wait and clear the receive buffer. */
	if (rec->is_pending_inter_word_space) {
		/* Tell receiver to prepare (to make space) for
		   receiving new character. */

		/* The tone start means that we're seeing the next
		   incoming character within the same word, so no
		   inter-word space is possible at this point in
		   time. The space that we were observing/waiting for,
		   was just inter-character space.

		   reset_state() will reset state of rec, including
		   is_pending_inter_word_space flag. */
		cw_rec_reset_state(rec);
	}


	/* If the receive state is not idle or inter-mark-space, this is a
	   state error.  A start of mark can only happen while we are
	   idle, or in inter-mark-space of a current character. */
	if (rec->state != RS_IDLE && rec->state != RS_SPACE) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw:rec:mark_begin: receive state not idle and not inter-mark-space: %s", cw_receiver_states[rec->state]);

		/*
		  ->state should be RS_IDLE at the beginning of new character;
		  ->state should be RS_SPACE in the middle of character (between marks).
		*/

		errno = ERANGE;
		return CW_FAILURE;
	}
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw:rec:mark_begin: receive state: %s", cw_receiver_states[rec->state]);

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
		   cw_rec_add_mark(). */
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




int cw_rec_mark_end(cw_rec_t *rec, const struct timeval *timestamp)
{
	/* The receive state is expected to be inside of a mark. */
	if (rec->state != RS_MARK) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw:rec:mark_end: receive state not RS_MARK: %s", cw_receiver_states[rec->state]);
		errno = ERANGE;
		return CW_FAILURE;
	}
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw:rec:mark_end: receive state: %s", cw_receiver_states[rec->state]);

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
		   complementary cw_rec_mark_begin(). After
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

		cw_debug_msg (&cw_debug_object, CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw:rec:mark_end: '%d [us]' mark identified as spike noise (threshold = '%d [us]')",
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
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw:rec:mark_end: recognized representation is '%s'", rec->representation);

	/* We just added a mark to the receive buffer.  If it's full,
	   then we have to do something, even though it's unlikely.
	   What we'll do is make a unilateral declaration that if we
	   get this far, we go to end-of-char error state
	   automatically. */
	if (rec->representation_ind == CW_REC_REPRESENTATION_CAPACITY - 1) {

		CW_REC_SET_STATE (rec, RS_EOC_GAP_ERR, (&cw_debug_object));

		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw:rec:mark_end: receiver's representation buffer is full");

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

		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO, "libcw:rec:identify: mark '%d [us]' recognized as DOT (limits: %d - %d [us])", mark_len, rec->dot_len_min, rec->dot_len_max);

		*mark = CW_DOT_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* Do the same for a dash. */
	if (mark_len >= rec->dash_len_min
	    && mark_len <= rec->dash_len_max) {

		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO, "libcw:rec:identify: mark '%d [us]' recognized as DASH (limits: %d - %d [us])", mark_len, rec->dash_len_min, rec->dash_len_max);

		*mark = CW_DASH_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* This mark is not a dot or a dash, so we have an error
	   case. */
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw:rec:identify: unrecognized mark, len = %d [us]", mark_len);
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw:rec:identify: dot limits: %d - %d [us]", rec->dot_len_min, rec->dot_len_max);
	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw:rec:identify: dash limits: %d - %d [us]", rec->dash_len_min, rec->dash_len_max);

	/* We should never reach here when in adaptive timing receive
	   mode - a mark should be always recognized as dot or dash,
	   and function should have returned before reaching this
	   point. */
	if (rec->is_adaptive_receive_mode) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw:rec:identify: unrecognized mark in adaptive receive");
	} else {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw:rec:identify: unrecognized mark in non-adaptive receive");
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
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_WARNING,
			      "Called \"adaptive\" function when receiver is not in adaptive mode\n");
		return;
	}

	/* Update moving averages for dots or dashes. */
	if (mark == CW_DOT_REPRESENTATION) {
		cw_rec_update_average_internal(&rec->dot_averaging, mark_len);
	} else if (mark == CW_DASH_REPRESENTATION) {
		cw_rec_update_average_internal(&rec->dash_averaging, mark_len);
	} else {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
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
   to cw_rec_mark_end().

   \param rec - receiver
   \param timestamp - timestamp of "end of mark" event
   \param mark - mark to be inserted into receiver's representation buffer

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_rec_add_mark(cw_rec_t *rec, const struct timeval *timestamp, char mark)
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

		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: receiver's representation buffer is full");

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* Since we effectively just saw the end of a mark, move to
	   the inter-mark-space state. */
	CW_REC_SET_STATE (rec, RS_SPACE, (&cw_debug_object));

	return CW_SUCCESS;
}





int cw_rec_poll_representation(cw_rec_t *rec,
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
		cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: space len == INT_MAX");

		errno = EAGAIN;
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

   Return representation and receiver's state flags after receiver has
   encountered end-of-character gap.

   Update receiver's state (\p rec) so that it matches end-of-character state.

   Since this is _eoc_function, \p is_end_of_word is set to false.

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

	cw_debug_msg (&cw_debug_object, CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw:rec:poll_eoc: state: %s", cw_receiver_states[rec->state]);

	/* Return the representation from receiver's buffer. */
	if (is_end_of_word) {
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





/**
   \brief Prepare return values at end-of-word

   Return representation and receiver's state flags after receiver has
   encountered end-of-word gap.

   Update receiver's state (\p rec) so that it matches end-of-word state.

   Since this is _eow_function, \p is_end_of_word is set to true.

   \p rec - receiver
   \p representation - representation of character from receiver's buffer
   \p is_end_of_word - end-of-word flag
   \p is_error - error flag
*/
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





int cw_rec_poll_character(cw_rec_t *rec,
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
   \brief Get the number of elements (dots/dashes) the receiver's buffer can accommodate

   The maximum number of elements written out by cw_receive_representation()
   is the capacity + 1, the extra character being used for the terminating
   NUL.

   \return number of elements that can be stored in receiver's representation buffer
*/
int cw_rec_get_receive_buffer_capacity_internal(void)
{
	return CW_REC_REPRESENTATION_CAPACITY;
}





int cw_rec_get_buffer_length_internal(cw_rec_t *rec)
{
	return rec->representation_ind;
}




/**
  \brief Reset essential receive parameters to their initial values
*/
void cw_rec_reset_parameters_internal(cw_rec_t *rec)
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

#if 0
		int debug_eoc_len_max = rec->eoc_len_max;
#endif

		/* Make the inter-mark space be anything up to the
		   adaptive threshold lengths - that is two dots.  And
		   the end-of-character gap is anything longer than
		   that, and shorter than five dots. */
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

	cw_debug_msg (&cw_debug_object, CW_DEBUG_PARAMETERS, CW_DEBUG_INFO,
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




bool cw_rec_poll_is_pending_inter_word_space(cw_rec_t const * rec)
{
	return rec->is_pending_inter_word_space;
}




#ifdef WITH_EXPERIMENTAL_RECEIVER

void cw_rec_register_push_callback(cw_rec_t *rec, cw_rec_push_callback_t *callback)
{
	rec->push_callback = callback;

	return;
}

#endif












#ifdef LIBCW_UNIT_TESTS




#define TEST_CW_REC_DATA_LEN_MAX 30 /* There is no character that would have that many time points corresponding to a representation. */
struct cw_rec_test_data {
	char c;                               /* Character. */
	char * representation;                /* Character's representation (dots and dashes). */
	int times[TEST_CW_REC_DATA_LEN_MAX];  /* Character's representation's times - time information for marks and spaces. */
	int n_times;                          /* Number of data points encoding given representation of given character. */
	float speed;                          /* Send speed (speed at which the character is incoming). */

	bool is_last_in_word;                 /* Is this character a last character in a word? (is it followed by end-of-word space?) */
};




static struct cw_rec_test_data * test_cw_rec_generate_data(const char * characters, float speeds[], int fuzz_percent);
static struct cw_rec_test_data * test_cw_rec_generate_base_data_constant(int speed, int fuzz_percent);
static struct cw_rec_test_data * test_cw_rec_generate_data_random_constant(int speed, int fuzz_percent);
static struct cw_rec_test_data * test_cw_rec_generate_data_random_varying(int speed_min, int speed_max, int fuzz_percent);

static void test_cw_rec_delete_data(struct cw_rec_test_data ** data);
__attribute__((unused)) static void test_cw_rec_print_data(struct cw_rec_test_data * data);
static bool test_cw_rec_test_begin_end(cw_rec_t * rec, struct cw_rec_test_data * data);

/* Functions creating tables of test values: characters and speeds.
   Characters and speeds will be combined into test (timing) data. */
static char  * test_cw_rec_new_base_characters(void);
static char  * test_cw_rec_generate_characters_random(int n);
static float * test_cw_rec_generate_speeds_constant(int speed, size_t n);
static float * test_cw_rec_generate_speeds_varying(int speed_min, int speed_max, size_t n);




/**
   tests::cw_rec_identify_mark_internal()

   Test if function correctly recognizes dots and dashes for a range
   of receive speeds.  This test function also checks if marks of
   lengths longer or shorter than certain limits (dictated by
   receiver) are handled properly (i.e. if they are recognized as
   invalid marks).

   Currently the function only works for non-adaptive receiving.
*/
unsigned int test_cw_rec_identify_mark_internal(cw_test_stats_t * stats)
{
	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, "libcw/rec: identify mark: failed to create new receiver\n");
	cw_rec_disable_adaptive_mode(rec);

	int speed_step = (CW_SPEED_MAX - CW_SPEED_MIN) / 10;

	for (int speed = CW_SPEED_MIN; speed < CW_SPEED_MAX; speed += speed_step) {
		int rv = cw_rec_set_speed(rec, speed);
		cw_assert (rv, "libcw/rec: identify mark @ %02d [wpm]: failed to set receive speed\n", speed);


		bool failure = true;
		int n = 0;
		char representation;




		/* Test marks that have length appropriate for a dot. */
		int len_step = (rec->dot_len_max - rec->dot_len_min) / 10;
		for (int len = rec->dot_len_min; len < rec->dot_len_max; len += len_step) {
			rv = cw_rec_identify_mark_internal(rec, len, &representation);

			failure = (rv != CW_SUCCESS);
			if (failure) {
				fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: failed to identify dot for len = %d [us]\n", speed, len);
				break;
			}
			failure = (representation != CW_DOT_REPRESENTATION);
			if (failure) {
				fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: failed to get dot representation for len = %d [us]\n", speed, len);
				break;
			}
		}
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: identify valid dot:", speed);
		CW_TEST_PRINT_TEST_RESULT (failure, n);




		/* Test mark shorter than minimal length of dot. */
		rv = cw_rec_identify_mark_internal(rec, rec->dot_len_min - 1, &representation);
		failure = (rv != CW_FAILURE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: mark shorter than min dot:", speed);
		CW_TEST_PRINT_TEST_RESULT (failure, n);




		/* Test mark longer than maximal length of dot (but shorter than minimal length of dash). */
		rv = cw_rec_identify_mark_internal(rec, rec->dot_len_max + 1, &representation);
		failure = (rv != CW_FAILURE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: mark longer than max dot:", speed);
		CW_TEST_PRINT_TEST_RESULT (failure, n);




		/* Test marks that have length appropriate for a dash. */
		len_step = (rec->dash_len_max - rec->dash_len_min) / 10;
		for (int len = rec->dash_len_min; len < rec->dash_len_max; len += len_step) {
			rv = cw_rec_identify_mark_internal(rec, len, &representation);

			failure = (rv != CW_SUCCESS);
			if (failure) {
				fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: failed to identify dash for len = %d [us]\n", speed, len);
				break;
			}

			failure = (representation != CW_DASH_REPRESENTATION);
			if (failure) {
				fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: failed to get dash representation for len = %d [us]\n", speed, len);
				break;
			}
		}
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: identify valid dash:", speed);
		CW_TEST_PRINT_TEST_RESULT (failure, n);




		/* Test mark shorter than minimal length of dash (but longer than maximal length of dot). */
		rv = cw_rec_identify_mark_internal(rec, rec->dash_len_min - 1, &representation);
		failure = (rv != CW_FAILURE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: mark shorter than min dash:", speed);
		CW_TEST_PRINT_TEST_RESULT (failure, n);




		/* Test mark longer than maximal length of dash. */
		rv = cw_rec_identify_mark_internal(rec, rec->dash_len_max + 1, &representation);
		failure = (rv != CW_FAILURE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/rec: identify mark @ %02d [wpm]: mark longer than max dash:", speed);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_rec_delete(&rec);

	return 0;
}




/*
  Test a receiver with data set that has following characteristics:

  Characters: base (all characters supported by libcw, occurring only once in the data set, in ordered fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to test receiver with test data set guaranteed to contain all characters supported by libcw.
*/
unsigned int test_cw_rec_test_with_base_constant(cw_test_stats_t * stats)
{
	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, "libcw/rec: begin/end: base/constant: failed to create new receiver\n");


	for (int speed = CW_SPEED_MIN; speed <= CW_SPEED_MAX; speed++) {
		struct cw_rec_test_data * data = test_cw_rec_generate_base_data_constant(speed, 0);
		//test_cw_rec_print_data(data);

		/* Reset. */
		cw_rec_reset_statistics(rec);
		cw_rec_reset_state(rec);

		cw_rec_set_speed(rec, speed);
		cw_rec_disable_adaptive_mode(rec);

		/* Make sure that the test speed has been set correctly. */
		float diff = cw_rec_get_speed(rec) - (float) speed;
		cw_assert (diff < 0.1, "libcw/rec: begin/end: base/constant: %f != %f\n",  cw_rec_get_speed(rec), (float) speed);

		/* Actual tests of receiver functions are here. */
		bool failure = test_cw_rec_test_begin_end(rec, data);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:rec: begin/end: base/constant @ %02d [wpm]:", speed);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		test_cw_rec_delete_data(&data);
	}

	cw_rec_delete(&rec);

	return 0;
}





/**
   \brief The core test function, testing receiver's "begin" and "end" functions

   As mentioned in file's top-level comment, there are two main
   methods to add data to receiver. This function tests first method:
   using cw_rec_mark_begin() and cw_rec_mark_end().

   Other helper functions are used/tested here as well, because adding
   marks and spaces to receiver is just half of the job necessary to
   receive Morse code. You have to interpret the marks and spaces,
   too.

   \param rec - receiver variable used during tests
   \param data - table with timings, used to test the receiver
*/
bool test_cw_rec_test_begin_end(cw_rec_t * rec, struct cw_rec_test_data * data)
{
	struct timeval tv = { 0, 0 };

	bool begin_end_failure = true;

	bool buffer_length_failure = true;

	bool poll_representation_failure = true;
	bool match_representation_failure = true;
	bool error_representation_failure = true;
	bool word_representation_failure = true;

	bool poll_character_failure = true;
	bool match_character_failure = true;
	bool empty_failure = true;

	for (int i = 0; data[i].representation; i++) {

#ifdef LIBCW_UNIT_TESTS_VERBOSE
		printf("\nlibcw/rec: begin/end: input test data #%d: <%c> / <%s> @ %.2f [wpm] (%d time values)\n",
		       i, data[i].c, data[i].r, data[i].s, data[i].n_times);
#endif

#if 0 /* Should we remove it? */
		/* Start sending every character at the beginning of a
		   new second.

		   TODO: here we make an assumption that every
		   character is sent in less than a second. Which is a
		   good assumption when we have a speed of tens of
		   WPM. If the speed will be lower, the assumption
		   will be false. */
		tv.tv_sec = 0;
		tv.tv_usec = 0;
#endif

		/* This loop simulates "key down" and "key up" events
		   in specific moments, and in specific time
		   intervals.

		   key down -> call to cw_rec_mark_begin()
		   key up -> call to cw_rec_mark_end().

		   First "key down" event is at X seconds Y
		   microseconds (see initialization of 'tv'). Time of
		   every following event is calculated by iterating
		   over tone lengths specified in data table. */
		int tone;
		for (tone = 0; data[i].times[tone] > 0; tone++) {
			begin_end_failure = false;

			if (tone % 2) {
				int rv = cw_rec_mark_end(rec, &tv);
				if (CW_SUCCESS != rv) {
					fprintf(out_file, "libcw/rec: begin/end: cw_rec_mark_end(): tone = %d, time = %d.%d\n", tone, (int) tv.tv_sec, (int) tv.tv_usec);
					begin_end_failure = true;
					break;
				}
			} else {
				int rv = cw_rec_mark_begin(rec, &tv);
				if (CW_SUCCESS != rv) {
					fprintf(out_file, "libcw/rec: begin/end: cw_rec_mark_begin(): tone = %d, time = %d.%d\n", tone, (int) tv.tv_sec, (int) tv.tv_usec);
					begin_end_failure = true;
					break;
				}
			}

			tv.tv_usec += data[i].times[tone];
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

		cw_assert (tone, "libcw/rec: begin/end executed zero times\n");
		if (begin_end_failure) {
			break;
		}




		/* Test: length of receiver's buffer (only marks!)
		   after adding a representation of a single character
		   to receiver's buffer. */
		{
			int n = cw_rec_get_buffer_length_internal(rec);
			buffer_length_failure = (n != (int) strlen(data[i].representation));
			if (buffer_length_failure) {
				fprintf(out_file, "libcw/rec: begin/end: cw_rec_get_buffer_length_internal(<nonempty>): %d != %zd\n", n, strlen(data[i].representation));
				break;
			}
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
			   of final end-of-character space.

			   With this final passing of "end of space"
			   timestamp to libcw the test code informs
			   receiver, that end-of-character space has
			   occurred, i.e. a full character has been
			   passed to receiver.

			   The space length in input data is (3 x dot
			   + jitter). In libcw maximum recognizable
			   length of "end of character" space is 5 x
			   dot. */
			poll_representation_failure = (CW_SUCCESS != cw_rec_poll_representation(rec, &tv, representation, &is_word, &is_error));
			if (poll_representation_failure) {
				fprintf(out_file, "libcw/rec: begin/end: poll representation returns !CW_SUCCESS\n");
				break;
			}

			match_representation_failure = (0 != strcmp(representation, data[i].representation));
			if (match_representation_failure) {
				fprintf(out_file, "libcw/rec: being/end: polled representation does not match test representation: \"%s\" != \"%s\"\n", representation, data[i].representation);
				break;
			}

			error_representation_failure = (true == is_error);
			if (error_representation_failure) {
				fprintf(out_file, "libcw/rec: begin/end: poll representation sets is_error\n");
				break;
			}



			/* If the last space in character's data is
			   end-of-word space (which is indicated by
			   is_last_in_word), then is_word should be
			   set by poll() to true. Otherwise both
			   values should be false. */
			word_representation_failure = (is_word != data[i].is_last_in_word);
			if (word_representation_failure) {
				fprintf(out_file, "libcw/rec: begin/end: poll representation: 'is_word' flag error: function returns '%d', data is tagged with '%d'\n" \
					"'%c'  '%c'  '%c'  '%c'  '%c'",
					is_word, data[i].is_last_in_word,
					data[i - 2].c, data[i - 1].c, data[i].c, data[i + 1].c, data[i + 2].c );
				break;
			}

#if 0
			/* Debug code. Print times of character with
			   end-of-word space to verify length of the
			   space. */
			if (data[i].is_last_in_word) {
				fprintf(stderr, "------- character '%c' is last in word\n", data[i].c);
				for (int m = 0; m < data[i].n_times; m++) {
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
			poll_character_failure = (CW_SUCCESS != cw_rec_poll_character(rec, &tv, &c, &is_word, &is_error));
			if (poll_character_failure) {
				fprintf(out_file, "libcw/rec: begin/end: poll character false\n");
				break;
			}

			match_character_failure = (c != data[i].c);
			if (match_character_failure) {
				fprintf(out_file, "libcw/rec: begin/end: polled character does not match test character: '%c' != '%c'\n", c, data[i].c);
				break;
			}
		}




		/* Test: getting length of receiver's representation
		   buffer after cleaning the buffer. */
		{
			/* We have a copy of received representation,
			   we have a copy of character. The receiver
			   no longer needs to store the
			   representation. If I understand this
			   correctly, the call to reset_state() is necessary
			   to prepare the receiver for receiving next
			   character. */
			cw_rec_reset_state(rec);
			int length = cw_rec_get_buffer_length_internal(rec);

			empty_failure = (length != 0);
			if (empty_failure) {
				fprintf(out_file, "libcw/rec: begin/end: get buffer length: length of cleared buffer is non zero (is %d)",  length);
				break;
			}
		}


#ifdef LIBCW_UNIT_TESTS_VERBOSE
		float speed = cw_rec_get_speed(rec);
		printf("libcw: received data #%d:   <%c> / <%s> @ %.2f [wpm]\n",
		       i, c, representation, speed);
#endif

#if 0
		if (adaptive) {
			printf("libcw: adaptive speed tracking reports %f [wpm]\n",  );
		}
#endif

	}

	return begin_end_failure
		|| buffer_length_failure
		|| poll_representation_failure || match_representation_failure || error_representation_failure || word_representation_failure
		|| poll_character_failure || match_character_failure || empty_failure;
}




/*
  Generate small test data set.

  Characters: base (all characters supported by libcw, occurring only once in the data set, in ordered fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to generate a data set guaranteed to contain all characters supported by libcw.
*/
struct cw_rec_test_data * test_cw_rec_generate_base_data_constant(int speed, int fuzz_percent)
{
	/* All characters supported by libcw.  Don't use
	   get_characters_random(): for this test get a small table of
	   all characters supported by libcw. This should be a quick
	   test, and it should cover all characters. */
	char * base_characters = test_cw_rec_new_base_characters();
	cw_assert (base_characters, "libcw/rec: new base data fixed: test_cw_rec_new_base_characters() failed\n");


	size_t n = strlen(base_characters);


	/* Fixed speed receive mode - speed is constant for all
	   characters. */
	float * speeds = test_cw_rec_generate_speeds_constant(speed, n);
	cw_assert (speeds, "libcw/rec: new base data fixed: test_cw_rec_generate_speeds_constant() failed\n");


	/* Generate timing data for given set of characters, each
	   character is sent with speed dictated by speeds[]. */
	struct cw_rec_test_data * data = test_cw_rec_generate_data(base_characters, speeds, fuzz_percent);
	cw_assert (data, "libcw/rec: failed to generate base/fixed test data\n");


	free(base_characters);
	base_characters = NULL;

	free(speeds);
	speeds = NULL;

	return data;
}





/*
  Test a receiver with data set that has following characteristics:

  Characters: random (all characters supported by libcw + inter-word space, occurring once or more in the data set, in random fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to test receiver with very large test data set.
*/
unsigned int test_cw_rec_test_with_random_constant(cw_test_stats_t * stats)
{
	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, "libcw/rec: begin/end: random/constant: failed to create new receiver\n");


	for (int speed = CW_SPEED_MIN; speed <= CW_SPEED_MAX; speed++) {
		struct cw_rec_test_data * data = test_cw_rec_generate_data_random_constant(speed, 0);
		//test_cw_rec_print_data(data);

		/* Reset. */
		cw_rec_reset_statistics(rec);
		cw_rec_reset_state(rec);

		cw_rec_set_speed(rec, speed);
		cw_rec_disable_adaptive_mode(rec);

		/* Verify that test speed has been set correctly. */
		float diff = cw_rec_get_speed(rec) - speed;
		cw_assert (diff < 0.1, "libcw/rec: begin/end: random/constant: incorrect receive speed: %f != %f\n", cw_rec_get_speed(rec), (float) speed);

		/* Actual tests of receiver functions are here. */
		bool failure = test_cw_rec_test_begin_end(rec, data);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw:rec: begin/end: random/constant @ %02d [wpm]:", speed);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		test_cw_rec_delete_data(&data);
	}

	cw_rec_delete(&rec);

	return 0;
}





/*
  Test a receiver with data set that has following characteristics:

  Characters: random (all characters supported by libcw + inter-word space, occurring once or more in the data set, in random fashion).
  Send speeds: varying (each character will be sent to receiver at different speed).

  This function is used to test receiver with very large test data set.
*/
unsigned int test_cw_rec_test_with_random_varying(cw_test_stats_t * stats)
{
	struct cw_rec_test_data * data = test_cw_rec_generate_data_random_varying(CW_SPEED_MIN, CW_SPEED_MAX, 0);
	//test_cw_rec_print_data(data);

	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, "libcw/rec: begin/end: random/varying: failed to create new receiver\n");

	/* Reset. */
	cw_rec_reset_statistics(rec);
	cw_rec_reset_state(rec);

	cw_rec_set_speed(rec, CW_SPEED_MAX);
	cw_rec_enable_adaptive_mode(rec);

	/* Verify that initial test speed has been set correctly. */
	float diff = cw_rec_get_speed(rec) - CW_SPEED_MAX;
	cw_assert (diff < 0.1, "libcw/rec: begin/end: random/varying: incorrect receive speed: %f != %f\n", cw_rec_get_speed(rec), (float) CW_SPEED_MAX);

	/* Actual tests of receiver functions are here. */
	bool failure = test_cw_rec_test_begin_end(rec, data);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw:rec: begin/end: random/varying:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	test_cw_rec_delete_data(&data);

	cw_rec_delete(&rec);

	return 0;
}





/*
  Generate large test data set.

  Characters: random (all characters supported by libcw + inter-word space, occurring once or more in the data set, in random fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to generate a large test data set.
*/
struct cw_rec_test_data * test_cw_rec_generate_data_random_constant(int speed, int fuzz_percent)
{
	const int n = cw_get_character_count() * 30;

	char * characters = test_cw_rec_generate_characters_random(n);
	cw_assert (characters, "libcw/rec: test_cw_rec_generate_characters_random() failed\n");

	/* Fixed speed receive mode - speed is constant for all characters. */
	float * speeds = test_cw_rec_generate_speeds_constant(speed, n);
	cw_assert (speeds, "libcw/rec: test_cw_rec_generate_speeds_constant() failed\n");


	/* Generate timing data for given set of characters, each
	   character is sent with speed dictated by speeds[]. */
	struct cw_rec_test_data * data = test_cw_rec_generate_data(characters, speeds, fuzz_percent);
	cw_assert (data, "libcw/rec: random/constant: failed to generate test data\n");


	free(characters);
	characters = NULL;

	free(speeds);
	speeds = NULL;

	return data;
}





/*
  Generate large test data set.

  Characters: random (all characters supported by libcw + inter-word space, occurring once or more in the data set, in random fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to generate a large test data set.
*/
struct cw_rec_test_data * test_cw_rec_generate_data_random_varying(int speed_min, int speed_max, int fuzz_percent)
{
	int n = cw_get_character_count() * 30;

	char *characters = test_cw_rec_generate_characters_random(n);
	cw_assert (characters, "libcw/rec: begin/end: test_cw_rec_generate_characters_random() failed\n");

	/* Adaptive speed receive mode - speed varies for all
	   characters. */
	float * speeds = test_cw_rec_generate_speeds_varying(speed_min, speed_max, n);
	cw_assert (speeds, "libcw/rec: test_cw_rec_generate_speeds_varying() failed\n");


	/* Generate timing data for given set of characters, each
	   character is sent with speed dictated by speeds[]. */
	struct cw_rec_test_data *data = test_cw_rec_generate_data(characters, speeds, fuzz_percent);
	cw_assert (data, "libcw/rec: failed to generate random/varying test data\n");


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
char * test_cw_rec_new_base_characters(void)
{
	int n = cw_get_character_count();
	char * base_characters = (char *) malloc((n + 1) * sizeof (char));
	cw_assert (base_characters, "libcw/rec: get base characters: malloc() failed\n");
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
char * test_cw_rec_generate_characters_random(int n)
{
	/* All characters supported by libcw - this will be an input
	   set of all characters. */
	char * base_characters = test_cw_rec_new_base_characters();
	cw_assert (base_characters, "libcw/rec: test_cw_rec_new_base_characters() failed\n");
	size_t length = strlen(base_characters);


	char * characters = (char *) malloc ((n + 1) * sizeof (char));
	cw_assert (characters, "libcw/rec: malloc() failed\n");
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

	//fprintf(stderr, "%s\n", characters);


	free(base_characters);
	base_characters = NULL;


	return characters;
}





/**
   \brief Generate a table of constant speeds

   Function allocates and returns a table of speeds of constant value
   specified by \p speed. There are \p n valid (non-negative) values
   in the table. After the last valid value there is a small negative
   value at position 'n' in the table that acts as a guard.

   \param speed - a constant value to be used as initializer of the table
   \param n - size of table (function allocates additional one cell for guard)

   \return table of speeds of constant value
*/
float * test_cw_rec_generate_speeds_constant(int speed, size_t n)
{
	cw_assert (speed > 0, "libcw/rec: generate speeds constant: speed must be larger than zero\n");

	float * speeds = (float *) malloc((n + 1) * sizeof (float));
	cw_assert (speeds, "libcw/rec: generate speeds constant: malloc() failed\n");

	for (size_t i = 0; i < n; i++) {
		/* Fixed speed receive mode - speed values are constant for
		   all characters. */
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
   value there is a small negative value at position 'n' in the table
   that acts as a guard.

   \param speed_min - minimal speed
   \param speed_max - maximal speed
   \param n - size of table (function allocates additional one cell for guard)

   \return table of speeds
*/
float * test_cw_rec_generate_speeds_varying(int speed_min, int speed_max, size_t n)
{
	cw_assert (speed_min > 0, "libcw/rec: generate speeds varying: speed_min must be larger than zero\n");
	cw_assert (speed_max > 0, "libcw/rec: generate speeds varying: speed_max must be larger than zero\n");
	cw_assert (speed_min <= speed_max, "libcw/rec: generate speeds varying: speed_min can't be larger than speed_max\n");

	float * speeds = (float *) malloc((n + 1) * sizeof (float));
	cw_assert (speeds, "libcw/rec: generate speeds varying: malloc() failed\n");

	for (size_t i = 0; i < n; i++) {

		/* Adaptive speed receive mode - speed varies for all
		   characters. */

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

   .-    ==   40000 (dot mark); 40000 (inter-mark space); 120000 (dash mark); 240000 (end-of-word space); 0 (guard, zero timing)

   Last element in the created table (a guard "pseudo-character") has
   'representation' field set to NULL.

   Use test_cw_rec_delete_data() to deallocate the timing data table.

   \brief characters - list of characters for which to generate table with timing data
   \brief speeds - list of speeds (per-character)

   \return table of timing data sets
*/
struct cw_rec_test_data * test_cw_rec_generate_data(char const * characters, float speeds[], __attribute__((unused)) int fuzz_percent)
{
	size_t n = strlen(characters);
	/* +1 for guard. */
	struct cw_rec_test_data * test_data = (struct cw_rec_test_data *) malloc((n + 1) * sizeof(struct cw_rec_test_data));
	cw_assert (test_data, "libcw/rec: generate data: malloc() failed\n");

	/* Initialization. */
	for (size_t i = 0; i < n + 1; i++) {
		test_data[i].representation = (char *) NULL;
	}

	size_t out = 0; /* For indexing output data table. */
	for (size_t in = 0; in < n; in++) {

		int unit_len = CW_DOT_CALIBRATION / speeds[in]; /* Dot length, [us]. Used as basis for other elements. */
		// fprintf(stderr, "unit_len = %d [us] for speed = %d [wpm]\n", unit_len, speed);


		/* First handle a special case: end-of-word
		   space. This long space will be put at the end of
		   table of time values for previous
		   representation. */
		if (characters[in] == ' ') {
			/* We don't want to affect current output
			   character, we want to turn end-of-char
			   space of previous character into
			   end-of-word space, hence 'out - 1'. */
			int space_i = test_data[out - 1].n_times - 1;    /* Index of last space (end-of-char, to become end-of-word). */
			test_data[out - 1].times[space_i] = unit_len * 6; /* unit_len * 5 is the minimal end-of-word space. */

			test_data[out - 1].is_last_in_word = true;

			continue;
		} else {
			/* A regular character, handled below. */
		}


		test_data[out].c = characters[in];
		test_data[out].representation = cw_character_to_representation(test_data[out].c);
		cw_assert (test_data[out].representation,
			   "libcw/rec: generate data: cw_character_to_representation() failed for input char #%zu: '%c'\n",
			   in, characters[in]);
		test_data[out].speed = speeds[in];


		/* Build table of times (data points) 'd[]' for given
		   representation 'r'. */


		size_t n_times = 0; /* Number of data points in data table. */

		size_t rep_length = strlen(test_data[out].representation);
		for (size_t k = 0; k < rep_length; k++) {

			/* Length of mark. */
			if (test_data[out].representation[k] == CW_DOT_REPRESENTATION) {
				test_data[out].times[n_times] = unit_len;

			} else if (test_data[out].representation[k] == CW_DASH_REPRESENTATION) {
				test_data[out].times[n_times] = unit_len * 3;

			} else {
				cw_assert (0, "libcw/rec: generate data: unknown char in representation: '%c'\n", test_data[out].representation[k]);
			}
			n_times++;


			/* Length of space (inter-mark space). Mark
			   and space always go in pair. */
			test_data[out].times[n_times] = unit_len;
			n_times++;
		}

		/* Every character has non-zero marks and spaces. */
		cw_assert (n_times > 0, "libcw/rec: generate data: number of data points is %zu for representation '%s'\n", n_times, test_data[out].representation);

		/* Mark and space always go in pair, so nd should be even. */
		cw_assert (! (n_times % 2), "libcw/rec: generate data: number of times is not even\n");

		/* Mark/space pair per each dot or dash. */
		cw_assert (n_times == 2 * rep_length, "libcw/rec: generate data: number of times incorrect: %zu != 2 * %zu\n", n_times, rep_length);


		/* Graduate that last space (inter-mark space) into
		   end-of-character space. */
		test_data[out].times[n_times - 1] = (unit_len * 3) + (unit_len / 2);

		/* Guard. */
		test_data[out].times[n_times] = 0;

		test_data[out].n_times = n_times;

		/* This may be overwritten by this function when a
		   space character (' ') is encountered in input
		   string. */
		test_data[out].is_last_in_word = false;

		out++;
	}


	/* Guard. */
	test_data[n].representation = (char *) NULL;


	return test_data;
}





/**
   \brief Deallocate timing data used for testing a receiver

   \param data - pointer to data to be deallocated
*/
void test_cw_rec_delete_data(struct cw_rec_test_data ** data)
{
	int i = 0;
	while ((*data)[i].representation) {
		free((*data)[i].representation);
		(*data)[i].representation = (char *) NULL;

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
void test_cw_rec_print_data(struct cw_rec_test_data * data)
{
	int i = 0;

	fprintf(stderr, "---------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	while (data[i].representation) {
		/* Debug output. */
		if (!(i % 10)) {
			/* Print header. */
			fprintf(stderr, "char  repr      [wpm]    mark     space      mark     space      mark     space      mark     space      mark     space      mark     space      mark     space\n");
		}
		fprintf(stderr, "%c     %-7s  %02.2f", data[i].c, data[i].representation, data[i].speed);
		for (int j = 0; j < data[i].n_times; j++) {
			fprintf(stderr, "%9d ", data[i].times[j]);
		}
		fprintf(stderr, "\n");

		i++;
	}

	return;
}




unsigned int test_cw_rec_get_parameters(cw_test_stats_t * stats)
{
	bool failure = true;
	int n = 0;

	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, "libcw/rec: get: failed to create new receiver\n");

	cw_rec_reset_parameters_internal(rec);
	cw_rec_sync_parameters_internal(rec);

	int dot_len_ideal = 0;
	int dash_len_ideal = 0;

	int dot_len_min = 0;
	int dot_len_max = 0;

	int dash_len_min = 0;
	int dash_len_max = 0;

	int eom_len_min = 0;
	int eom_len_max = 0;
	int eom_len_ideal = 0;

	int eoc_len_min = 0;
	int eoc_len_max = 0;
	int eoc_len_ideal = 0;

	int adaptive_speed_threshold = 0;

	cw_rec_get_parameters_internal(rec,
				       &dot_len_ideal, &dash_len_ideal, &dot_len_min, &dot_len_max, &dash_len_min, &dash_len_max,
				       &eom_len_min, &eom_len_max, &eom_len_ideal,
				       &eoc_len_min, &eoc_len_max, &eoc_len_ideal,
				       &adaptive_speed_threshold);

	cw_rec_delete(&rec);

	fprintf(out_file,
		"libcw/rec: get: dot/dash:  %d, %d, %d, %d, %d, %d\n" \
		"libcw/rec: get: eom:       %d, %d, %d\n" \
		"libcw/rec: get: eoc:       %d, %d, %d\n" \
		"libcw/rec: get: threshold: %d\n",

		dot_len_ideal, dash_len_ideal, dot_len_min, dot_len_max, dash_len_min, dash_len_max,
		eom_len_min, eom_len_max, eom_len_ideal,
		eoc_len_min, eoc_len_max, eoc_len_ideal,
		adaptive_speed_threshold);


	failure = (dot_len_ideal <= 0
		   || dash_len_ideal <= 0
		   || dot_len_min <= 0
		   || dot_len_max <= 0
		   || dash_len_min <= 0
		   || dash_len_max <= 0

		   || eom_len_min <= 0
		   || eom_len_max <= 0
		   || eom_len_ideal <= 0

		   || eoc_len_min <= 0
		   || eoc_len_max <= 0
		   || eoc_len_ideal <= 0

		   || adaptive_speed_threshold <= 0);


	failure = dot_len_max >= dash_len_min;
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get: max dot len < min dash len (%d/%d):", dot_len_max, dash_len_min);
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	failure = (dot_len_min >= dot_len_ideal) || (dot_len_ideal >= dot_len_max);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get: dot len consistency (%d/%d/%d):", dot_len_min, dot_len_ideal, dot_len_max);
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	failure = (dash_len_min >= dash_len_ideal) || (dash_len_ideal >= dash_len_max);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get: dash len consistency (%d/%d/%d):", dash_len_min, dash_len_ideal, dash_len_max);
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	failure = (eom_len_max >= eoc_len_min);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get: max eom len < min eoc len (%d/%d):", eom_len_max, eoc_len_min);
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	failure = (eom_len_min >= eom_len_ideal) || (eom_len_ideal >= eom_len_max);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get: eom len consistency (%d/%d/%d)", eom_len_min, eom_len_ideal, eom_len_max);
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	failure = (eoc_len_min >= eoc_len_ideal) || (eoc_len_ideal >= eoc_len_max);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get: eoc len consistency (%d/%d/%d)", eoc_len_min, eoc_len_ideal, eoc_len_max);
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	return 0;
}





/* Parameter getters and setters are independent of audio system, so
   they can be tested just with CW_AUDIO_NULL.  This is even more true
   for limit getters, which don't require a receiver at all. */
unsigned int test_cw_rec_parameter_getters_setters_1(cw_test_stats_t * stats)
{
	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, "libcw/rec: get/set param 1: failed to create new receiver\n");

	/* Test setting and getting of some basic parameters. */

	int off_limits = 10000;

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int *min, int *max);
		int (* set_new_value)(cw_rec_t *rec, int new_value);
		float (* get_value)(cw_rec_t *rec);

		int min; /* Minimal acceptable value of parameter. */
		int max; /* Maximal acceptable value of parameter. */

		const char *name;
	} test_data[] = {
		{ cw_get_speed_limits,      cw_rec_set_speed,      cw_rec_get_speed,      off_limits,  -off_limits,  "receive speed" },
		{ NULL,                     NULL,                  NULL,                           0,            0,  NULL            }
	};

	bool get_failure = true;
	bool set_min_failure = true;
	bool set_max_failure = true;
	bool set_ok_failure = false;
	int n = 0;


	for (int i = 0; test_data[i].get_limits; i++) {

		int status;
		int value = 0;

		/* Get limits of values to be tested. */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);

		get_failure = (test_data[i].min <= -off_limits);
		if (get_failure) {
			fprintf(out_file, "libcw/rec: get/set param 1: get min %s: failed to get low limit, returned value = %d\n", test_data[i].name, test_data[i].min);
			break;
		}
		get_failure = (test_data[i].max >= off_limits);
		if (get_failure) {
			fprintf(out_file, "libcw/rec: get/set param 1: get max %s: failed to get high limit, returned value = %d\n", test_data[i].name, test_data[i].max);
			break;
		}


		/* Test out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].min - 1;
		status = test_data[i].set_new_value(rec, value);

		set_min_failure = (status == CW_SUCCESS);
		if (set_min_failure) {
			fprintf(out_file, "libcw/rec: get/set param 1: setting %s value below minimum succeeded, minimum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value);
			break;
		}
		set_min_failure = (errno != EINVAL);
		if (set_min_failure) {
			fprintf(out_file, "libcw/rec: get/set param 1: setting %s value below minimum didn't result in EINVAL, minimum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value);
			break;
		}



		/* Test out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].max + 1;
		status = test_data[i].set_new_value(rec, value);

		set_max_failure = (status == CW_SUCCESS);
		if (set_max_failure) {
			fprintf(out_file, "libcw/rec: get/set param 1: setting %s value above minimum succeeded, maximum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value);
			break;
		}
		set_max_failure = (errno != EINVAL);
		if (set_max_failure) {
			fprintf(out_file, "libcw/rec: get/set param 1: setting %s value above maximum didn't result in EINVAL, maximum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value);
			break;
		}


		/* Test in-range values. Set with setter and then read back with getter. */
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			test_data[i].set_new_value(rec, j);

			float diff = test_data[i].get_value(rec) - j;
			set_ok_failure = (diff >= 0.01);
			if (set_ok_failure) {
				fprintf(stderr, "libcw/rec: get/set param 1: setting value in-range failed for %s value = %d (%f - %d = %f)\n",
					test_data[i].name, j,
					(float) test_data[i].get_value(rec), j, diff);
				break;
			}
		}
		if (set_ok_failure) {
			break;
		}
	}

	cw_rec_delete(&rec);


	get_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get/set param 1: get:");
	CW_TEST_PRINT_TEST_RESULT (get_failure, n);

	set_min_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get/set param 1: set value below min:");
	CW_TEST_PRINT_TEST_RESULT (set_min_failure, n);

	set_max_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get/set param 1: set value above max:");
	CW_TEST_PRINT_TEST_RESULT (set_max_failure, n);

	set_ok_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get/set param 1: set value in range:");
	CW_TEST_PRINT_TEST_RESULT (set_ok_failure, n);

	return 0;
}




/* Parameter getters and setters are independent of audio system, so
   they can be tested just with CW_AUDIO_NULL.  This is even more true
   for limit getters, which don't require a receiver at all. */
unsigned int test_cw_rec_parameter_getters_setters_2(cw_test_stats_t * stats)
{
	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, "libcw/rec: get/set param 2: failed to create new receiver\n");

	/* Test setting and getting of some basic parameters. */

	int off_limits = 10000;

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int *min, int *max);
		int (* set_new_value)(cw_rec_t *rec, int new_value);
		int (* get_value)(cw_rec_t *rec);

		int min; /* Minimal acceptable value of parameter. */
		int max; /* Maximal acceptable value of parameter. */

		const char *name;
	} test_data[] = {
		{ cw_get_tolerance_limits,  cw_rec_set_tolerance,  cw_rec_get_tolerance,  off_limits,  -off_limits,  "tolerance"     },
		{ NULL,                     NULL,                  NULL,                           0,            0,  NULL            }
	};

	bool get_failure = true;
	bool set_min_failure = true;
	bool set_max_failure = true;
	bool set_ok_failure = false;
	int n = 0;


	for (int i = 0; test_data[i].get_limits; i++) {

		int status;
		int value = 0;

		/* Get limits of values to be tested. */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);

		get_failure = (test_data[i].min <= -off_limits);
		if (get_failure) {
			fprintf(out_file, "libcw/rec: get/set param 2: get min %s: failed to get low limit, returned value = %d\n", test_data[i].name, test_data[i].min);
			break;
		}
		get_failure = (test_data[i].max >= off_limits);
		if (get_failure) {
			fprintf(out_file, "libcw/rec: get/set param 2: get max %s: failed to get high limit, returned value = %d\n", test_data[i].name, test_data[i].max);
			break;
		}


		/* Test out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].min - 1;
		status = test_data[i].set_new_value(rec, value);

		set_min_failure = (status == CW_SUCCESS);
		if (set_min_failure) {
			fprintf(out_file, "libcw/rec: get/set param 2: setting %s value below minimum succeeded, minimum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value);
			break;
		}
		set_min_failure = (errno != EINVAL);
		if (set_min_failure) {
			fprintf(out_file, "libcw/rec: get/set param: setting %s value below minimum didn't result in EINVAL, minimum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value);
			break;
		}



		/* Test out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].max + 1;
		status = test_data[i].set_new_value(rec, value);

		set_max_failure = (status == CW_SUCCESS);
		if (set_max_failure) {
			fprintf(out_file, "libcw/rec: get/set param 2: setting %s value above minimum succeeded, maximum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value);
			break;
		}
		set_max_failure = (errno != EINVAL);
		if (set_max_failure) {
			fprintf(out_file, "libcw/rec: get/set param 2: setting %s value above maximum didn't result in EINVAL, maximum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value);
			break;
		}


		/* Test in-range values. Set with setter and then read back with getter. */
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			test_data[i].set_new_value(rec, j);

			float diff = test_data[i].get_value(rec) - j;
			set_ok_failure = (diff >= 0.01);
			if (set_ok_failure) {
				fprintf(stderr, "libcw/rec: get/set param 2: setting value in-range failed for %s value = %d (%f - %d = %f)\n",
					test_data[i].name, j,
					(float) test_data[i].get_value(rec), j, diff);
				break;
			}
		}
		if (set_ok_failure) {
			break;
		}
	}

	cw_rec_delete(&rec);


	get_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get/set param 2: get:");
	CW_TEST_PRINT_TEST_RESULT (get_failure, n);

	set_min_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get/set param 2: set value below min:");
	CW_TEST_PRINT_TEST_RESULT (set_min_failure, n);

	set_max_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get/set param 2: set value above max:");
	CW_TEST_PRINT_TEST_RESULT (set_max_failure, n);

	set_ok_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:rec: get/set param 2: set value in range:");
	CW_TEST_PRINT_TEST_RESULT (set_ok_failure, n);

	return 0;
}




#endif /* #ifdef LIBCW_UNIT_TESTS */
