#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>




#include "tests/libcw_test_utils.h"
#include "libcw_tq.h"
#include "libcw_tq_internal.h"
#include "libcw_tq_tests.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/tq: "




static cw_tone_queue_t *test_cw_tq_capacity_test_init(size_t capacity, size_t high_water_mark, int head_shift);
static unsigned int test_cw_tq_enqueue_internal_1(cw_tone_queue_t *tq, cw_test_stats_t * stats);
static unsigned int test_cw_tq_dequeue_internal(cw_tone_queue_t *tq, cw_test_stats_t * stats);





/**
   tests::cw_tq_new_internal()
   tests::cw_tq_delete_internal()
*/
unsigned int test_cw_tq_new_delete_internal(cw_test_stats_t * stats)
{
	/* Arbitrary number of calls to new()/delete() pair. */
	const int max = 40;
	bool failure = false;

	for (int i = 0; i < max; i++) {
		cw_tone_queue_t * tq = cw_tq_new_internal();

		failure = (tq == NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "failed to create new tone queue\n");
			break;
		}

		/* Try to access some fields in cw_tone_queue_t just
		   to be sure that the tq has been allocated properly. */
		failure = (tq->head != 0);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "head in new tone queue is not at zero\n");
			break;
		}

		tq->tail = tq->head + 10;
		failure = (tq->tail != 10);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "tail didn't store correct new value\n");
			break;
		}

		cw_tq_delete_internal(&tq);
		failure = (tq != NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "delete function didn't set the pointer to NULL\n");
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "new/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   tests::cw_tq_get_capacity_internal()
*/
unsigned int test_cw_tq_get_capacity_internal(cw_test_stats_t * stats)
{
	bool failure = false;

	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create new tone queue");
	for (size_t i = 10; i < 40; i++) {
		/* This is a silly test, but let's have any test of
		   the getter. */

		tq->capacity = i;
		size_t capacity = cw_tq_get_capacity_internal(tq);
		failure = (capacity != i);

		if (failure) {
			fprintf(out_file, MSG_PREFIX "incorrect capacity: %zu != %zu", capacity, i);
			break;
		}
	}

	cw_tq_delete_internal(&tq);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "get capacity:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   tests::cw_tq_prev_index_internal()
*/
unsigned int test_cw_tq_prev_index_internal(cw_test_stats_t * stats)
{
	cw_tone_queue_t * tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tone queue");

	struct {
		int arg;
		size_t expected;
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
		size_t prev = cw_tq_prev_index_internal(tq, input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, (int) prev, input[i].expected);
		failure = (prev != input[i].expected);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "calculated \"prev\" != expected \"prev\": %zu != %zu", prev, input[i].expected);
			break;
		}
		i++;
	}

	cw_tq_delete_internal(&tq);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "prev index:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   tests::cw_tq_next_index_internal()
*/
unsigned int test_cw_tq_next_index_internal(cw_test_stats_t * stats)
{
	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tone queue");

	struct {
		int arg;
		size_t expected;
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
		size_t next = cw_tq_next_index_internal(tq, input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, (int) next, input[i].expected);
		failure = (next != input[i].expected);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "calculated \"next\" != expected \"next\": %zu != %zu",  next, input[i].expected);
			break;
		}
		i++;
	}

	cw_tq_delete_internal(&tq);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "next index:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   tests::cw_tq_length_internal()
   tests::cw_get_tone_queue_length()
