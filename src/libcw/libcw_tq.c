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
   \file libcw_tq.c

   \brief Queue of tones to be converted by generator to pcm data and
   sent to audio sink.

   Tone queue - a circular list of tone durations and frequencies
   pending, with a pair of indexes: tail (enqueue) and head (dequeue).
   The indexes are used to manage addition and removal of tones from
   queue.


   The tone queue (the circular list) is implemented using constant
   size table.


   Explanation of "forever" tone:

   If a "forever" flag is set in a tone that is a last one on a tone
   queue, the tone should be constantly returned by dequeue function,
   without removing the tone - as long as it is a last tone on queue.

   Adding new, "non-forever" tone to the queue results in permanent
   dequeuing "forever" tone and proceeding to newly added tone.
   Adding the new "non-forever" tone ends generation of "forever" tone.

   The "forever" tone is useful for generating tones of length unknown
   in advance.

   dequeue() function recognizes the "forever" tone and acts as
   described above; there is no visible difference between dequeuing N
   separate "non-forever" tones of length L [us], and dequeuing a
   "forever" tone of length L [us] N times in a row.

   Because of some corner cases related to "forever" tones it is very
   strongly advised to set "low water mark" level to no less than 2
   tones.


   Tone queue data type is not visible to user of library's API. Tone
   queue is an integral part of a generator. Generator data type is
   visible to user of library's API.
*/




#include <inttypes.h> /* "PRIu32" */
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h> /* SIGALRM */
#include <unistd.h> /* sleep() */




#include "libcw.h"
#include "libcw_tq.h"
#include "libcw_tq_internal.h"
#include "libcw_gen.h"
#include "libcw_debug.h"
#include "libcw_signal.h"
#include "libcw2.h"




#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif




#define MSG_PREFIX "libcw/tq: "




/*
   The CW tone queue functions implement the following state graph:

                              (queue empty)
            +-----------------------------------------------------+
            |                                                     |
            |                                                     |
            |        (tone(s) added to queue,                     |
            v        dequeueing process started)                  |
   ----> CW_TQ_IDLE -------------------------------> CW_TQ_BUSY --+
                                                 ^        |
                                                 |        |
                                                 +--------+
                                             (queue not empty)


   Above diagram shows two states of a queue, but dequeue function
   returns three distinct values: CW_TQ_DEQUEUED,
   CW_TQ_NDEQUEUED_EMPTY, CW_TQ_NDEQUEUED_IDLE. Having these three
   values is important for the function that calls the dequeue
   function. If you ever intend to limit number of return values of
   dequeue function to two, you will also have to re-think how
   cw_gen_dequeue_and_generate_internal() operates.

   Future libcw API should (completely) hide tone queue from client
   code. The client code should only operate on a generator - enqueue
   tones to generator, flush a generator, register low water callback
   with generator etc. There is very little (or even no) need to
   explicitly reveal to client code this implementation detail called
   "tone queue".
*/





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




/* Not used anymore. 2015.02.22. */
#if 0
/* Remember that tail and head are of unsigned type.  Make sure that
   order of calculations is correct when tail < head. */
#define CW_TONE_QUEUE_LENGTH(m_tq)				\
	( m_tq->tail >= m_tq->head				\
	  ? m_tq->tail - m_tq->head				\
	  : m_tq->capacity - m_tq->head + m_tq->tail)		\

#endif





/**
   \brief Create new tone queue

   Allocate and initialize new tone queue structure.

   \return pointer to new tone queue on success
   \return NULL pointer on failure
*/
cw_tone_queue_t *cw_tq_new_internal(void)
{
	cw_tone_queue_t *tq = (cw_tone_queue_t *) malloc(sizeof (cw_tone_queue_t));
	if (!tq) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_TONE_QUEUE, CW_DEBUG_ERROR,
				      MSG_PREFIX "new: failed to malloc() tone queue");
		return (cw_tone_queue_t *) NULL;
	}

	int rv = pthread_mutex_init(&tq->mutex, NULL);
	cw_assert (!rv, MSG_PREFIX "new: failed to initialize mutex");

	pthread_mutex_lock(&tq->mutex);

	pthread_cond_init(&tq->wait_var, NULL);
	pthread_mutex_init(&tq->wait_mutex, NULL);

	pthread_cond_init(&tq->dequeue_var, NULL);
	pthread_mutex_init(&tq->dequeue_mutex, NULL);

	/* This function operates on cw_tq_t::wait_var and
	   cdw_tq_t::wait_mutex. Therefore it needs to be called
	   after pthread_X_init(). */
	cw_tq_make_empty_internal(tq);

	tq->low_water_mark = 0;
	tq->low_water_callback = NULL;
	tq->low_water_callback_arg = NULL;
	tq->call_callback = false;

	tq->gen = (cw_gen_t *) NULL; /* This field will be set by generator code. */

	rv = cw_tq_set_capacity_internal(tq, CW_TONE_QUEUE_CAPACITY_MAX, CW_TONE_QUEUE_HIGH_WATER_MARK_MAX);
	cw_assert (rv, MSG_PREFIX "new: failed to set initial capacity of tq");

	pthread_mutex_unlock(&tq->mutex);

	return tq;
}




