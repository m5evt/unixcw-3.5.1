/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */




#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>




#include "test_framework.h"

#include "libcw_tq.h"
#include "libcw_tq_internal.h"
#include "libcw_tq_tests.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/tq: "




static int test_cw_tq_enqueue_internal_A(cw_test_executor_t * cte, cw_tone_queue_t * tq);
static int test_cw_tq_dequeue_internal(cw_test_executor_t * cte, cw_tone_queue_t * tq);


static void gen_setup(cw_test_executor_t * cte, cw_gen_t ** gen);
static void gen_destroy(cw_gen_t ** gen);
static void enqueue_tone_low_level(cw_test_executor_t * cte, cw_tone_queue_t * tq, const cw_tone_t * tone);




static void gen_setup(cw_test_executor_t * cte, cw_gen_t ** gen)
{
	*gen = cw_gen_new(cte->current_sound_system, NULL);
	if (!*gen) {
		cte->log_error(cte, "Can't create generator, stopping the test\n");
		return;
	}

	cw_gen_reset_parameters_internal(*gen);
	cw_gen_sync_parameters_internal(*gen);
	cw_gen_set_speed(*gen, 30);

	return;
}




static void gen_destroy(cw_gen_t ** gen)
{
	cw_gen_delete(gen);
}




/**
   tests::cw_tq_new_internal()
   tests::cw_tq_delete_internal()

   @reviewed on 2019-10-03
*/
int test_cw_tq_new_delete_internal(cw_test_executor_t * cte)
{
	const int loop_max = (rand() % 40) + 20;

	cte->print_test_header(cte, "%s (%d)", __func__, loop_max);

	bool failure = false;
	cw_tone_queue_t * tq = NULL;

	for (int i = 0; i < loop_max; i++) {
		tq = cw_tq_new_internal();
		if (!cte->expect_valid_pointer_errors_only(cte, tq, "creating new tone queue")) {
			failure = true;
			break;
		}


		/*
		  Try to access some fields in cw_tone_queue_t just to
		  be sure that the tq has been allocated properly.

		  Trying to read and write tq->head and tq->tail may
		  seem silly, but I just want to dereference tq
		  pointer and be sure that nothing crashes.
		*/
		{
			if (!cte->expect_eq_int_errors_only(cte, 0, tq->head, "trying to dereference tq (read ::head)")) {
				failure = true;
				break;
			}

			tq->tail = tq->head + 10;
			if (!cte->expect_eq_int_errors_only(cte, 10, tq->tail, "trying to dereference tq (read ::tail)")) {
				failure = true;
				break;
			}
		}


		cw_tq_delete_internal(&tq);
		if (!cte->expect_null_pointer_errors_only(cte, tq, "deleting tone queue")) {
			failure = true;
			break;
		}
	}

	cte->expect_eq_int(cte, false, failure, "using tone queue's new/delete methods");

	/* Cleanup after (possibly) failed tests. */
	if (tq) {
		cw_tq_delete_internal(&tq);
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   tests::cw_tq_get_capacity_internal()

   @reviewed on 2019-10-03
*/
int test_cw_tq_get_capacity_internal(cw_test_executor_t * cte)
{
	const int loop_max = (rand() % 40) + 20;

	cte->print_test_header(cte, "%s (%d)", __func__, loop_max);

	bool failure = false;
	cw_tone_queue_t * tq = cw_tq_new_internal();
	cte->assert2(cte, tq, "failed to create new tone queue");

	for (int i = 0; i < loop_max; i++) {
		/* This is a silly test, but let's have any test of
		   the getter. */

		const size_t intended_capacity = (rand() % 4000) + 10;
		tq->capacity = intended_capacity;

		const size_t readback_capacity = cw_tq_get_capacity_internal(tq);
		if (!cte->expect_eq_int_errors_only(cte, intended_capacity, readback_capacity, "getting tone queue capacity")) {
			failure = true;
			break;
		}
	}

	cw_tq_delete_internal(&tq);

	cte->expect_eq_int(cte, false, failure, "getting tone queue capacity");

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   tests::cw_tq_prev_index_internal()

   @reviewed on 2019-10-03
*/
int test_cw_tq_prev_index_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_tone_queue_t * tq = cw_tq_new_internal();
	cte->assert2(cte, NULL != tq, "failed to create new tone queue");

	struct {
		size_t current_index;
		size_t expected_prev_index;
		bool guard;
	} input[] = {
		{ tq->capacity - 4, tq->capacity - 5, false },
		{ tq->capacity - 3, tq->capacity - 4, false },
		{ tq->capacity - 2, tq->capacity - 3, false },
		{ tq->capacity - 1, tq->capacity - 2, false },

		/* This one should never happen. We can't pass index
		   equal "capacity" because it's out of range. */
		/*
		{ tq->capacity - 0, tq->capacity - 1, false },
		*/

		{                0, tq->capacity - 1, false },
		{                1,                0, false },
		{                2,                1, false },
		{                3,                2, false },
		{                4,                3, false },

		{                0,                0, true  } /* guard */
	};

	int i = 0;
	bool failure = false;
	while (!input[i].guard) {
		const size_t readback_prev_index = cw_tq_prev_index_internal(tq, input[i].current_index);
		if (!cte->expect_eq_int_errors_only(cte, input[i].expected_prev_index, readback_prev_index, "calculating 'prev' index, test %d", i)) {
			failure = true;
			break;
		}
		i++;
	}

	cw_tq_delete_internal(&tq);

	cte->expect_eq_int(cte, false, failure, "calculating 'prev' index");

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   tests::cw_tq_next_index_internal()

   @reviewed on 2019-10-03
*/
int test_cw_tq_next_index_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_tone_queue_t * tq = cw_tq_new_internal();
	cte->assert2(cte, NULL != tq, "failed to create new tone queue");

	struct {
		size_t current_index;
		size_t expected_next_index;
		bool guard;
	} input[] = {
		{ tq->capacity - 5, tq->capacity - 4, false },
		{ tq->capacity - 4, tq->capacity - 3, false },
		{ tq->capacity - 3, tq->capacity - 2, false },
		{ tq->capacity - 2, tq->capacity - 1, false },
		{ tq->capacity - 1,                0, false },
		{                0,                1, false },
		{                1,                2, false },
		{                2,                3, false },
		{                3,                4, false },

		{                0,                0, true  } /* guard */
	};

	int i = 0;
	bool failure = false;
	while (!input[i].guard) {
		const size_t readback_next_index = cw_tq_next_index_internal(tq, input[i].current_index);
		if (!cte->expect_eq_int_errors_only(cte, input[i].expected_next_index, readback_next_index, "calculating 'next' index, test %d", i)) {
			failure = true;
			break;
		}
		i++;
	}

	cw_tq_delete_internal(&tq);

	cte->expect_eq_int(cte, false, failure, "calculating 'next' index");

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   Helper function, wrapper for some low-level operations.

   @reviewed on 2019-10-04
*/
void enqueue_tone_low_level(cw_test_executor_t * cte, cw_tone_queue_t * tq, const cw_tone_t * tone)
{
	/* This is just some code copied from implementation of
	   'enqueue' function. I don't use 'enqueue' function itself
	   because it's not tested yet. I get rid of all the other
	   code from the 'enqueue' function and use only the essential
	   part to manually add elements to list, and then to check
	   length of the list. */

	/* This block of code pretends to be enqueue function.  The
	   most important functionality of enqueue function is done
	   here manually. We don't do any checks of boundaries of tq,
	   we trust that this is enforced by for loop's conditions. */

	/* Notice that this is *before* enqueueing the tone. */
	cte->assert2(cte, tq->len < tq->capacity,
		     "length before enqueue reached capacity: %zu / %zu",
		     tq->len, tq->capacity);

	/* Enqueue the new tone and set the new tail index. */
	tq->queue[tq->tail] = *tone;
	tq->tail = cw_tq_next_index_internal(tq, tq->tail);
	tq->len++;

	cte->assert2(cte, tq->len <= tq->capacity,
		     "length after enqueue exceeded capacity: %zu / %zu",
		     tq->len, tq->capacity);
}




/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   tests::cw_tq_length_internal()
   tests::cw_get_tone_queue_length()

   @reviewed on 2019-10-04
*/
int test_cw_tq_length_internal_1(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_tone_queue_t * tq = cw_tq_new_internal();
	cte->assert2(cte, tq, "failed to create new tone queue");

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	bool failure = false;

	for (size_t i = 0; i < tq->capacity; i++) {

		enqueue_tone_low_level(cte, tq, &tone);

		/* OK, added a tone, ready to measure length of the queue. */
		const size_t expected_len = i + 1;
		const size_t readback_len = cw_tq_length_internal(tq);
		if (!cte->expect_eq_int_errors_only(cte, expected_len, readback_len, "tone queue length A, readback #1\n")) {
			failure = true;
			break;
		}
		if (!cte->expect_eq_int_errors_only(cte, tq->len, readback_len, "tone queue length A, readback #2\n")) {
			failure = true;
			break;
		}
	}

	cw_tq_delete_internal(&tq);

	cte->expect_eq_int(cte, false, failure, "tone queue length A");

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
  \brief Wrapper for tests of enqueue() and dequeue() function

  First we fill a tone queue when testing enqueue(), and then use the
  filled tone queue to test dequeue().

  @reviewed on 2019-10-04
*/
int test_cw_tq_enqueue_dequeue_internal(cw_test_executor_t * cte)
{
	const int max = (rand() % 10) + 10;
	cte->print_test_header(cte, "%s (%d)", __func__, max);

	cw_tone_queue_t * tq = cw_tq_new_internal();
	cte->assert2(cte, tq, "failed to create new tone queue");

	for (int i = 0; i < max; i++) {

		// tq->state = CW_TQ_BUSY; /* TODO: why this assignment? */

		/* Fill the tone queue with tones. */
		test_cw_tq_enqueue_internal_A(cte, tq);

		/* Use the same (now filled) tone queue to test dequeue()
		   function. */
		test_cw_tq_dequeue_internal(cte, tq);
	}

	cw_tq_delete_internal(&tq);

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   tests::cw_tq_enqueue_internal()

   @reviewed on 2019-10-04
*/
int test_cw_tq_enqueue_internal_A(cw_test_executor_t * cte, cw_tone_queue_t * tq)
{
	/* At this point cw_tq_length_internal() should be
	   tested, so we can use it to verify correctness of 'enqueue'
	   function. */

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);
	bool enqueue_failure = false;
	bool length_failure = false;

	for (size_t i = 0; i < tq->capacity; i++) {

		/* This tests for potential problems with function call. */
		const int cwret = cw_tq_enqueue_internal(tq, &tone);
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "enqueueing tone")) {
			enqueue_failure = true;
			break;
		}

		/* This tests for correctness of working of the
		   'enqueue' function and of keeping track of tone
		   queue length. */
		const size_t expected_len = i + 1;
		const size_t readback_len = cw_tq_length_internal(tq);
		if (!cte->expect_eq_int_errors_only(cte, expected_len, readback_len, "enqueue A, readback #1")) {
			length_failure = true;
			break;
		}
		if (!cte->expect_eq_int_errors_only(cte, tq->len, readback_len, "enqueue A, readback #2")) {
			length_failure = true;
			break;
		}
	}

	cte->expect_eq_int(cte, false, enqueue_failure, "enqueue A: enqueueing");
	cte->expect_eq_int(cte, false, length_failure, "enqueue A: tone queue length");



	/* Try adding a tone to full tq. */
	/* This tests for potential problems with function call.
	   Enqueueing should fail when the queue is full. */
	cte->log_info(cte, "*** you may now see \"EE: can't enqueue tone, tq is full\" message ***\n");
	const int cwret = cw_tq_enqueue_internal(tq, &tone);
	cte->expect_eq_int(cte, CW_FAILURE, cwret, "enqueue A: attempting to enqueue tone to full queue");

	/* This tests for correctness of working of the 'enqueue'
	   function.  Full tq should not grow beyond its capacity. */
	cte->expect_eq_int(cte, tq->capacity, tq->len, "enqueue A: length of full queue vs. capacity");

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   tests::cw_tq_dequeue_internal()

   @reviewed on 2019-10-04
*/
int test_cw_tq_dequeue_internal(cw_test_executor_t * cte, cw_tone_queue_t * tq)
{
	/* tq should be completely filled after tests of enqueue()
	   function. */

	/* Test some assertions about full tq, just to be sure. */
	cte->assert2(cte, tq->capacity == tq->len,
		     "dequeue: capacity != len of full queue: %zu != %zu",
		     tq->capacity, tq->len);

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	bool dequeue_failure = false;
	bool length_failure = false;

	for (size_t i = tq->capacity; i > 0; i--) {
		size_t expected_len;
		size_t readback_len;

		expected_len = i;
		readback_len = tq->len;
		/* Length of tone queue before dequeue. */
		if (!cte->expect_eq_int_errors_only(cte, expected_len, readback_len, "dequeue: length before dequeueing tone #%zu", i)) {
			length_failure = true;
			break;
		}

		/* This tests for potential problems with function call. */
		const int cwret = cw_tq_dequeue_internal(tq, &tone);
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "dequeue: dequeueing tone #%zu", i)) {
			dequeue_failure = true;
			break;
		}

		/* Length of tone queue after dequeue. */
		expected_len = i - 1;
		readback_len = tq->len;
		if (!cte->expect_eq_int_errors_only(cte, expected_len, readback_len, "dequeue: length after dequeueing tone #%zu",  i)) {
			length_failure = true;
			break;
		}
	}

	cte->expect_eq_int(cte, false, dequeue_failure, "dequeue: dequeueing tones");
	cte->expect_eq_int(cte, false, length_failure, "dequeue: length of tq");



	/* Try removing a tone from empty queue. */
	/* This tests for potential problems with function call. */
	const int cwret = cw_tq_dequeue_internal(tq, &tone);
	cte->expect_eq_int(cte, CW_FAILURE, cwret, "dequeue: attempting to dequeue tone from empty queue");


	/* This tests for correctness of working of the dequeue()
	   function.  Empty tq should stay empty.

	   At this point cw_tq_length_internal() should be already
	   tested, so we can use it to verify correctness of dequeue()
	   function. */
	const size_t readback_len = cw_tq_length_internal(tq);
	cte->expect_eq_int(cte, 0, readback_len, "dequeue: length of empty queue (readback)");
	cte->expect_eq_int(cte, 0, tq->len, "dequeue: length of empty queue (direct check)");


	return 0;
}




/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   Remember that the function checks whether tq is full, not whether
   it is non-empty.

   tests::cw_tq_is_full_internal()
   tests::cw_is_tone_queue_full()

   @reviewed on 2019-10-04
*/
int test_cw_tq_is_full_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_tone_queue_t * tq = cw_tq_new_internal();
	cte->assert2(cte, tq, "failed to create new tq");
	//tq->state = CW_TQ_BUSY; // TODO: what is it doing here?
	bool failure = false;
	bool is_full = false;

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	/* Notice the "capacity - 1" in loop condition: we leave one
	   place in tq free so that is_full() called in the loop
	   always returns false. */
	for (size_t i = 0; i < tq->capacity - 1; i++) {
		const int cwret = cw_tq_enqueue_internal(tq, &tone);
		/* The 'enqueue' function has been already tested, but
		   it won't hurt to check this simple condition here
		   as well. */
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "is_full: enqueuing tone #%zu", i)) {
			failure = true;
			break;
		}

		/* The queue shouldn't become full in this loop
		   because we enqueue only 'capacity - 1' tones. */
		is_full = cw_tq_is_full_internal(tq);
		if (!cte->expect_eq_int_errors_only(cte, false, is_full, "is_full: is tone queue full after enqueueing tone #%zu", i)) {
			failure = true;
			break;
		}
	}
	cte->expect_eq_int(cte, false, failure, "is_full: 'full' state during enqueueing:");



	/* At this point there is still place in tq for one more
	   tone. Enqueue it and verify that the tq is now full. */
	int cwret = cw_tq_enqueue_internal(tq, &tone);
	cte->expect_eq_int(cte, CW_SUCCESS, cwret, "is_full: adding last element");

	is_full = cw_tq_is_full_internal(tq);
	cte->expect_eq_int(cte, true, is_full, "is_full: queue is full after adding last element");



	/* Now test the function as we dequeue ALL tones. */
	for (size_t i = tq->capacity; i > 0; i--) {
		/* The 'dequeue' function has been already tested, but
		   it won't hurt to check this simple condition here
		   as well. */
		cwret = cw_tq_dequeue_internal(tq, &tone);
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "is_full: dequeueing tone #%zd\n", i)) {
			failure = true;
			break;
		}

		/* Here is the proper test of tested function. Since
		   we have called "dequeue" above, the queue becomes
		   non-full during first iteration. */
		is_full = cw_tq_is_full_internal(tq);
		if (!cte->expect_eq_int_errors_only(cte, false, is_full, "is_full: queue should not be full after dequeueing tone %zd\n", i)) {
			failure = true;
			break;
		}
	}
	cte->expect_eq_int(cte, false, failure, "is_full: 'full' state during dequeueing:");



	cw_tq_delete_internal(&tq);

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   \brief Test "capacity" property of tone queue

   Function tests "capacity" property of tone queue, and also tests
   related properties: head and tail.

   Just like in test_cw_tq_test_capacity_2(), enqueueing is done with
   cw_tq_enqueue_internal().

   Unlike test_cw_tq_test_capacity_2(), this function dequeues tones
   using "manual" method.

   After every dequeue we check that dequeued tone is the one that we
   were expecting to get.

   tests::cw_tq_enqueue_internal()
