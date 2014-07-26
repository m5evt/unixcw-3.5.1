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


#include <inttypes.h> /* "PRIu32" */
#include <errno.h>
#include <pthread.h>
#include <signal.h> /* SIGALRM */

#include "libcw_debug.h"
#include "libcw_internal.h"
#include "libcw_tq.h"
#include "libcw_key.h"



/**
   \file libcw_tq.c

   Tone queue - a circular list of tone durations and frequencies pending,
   and a pair of indexes, tail (enqueue) and head (dequeue) to manage
   additions and asynchronous sending.

   The CW tone queue functions implement the following state graph:

                     (queue empty)
            +-------------------------------+
            |                               |
            v    (queue started)            |
   ----> QS_IDLE ---------------> QS_BUSY --+
                                  ^     |
                                  |     |
                                  +-----+
                              (queue not empty)
*/




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;

extern cw_gen_t **cw_generator;


/* Tone queue associated with a generator.  Every generator should
   have a tone queue from which to draw/dequeue tones to play. The
   library also provides complementary function for enqueueing tones
   in the tone queue.

   Since generator is a global variable, so is tone queue (at least
   for now). */
cw_tone_queue_t cw_tone_queue;





#if 0
/* Remember that tail and head are of unsigned type.  Make sure that
   order of calculations is correct when tail < head. */
#define CW_TONE_QUEUE_LENGTH(m_tq)				\
	( m_tq->tail >= m_tq->head				\
	  ? m_tq->tail - m_tq->head				\
	  : m_tq->capacity - m_tq->head + m_tq->tail)		\

#endif




/**
   \brief Initialize a tone queue

   Initialize tone queue structure - \p tq

   testedin::test_cw_tone_queue_init_internal()

   \param tq - tone queue to initialize

   \return CW_SUCCESS on completion
*/
int cw_tone_queue_init_internal(cw_tone_queue_t *tq)
{
	int rv = pthread_mutex_init(&tq->mutex, NULL);
	assert (!rv);

	pthread_mutex_lock(&tq->mutex);

	tq->tail = 0;
	tq->head = 0;
	tq->len = 0;
	tq->state = QS_IDLE;

	tq->low_water_mark = 0;
	tq->low_water_callback = NULL;
	tq->low_water_callback_arg = NULL;

	rv = cw_tone_queue_set_capacity_internal(tq, CW_TONE_QUEUE_CAPACITY_MAX, CW_TONE_QUEUE_HIGH_WATER_MARK_MAX);
	assert (rv);

	pthread_mutex_unlock(&tq->mutex);

	return CW_SUCCESS;
}