/**
   \brief Delete tone queue

   Function deallocates all resources held by \p tq, deallocates the \p tq
   itself, and sets the pointer to NULL.

   \reviewed on 2017-01-30

   \param tq - tone queue to delete
*/
void cw_tq_delete_internal(cw_tone_queue_t **tq)
{
	cw_assert (tq, MSG_PREFIX "delete: pointer to tq is NULL");

	if (!tq || !*tq) {
		return;
	}


	/* Don't call pthread_cond_destroy().

	   When pthread_cond_wait() is waiting for signal, and a
	   SIGINT signal arrives, the _wait() function will be
	   interrupted, application's signal handler will call
	   cw_gen_delete(), which will call cw_tq_delete_internal(),
	   which will call pthread_cond_destroy().

	   pthread_cond_destroy() called from (effectively) signal
	   handler will signal all waiters to release condition
	   variable before destroying conditional variable, but since
	   our _wait() is interrupted by signal, it won't release the
	   condition variable.

	   So we have a deadlock: _destroy() telling _wait() to stop
	   waiting, but _wait() being interrupted by signal, handled
	   by function called _destroy().

	   So don't call _destroy(). */

	//pthread_cond_destroy(&(*tq)->wait_var);
	pthread_mutex_destroy(&(*tq)->wait_mutex);

	//pthread_cond_destroy(&(*tq)->dequeue_var);
	pthread_mutex_destroy(&(*tq)->dequeue_mutex);


	pthread_mutex_destroy(&(*tq)->mutex);


	free(*tq);
	*tq = (cw_tone_queue_t *) NULL;

	return;
}




/**
   \brief Reset state of given tone queue

   This makes the \p tq empty, but without calling low water mark callback.

*/
void cw_tq_make_empty_internal(cw_tone_queue_t * tq)
{
	int rv = pthread_mutex_trylock(&tq->mutex);
	cw_assert (rv == EBUSY, MSG_PREFIX "make empty: resetting tq state outside of mutex!");

	pthread_mutex_lock(&tq->wait_mutex);

	tq->head = 0;
	tq->tail = 0;
	tq->len = 0;
	tq->state = CW_TQ_IDLE;

	//fprintf(stderr, MSG_PREFIX "make empty: broadcast on tq->len = 0\n");
	pthread_cond_broadcast(&tq->wait_var);
	pthread_mutex_unlock(&tq->wait_mutex);

	return;
}





/**
   \brief Set capacity and high water mark for queue

   Set two parameters of queue: total capacity of the queue, and high
   water mark. When calling the function, client code must provide
   valid values of both parameters.

   Calling the function *by a client code* for a queue is optional, as
   a queue has these parameters always set to default values
   (CW_TONE_QUEUE_CAPACITY_MAX and CW_TONE_QUEUE_HIGH_WATER_MARK_MAX)
   by internal call to cw_tq_new_internal().

   \p capacity must be no larger than CW_TONE_QUEUE_CAPACITY_MAX.
   \p high_water_mark must be no larger than CW_TONE_QUEUE_HIGH_WATER_MARK_MAX.

   Both values must be larger than zero (this condition is subject to
   changes in future revisions of the library).

   \p high_water_mark must be no larger than \p capacity.

   \errno EINVAL - any of the two parameters (\p capacity or \p high_water_mark) is invalid.

   \param tq - tone queue to configure
   \param capacity - new capacity of queue
   \param high_water_mark - high water mark for the queue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_set_capacity_internal(cw_tone_queue_t *tq, size_t capacity, size_t high_water_mark)
{
	cw_assert (tq, MSG_PREFIX "set capacity: tq is NULL");
	if (!tq) {
		return CW_FAILURE;
	}

	if (high_water_mark == 0 || high_water_mark > CW_TONE_QUEUE_HIGH_WATER_MARK_MAX) {
		/* If we allowed high water mark to be zero, the queue
		   would not accept any new tones: it would constantly
		   be full. Any attempt to enqueue any tone would
		   result in "sorry, new tones would reach above
		   high_water_mark of the queue". */
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (capacity == 0 || capacity > CW_TONE_QUEUE_CAPACITY_MAX) {
		/* Tone queue of capacity zero doesn't make much
		   sense, so capacity == 0 is not allowed. */
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (high_water_mark > capacity) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	tq->capacity = capacity;
	tq->high_water_mark = high_water_mark;

	return CW_SUCCESS;
}