*/
unsigned int test_cw_tq_length_internal(cw_test_stats_t * stats)
{
	/* This is just some code copied from implementation of
	   'enqueue' function. I don't use 'enqueue' function itself
	   because it's not tested yet. I get rid of all the other
	   code from the 'enqueue' function and use only the essential
	   part to manually add elements to list, and then to check
	   length of the list. */

	cw_tone_queue_t * tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tone queue");

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	bool failure = false;

	for (size_t i = 0; i < tq->capacity; i++) {

		/* This block of code pretends to be enqueue function.
		   The most important functionality of enqueue
		   function is done here manually. We don't do any
		   checks of boundaries of tq, we trust that this is
		   enforced by for loop's conditions. */
		{
			/* Notice that this is *before* enqueueing the tone. */
			cw_assert (tq->len < tq->capacity,
				   "length before enqueue reached capacity: %zu / %zu",
				   tq->len, tq->capacity);

			/* Enqueue the new tone and set the new tail index. */
			tq->queue[tq->tail] = tone;
			tq->tail = cw_tq_next_index_internal(tq, tq->tail);
			tq->len++;

			cw_assert (tq->len <= tq->capacity,
				   "length after enqueue exceeded capacity: %zu / %zu",
				   tq->len, tq->capacity);
		}


		/* OK, added a tone, ready to measure length of the queue. */
		size_t len = cw_tq_length_internal(tq);
		failure = (len != i + 1);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "length: after adding tone #%zu length is incorrect (%zu)\n", i, len);
			break;
		}

		failure = (len != tq->len);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "length: after adding tone #%zu lengths don't match: %zu != %zu", i, len, tq->len);
			break;
		}
	}

	cw_tq_delete_internal(&tq);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "length:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
  \brief Wrapper for tests of enqueue() and dequeue() function

  First we fill a tone queue when testing enqueue(), and then use the
  tone queue to test dequeue().
*/
unsigned int test_cw_tq_enqueue_dequeue_internal(cw_test_stats_t * stats)
{
	cw_tone_queue_t * tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tone queue");
	tq->state = CW_TQ_BUSY; /* TODO: why this assignment? */

	/* Fill the tone queue with tones. */
	test_cw_tq_enqueue_internal_1(tq, stats);

	/* Use the same (now filled) tone queue to test dequeue()
	   function. */
	test_cw_tq_dequeue_internal(tq, stats);

	cw_tq_delete_internal(&tq);

	return 0;
}





/**
   tests::cw_tq_enqueue_internal()
*/
unsigned int test_cw_tq_enqueue_internal_1(cw_tone_queue_t *tq, cw_test_stats_t * stats)
{
	/* At this point cw_tq_length_internal() should be
	   tested, so we can use it to verify correctness of 'enqueue'
	   function. */

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);
	bool enqueue_failure = false;
	bool length_failure = false;
	bool failure = false;
	int n = 0;

	for (size_t i = 0; i < tq->capacity; i++) {

		/* This tests for potential problems with function call. */
		int rv = cw_tq_enqueue_internal(tq, &tone);
		if (rv != CW_SUCCESS) {
			fprintf(out_file, MSG_PREFIX "enqueue: failed to enqueue tone #%zu/%zu", i, tq->capacity);
			enqueue_failure = true;
			break;
		}

		/* This tests for correctness of working of the 'enqueue' function. */
		size_t len = cw_tq_length_internal(tq);
		if (len != i + 1) {
			fprintf(out_file, MSG_PREFIX "enqueue: incorrect tone queue length: %zu != %zu", len, i + 1);
			length_failure = true;
			break;
		}
	}

	enqueue_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "enqueue: enqueueing tones to queue:");
	CW_TEST_PRINT_TEST_RESULT (enqueue_failure, n);

	length_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "enqueue: length of tq during enqueueing:");
	CW_TEST_PRINT_TEST_RESULT (length_failure, n);



	/* Try adding a tone to full tq. */
	/* This tests for potential problems with function call.
	   Enqueueing should fail when the queue is full. */
	fprintf(out_file, MSG_PREFIX "you may now see \"EE:" MSG_PREFIX "can't enqueue tone, tq is full\" message:\n");
	fflush(out_file);
	int rv = cw_tq_enqueue_internal(tq, &tone);
	failure = rv != CW_FAILURE;
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "enqueue: attempting to enqueue tone to full queue:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* This tests for correctness of working of the 'enqueue'
	   function.  Full tq should not grow beyond its capacity. */
	failure = (tq->len != tq->capacity);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "enqueue: length of full queue == capacity (%zd == %zd):", tq->len, tq->capacity);
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}




