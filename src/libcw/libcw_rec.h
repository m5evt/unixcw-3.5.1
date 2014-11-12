/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_REC
#define H_LIBCW_REC


#include "libcw.h"
//#include "libcw_gen.h"



/* Morse code controls and timing parameters */

/* Dot length magic number; from PARIS calibration, 1 Dot=1200000/WPM usec. */
enum { DOT_CALIBRATION = 1200000 };


/* Default initial values for library controls. */
enum {
	CW_REC_ADAPTIVE_INITIAL  = false,  /* Initial adaptive receive setting */
	CW_REC_INITIAL_THRESHOLD = (DOT_CALIBRATION / CW_SPEED_INITIAL) * 2,   /* Initial adaptive speed threshold */
	CW_REC_INITIAL_NOISE_THRESHOLD = (DOT_CALIBRATION / CW_SPEED_MAX) / 2  /* Initial noise filter threshold */
};





/* TODO: what is the relationship between this constant and CW_REC_REPRESENTATION_CAPACITY?
   Both have value of 256. Coincidence? I don't think so. */
enum { CW_REC_STATISTICS_CAPACITY = 256 };



/* Receiver contains a fixed-length buffer for representation of received data.
   Capacity of the buffer is vastly longer than any practical representation.
   Don't know why, a legacy thing. */
enum { CW_REC_REPRESENTATION_CAPACITY = 256 };



/* Adaptive speed tracking for receiving. */
enum { CW_REC_AVERAGE_ARRAY_LENGTH = 4 };



/* Receive timing statistics.
   A circular buffer of entries indicating the difference between the
   actual and the ideal timing for a receive mark, tagged with the
   type of statistic held, and a circular buffer pointer.
   STAT_NONE must be zero so that the statistics buffer is initially empty. */
typedef enum {
	STAT_NONE = 0,
	STAT_DOT,
	STAT_DASH,
	STAT_END_ELEMENT,
	STAT_END_CHARACTER
} stat_type_t;



typedef struct {
	stat_type_t type;  /* Record type */
	int delta;         /* Difference between actual and ideal timing */
} cw_statistics_t;



/* A moving averages structure, comprising a small array of mark
   lengths, a circular index into the array, and a running sum of
   lengths of marks for efficient calculation of moving averages. */
typedef struct cw_tracking_struct {
	int buffer[CW_REC_AVERAGE_ARRAY_LENGTH];  /* Buffered mark lengths */
	int cursor;                               /* Circular buffer cursor */
	int sum;                                  /* Running sum */
} cw_tracking_t;




typedef struct {
	/* State of receiver state machine. */
	int state;

	int speed;

	int gap; /* Inter-mark gap, similar as in generator. */

	int noise_spike_threshold;
	bool is_adaptive_receive_enabled;

	/* Library variable which is automatically maintained from the Morse input
	   stream, rather than being settable by the user.
	   Initially 2-dot threshold for adaptive speed */
	int adaptive_receive_threshold;


	/* Setting this value may trigger a recalculation of some low
	   level timing parameters. */
	int tolerance;


	/* After changing receiver's receive speed, tolerance or adaptive mode,
	   some receiver's internal parameters need to be
	   re-calculated. This is a flag that shows when this needs to
	   be done. */
	bool parameters_in_sync;



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



	/* Receiver timing parameters */
	/* These are basic timing parameters which should be
	   recalculated each time client code demands changing some
	   higher-level parameter of receiver. */
	int dot_len_ideal;        /* Length of an ideal dot. [us] */
	int dot_len_min;          /* Minimal length of mark that will be identified as dot. [us] */
	int dot_len_max;          /* Maximal length of mark that will be identified as dot. [us] */

	int dash_len_ideal;       /* Length of an ideal dash. [us] */
	int dash_len_min;         /* Minimal length of mark that will be identified as dash. [us] */
	int dash_len_max;         /* Maximal length of mark that will be identified as dash. [us] */

	int eoe_len_ideal;        /* Ideal end of mark, for stats */
	int eoe_len_min;          /* Shortest end of mark allowable */
	int eoe_len_max;          /* Longest end of mark allowable */

	int eoc_len_ideal;        /* Ideal end of char, for stats */
	int eoc_len_min;          /* Shortest end of char allowable */
	int eoc_len_max;          /* Longest end of char allowable */

	/* These two fields have the same function as in
	   cw_gen_t. They are needed in function re-synchronizing
	   parameters. */
	int additional_delay;     /* More delay at the end of a char */
	int adjustment_delay;     /* More delay at the end of a word */



	/* Receiver statistics */
	cw_statistics_t statistics[CW_REC_STATISTICS_CAPACITY];
	int statistics_ind;


	/* Receiver speed tracking */
	cw_tracking_t dot_tracking;
	cw_tracking_t dash_tracking;

} cw_rec_t;





void cw_rec_reset_receive_parameters_internal(cw_rec_t *rec);
void cw_rec_sync_parameters_internal(cw_rec_t *rec);



#ifdef LIBCW_UNIT_TESTS

unsigned int test_cw_rec_mark_identify_internal(void);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_REC */
