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
int test_cw_gen_new_delete(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Arbitrary number of calls to a set of tested functions. */
	int max = 100;

	bool failure = true;
	cw_gen_t * gen = NULL;

	/* new() + delete() */
	for (int i = 0; i < max; i++) {
		fprintf(stderr, MSG_PREFIX "new/delete: generator test 1/4, loop #%d/%d\n", i, max);

		gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		if (!cte->expect_valid_pointer_errors_only(cte, gen, "new/delete: failed to initialize generator (loop #%d)", i)) {
			failure = true;
			break;
		}

		/* Try to access some fields in cw_gen_t just to be sure that the gen has been allocated properly. */
		if (!cte->expect_eq_int_errors_only(cte, 0, gen->buffer_sub_start, "new/delete: buffer_sub_start in new generator is not at zero")) {
			failure = true;
			break;
		}

		gen->buffer_sub_stop = gen->buffer_sub_start + 10;
		if (!cte->expect_eq_int_errors_only(cte, 10, gen->buffer_sub_stop, "new/delete: buffer_sub_stop didn't store correct new value")) {
			failure = true;
			break;
		}

		if (!cte->expect_null_pointer_errors_only(cte, gen->client.name, "new/delete: initial value of generator's client name is not NULL")) {
			failure = true;
			break;
		}

		if (!cte->expect_valid_pointer_errors_only(cte, gen->tq, "new/delete: tone queue is NULL")) {
			failure = true;
			break;
		}

		cw_gen_delete(&gen);
		if (!cte->expect_null_pointer_errors_only(cte, gen, "new/delete: delete() didn't set the pointer to NULL (loop #%d)", i)) {
			failure = true;
			break;
		}
	}

	cte->expect_eq_int(cte, false, failure, "new/delete:");

	/* Clean up after (possibly) failed test. */
	if (gen) {
		cw_gen_delete(&gen);
	}




	max = 5;

	/* new() + start() + delete() (skipping stop() on purpose). */
	gen = NULL;
	for (int i = 0; i < max; i++) {
		fprintf(stderr, MSG_PREFIX "new/start/delete: generator test 2/4, loop #%d/%d\n", i, max);

		gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		if (!cte->expect_valid_pointer_errors_only(cte, gen, "new/start/delete: failed to initialize generator (loop #%d)", i)) {
			failure = true;
			break;
		}

		const int cwret = cw_gen_start(gen);
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "new/start/delete: failed to start generator (loop #%d)", i)) {
			failure = true;
			break;
		}

		cw_gen_delete(&gen);
		if (cte->expect_null_pointer_errors_only(cte, gen, "new/start/delete: delete() didn't set the pointer to NULL (loop #%d)", i)) {
			failure = true;
			break;
		}
	}
	cte->expect_eq_int(cte, false, failure, "new/start/delete:");

	/* Clean up after (possibly) failed test. */
	if (gen) {
		cw_gen_delete(&gen);
	}



	/* new() + stop() + delete() (skipping start() on purpose). */
	fprintf(stderr, MSG_PREFIX "new/stop/delete: generator test 3/4\n");
	gen = NULL;
	for (int i = 0; i < max; i++) {
		gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		if (!cte->expect_valid_pointer_errors_only(cte, gen, "new/stop/delete: failed to initialize generator (loop #%d)", i)) {
			failure = true;
			break;
		}

		const int cwret = cw_gen_stop(gen);
		if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "new/stop/delete: failed to stop generator (loop #%d)", i)) {
			failure = true;
			break;
		}

		cw_gen_delete(&gen);
		if (!cte->expect_null_pointer_errors_only(cte, gen, "new/stop/delete: delete() didn't set the pointer to NULL (loop #%d)", i)) {
			failure = true;
			break;
		}
	}
	cte->expect_eq_int(cte, false, failure, "new/stop/delete:");
	/* Clean up after (possibly) failed test. */
	if (gen) {
		cw_gen_delete(&gen);
	}



	/* new() + start() + stop() + delete() */
	gen = NULL;
	for (int i = 0; i < max; i++) {
		fprintf(stderr, MSG_PREFIX "new/start/stop/delete: generator test 4/4, loop #%d/%d\n", i, max);

		gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		if (!cte->expect_valid_pointer_errors_only(cte, gen, "new/start/stop/delete: failed to initialize generator (loop #%d)", i)) {
			failure = true;
			break;
		}

		int sub_max = max;

		for (int j = 0; j < sub_max; j++) {
			int cwret = CW_FAILURE;

			cwret = cw_gen_start(gen);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "new/start/stop/delete: failed to start generator (loop #%d-%d)", i, j)) {
				failure = true;
				break;
			}

			cwret = cw_gen_stop(gen);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "new/start/stop/delete: failed to stop generator (loop #%d-%d)", i, j)) {
				failure = true;
				break;
			}
		}
		if (failure) {
			break;
		}

		cw_gen_delete(&gen);
		if (!cte->expect_null_pointer_errors_only(cte, gen, "new/start/stop/delete: delete() didn't set the pointer to NULL (loop #%d)", i)) {
			failure = true;
			break;
		}
	}
	cte->expect_eq_int(cte, false, failure, "new/start/stop/delete:");

	/* Clean up after (possibly) failed test. */
	if (gen) {
		cw_gen_delete(&gen);
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}




