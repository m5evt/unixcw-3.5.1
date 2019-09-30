#include <stdio.h>
#include <stdlib.h> /* atoi() */
#include <unistd.h> /* sleep() */
#include <libcw.h>
#include <string.h>




#include "libcw_debug.h"
#include "test_framework.h"
#include "libcw_test_tq_short_space.h"




/* This test checks presence of a specific bug in tone queue. The bug
   occurs when the application has registered low-tone-queue callback,
   the threshold for the callback is set to 1, and a single
   end-of-word space has been enqueued by client application.

   When the eow space is enqueued as a single tone-queue tone (or even
   as two tones) ("short space"), libcw may miss the event of passing
   of tone queue level from 2 to 1 and will not call the
   callback. This "miss" is probably caused by the fact that the first
   tone is deqeued and played before the second one gets enqueued.

   The solution in libcw is to enqueue eow space as more than two
   tones (three tones seem to be ok).

   The bug sits at the border between tone queue and generator, but
   since it's related to how tones are enqueued and dequeued, then I'm
   treating the bug as related to tone queue.
*/




struct tq_short_space_data {
	cw_test_executor_t * cte;
	int cw_speed;

	/* This is how many actual calls to callback have been made.
	   We expect this value to be equal to
	   n_expected_callback_executions that you will find below. */
	int n_actual_callback_executions;
};




static bool single_test_over_speed_range(struct tq_short_space_data * data, int i, int n);
static void tone_queue_low_callback(void * arg);

/* Callback to be called when tone queue level passes this mark. */
static const int tq_low_watermark = 1;



#if 0
int main(int argc, char * const argv[])
{
	int n = 1;
	if (argc > 1) {
		n = atoi(argv[1]);
		if (n < 1) {
			return -1;
		}
	}
	fprintf(stderr, "%s: %d cycle(s)\n", argv[0], n);


	fprintf(stdout, "libcw/tq: testing tq for \"short space\" problem\n");
	fflush(stdout);

	/* Expected number of calls to the callback with correct
	   implementation of tone queue in libcw. */

	cw_test_executor_t cte;
	cw_test_init(&cte, stdout, stderr, "tq short space");
	cte.current_sound_system = CW_AUDIO_NULL;

	int cwret = legacy_api_test_tq_short_space(&cte);

	if (CW_SUCCESS == cwret) {
		/* "make check" facility requires this message to be
		   printed on stdout; don't localize it */
		fprintf(stdout, "\nlibcw: test result: success\n\n");
		return 0;
	} else {
		fprintf(stdout, "\nlibcw: test result: failure\n\n");
		return -1;
	}
}
#endif



int legacy_api_test_tq_short_space(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	const int n = 3; /* TODO: this should be a large value that will allow making the test many times. */

	struct tq_short_space_data data;
	memset(&data, 0, sizeof (data));
	data.cte = cte;

	bool success = true;;
	for (int i = 0; i < n; i++) {
		cte->log_info(cte, "libcw/tq: iteration #%d / %d\n", i + 1, n);

		data.n_actual_callback_executions = 0;

		success = single_test_over_speed_range(&data, i, n);
		if (!success) {
			break;
		}
	}

	cte->expect_eq_int(cte, true, success, "Testing dequeuing short space");

	cte->print_test_footer(cte, __func__);

	return 0;
}




bool single_test_over_speed_range(struct tq_short_space_data * data, int i, int n)
{
	/* Library initialization. */
	cw_generator_new(data->cte->current_sound_system, NULL);
	cw_generator_start();


	cw_register_tone_queue_low_callback(tone_queue_low_callback, data, tq_low_watermark);


	/* Let's test this for a full range of supported speeds (from
	   min to max). MIN and MAX are even numbers, so +=2 is ok. */
	int n_iterations = 0;
	for (data->cw_speed = CW_SPEED_MIN; data->cw_speed <= CW_SPEED_MAX; data->cw_speed += 2) {
		cw_set_send_speed(data->cw_speed);
		cw_set_volume(50);
		cw_set_frequency(200);

		data->cte->log_info(data->cte, "current send speed = %d WPM\n", data->cw_speed);

		/* Correct action for libcw upon sending a single
		   space will be to enqueue few tones, and to call
		   callback when tq_low_watermark is passed.

		   In incorrect implementation of libcw's tone queue
		   the event of passing tq_low_watermark threshold
		   will be missed and callback won't be called. */
		cw_send_character(' ');

		cw_wait_for_tone_queue();
		usleep(300);
		n_iterations++;
	}


	/* Library cleanup. */
	cw_generator_stop();
	cw_generator_delete();


	/* This is how many times we did a following test: send a
	   single space and wait for queue to drain. */
	const int n_expected_callback_executions = ((CW_SPEED_MAX - CW_SPEED_MIN) / 2) + 1;
	cw_assert (n_expected_callback_executions == n_iterations, "Number of loop iterations does not meet expectations: %d vs. %d\n",
		   n_expected_callback_executions, data->n_actual_callback_executions);


	const bool success = data->cte->expect_eq_int(data->cte,
						      n_expected_callback_executions,
						      data->n_actual_callback_executions,
						      "test execution %d out of %d", i + 1, n);
	return success;
}




void tone_queue_low_callback(void * arg)
{
	struct tq_short_space_data * data = (struct tq_short_space_data *) arg;

	/* TODO: move the log to a loop looping over speeds, and add an expect there? */
	data->cte->log_info(data->cte, "current send speed = %d WPM, callback has been called (as expected)\n", data->cw_speed);

	data->n_actual_callback_executions++;

	return;
}