/**
   \brief Return capacity of a queue

   \param tq - tone queue, for which you want to get capacity

   \return capacity of tone queue
*/
size_t cw_tq_get_capacity_internal(cw_tone_queue_t *tq)
{
	cw_assert (tq, MSG_PREFIX "get capacity: tone queue is NULL");
	return tq->capacity;
}




/**
   \brief Return high water mark of a queue

   \reviewed on 2017-01-30

   \param tq - tone queue, for which you want to get high water mark

   \return high water mark of tone queue
*/
size_t cw_tq_get_high_water_mark_internal(const cw_tone_queue_t *tq)
{
	cw_assert (tq, MSG_PREFIX "get high water mark: tone queue is NULL");

	return tq->high_water_mark;
}




/**
   \brief Return number of items (tones) on tone queue

   \param tq - tone queue

   \return the count of tones currently held in the tone queue
*/
size_t cw_tq_length_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&tq->mutex);
	size_t len = tq->len;
	pthread_mutex_unlock(&tq->mutex);

	return len;
}




/**
   \brief Get previous index to queue

   Calculate index of previous element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   \param tq - tone queue for which to calculate previous index
   \param ind - index in relation to which to calculate index of previous element in queue

   \return index of previous element in queue
*/
size_t cw_tq_prev_index_internal(const cw_tone_queue_t *tq, size_t ind)
{
	return ind == 0 ? tq->capacity - 1 : ind - 1;
}




/**
   \brief Get next index to queue

   Calculate index of next element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   \param tq - tone queue for which to calculate next index
   \param ind - index in relation to which to calculate index of next element in queue

   \return index of next element in queue
*/
size_t cw_tq_next_index_internal(const cw_tone_queue_t *tq, size_t ind)
{
	return ind == tq->capacity - 1 ? 0 : ind + 1;
}




/**
   \brief Dequeue a tone from tone queue

   If there are any tones in queue (i.e. queue's state is not
   CW_TQ_IDLE), function copies tone from \p tq queue into \p tone
   supplied by caller, removes the tone from \p tq queue (with
   exception for "forever" tone) and returns CW_SUCCESS (i.e. "dequeued
   (successfully)").

   If there are no tones in \p tq queue (i.e. queue's state is
   CW_TQ_IDLE), function does nothing with \p tone, and returns CW_FAILURE
   (i.e. "not dequeued").

   Notice that returned value does not describe current internal state
   of tone queue, only whether contents of \p tone has been updated
   with dequeued tone or not.

   dequeue() is not a totally dumb function. It understands how
   "forever" tone works and how it should be handled.  If the last
   tone in queue has "forever" flag set, the function won't
   permanently dequeue it. Instead, it will keep returning (through \p
   tone) the tone on every call, until a new tone is added to the
   queue after the "forever" tone. Since "forever" tone is successfully
   copied into \p tone, function returns true on "forever" tone.

   \p tq must be a valid queue.
   \p tone must be allocated by caller.

   If queue \p tq has registered low water callback function, and
   condition to call the function is met after dequeue has occurred,
   the function calls the callback.

   \param tq - tone queue
   \param tone - dequeued tone

   \return CW_SUCCESS if a tone has been dequeued
   \return CW_FAILURE if no tone has been dequeued
*/
int cw_tq_dequeue_internal(cw_tone_queue_t *tq, /* out */ cw_tone_t *tone)
{
	pthread_mutex_lock(&tq->mutex);
	pthread_mutex_lock(&tq->wait_mutex);

	cw_assert (tq->state == CW_TQ_IDLE || tq->state == CW_TQ_BUSY,
		   MSG_PREFIX "dequeue: unexpected value of tq->state = %d", tq->state);

	if (tq->state == CW_TQ_IDLE) {
		/* Ignore calls if our state is idle. */
		pthread_mutex_unlock(&tq->wait_mutex);
		pthread_mutex_unlock(&tq->mutex);
		return CW_FAILURE;

	} else { /* tq->state == CW_TQ_BUSY */
		cw_assert (tq->len, MSG_PREFIX "dequeue: tone queue is CW_TQ_BUSY, but tq->len = %zu\n", tq->len);

		bool call_callback = cw_tq_dequeue_sub_internal(tq, tone);

		if (!tq->len) {
			tq->state = CW_TQ_IDLE;
		}

		pthread_mutex_unlock(&tq->wait_mutex);
		pthread_mutex_unlock(&tq->mutex);

		/* Since client's callback can use libcw functions
		   that call pthread_mutex_lock(&tq->...), we should
		   call the callback *after* we unlock queue's mutexes
		   in this function. */
		if (call_callback) {
			(*(tq->low_water_callback))(tq->low_water_callback_arg);
		}

		return CW_SUCCESS;
	}
}