int test_cw_gen_set_tone_slope(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	int audio_system = CW_AUDIO_NULL;
	bool failure = true;
	int cwret = CW_FAILURE;

	/* Test 0: test property of newly created generator. */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, MSG_PREFIX "set slope: failed to initialize generator in test 0");

		cte->expect_eq_int(cte, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, gen->tone_slope.shape, "set slope: initial shape (%d)", gen->tone_slope.shape);
		cte->expect_eq_int(cte, CW_AUDIO_SLOPE_LEN, gen->tone_slope.len, "set slope: initial length (%d)", gen->tone_slope.len);

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

		cwret = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 10);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: conflicting arguments");

		cw_gen_delete(&gen);
	}



	/* Test B: pass '-1' as both arguments.

	   "B: If you pass to function '-1' as value of both \p
	   slope_shape and \p slope_len, the function won't change
	   any of the related two generator's parameters." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, MSG_PREFIX "set slope: failed to initialize generator in test B");

		const int shape_before = gen->tone_slope.shape;
		const int len_before = gen->tone_slope.len;

		const int cwret = failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, -1, -1));

		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: set tone slope -1 -1");
		cte->expect_eq_int(cte, shape_before, gen->tone_slope.shape, "set slope -1 -1: shape (%d / %d)", shape_before, gen->tone_slope.shape);
		cte->expect_eq_int(cte, len_before, gen->tone_slope.len, "set slope -1 -1: len (%d / %d)", len_before, gen->tone_slope.len);

		cw_gen_delete(&gen);
	}



	/* Test C1

	   "C1: If you pass to function '-1' as value of either \p
	   slope_shape or \p slope_len, the function will attempt to
	   set only this generator's parameter that is different than
	   '-1'." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		if (!cte->expect_valid_pointer(cte, gen, "set slope: failed to initialize generator in test C1")) {
			return -1;
		}


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;


		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		cte->expect_eq_int(cte, expected_shape, gen->tone_slope.shape, "set slope: N -1: initial shape (%d / %d)", gen->tone_slope.shape, expected_shape);
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: N -1: initial length (%d / %d)", gen->tone_slope.len, expected_len);



		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_LINEAR;
		cwret = cw_gen_set_tone_slope(gen, expected_shape, -1);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: N -1: set");

		/* At this point only slope shape should be updated. */
		cte->expect_eq_int(cte, expected_shape, gen->tone_slope.shape, "set slope: N -1: get:");
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: N -1: preserved length");



		/* Set only new slope length. */
		expected_len = 30;
		cwret = cw_gen_set_tone_slope(gen, -1, expected_len);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: -1 N: set:");

		/* At this point only slope length should be updated
		   (compared to previous function call). */
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: -1 N: get (%d / %d)", gen->tone_slope.len, expected_len);
		cte->expect_eq_int(cte, expected_shape, gen->tone_slope.shape, "set slope: -1 N: preserved shape:");



		cw_gen_delete(&gen);
	}



	/* Test C2

	   "C2: However, if selected slope shape is rectangular,
	   function will set generator's slope length to zero, even if
	   value of \p slope_len is '-1'." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		if (!cte->expect_valid_pointer(cte, gen, "set slope: failed to initialize generator in test C2")) {
			return -1;
		}


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;


		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		cte->expect_eq_int(cte, expected_shape, gen->tone_slope.shape, "set slope: initial shape (%d / %d):", gen->tone_slope.shape, expected_shape);
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: initial length (%d / %d):", gen->tone_slope.len, expected_len);



		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_RECTANGULAR;
		expected_len = 0; /* Even though we won't pass this to function, this is what we expect to get after this call. */
		cwret = cw_gen_set_tone_slope(gen, expected_shape, -1);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: set rectangular");



		/* At this point slope shape AND slope length should
		   be updated (slope length is updated only because of
		   requested rectangular slope shape). */
		cte->expect_eq_int(cte, expected_shape, gen->tone_slope.shape, "set slope: set rectangular: shape (%d/ %d):", gen->tone_slope.shape, expected_shape);
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: set rectangular: length (%d / %d):", gen->tone_slope.len, expected_len);


		cw_gen_delete(&gen);
	}



	/* Test D

	   "D: Notice that the function allows non-rectangular slope
	   shape with zero length of the slopes. The slopes will be
	   non-rectangular, but just unusually short." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		if (!cte->expect_valid_pointer(cte, gen, "set slope: failed to initialize generator in test D")) {
			return -1;
		}

		const int expected_len = 0;


		cwret = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_LINEAR, expected_len);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: <LINEAR/0>: set");
		cte->expect_eq_int(cte, CW_TONE_SLOPE_SHAPE_LINEAR, gen->tone_slope.shape, "set slope: <LINEAR/0>: get");
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: <LINEAR/0>: length = %d:", gen->tone_slope.len);


		cwret = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, 0);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: <RAISED_COSINE/0>: set");
		cte->expect_eq_int(cte, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, gen->tone_slope.shape, "set slope: <RAISED_COSINE/0>: get");
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: <RAISED_COSINE/0>: length = %d:", gen->tone_slope.len);


		cwret = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_SINE, 0);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: <SINE/0>: set:");
		cte->expect_eq_int(cte, CW_TONE_SLOPE_SHAPE_SINE, gen->tone_slope.shape, "set slope: <SINE/0>: get:");
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: <SINE/0>: length = %d", gen->tone_slope.len);


		cwret = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 0);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: <RECTANGULAR/0>: set");
		cte->expect_eq_int(cte, CW_TONE_SLOPE_SHAPE_RECTANGULAR, gen->tone_slope.shape, "set slope: <RECTANGULAR/0>: get");
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: <RECTANGULAR/0>: length = %d:", gen->tone_slope.len);


		cwret = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_LINEAR, 0);
		cte->expect_eq_int(cte, CW_SUCCESS, cwret, "set slope: <LINEAR/0>: set");
		cte->expect_eq_int(cte, CW_TONE_SLOPE_SHAPE_LINEAR, gen->tone_slope.shape, "set slope: <LINEAR/0>: get");
		cte->expect_eq_int(cte, expected_len, gen->tone_slope.len, "set slope: <LINEAR/0>: length = %d", gen->tone_slope.len);


		cw_gen_delete(&gen);
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}





/* Test some assertions about CW_TONE_SLOPE_SHAPE_*

   Code in this file depends on the fact that these values are
   different than -1. I think that ensuring that they are in general
   small, non-negative values is a good idea.

   I'm testing these values to be sure that when I get a silly idea to
   modify them, the test will catch this modification.
*/
int test_cw_gen_tone_slope_shape_enums(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	const bool failure = CW_TONE_SLOPE_SHAPE_LINEAR < 0
		|| CW_TONE_SLOPE_SHAPE_RAISED_COSINE < 0
		|| CW_TONE_SLOPE_SHAPE_SINE < 0
		|| CW_TONE_SLOPE_SHAPE_RECTANGULAR < 0;

	cte->expect_eq_int_errors_only(cte, false, failure, "slope shape enums:");

	cte->print_test_footer(cte, __func__);

	return 0;
}




