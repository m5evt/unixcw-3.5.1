/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_REC
#define H_LIBCW_REC


#include "libcw.h"



/* Morse code controls and timing parameters */

/* Dot length magic number; from PARIS calibration, 1 Dot=1200000/WPM
   usec.
   This variable is used in generator code as well. */
enum { CW_DOT_CALIBRATION = 1200000 };

/* Default initial values for library controls. */
enum { CW_REC_ADAPTIVE_INITIAL = false };                                           /* Initial adaptive receive setting. */
enum { CW_REC_THRESHOLD_INITIAL = (CW_DOT_CALIBRATION / CW_SPEED_INITIAL) * 2 };    /* Initial adaptive speed threshold. [us] */
enum { CW_REC_NOISE_THRESHOLD_INITIAL = (CW_DOT_CALIBRATION / CW_SPEED_MAX) / 2 };  /* Initial noise filter threshold. */





/* TODO: what is the relationship between this constant and CW_REC_REPRESENTATION_CAPACITY?
   Both have value of 256. Coincidence? I don't think so. */
enum { CW_REC_STATISTICS_CAPACITY = 256 };



/* Receiver contains a fixed-length buffer for representation of received data.
   Capacity of the buffer is vastly longer than any practical representation.
   Don't know why, a legacy thing. */
enum { CW_REC_REPRESENTATION_CAPACITY = 256 };



/* Length of array used to calculate average length of a mark. Average
   length of a mark is used in adaptive receiving mode to track speed
   of incoming Morse data. */
enum { CW_REC_AVERAGING_ARRAY_LENGTH = 4 };



/* Receive timing statistics.
   A circular buffer of entries indicating the difference between the
   actual and the ideal timing for a receive mark, tagged with the
   type of statistic held, and a circular buffer pointer.
   STAT_NONE must be zero so that the statistics buffer is initially empty. */
typedef enum {
	CW_REC_STAT_NONE = 0,
	CW_REC_STAT_DOT,
	CW_REC_STAT_DASH,
	CW_REC_STAT_MARK_END,
	CW_REC_STAT_CHAR_END
} stat_type_t;



typedef struct {
	stat_type_t type;  /* Record type */
	int delta;         /* Difference between actual and ideal timing */
} cw_statistics_t;



/* A moving averages structure - circular buffer. Used for calculating
   averaged length ([us]) of dots and dashes. */
typedef struct {
	int buffer[CW_REC_AVERAGING_ARRAY_LENGTH];  /* Buffered mark lengths. */
	int cursor;                                 /* Circular buffer cursor. */
	int sum;                                    /* Running sum of lengths of marks. [us] */
	int average;                                /* Averaged length of a mark. [us] */
} cw_rec_averaging_t;




typedef struct {
	/* State of receiver state machine. */
	int state;

	int gap; /* Inter-mark gap, similar as in generator. */



	/* Essential receive parameters. */
	/* Changing values of speed, tolerance or
	   is_adaptive_receive_mode will trigger a recalculation of
	   low level timing parameters. */

	int speed; /* [wpm] */
	int tolerance;
	bool is_adaptive_receive_mode;
	int noise_spike_threshold;



	/* Library variable which is automatically maintained from the
	   Morse input stream, rather than being settable by the
	   user. */
	int adaptive_receive_threshold; /* [us] */


	/* Retained tone start and end timestamps. */
	struct timeval tone_start;
	struct timeval tone_end;

	/* Buffer for received representation (dots/dashes). This is a
	   fixed-length buffer, filled in as tone on/off timings are
	   taken. The buffer is vastly longer than any practical
	   representation.

	   Along with it we maintain a cursor indicating the current
	   write position. */
	char representation[CW_REC_REPRESENTATION_CAPACITY];
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

	   After changing receiver's receive speed, tolerance or
	   adaptive mode, some receiver's internal parameters need to
	   be re-calculated. This is a flag that shows when this needs
	   to be done. */
	bool parameters_in_sync;



	/* Receiver statistics */
	cw_statistics_t statistics[CW_REC_STATISTICS_CAPACITY];
	int statistics_ind;


	/* Data structures for calculating averaged length of dots and
	   dashes. The averaged lengths are used for adaptive tracking
	   of receiver's speed (tracking of speed of incoming data). */
	cw_rec_averaging_t dot_averaging;
	cw_rec_averaging_t dash_averaging;

} cw_rec_t;





void cw_rec_reset_receive_parameters_internal(cw_rec_t *rec);
void cw_rec_sync_parameters_internal(cw_rec_t *rec);



#ifdef LIBCW_UNIT_TESTS

unsigned int test_cw_rec_mark_identify_internal(void);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_REC */