/**
   \brief Handle dequeueing of tone from non-empty tone queue

   Function gets a tone from head of the queue.

   If this was a last tone in queue, and it was a "forever" tone, the
   tone is not removed from the queue (the philosophy of "forever"
   tone), and "low watermark" condition is not checked.

   Otherwise remove the tone from tone queue, check "low watermark"
   condition, and return value of the check (true/false).

   In any case, dequeued tone is returned through \p tone. \p tone
   must be a valid pointer provided by caller.

   TODO: add unit tests

   \param tq - tone queue
   \param tone - dequeued tone (output from the function)

   \return true if a condition for calling "low watermark" callback is true
   \return false otherwise
*/
bool cw_tq_dequeue_sub_internal(cw_tone_queue_t * tq, /* out */ cw_tone_t * tone)
{
	CW_TONE_COPY(tone, &(tq->queue[tq->head]));

	if (tone->is_forever && tq->len == 1) {
		/* Don't permanently remove the last tone that is
		   "forever" tone in queue. Keep it in tq until client
		   code adds next tone (this means possibly waiting
		   forever). Queue's head should not be
		   iterated. "forever" tone should be played by caller
		   code, this is why we return the tone through
		   function's argument. */

		/* Don't call "low watermark" callback for "forever"
		   tone. As the function's top-level comment has
		   stated: avoid endlessly calling the callback if the
		   only queued tone is "forever" tone.*/
		return false;
	}

	/* Used to check if we passed tq's low level watermark. */
	size_t tq_len_before = tq->len;

	/* Dequeue. We already have the tone, now update tq's state. */
	tq->head = cw_tq_next_index_internal(tq, tq->head);
	tq->len--;
	//fprintf(stderr, MSG_PREFIX "dequeue sub: broadcast on tq->len--\n");
	pthread_cond_broadcast(&tq->wait_var);


	if (tq->len == 0) {
		/* Verify basic property of empty tq. */
		cw_assert (tq->head == tq->tail, MSG_PREFIX "dequeue sub: head: %zu, tail: %zu", tq->head, tq->tail);
	}


#if 0   /* Disabled because these debug messages produce lots of output
	   to console. Enable only when necessary. */
	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      MSG_PREFIX "dequeue sub: dequeue tone %d us, %d Hz", tone->len, tone->frequency);
	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      MSG_PREFIX "dequeue sub: head = %zu, tail = %zu, length = %zu -> %zu",
		      tq->head, tq->tail, tq_len_before, tq->len);
#endif

	/* You can remove this assert in future. It is only temporary,
	   to check that some changes introduced on 2015.03.01 didn't
	   break one assumption. */
	cw_assert (!(tone->is_forever && tq_len_before == 1), MSG_PREFIX "dequeue sub: 'forever' tone appears!");


	bool call_callback = false;
	if (tq->low_water_callback) {
		/* It may seem that the double condition in 'if ()' is
		   redundant, but for some reason it is necessary. Be
		   very, very careful when modifying this. */
		if (tq_len_before > tq->low_water_mark
		    && tq->len <= tq->low_water_mark) {

			call_callback = true;
		}
	}

	return call_callback;
}