/*
   It's not a test of a "forever" function, but of "forever"
   functionality.
*/
int test_cw_gen_forever_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	const int seconds = 2;
	cte->log_info(cte, "forever tone (%d seconds):", seconds);

	const int rv = test_cw_gen_forever_sub(cte, 2, CW_AUDIO_NULL, (const char *) NULL);
	cte->expect_eq_int_errors_only(cte, 0, rv, "'forever' test");

	cte->print_test_footer(cte, __func__);

	return 0;
}





int test_cw_gen_forever_sub(cw_test_executor_t * cte, int seconds, int audio_system, const char *audio_device)
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
	const int cwret1 = cw_tq_enqueue_internal(gen->tq, &tone);
	cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret1, "forever tone: enqueue first tone"); /* Use "_errors_only() here because this is not a core part of test. */

	CW_TONE_INIT(&tone, freq, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
	tone.is_forever = true;
	const int cwret2 = cw_tq_enqueue_internal(gen->tq, &tone);
	cte->expect_eq_int(cte, CW_SUCCESS, cwret2, "forever tone: enqueue forever tone");

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
	const int cwret3 = cw_tq_enqueue_internal(gen->tq, &tone);
	cte->expect_eq_int(cte, CW_SUCCESS, cwret3, "forever tone: silence the generator");

	return 0;
}