*/
int test_cw_tq_test_capacity_1(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

#if 0
	/* We don't need to check tq with capacity ==
	   CW_TONE_QUEUE_CAPACITY_MAX (yet). Let's test a smaller
	   queue. 30 tones will be enough (for now), and 30-4 is a
	   good value for high water mark. */
	size_t capacity = 30;
	size_t watermark = capacity - 4;

	/* We will do tests of queue with constant capacity, but with
	   different initial position at which we insert first element
	   (tone), i.e. different position of queue's head.

	   Put the guard after "capacity - 1".

	   TODO: allow negative head shifts in the test. */
	int head_shifts[] = { 0, 5, 10, 29, -1, 30, -1 };
	int s = 0;

	while (head_shifts[s] != -1) {

		bool enqueue_failure = true;
		bool dequeue_failure = true;

		// fprintf(stderr, "\nTesting with head shift = %d\n", head_shifts[s]);

		/* For every new test with new head shift we need a
		   "clean" queue. */
		cw_tone_queue_t * tq = test_cw_tq_capacity_test_init(capacity, watermark, head_shifts[s]);

		/* Fill all positions in queue with tones of known
		   frequency.  If shift_head != 0, the enqueue
		   function should make sure that the enqueued tones
		   are nicely wrapped after end of queue. */
		for (size_t i = 0; i < tq->capacity; i++) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, (int) i, 1000, CW_SLOPE_MODE_NO_SLOPES);

			const cwret = cw_tq_enqueue_internal(tq, &tone);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "capacity1: failed to enqueue tone #%zu", i)) {
				enqueue_failure = true;
				break;
			}
		}

		/* With the queue filled with valid and known data,
		   it's time to read back the data and verify that the
		   tones were placed in correct positions, as
		   expected. Let's do the readback N times, just for
		   fun. Every time the results should be the same. */

		for (int l = 0; l < 3; l++) {
			for (size_t i = 0; i < tq->capacity; i++) {

				/* When shift of head == 0, tone with
				   frequency 'i' is at index 'i'. But with
				   non-zero shift of head, tone with frequency
				   'i' is at index 'shifted_i'. */


				size_t shifted_i = (i + head_shifts[s]) % (tq->capacity);
				// fprintf(stderr, "Readback %d: position %zu: checking tone %zu, expected %zu, got %d\n",
				// 	l, shifted_i, i, i, tq->queue[shifted_i].frequency);

				/* This is the "manual" dequeue. We
				   don't really remove the tone from
				   tq, just checking that tone at
				   shifted_i has correct, expected
				   properties. */
				if (!cte->expect_eq_int_errors_only(cte, tq->queue[shifted_i].frequency, (int) i, "capacity1: frequency of dequeued tone is incorrect: %d != %d", tq->queue[shifted_i].frequency, (int) i)) {
					dequeue_failure = true;
					break;
				}
			}
		}


		/* Matches tone queue creation made in
		   test_cw_tq_capacity_test_init(). */
		cw_tq_delete_internal(&tq);

		cte->expect_eq_int(cte, false, enqueue_failure, "capacity1: enqueue @ shift=%d:", head_shifts[s]);
		cte->expect_eq_int(cte, false, dequeue_failure, "capacity1: dequeue @ shift=%d:", head_shifts[s]);

		s++;
	}
