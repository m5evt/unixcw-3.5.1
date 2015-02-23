/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_TQ
#define H_LIBCW_TQ





#include <stdint.h>     /* uint32_t */
#include <pthread.h>    /* pthread_mutex_t */
#include <stdbool.h>    /* bool */





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





/* Tone queue states, used also as return values from dequeue function. */
enum cw_queue_state {
	CW_TQ_IDLE  = 0,
	CW_TQ_BUSY  = 1
};


/* Additional value used as return value from dequeue function. */
enum {
	CW_TQ_EMPTY = 2
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





struct cw_gen_struct;

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
	/* Set to true when conditions for calling low water callback
	   are true. The flag is set in cw_tq module, but the callback
	   itself may be called outside of the module, e.g. by cw_gen
	   code. */
	bool         call_callback;

	pthread_mutex_t mutex;

	/* Generator associated with a tone queue. */
	struct cw_gen_struct *gen;
} cw_tone_queue_t;



cw_tone_queue_t *cw_tq_new_internal(void);
void             cw_tq_delete_internal(cw_tone_queue_t **tq);
void             cw_tq_flush_internal(cw_tone_queue_t *tq);

uint32_t cw_tq_get_capacity_internal(cw_tone_queue_t *tq);
uint32_t cw_tq_length_internal(cw_tone_queue_t *tq);
int      cw_tq_enqueue_internal(cw_tone_queue_t *tq, cw_tone_t *tone);
int      cw_tq_dequeue_internal(cw_tone_queue_t *tq, cw_tone_t *tone);

int  cw_tq_wait_for_level_internal(cw_tone_queue_t *tq, uint32_t level);
int  cw_tq_register_low_level_callback_internal(cw_tone_queue_t *tq, void (*callback_func)(void*), void *callback_arg, int level);
bool cw_tq_is_busy_internal(cw_tone_queue_t *tq);
int  cw_tq_wait_for_tone_internal(cw_tone_queue_t *tq);
int  cw_tq_wait_for_tone_queue_internal(cw_tone_queue_t *tq);
void cw_tq_reset_internal(cw_tone_queue_t *tq);
bool cw_tq_is_full_internal(cw_tone_queue_t *tq);






#ifdef LIBCW_UNIT_TESTS

unsigned int test_cw_tq_new_internal(void);
unsigned int test_cw_tq_init_internal(void);
unsigned int test_cw_tq_get_capacity_internal(void);
unsigned int test_cw_tq_prev_index_internal(void);
unsigned int test_cw_tq_next_index_internal(void);
unsigned int test_cw_tq_length_internal(void);
unsigned int test_cw_tq_enqueue_internal_1(void);
unsigned int test_cw_tq_enqueue_internal_2(void);
unsigned int test_cw_tq_dequeue_internal(void);
unsigned int test_cw_tq_is_full_internal(void);
unsigned int test_cw_tq_test_capacity_1(void);
unsigned int test_cw_tq_test_capacity_2(void);
unsigned int test_cw_queue_tone(void);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_TQ */
