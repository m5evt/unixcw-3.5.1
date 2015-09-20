#include <stdio.h>
#include <unistd.h> /* sleep() */
#include "libcw2.h"


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

void tone_queue_low_callback(__attribute__((unused)) void *arg);


/* Callback to be called when tone queue level passes this mark. */
static const int tq_low_watermark = 1;

/* There were actual n calls made to callback. We expect this to be
   equal to n_expected. */
static int n_calls = 0;

/* libcw's send speed. */
static int speed = 0;





int main(void)
{
	fprintf(stdout, "libcw/tq: testing tq for \"short space\" problem\n");
	fflush(stdout);

        /* Library initialization. */
        cw_gen_t *gen = cw_gen_new(CW_AUDIO_SOUNDCARD, NULL);
        cw_gen_start(gen);

	cw_gen_register_low_level_callback(gen, tone_queue_low_callback, NULL, tq_low_watermark);

	/* Expected number of calls to the callback with correct
	   implementation of tone queue in libcw. */
	int n_expected = ((CW_SPEED_MAX - CW_SPEED_MIN) / 2) + 1;


	/* Let's test this for a full range of supported speeds (from
	   min to max). MIN and MAX are even numbers, so +=2 is ok. */
	for (speed = CW_SPEED_MIN; speed <= CW_SPEED_MAX; speed += 2) {
		cw_gen_set_speed(gen, speed);

		fprintf(stdout, "speed = %d\n", speed);
		fflush(stdout);

		/* Correct action for libcw upon sending a single
		   space will be to enqueue few tones, and to call
		   callback when tq_low_watermark is passed.

		   In incorrect implementation of libcw's tone queue
		   the event of passing tq_low_watermark threshold
		   will be missed and callback won't be called. */
		cw_gen_enqueue_character(gen, ' ');

		cw_gen_wait_for_queue_level(gen, 0);
	}


        /* Library cleanup. */
        cw_gen_stop(gen);
        cw_gen_delete(&gen);


	fprintf(stdout, "expected %d calls, actual calls: %d\n", n_expected, n_calls);
	fflush(stdout);


	if (n_expected == n_calls) {
		/* "make check" facility requires this message to be
		   printed on stdout; don't localize it */
		fprintf(stdout, "\nlibcw: test result: success\n\n");
		return 0;
	} else {
		fprintf(stdout, "\nlibcw: test result: failure\n\n");
		return -1;
	}
}





void tone_queue_low_callback(__attribute__((unused)) void *arg)
{
	fprintf(stdout, "speed = %d, callback called\n", speed);
	fflush(stdout);

	n_calls++;

	return;
}