#endif
	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   \brief Test "capacity" property of tone queue

   Function tests "capacity" property of tone queue, and also tests
   related properties: head and tail.

   Just like in test_cw_tq_test_capacity_1(), enqueueing is done with
   cw_tq_enqueue_internal().

   Unlike test_cw_tq_test_capacity_1(), this function dequeues tones
   using cw_tq_dequeue_internal().

   After every dequeue we check that dequeued tone is the one that we
   were expecting to get.

   tests::cw_tq_enqueue_internal()
   tests::cw_tq_dequeue_internal()
*/
int test_cw_tq_test_capacity_2(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

#if 0
	/* We don't need to check tq with capacity ==
	   CW_TONE_QUEUE_CAPACITY_MAX (yet). Let's test a smaller
	   queue. 30 tones will be enough (for now), and 30-4 is a
	   good value for high water mark. */
	size_t capacity = 30;
	size_t watermark = capacity - 4;

	/* We will do tests of queue with constant capacity, but with
	   different initial position at which we insert first element
	   (tone), i.e. different position of queue's head.

	   Put the guard after "capacity - 1".

	   TODO: allow negative head shifts in the test. */
	int head_shifts[] = { 0, 5, 10, 29, -1, 30, -1 };
	int s = 0;

	while (head_shifts[s] != -1) {

		bool enqueue_failure = true;
		bool dequeue_failure = true;
		bool capacity_failure = true;

		// fprintf(stderr, "\nTesting with head shift = %d\n", head_shifts[s]);

		/* For every new test with new head shift we need a
		   "clean" queue. */
		cw_tone_queue_t *tq = test_cw_tq_capacity_test_init(capacity, watermark, head_shifts[s]);

		/* Fill all positions in queue with tones of known
		   frequency.  If shift_head != 0, the enqueue
		   function should make sure that the enqueued tones
		   are nicely wrapped after end of queue. */
		for (size_t i = 0; i < tq->capacity; i++) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, (int) i, 1000, CW_SLOPE_MODE_NO_SLOPES);

			const int cwret = cw_tq_enqueue_internal(tq, &tone);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "capacity2: failed to enqueue tone #%zu", i)) {
				enqueue_failure = true;
				break;
			}
		}

		/* With the queue filled with valid and known data,
		   it's time to read back the data and verify that the
		   tones were placed in correct positions, as
		   expected.

		   In test_cw_tq_test_capacity_1() we did the
		   readback "manually", this time let's use "dequeue"
		   function to do the job.

		   Since the "dequeue" function moves queue pointers,
		   we can do this test only once (we can't repeat the
		   readback N times with calls to dequeue() expecting
		   the same results). */

		size_t i = 0;
		cw_tone_t tone; /* For output only, so no need to initialize. */

		while (CW_SUCCESS == cw_tq_dequeue_internal(tq, &tone)) {

			/* When shift of head == 0, tone with
			   frequency 'i' is at index 'i'. But with
			   non-zero shift of head, tone with frequency
			   'i' is at index 'shifted_i'. */

			size_t shifted_i = (i + head_shifts[s]) % (tq->capacity);
			if (!cte->expect_eq_int_errors_only(cte, tq->queue[shifted_i].frequency, (int) i, "capacity2: position %zu: checking tone %zu, expected %zu, got %d\n", shifted_i, i, i, tq->queue[shifted_i].frequency)) {
				dequeue_failure = true;
				break;
			}

			i++;
		}

		if (!cte->expect_eq_int_errors_only(cte, i, tq->capacity, "capacity2: number of dequeues (%zu) is different than capacity (%zu)\n", i, tq->capacity)) {
			capacity_failure = true;
		}


		/* Matches tone queue creation made in
		   test_cw_tq_capacity_test_init(). */
		cw_tq_delete_internal(&tq);


		cte->expect_eq_int(cte, false, enqueue_failure, "capacity2: enqueue  @ shift=%d:", head_shifts[s]);
		cte->expect_eq_int(cte, false, dequeue_failure, "capacity2: dequeue  @ shift=%d:", head_shifts[s]);
		cte->expect_eq_int(cte, false, capacity_failure, "capacity2: capacity @ shift=%d:", head_shifts[s]);


		s++;
	}