/**
   tests::cw_gen_get_timing_parameters_internal()
*/
int test_cw_gen_get_timing_parameters_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

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
	cte->expect_eq_int_errors_only(cte, false, failure, "get timing parameters:");

	cw_gen_delete(&gen);

	cte->print_test_footer(cte, __func__);

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
int test_cw_gen_parameter_getters_setters(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

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
		int cwret = CW_FAILURE;

		/* Test getting limits of values to be tested. */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);
		failure = (test_data[i].min <= -off_limits) || (test_data[i].max >= off_limits);
		cte->expect_eq_int_errors_only(cte, false, failure, "get %s limits:", test_data[i].name);


		/* Test setting out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].min - 1;
		cwret = test_data[i].set_new_value(gen, value);
		cte->expect_eq_int(cte, CW_FAILURE, cwret, "set %s below limit (cwret)", test_data[i].name);
		cte->expect_eq_int(cte, EINVAL, errno, "set %s below limit (errno)", test_data[i].name);


		/* Test setting out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].max + 1;
		cwret = test_data[i].set_new_value(gen, value);
		cte->expect_eq_int(cte, CW_FAILURE, cwret, "set %s above limit (cwret)", test_data[i].name);
		cte->expect_eq_int(cte, EINVAL, errno, "set %s above limit (errno)", test_data[i].name);



		/* Test setting in-range values. Set with setter and then read back with getter. */
		failure = false;
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			cwret = test_data[i].set_new_value(gen, j);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "set %s within limits:", test_data[i].name)) {
				failure = true;
				break;
			}

			const int readback_value = test_data[i].get_value(gen);
			if (!cte->expect_eq_int_errors_only(cte, readback_value, j, "readback %s within limits:", test_data[i].name)) {
				failure = true;
				break;
			}
		}

		cte->expect_eq_int(cte, false, failure, "set/get %s within limits:", test_data[i].name);
	}

	cw_gen_delete(&gen);

	cte->print_test_footer(cte, __func__);

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
int test_cw_gen_volume_functions(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	int cw_min = -1, cw_max = -1;

	cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
	cw_gen_start(gen);

	/* Test: get range of allowed volumes. */
	{
		cw_get_volume_limits(&cw_min, &cw_max);

		bool failure = cw_min != CW_VOLUME_MIN
			|| cw_max != CW_VOLUME_MAX;

		cte->expect_eq_int(cte, false, failure, "cw_get_volume_limits(): %d, %d", cw_min, cw_max);
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
			const int cwret = cw_gen_set_volume(gen, i);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_gen_set_volume(%d)", i)) {
				set_failure = true;
				break;
			}

			const int readback_value = cw_gen_get_volume(gen);
			if (!cte->expect_eq_int_errors_only(cte, readback_value, i, "cw_gen_get_volume() (i = %d)", i)) {
				get_failure = true;
				break;
			}

			cw_gen_wait_for_tone(gen);
		}

		cte->expect_eq_int(cte, false, set_failure, "cw_gen_set_volume() (down)");
		cte->expect_eq_int(cte, false, get_failure, "cw_gen_get_volume() (down)");
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
			const int cwret = cw_gen_set_volume(gen, i);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_gen_set_volume(%d)", i)) {
				set_failure = true;
				break;
			}

			const int readback_value = cw_gen_get_volume(gen);
			if (!cte->expect_eq_int_errors_only(cte, readback_value, i, "cw_gen_get_volume() (vol = %d)", i)) {
				get_failure = true;
				break;
			}
			cw_gen_wait_for_tone(gen);
		}

		cte->expect_eq_int(cte, false, set_failure, "cw_gen_set_volume() (up)");
		cte->expect_eq_int(cte, false, get_failure, "cw_gen_get_volume() (up)");
	}

	cw_gen_wait_for_tone(gen);
	cw_tq_flush_internal(gen->tq);

	cw_gen_delete(&gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   \brief Test enqueueing and playing most basic elements of Morse code

   tests::cw_gen_enqueue_mark_internal()
   tests::cw_gen_enqueue_eoc_space_internal()
   tests::cw_gen_enqueue_eow_space_internal()
*/
int test_cw_gen_enqueue_primitives(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	int N = 20;

	cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
	cw_gen_start(gen);

	/* Test: sending dot. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			const int cwret = cw_gen_enqueue_mark_internal(gen, CW_DOT_REPRESENTATION, false);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_gen_enqueue_mark_internal(CW_DOT_REPRESENTATION) (i = %d)", i)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		cte->expect_eq_int(cte, false, failure, "cw_gen_enqueue_mark_internal(CW_DOT_REPRESENTATION)");
	}



	/* Test: sending dash. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			const int cwret = cw_gen_enqueue_mark_internal(gen, CW_DASH_REPRESENTATION, false);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_gen_enqueue_mark_internal(CW_DASH_REPRESENTATION) (i = %d)", i)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		cte->expect_eq_int(cte, false, failure, "cw_gen_enqueue_mark_internal(CW_DASH_REPRESENTATION)");
	}


	/* Test: sending inter-character space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			const int cwret = cw_gen_enqueue_eoc_space_internal(gen);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_gen_enqueue_eoc_space_internal() (i = %d)", i)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		cte->expect_eq_int(cte, false, failure, "cw_gen_enqueue_eoc_space_internal()");
	}



	/* Test: sending inter-word space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			const int cwret = cw_gen_enqueue_eow_space_internal(gen);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_gen_enqueue_eow_space_internal() (i = %d)", i)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		cte->expect_eq_int(cte, false, failure, "cw_gen_enqueue_eow_space_internal()");
	}

	cw_gen_delete(&gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   \brief Test playing representations of characters

   tests::cw_gen_enqueue_representation_partial_internal()
*/
int test_cw_gen_enqueue_representations(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

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

		cte->expect_eq_int(cte, false, failure, "cw_gen_enqueue_representation_partial_internal(<valid>)");
	}


	/* Test: sending invalid representations. */
	{
		bool failure = (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "INVALID"))
			|| (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "_._T"))
			|| (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "_.A_."))
			|| (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "S-_-"));

		cte->expect_eq_int(cte, false, failure, "cw_gen_enqueue_representation_partial_internal(<invalid>)");
	}

	cw_gen_wait_for_tone(gen);

	struct timespec req = { .tv_sec = 3, .tv_nsec = 0 };
	cw_nanosleep_internal(&req);

	cw_gen_delete(&gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   Send all supported characters: first as individual characters, and then as a string.

   tests::cw_gen_enqueue_character()
   tests::cw_gen_enqueue_string()
*/
int test_cw_gen_enqueue_character_and_string(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

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
			const int cwret = cw_gen_enqueue_character(gen, charlist[i]);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "cw_gen_enqueue_character() (i = %d)", i)) {
				failure = true;
				break;
			}
			cw_gen_wait_for_queue_level(gen, 0);
		}

		fprintf(out_file, "\n");

		cte->expect_eq_int(cte, false, failure, "cw_gen_enqueue_character(<valid>)");
	}



	/* Test: sending invalid character. */
	{
		const int cwret = cw_gen_enqueue_character(gen, 0);
		cte->expect_eq_int(cte, CW_FAILURE, cwret, "cw_gen_enqueue_character(<invalid>):");
	}



	/* Test: sending all supported characters as single string. */
	{
		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);

		/* Send the complete charlist as a single string. */
		fprintf(out_file, MSG_PREFIX "cw_gen_enqueue_string(<valid>):\n"
			MSG_PREFIX "    %s\n", charlist);
		const int enqueue_cwret = cw_gen_enqueue_string(gen, charlist);


		while (cw_gen_get_queue_length(gen) > 0) {
			fprintf(out_file, MSG_PREFIX "tone queue length %-6zu\r", cw_gen_get_queue_length(gen));
			fflush(out_file);
			cw_gen_wait_for_tone(gen);
		}
		fprintf(out_file, "libcw:gen tone queue length %-6zu\n", cw_gen_get_queue_length(gen));
		cw_gen_wait_for_queue_level(gen, 0);

		cte->expect_eq_int(cte, CW_SUCCESS, enqueue_cwret, "cw_gen_enqueue_string(<valid>)");
	}


	/* Test: sending invalid string. */
	{
		const int cwret = cw_gen_enqueue_string(gen, "%INVALID%");
		cte->expect_eq_int(cte, CW_FAILURE, cwret, "cw_gen_enqueue_string(<invalid>):");
	}

	cw_gen_delete(&gen);

	cte->print_test_footer(cte, __func__);

	return 0;
}