/**
   \brief Set capacity and high water mark for queue

   Set two parameters of queue: total capacity of the queue, and high
   water mark. When calling the function, client code must provide
   valid values of both parameters.

   Calling the function *by a client code* for a queue is optional, as
   a queue has these parameters always set to default values
   (CW_TONE_QUEUE_CAPACITY_MAX and CW_TONE_QUEUE_HIGH_WATER_MARK_MAX)
   by internal call to cw_tone_queue_init_internal().

   \p capacity must be no larger than CW_TONE_QUEUE_CAPACITY_MAX.
   \p high_water_mark must be no larger than  CW_TONE_QUEUE_HIGH_WATER_MARK_MAX.

   Both values must be larger than zero (this condition is subject to
   changes in future revisions of the library).

   \p high_water_mark must be no larger than \p capacity.

   Functions set errno to EINVAL if any of the two parameters is invalid.

   testedin::test_cw_tone_queue_capacity_test_init()

   \param tq - tone queue to configure
   \param capacity - new capacity of queue
   \param high_water_mark - high water mark for the queue

   \return CW_SUCCESS on success, CW_FAILURE otherwise
*/
int cw_tone_queue_set_capacity_internal(cw_tone_queue_t *tq, uint32_t capacity, uint32_t high_water_mark)
{
	assert (tq);
	if (!tq) {
		return CW_FAILURE;
	}

	if (!high_water_mark || high_water_mark > CW_TONE_QUEUE_HIGH_WATER_MARK_MAX) {
		/* If we allowed high water mark to be zero, the queue
		   would not accept any new tones: it would constantly
		   be full. */
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (!capacity || capacity > CW_TONE_QUEUE_CAPACITY_MAX) {
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

   testedin::test_cw_tone_queue_get_capacity_internal()

   \param tq - tone queue, for which you want to get capacity

   \return capacity of tone queue
*/
uint32_t cw_tone_queue_get_capacity_internal(cw_tone_queue_t *tq)
{
	assert (tq);
	return tq->capacity;
}





/**
   \brief Return high water mark of a queue

   \param tq - tone queue, for which you want to get high water mark

   \return high water mark of tone queue
*/
uint32_t cw_tone_queue_get_high_water_mark_internal(cw_tone_queue_t *tq)
{
	assert (tq);

	return tq->high_water_mark;
}





/**
   \brief Return number of items on tone queue

   testedin::test_cw_tone_queue_length_internal()

   \param tq - tone queue

   \return the count of tones currently held in the circular tone buffer.
*/
uint32_t cw_tone_queue_length_internal(cw_tone_queue_t *tq)
{
	// pthread_mutex_lock(&tq->mutex);
	// uint32_t len = CW_TONE_QUEUE_LENGTH(tq);
	// pthread_mutex_unlock(&tq->mutex);

	return tq->len;
}





/**
   \brief Get previous index to queue

   Calculate index of previous element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   testedin::test_cw_tone_queue_prev_index_internal()

   \param tq - tone queue for which to calculate index
   \param ind - index in relation to which to calculate index of previous element in queue

   \return index of previous element in queue
*/
uint32_t cw_tone_queue_prev_index_internal(cw_tone_queue_t *tq, uint32_t ind)
{
	return ind == 0 ? tq->capacity - 1 : ind - 1;
}





/**
   \brief Get next index to queue

   Calculate index of next element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   testedin::test_cw_tone_queue_next_index_internal()

   \param tq - tone queue for which to calculate index
   \param ind - index in relation to which to calculate index of next element in queue

   \return index of next element in queue
*/
uint32_t cw_tone_queue_next_index_internal(cw_tone_queue_t *tq, uint32_t ind)
{
	return ind == tq->capacity - 1 ? 0 : ind + 1;
}





/**
   \brief Dequeue a tone from tone queue

   Dequeue a tone from tone queue.

   The queue returns two distinct values when it is empty, and one value
   when it is not empty:
   \li CW_TQ_JUST_EMPTIED - when there were no new tones in the queue, but
       the queue still remembered its "BUSY" state; this return value
       is a way of telling client code "I've had tones, but no more, you
       should probably stop playing any sounds and become silent";
   \li CW_TQ_STILL_EMPTY - when there were no new tones in the queue, and
       the queue can't recall if it was "BUSY" before; this return value
       is a way of telling client code "I don't have any tones, you should
       probably stay silent";
   \li CW_TQ_NONEMPTY - when there was at least one tone in the queue;
       client code can call the function again, and the function will
       then return CW_TQ_NONEMPTY (if there is yet another tone), or
       CW_TQ_JUST_EMPTIED (if the tone from previous call was the last one);

   Information about successfully dequeued tone is returned through
   function's argument \p tone.
   The function does not modify the arguments if there are no tones to
   dequeue.

   If the last tone in queue has duration "CW_AUDIO_FOREVER_USECS", the function
   won't permanently dequeue it (won't "destroy" it). Instead, it will keep
   returning (through \p tone) the tone on every call, until a new tone is
   added to the queue after the "CW_AUDIO_FOREVER_USECS" tone.

   testedin::test_cw_tone_queue_dequeue_internal()
   testedin::test_cw_tone_queue_test_capacity2()

   \param tq - tone queue
   \param tone - dequeued tone

   \return CW_TQ_JUST_EMPTIED (see information above)
   \return CW_TQ_STILL_EMPTY (see information above)
   \return CW_TQ_NONEMPTY (see information above)
*/
int cw_tone_queue_dequeue_internal(cw_tone_queue_t *tq, /* out */ cw_tone_t *tone)
{
	pthread_mutex_lock(&tq->mutex);

#ifdef LIBCW_WITH_DEV
	static enum {
		REPORTED_STILL_EMPTY,
		REPORTED_JUST_EMPTIED,
		REPORTED_NONEMPTY
	} tq_report = REPORTED_STILL_EMPTY;
#endif


	/* Decide what to do based on the current state. */
	switch (tq->state) {

	case QS_IDLE:
#ifdef LIBCW_WITH_DEV
		if (tq_report != REPORTED_STILL_EMPTY) {
			/* tone queue is empty */
			cw_debug_ev ((&cw_debug_object_ev), 0, CW_DEBUG_EVENT_TQ_STILL_EMPTY);
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
				      "libcw: tone queue: still empty");
			tq_report = REPORTED_STILL_EMPTY;
		}
#endif
		pthread_mutex_unlock(&tq->mutex);
		/* Ignore calls if our state is idle. */
		return CW_TQ_STILL_EMPTY;

	case QS_BUSY:
		/* If there are some tones in queue, dequeue the next
		   tone. If there are no more tones, go to the idle state. */
		if (tq->len) {
			/* Get the current queue length.  Later on, we'll
			   compare with the length after we've scanned
			   over every tone we can omit, and use it to see
			   if we've crossed the low water mark, if any. */
			uint32_t queue_len = tq->len;

			/* Get parameters of tone to be played */
			tone->usecs = tq->queue[tq->head].usecs;
			tone->frequency = tq->queue[tq->head].frequency;
			tone->slope_mode = tq->queue[tq->head].slope_mode;

			if (tone->usecs == CW_AUDIO_FOREVER_USECS && queue_len == 1) {
				/* The last tone currently in queue is
				   CW_AUDIO_FOREVER_USECS, which means that we
				   should play certain tone until client
				   code adds next tone (possibly forever).

				   Don't dequeue this "forever" tone,
				   don't iterate head.

				   TODO: shouldn't we 'return
				   CW_TQ_NONEMPTY' at this point?
				   Since we are in "forever" tone,
				   what else should we do?  Maybe call
				   cw_key_set_state_internal(), but I
				   think that this would be all. */
			} else {
				tq->head = cw_tone_queue_next_index_internal(tq, tq->head);;
				tq->len--;

				if (!tq->len) {
					/* If tq has been emptied,
					   head and tail should be
					   back to initial state. */
					cw_assert (tq->head == tq->tail,
						   "Head: %"PRIu32", tail: %"PRIu32"",
						   tq->head, tq->tail);
				}
			}

			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
				      "libcw: tone queue: dequeue tone %d usec, %d Hz", tone->usecs, tone->frequency);
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
				      "libcw: tone queue: head = %"PRIu32", tail = %"PRIu32", length = %"PRIu32" -> %"PRIu32"",
				      tq->head, tq->tail, queue_len, tq->len);

			/* Notify the key control function that there might
			   have been a change of keying state (and then
			   again, there might not have been -- it will sort
			   this out for us). */
			cw_key_set_state_internal(tone->frequency ? CW_KEY_STATE_CLOSED : CW_KEY_STATE_OPEN);

#if 0
			/* If microseconds is zero, leave it at that.  This
			   way, a queued tone of 0 usec implies leaving the
			   sound in this state, and 0 usec and 0 frequency
			   leaves silence.  */ /* TODO: ??? */
			if (tone->usecs == 0) {
				/* Autonomous dequeuing has finished for
				   the moment. */
				tq->state = QS_IDLE;
				cw_finalization_schedule_internal();
			}
#endif


#ifdef LIBCW_WITH_DEV
			if (tq_report != REPORTED_NONEMPTY) {
				cw_debug_ev ((&cw_debug_object_ev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_EVENT_TQ_NONEMPTY);
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
					      "libcw: tone queue: nonempty: usecs = %d, freq = %d, slope = %d", tone->usecs, tone->frequency, tone->slope_mode);
				tq_report = REPORTED_NONEMPTY;
			}
#endif

			/* If there is a low water mark callback registered,
			   and if we passed under the water mark, call the
			   callback here.  We want to be sure to call this
			   late in the processing, especially after setting
			   the state to idle, since the most likely action
			   of this routine is to queue tones, and we don't
			   want to play with the state here after that. */
			bool call_callback = false;
			if (tq->low_water_callback) {

				/* If the length we originally calculated
				   was above the low water mark, and the
				   one we have now is below or equal to it,
				   call the callback. */

				/* It may seem that the double condition in
				   'if ()' is redundant, but for some reason
				   it is necessary. Be very, very careful
				   when modifying this. */
				if (queue_len > tq->low_water_mark
				    && tq->len <= tq->low_water_mark

				    /* Avoid endlessly calling the callback
				       if the only queued tone is 'forever'
				       tone. Once client code decides to end the
				       'forever' tone, we will be ready to
				       call the callback again. */
				    && !(tone->usecs == CW_AUDIO_FOREVER_USECS && queue_len == 1)) {

					// fprintf(stderr, "libcw: solution 7, calling callback A, %d / %d / %d\n", tq->len, queue_len, tq->low_water_mark);
					call_callback = true;
				}
			}

			pthread_mutex_unlock(&tq->mutex);

			/* Since client's callback can use functions
			   that call pthread_mutex_lock(), we should
			   put the callback *after* we release
			   pthread_mutex_unlock() in this function. */

			if (call_callback) {
				// fprintf(stderr, "libcw: solution 7, calling callback B, %d / %d / %d\n", tq->len, queue_len, tq->low_water_mark);
				(*(tq->low_water_callback))(tq->low_water_callback_arg);
			}

			return CW_TQ_NONEMPTY;
		} else { /* tq->len == 0 */
			/* State of tone queue (as indicated by
			   tq->state) is "busy", but it turns out that
			   there are no tones left on the queue to
			   play (tq->len == 0).

			   Time to bring tq->state in sync with
			   len. Set state to idle, indicating that
			   autonomous dequeuing has finished for the
			   moment. */
			tq->state = QS_IDLE;

			/* There is no tone to dequeue, so don't modify
			   function's arguments. Client code will learn
			   about "no tones" state through return value. */

			/* Notify the keying control function about the silence. */
			cw_key_set_state_internal(CW_KEY_STATE_OPEN);

			//cw_finalization_schedule_internal();

#ifdef LIBCW_WITH_DEV
			if (tq_report != REPORTED_JUST_EMPTIED) {
				cw_debug_ev ((&cw_debug_object_ev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_EVENT_TQ_JUST_EMPTIED);
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
					      "libcw: tone queue: just emptied");
				tq_report = REPORTED_JUST_EMPTIED;
			}
#endif

			pthread_mutex_unlock(&tq->mutex);
			return CW_TQ_JUST_EMPTIED;
		}
	}

	pthread_mutex_unlock(&tq->mutex);
	/* will never get here as "queue state" enum has only two values */
	assert(0);
	return CW_TQ_STILL_EMPTY;
}





