#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>




#include "tests/libcw_test_framework.h"
#include "libcw_rec.h"
#include "libcw_rec_tests.h"
#include "libcw_rec_internal.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_tq.h"
#include "libcw_key.h"
#include "libcw.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/rec: "




#define TEST_CW_REC_DATA_LEN_MAX 30 /* There is no character that would have that many time points corresponding to a representation. */
struct cw_rec_test_data {
	char c;                               /* Character. */
	char * representation;                /* Character's representation (dots and dashes). */
	int times[TEST_CW_REC_DATA_LEN_MAX];  /* Character's representation's times - time information for marks and spaces. */
	int n_times;                          /* Number of data points encoding given representation of given character. */
	float speed;                          /* Send speed (speed at which the character is incoming). */

	bool is_last_in_word;                 /* Is this character a last character in a word? (is it followed by end-of-word space?) */
};




static struct cw_rec_test_data * test_cw_rec_generate_data(const char * characters, float speeds[], int fuzz_percent);
static struct cw_rec_test_data * test_cw_rec_generate_base_data_constant(int speed, int fuzz_percent);
static struct cw_rec_test_data * test_cw_rec_generate_data_random_constant(int speed, int fuzz_percent);
static struct cw_rec_test_data * test_cw_rec_generate_data_random_varying(int speed_min, int speed_max, int fuzz_percent);

static void test_cw_rec_delete_data(struct cw_rec_test_data ** data);
__attribute__((unused)) static void test_cw_rec_print_data(struct cw_rec_test_data * data);
static bool test_cw_rec_test_begin_end(cw_test_executor_t * cte, cw_rec_t * rec, struct cw_rec_test_data * data);

/* Functions creating tables of test values: characters and speeds.
   Characters and speeds will be combined into test (timing) data. */
static char  * test_cw_rec_new_base_characters(void);
static char  * test_cw_rec_generate_characters_random(int n);
static float * test_cw_rec_generate_speeds_constant(int speed, size_t n);
static float * test_cw_rec_generate_speeds_varying(int speed_min, int speed_max, size_t n);




/**
   tests::cw_rec_identify_mark_internal()

   Test if function correctly recognizes dots and dashes for a range
   of receive speeds.  This test function also checks if marks of
   lengths longer or shorter than certain limits (dictated by
   receiver) are handled properly (i.e. if they are recognized as
   invalid marks).

   Currently the function only works for non-adaptive receiving.
*/
int test_cw_rec_identify_mark_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

#if 0
	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, MSG_PREFIX "identify mark: failed to create new receiver\n");
	cw_rec_disable_adaptive_mode(rec);

	int speed_step = (CW_SPEED_MAX - CW_SPEED_MIN) / 10;

	for (int speed = CW_SPEED_MIN; speed < CW_SPEED_MAX; speed += speed_step) {
		int rv = cw_rec_set_speed(rec, speed);
		cw_assert (rv, MSG_PREFIX "identify mark @ %02d [wpm]: failed to set receive speed\n", speed);


		bool failure = true;
		int n = 0;
		char representation;




		/* Test marks that have length appropriate for a dot. */
		int len_step = (rec->dot_len_max - rec->dot_len_min) / 10;
		for (int len = rec->dot_len_min; len < rec->dot_len_max; len += len_step) {
			const int cwret = cw_rec_identify_mark_internal(rec, len, &representation);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "identify mark @ %02d [wpm]: failed to identify dot for len = %d [us]\n", speed, len)) {
				failure = true;
				break;
			}
			if (!cte->expect_eq_int_errors_only(cte, CW_DOT_REPRESENTATION, representation, "identify mark @ %02d [wpm]: failed to get dot representation for len = %d [us]\n", speed, len)) {
				failure = true;
				break;
			}
		}

		cte->expect_eq_int_errors_only(cte, false, failure, "identify mark @ %02d [wpm]: identify valid dot:", speed);


#if 0
		/* Test mark shorter than minimal length of dot. */
		cwret = cw_rec_identify_mark_internal(rec, rec->dot_len_min - 1, &representation);
		cte->expect_eq_int_errors_only(cte, CW_FAILURE, cwret, "identify mark @ %02d [wpm]: mark shorter than min dot:", speed);

		/* Test mark longer than maximal length of dot (but shorter than minimal length of dash). */
		cwret = cw_rec_identify_mark_internal(rec, rec->dot_len_max + 1, &representation);
		cte->expect_eq_int_errors_only(cte, CW_FAILURE, cwret, "identify mark @ %02d [wpm]: mark longer than max dot:", speed);


		/* Test marks that have length appropriate for a dash. */
		len_step = (rec->dash_len_max - rec->dash_len_min) / 10;
		for (int len = rec->dash_len_min; len < rec->dash_len_max; len += len_step) {
			cwret = cw_rec_identify_mark_internal(rec, len, &representation);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "identify mark @ %02d [wpm]: failed to identify dash for len = %d [us]\n", speed, len)) {
				failure = true;
				break;
			}

			if (!cte->expect_eq_int_errors_only(cte, CW_DASH_REPRESENTATION, representation, "identify mark @ %02d [wpm]: failed to get dash representation for len = %d [us]\n", speed, len)) {
				failure = true;
				break;
			}
		}
		cte->expect_eq_int_errors_only(cte, false, failure, "identify mark @ %02d [wpm]: identify valid dash:", speed);


		/* Test mark shorter than minimal length of dash (but longer than maximal length of dot). */
		cwret = = cw_rec_identify_mark_internal(rec, rec->dash_len_min - 1, &representation);
		cte->expect_eq_int_errors_only(cte, CW_FAILURE, cwret, "identify mark @ %02d [wpm]: mark shorter than min dash:", speed);



		/* Test mark longer than maximal length of dash. */
		cwret = cw_rec_identify_mark_internal(rec, rec->dash_len_max + 1, &representation);
		cte->expect_eq_int_errors_only(cte, CW_FAILURE, cwret, "identify mark @ %02d [wpm]: mark longer than max dash:", speed);