#endif
	cte->print_test_footer(cte, __func__);

	return 0;
}




#if 0
/**
   \brief Create and initialize tone queue for tests of capacity

   Create new tone queue for tests using three given parameters: \p
   capacity, \p high_water_mark, \p head_shift. The function is used
   to create a new tone queue in tests of "capacity" parameter of a
   tone queue.

   First two function parameters are rather boring. What is
   interesting is the third parameter: \p head_shift.

   In general the behaviour of tone queue (a circular list) should be
   independent of initial position of queue's head (i.e. from which
   position in the queue we start adding new elements to the queue).

   By initializing the queue with different initial positions of head
   pointer, we can test this assertion about irrelevance of initial
   head position.

   The "initialize" word may be misleading. The function does not
   enqueue any tones, it just initializes (resets) every slot in queue
   to non-random value.

   Returned pointer is owned by caller.

   tests::cw_tq_set_capacity_internal()

   \param capacity - intended capacity of tone queue
   \param high_water_mark - high water mark to be set for tone queue
   \param head_shift - position of first element that will be inserted in empty queue

   \return newly allocated and initialized tone queue
*/
cw_tone_queue_t *test_cw_tq_capacity_test_init(size_t capacity, size_t high_water_mark, int head_shift)
{
	cw_tone_queue_t *tq = cw_tq_new_internal();
	cte->assert2(cte, tq, "failed to create new tone queue");
	tq->state = CW_TQ_BUSY;

	int rv = cw_tq_set_capacity_internal(tq, capacity, high_water_mark);
	cte->assert2(cte, rv == CW_SUCCESS, "failed to set capacity/high water mark");
	cte->assert2(cte, tq->capacity == capacity, "incorrect capacity: %zu != %zu", tq->capacity, capacity);
	cte->assert2(cte, tq->high_water_mark == high_water_mark, "incorrect high water mark: %zu != %zu", tq->high_water_mark, high_water_mark);

	/* Initialize *all* tones with known value. Do this manually,
	   to be 100% sure that all tones in queue table have been
	   initialized. */
	for (int i = 0; i < CW_TONE_QUEUE_CAPACITY_MAX; i++) {
		CW_TONE_INIT(&(tq->queue[i]), 10000 + i, 1, CW_SLOPE_MODE_STANDARD_SLOPES);
	}

	/* Move head and tail of empty queue to initial position. The
	   queue is empty - the initialization of fields done above is not
	   considered as real enqueueing of valid tones. */
	tq->tail = head_shift;
	tq->head = tq->tail;
	tq->len = 0;

	/* TODO: why do this here? */
	tq->state = CW_TQ_BUSY;

	return tq;
}
#endif