/**
   tests::cw_tq_dequeue_internal()
*/
unsigned int test_cw_tq_dequeue_internal(cw_tone_queue_t *tq, cw_test_stats_t * stats)
{
	/* tq should be completely filled after tests of enqueue()
	   function. */

	/* Test some assertions about full tq, just to be sure. */
	cw_assert (tq->capacity == tq->len,
		   MSG_PREFIX "enqueue: capacity != len of full queue: %zu != %zu",
		   tq->capacity, tq->len);

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);
	int n = 0;

	bool dequeue_failure = false;
	bool length_failure = false;
	bool failure = false;

	for (size_t i = tq->capacity; i > 0; i--) {

		/* Length of tone queue before dequeue. */
		if (i != tq->len) {
			fprintf(out_file, MSG_PREFIX "dequeue: iteration before dequeue doesn't match len: %zu != %zu", i, tq->len);
			length_failure = true;
			break;
		}

		/* This tests for potential problems with function call. */
		int rv = cw_tq_dequeue_internal(tq, &tone);
		if (rv != CW_SUCCESS) {
			fprintf(out_file, MSG_PREFIX "dequeue: can't dequeue tone %zd/%zd", i, tq->capacity);
			dequeue_failure = true;
			break;
		}

		/* Length of tone queue after dequeue. */
		if (i - 1 != tq->len) {
			fprintf(out_file, "libcw_tq: dequeue: iteration after dequeue doesn't match len: %zu != %zu",  i - 1, tq->len);
			length_failure = true;
			break;
		}
	}

	dequeue_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "dequeue: dequeueing tones from queue:");
	CW_TEST_PRINT_TEST_RESULT (dequeue_failure, n);

	length_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "dequeue: length of tq during dequeueing:");
	CW_TEST_PRINT_TEST_RESULT (length_failure, n);



	/* Try removing a tone from empty queue. */
	/* This tests for potential problems with function call. */
	int rv = cw_tq_dequeue_internal(tq, &tone);
	failure = rv != CW_FAILURE;
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "dequeue: attempting to dequeue tone from empty queue:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* This tests for correctness of working of the dequeue()
	   function.  Empty tq should stay empty.

	   At this point cw_tq_length_internal() should be tested, so
	   we can use it to verify correctness of dequeue()
	   function. */
	size_t len = cw_tq_length_internal(tq);
	failure = (len != 0) || (tq->len != 0);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "dequeue: length of empty queue should be zero (%zd == %zd):", len, tq->len);
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* Try removing a tone from empty queue. Since the queue is
	   empty, this should fail. */
	rv = cw_tq_dequeue_internal(tq, &tone);
	bool expect = (rv == CW_FAILURE);
	expect ? stats->successes++ : stats->failures++;
	n = fprintf(out_file, MSG_PREFIX "dequeue: attempt to dequeue from empty queue should fail (got rv = %d):", rv);
	CW_TEST_PRINT_TEST_RESULT(!expect, n);

	return 0;
}





/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   tests::cw_tq_is_full_internal()
   tests::cw_is_tone_queue_full()
