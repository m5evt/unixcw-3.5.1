#ifndef H_LIBCW_TQ
#define H_LIBCW_TQ





#include <stdint.h>     /* uint32_t */
#include <pthread.h>    /* pthread_mutex_t */
#include <stdbool.h>    /* bool */





/* Return values from cw_tone_queue_dequeue_internal(). */
enum {
	CW_TQ_JUST_EMPTIED = 0,
	CW_TQ_STILL_EMPTY  = 1,
	CW_TQ_NONEMPTY     = 2
};





/* Right now there is no function that would calculate number of tones
   representing given character or string, so there is no easy way to
   present exact relationship between capacity of tone queue and
   number of characters that it can hold.  TODO: perhaps we could
   write utility functions to do that calculation? */

/* TODO: create tests that validate correctness of handling of tone
   queue capacity. See if we really handle the capacity correctly. */

enum {
	/* Default and maximum values of two basic parameters of tone
	   queue: capacity and high water mark. The parameters can be
	   modified using suitable function. */

	/* Tone queue will accept at most "capacity" tones. */
	CW_TONE_QUEUE_CAPACITY_MAX = 3000,        /* ~= 5 minutes at 12 WPM */

	/* Tone queue will refuse to accept new tones (characters?) if
	   number of tones in queue (queue length) is already equal or
	   larger than queue's high water mark. */
	CW_TONE_QUEUE_HIGH_WATER_MARK_MAX = 2900
};





enum cw_queue_state {
	QS_IDLE,
	QS_BUSY
};





typedef struct {
	/* Frequency of a tone. */
	int frequency;

	/* Duration of a tone, in microseconds. */
	int usecs;

	/* Duration of a tone, in samples.
	   This is a derived value, a function of usecs and sample rate. */

	/* TODO: come up with thought-out, consistent type system for
	   samples and usecs. The type system should take into
	   consideration very long duration of tones in QRSS. */
	int64_t n_samples;

	/* We need two indices to gen->buffer, indicating beginning and end
	   of a subarea in the buffer.
	   The subarea is not the same as gen->buffer for variety of reasons:
	    - buffer length is almost always smaller than length of a dash,
	      a dot, or inter-element space that we want to produce;
	    - moreover, length of a dash/dot/space is almost never an exact
	      multiple of length of a buffer;
            - as a result, a sound representing a dash/dot/space may start
	      and end anywhere between beginning and end of the buffer;

	   A workable solution is have a subarea of the buffer, a window,
	   into which we will write a series of fragments of calculated sound.

	   The subarea won't wrap around boundaries of the buffer. "stop"
	   will be no larger than "gen->buffer_n_samples - 1", and it will
	   never be smaller than "stop".

	   "start" and "stop" mark beginning and end of the subarea.
	   Very often (in the middle of the sound), "start" will be zero,
	   and "stop" will be "gen->buffer_n_samples - 1".

	   Sine wave (sometimes with amplitude = 0) will be calculated for
	   cells ranging from cell "start" to cell "stop", inclusive. */
	int sub_start;
	int sub_stop;

	/* a tone can start and/or end abruptly (which may result in
	   audible clicks), or its beginning and/or end can have form
	   of slopes (ramps), where amplitude increases/decreases less
	   abruptly than if there were no slopes;

	   using slopes reduces audible clicks at the beginning/end of
	   tone, and can be used to shape spectrum of a tone;

	   AFAIK most desired shape of a slope looks like sine wave;
	   most simple one is just a linear slope;

	   slope area should be integral part of a tone, i.e. it shouldn't
	   make the tone longer than usecs/n_samples;

	   a tone with rising and falling slope should have this length
	   (in samples):
	   slope_n_samples   +   (n_samples - 2 * slope_n_samples)   +   slope_n_samples

	   libcw allows following slope area scenarios (modes):
	   1. no slopes: tone shouldn't have any slope areas (i.e. tone
	      with constant amplitude);
	   1.a. a special case of this mode is silent tone - amplitude
	        of a tone is zero for whole duration of the tone;
	   2. tone has nothing more than a single slope area (rising or
	      falling); there is no area with constant amplitude;
	   3. a regular tone, with area of rising slope, then area with
	   constant amplitude, and then falling slope;

	   currently, if a tone has both slopes (rising and falling), both
	   slope areas have to have the same length; */
	int slope_iterator;     /* counter of samples in slope area */
	int slope_mode;         /* mode/scenario of slope */
	int slope_n_samples;    /* length of slope area */
} cw_tone_t;





typedef struct {
	volatile cw_tone_t queue[CW_TONE_QUEUE_CAPACITY_MAX];

	/* Tail index of tone queue. Index of last (newest) inserted
	   tone, index of tone to be dequeued from the list as a last
	   one.

	   The index is incremented *after* adding a tone to queue. */
	volatile uint32_t tail;

	/* Head index of tone queue. Index of first (oldest) tone
	   inserted to the queue. Index of the tone to be dequeued
	   from the queue as a first one. */
	volatile uint32_t head;

	volatile enum cw_queue_state state;

	uint32_t capacity;
	uint32_t high_water_mark;
	uint32_t len;

	/* It's useful to have the tone queue dequeue function call
	   a client-supplied callback routine when the amount of data
	   in the queue drops below a defined low water mark.
	   This routine can then refill the buffer, as required. */
	volatile uint32_t low_water_mark;
	void       (*low_water_callback)(void*);
	void        *low_water_callback_arg;

	pthread_mutex_t mutex;
} cw_tone_queue_t;





int      cw_tone_queue_init_internal(cw_tone_queue_t *tq);

/* Some day the following two functions will be made public. */
int      cw_tone_queue_set_capacity_internal(cw_tone_queue_t *tq, uint32_t capacity, uint32_t high_water_mark);
__attribute__((unused)) uint32_t cw_tone_queue_get_high_water_mark_internal(cw_tone_queue_t *tq);
uint32_t cw_tone_queue_get_capacity_internal(cw_tone_queue_t *tq);

uint32_t cw_tone_queue_length_internal(cw_tone_queue_t *tq);
__attribute__((unused)) uint32_t cw_tone_queue_prev_index_internal(cw_tone_queue_t *tq, uint32_t current);
uint32_t cw_tone_queue_next_index_internal(cw_tone_queue_t *tq, uint32_t current);
int      cw_tone_queue_enqueue_internal(cw_tone_queue_t *tq, cw_tone_t *tone);
int      cw_tone_queue_dequeue_internal(cw_tone_queue_t *tq, cw_tone_t *tone);
bool     cw_tone_queue_is_full_internal(cw_tone_queue_t *tq);





/* Unit tests. */

#ifdef LIBCW_UNIT_TESTS
int          test_cw_tone_queue_capacity_test_init(cw_tone_queue_t *tq, uint32_t capacity, uint32_t high_water_mark, int head_shift);

unsigned int test_cw_tone_queue_init_internal(void);
unsigned int test_cw_tone_queue_get_capacity_internal(void);
unsigned int test_cw_tone_queue_prev_index_internal(void);
unsigned int test_cw_tone_queue_next_index_internal(void);
unsigned int test_cw_tone_queue_length_internal(void);
unsigned int test_cw_tone_queue_enqueue_internal(void);
unsigned int test_cw_tone_queue_dequeue_internal(void);
unsigned int test_cw_tone_queue_is_full_internal(void);
unsigned int test_cw_tone_queue_test_capacity1(void);
unsigned int test_cw_tone_queue_test_capacity2(void);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_TQ */