#endif
	}

	cw_rec_delete(&rec);
#endif

	cte->print_test_footer(cte, __func__);

	return 0;
}




/*
  Test a receiver with data set that has following characteristics:

  Characters: base (all characters supported by libcw, occurring only once in the data set, in ordered fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to test receiver with test data set guaranteed to contain all characters supported by libcw.
*/
int test_cw_rec_test_with_base_constant(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, MSG_PREFIX "begin/end: base/constant: failed to create new receiver\n");


	for (int speed = CW_SPEED_MIN; speed <= CW_SPEED_MAX; speed++) {
		struct cw_rec_test_data * data = test_cw_rec_generate_base_data_constant(speed, 0);
		//test_cw_rec_print_data(data);

		/* Reset. */
		cw_rec_reset_statistics(rec);
		cw_rec_reset_state(rec);

		cw_rec_set_speed(rec, speed);
		cw_rec_disable_adaptive_mode(rec);

		/* Make sure that the test speed has been set correctly. */
		float diff = cw_rec_get_speed(rec) - (float) speed;
		cw_assert (diff < 0.1, MSG_PREFIX "begin/end: base/constant: %f != %f\n",  cw_rec_get_speed(rec), (float) speed);
		// cte->expect_eq_int_errors_only(cte, ); // TODO: implement

		/* Actual tests of receiver functions are here. */
		bool failure = test_cw_rec_test_begin_end(cte, rec, data);
		cte->expect_eq_int_errors_only(cte, false, failure, "begin/end: base/constant @ %02d [wpm]:", speed);

		test_cw_rec_delete_data(&data);
	}

	cw_rec_delete(&rec);

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   \brief The core test function, testing receiver's "begin" and "end" functions

   As mentioned in file's top-level comment, there are two main
   methods to add data to receiver. This function tests first method:
   using cw_rec_mark_begin() and cw_rec_mark_end().

   Other helper functions are used/tested here as well, because adding
   marks and spaces to receiver is just half of the job necessary to
   receive Morse code. You have to interpret the marks and spaces,
   too.

   \param rec - receiver variable used during tests
   \param data - table with timings, used to test the receiver
*/
bool test_cw_rec_test_begin_end(cw_test_executor_t * cte, cw_rec_t * rec, struct cw_rec_test_data * data)
{
	struct timeval tv = { 0, 0 };

	bool begin_end_failure = false;

	bool buffer_length_failure = false;

	bool poll_representation_failure = false;
	bool match_representation_failure = false;
	bool error_representation_failure = false;
	bool word_representation_failure = false;

	bool poll_character_failure = false;
	bool match_character_failure = false;
	bool empty_failure = false;

	for (int i = 0; data[i].representation; i++) {

#ifdef LIBCW_UNIT_TESTS_VERBOSE
		printf("\n" MSG_PREFIX "begin/end: input test data #%d: <%c> / <%s> @ %.2f [wpm] (%d time values)\n",
		       i, data[i].c, data[i].r, data[i].s, data[i].n_times);
#endif

#if 0 /* Should we remove it? */
		/* Start sending every character at the beginning of a
		   new second.

		   TODO: here we make an assumption that every
		   character is sent in less than a second. Which is a
		   good assumption when we have a speed of tens of
		   WPM. If the speed will be lower, the assumption
		   will be false. */
		tv.tv_sec = 0;
		tv.tv_usec = 0;
#endif

		/* This loop simulates "key down" and "key up" events
		   in specific moments, and in specific time
		   intervals.

		   key down -> call to cw_rec_mark_begin()
		   key up -> call to cw_rec_mark_end().

		   First "key down" event is at X seconds Y
		   microseconds (see initialization of 'tv'). Time of
		   every following event is calculated by iterating
		   over tone lengths specified in data table. */
		int tone;
		for (tone = 0; data[i].times[tone] > 0; tone++) {
			begin_end_failure = false;

			if (tone % 2) {
				const int cwret = cw_rec_mark_end(rec, &tv);
				if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "begin/end: cw_rec_mark_end(): tone = %d, time = %d.%d\n", tone, (int) tv.tv_sec, (int) tv.tv_usec)) {
					begin_end_failure = true;
					break;
				}
			} else {
				const int cwret = cw_rec_mark_begin(rec, &tv);
				if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "begin/end: cw_rec_mark_begin(): tone = %d, time = %d.%d\n", tone, (int) tv.tv_sec, (int) tv.tv_usec)) {
					begin_end_failure = true;
					break;
				}
			}

			tv.tv_usec += data[i].times[tone];
			if (tv.tv_usec >= CW_USECS_PER_SEC) {
				/* Moving event to next second. */
				tv.tv_sec += tv.tv_usec / CW_USECS_PER_SEC;
				tv.tv_usec %= CW_USECS_PER_SEC;

			}
			/* If we exit the loop at this point, the last
			   'tv' with length of end-of-character space
			   will be used below in
			   cw_rec_poll_representation(). */
		}

		cw_assert (tone, MSG_PREFIX "begin/end executed zero times\n");
		if (begin_end_failure) {
			break;
		}




		/* Test: length of receiver's buffer (only marks!)
		   after adding a representation of a single character
		   to receiver's buffer. */
		{
			int n = cw_rec_get_buffer_length_internal(rec);
			buffer_length_failure = (n != (int) strlen(data[i].representation));

			if (!cte->expect_eq_int_errors_only(cte, false, buffer_length_failure, "begin/end: cw_rec_get_buffer_length_internal(<nonempty>): %d != %zd\n", n, strlen(data[i].representation))) {
				buffer_length_failure = true;
				break;
			}
		}




		/* Test: getting representation from receiver's buffer. */
		char representation[CW_REC_REPRESENTATION_CAPACITY + 1];
		{
			/* Get representation (dots and dashes)
			   accumulated by receiver. Check for
			   errors. */

			bool is_word, is_error;

			/* Notice that we call the function with last
			   timestamp (tv) from input data. The last
			   timestamp in the input data represents end
			   of final end-of-character space.

			   With this final passing of "end of space"
			   timestamp to libcw the test code informs
			   receiver, that end-of-character space has
			   occurred, i.e. a full character has been
			   passed to receiver.

			   The space length in input data is (3 x dot
			   + jitter). In libcw maximum recognizable
			   length of "end of character" space is 5 x
			   dot. */
			int cwret = cw_rec_poll_representation(rec, &tv, representation, &is_word, &is_error);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "begin/end: poll representation returns !CW_SUCCESS")) {
				poll_representation_failure = true;
				fprintf(out_file, MSG_PREFIX );
				break;
			}

			const int strcmp_result = strcmp(representation, data[i].representation);
			if (!cte->expect_eq_int_errors_only(cte, 0, strcmp_result, "being/end: polled representation does not match test representation: \"%s\" != \"%s\"\n", representation, data[i].representation)) {
				match_representation_failure = true;
				break;
			}

			if (!cte->expect_eq_int_errors_only(cte, false, is_error, "begin/end: poll representation sets is_error\n")) {
				error_representation_failure = true;
				fprintf(out_file, MSG_PREFIX );
				break;
			}



			/* If the last space in character's data is
			   end-of-word space (which is indicated by
			   is_last_in_word), then is_word should be
			   set by poll() to true. Otherwise both
			   values should be false. */
			word_representation_failure = (is_word != data[i].is_last_in_word);
			// cte->expect_eq_int_errors_only(cte, ); // TODO: implement
			if (word_representation_failure) {
				fprintf(out_file, MSG_PREFIX "begin/end: poll representation: 'is_word' flag error: function returns '%d', data is tagged with '%d'\n" \
					"'%c'  '%c'  '%c'  '%c'  '%c'",
					is_word, data[i].is_last_in_word,
					data[i - 2].c, data[i - 1].c, data[i].c, data[i + 1].c, data[i + 2].c );
				break;
			}