/**
   \brief Add tone to tone queue

   Enqueue a tone for specified frequency and number of microseconds.
   This routine adds the new tone to the queue, and if necessary
   starts the itimer process to have the tone sent (TODO: check if the
   information about itimer is still true).

   The routine returns CW_SUCCESS on success. If the tone queue is
   full, the routine returns CW_FAILURE, with errno set to EAGAIN.  If
   the iambic keyer or straight key are currently busy, the routine
   returns CW_FAILURE, with errno set to EBUSY.

   If length of a tone (tone->usecs) is zero, the function does not
   add it to tone queue and returns CW_SUCCESS.

   The function does accept tones with negative values of usecs,
   representing special tones.

   testedin::test_cw_tone_queue_enqueue_internal()
   testedin::test_cw_tone_queue_test_capacity1()
   testedin::test_cw_tone_queue_test_capacity2()

   \param tq - tone queue
   \param tone - tone to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tone_queue_enqueue_internal(cw_tone_queue_t *tq, cw_tone_t *tone)
{
	if (!tone->usecs) {
		/* Drop empty tone. It won't be played anyway, and for
		   now there are no other good reasons to enqueue
		   it. While it may happen in higher-level code to
		   create such tone, but there is no need to spend
		   time on it here. */
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_INFO,
			      "libcw: tone queue: dropped tone with usecs == 0");
		return CW_SUCCESS;
	}

	pthread_mutex_lock(&tq->mutex);
	/* If the keyer or straight key are busy, return an error.
	   This is because they use the sound card/console tones and key
	   control, and will interfere with us if we try to use them at
	   the same time. */
	// if (cw_is_keyer_busy() || cw_is_straight_key_busy()) {
	if (0) {
		errno = EBUSY;
		pthread_mutex_unlock(&tq->mutex);
		return CW_FAILURE;
	}

	// fprintf(stderr, "Attempting to enqueue tone #%"PRIu32"\n", tq->len + 1);

	if (tq->len == tq->capacity) {
		/* Tone queue is full. */

		errno = EAGAIN;
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_ERROR,
			      "libcw: tone queue: can't enqueue tone, tq is full");
		pthread_mutex_unlock(&tq->mutex);
		return CW_FAILURE;
	}


	cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      "libcw: tone queue: enqueue tone %d usec, %d Hz", tone->usecs, tone->frequency);

	/* Enqueue the new tone.

	   Notice that tail is incremented after adding a tone. This
	   means that for empty tq new tone is inserted at index
	   tail == head (which should be kind of obvious). */
	tq->queue[tq->tail].usecs = tone->usecs;
	tq->queue[tq->tail].frequency = tone->frequency;
	tq->queue[tq->tail].slope_mode = tone->slope_mode;

	tq->tail = cw_tone_queue_next_index_internal(tq, tq->tail);
	tq->len++;

	// fprintf(stderr, "enqueued %"PRIu32" tones\n", tq->len);

	/* If there is currently no autonomous (asynchronous)
	   dequeueing happening, kick off the itimer process.

	   (TODO: check if the part of the comment about itimer is
	   still valid.) */
	if (tq->state == QS_IDLE) {
		tq->state = QS_BUSY;
		/* A loop in write() function may await for the queue
		   to be filled with new tones to dequeue and play.
		   It waits for a signal. This is a right place and time
		   to send such a signal. */
		pthread_kill((*cw_generator)->thread.id, SIGALRM);
	}

	pthread_mutex_unlock(&tq->mutex);
	return CW_SUCCESS;
}





