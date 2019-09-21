/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef H_LIBCW_REC
#define H_LIBCW_REC





#include <stdbool.h>
#include <sys/time.h> /* struct timeval */





#include "libcw.h"





/* Dot length magic number.

   From PARIS calibration, 1 Dot length [us] = 1200000 / speed [wpm].

   This variable is used in generator code as well. */
enum { CW_DOT_CALIBRATION = 1200000 };


/* "RS" stands for "Receiver State" */
enum {
	RS_IDLE,          /* Representation buffer is empty and ready to accept data. */
	RS_MARK,          /* Mark. */
	RS_IMARK_SPACE,   /* Inter-mark space. */
	RS_EOC_GAP,       /* Gap after a character, without error (EOC = end-of-character). */
	RS_EOW_GAP,       /* Gap after a word, without error (EOW = end-of-word). */
	RS_EOC_GAP_ERR,   /* Gap after a character, with error. */
	RS_EOW_GAP_ERR    /* Gap after a word, with error. */
};


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



	/* Data structures for calculating averaged length of dots and
	   dashes. The averaged lengths are used for adaptive tracking
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


typedef struct cw_rec_struct cw_rec_t;




/* Other helper functions. */
void cw_rec_reset_parameters_internal(cw_rec_t *rec);
void cw_rec_sync_parameters_internal(cw_rec_t *rec);
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
				    int *adaptive_threshold);
void cw_rec_get_statistics_internal(cw_rec_t *rec, double *dot_sd, double *dash_sd,
				    double *element_end_sd, double *character_end_sd);
int cw_rec_get_buffer_length_internal(const cw_rec_t *rec);
int cw_rec_get_receive_buffer_capacity_internal(void);




#ifdef LIBCW_UNIT_TESTS

unsigned int test_cw_rec_with_base_data_fixed(void);
unsigned int test_cw_rec_with_random_data_fixed(void);
unsigned int test_cw_rec_with_random_data_adaptive(void);
unsigned int test_cw_get_receive_parameters(void);


#include "libcw_test.h"

unsigned int test_cw_rec_identify_mark_internal(cw_test_stats_t * stats);
unsigned int test_cw_rec_test_with_base_constant(cw_test_stats_t * stats);
unsigned int test_cw_rec_test_with_random_constant(cw_test_stats_t * stats);
unsigned int test_cw_rec_test_with_random_varying(cw_test_stats_t * stats);
unsigned int test_cw_rec_get_parameters(cw_test_stats_t * stats);
unsigned int test_cw_rec_parameter_getters_setters_1(cw_test_stats_t * stats);
unsigned int test_cw_rec_parameter_getters_setters_2(cw_test_stats_t * stats);


#endif /* #ifdef LIBCW_UNIT_TESTS */




#endif /* #ifndef H_LIBCW_REC */