#if 0
			/* Debug code. Print times of character with
			   end-of-word space to verify length of the
			   space. */
			if (data[i].is_last_in_word) {
				fprintf(stderr, "------- character '%c' is last in word\n", data[i].c);
				for (int m = 0; m < data[i].n_times; m++) {
					fprintf(stderr, "#%d: %d\n", m, data[i].d[m]);
				}
			}
#endif

		}




		char c;
		/* Test: getting character from receiver's buffer. */
		{
			bool is_word, is_error;

			/* The representation is still held in
			   receiver. Ask receiver for converting the
			   representation to character. */
			const int cwret = cw_rec_poll_character(rec, &tv, &c, &is_word, &is_error);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "begin/end: poll character false\n")) {
				poll_character_failure = true;
				break;
			}
			if (!cte->expect_eq_int_errors_only(cte, data[i].c, c, "begin/end: polled character does not match test character: '%c' != '%c'\n", c, data[i].c)) {
				match_character_failure = true;
				break;
			}
		}




		/* Test: getting length of receiver's representation
		   buffer after cleaning the buffer. */
		{
			/* We have a copy of received representation,
			   we have a copy of character. The receiver
			   no longer needs to store the
			   representation. If I understand this
			   correctly, the call to reset_state() is necessary
			   to prepare the receiver for receiving next
			   character. */
			cw_rec_reset_state(rec);
			const int length = cw_rec_get_buffer_length_internal(rec);
			if (!cte->expect_eq_int_errors_only(cte, 0, length, "begin/end: get buffer length: length of cleared buffer is non zero (is %d)", length)) {
				empty_failure = true;
				break;
			}
		}


#ifdef LIBCW_UNIT_TESTS_VERBOSE
		float speed = cw_rec_get_speed(rec);
		printf("libcw: received data #%d:   <%c> / <%s> @ %.2f [wpm]\n",
		       i, c, representation, speed);
#endif

#if 0
		if (adaptive) {
			printf("libcw: adaptive speed tracking reports %f [wpm]\n",  );
		}
#endif

	}

	return begin_end_failure
		|| buffer_length_failure
		|| poll_representation_failure || match_representation_failure || error_representation_failure || word_representation_failure
		|| poll_character_failure || match_character_failure || empty_failure;
}




/*
  Generate small test data set.

  Characters: base (all characters supported by libcw, occurring only once in the data set, in ordered fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to generate a data set guaranteed to contain all characters supported by libcw.
*/
struct cw_rec_test_data * test_cw_rec_generate_base_data_constant(int speed, int fuzz_percent)
{
	/* All characters supported by libcw.  Don't use
	   get_characters_random(): for this test get a small table of
	   all characters supported by libcw. This should be a quick
	   test, and it should cover all characters. */
	char * base_characters = test_cw_rec_new_base_characters();
	cw_assert (base_characters, MSG_PREFIX "new base data fixed: test_cw_rec_new_base_characters() failed\n");


	size_t n = strlen(base_characters);