/**
   \brief Register callback for low queue state

   Register a function to be called automatically by the dequeue routine
   whenever the tone queue falls to a given \p level. To be more precise:
   the callback is called by queue manager if, after dequeueing a tone,
   the manager notices that tone queue length has become equal or less
   than \p level.

   \p callback_arg may be used to give a value passed back on callback
   calls.  A NULL function pointer suppresses callbacks.  On success,
   the routine returns CW_SUCCESS.

   If \p level is invalid, the routine returns CW_FAILURE with errno set to
   EINVAL.  Any callback supplied will be called in signal handler context.

   testedin::test_tone_queue_callback()

   \param callback_func - callback function to be registered
   \param callback_arg - argument for callback_func to pass return value
   \param level - low level of queue triggering callback call

   \return CW_SUCCESS on successful registration
   \return CW_FAILURE on failure
*/
int cw_register_tone_queue_low_callback(void (*callback_func)(void*), void *callback_arg, int level)
{
	if (level < 0 || (uint32_t) level >= cw_tone_queue.capacity) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Store the function and low water mark level. */
	cw_tone_queue.low_water_mark = (uint32_t) level;
	cw_tone_queue.low_water_callback = callback_func;
	cw_tone_queue.low_water_callback_arg = callback_arg;

	return CW_SUCCESS;
}





/**
   \brief Check if tone sender is busy

   Indicate if the tone sender is busy.

   \return true if there are still entries in the tone queue
   \return false if the queue is empty
*/
bool cw_is_tone_busy(void)
{
	return cw_tone_queue.state != QS_IDLE;
}