/**
   \brief Test the limits of the parameters to the tone queue routine

   tests::cw_tq_enqueue_internal()
*/
int test_cw_tq_enqueue_internal_B(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_tone_queue_t *tq = cw_tq_new_internal();
	cte->assert2(cte, tq, "failed to create a tone queue\n");
	cw_tone_t tone;
	int cwret = CW_FAILURE;


	int f_min, f_max;
	cw_get_frequency_limits(&f_min, &f_max);


	/* Test 1: invalid length of tone. */
	errno = 0;
	tone.len = -1;            /* Invalid length. */
	tone.frequency = f_min;   /* Valid frequency. */
	cwret = cw_tq_enqueue_internal(tq, &tone);
	cte->expect_eq_int(cte, CW_FAILURE, cwret, "enqueued tone with invalid length (cwret)");
	cte->expect_eq_int(cte, EINVAL, errno, "enqueued tone with invalid length (cwret)");


	/* Test 2: tone's frequency too low. */
	errno = 0;
	tone.len = 100;                /* Valid length. */
	tone.frequency = f_min - 1;    /* Invalid frequency. */
	cwret = cw_tq_enqueue_internal(tq, &tone);
	cte->expect_eq_int(cte, CW_FAILURE, cwret, "enqueued tone with too low frequency (cwret)");
	cte->expect_eq_int(cte, EINVAL, errno, "enqueued tone with too low frequency (errno)");


	/* Test 3: tone's frequency too high. */
	errno = 0;
	tone.len = 100;                /* Valid length. */
	tone.frequency = f_max + 1;    /* Invalid frequency. */
	cwret = cw_tq_enqueue_internal(tq, &tone);
	cte->expect_eq_int(cte, CW_FAILURE, cwret, "enqueued tone with too high frequency (cwret)");
	cte->expect_eq_int(cte, EINVAL, errno, "enqueued tone with too high frequency (errno)");


	cw_tq_delete_internal(&tq);
	cte->expect_null_pointer(cte, tq, "tone queue not deleted properly");

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   This function creates a generator that internally uses a tone
   queue. The generator is needed to perform automatic dequeueing
   operations, so that cw_tq_wait_for_level_internal() can detect
   expected level.

  tests::cw_tq_wait_for_level_internal()
*/
int test_cw_tq_wait_for_level_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 20, 10000, CW_SLOPE_MODE_STANDARD_SLOPES);

	for (int i = 0; i < 10; i++) {
		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, CW_DEFAULT_NULL_DEVICE);
		cte->assert2(cte, gen, "failed to create a tone queue\n");
		cw_gen_start(gen);

		bool wait_failure = false;

		/* Test the function for very small values,
		   but for a bit larger as well. */
		int level = i <= 5 ? i : 10 * i;

		/* Add a lot of tones to tone queue. "a lot" means three times more than a value of trigger level. */
		for (int j = 0; j < 3 * level; j++) {
			const int cwret = cw_tq_enqueue_internal(gen->tq, &tone);
			cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "wait for level: failed to enqueue tone #%d", j);
		}

		const int cwret = cw_tq_wait_for_level_internal(gen->tq, level);
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "wait failed for level = %d", level)) {
			wait_failure = true;
		}

		size_t len = cw_tq_length_internal(gen->tq);

		/* cw_tq_length_internal() is called after return of
		   tested function, so 'len' can be smaller by one,
		   but never larger, than 'level'.

		   During initial tests, for function implemented with
		   signals and with alternative IPC, diff was always
		   zero on my primary Linux box. */
		int diff = level - len;
		const bool diff_failure = (abs(diff) > 1);
		cte->expect_eq_int_errors_only(cte, false, diff_failure, "wait for level: difference is too large: level = %d, len = %zu, diff = %d\n", level, len, diff);

		fprintf(stderr, "          level = %d, len = %zu, diff = %d\n", level, len, diff);

		cw_gen_stop(gen);
		cw_gen_delete(&gen);

		cte->expect_eq_int(cte, false, wait_failure, "wait for level: wait @ level=%d:", level);
		cte->expect_eq_int(cte, false, diff_failure, "wait for level: diff @ level=%d:", level);
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   \brief Simple tests of queueing and dequeueing of tones

   Ensure we can generate a few simple tones, and wait for them to end.

   tests::cw_tq_enqueue_internal()
   tests::cw_tq_length_internal()
   tests::cw_wait_for_tone()
   tests::cw_tq_wait_for_level_internal()
