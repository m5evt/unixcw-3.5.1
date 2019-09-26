#include <stdio.h>
#include <limits.h> /* UCHAR_MAX */
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>




#include "tests/libcw_test_framework.h"
#include "libcw_gen.h"
#include "libcw_gen_tests.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/gen: "




/**
   tests::cw_gen_new()
   tests::cw_gen_delete()
*/
unsigned int test_cw_gen_new_delete(cw_test_stats_t * stats)
{
	/* Arbitrary number of calls to a set of tested functions. */
	int max = 100;

	bool failure = true;
	int n = 0;

	/* new() + delete() */
	for (int i = 0; i < max; i++) {
		fprintf(stderr, MSG_PREFIX "new/delete: generator test 1/4, loop #%d/%d\n", i, max);

		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		failure = (NULL == gen);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/delete: failed to initialize generator (loop #%d)", i);
			break;
		}

		/* Try to access some fields in cw_gen_t just to be sure that the gen has been allocated properly. */
		failure = (gen->buffer_sub_start != 0);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/delete: buffer_sub_start in new generator is not at zero");
			break;
		}

		gen->buffer_sub_stop = gen->buffer_sub_start + 10;
		failure = (gen->buffer_sub_stop != 10);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/delete: buffer_sub_stop didn't store correct new value");
			break;
		}

		failure = (gen->client.name != (char *) NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/delete: initial value of generator's client name is not NULL");
			break;
		}

		failure = (gen->tq == NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/delete: tone queue is NULL");
			break;
		}

		cw_gen_delete(&gen);
		failure = (gen != NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/delete: delete() didn't set the pointer to NULL (loop #%d)", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "new/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	max = 5;

	/* new() + start() + delete() (skipping stop() on purpose). */
	for (int i = 0; i < max; i++) {
		fprintf(stderr, MSG_PREFIX "new/start/delete: generator test 2/4, loop #%d/%d\n", i, max);

		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		failure = (gen == NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/start/delete: failed to initialize generator (loop #%d)", i);
			break;
		}

		failure = (CW_SUCCESS != cw_gen_start(gen));
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/start/delete: failed to start generator (loop #%d)", i);
			break;
		}

		cw_gen_delete(&gen);
		failure = (gen != NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/start/delete: delete() didn't set the pointer to NULL (loop #%d)", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "new/start/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* new() + stop() + delete() (skipping start() on purpose). */
	fprintf(stderr, MSG_PREFIX "new/stop/delete: generator test 3/4\n");
	for (int i = 0; i < max; i++) {
		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		failure = (gen == NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/stop/delete: failed to initialize generator (loop #%d)", i);
			break;
		}

		failure = (CW_SUCCESS != cw_gen_stop(gen));
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/stop/delete: failed to stop generator (loop #%d)", i);
			break;
		}

		cw_gen_delete(&gen);
		failure = (gen != NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/stop/delete: delete() didn't set the pointer to NULL (loop #%d)", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "new/stop/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* new() + start() + stop() + delete() */
	for (int i = 0; i < max; i++) {
		fprintf(stderr, MSG_PREFIX "new/start/stop/delete: generator test 4/4, loop #%d/%d\n", i, max);

		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		failure = (gen == NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/start/stop/delete: failed to initialize generator (loop #%d)", i);
			break;
		}

		int sub_max = max;

		for (int j = 0; j < sub_max; j++) {
			failure = (CW_SUCCESS != cw_gen_start(gen));
			if (failure) {
				fprintf(out_file, MSG_PREFIX "new/start/stop/delete: failed to start generator (loop #%d-%d)", i, j);
				break;
			}

			failure = (CW_SUCCESS != cw_gen_stop(gen));
			if (failure) {
				fprintf(out_file, MSG_PREFIX "new/start/stop/delete: failed to stop generator (loop #%d-%d)", i, j);
				break;
			}
		}
		if (failure) {
			break;
		}

		cw_gen_delete(&gen);
		failure = (gen != NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "new/start/stop/delete: delete() didn't set the pointer to NULL (loop #%d)", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "new/start/stop/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	return 0;
}




unsigned int test_cw_gen_set_tone_slope(cw_test_stats_t * stats)
{
	int audio_system = CW_AUDIO_NULL;
	bool failure = true;
	int n = 0;

	/* Test 0: test property of newly created generator. */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, MSG_PREFIX "set slope: failed to initialize generator in test 0");

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_RAISED_COSINE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: initial shape (%d):", gen->tone_slope.shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != CW_AUDIO_SLOPE_LEN);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: initial length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_gen_delete(&gen);
	}



	/* Test A: pass conflicting arguments.

	   "A: If you pass to function conflicting values of \p
	   slope_shape and \p slope_len, the function will return
	   CW_FAILURE. These conflicting values are rectangular slope
	   shape and larger than zero slope length. You just can't
	   have rectangular slopes that have non-zero length." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, MSG_PREFIX "set slope: failed to initialize generator in test A");

		failure = (CW_SUCCESS == cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 10));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: conflicting arguments:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_gen_delete(&gen);
	}



	/* Test B: pass '-1' as both arguments.

	   "B: If you pass to function '-1' as value of both \p
	   slope_shape and \p slope_len, the function won't change
	   any of the related two generator's parameters." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, MSG_PREFIX "set slope: failed to initialize generator in test B");

		int shape_before = gen->tone_slope.shape;
		int len_before = gen->tone_slope.len;

		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, -1, -1));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: set tone slope -1 -1:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != shape_before);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope -1 -1: shape (%d / %d)", shape_before, gen->tone_slope.shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != len_before);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope -1 -1: len (%d / %d):", len_before, gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_gen_delete(&gen);
	}



	/* Test C1

	   "C1: If you pass to function '-1' as value of either \p
	   slope_shape or \p slope_len, the function will attempt to
	   set only this generator's parameter that is different than
	   '-1'." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, MSG_PREFIX "set slope: failed to initialize generator in test C1");


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;


		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: N -1: initial shape (%d / %d):", gen->tone_slope.shape, expected_shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: N -1: initial length (%d / %d):", gen->tone_slope.len, expected_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_LINEAR;
		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, expected_shape, -1));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: N -1: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		/* At this point only slope shape should be updated. */
		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: N -1: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: N -1: preserved length:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Set only new slope length. */
		expected_len = 30;
		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, -1, expected_len));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: -1 N: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		/* At this point only slope length should be updated
		   (compared to previous function call). */
		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: -1 N: get (%d / %d):", gen->tone_slope.len, expected_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: -1 N: preserved shape:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		cw_gen_delete(&gen);
	}



	/* Test C2

	   "C2: However, if selected slope shape is rectangular,
	   function will set generator's slope length to zero, even if
	   value of \p slope_len is '-1'." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, MSG_PREFIX "set slope: failed to initialize generator in test C2");


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;


		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: initial shape (%d / %d):", gen->tone_slope.shape, expected_shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: initial length (%d / %d):", gen->tone_slope.len, expected_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_RECTANGULAR;
		expected_len = 0; /* Even though we won't pass this to function, this is what we expect to get after this call. */
		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, expected_shape, -1));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: set rectangular:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* At this point slope shape AND slope length should
		   be updated (slope length is updated only because of
		   requested rectangular slope shape). */
		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: set rectangular: shape (%d/ %d):", gen->tone_slope.shape, expected_shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: set rectangular: length (%d / %d):", gen->tone_slope.len, expected_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		cw_gen_delete(&gen);
	}



	/* Test D

	   "D: Notice that the function allows non-rectangular slope
	   shape with zero length of the slopes. The slopes will be
	   non-rectangular, but just unusually short." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, MSG_PREFIX "set slope: failed to initialize generator in test D");


		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_LINEAR, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: LINEAR 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_LINEAR);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: LINEAR 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: LINEAR 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: RAISED_COSINE 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_RAISED_COSINE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: RAISED_COSINE 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: RAISED_COSINE 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_SINE, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: SINE 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_SINE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: SINE 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: SINE 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: RECTANGULAR 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_RECTANGULAR);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: RECTANGULAR 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: RECTANGULAR 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_LINEAR, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: LINEAR 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_LINEAR);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: LINEAR 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set slope: LINEAR 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		cw_gen_delete(&gen);
	}


	return 0;
}





/* Test some assertions about CW_TONE_SLOPE_SHAPE_*

   Code in this file depends on the fact that these values are
   different than -1. I think that ensuring that they are in general
   small, non-negative values is a good idea.

   I'm testing these values to be sure that when I get a silly idea to
   modify them, the test will catch this modification.
*/
unsigned int test_cw_gen_tone_slope_shape_enums(cw_test_stats_t * stats)
{
	bool failure = CW_TONE_SLOPE_SHAPE_LINEAR < 0
		|| CW_TONE_SLOPE_SHAPE_RAISED_COSINE < 0
		|| CW_TONE_SLOPE_SHAPE_SINE < 0
		|| CW_TONE_SLOPE_SHAPE_RECTANGULAR < 0;

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "slope shape enums:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}




/*
   It's not a test of a "forever" function, but of "forever"
   functionality.
*/
unsigned int test_cw_gen_forever_internal(cw_test_stats_t * stats)
{
	int seconds = 2;
	int p = fprintf(stdout, MSG_PREFIX "forever tone (%d seconds):", seconds);
	fflush(stdout);

	unsigned int rv = test_cw_gen_forever_sub(stats, 2, CW_AUDIO_NULL, (const char *) NULL);
	cw_assert (rv == 0, "\"forever\" test failed");

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





unsigned int test_cw_gen_forever_sub(cw_test_stats_t * stats, int seconds, int audio_system, const char *audio_device)
{
	cw_gen_t *gen = cw_gen_new(audio_system, audio_device);
	cw_assert (gen, "ERROR: failed to create generator\n");
	cw_gen_start(gen);

	sleep(1);

	cw_tone_t tone;
	/* Just some acceptable values. */
	int len = 100; /* [us] */
	int freq = 500;

	CW_TONE_INIT(&tone, freq, len, CW_SLOPE_MODE_RISING_SLOPE);
	int rv1 = cw_tq_enqueue_internal(gen->tq, &tone);

	CW_TONE_INIT(&tone, freq, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
	tone.is_forever = true;
	int rv2 = cw_tq_enqueue_internal(gen->tq, &tone);

#ifdef __FreeBSD__  /* Tested on FreeBSD 10. */
	/* Separate path for FreeBSD because for some reason signals
	   badly interfere with value returned through second arg to
	   nanolseep().  Try to run the section in #else under FreeBSD
	   to see what happens - value returned by nanosleep() through
	   "rem" will be increasing. */
	fprintf(stderr, "enter any character to end \"forever\" tone\n");
	char c;
	scanf("%c", &c);
#else
	struct timespec t;
	cw_usecs_to_timespec_internal(&t, seconds * CW_USECS_PER_SEC);
	cw_nanosleep_internal(&t);
#endif

	/* Silence the generator. */
	CW_TONE_INIT(&tone, 0, len, CW_SLOPE_MODE_FALLING_SLOPE);
	int rv3 = cw_tq_enqueue_internal(gen->tq, &tone);

	bool failure = (rv1 != CW_SUCCESS || rv2 != CW_SUCCESS || rv3 != CW_SUCCESS);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "forever tone:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}




/**
   tests::cw_gen_get_timing_parameters_internal()
*/
unsigned int test_cw_gen_get_timing_parameters_internal(cw_test_stats_t * stats)
{
	int initial = -5;

	int dot_len = initial;
	int dash_len = initial;
	int eom_space_len = initial;
	int eoc_space_len = initial;
	int eow_space_len = initial;
	int additional_space_len = initial;
	int adjustment_space_len = initial;

	cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
	cw_gen_start(gen);


	cw_gen_reset_parameters_internal(gen);
	/* Reset requires resynchronization. */
	cw_gen_sync_parameters_internal(gen);


	cw_gen_get_timing_parameters_internal(gen,
					      &dot_len,
					      &dash_len,
					      &eom_space_len,
					      &eoc_space_len,
					      &eow_space_len,
					      &additional_space_len,
					      &adjustment_space_len);

	bool failure = (dot_len == initial)
		|| (dash_len == initial)
		|| (eom_space_len == initial)
		|| (eoc_space_len == initial)
		|| (eow_space_len == initial)
		|| (additional_space_len == initial)
		|| (adjustment_space_len == initial);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "get timing parameters:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	cw_gen_delete(&gen);

	return 0;
}




/**
   \brief Test setting and getting of some basic parameters

   tests::cw_get_speed_limits()
   tests::cw_get_frequency_limits()
   tests::cw_get_volume_limits()
   tests::cw_get_gap_limits()
   tests::cw_get_weighting_limits()

   tests::cw_gen_set_speed()
   tests::cw_gen_set_frequency()
   tests::cw_gen_set_volume()
   tests::cw_gen_set_gap()
   tests::cw_gen_set_weighting()

   tests::cw_gen_get_speed()
   tests::cw_gen_get_frequency()
   tests::cw_gen_get_volume()
   tests::cw_gen_get_gap()
   tests::cw_gen_get_weighting()
*/
unsigned int test_cw_gen_parameter_getters_setters(cw_test_stats_t * stats)
{
	int off_limits = 10000;

	cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
	cw_gen_start(gen);

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int * min, int * max);
		int (* set_new_value)(cw_gen_t * gen, int new_value);
		int (* get_value)(cw_gen_t const * gen);

		int min; /* Minimal acceptable value of parameter. */
		int max; /* Maximal acceptable value of parameter. */

		const char *name;
	} test_data[] = {
		{ cw_get_speed_limits,      cw_gen_set_speed,      cw_gen_get_speed,      off_limits,  -off_limits,  "speed"      },
		{ cw_get_frequency_limits,  cw_gen_set_frequency,  cw_gen_get_frequency,  off_limits,  -off_limits,  "frequency"  },
		{ cw_get_volume_limits,     cw_gen_set_volume,     cw_gen_get_volume,     off_limits,  -off_limits,  "volume"     },
		{ cw_get_gap_limits,        cw_gen_set_gap,        cw_gen_get_gap,        off_limits,  -off_limits,  "gap"        },
		{ cw_get_weighting_limits,  cw_gen_set_weighting,  cw_gen_get_weighting,  off_limits,  -off_limits,  "weighting"  },
		{ NULL,                     NULL,                  NULL,                      0,                 0,  NULL         }
	};


	for (int i = 0; test_data[i].get_limits; i++) {

		int value = 0;
		bool failure = false;
		int n = 0;

		/* Test getting limits of values to be tested. */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);

		failure = (test_data[i].min <= -off_limits) || (test_data[i].max >= off_limits);

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "get %s limits:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test setting out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].min - 1;
		failure = (CW_SUCCESS == test_data[i].set_new_value(gen, value)) || (errno != EINVAL);

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set %s below limit:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test setting out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].max + 1;
		failure = (CW_SUCCESS == test_data[i].set_new_value(gen, value)) || (errno != EINVAL);

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set %s above limit:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test setting in-range values. Set with setter and then read back with getter. */
		failure = false;
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			failure = (CW_SUCCESS != test_data[i].set_new_value(gen, j));
			if (failure) {
				break;
			}

			failure = (test_data[i].get_value(gen) != j);
			if (failure) {
				break;
			}
		}

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "set %s within limits:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_gen_delete(&gen);

	return 0;
}




/**
   \brief Test control of volume

   Fill tone queue with short tones, then check that we can move the
   volume through its entire range.  Flush the queue when complete.

   tests::cw_get_volume_limits()
   tests::cw_gen_set_volume()
   tests::cw_gen_get_volume()
*/
unsigned int test_cw_gen_volume_functions(cw_test_stats_t * stats)
{
	int cw_min = -1, cw_max = -1;

	cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
	cw_gen_start(gen);

	/* Test: get range of allowed volumes. */
	{
		cw_get_volume_limits(&cw_min, &cw_max);

		bool failure = cw_min != CW_VOLUME_MIN
			|| cw_max != CW_VOLUME_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_get_volume_limits(): %d, %d", cw_min, cw_max);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: decrease volume from max to low. */
	{
		/* Fill the tone queue with valid tones. */
		while (!cw_gen_is_queue_full(gen)) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, 440, 100000, CW_SLOPE_MODE_STANDARD_SLOPES);
			cw_tq_enqueue_internal(gen->tq, &tone);
		}

		bool set_failure = false;
		bool get_failure = false;

		/* TODO: why call the cw_gen_wait_for_tone() at the
		   beginning and end of loop's body? */
		for (int i = cw_max; i >= cw_min; i -= 10) {
			cw_gen_wait_for_tone(gen);
			if (CW_SUCCESS != cw_gen_set_volume(gen, i)) {
				set_failure = true;
				break;
			}

			if (cw_gen_get_volume(gen) != i) {
				get_failure = true;
				break;
			}

			cw_gen_wait_for_tone(gen);
		}

		set_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_gen_set_volume() (down):");
		CW_TEST_PRINT_TEST_RESULT (set_failure, n);

		get_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_gen_get_volume() (down):");
		CW_TEST_PRINT_TEST_RESULT (get_failure, n);
	}




	/* Test: increase volume from zero to high. */
	{
		/* Fill tone queue with valid tones. */
		while (!cw_gen_is_queue_full(gen)) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, 440, 100000, CW_SLOPE_MODE_STANDARD_SLOPES);
			cw_tq_enqueue_internal(gen->tq, &tone);
		}

		bool set_failure = false;
		bool get_failure = false;

		/* TODO: why call the cw_gen_wait_for_tone() at the
		   beginning and end of loop's body? */
		for (int i = cw_min; i <= cw_max; i += 10) {
			cw_gen_wait_for_tone(gen);
			if (CW_SUCCESS != cw_gen_set_volume(gen, i)) {
				set_failure = true;
				break;
			}

			if (cw_gen_get_volume(gen) != i) {
				get_failure = true;
				break;
			}
			cw_gen_wait_for_tone(gen);
		}

		set_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_gen_set_volume() (up):");
		CW_TEST_PRINT_TEST_RESULT (set_failure, n);

		get_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_gen_get_volume() (up):");
		CW_TEST_PRINT_TEST_RESULT (get_failure, n);
	}

	cw_gen_wait_for_tone(gen);
	cw_tq_flush_internal(gen->tq);

	cw_gen_delete(&gen);

	return 0;
}