/**
   \brief Wait for the current tone to complete

   The routine returns CW_SUCCESS on success.  If called with SIGALRM
   blocked, the routine returns CW_FAILURE, with errno set to EDEADLK,
   to avoid indefinite waits.

   testedin::test_tone_queue_1()
   testedin::test_tone_queue_2()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_tone(void)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait for the tail index to change or the dequeue to go idle. */
	/* FIXME: the comment is about tail, but below we wait for head. */
	uint32_t check_tq_head = cw_tone_queue.head;
	while (cw_tone_queue.head == check_tq_head && cw_tone_queue.state != QS_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the tone queue to drain

   The routine returns CW_SUCCESS on success. If called with SIGALRM
   blocked, the routine returns false, with errno set to EDEADLK,
   to avoid indefinite waits.

   testedin::test_tone_queue_1()
   testedin::test_tone_queue_2()
   testedin::test_tone_queue_3()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_tone_queue(void)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait until the dequeue indicates it has hit the end of the queue. */
	while (cw_tone_queue.state != QS_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the tone queue to drain until only as many tones as given in level remain queued

   This routine is for use by programs that want to optimize themselves
   to avoid the cleanup that happens when the tone queue drains completely;
   such programs have a short time in which to add more tones to the queue.

   The routine returns CW_SUCCESS on success.  If called with SIGALRM
   blocked, the routine returns false, with errno set to EDEADLK, to
   avoid indefinite waits.

   \param level - low level in queue, at which to return

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_tone_queue_critical(int level)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait until the queue length is at or below criticality. */
	while (cw_tone_queue_length_internal(&cw_tone_queue) > (uint32_t) level) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Indicate if the tone queue is full

   testedin::test_cw_tone_queue_is_full_internal()

   \return true if tone queue is full
   \return false if tone queue is not full
*/
bool cw_is_tone_queue_full(void)
{
	/* TODO: should we pass 'cw_tone_queue' or 'generator->tq'? */
	return cw_tone_queue_is_full_internal(&cw_tone_queue);
}





/**
   \brief Indicate if the tone queue is full

   This is a helper subroutine created so that I can pass a test tone
   queue in unit tests. The 'cw_is_tone_queue_full() works only on
   default tone queue object.

   testedin::test_cw_tone_queue_is_full_internal()

   \param tq - tone queue to check

   \return true if tone queue is full
   \return false if tone queue is not full
*/
bool cw_tone_queue_is_full_internal(cw_tone_queue_t *tq)
{
	return tq->len == tq->capacity;
}





/**
   \brief Return the number of entries the tone queue can accommodate

   testedin::test_tone_queue_3()
   testedin::test_cw_tone_queue_get_capacity_internal()
*/
int cw_get_tone_queue_capacity(void)
{
	return (int) cw_tone_queue_get_capacity_internal(&cw_tone_queue);
}





/**
   \brief Return the number of entries currently pending in the tone queue

   testedin::test_cw_tone_queue_length_internal()
   testedin::test_tone_queue_1()
   testedin::test_tone_queue_3()
*/
int cw_get_tone_queue_length(void)
{
	/* TODO: change return type to uint32_t. */
	return (int) cw_tone_queue_length_internal(&cw_tone_queue);
}





/**
   \brief Cancel all pending queued tones, and return to silence.

   If there is a tone in progress, the function will wait until this
   last one has completed, then silence the tones.

   This function may be called with SIGALRM blocked, in which case it
   will empty the queue as best it can, then return without waiting for
   the final tone to complete.  In this case, it may not be possible to
   guarantee silence after the call.
*/
void cw_flush_tone_queue(void)
{
	pthread_mutex_lock(&(*cw_generator)->tq->mutex);

	/* Empty and reset the queue. */
	(*cw_generator)->tq->len = 0;
	(*cw_generator)->tq->head = (*cw_generator)->tq->tail;

	pthread_mutex_unlock(&(*cw_generator)->tq->mutex);

	/* If we can, wait until the dequeue goes idle. */
	if (!cw_sigalrm_is_blocked_internal()) {
		cw_wait_for_tone_queue();
	}

	/* Force silence on the speaker anyway, and stop any background
	   soundcard tone generation. */
	cw_generator_silence_internal((*cw_generator));
	//cw_finalization_schedule_internal();

	return;
}





/**
   \brief Primitive access to simple tone generation

   This routine queues a tone of given duration and frequency.
   The routine returns CW_SUCCESS on success.  If usec or frequency
   are invalid, it returns CW_FAILURE with errno set to EINVAL.
   If the sound card, console speaker, or keying function are busy,
   it returns CW_FAILURE  with errno set to EBUSY.  If the tone queue
   is full, it returns false with errno set to EAGAIN.

   testedin::test_tone_queue_0()
   testedin::test_tone_queue_1()
   testedin::test_tone_queue_2()
   testedin::test_tone_queue_3()

   \param usecs - duration of queued tone, in microseconds
   \param frequency - frequency of queued tone

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_queue_tone(int usecs, int frequency)
{
	/* Check the arguments given for realistic values.  Note that we
	   do nothing here to protect the caller from setting up
	   neverending (0 usecs) tones, if that's what they want to do. */
	if (usecs < 0
	    || frequency < 0 /* TODO: Is this line necessary? We have CW_FREQUENCY_MIN/MAX below. */
	    || frequency < CW_FREQUENCY_MIN
	    || frequency > CW_FREQUENCY_MAX) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES;
	tone.usecs = usecs;
	tone.frequency = frequency;
	return cw_tone_queue_enqueue_internal(&cw_tone_queue, &tone);
}





/**
   Cancel all pending queued tones, reset any queue low callback registered,
   and return to silence.  This function is suitable for calling from an
   application exit handler.
*/
void cw_reset_tone_queue(void)
{
	/* Empty and reset the queue, and force state to idle. */
	cw_tone_queue.len = 0;
	cw_tone_queue.head = cw_tone_queue.tail;
	cw_tone_queue.state = QS_IDLE;

	/* Reset low water mark details to their initial values. */
	cw_tone_queue.low_water_mark = 0;
	cw_tone_queue.low_water_callback = NULL;
	cw_tone_queue.low_water_callback_arg = NULL;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_generator_silence_internal((*cw_generator));
	//cw_finalization_schedule_internal();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_TONE_QUEUE, CW_DEBUG_INFO,
		      "libcw: tone queue: reset");

	return;
}





/* *** Unit tests *** */




#ifdef LIBCW_UNIT_TESTS


#include "libcw_test.h"


static cw_tone_queue_t test_tone_queue;