/**
   \brief Add tone to tone queue

   This routine adds the new tone to the queue, and - if necessary -
   sends a signal to generator, so that the generator can dequeue the
   tone.

   The function does not accept tones with frequency outside of
   CW_FREQUENCY_MIN-CW_FREQUENCY_MAX range.

   If length of a tone (tone->len) is zero, the function does not
   add it to tone queue and returns CW_SUCCESS.

   The function does not accept tones with negative values of len.

   \errno EINVAL - invalid values of \p tone
   \errno EAGAIN - tone not enqueued because tone queue is full

   \param tq - tone queue
   \param tone - tone to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_enqueue_internal(cw_tone_queue_t *tq, cw_tone_t *tone)
{
	cw_assert (tq, MSG_PREFIX "enqueue: tone queue is null");
	cw_assert (tone, MSG_PREFIX "enqueue: tone is null");

	/* Check the arguments given for realistic values. */
	if (tone->frequency < CW_FREQUENCY_MIN
	    || tone->frequency > CW_FREQUENCY_MAX) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	if (tone->len < 0) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	if (tone->len == 0) {
		/* Drop empty tone. It won't be played anyway, and for
		   now there are no other good reasons to enqueue
		   it. While it may happen in higher-level code to
		   create such tone, but there is no need to spend
		   time on it here. */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_INFO,
			      MSG_PREFIX "enqueue: ignoring tone with len == 0");
		return CW_SUCCESS;
	}


	pthread_mutex_lock(&tq->mutex);
	pthread_mutex_lock(&tq->wait_mutex);

	if (tq->len == tq->capacity) {
		/* Tone queue is full. */

		errno = EAGAIN;
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_ERROR,
			      MSG_PREFIX "enqueue: can't enqueue tone, tq is full");
		pthread_mutex_unlock(&tq->wait_mutex);
		pthread_mutex_unlock(&tq->mutex);

		return CW_FAILURE;
	}


	// cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG, MSG_PREFIX "enqueue: enqueue tone %d us, %d Hz", tone->len, tone->frequency);

	/* Enqueue the new tone.

	   Notice that tail is incremented after adding a tone. This
	   means that for empty tq new tone is inserted at index
	   tail == head (which should be kind of obvious). */
	tq->queue[tq->tail] = *tone;

	tq->tail = cw_tq_next_index_internal(tq, tq->tail);
	tq->len++;
	// fprintf(stderr, MSG_PREFIX "enqueue: broadcast on tq->len++\n");
	pthread_cond_broadcast(&tq->wait_var);


	if (tq->state == CW_TQ_IDLE) {
		tq->state = CW_TQ_BUSY;

		/* A loop in cw_gen_dequeue_and_play_internal()
		   function may await for the queue to be filled with
		   new tones to dequeue and play.  It waits for a
		   notification from tq that there are some new tones
		   in tone queue. This is a right place and time to
		   send such notification. */
		pthread_mutex_lock(&tq->dequeue_mutex);
		pthread_cond_signal(&tq->dequeue_var); /* Use pthread_cond_signal() because there is only one listener. */
		pthread_mutex_unlock(&tq->dequeue_mutex);
	}

	pthread_mutex_unlock(&tq->wait_mutex);
	pthread_mutex_unlock(&tq->mutex);
	return CW_SUCCESS;
}




/**
   \brief Register callback for low queue state

   Register a function to be called automatically by the dequeue routine
   whenever the tone queue falls to a given \p level. To be more precise:
   the callback is called by queue's dequeue function if, after dequeueing a tone,
   the function notices that tone queue length has become equal or less
   than \p level.

   \p callback_arg may be used to give a value passed back on callback
   calls.  A NULL function pointer suppresses callbacks.

   \errno EINVAL - \p level is invalid

   \reviewed on 2017-01-30

   \param tq - tone queue
   \param callback_func - callback function to be registered
   \param callback_arg - argument for callback_func to pass return value
   \param level - low level of queue triggering callback call

   \return CW_SUCCESS on successful registration
   \return CW_FAILURE on failure
*/
int cw_tq_register_low_level_callback_internal(cw_tone_queue_t * tq, cw_queue_low_callback_t callback_func, void * callback_arg, size_t level)
{
	if (level >= tq->capacity) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Store the function and low water mark level. */
	tq->low_water_mark = level;
	tq->low_water_callback = callback_func;
	tq->low_water_callback_arg = callback_arg;

	return CW_SUCCESS;
}