	/* Fixed speed receive mode - speed is constant for all
	   characters. */
	float * speeds = test_cw_rec_generate_speeds_constant(speed, n);
	cw_assert (speeds, MSG_PREFIX "new base data fixed: test_cw_rec_generate_speeds_constant() failed\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement


	/* Generate timing data for given set of characters, each
	   character is sent with speed dictated by speeds[]. */
	struct cw_rec_test_data * data = test_cw_rec_generate_data(base_characters, speeds, fuzz_percent);
	cw_assert (data, MSG_PREFIX "failed to generate base/fixed test data\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement


	free(base_characters);
	base_characters = NULL;

	free(speeds);
	speeds = NULL;

	return data;
}





/*
  Test a receiver with data set that has following characteristics:

  Characters: random (all characters supported by libcw + inter-word space, occurring once or more in the data set, in random fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to test receiver with very large test data set.
*/
int test_cw_rec_test_with_random_constant(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, MSG_PREFIX "begin/end: random/constant: failed to create new receiver\n");


	for (int speed = CW_SPEED_MIN; speed <= CW_SPEED_MAX; speed++) {
		struct cw_rec_test_data * data = test_cw_rec_generate_data_random_constant(speed, 0);
		//test_cw_rec_print_data(data);

		/* Reset. */
		cw_rec_reset_statistics(rec);
		cw_rec_reset_state(rec);

		cw_rec_set_speed(rec, speed);
		cw_rec_disable_adaptive_mode(rec);

		/* Verify that test speed has been set correctly. */
		float diff = cw_rec_get_speed(rec) - speed;
		cw_assert (diff < 0.1, MSG_PREFIX "begin/end: random/constant: incorrect receive speed: %f != %f\n", cw_rec_get_speed(rec), (float) speed);
		// cte->expect_eq_int_errors_only(cte, );  // TODO: implement

		/* Actual tests of receiver functions are here. */
		bool failure = test_cw_rec_test_begin_end(cte, rec, data);
		cte->expect_eq_int_errors_only(cte, false, failure, "begin/end: random/constant @ %02d [wpm]:", speed);

		test_cw_rec_delete_data(&data);
	}

	cw_rec_delete(&rec);

	cte->print_test_footer(cte, __func__);

	return 0;
}





/*
  Test a receiver with data set that has following characteristics:

  Characters: random (all characters supported by libcw + inter-word space, occurring once or more in the data set, in random fashion).
  Send speeds: varying (each character will be sent to receiver at different speed).

  This function is used to test receiver with very large test data set.
*/
int test_cw_rec_test_with_random_varying(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	struct cw_rec_test_data * data = test_cw_rec_generate_data_random_varying(CW_SPEED_MIN, CW_SPEED_MAX, 0);
	//test_cw_rec_print_data(data);

	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, MSG_PREFIX "begin/end: random/varying: failed to create new receiver\n");

	/* Reset. */
	cw_rec_reset_statistics(rec);
	cw_rec_reset_state(rec);

	cw_rec_set_speed(rec, CW_SPEED_MAX);
	cw_rec_enable_adaptive_mode(rec);

	/* Verify that initial test speed has been set correctly. */
	float diff = cw_rec_get_speed(rec) - CW_SPEED_MAX;
	cw_assert (diff < 0.1, MSG_PREFIX "begin/end: random/varying: incorrect receive speed: %f != %f\n", cw_rec_get_speed(rec), (float) CW_SPEED_MAX);
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement

	/* Actual tests of receiver functions are here. */
	bool failure = test_cw_rec_test_begin_end(cte, rec, data);
	cte->expect_eq_int(cte, false, failure, "begin/end: random/varying:");

	test_cw_rec_delete_data(&data);

	cw_rec_delete(&rec);

	cte->print_test_footer(cte, __func__);

	return 0;
}





/*
  Generate large test data set.

  Characters: random (all characters supported by libcw + inter-word space, occurring once or more in the data set, in random fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to generate a large test data set.
*/
struct cw_rec_test_data * test_cw_rec_generate_data_random_constant(int speed, int fuzz_percent)
{
	const int n = cw_get_character_count() * 30;