/**
   tests::cw_tone_queue_init_internal()
*/
unsigned int test_cw_tone_queue_init_internal(void)
{
	int p = fprintf(stderr, "libwc: cw_tone_queue_init_internal():");
	int rv = cw_tone_queue_init_internal(&test_tone_queue);
	assert (rv == CW_SUCCESS);

	/* This is preparation for other tests that will be performed
	   on the global test tq. */
	test_tone_queue.state = QS_BUSY;

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_tone_queue_get_capacity_internal()
   tests::cw_get_tone_queue_capacity()
*/
unsigned int test_cw_tone_queue_get_capacity_internal(void)
{
	int p = fprintf(stderr, "libcw: cw_tone_queue_get_capacity_internal():");

	uint32_t n = cw_tone_queue_get_capacity_internal(&test_tone_queue);
	assert (n == test_tone_queue.capacity);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_tone_queue_prev_index_internal()
*/
unsigned int test_cw_tone_queue_prev_index_internal(void)
{
	int p = fprintf(stderr, "libcw: cw_tone_queue_prev_index_internal():");

	struct {
		int arg;
		uint32_t expected;
	} input[] = {
		{ test_tone_queue.capacity - 4, test_tone_queue.capacity - 5 },
		{ test_tone_queue.capacity - 3, test_tone_queue.capacity - 4 },
		{ test_tone_queue.capacity - 2, test_tone_queue.capacity - 3 },
		{ test_tone_queue.capacity - 1, test_tone_queue.capacity - 2 },

		/* This one should never happen. We can't pass index
		   equal "capacity" because it's out of range. */
		/*
		{ test_tone_queue.capacity - 0, test_tone_queue.capacity - 1 },
		*/

		{                            0, test_tone_queue.capacity - 1 },
		{                            1,                            0 },
		{                            2,                            1 },
		{                            3,                            2 },
		{                            4,                            3 },

		{ -1000, -1000 } /* guard */
	};

	int i = 0;
	while (input[i].arg != -1000) {
		uint32_t prev = cw_tone_queue_prev_index_internal(&test_tone_queue, input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, (int) prev, input[i].expected);
		assert (prev == input[i].expected);
		i++;
	}

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_tone_queue_next_index_internal()
*/
unsigned int test_cw_tone_queue_next_index_internal(void)
{
	int p = fprintf(stderr, "libcw: cw_tone_queue_next_index_internal():");

	struct {
		int arg;
		uint32_t expected;
	} input[] = {
		{ test_tone_queue.capacity - 5, test_tone_queue.capacity - 4 },
		{ test_tone_queue.capacity - 4, test_tone_queue.capacity - 3 },
		{ test_tone_queue.capacity - 3, test_tone_queue.capacity - 2 },
		{ test_tone_queue.capacity - 2, test_tone_queue.capacity - 1 },
		{ test_tone_queue.capacity - 1,                            0 },
		{                            0,                            1 },
		{                            1,                            2 },
		{                            2,                            3 },
		{                            3,                            4 },

		{ -1000, -1000 } /* guard */
	};

	int i = 0;
	while (input[i].arg != -1000) {
		uint32_t next = cw_tone_queue_next_index_internal(&test_tone_queue, input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, (int) next, input[i].expected);
		assert (next == input[i].expected);
		i++;
	}

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   tests::cw_tone_queue_length_internal()
   tests::cw_get_tone_queue_length()
*/
unsigned int test_cw_tone_queue_length_internal(void)
{
	int p = fprintf(stderr, "libcw: cw_tone_queue_length_internal():");

	/* This is just some code copied from implementation of
	   'enqueue' function. I don't use 'enqueue' function itself
	   because it's not tested yet. I get rid of all the other
	   code from the 'enqueue' function and use only the essential
	   part to manually add elements to list, and then to check
	   length of the list. */

	cw_tone_t tone;
	tone.usecs = 1;
	tone.frequency = 1;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;

	for (uint32_t i = 0; i < test_tone_queue.capacity; i++) {

		{
			/* This part of code pretends to be enqueue
			   function.  The most important functionality
			   of enqueue function is done here
			   manually. We don't do any checks of
			   boundaries of tq, we trust that this is
			   enforced by for loop's conditions. */

			if (test_tone_queue.len == test_tone_queue.capacity) {
				/* This shouldn't happen because queue
				   boundary is watched by loop
				   condition. */
				assert (0);
			}

			/* Enqueue the new tone and set the new tail index. */
			test_tone_queue.queue[test_tone_queue.tail].usecs = tone.usecs;
			test_tone_queue.queue[test_tone_queue.tail].frequency = tone.frequency;
			test_tone_queue.queue[test_tone_queue.tail].slope_mode = tone.slope_mode;

			test_tone_queue.tail = cw_tone_queue_next_index_internal(&test_tone_queue, test_tone_queue.tail);
			test_tone_queue.len++;
		}

		/* OK, added a tone, ready to measure length of the queue. */
		uint32_t len = cw_tone_queue_length_internal(&test_tone_queue);
		assert (len == i + 1);

		cw_assert(len == test_tone_queue.len,
			  "Lengths don't match: %"PRIu32" != %"PRIu32"",
			  len, test_tone_queue.len);
	}

	/* Empty and reset the queue. */
	test_tone_queue.len = 0;
	test_tone_queue.head = test_tone_queue.tail;

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_tone_queue_enqueue_internal()
*/
unsigned int test_cw_tone_queue_enqueue_internal(void)
{
	int p = fprintf(stderr, "libcw: cw_tone_queue_enqueue_internal():");

	/* At this point cw_tone_queue_length_internal() should be
	   tested, so we can use it to verify correctness of 'enqueue'
	   function. */

	cw_tone_t tone;
	tone.usecs = 1;
	tone.frequency = 1;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;

	for (uint32_t i = 0; i < test_tone_queue.capacity; i++) {

		/* This tests for potential problems with function call. */
		int rv = cw_tone_queue_enqueue_internal(&test_tone_queue, &tone);
		assert (rv);

		/* This tests for correctness of working of the 'enqueue' function. */
		uint32_t len = cw_tone_queue_length_internal(&test_tone_queue);
		assert (len == i + 1);
	}


	/* Try adding a tone to full tq. */
	/* This tests for potential problems with function call.
	   Enqueueing should fail when the queue is full. */
	int rv = cw_tone_queue_enqueue_internal(&test_tone_queue, &tone);
	assert (rv == CW_FAILURE);

	/* This tests for correctness of working of the 'enqueue'
	   function.  Full tq should not grow beyond its capacity. */
	assert (test_tone_queue.len == test_tone_queue.capacity);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_tone_queue_dequeue_internal()
*/
unsigned int test_cw_tone_queue_dequeue_internal(void)
{
	int p = fprintf(stderr, "libcw: cw_tone_queue_dequeue_internal():");

	/* At this point cw_tone_queue_length_internal() should be
	   tested, so we can use it to verify correctness of 'deenqueue'
	   function.

	   test_tone_queue should be completely filled after tests of
	   'enqueue' function. */

	/* Test some assertions about full tq, just to be sure. */
	cw_assert (test_tone_queue.capacity == test_tone_queue.len,
		   "Capacity != Len of full queue: %"PRIu32" != %"PRIu32"",
		   test_tone_queue.capacity, test_tone_queue.len)

	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;;

	for (uint32_t i = test_tone_queue.capacity; i > 0; i--) {

		/* Length of tone queue before dequeue. */
		cw_assert (i == test_tone_queue.len,
			   "Iteration before dequeue doesn't match len: %"PRIu32" != %"PRIu32"",
			   i, test_tone_queue.len);

		/* This tests for potential problems with function call. */
		int rv = cw_tone_queue_dequeue_internal(&test_tone_queue, &tone);
		assert (rv == CW_TQ_NONEMPTY);

		/* Length of tone queue after dequeue. */
		cw_assert (i - 1 == test_tone_queue.len,
			   "Iteration after dequeue doesn't match len: %"PRIu32" != %"PRIu32"",
			   i - 1, test_tone_queue.len);
	}

	/* Try removing a tone from empty queue. */
	/* This tests for potential problems with function call. */
	int rv = cw_tone_queue_dequeue_internal(&test_tone_queue, &tone);
	assert (rv == CW_TQ_JUST_EMPTIED);

	/* This tests for correctness of working of the 'dequeue'
	   function.  Empty tq should stay empty. */
	uint32_t len = cw_tone_queue_length_internal(&test_tone_queue);
	assert (len == 0);
	cw_assert (test_tone_queue.len == 0,
		   "Length of empty queue is != 0 (%"PRIu32")",
		   test_tone_queue.len);

	/* Try removing a tone from empty queue. */
	/* This time we should get CW_TQ_STILL_EMPTY return value. */
	rv = cw_tone_queue_dequeue_internal(&test_tone_queue, &tone);
	assert (rv == CW_TQ_STILL_EMPTY);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   tests::cw_tone_queue_is_full_internal()
   tests::cw_is_tone_queue_full()
*/
unsigned int test_cw_tone_queue_is_full_internal(void)
{
	int p = fprintf(stderr, "libcw: cw_tone_queue_is_full_internal():");

	/* The tq should be empty after the last test, but just in case...
	   Empty and reset the queue. */
	test_tone_queue.len = 0;
	test_tone_queue.head = test_tone_queue.tail;

	test_tone_queue.state = QS_BUSY;

	cw_tone_t tone;
	tone.usecs = 1;
	tone.frequency = 1;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;;

	/* Notice the "capacity - 1" in loop condition: we leave one
	   place in tq free so that is_full() called in the loop
	   always returns false. */
	for (uint32_t i = 0; i < test_tone_queue.capacity - 1; i++) {
		int rv = cw_tone_queue_enqueue_internal(&test_tone_queue, &tone);
		/* The 'enqueue' function has been already tested, but
		   it won't hurt to check this simple assertion here
		   as well. */
		assert (rv == CW_SUCCESS);

		/* Here is the proper test of tested function. */
		assert (!cw_tone_queue_is_full_internal(&test_tone_queue));
	}

	/* At this point there is still place in tq for one more
	   tone. Enqueue it and verify that the tq is now full. */
	int rv = cw_tone_queue_enqueue_internal(&test_tone_queue, &tone);
	assert (rv == CW_SUCCESS);

	assert (cw_tone_queue_is_full_internal(&test_tone_queue));

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   \brief Test "capacity" property of tone queue

   Function tests "capacity" property of tone queue, and also tests
   related properties: head and tail.

   In this function it is done by first enqueueing N known tones to a
   tone queue using cw_tone_queue_enqueue_internal(), and then
   "manually" checking content of tone queue to be sure that all the
   tones are in place.

   tests::cw_tone_queue_enqueue_internal()
*/
unsigned int test_cw_tone_queue_test_capacity1(void)
{
	int p = fprintf(stderr, "libcw: testing correctness of handling capacity (1):");

	/* We don't need to check tq with capacity ==
	   CW_TONE_QUEUE_CAPACITY_MAX (yet). Let's test a smaller
	   queue. 30 tones will be enough (for now), and 30-4 is a
	   good value for high water mark. */
	uint32_t capacity = 30;
	uint32_t watermark = capacity - 4;

	/* We will do tests of queue with constant capacity, but with
	   different initial position at which we insert first element
	   (tone), i.e. different position of queue's head.

	   Put the guard after "capacity - 1". */
	int head_shifts[] = { 0, 5, 10, 29, -1, 30, -1 };
	int s = 0;

	while (head_shifts[s] != -1) {

		// fprintf(stderr, "\nTesting with head shift = %d\n", head_shifts[s]);

		/* For every new test with new head shift we need a
		   "clean" queue. */
		int rv = test_cw_tone_queue_capacity_test_init(&test_tone_queue, capacity, watermark, head_shifts[s]);

		/* Fill all positions in queue with tones of known
		   frequency.  If shift_head != 0, the enqueue
		   function should make sure that the enqueued tones
		   are nicely wrapped after end of queue. */
		for (uint32_t i = 0; i < test_tone_queue.capacity; i++) {
			cw_tone_t tone;
			tone.frequency = (int) i;
			tone.usecs = 1000;
			rv = cw_tone_queue_enqueue_internal(&test_tone_queue, &tone);
			assert (rv == CW_SUCCESS);
		}

		/* With the queue filled with valid and known data,
		   it's time to read back the data and verify that the
		   tones were placed in correct positions, as
		   expected. Let's do the readback N times, just for
		   fun. Every time the results should be the same. */

		for (int l = 0; l < 3; l++) {
			for (uint32_t i = 0; i < test_tone_queue.capacity; i++) {

				uint32_t shifted = (i + head_shifts[s]) % (test_tone_queue.capacity);
				// fprintf(stderr, "Readback %d: position %"PRIu32": checking tone %"PRIu32", expected %"PRIu32", got %d\n",
				// 	l, shifted, i, i, test_tone_queue.queue[shifted].frequency);
				assert (test_tone_queue.queue[shifted].frequency == (int) i);
			}
		}

		s++;
	}

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   \brief Test "capacity" property of tone queue

   Function tests "capacity" property of tone queue, and also tests
   related properties: head and tail.

   In this function it is done by first enqueueing N known tones to a
   tone queue using cw_tone_queue_enqueue_internal(), then dequeueing
   the tones with cw_tone_queue_dequeue_internal() and then checking
   that enqueued tones are the ones that we were expecting to get.

   tests::cw_tone_queue_enqueue_internal()

   tests::cw_tone_queue_dequeue_internal()
*/
unsigned int test_cw_tone_queue_test_capacity2(void)
{
	int p = fprintf(stderr, "libcw: testing correctness of handling capacity (2):");

	/* We don't need to check tq with capacity ==
	   CW_TONE_QUEUE_CAPACITY_MAX (yet). Let's test a smaller
	   queue. 30 tones will be enough (for now), and 30-4 is a
	   good value for high water mark. */
	uint32_t capacity = 30;
	uint32_t watermark = capacity - 4;

	/* We will do tests of queue with constant capacity, but with
	   different initial position at which we insert first element
	   (tone), i.e. different position of queue's head.

	   Put the guard after "capacity - 1". */
	int head_shifts[] = { 0, 5, 10, 29, -1, 30, -1 };
	int s = 0;

	while (head_shifts[s] != -1) {

		// fprintf(stderr, "\nTesting with head shift = %d\n", head_shifts[s]);

		/* For every new test with new head shift we need a
		   "clean" queue. */
		int rv = test_cw_tone_queue_capacity_test_init(&test_tone_queue, capacity, watermark, head_shifts[s]);

		/* Fill all positions in queue with tones of known
		   frequency.  If shift_head != 0, the enqueue
		   function should make sure that the enqueued tones
		   are nicely wrapped after end of queue. */
		for (uint32_t i = 0; i < test_tone_queue.capacity; i++) {
			cw_tone_t tone;
			tone.frequency = (int) i;
			tone.usecs = 1000;
			rv = cw_tone_queue_enqueue_internal(&test_tone_queue, &tone);
			assert (rv == CW_SUCCESS);
			//fprintf(stderr, "Tone %d enqueued\n", i);
		}



		/* With the queue filled with valid and known data
		   (tones), it's time to read back the data and verify
		   that the tones were placed in correct positions, as
		   expected.

		   In test_cw_tone_queue_test_capacity1() we did the
		   readback "manually", this time let's use "dequeue"
		   function to do the job.

		   Since the "dequeue" function moves queue pointers,
		   we can do this test only once (we can't repeat the
		   readback N times with calls to dequeue() expecting
		   the same results). */

		uint32_t i = 0;
		cw_tone_t tone;

		while ((rv = cw_tone_queue_dequeue_internal(&test_tone_queue, &tone))
		       && rv == CW_TQ_NONEMPTY) {

			uint32_t shifted = (i + head_shifts[s]) % (test_tone_queue.capacity);

			cw_assert (test_tone_queue.queue[shifted].frequency == (int) i,
				   "Position %"PRIu32": checking tone %"PRIu32", expected %"PRIu32", got %d\n",
				   shifted, i, i, test_tone_queue.queue[shifted].frequency);

			i++;
		}

		cw_assert (i == test_tone_queue.capacity,
			   "Number of dequeues (%"PRIu32") is different than capacity (%"PRIu32")\n",
			   i, test_tone_queue.capacity);

		s++;
	}

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   \brief Initialize tone queue for tests of capacity

   Initialize given queue for tests of capacity of tone queue, and of
   related parameters of the tq: head and tail.

   First three function parameters are rather boring. What is
   interesting is the fourth parameter: \p head_shift.

   In general the behaviour of tone queue (a circular list) should be
   independent of initial position of queue's head (i.e. from which
   position in the queue we start adding new elements to the queue).

   By initializing the queue with different initial positions of head
   pointer, we can test this assertion about irrelevance of initial
   head position.

   tests::cw_tone_queue_set_capacity_internal()

   \param tq - tone queue to test
   \param capacity - intended capacity of tone queue
   \param high_water_mark - high water mark to be set for tone queue
   \param head_shift - position of first element that will be inserted in empty queue
*/
int test_cw_tone_queue_capacity_test_init(cw_tone_queue_t *tq, uint32_t capacity, uint32_t high_water_mark, int head_shift)
{
	/* Always reset the queue. If it was used previously in
	   different tests, the tests may have modified internals of
	   the queue so that some assumptions that I make about a tq
	   may be no longer valid for 'used' tq. */
	int rv = cw_tone_queue_init_internal(tq);
	assert (rv == CW_SUCCESS);

	rv = cw_tone_queue_set_capacity_internal(tq, capacity, high_water_mark);
	assert (rv == CW_SUCCESS);
	assert (tq->capacity == capacity);
	assert (tq->high_water_mark == high_water_mark);

	/* Initialize *all* tones with known value. Do this manually,
	   to be 100% sure that all tones in queue table have been
	   initialized. */
	for (int i = 0; i < CW_TONE_QUEUE_CAPACITY_MAX; i++) {
		cw_tone_t tone = { .usecs = 1,
				   .frequency = 10000 + i,
				   .slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES };

		tq->queue[i].usecs = tone.usecs;
		tq->queue[i].frequency = tone.frequency;
		tq->queue[i].slope_mode = tone.slope_mode;
	}

	/* Move head and tail of empty queue to initial position. The
	   queue is empty - the initialization of fields done above is not
	   considered as real enqueueing of valid tones. */
	tq->tail = head_shift;
	tq->head = tq->tail;
	tq->len = 0;

	/* TODO: why do this here? */
	tq->state = QS_BUSY;

	return CW_SUCCESS;
}


#endif /* #ifdef LIBCW_UNIT_TESTS */