/**
   \brief Test enqueueing and playing most basic elements of Morse code

   tests::cw_gen_enqueue_mark_internal()
   tests::cw_gen_enqueue_eoc_space_internal()
   tests::cw_gen_enqueue_eow_space_internal()
*/
unsigned int test_cw_gen_enqueue_primitives(cw_test_stats_t * stats)
{
	int N = 20;

	cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
	cw_gen_start(gen);

	/* Test: sending dot. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (CW_SUCCESS != cw_gen_enqueue_mark_internal(gen, CW_DOT_REPRESENTATION, false)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_gen_enqueue_mark_internal(CW_DOT_REPRESENTATION):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending dash. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (CW_SUCCESS != cw_gen_enqueue_mark_internal(gen, CW_DASH_REPRESENTATION, false)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_gen_enqueue_mark_internal(CW_DASH_REPRESENTATION):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending inter-character space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (CW_SUCCESS != cw_gen_enqueue_eoc_space_internal(gen)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_gen_enqueue_eoc_space_internal():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending inter-word space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (CW_SUCCESS != cw_gen_enqueue_eow_space_internal(gen)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_gen_enqueue_eow_space_internal():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_gen_delete(&gen);

	return 0;
}




/**
   \brief Test playing representations of characters

   tests::cw_gen_enqueue_representation_partial_internal()
*/
unsigned int test_cw_gen_enqueue_representations(cw_test_stats_t * stats)
{
	/* Representation is valid when it contains dots and dashes
	   only.  cw_gen_enqueue_representation_partial_internal()
	   doesn't care about correct mapping of representation to a
	   character. */

	cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
	cw_gen_start(gen);

	/* Test: sending valid representations. */
	{
		bool failure = (CW_SUCCESS != cw_gen_enqueue_representation_partial_internal(gen, ".-.-.-"))
			|| (CW_SUCCESS != cw_gen_enqueue_representation_partial_internal(gen, ".-"))
			|| (CW_SUCCESS != cw_gen_enqueue_representation_partial_internal(gen, "---"))
			|| (CW_SUCCESS != cw_gen_enqueue_representation_partial_internal(gen, "...-"));

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_gen_enqueue_representation_partial_internal(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending invalid representations. */
	{
		bool failure = (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "INVALID"))
			|| (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "_._T"))
			|| (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "_.A_."))
			|| (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "S-_-"));

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_gen_enqueue_representation_partial_internal(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_gen_wait_for_tone(gen);

	struct timespec req = { .tv_sec = 3, .tv_nsec = 0 };
	cw_nanosleep_internal(&req);

	cw_gen_delete(&gen);

	return 0;
}




/**
   Send all supported characters: first as individual characters, and then as a string.

   tests::cw_gen_enqueue_character()
   tests::cw_gen_enqueue_string()
*/
unsigned int test_cw_gen_enqueue_character_and_string(cw_test_stats_t * stats)
{
	cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
	cw_gen_start(gen);

	/* Test: sending all supported characters as individual characters. */
	{
		char charlist[UCHAR_MAX + 1];
		bool failure = false;

		/* Send all the characters from the charlist individually. */
		cw_list_characters(charlist);
		fprintf(out_file, MSG_PREFIX "cw_enqueue_character(<valid>):\n"
			MSG_PREFIX "    ");
		for (int i = 0; charlist[i] != '\0'; i++) {
			fprintf(out_file, "%c", charlist[i]);
			fflush(out_file);
			if (CW_SUCCESS != cw_gen_enqueue_character(gen, charlist[i])) {
				failure = true;
				break;
			}
			cw_gen_wait_for_queue_level(gen, 0);
		}

		fprintf(out_file, "\n");

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file,MSG_PREFIX "cw_gen_enqueue_character(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending invalid character. */
	{
		bool failure = CW_SUCCESS == cw_gen_enqueue_character(gen, 0);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_gen_enqueue_character(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending all supported characters as single string. */
	{
		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);

		/* Send the complete charlist as a single string. */
		fprintf(out_file, MSG_PREFIX "cw_gen_enqueue_string(<valid>):\n"
			MSG_PREFIX "    %s\n", charlist);
		bool failure = CW_SUCCESS != cw_gen_enqueue_string(gen, charlist);

		while (cw_gen_get_queue_length(gen) > 0) {
			fprintf(out_file, MSG_PREFIX "tone queue length %-6zu\r", cw_gen_get_queue_length(gen));
			fflush(out_file);
			cw_gen_wait_for_tone(gen);
		}
		fprintf(out_file, "libcw:gen tone queue length %-6zu\n", cw_gen_get_queue_length(gen));
		cw_gen_wait_for_queue_level(gen, 0);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_gen_enqueue_string(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending invalid string. */
	{
		bool failure = CW_SUCCESS == cw_gen_enqueue_string(gen, "%INVALID%");

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "cw_gen_enqueue_string(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_gen_delete(&gen);

	return 0;
}