	char * characters = test_cw_rec_generate_characters_random(n);
	cw_assert (characters, MSG_PREFIX "test_cw_rec_generate_characters_random() failed\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement

	/* Fixed speed receive mode - speed is constant for all characters. */
	float * speeds = test_cw_rec_generate_speeds_constant(speed, n);
	cw_assert (speeds, MSG_PREFIX "test_cw_rec_generate_speeds_constant() failed\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement


	/* Generate timing data for given set of characters, each
	   character is sent with speed dictated by speeds[]. */
	struct cw_rec_test_data * data = test_cw_rec_generate_data(characters, speeds, fuzz_percent);
	cw_assert (data, MSG_PREFIX "random/constant: failed to generate test data\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement


	free(characters);
	characters = NULL;

	free(speeds);
	speeds = NULL;

	return data;
}





/*
  Generate large test data set.

  Characters: random (all characters supported by libcw + inter-word space, occurring once or more in the data set, in random fashion).
  Send speeds: constant (each character will be sent to receiver at the same, constant speed).

  This function is used to generate a large test data set.
*/
struct cw_rec_test_data * test_cw_rec_generate_data_random_varying(int speed_min, int speed_max, int fuzz_percent)
{
	int n = cw_get_character_count() * 30;

	char *characters = test_cw_rec_generate_characters_random(n);
	cw_assert (characters, MSG_PREFIX "begin/end: test_cw_rec_generate_characters_random() failed\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement

	/* Adaptive speed receive mode - speed varies for all
	   characters. */
	float * speeds = test_cw_rec_generate_speeds_varying(speed_min, speed_max, n);
	cw_assert (speeds, MSG_PREFIX "test_cw_rec_generate_speeds_varying() failed\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement


	/* Generate timing data for given set of characters, each
	   character is sent with speed dictated by speeds[]. */
	struct cw_rec_test_data *data = test_cw_rec_generate_data(characters, speeds, fuzz_percent);
	cw_assert (data, MSG_PREFIX "failed to generate random/varying test data\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement


	free(characters);
	characters = NULL;

	free(speeds);
	speeds = NULL;

	return data;
}





/**
   \brief Get a string with all characters supported by libcw

   Function allocates and returns a string with all characters that are supported/recognized by libcw.

   \return allocated string.
*/
char * test_cw_rec_new_base_characters(void)
{
	int n = cw_get_character_count();
	char * base_characters = (char *) malloc((n + 1) * sizeof (char));
	cw_assert (base_characters, MSG_PREFIX "get base characters: malloc() failed\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement
	cw_list_characters(base_characters);

	return base_characters;
}





/**
   \brief Generate a set of characters of size \p n.

   Function allocates and returns a string of \p n characters. The
   characters are randomly drawn from set of all characters supported
   by libcw.

   Spaces are added to the string in random places to mimic a regular
   text. Function makes sure that there are no consecutive spaces (two
   or more) in the string.

   \param n - number of characters in output string

   \return string of random characters (including spaces)
*/
char * test_cw_rec_generate_characters_random(int n)
{
	/* All characters supported by libcw - this will be an input
	   set of all characters. */
	char * base_characters = test_cw_rec_new_base_characters();
	cw_assert (base_characters, MSG_PREFIX "test_cw_rec_new_base_characters() failed\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement
	size_t length = strlen(base_characters);


	char * characters = (char *) malloc ((n + 1) * sizeof (char));
	cw_assert (characters, MSG_PREFIX "malloc() failed\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement
	for (int i = 0; i < n; i++) {
		int r = rand() % length;
		if (!(r % 3)) {
			characters[i] = ' ';

			/* To prevent two consecutive spaces. */
			i++;
			characters[i] = base_characters[r];
		} else {
			characters[i] = base_characters[r];
		}
	}

	/* First character in input data can't be a space - we can't
	   start a receiver's state machine with space. Also when a
	   end-of-word space appears in input character set, it is
	   added as last time value at the end of time values table
	   for "previous char". We couldn't do this for -1st char. */
	characters[0] = 'K'; /* Use capital letter. libcw uses capital letters internally. */

	characters[n] = '\0';

	//fprintf(stderr, "%s\n", characters);


	free(base_characters);
	base_characters = NULL;


	return characters;
}





/**
   \brief Generate a table of constant speeds

   Function allocates and returns a table of speeds of constant value
   specified by \p speed. There are \p n valid (non-negative) values
   in the table. After the last valid value there is a small negative
   value at position 'n' in the table that acts as a guard.

   \param speed - a constant value to be used as initializer of the table
   \param n - size of table (function allocates additional one cell for guard)

   \return table of speeds of constant value
*/
float * test_cw_rec_generate_speeds_constant(int speed, size_t n)
{
	cw_assert (speed > 0, MSG_PREFIX "generate speeds constant: speed must be larger than zero\n");

	float * speeds = (float *) malloc((n + 1) * sizeof (float));
	cw_assert (speeds, MSG_PREFIX "generate speeds constant: malloc() failed\n");

	for (size_t i = 0; i < n; i++) {
		/* Fixed speed receive mode - speed values are constant for
		   all characters. */
		speeds[i] = (float) speed;
	}

	speeds[n] = -1.0;

	return speeds;
}





/**
   \brief Generate a table of varying speeds

   Function allocates and returns a table of speeds of varying values,
   changing between \p speed_min and \p speed_max. There are \p n
   valid (non-negative) values in the table. After the last valid
   value there is a small negative value at position 'n' in the table
   that acts as a guard.

   \param speed_min - minimal speed
   \param speed_max - maximal speed
   \param n - size of table (function allocates additional one cell for guard)

   \return table of speeds
*/
float * test_cw_rec_generate_speeds_varying(int speed_min, int speed_max, size_t n)
{
	cw_assert (speed_min > 0, MSG_PREFIX "generate speeds varying: speed_min must be larger than zero\n");
	cw_assert (speed_max > 0, MSG_PREFIX "generate speeds varying: speed_max must be larger than zero\n");
	cw_assert (speed_min <= speed_max, MSG_PREFIX "generate speeds varying: speed_min can't be larger than speed_max\n");

	float * speeds = (float *) malloc((n + 1) * sizeof (float));
	cw_assert (speeds, MSG_PREFIX "generate speeds varying: malloc() failed\n");
	// cte->expect_eq_int_errors_only(cte, ); // TODO: implement

	for (size_t i = 0; i < n; i++) {

		/* Adaptive speed receive mode - speed varies for all
		   characters. */

		float t = (1.0 * i) / n;

		speeds[i] = (1 + cosf(2 * 3.1415 * t)) / 2.0; /* 0.0 -  1.0 */
		speeds[i] *= (speed_max - speed_min);         /* 0.0 - 56.0 */
		speeds[i] += speed_min;                       /* 4.0 - 60.0 */

		// fprintf(stderr, "%f\n", speeds[i]);
	}

	speeds[n] = -1.0;

	return speeds;
}




/**
   \brief Create timing data used for testing a receiver

   This is a generic function that can generate different sets of data
   depending on input parameters. It is to be used by wrapper
   functions that first specify parameters of test data, and then pass
   the parameters to this function.

   The function allocates a table with timing data (and some other
   data as well) that can be used to test receiver's functions that
   accept timestamp argument.

   All characters in \p characters must be valid (i.e. they must be
   accepted by cw_character_is_valid()).

   All values in \p speeds must be valid (i.e. must be between
   CW_SPEED_MIN and CW_SPEED_MAX, inclusive).

   Size of \p characters and \p speeds must be equal.

   The data is valid and represents valid Morse representations.  If
   you want to generate invalid data or to generate data based on
   invalid representations, you have to use some other function.

   For each character the last timing parameter represents
   end-of-character space or end-of-word space. The next timing
   parameter after the space is zero. For character 'A' that would
   look like this:

   .-    ==   40000 (dot mark); 40000 (inter-mark space); 120000 (dash mark); 240000 (end-of-word space); 0 (guard, zero timing)

   Last element in the created table (a guard "pseudo-character") has
   'representation' field set to NULL.

   Use test_cw_rec_delete_data() to deallocate the timing data table.

   \brief characters - list of characters for which to generate table with timing data
   \brief speeds - list of speeds (per-character)

   \return table of timing data sets
*/
struct cw_rec_test_data * test_cw_rec_generate_data(char const * characters, float speeds[], __attribute__((unused)) int fuzz_percent)
{
	size_t n = strlen(characters);
	/* +1 for guard. */
	struct cw_rec_test_data * test_data = (struct cw_rec_test_data *) malloc((n + 1) * sizeof(struct cw_rec_test_data));
	cw_assert (test_data, MSG_PREFIX "generate data: malloc() failed\n");
	//// cte->expect_eq_int_errors_only(cte, ); // TODO: implement

	/* Initialization. */
	for (size_t i = 0; i < n + 1; i++) {
		test_data[i].representation = (char *) NULL;
	}

	size_t out = 0; /* For indexing output data table. */
	for (size_t in = 0; in < n; in++) {

		int unit_len = CW_DOT_CALIBRATION / speeds[in]; /* Dot length, [us]. Used as basis for other elements. */
		// fprintf(stderr, "unit_len = %d [us] for speed = %d [wpm]\n", unit_len, speed);


		/* First handle a special case: end-of-word
		   space. This long space will be put at the end of
		   table of time values for previous
		   representation. */
		if (characters[in] == ' ') {
			/* We don't want to affect current output
			   character, we want to turn end-of-char
			   space of previous character into
			   end-of-word space, hence 'out - 1'. */
			int space_i = test_data[out - 1].n_times - 1;    /* Index of last space (end-of-char, to become end-of-word). */
			test_data[out - 1].times[space_i] = unit_len * 6; /* unit_len * 5 is the minimal end-of-word space. */

			test_data[out - 1].is_last_in_word = true;

			continue;
		} else {
			/* A regular character, handled below. */
		}


		test_data[out].c = characters[in];
		test_data[out].representation = cw_character_to_representation(test_data[out].c);
		cw_assert (test_data[out].representation,
			   MSG_PREFIX "generate data: cw_character_to_representation() failed for input char #%zu: '%c'\n",
			   in, characters[in]);
		//// cte->expect_eq_int_errors_only(cte, ); // TODO: implement
		test_data[out].speed = speeds[in];


		/* Build table of times (data points) 'd[]' for given
		   representation 'r'. */


		size_t n_times = 0; /* Number of data points in data table. */

		size_t rep_length = strlen(test_data[out].representation);
		for (size_t k = 0; k < rep_length; k++) {

			/* Length of mark. */
			if (test_data[out].representation[k] == CW_DOT_REPRESENTATION) {
				test_data[out].times[n_times] = unit_len;

			} else if (test_data[out].representation[k] == CW_DASH_REPRESENTATION) {
				test_data[out].times[n_times] = unit_len * 3;

			} else {
				cw_assert (0, MSG_PREFIX "generate data: unknown char in representation: '%c'\n", test_data[out].representation[k]);
			}
			n_times++;


			/* Length of space (inter-mark space). Mark
			   and space always go in pair. */
			test_data[out].times[n_times] = unit_len;
			n_times++;
		}

		/* Every character has non-zero marks and spaces. */
		cw_assert (n_times > 0, MSG_PREFIX "generate data: number of data points is %zu for representation '%s'\n", n_times, test_data[out].representation);

		/* Mark and space always go in pair, so nd should be even. */
		cw_assert (! (n_times % 2), MSG_PREFIX "generate data: number of times is not even\n");

		/* Mark/space pair per each dot or dash. */
		cw_assert (n_times == 2 * rep_length, MSG_PREFIX "generate data: number of times incorrect: %zu != 2 * %zu\n", n_times, rep_length);


		/* Graduate that last space (inter-mark space) into
		   end-of-character space. */
		test_data[out].times[n_times - 1] = (unit_len * 3) + (unit_len / 2);

		/* Guard. */
		test_data[out].times[n_times] = 0;

		test_data[out].n_times = n_times;

		/* This may be overwritten by this function when a
		   space character (' ') is encountered in input
		   string. */
		test_data[out].is_last_in_word = false;

		out++;
	}


	/* Guard. */
	test_data[n].representation = (char *) NULL;


	return test_data;
}





/**
   \brief Deallocate timing data used for testing a receiver

   \param data - pointer to data to be deallocated
*/
void test_cw_rec_delete_data(struct cw_rec_test_data ** data)
{
	int i = 0;
	while ((*data)[i].representation) {
		free((*data)[i].representation);
		(*data)[i].representation = (char *) NULL;

		i++;
	}

	free(*data);
	*data = NULL;

	return;
}





/**
   \brief Pretty-print timing data used for testing a receiver

   \param data timing data to be printed
*/
void test_cw_rec_print_data(struct cw_rec_test_data * data)
{
	int i = 0;

	fprintf(stderr, "---------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	while (data[i].representation) {
		/* Debug output. */
		if (!(i % 10)) {
			/* Print header. */
			fprintf(stderr, "char  repr      [wpm]    mark     space      mark     space      mark     space      mark     space      mark     space      mark     space      mark     space\n");
		}
		fprintf(stderr, "%c     %-7s  %02.2f", data[i].c, data[i].representation, data[i].speed);
		for (int j = 0; j < data[i].n_times; j++) {
			fprintf(stderr, "%9d ", data[i].times[j]);
		}
		fprintf(stderr, "\n");

		i++;
	}

	return;
}




int test_cw_rec_get_parameters(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	bool failure = true;

	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, MSG_PREFIX "get: failed to create new receiver\n");

	cw_rec_reset_parameters_internal(rec);
	cw_rec_sync_parameters_internal(rec);

	int dot_len_ideal = 0;
	int dash_len_ideal = 0;

	int dot_len_min = 0;
	int dot_len_max = 0;

	int dash_len_min = 0;
	int dash_len_max = 0;

	int eom_len_min = 0;
	int eom_len_max = 0;
	int eom_len_ideal = 0;

	int eoc_len_min = 0;
	int eoc_len_max = 0;
	int eoc_len_ideal = 0;

	int adaptive_speed_threshold = 0;

	cw_rec_get_parameters_internal(rec,
				       &dot_len_ideal, &dash_len_ideal, &dot_len_min, &dot_len_max, &dash_len_min, &dash_len_max,
				       &eom_len_min, &eom_len_max, &eom_len_ideal,
				       &eoc_len_min, &eoc_len_max, &eoc_len_ideal,
				       &adaptive_speed_threshold);

	cw_rec_delete(&rec);

	fprintf(out_file,
		MSG_PREFIX "get: dot/dash:  %d, %d, %d, %d, %d, %d\n" \
		MSG_PREFIX "get: eom:       %d, %d, %d\n" \
		MSG_PREFIX "get: eoc:       %d, %d, %d\n" \
		MSG_PREFIX "get: threshold: %d\n",

		dot_len_ideal, dash_len_ideal, dot_len_min, dot_len_max, dash_len_min, dash_len_max,
		eom_len_min, eom_len_max, eom_len_ideal,
		eoc_len_min, eoc_len_max, eoc_len_ideal,
		adaptive_speed_threshold);


	failure = (dot_len_ideal <= 0
		   || dash_len_ideal <= 0
		   || dot_len_min <= 0
		   || dot_len_max <= 0
		   || dash_len_min <= 0
		   || dash_len_max <= 0

		   || eom_len_min <= 0
		   || eom_len_max <= 0
		   || eom_len_ideal <= 0

		   || eoc_len_min <= 0
		   || eoc_len_max <= 0
		   || eoc_len_ideal <= 0

		   || adaptive_speed_threshold <= 0);
	cte->expect_eq_int_errors_only(cte, false, failure, "cw_rec_get_parameters_internal()");


	failure = dot_len_max >= dash_len_min;
	cte->expect_eq_int_errors_only(cte, false, failure, "get: max dot len < min dash len (%d/%d):", dot_len_max, dash_len_min);


	failure = (dot_len_min >= dot_len_ideal) || (dot_len_ideal >= dot_len_max);
	cte->expect_eq_int_errors_only(cte, false, failure, "get: dot len consistency (%d/%d/%d):", dot_len_min, dot_len_ideal, dot_len_max);


	failure = (dash_len_min >= dash_len_ideal) || (dash_len_ideal >= dash_len_max);
	cte->expect_eq_int_errors_only(cte, false, failure, "get: dash len consistency (%d/%d/%d):", dash_len_min, dash_len_ideal, dash_len_max);


	failure = (eom_len_max >= eoc_len_min);
	cte->expect_eq_int_errors_only(cte, false, failure, "get: max eom len < min eoc len (%d/%d):", eom_len_max, eoc_len_min);


	failure = (eom_len_min >= eom_len_ideal) || (eom_len_ideal >= eom_len_max);
	cte->expect_eq_int_errors_only(cte, false, failure, "get: eom len consistency (%d/%d/%d)", eom_len_min, eom_len_ideal, eom_len_max);


	failure = (eoc_len_min >= eoc_len_ideal) || (eoc_len_ideal >= eoc_len_max);
	cte->expect_eq_int_errors_only(cte, false, failure, "get: eoc len consistency (%d/%d/%d)", eoc_len_min, eoc_len_ideal, eoc_len_max);

	cte->print_test_footer(cte, __func__);

	return 0;
}





/* Parameter getters and setters are independent of audio system, so
   they can be tested just with CW_AUDIO_NULL.  This is even more true
   for limit getters, which don't require a receiver at all. */
int test_cw_rec_parameter_getters_setters_1(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, MSG_PREFIX "get/set param 1: failed to create new receiver\n");

	/* Test setting and getting of some basic parameters. */

	int off_limits = 10000;

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int *min, int *max);
		int (* set_new_value)(cw_rec_t *rec, int new_value);
		float (* get_value)(cw_rec_t *rec);

		int min; /* Minimal acceptable value of parameter. */
		int max; /* Maximal acceptable value of parameter. */

		const char *name;
	} test_data[] = {
		{ cw_get_speed_limits,      cw_rec_set_speed,      cw_rec_get_speed,      off_limits,  -off_limits,  "receive speed" },
		{ NULL,                     NULL,                  NULL,                           0,            0,  NULL            }
	};

	bool get_failure = true;
	bool set_min_failure = true;
	bool set_max_failure = true;
	bool set_ok_failure = false;


	for (int i = 0; test_data[i].get_limits; i++) {

		int status;
		int value = 0;

		/* Get limits of values to be tested. */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);

		get_failure = (test_data[i].min <= -off_limits);
		if (!cte->expect_eq_int_errors_only(cte, false, get_failure, "get/set param 1: get min %s: failed to get low limit, returned value = %d\n", test_data[i].name, test_data[i].min)) {
			get_failure = true;
			break;
		}
		get_failure = (test_data[i].max >= off_limits);
		if (!cte->expect_eq_int_errors_only(cte, false, get_failure, "get/set param 1: get max %s: failed to get high limit, returned value = %d\n", test_data[i].name, test_data[i].max)) {
			get_failure = true;
			break;
		}


		/* Test out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].min - 1;
		status = test_data[i].set_new_value(rec, value);
		if (!cte->expect_eq_int_errors_only(cte, CW_FAILURE, status, "get/set param 1: setting %s value below minimum succeeded, minimum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value)) {
			set_min_failure = true;
			break;
		}
		if (!cte->expect_eq_int_errors_only(cte, EINVAL, errno, "get/set param 1: setting %s value below minimum didn't result in EINVAL, minimum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value)) {
			set_min_failure = true;
			break;
		}



		/* Test out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].max + 1;
		status = test_data[i].set_new_value(rec, value);
		if (!cte->expect_eq_int_errors_only(cte, CW_FAILURE, status, "get/set param 1: setting %s value above minimum succeeded, maximum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value)) {
			set_max_failure = true;
			break;
		}
		if (!cte->expect_eq_int_errors_only(cte, EINVAL, errno, "get/set param 1: setting %s value above maximum didn't result in EINVAL, maximum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value)) {
			set_max_failure = true;
			break;
		}


		/* Test in-range values. Set with setter and then read back with getter. */
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			test_data[i].set_new_value(rec, j);

			float diff = test_data[i].get_value(rec) - j;
			set_ok_failure = (diff >= 0.01);
			if (!cte->expect_eq_int_errors_only(cte, false, set_ok_failure, "get/set param 1: setting value in-range failed for %s value = %d (%f - %d = %f)\n", test_data[i].name, j,	(float) test_data[i].get_value(rec), j, diff)) {
				set_ok_failure = true;
				break;
			}
		}
		if (set_ok_failure) {
			break;
		}
	}

	cw_rec_delete(&rec);

	cte->expect_eq_int(cte, false, get_failure, "get/set param 1: get:");
	cte->expect_eq_int(cte, false, set_min_failure, "get/set param 1: set value below min:");
	cte->expect_eq_int(cte, false, set_max_failure, "get/set param 1: set value above max:");
	cte->expect_eq_int(cte, false, set_ok_failure, "get/set param 1: set value in range:");

	cte->print_test_footer(cte, __func__);

	return 0;
}




/* Parameter getters and setters are independent of audio system, so
   they can be tested just with CW_AUDIO_NULL.  This is even more true
   for limit getters, which don't require a receiver at all. */
int test_cw_rec_parameter_getters_setters_2(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	cw_rec_t * rec = cw_rec_new();
	cw_assert (rec, MSG_PREFIX "get/set param 2: failed to create new receiver\n");

	/* Test setting and getting of some basic parameters. */

	int off_limits = 10000;

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int *min, int *max);
		int (* set_new_value)(cw_rec_t *rec, int new_value);
		int (* get_value)(cw_rec_t *rec);

		int min; /* Minimal acceptable value of parameter. */
		int max; /* Maximal acceptable value of parameter. */

		const char *name;
	} test_data[] = {
		{ cw_get_tolerance_limits,  cw_rec_set_tolerance,  cw_rec_get_tolerance,  off_limits,  -off_limits,  "tolerance"     },
		{ NULL,                     NULL,                  NULL,                           0,            0,  NULL            }
	};

	bool get_failure = true;
	bool set_min_failure = true;
	bool set_max_failure = true;
	bool set_ok_failure = false;


	for (int i = 0; test_data[i].get_limits; i++) {

		int status;
		int value = 0;

		/* Get limits of values to be tested. */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);

		get_failure = (test_data[i].min <= -off_limits);
		if (!cte->expect_eq_int_errors_only(cte, false, get_failure, "get/set param 2: get min %s: failed to get low limit, returned value = %d\n", test_data[i].name, test_data[i].min)) {
			get_failure = true;
			break;
		}
		get_failure = (test_data[i].max >= off_limits);
		if (!cte->expect_eq_int_errors_only(cte, false, get_failure, "get/set param 2: get max %s: failed to get high limit, returned value = %d\n", test_data[i].name, test_data[i].max)) {
			get_failure = true;
			break;
		}


		/* Test out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].min - 1;
		status = test_data[i].set_new_value(rec, value);

		if (!cte->expect_eq_int_errors_only(cte, CW_FAILURE, status, "get/set param 2 (cwret): setting %s value below minimum succeeded, minimum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value)) {
			set_min_failure = true;
			break;
		}
		if (!cte->expect_eq_int_errors_only(cte, EINVAL, errno, "get/set param (errno): setting %s value below minimum didn't result in EINVAL, minimum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value)) {
			set_min_failure = true;
			break;
		}



		/* Test out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].max + 1;
		status = test_data[i].set_new_value(rec, value);

		if (!cte->expect_eq_int_errors_only(cte, CW_FAILURE, status, "get/set param 2 (cwret): setting %s value above minimum succeeded, maximum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value)) {
			set_max_failure = true;
			break;
		}
		if (!cte->expect_eq_int_errors_only(cte, EINVAL, errno, "get/set param 2 (errno): setting %s value above maximum didn't result in EINVAL, maximum is %d, attempted value is %d\n", test_data[i].name, test_data[i].min, value)) {
			set_max_failure = true;
			break;
		}


		/* Test in-range values. Set with setter and then read back with getter. */
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			test_data[i].set_new_value(rec, j);

			float diff = test_data[i].get_value(rec) - j;
			set_ok_failure = (diff >= 0.01);
			if (!cte->expect_eq_int_errors_only(cte, false, set_ok_failure, "get/set param 2: setting value in-range failed for %s value = %d (%f - %d = %f)\n", test_data[i].name, j,(float) test_data[i].get_value(rec), j, diff)) {
				set_ok_failure = true;
				break;
			}
		}
		if (set_ok_failure) {
			break;
		}
	}

	cw_rec_delete(&rec);


	cte->expect_eq_int(cte, false, get_failure, "get/set param 2: get");
	cte->expect_eq_int(cte, false, set_min_failure, "get/set param 2: set value below min:");
	cte->expect_eq_int(cte, false, set_max_failure, "get/set param 2: set value above max:");
	cte->expect_eq_int(cte, false, set_ok_failure, "get/set param 2: set value in range:");

	cte->print_test_footer(cte, __func__);

	return 0;
}