*/
int test_cw_tq_operations_1(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_gen_t * gen = NULL;
	gen_setup(cte, &gen);


	int readback_length = 0;         /* Measured length of tone queue. */
	int expected_length = 0;  /* Expected length of tone queue. */

	int f_min, f_max;

	cw_gen_set_volume(gen, 70);
	cw_get_frequency_limits(&f_min, &f_max);

	int N = 6;              /* Number of test tones put in queue. */
	int duration = 100000;  /* Duration of tone. */
	int delta_f = ((f_max - f_min) / (N - 1));      /* Delta of frequency in loops. */


	/* Test 1: enqueue N tones, and wait for each of them
	   separately. Control length of tone queue in the process. */

	/* Enqueue first tone. Don't check queue length yet.

	   The first tone is being dequeued right after enqueueing, so
	   checking the queue length would yield incorrect result.
	   Instead, enqueue the first tone, and during the process of
	   dequeueing it, enqueue rest of the tones in the loop,
	   together with checking length of the tone queue. */
	int f = f_min;
	cw_tone_t tone;
	CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
	int cwret = cw_tq_enqueue_internal(gen->tq, &tone);
	cte->expect_eq_int(cte, CW_SUCCESS, cwret, "cw_tq_enqueue_internal()");

	/* This is to make sure that rest of tones is enqueued when
	   the first tone is being dequeued. */
	usleep(duration / 4);

	/* Enqueue rest of N tones. It is now safe to check length of
	   tone queue before and after queueing each tone: length of
	   the tone queue should increase (there won't be any decrease
	   due to dequeueing of first tone). */
	for (int i = 1; i < N; i++) {

		/* Monitor length of a queue as it is filled - before
		   adding a new tone. */
		readback_length = cw_tq_length_internal(gen->tq);
		expected_length = (i - 1);
		cte->expect_eq_int_errors_only(cte, readback_length, expected_length, "cw_tq_length_internal(): pre-enqueue (#%02d):", i);


		/* Add a tone to queue. All frequencies should be
		   within allowed range, so there should be no
		   error. */
		f = f_min + i * delta_f;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		cwret = cw_tq_enqueue_internal(gen->tq, &tone);
		cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_tq_enqueue_internal(): enqueue (#%02d)", i);


		/* Monitor length of a queue as it is filled - after
		   adding a new tone. */
		readback_length = cw_tq_length_internal(gen->tq);
		expected_length = (i - 1) + 1;
		cte->expect_eq_int_errors_only(cte, expected_length, readback_length, "cw_tq_length_internal(): post-enqueue (#%02d):", i);
	}



	/* Above we have queued N tones. libcw starts dequeueing first
	   of them before the last one is enqueued. This is why below
	   we should only check for N-1 of them. Additionally, let's
	   wait a moment till dequeueing of the first tone is without
	   a question in progress. */

	usleep(duration / 4);

	/* And this is the proper test - waiting for dequeueing tones. */
	for (int i = 1; i < N; i++) {
#if 0
		/* Monitor length of a queue as it is emptied - before dequeueing. */
		readback_length = cw_tq_length_internal(gen->tq);
		expected_length = N - i;
		cte->expect_eq_int_errors_only(cte, expected_length, readback_length, "cw_tq_length_internal(): pre-enqueue (#%02d):", i);


		/* Wait for each of N tones to be dequeued. */
		cwret = cw_wait_for_tone();
		cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "libcw: cw_wait_for_tone()");


		/* Monitor length of a queue as it is emptied - after dequeueing. */
		readback_length = cw_tq_length_internal();
		expected_length = N - i - 1;
		cte->expect_eq_int_errors_only(cte, expected_length, readback_length, "libcw: cw_tq_length_internal(): post-dequeue: readback length = %d\n", readback_length);
#endif
	}



	/* Test 2: fill a queue, but this time don't wait for each
	   tone separately, but wait for a whole queue to become
	   empty. */
	bool failure = false;
	f = 0;
	for (int i = 0; i < N; i++) {
		f = f_min + i * delta_f;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		cwret = cw_tq_enqueue_internal(gen->tq, &tone);
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_tq_enqueue_internal(%08d, %04d):", duration, f)) {
			failure = true;
			break;
		}
	}
	cte->expect_eq_int(cte, false, failure, "cw_tq_enqueue_internal(%08d, %04d):", duration, f);


	cwret = cw_tq_wait_for_level_internal(gen->tq, 0);
	cte->expect_eq_int(cte, CW_SUCCESS, cwret, "cw_tq_wait_for_level_internal(0)");


	gen_destroy(&gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   Run the complete range of tone generation, at 100Hz intervals,
   first up the octaves, and then down.  If the queue fills, though it
   shouldn't with this amount of data, then pause until it isn't so
   full.

   tests::cw_tq_enqueue_internal()
   tests::cw_tq_wait_for_level_internal()
*/
int test_cw_tq_operations_2(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_gen_t * gen = NULL;
	gen_setup(cte, &gen);

	cw_gen_set_volume(gen, 70);
	int duration = 40000;

	int f_min, f_max;
	cw_get_frequency_limits(&f_min, &f_max);

	bool wait_failure = false;
	bool queue_failure = false;

	for (int f = f_min; f < f_max; f += 100) {
		while (cw_tq_is_full_internal(gen->tq)) {
			const int cwret = cw_tq_wait_for_level_internal(gen->tq, 0);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "wait for level 0 (up)")) {
				wait_failure = true;
				break;
			}
		}

		cw_tone_t tone;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		const int cwret = cw_tq_enqueue_internal(gen->tq, &tone);
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "enqueue tone (up)")) {
			queue_failure = true;
			break;
		}
	}

	for (int f = f_max; f > f_min; f -= 100) {
		while (cw_tq_is_full_internal(gen->tq)) {
			const int cwret = cw_tq_wait_for_level_internal(gen->tq, 0);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "wait for level 0 (down)")) {
				wait_failure = true;
				break;
			}
		}

		cw_tone_t tone;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		const int cwret = cw_tq_enqueue_internal(gen->tq, &tone);
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "enqueue tone (down)")) {
			queue_failure = true;
			break;
		}
	}

	cte->expect_eq_int(cte, false, queue_failure, "cw_tq_enqueue_internal():");
	cte->expect_eq_int(cte, false, wait_failure, "cw_tq_wait_for_level_internal(A):");


	const int cwret = cw_tq_wait_for_level_internal(gen->tq, 0);
	cte->expect_eq_int(cte, CW_SUCCESS, cwret, "cw_tq_wait_for_level_internal(B):");


	/* Silence the generator before next test. */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, 100, CW_SLOPE_MODE_NO_SLOPES);
	cw_tq_enqueue_internal(gen->tq, &tone);
	cw_tq_wait_for_level_internal(gen->tq, 0);

	gen_destroy(&gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   Test the tone queue manipulations, ensuring that we can fill the
   queue, that it looks full when it is, and that we can flush it all
   again afterwards, and recover.

   tests::cw_tq_get_capacity_internal()
   tests::cw_tq_length_internal()
   tests::cw_tq_enqueue_internal()
   tests::cw_tq_wait_for_level_internal()
*/
int test_cw_tq_operations_3(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_gen_t * gen = NULL;
	gen_setup(cte, &gen);


	/* Small setup. */
	cw_gen_set_volume(gen, 70);



	/* Test: properties (capacity and length) of empty tq. */
	{
		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_tq_flush_internal(gen->tq);
		cw_tq_wait_for_level_internal(gen->tq, 0);

		const int capacity = cw_tq_get_capacity_internal(gen->tq);
		cte->expect_eq_int(cte, CW_TONE_QUEUE_CAPACITY_MAX, capacity, "empty queue's capacity");

		const int len_empty = cw_tq_length_internal(gen->tq);
		cte->expect_eq_int(cte, 0, len_empty, "empty queue's length");
	}



	/* Test: properties (capacity and length) of full tq. */

	/* FIXME: we call cw_tq_enqueue_internal() until tq is full, and then
	   expect the tq to be full while we perform tests. Doesn't
	   the tq start dequeuing tones right away? Can we expect the
	   tq to be full for some time after adding last tone?
	   Hint: check when a length of tq is decreased. Probably
	   after playing first tone on tq, which - in this test - is
	   pretty long. Or perhaps not. */
	{
		int i = 0;
		/* FIXME: cw_tq_is_full_internal() is not tested */
		while (!cw_tq_is_full_internal(gen->tq)) {
			cw_tone_t tone;
			int f = 5; /* I don't want to hear the tone during tests. */
			CW_TONE_INIT(&tone, f + (i++ & 1) * f, 1000000, CW_SLOPE_MODE_NO_SLOPES);
			cw_tq_enqueue_internal(gen->tq, &tone);
		}


		const int capacity = cw_tq_get_capacity_internal(gen->tq);
		cte->expect_eq_int(cte, CW_TONE_QUEUE_CAPACITY_MAX, capacity, "full queue's capacity");


		const int len_full = cw_tq_length_internal(gen->tq);
		cte->expect_eq_int(cte, CW_TONE_QUEUE_CAPACITY_MAX, len_full, "full queue's length");
	}



	/* Test: attempt to add tone to full queue. */
	{
		fprintf(out_file, MSG_PREFIX "you may now see \"EE:" MSG_PREFIX "can't enqueue tone, tq is full\" message:\n");
		fflush(out_file);

		cw_tone_t tone;
		CW_TONE_INIT(&tone, 100, 1000000, CW_SLOPE_MODE_NO_SLOPES);
		errno = 0;
		const int cwret = cw_tq_enqueue_internal(gen->tq, &tone);
		cte->expect_eq_int(cte, CW_FAILURE, cwret, "trying to enqueue tone to full queue (cwret)");
		cte->expect_eq_int(cte, EAGAIN, errno, "trying to enqueue tone to full queue (cwret)");
	}



	/* Test: check again properties (capacity and length) of empty
	   tq after it has been in use.

	   Empty the tq, ensure that it is empty, and do the test. */
	{
		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_tq_flush_internal(gen->tq);
		cw_tq_wait_for_level_internal(gen->tq, 0);

		const int capacity = cw_tq_get_capacity_internal(gen->tq);
		cte->expect_eq_int(cte, CW_TONE_QUEUE_CAPACITY_MAX, capacity, "empty queue's capacity");


		/* Test that the tq is really empty after
		   cw_tq_wait_for_level_internal() has returned. */
		const int len_empty = cw_tq_length_internal(gen->tq);
		cte->expect_eq_int(cte, 0, len_empty, "empty queue's length");
	}

	gen_destroy(&gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}




static void cw_test_helper_tq_callback(void * data);
static size_t cw_test_tone_queue_callback_data = 999999;
static int cw_test_helper_tq_callback_capture = false;

struct cw_test_struct{
	cw_gen_t *gen;
	size_t *data;
};


/**
   tests::cw_register_tone_queue_low_callback()
*/
int test_cw_tq_callback(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_gen_t * gen = NULL;
	gen_setup(cte, &gen);

	struct cw_test_struct s;
	s.gen = gen;
	s.data = &cw_test_tone_queue_callback_data;

	for (int i = 1; i < 10; i++) {
		/* Test the callback mechanism for very small values,
		   but for a bit larger as well. */
		int level = i <= 5 ? i : 3 * i;

		int cwret = cw_tq_register_low_level_callback_internal(gen->tq, cw_test_helper_tq_callback, (void *) &s, level);
		cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "libcw: cw_register_tone_queue_low_callback(): threshold = %d:", level);
		sleep(1);



		/* Add a lot of tones to tone queue. "a lot" means two
		   times more than a value of trigger level. */
		for (int j = 0; j < 2 * level; j++) {
			const int cwret = cw_gen_enqueue_character_partial(gen, 'e');
			cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "enqueueing tones, tone #%d", j);
		}


		/* Allow the callback to work only after initial
		   filling of queue. */
		cw_test_helper_tq_callback_capture = true;

		/* Wait for the queue to be drained to zero. While the
		   tq is drained, and level of tq reaches trigger
		   level, a callback will be called. Its only task is
		   to copy the current level (tq level at time of
		   calling the callback) value into
		   cw_test_tone_queue_callback_data.

		   Since the value of trigger level is different in
		   consecutive iterations of loop, we can test the
		   callback for different values of trigger level. */
		cw_tq_wait_for_level_internal(gen->tq, 0);

		/* Because of order of calling callback and decreasing
		   length of queue, I think that it's safe to assume
		   that there may be a difference of 1 between these
		   two values. */
		const int diff = level - cw_test_tone_queue_callback_data;
		const bool failure = abs(diff) > 1;
		cte->expect_eq_int_errors_only(cte, false, failure, "libcw: tone queue callback:           level at callback = %zd:", cw_test_tone_queue_callback_data);


		cw_tq_flush_internal(gen->tq);
	}

	gen_destroy(&gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}





static void cw_test_helper_tq_callback(void *data)
{
	if (cw_test_helper_tq_callback_capture) {
		struct cw_test_struct *s = (struct cw_test_struct *) data;

		*(s->data) = cw_tq_length_internal(s->gen->tq);

		cw_test_helper_tq_callback_capture = false;
		fprintf(stderr, MSG_PREFIX "cw_test_helper_tq_callback:    captured level = %zd\n", *(s->data));
	}

	return;
}