*/
unsigned int test_cw_tq_is_full_internal(cw_test_stats_t * stats)
{
	cw_tone_queue_t * tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tq");
	tq->state = CW_TQ_BUSY;
	bool failure = true;
	int n = 0;

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	/* Notice the "capacity - 1" in loop condition: we leave one
	   place in tq free so that is_full() called in the loop
	   always returns false. */
	for (size_t i = 0; i < tq->capacity - 1; i++) {
		int rv = cw_tq_enqueue_internal(tq, &tone);
		/* The 'enqueue' function has been already tested, but
		   it won't hurt to check this simple assertion here
		   as well. */
		failure = (rv != CW_SUCCESS);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "is_full: failed to enqueue tone #%zu", i);
			break;
		}

		bool is_full = cw_tq_is_full_internal(tq);
		failure = is_full;
		if (failure) {
			fprintf(out_file, MSG_PREFIX "is_full: tone queue is full after enqueueing tone #%zu", i);
			break;
		}
	}


	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "is_full: 'full' state during enqueueing:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* At this point there is still place in tq for one more
	   tone. Enqueue it and verify that the tq is now full. */
	int rv = cw_tq_enqueue_internal(tq, &tone);
	failure = rv != CW_SUCCESS;
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "is_full: adding last element:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	bool is_full = cw_tq_is_full_internal(tq);
	failure = !is_full;
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "is_full: queue is full after adding last element:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* Now test the function as we dequeue tones. */

	for (size_t i = tq->capacity; i > 0; i--) {
		/* The 'dequeue' function has been already tested, but
		   it won't hurt to check this simple assertion here
		   as well. */
		failure = (CW_FAILURE == cw_tq_dequeue_internal(tq, &tone));
		if (failure) {
			fprintf(out_file, MSG_PREFIX "is_full: failed to dequeue tone %zd\n", i);
			break;
		}

		/* Here is the proper test of tested function. */
		failure = (true == cw_tq_is_full_internal(tq));
		if (failure) {
			fprintf(out_file, MSG_PREFIX "is_full: queue is full after dequeueing tone %zd\n", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "is_full: 'full' state during dequeueing:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	cw_tq_delete_internal(&tq);

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
unsigned int test_cw_tq_test_capacity_1(cw_test_stats_t * stats)
{
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

			int rv = cw_tq_enqueue_internal(tq, &tone);
			enqueue_failure = (rv != CW_SUCCESS);
			if (enqueue_failure) {
				fprintf(out_file, MSG_PREFIX "capacity1: failed to enqueue tone #%zu", i);
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
				dequeue_failure = tq->queue[shifted_i].frequency != (int) i;
				if (dequeue_failure) {
					fprintf(out_file, MSG_PREFIX "capacity1: frequency of dequeued tone is incorrect: %d != %d", tq->queue[shifted_i].frequency, (int) i);
					break;
				}
			}
		}


		/* Matches tone queue creation made in
		   test_cw_tq_capacity_test_init(). */
		cw_tq_delete_internal(&tq);

		enqueue_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "capacity1: enqueue @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (enqueue_failure, n);

		dequeue_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "capacity1: dequeue @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (dequeue_failure, n);

		s++;
	}

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
unsigned int test_cw_tq_test_capacity_2(cw_test_stats_t * stats)
{
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

			int rv = cw_tq_enqueue_internal(tq, &tone);
			enqueue_failure = (rv != CW_SUCCESS);
			if (enqueue_failure) {
				fprintf(out_file, MSG_PREFIX "capacity2: failed to enqueue tone #%zu", i);
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

			dequeue_failure = (tq->queue[shifted_i].frequency != (int) i);
			if (dequeue_failure) {
				fprintf(out_file, MSG_PREFIX "capacity2: position %zu: checking tone %zu, expected %zu, got %d\n", shifted_i, i, i, tq->queue[shifted_i].frequency);
				break;
			}

			i++;
		}

		capacity_failure = (i != tq->capacity);
		if (capacity_failure) {
			fprintf(out_file, MSG_PREFIX "capacity2: number of dequeues (%zu) is different than capacity (%zu)\n", i, tq->capacity);
		}


		/* Matches tone queue creation made in
		   test_cw_tq_capacity_test_init(). */
		cw_tq_delete_internal(&tq);


		enqueue_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "capacity2: enqueue  @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (enqueue_failure, n);

		dequeue_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "capacity2: dequeue  @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (dequeue_failure, n);

		capacity_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "capacity2: capacity @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (capacity_failure, n);


		s++;
	}

	return 0;
}





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
	cw_assert (tq, "failed to create new tone queue");
	tq->state = CW_TQ_BUSY;

	int rv = cw_tq_set_capacity_internal(tq, capacity, high_water_mark);
	cw_assert (rv == CW_SUCCESS, "failed to set capacity/high water mark");
	cw_assert (tq->capacity == capacity, "incorrect capacity: %zu != %zu", tq->capacity, capacity);
	cw_assert (tq->high_water_mark == high_water_mark, "incorrect high water mark: %zu != %zu", tq->high_water_mark, high_water_mark);

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





/**
   \brief Test the limits of the parameters to the tone queue routine

   tests::cw_tq_enqueue_internal()
*/
unsigned int test_cw_tq_enqueue_internal_2(void)
{
	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create a tone queue\n");
	cw_tone_t tone;


	int f_min, f_max;
	cw_get_frequency_limits(&f_min, &f_max);


	/* Test 1: invalid length of tone. */
	errno = 0;
	tone.len = -1;            /* Invalid length. */
	tone.frequency = f_min;   /* Valid frequency. */
	int status = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (status == CW_FAILURE, "enqueued tone with invalid length.\n");
	cw_assert (errno == EINVAL, "bad errno: %s\n", strerror(errno));


	/* Test 2: tone's frequency too low. */
	errno = 0;
	tone.len = 100;                /* Valid length. */
	tone.frequency = f_min - 1;    /* Invalid frequency. */
	status = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (status == CW_FAILURE, "enqueued tone with too low frequency.\n");
	cw_assert (errno == EINVAL, "bad errno: %s\n", strerror(errno));


	/* Test 3: tone's frequency too high. */
	errno = 0;
	tone.len = 100;                /* Valid length. */
	tone.frequency = f_max + 1;    /* Invalid frequency. */
	status = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (status == CW_FAILURE, "enqueued tone with too high frequency.\n");
	cw_assert (errno == EINVAL, "bad errno: %s\n", strerror(errno));

	cw_tq_delete_internal(&tq);
	cw_assert (!tq, "tone queue not deleted properly\n");

	int n = printf(MSG_PREFIX "cw_tq_enqueue_internal():");
	CW_TEST_PRINT_TEST_RESULT (false, n);

	return 0;
}





/**
   This function creates a generator that internally uses a tone
   queue. The generator is needed to perform automatic dequeueing
   operations, so that cw_tq_wait_for_level_internal() can detect
   expected level.

  tests::cw_tq_wait_for_level_internal()
*/
unsigned int test_cw_tq_wait_for_level_internal(cw_test_stats_t * stats)
{
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 20, 10000, CW_SLOPE_MODE_STANDARD_SLOPES);

	for (int i = 0; i < 10; i++) {
		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, CW_DEFAULT_NULL_DEVICE);
		cw_assert (gen, "failed to create a tone queue\n");
		cw_gen_start(gen);

		/* Test the function for very small values,
		   but for a bit larger as well. */
		int level = i <= 5 ? i : 10 * i;

		/* Add a lot of tones to tone queue. "a lot" means three times more than a value of trigger level. */
		for (int j = 0; j < 3 * level; j++) {
			int rv = cw_tq_enqueue_internal(gen->tq, &tone);
			cw_assert (rv, MSG_PREFIX "wait for level: failed to enqueue tone #%d", j);
		}

		int rv = cw_tq_wait_for_level_internal(gen->tq, level);
		bool wait_failure = (rv != CW_SUCCESS);
		if (wait_failure) {
			fprintf(out_file, MSG_PREFIX "wait failed for level = %d", level);
		}

		size_t len = cw_tq_length_internal(gen->tq);

		/* cw_tq_length_internal() is called after return of
		   tested function, so 'len' can be smaller by one,
		   but never larger, than 'level'.

		   During initial tests, for function implemented with
		   signals and with alternative IPC, diff was always
		   zero on my primary Linux box. */
		int diff = level - len;
		bool diff_failure = (abs(diff) > 1);
		if (diff_failure) {
			fprintf(out_file, MSG_PREFIX "wait for level: difference is too large: level = %d, len = %zu, diff = %d\n", level, len, diff);
		}

		fprintf(stderr, "          level = %d, len = %zu, diff = %d\n", level, len, diff);

		cw_gen_stop(gen);
		cw_gen_delete(&gen);

		wait_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "wait for level: wait @ level=%d:", level);
		CW_TEST_PRINT_TEST_RESULT (wait_failure, n);

		diff_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "wait for level: diff @ level=%d:", level);
		CW_TEST_PRINT_TEST_RESULT (diff_failure, n);
	}


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
unsigned int test_cw_tq_operations_1(cw_gen_t * gen, cw_test_stats_t * stats)
{
	int l = 0;         /* Measured length of tone queue. */
	int expected = 0;  /* Expected length of tone queue. */

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
	bool failure = !cw_tq_enqueue_internal(gen->tq, &tone);
	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "cw_tq_enqueue_internal():");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


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
		l = cw_tq_length_internal(gen->tq);
		expected = (i - 1);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_tq_length_internal(): pre (#%02d):", i);
		// n = printf("libcw: cw_tq_length_internal(): pre-queue: expected %d != result %d:", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Add a tone to queue. All frequencies should be
		   within allowed range, so there should be no
		   error. */
		f = f_min + i * delta_f;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		failure = !cw_tq_enqueue_internal(gen->tq, &tone);

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_tq_enqueue_internal():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Monitor length of a queue as it is filled - after
		   adding a new tone. */
		l = cw_tq_length_internal(gen->tq);
		expected = (i - 1) + 1;
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_tq_length_internal(): post (#%02d):", i);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
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
		l = cw_tq_length_internal(gen->tq);
		expected = N - i;
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_tq_length_internal(): pre (#%02d):", i);
		// n = printf("libcw: cw_tq_length_internal(): pre-dequeue:  expected %d != result %d: failure\n", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Wait for each of N tones to be dequeued. */
		failure = !cw_wait_for_tone();

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw: cw_wait_for_tone():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Monitor length of a queue as it is emptied - after dequeueing. */
		l = cw_tq_length_internal();
		expected = N - i - 1;
		//printf("libcw: cw_tq_length_internal(): post: l = %d\n", l);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_tq_length_internal(): post (#%02d):", i);
		// n = printf("libcw: cw_tq_length_internal(): post-dequeue: expected %d != result %d: failure\n", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
#endif
	}



	/* Test 2: fill a queue, but this time don't wait for each
	   tone separately, but wait for a whole queue to become
	   empty. */
	failure = false;
	f = 0;
	for (int i = 0; i < N; i++) {
		f = f_min + i * delta_f;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		if (!cw_tq_enqueue_internal(gen->tq, &tone)) {
			failure = true;
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "cw_tq_enqueue_internal(%08d, %04d):", duration, f);
	CW_TEST_PRINT_TEST_RESULT (failure, n);



	failure = !cw_tq_wait_for_level_internal(gen->tq, 0);

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "cw_tq_wait_for_level_internal():");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

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
unsigned int test_cw_tq_operations_2(cw_gen_t * gen, cw_test_stats_t * stats)
{
	cw_gen_set_volume(gen, 70);
	int duration = 40000;

	int f_min, f_max;
	cw_get_frequency_limits(&f_min, &f_max);

	bool wait_failure = false;
	bool queue_failure = false;

	for (int f = f_min; f < f_max; f += 100) {
		while (cw_tq_is_full_internal(gen->tq)) {
			if (!cw_tq_wait_for_level_internal(gen->tq, 0)) {
				wait_failure = true;
				break;
			}
		}

		cw_tone_t tone;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		if (!cw_tq_enqueue_internal(gen->tq, &tone)) {
			queue_failure = true;
			break;
		}
	}

	for (int f = f_max; f > f_min; f -= 100) {
		while (cw_tq_is_full_internal(gen->tq)) {
			if (!cw_tq_wait_for_level_internal(gen->tq, 0)) {
				wait_failure = true;
				break;
			}
		}
		cw_tone_t tone;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		if (!cw_tq_enqueue_internal(gen->tq, &tone)) {
			queue_failure = true;
			break;
		}
	}


	queue_failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "cw_tq_enqueue_internal():");
	CW_TEST_PRINT_TEST_RESULT (queue_failure, n);


	wait_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "cw_tq_wait_for_level_internal(A):");
	CW_TEST_PRINT_TEST_RESULT (wait_failure, n);


	n = fprintf(out_file, MSG_PREFIX "cw_tq_wait_for_level_internal(B):");
	fflush(out_file);
	bool wait_tq_failure = !cw_tq_wait_for_level_internal(gen->tq, 0);
	wait_tq_failure ? stats->failures++ : stats->successes++;
	CW_TEST_PRINT_TEST_RESULT (wait_tq_failure, n);


	/* Silence the generator before next test. */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, 100, CW_SLOPE_MODE_NO_SLOPES);
	cw_tq_enqueue_internal(gen->tq, &tone);
	cw_tq_wait_for_level_internal(gen->tq, 0);

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
unsigned int test_cw_tq_operations_3(cw_gen_t * gen, cw_test_stats_t * stats)
{
	/* Small setup. */
	cw_gen_set_volume(gen, 70);



	/* Test: properties (capacity and length) of empty tq. */
	{
		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_tq_flush_internal(gen->tq);
		cw_tq_wait_for_level_internal(gen->tq, 0);

		int capacity = cw_tq_get_capacity_internal(gen->tq);
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "empty queue's capacity: %d %s %d:",
				capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		int len_empty = cw_tq_length_internal(gen->tq);
		failure = len_empty > 0;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "empty queue's length: %d %s 0:", len_empty, failure ? "!=" : "==");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
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


		int capacity = cw_tq_get_capacity_internal(gen->tq);
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "full queue's capacity: %d %s %d:",
				capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		int len_full = cw_tq_length_internal(gen->tq);
		failure = len_full != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "full queue's length: %d %s %d:",
			    len_full, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: attempt to add tone to full queue. */
	{
		fprintf(out_file, MSG_PREFIX "you may now see \"EE:" MSG_PREFIX "can't enqueue tone, tq is full\" message:\n");
		fflush(out_file);

		cw_tone_t tone;
		CW_TONE_INIT(&tone, 100, 1000000, CW_SLOPE_MODE_NO_SLOPES);
		errno = 0;
		int status = cw_tq_enqueue_internal(gen->tq, &tone);
		bool failure = status || errno != EAGAIN;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "trying to enqueue tone to full queue:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: check again properties (capacity and length) of empty
	   tq after it has been in use.

	   Empty the tq, ensure that it is empty, and do the test. */
	{
		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_tq_flush_internal(gen->tq);
		cw_tq_wait_for_level_internal(gen->tq, 0);

		int capacity = cw_tq_get_capacity_internal(gen->tq);
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "empty queue's capacity: %d %s %d:",
				capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test that the tq is really empty after
		   cw_tq_wait_for_level_internal() has returned. */
		int len_empty = cw_tq_length_internal(gen->tq);
		failure = len_empty > 0;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "empty queue's length: %d %s 0:", len_empty, failure ? "!=" : "==");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

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
unsigned int test_cw_tq_callback(cw_gen_t *gen, cw_test_stats_t *stats)
{
	struct cw_test_struct s;
	s.gen = gen;
	s.data = &cw_test_tone_queue_callback_data;

	for (int i = 1; i < 10; i++) {
		/* Test the callback mechanism for very small values,
		   but for a bit larger as well. */
		int level = i <= 5 ? i : 3 * i;

		int rv = cw_tq_register_low_level_callback_internal(gen->tq, cw_test_helper_tq_callback, (void *) &s, level);
		bool failure = rv == CW_FAILURE;
		sleep(1);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_register_tone_queue_low_callback(): threshold = %d:", level);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Add a lot of tones to tone queue. "a lot" means two
		   times more than a value of trigger level. */
		for (int j = 0; j < 2 * level; j++) {
			rv = cw_gen_enqueue_character_partial(gen, 'e');
			assert (rv);
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
		int diff = level - cw_test_tone_queue_callback_data;
		failure = abs(diff) > 1;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: tone queue callback:           level at callback = %zd:", cw_test_tone_queue_callback_data);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_tq_flush_internal(gen->tq);
	}

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