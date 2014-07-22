#ifndef H_LIBCW_TQ
#define H_LIBCW_TQ


/* return values from cw_tone_queue_dequeue_internal() */
#define CW_TQ_JUST_EMPTIED 0
#define CW_TQ_STILL_EMPTY  1
#define CW_TQ_NONEMPTY     2





enum cw_queue_state {
	QS_IDLE,
	QS_BUSY
};




struct cw_tone_queue_struct {
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
}; /* typedef cw_tone_queue_t */


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