/**
   \brief Wait for the current tone to complete

   The routine always returns CW_SUCCESS.

   TODO: add unit test for this function.

   \param tq - tone queue

   \return CW_SUCCESS
*/
int cw_tq_wait_for_tone_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&tq->wait_mutex);
	pthread_cond_wait(&tq->wait_var, &tq->wait_mutex);
	pthread_mutex_unlock(&tq->wait_mutex);


#if 0   /* Original implementation using signals. */ /* This code has been disabled some time before 2017-01-30. */
	/* Wait for the head index to change or the dequeue to go idle. */
	size_t check_tq_head = tq->head;
	while (tq->head == check_tq_head && tq->state != CW_TQ_IDLE) {
		cw_signal_wait_internal();
	}
#endif
	return CW_SUCCESS;
}




/**
   \brief Wait for the tone queue to drain until only as many tones as given in level remain queued

   This function is for use by programs that want to optimize themselves
   to avoid the cleanup that happens when the tone queue drains completely;
   such programs have a short time in which to add more tones to the queue.

   The function returns when queue's level is equal or lower than \p
   level.  If at the time of function call the level of queue is
   already equal or lower than \p level, function returns immediately.

   Notice that generator must be running (started with cw_gen_start())
   when this function is called, otherwise it will be waiting forever
   for a change of tone queue's level that will never happen.

   \reviewed on 2017-01-30

   \param tq - tone queue
   \param level - low level in queue, at which to return

   \return CW_SUCCESS
*/
int cw_tq_wait_for_level_internal(cw_tone_queue_t *tq, size_t level)
{
	/* Wait until the queue length is at or below given level. */
	pthread_mutex_lock(&tq->wait_mutex);
	while (tq->len > level) {
		pthread_cond_wait(&tq->wait_var, &tq->wait_mutex);
	}
	pthread_mutex_unlock(&tq->wait_mutex);


#if 0   /* Original implementation using signals. */  /* This code has been disabled some time before 2017-01-30. */
	/* Wait until the queue length is at or below critical level. */
	while (cw_tq_length_internal(tq) > level) {
		cw_signal_wait_internal();
	}
#endif
	return CW_SUCCESS;
}





/**
   \brief See if the tone queue is full

   This is a helper subroutine created so that I can pass a test tone
   queue in unit tests. The 'cw_is_tone_queue_full() works only on
   default tone queue object.

   \reviewed on 2017-01-30

   \param tq - tone queue to check

   \return true if tone queue is full
   \return false if tone queue is not full
*/
bool cw_tq_is_full_internal(const cw_tone_queue_t *tq)
{
	return tq->len == tq->capacity;
}





/**
   \brief Force emptying tone queue. Wait until it's really empty.

   Notice that because this function uses
   cw_tq_wait_for_level_internal(), generator must be running (started
   with cw_gen_start()) when this function is called, otherwise it
   will be waiting forever for a change of tone queue's level that
   will never happen.

   \reviewed on 2017-01-30

   \param tq
*/
void cw_tq_flush_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&tq->mutex);
	/* Force zero length state. */
	cw_tq_make_empty_internal(tq);
	pthread_mutex_unlock(&tq->mutex);


	/* TODO: is this necessary? We have already reset queue
	   state. */
	cw_tq_wait_for_level_internal(tq, 0);


#if 0   /* Original implementation using signals. */ /* This code has been disabled some time before 2017-01-30. */
	/* If we can, wait until the dequeue goes idle. */
	if (!cw_sigalrm_is_blocked_internal()) {
		cw_tq_wait_for_level_internal(tq, 0);
	}
#endif

	return;
}




bool cw_tq_is_busy_internal(cw_tone_queue_t *tq)
{
	return CW_TQ_BUSY == tq->state;
}




/**
   \brief Attempt to remove all tones constituting full, single character

   Try to remove all tones until and including first tone with ->is_first tone flag set.

   The function removes character's tones only if all the tones,
   including the first tone in the character, are still in tone queue.

   \param tq - tone queue
*/
void cw_tq_handle_backspace_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&tq->mutex);

	size_t len = tq->len;
	size_t idx = tq->tail;
	bool is_found = false;

	while (len > 0) {
		--len;
		idx = cw_tq_prev_index_internal(tq, idx);
		if (tq->queue[idx].is_first) {
			is_found = true;
			break;
		}
	}

	if (is_found) {
		tq->len = len;
		tq->tail = idx;
	}

	pthread_mutex_unlock(&tq->mutex);
}




/* *** Unit tests *** */
