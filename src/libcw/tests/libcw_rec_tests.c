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
#include <math.h>




#include "test_framework.h"

#include "libcw_rec.h"
#include "libcw_rec_tests.h"
#include "libcw_rec_internal.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_tq.h"
#include "libcw_key.h"
#include "libcw.h"
#include "libcw2.h"



typedef struct cw_variation_params {
	/* For functions generating constant send speeds. */
	int speed;

	/* For functions generating varying send speeds. */
	int speed_min;
	int speed_max;

	/* For... something. */
	int fuzz_percent;
} cw_variation_params;




/* Data type describing sending speeds, at which test characters will
   be sent to receiver. */
typedef struct cw_send_speeds {
	float * values;
	size_t n_speeds;
} cw_send_speeds;
static cw_send_speeds * cw_send_speeds_new_constant(cw_test_executor_t * cte, size_t n, const cw_variation_params * variation_params);
static cw_send_speeds * cw_send_speeds_new_varying_sine(cw_test_executor_t * cte, size_t n, const cw_variation_params * variation_params);
static void cw_send_speeds_delete(cw_send_speeds ** speeds);
typedef cw_send_speeds * (* send_speeds_maker_t)(cw_test_executor_t *, size_t n, const cw_variation_params * variation_params);



/* Set of characters that will be sent to receiver. */
typedef struct cw_characters_list {
	char * values;
	size_t n_characters; /* Does not include terminating NUL. */
} cw_characters_list;
static cw_characters_list * cw_characters_list_new_basic(cw_test_executor_t * cte);
static cw_characters_list * cw_characters_list_new_random(cw_test_executor_t * cte);
static void cw_characters_list_delete(cw_characters_list ** characters_list);
typedef cw_characters_list * (* characters_list_maker_t)(cw_test_executor_t *);




#define TEST_CW_REC_DATA_LEN_MAX 30 /* There is no character that would have that many time points corresponding to a representation. */
typedef struct cw_rec_test_point {
	char character;                         /* Character that is being sent to receiver. */
	char * representation;                  /* Character's representation (dots and dashes). */
	int tone_durations[TEST_CW_REC_DATA_LEN_MAX];  /* Character's representation's times - time information for marks and spaces. */
	size_t n_tone_durations;                /* Number of duration values encoding given representation of given character. */
	float send_speed;                       /* Send speed (speed at which the character is incoming). */

	bool is_last_in_word;                   /* Is this character a last character in a word? (is it followed by end-of-word space?) */
} cw_rec_test_point;
static cw_rec_test_point * cw_rec_test_point_new(cw_test_executor_t * cte);
static void cw_rec_test_point_delete(cw_rec_test_point ** point);




typedef struct cw_rec_test_vector {
	cw_rec_test_point ** points;

	/*
	  Because of how we treat space characters from list of test
	  characters (we don't put them in cw_rec_test_vector vector),
	  not all points allocated in this object will be valid (will
	  represent valid characters). In order to be able to
	  deallocate both valid and invalid points, we have to have
	  two separate variables: one for total count of allocated
	  points, and the other for count of valid points.
	*/
	size_t n_points_allocated; /* This is how many point objects were allocated and are in cw_rec_test_vector::points. */
	size_t n_points_valid;     /* This is how many valid points (with valid character and durations) are in cw_rec_test_vector::points. */

} cw_rec_test_vector;
static cw_rec_test_vector * cw_rec_test_vector_new(cw_test_executor_t * cte, size_t n);
static void cw_rec_test_vector_delete(cw_rec_test_vector ** vec);
static cw_rec_test_vector * cw_rec_test_vector_factory(cw_test_executor_t * cte, characters_list_maker_t characters_list_maker, send_speeds_maker_t send_speeds_maker, const cw_variation_params * variation_params);
__attribute__((unused)) static void cw_rec_test_vector_print(cw_test_executor_t * cte, cw_rec_test_vector * vec);
static bool test_cw_rec_test_begin_end(cw_test_executor_t * cte, cw_rec_t * rec, cw_rec_test_vector * vec);




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
	cte->assert2(cte, rec, "identify mark: failed to create new receiver\n");
	cw_rec_disable_adaptive_mode(rec);

	int speed_step = (CW_SPEED_MAX - CW_SPEED_MIN) / 10;

	for (int speed = CW_SPEED_MIN; speed < CW_SPEED_MAX; speed += speed_step) {
		int rv = cw_rec_set_speed(rec, speed);
		cte->assert2(cte, rv, "identify mark @ %02d [wpm]: failed to set receive speed\n", speed);


		bool failure = true;
		int n = 0;
		char representation;




		/* Test marks that have length appropriate for a dot. */
		int len_step = (rec->dot_len_max - rec->dot_len_min) / 10;
		for (int len = rec->dot_len_min; len < rec->dot_len_max; len += len_step) {
			const int cwret = cw_rec_identify_mark_internal(rec, len, &representation);
			if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "identify mark @ %02d [wpm]: failed to identify dot for len = %d [us]\n", speed, len)) {
				failure = true;
				break;
			}
			if (!cte->expect_op_int(cte, CW_DOT_REPRESENTATION, "==", representation, 1, "identify mark @ %02d [wpm]: failed to get dot representation for len = %d [us]\n", speed, len)) {
				failure = true;
				break;
			}
		}

		cte->expect_op_int(cte, false, "==", failure, 1, "identify mark @ %02d [wpm]: identify valid dot:", speed);


#if 0
		/* Test mark shorter than minimal length of dot. */
		cwret = cw_rec_identify_mark_internal(rec, rec->dot_len_min - 1, &representation);
		cte->expect_op_int(cte, CW_FAILURE, "==", cwret, 1, "identify mark @ %02d [wpm]: mark shorter than min dot:", speed);

		/* Test mark longer than maximal length of dot (but shorter than minimal length of dash). */
		cwret = cw_rec_identify_mark_internal(rec, rec->dot_len_max + 1, &representation);
		cte->expect_op_int(cte, CW_FAILURE, "==", cwret, 1, "identify mark @ %02d [wpm]: mark longer than max dot:", speed);


		/* Test marks that have length appropriate for a dash. */
		len_step = (rec->dash_len_max - rec->dash_len_min) / 10;
		for (int len = rec->dash_len_min; len < rec->dash_len_max; len += len_step) {
			cwret = cw_rec_identify_mark_internal(rec, len, &representation);
			if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "identify mark @ %02d [wpm]: failed to identify dash for len = %d [us]\n", speed, len)) {
				failure = true;
				break;
			}

			if (!cte->expect_op_int(cte, CW_DASH_REPRESENTATION, "==", representation, 1, "identify mark @ %02d [wpm]: failed to get dash representation for len = %d [us]\n", speed, len)) {
				failure = true;
				break;
			}
		}
		cte->expect_op_int(cte, false, "==", failure, 1, "identify mark @ %02d [wpm]: identify valid dash:", speed);


		/* Test mark shorter than minimal length of dash (but longer than maximal length of dot). */
		cwret = = cw_rec_identify_mark_internal(rec, rec->dash_len_min - 1, &representation);
		cte->expect_op_int(cte, CW_FAILURE, "==", cwret, 1, "identify mark @ %02d [wpm]: mark shorter than min dash:", speed);



		/* Test mark longer than maximal length of dash. */
		cwret = cw_rec_identify_mark_internal(rec, rec->dash_len_max + 1, &representation);
		cte->expect_op_int(cte, CW_FAILURE, "==", cwret, 1, "identify mark @ %02d [wpm]: mark longer than max dash:", speed);
#endif
	}

	cw_rec_delete(&rec);
#endif

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   @brief Test a receiver with characters sent at constant speed

   @reviewed on 2019-10-23
*/
int test_cw_rec_test_with_constant_speeds(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	const char * this_test_name = "constant speeds";

	struct {
		const char * name;
		characters_list_maker_t char_list_maker;
		send_speeds_maker_t send_speeds_maker;
	} test_data[] = {
		{
			/* All characters supported by libcw.  Don't
			   use get_characters_random(): for this test
			   get a small table of all characters
			   supported by libcw. This should be a quick
			   test, and it should give 100% guarantee
			   that it covers all characters.

			   Fixed speed receive mode: speed is constant
			   for all characters.
			*/
			"basic chars/const speed", cw_characters_list_new_basic, cw_send_speeds_new_constant
		},
		{
			/* Fixed speed receive mode: speed is constant
			   for all characters. */
			"random chars/const speed", cw_characters_list_new_random, cw_send_speeds_new_constant
		},
		{
			NULL, NULL, NULL
		}
	};

	cw_rec_t * rec = cw_rec_new();
	cte->assert2(cte, rec, "%s: failed to create new receiver\n", this_test_name);

	int i = 0;
	while (test_data[i].name) {
		for (int speed = CW_SPEED_MIN; speed <= CW_SPEED_MAX; speed++) {

			const cw_variation_params variation_params = { .speed = speed, .speed_min = 0, .speed_max = 0, .fuzz_percent = 0 };

			/* Generate duration data for given list of
			   characters, each character is sent with
			   speed calculated by "speeds maker". */
			cw_rec_test_vector * vec = cw_rec_test_vector_factory(cte,
									      test_data[i].char_list_maker,
									      test_data[i].send_speeds_maker,
									      &variation_params);
			cte->assert2(cte, vec, "%s: failed to generate test vector for test %s\n", this_test_name, test_data[i].name);
			// cw_rec_test_vector_print(cte, vec);

			/* Prepare receiver (by resetting it to fresh state). */
			cw_rec_reset_statistics(rec);
			cw_rec_reset_state(rec);
			cw_rec_set_speed(rec, speed);
			cw_rec_disable_adaptive_mode(rec);

			/* Verify that the test speed has been set correctly. */
			const float diff = cw_rec_get_speed(rec) - (float) speed;
			cte->assert2(cte, diff < 0.1, "%s: setting speed for test %s failed: %f != %f\n", this_test_name, test_data[i].name, cw_rec_get_speed(rec), (float) speed);

			/* Actual tests of receiver functions are here. */
			bool failure = test_cw_rec_test_begin_end(cte, rec, vec);
			cte->expect_op_int(cte, false, "==", failure, 0, "%s: %s @ %02d wpm", this_test_name, test_data[i].name, speed);

			cw_rec_test_vector_delete(&vec);
		}
		i++;
	}

	cw_rec_delete(&rec);

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   @brief Test a receiver with characters sent at varying speeds

   @reviewed on 2019-10-24
*/
int test_cw_rec_test_with_varying_speeds(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	const char * this_test_name = "varying speeds";

	struct {
		const char * name;
		characters_list_maker_t char_list_maker;
		send_speeds_maker_t send_speeds_maker;
	} test_data[] = {
		{
			/* All characters supported by libcw.  Don't
			   use get_characters_random(): for this test
			   get a small table of all characters
			   supported by libcw. This should be a quick
			   test, and it should give 100% guarantee
			   that it covers all characters.
			*/
			"basic chars/var speed", cw_characters_list_new_basic, cw_send_speeds_new_varying_sine
		},
		{
			"random chars/var speed", cw_characters_list_new_random, cw_send_speeds_new_varying_sine
		},
		{
			NULL, NULL, NULL
		}
	};

	cw_rec_t * rec = cw_rec_new();
	cte->assert2(cte, rec, "%s: failed to create new receiver\n", this_test_name);

	int i = 0;
	while (test_data[i].name) {

		cw_variation_params variation_params = { .speed = 0, .speed_min = CW_SPEED_MIN, .speed_max = CW_SPEED_MAX, .fuzz_percent = 0 };

		/* Generate duration data for given set of characters, each
		   character is sent with varying speed from range
		   speed_min-speed_max. */
		cw_rec_test_vector * vec = cw_rec_test_vector_factory(cte,
								      test_data[i].char_list_maker,
								      test_data[i].send_speeds_maker,
								      &variation_params);
		cte->assert2(cte, vec, "%s: failed to generate random/varying test data\n", this_test_name);
		// cw_rec_test_vector_print(cte, vec);


		/* Prepare receiver (by resetting it to fresh state). */
		cw_rec_reset_statistics(rec);
		cw_rec_reset_state(rec);

		cw_rec_set_speed(rec, CW_SPEED_MAX);
		cw_rec_enable_adaptive_mode(rec);

		/* Verify that initial test speed has been set correctly. */
		const float diff = cw_rec_get_speed(rec) - CW_SPEED_MAX;
		cte->assert2(cte, diff < 0.1, "%s: incorrect receive speed: %f != %f\n", this_test_name, cw_rec_get_speed(rec), (float) CW_SPEED_MAX);

		/* Actual tests of receiver functions are here. */
		const bool failure = test_cw_rec_test_begin_end(cte, rec, vec);
		cte->expect_op_int(cte, false, "==", failure, 0, "%s", this_test_name);

		cw_rec_test_vector_delete(&vec);

		i++;
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
   \param data - table with tone_durations, used to test the receiver
*/
bool test_cw_rec_test_begin_end(cw_test_executor_t * cte, cw_rec_t * rec, cw_rec_test_vector * vec)
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

	for (size_t i = 0; i < vec->n_points_valid; i++) {

		cw_rec_test_point * point = vec->points[i];

#ifdef LIBCW_UNIT_TESTS_VERBOSE
		printf("\n" "begin/end: input test data #%d: <%c> / <%s> @ %.2f [wpm] (%d time values)\n",
		       i, point->character, point->r, point->s, point->n_tone_durations);
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
		for (tone = 0; point->tone_durations[tone] > 0; tone++) {
			begin_end_failure = false;

			if (tone % 2) {
				const int cwret = cw_rec_mark_end(rec, &tv);
				if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "begin/end: cw_rec_mark_end(): tone = %d, time = %d.%d\n", tone, (int) tv.tv_sec, (int) tv.tv_usec)) {
					begin_end_failure = true;
					break;
				}
			} else {
				const int cwret = cw_rec_mark_begin(rec, &tv);
				if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "begin/end: cw_rec_mark_begin(): tone = %d, time = %d.%d\n", tone, (int) tv.tv_sec, (int) tv.tv_usec)) {
					begin_end_failure = true;
					break;
				}
			}

			tv.tv_usec += point->tone_durations[tone];
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

		cte->assert2(cte, tone, "begin/end executed zero times\n");
		if (begin_end_failure) {
			break;
		}




		/* Test: length of receiver's buffer (only marks!)
		   after adding a representation of a single character
		   to receiver's buffer. */
		{
			int n = cw_rec_get_buffer_length_internal(rec);
			buffer_length_failure = (n != (int) strlen(point->representation));

			if (!cte->expect_op_int(cte, false, "==", buffer_length_failure, 1, "begin/end: cw_rec_get_buffer_length_internal(<nonempty>): %d != %zd\n", n, strlen(point->representation))) {
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
			if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "begin/end: poll representation returns !CW_SUCCESS")) {
				poll_representation_failure = true;
				break;
			}

			const int strcmp_result = strcmp(representation, point->representation);
			if (!cte->expect_op_int(cte, 0, "==", strcmp_result, 1, "being/end: polled representation does not match test representation: \"%s\" != \"%s\"\n", representation, point->representation)) {
				match_representation_failure = true;
				break;
			}

			if (!cte->expect_op_int(cte, false, "==", is_error, 1, "begin/end: poll representation sets is_error\n")) {
				error_representation_failure = true;
				break;
			}



			/* If the last space in character's data is
			   end-of-word space (which is indicated by
			   is_last_in_word), then is_word should be
			   set by poll() to true. Otherwise both
			   values should be false. */
			word_representation_failure = (is_word != point->is_last_in_word);
			// cte->expect_op_int(cte, ); // TODO: implement
			if (word_representation_failure) {
				fprintf(out_file, "begin/end: poll representation: 'is_word' flag error: function returns '%d', data is tagged with '%d'\n" \
					"'%c'  '%c'  '%c'  '%c'  '%c'",
					is_word, point->is_last_in_word,
					vec->points[i - 2]->character, vec->points[i - 1]->character, vec->points[i]->character, vec->points[i + 1]->character, vec->points[i + 2]->character);
				break;
			}

#if 0
			/* Debug code. Print times of character with
			   end-of-word space to verify length of the
			   space. */
			if (point->is_last_in_word) {
				fprintf(stderr, "------- character '%c' is last in word\n", point->character);
				for (int m = 0; m < point->n_tone_durations; m++) {
					fprintf(stderr, "#%d: %d\n", m, point->d[m]);
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
			if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "begin/end: poll character false\n")) {
				poll_character_failure = true;
				break;
			}
			if (!cte->expect_op_int(cte, point->character, "==", c, 1, "begin/end: polled character does not match test character: '%c' != '%c'\n", c, point->character)) {
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
			if (!cte->expect_op_int(cte, 0, "==", length, 1, "begin/end: get buffer length: length of cleared buffer is non zero (is %d)", length)) {
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

	/*
	  This test function will be called many times so success
	  counters may go over a thousand, but without these expects
	  here the whole test of receiver would end with only of few
	  success counter hits. Maybe we should somehow move the calls
	  up the call chain.
	*/
	cte->expect_op_int(cte, false, "==", begin_end_failure, 1, "Signalling begin and end of mark");
	cte->expect_op_int(cte, false, "==", buffer_length_failure, 1, "Getting length of representation buffer");
	cte->expect_op_int(cte, false, "==", poll_representation_failure, 1, "Polling representation");
	cte->expect_op_int(cte, false, "==", match_representation_failure, 1, "Representation match");
	cte->expect_op_int(cte, false, "==", error_representation_failure, 1, "Representation 'is error'");
	cte->expect_op_int(cte, false, "==", word_representation_failure, 1, "Representation 'is word'");
	cte->expect_op_int(cte, false, "==", poll_character_failure, 1, "Polling character");
	cte->expect_op_int(cte, false, "==", match_character_failure, 1, "Character match");
	cte->expect_op_int(cte, false, "==", empty_failure, 1, "Empty representation buffer");

	return begin_end_failure
		|| buffer_length_failure
		|| poll_representation_failure || match_representation_failure || error_representation_failure || word_representation_failure
		|| poll_character_failure || match_character_failure || empty_failure;
}




/**
   \brief Get a string with all characters supported by libcw

   Function allocates and returns a string with all characters that
   are supported/recognized by libcw.

   @reviewed on 2019-10-24

   \return wrapper object for allocated string
*/
cw_characters_list * cw_characters_list_new_basic(cw_test_executor_t * cte)
{
	const int n = cw_get_character_count();

	cw_characters_list * characters_list = (cw_characters_list *) calloc(sizeof (cw_characters_list), 1);
	cte->assert2(cte, characters_list, "%s: first calloc() failed\n", __func__);

	characters_list->values = (char *) calloc(sizeof (char), n + 1); /* This will be a C string, so +1 for terminating NUL. */
	cte->assert2(cte, characters_list, "%s: second calloc() failed\n", __func__);

	characters_list->n_characters = n;
	cw_list_characters(characters_list->values);

	return characters_list;
}




/**
   \brief Generate a set of characters of size \p n.

   Function allocates and returns a string of \p n characters. The
   characters are randomly drawn from set of all characters supported
   by libcw.

   Spaces are added to the string in random places to mimic a regular
   text. Function makes sure that there are no consecutive spaces (two
   or more) in the string.

   @reviewed on 2019-10-24

   \return wrapper object for string of random characters (including spaces)
*/
cw_characters_list * cw_characters_list_new_random(cw_test_executor_t * cte)
{
	const size_t n_random_characters = cw_get_character_count() * ((rand() % 50) + 30);

	/* We will use basic characters list (all characters supported
	   by libcw) as an input for generating random characters
	   list. */
	cw_characters_list * basic_characters_list = cw_characters_list_new_basic(cte);
	const size_t n_basic_characters = basic_characters_list->n_characters;


	cw_characters_list * random_characters_list = (cw_characters_list *) calloc(sizeof (cw_characters_list), 1);
	cte->assert2(cte, random_characters_list, "first calloc() failed\n");

	random_characters_list->values = (char *) calloc(sizeof (char), n_random_characters + 1); /* This will be a C string, so +1 for terminating NUL. */
	cte->assert2(cte, random_characters_list->values, "second calloc() failed\n");


	size_t space_randomizer = 3;
	for (size_t i = 0; i < n_random_characters; i++) {
		int basic_idx = rand() % n_basic_characters;

		if (0 == (basic_idx % space_randomizer)) { /* Insert space at random places. */
			space_randomizer = (rand() % (n_basic_characters / 2)) + 3; /* Pick new value for next round. */
			random_characters_list->values[i] = ' ';

			/*
			  Also fill next cell, but with non-space
			  char, to prevent two consecutive spaces in
			  result string.

			  TODO: why we want to avoid two consecutive
			  spaces? Does it break test algorithm?
			*/
			if ((i + 1) < n_random_characters) {
				i++;
				random_characters_list->values[i] = basic_characters_list->values[basic_idx];
			}
		} else {
			random_characters_list->values[i] = basic_characters_list->values[basic_idx];
		}
	}

	/*
	  First character in input data can't be a space. Two reasons:
	  1. we can't start a receiver's state machine with space.
	  2. when a end-of-word space appears in test string, it is
	  added as last duration value at the end of duration values
	  table for "previous char". We couldn't do this (i.e. modify
	  table of duration of "previous char") for 1st char in test
	  string.
	*/
	random_characters_list->values[0] = 'K'; /* Use capital letter. libcw uses capital letters internally. */
	random_characters_list->values[n_random_characters] = '\0';
	random_characters_list->n_characters = n_random_characters;

	// fprintf(stderr, "\n%s\n\n", random_characters_list->values);

	cw_characters_list_delete(&basic_characters_list);

	return random_characters_list;
}




/**
   @reviewed on 2019-10-25
*/
void cw_characters_list_delete(cw_characters_list ** characters_list)
{
	if (NULL == characters_list) {
		return;
	}
	if (NULL == *characters_list) {
		return;
	}
	if (NULL != (*characters_list)->values) {
		free((*characters_list)->values);
		(*characters_list)->values = NULL;
	}
	free(*characters_list);
	*characters_list = NULL;
}




/**
   \brief Generate a table of constant speeds

   Function allocates and returns a table of speeds of constant value
   specified by cw_variation_params::speed. There will be \p n valid
   (non-negative and within valid range) values in the table.

   @reviewed on 2019-10-25

   \param n - size of speeds table
   \param cw_variation_params::speed - a constant value to be used as initializer of the table

   \return wrapper object for table of speeds of constant value
*/
cw_send_speeds * cw_send_speeds_new_constant(cw_test_executor_t * cte, size_t n, const cw_variation_params * variation_params)
{
	cte->assert2(cte, variation_params->speed >= CW_SPEED_MIN, "%s: speed must be at least %d\n", __func__, CW_SPEED_MIN);
	cte->assert2(cte, variation_params->speed <= CW_SPEED_MAX, "%s: speed must be no larger than %d\n", __func__, CW_SPEED_MAX);

	cw_send_speeds * speeds = (cw_send_speeds *) calloc(sizeof (cw_send_speeds), 1);
	cte->assert2(cte, speeds, "%s: first calloc() failed\n", __func__);

	speeds->values = (float *) calloc(sizeof (float), n);
	cte->assert2(cte, speeds, "%s: second calloc() failed\n", __func__);

	speeds->n_speeds = n;

	for (size_t i = 0; i < speeds->n_speeds; i++) {
		/* Constant speeds. */
		speeds->values[i] = (float) variation_params->speed;
	}

	return speeds;
}




/**
   \brief Generate a table of varying speeds

   Function allocates and returns a table of speeds of varying values,
   changing between \p variation_params::speed_min and \p
   variation_params::speed_max. There will be \p n valid (non-negative
   and within the specified range) values in the table.

   @reviewed on 2019-10-25

   \param n - size of speeds table
   \param variation_params::speed_min - minimal speed
   \param variation_params::speed_max - maximal speed

   \return wrapper object for table of speeds
*/
cw_send_speeds * cw_send_speeds_new_varying_sine(cw_test_executor_t * cte, size_t n, const cw_variation_params * variation_params)
{
	cte->assert2(cte, variation_params->speed_min >= CW_SPEED_MIN, "%s: speed_min must be at least %d\n", __func__, CW_SPEED_MIN);
	cte->assert2(cte, variation_params->speed_max >= CW_SPEED_MIN, "%s: speed_max must be at least %d\n", __func__, CW_SPEED_MIN);
	cte->assert2(cte, variation_params->speed_min <= CW_SPEED_MAX, "%s: speed_min must be no larger than %d\n", __func__, CW_SPEED_MAX);
	cte->assert2(cte, variation_params->speed_max <= CW_SPEED_MAX, "%s: speed_max must be no larger than %d\n", __func__, CW_SPEED_MAX);
	cte->assert2(cte, variation_params->speed_min <= variation_params->speed_max, "%s: speed_min can't be larger than speed_max\n", __func__);

	cw_send_speeds * speeds = (cw_send_speeds *) calloc(sizeof (cw_send_speeds), 1);
	cte->assert2(cte, speeds, "%s: first calloc() failed\n", __func__);

	speeds->values = (float *) calloc(sizeof (float), n);
	cte->assert2(cte, speeds, "%s: second calloc() failed\n", __func__);

	speeds->n_speeds = n;

	for (size_t i = 0; i < n; i++) {
		/* Varying speeds. */
		const float t = (1.0 * i) / n;
		speeds->values[i] = (1 + cosf(2 * 3.1415 * t)) / 2.0;                             /* 0.0 -  1.0 */
		speeds->values[i] *= (variation_params->speed_max - variation_params->speed_min); /* 0.0 - 56.0 */
		speeds->values[i] += variation_params->speed_min;                                 /* 4.0 - 60.0 */
		// fprintf(stderr, "%f\n", speeds->values[i]);
	}

	return speeds;
}




/**
   @reviewed on 2019-10-25
*/
void cw_send_speeds_delete(cw_send_speeds ** speeds)
{
	if (NULL == speeds) {
		return;
	}
	if (NULL == *speeds) {
		return;
	}
	if (NULL != (*speeds)->values) {
		free((*speeds)->values);
		(*speeds)->values = NULL;
	}
	free(*speeds);
	*speeds = NULL;
}




/**
   \brief Create durations data used for testing a receiver

   This is a generic function that can generate different sets of data
   depending on input parameters. It is to be used by wrapper
   functions that first specify parameters of test data, and then pass
   the parameters to this function.

   The function allocates a table with durations data (and some other
   data as well) that can be used to test receiver's functions that
   accept timestamp argument.

   @param character_list_maker generates list of (valid) characters
   that will be represented by durations.

   @param send_speeds_maker generates list of speeds (wpm) at which
   the characters will be sent to receiver.

   The data returned by the function is valid and represents valid
   Morse representations (durations describe a series of dots and
   dashes that in turn correspond to list of characters).  If you want
   to generate invalid data or to generate data based on invalid
   representations, you have to use some other function.

   For each character the last duration parameter represents
   end-of-character space or end-of-word space. The next duration
   parameter after that space is zero. For character 'A' that would
   look like this:

   .-    ==   40000 (dot mark); 40000 (inter-mark space); 120000 (dash mark); 240000 (end-of-word space); 0 (guard, zero duration)

   Last element in the created table (a guard "pseudo-character") has
   'representation' field set to NULL.

   Use cw_rec_test_vector_delete() to deallocate the duration data table.

   @reviewed on 2019-10-25

   \return wrapper object around table of duration data sets
*/
cw_rec_test_vector * cw_rec_test_vector_factory(cw_test_executor_t * cte, characters_list_maker_t characters_list_maker, send_speeds_maker_t send_speeds_maker, const cw_variation_params * variation_params)
{
	cw_characters_list * characters_list = characters_list_maker(cte);
	cw_send_speeds * send_speeds = send_speeds_maker(cte, characters_list->n_characters, variation_params);


	const size_t n_characters = characters_list->n_characters;
	cw_rec_test_vector * vec = cw_rec_test_vector_new(cte, n_characters);


	size_t out_idx = 0;
	for (size_t in_idx = 0; in_idx < n_characters; in_idx++) {

		/* TODO: in this loop we are using some formulas from
		   somewhere to calculate durations. Where do these
		   formulas come from? How do we know that they are
		   valid? */

		const int dot_duration = CW_DOT_CALIBRATION / send_speeds->values[in_idx]; /* [us]; used as basis for other elements. */
		//fprintf(stderr, "dot_duration = %d [us] for speed = %05.2f [wpm]\n", dot_duration, send_speeds->values[in_idx]);


		/*
		  First handle a special case: end-of-word space. This
		  long space will be put at the end of table of time
		  values for previous representation. The space in
		  character list is never transformed into separate
		  point in vector.

		  When generating list of characters, we make sure to
		  put non-space character at index 0, so when we index
		  points[] with 'out_idx-1' we are safe.
		*/
		if (characters_list->values[in_idx] == ' ') {
			/* We don't want to affect *current* output
			   point (we don't create a vector point for
			   space). We want to turn end-of-char space
			   of previous point into end-of-word space,
			   hence 'out_idx - 1'. */
			cw_rec_test_point * prev_point = vec->points[out_idx - 1];
			const int space_idx = prev_point->n_tone_durations - 1;   /* Index of last space (end-of-char, to become end-of-word). */
			prev_point->tone_durations[space_idx] = dot_duration * 6; /* dot_duration * 5 is the minimal end-of-word space. */
			prev_point->is_last_in_word = true;

			continue;
		} else {
			/* A regular character, handled below. */
		}

		cw_rec_test_point * point = vec->points[out_idx];

		point->character = characters_list->values[in_idx];
		point->representation = cw_character_to_representation(point->character);
		cte->assert2(cte, point->representation,
			     "%s: cw_character_to_representation() failed for input char #%zu: '%c'\n",
			     __func__,
			     in_idx, characters_list->values[in_idx]);
		point->send_speed = send_speeds->values[in_idx];


		/* Build table of durations 'tone_durations[]' for
		   given representation 'point->representation'. */

		size_t n_tone_durations = 0; /* Number of durations in durations table. */

		size_t rep_length = strlen(point->representation);
		for (size_t k = 0; k < rep_length; k++) {

			/* Length of mark. */
			if (point->representation[k] == CW_DOT_REPRESENTATION) {
				point->tone_durations[n_tone_durations] = dot_duration;

			} else if (point->representation[k] == CW_DASH_REPRESENTATION) {
				point->tone_durations[n_tone_durations] = dot_duration * 3;

			} else {
				cte->assert2(cte, 0, "%s: unknown char in representation: '%c'\n", __func__, point->representation[k]);
			}
			n_tone_durations++;


			/* Length of space (inter-mark space). Mark
			   and space always go in pair. */
			point->tone_durations[n_tone_durations] = dot_duration;
			n_tone_durations++;
		}

		/* Every character has non-zero marks and spaces. */
		cte->assert2(cte, n_tone_durations > 0, "%s: number of data points is %zu for representation '%s'\n", __func__, n_tone_durations, point->representation);

		/* Mark and space always go in pair, so nd should be even. */
		cte->assert2(cte, ! (n_tone_durations % 2), "%s: number of times is not even\n", __func__);

		/* Mark/space pair per each dot or dash. */
		cte->assert2(cte, n_tone_durations == 2 * rep_length, "%s: number of times incorrect: %zu != 2 * %zu\n", __func__, n_tone_durations, rep_length);


		/* Graduate that last space (inter-mark space) into
		   end-of-character space. */
		point->tone_durations[n_tone_durations - 1] = (dot_duration * 3) + (dot_duration / 2);

		/* Guard. */
		point->tone_durations[n_tone_durations] = 0;

		point->n_tone_durations = n_tone_durations;

		/* This may be overwritten by this function when a
		   space character (' ') is encountered in next cell
		   of input string. */
		point->is_last_in_word = false;

		out_idx++;
	}

	/* The count of valid points in vector (smaller than
	   n_characters because we have skipped all space (' ')
	   characters). */
	vec->n_points_valid = out_idx;

	cw_characters_list_delete(&characters_list);
	cw_send_speeds_delete(&send_speeds);


	return vec;
}




/**
   @reviewed on 2019-10-26
*/
cw_rec_test_point * cw_rec_test_point_new(__attribute__((unused)) cw_test_executor_t * cte)
{
	return (cw_rec_test_point *) calloc(sizeof (cw_rec_test_point), 1);
}




/**
   @reviewed on 2019-10-26
*/
void cw_rec_test_point_delete(cw_rec_test_point ** point)
{
	if (NULL == point) {
		return;
	}
	if (NULL == *point) {
		return;
	}
	if ((*point)->representation) {
		free((*point)->representation);
		(*point)->representation = NULL;
	}
	free(*point);
	*point = NULL;

	return;
}




/**
   @reviewed on 2019-10-26
*/
cw_rec_test_vector * cw_rec_test_vector_new(cw_test_executor_t * cte, size_t n)
{
	cw_rec_test_vector * vec = (cw_rec_test_vector *) calloc(sizeof (cw_rec_test_vector), 1);
	cte->assert2(cte, vec, "%s: first calloc() failed\n", __func__);

	vec->points = (cw_rec_test_point **) calloc(sizeof (cw_rec_test_point *), n);
	cte->assert2(cte, vec->points, "%s: second calloc() failed\n", __func__);

	vec->n_points_allocated = n;
	/* This will be overwritten later, once we know the real
	   number of valid points generated from non-space
	   characters. */
	vec->n_points_valid = n;

	for (size_t i = 0; i < vec->n_points_allocated; i++) {
		vec->points[i] = cw_rec_test_point_new(cte);
	}

	return vec;
}




/**
   \brief Deallocate vector of duration data used for testing a receiver

   @reviewed on 2019-10-26

   \param vec - pointer to vector to be deallocated
*/
void cw_rec_test_vector_delete(cw_rec_test_vector ** vec)
{
	if (NULL == vec) {
		return;
	}
	if (NULL == *vec) {
		return;
	}

	for (size_t i = 0; i < (*vec)->n_points_allocated; i++) {
		cw_rec_test_point_delete(&(*vec)->points[i]);
	}

	free((*vec)->points);
	(*vec)->points = NULL;

	free(*vec);
	*vec = NULL;

	return;
}




/**
   \brief Pretty-print duration data used for testing a receiver

   @reviewed on 2019-10-26

   \param vec - vector with duration data to be printed
*/
void cw_rec_test_vector_print(cw_test_executor_t * cte, cw_rec_test_vector * vec)
{
	cte->log_info_cont(cte, "---------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	for (size_t i = 0; i < vec->n_points_valid; i++) {

		cw_rec_test_point * point = vec->points[i];

		/* Print header. */
		if (!(i % 10)) {
			cte->log_info_cont(cte, "ch repr     [wpm]     mark     space      mark     space      mark     space      mark     space      mark     space      mark     space      mark     space\n");
		}

		/* Print data. */
		cte->log_info_cont(cte, "%c  %-7s %6.2f", point->character, point->representation, point->send_speed);
		for (size_t t = 0; t < point->n_tone_durations; t++) {
			cte->log_info_cont(cte, "%9d ", point->tone_durations[t]);
		}

		cte->log_info_cont(cte, "\n");
	}

	return;
}




/**
   Parameter getters are independent of audio system, so they can be
   tested just with CW_AUDIO_NULL.

   @reviewed on 2019-10-26
*/
int test_cw_rec_get_parameters(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	const char * this_test_name = "get params";

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

	cw_rec_t * rec = cw_rec_new();
	cte->assert2(cte, rec, "%s: failed to create new receiver\n", this_test_name);
	cw_rec_reset_parameters_internal(rec);
	cw_rec_sync_parameters_internal(rec);
	cw_rec_get_parameters_internal(rec,
				       &dot_len_ideal, &dash_len_ideal, &dot_len_min, &dot_len_max, &dash_len_min, &dash_len_max,
				       &eom_len_min, &eom_len_max, &eom_len_ideal,
				       &eoc_len_min, &eoc_len_max, &eoc_len_ideal,
				       &adaptive_speed_threshold);
	cw_rec_delete(&rec);

	cte->log_info(cte, "%s: dot/dash:  %d, %d, %d, %d, %d, %d\n", this_test_name, dot_len_ideal, dash_len_ideal, dot_len_min, dot_len_max, dash_len_min, dash_len_max);
	cte->log_info(cte, "%s: eom:       %d, %d, %d\n", this_test_name, eom_len_min, eom_len_max, eom_len_ideal);
	cte->log_info(cte, "%s: eoc:       %d, %d, %d\n", this_test_name, eoc_len_min, eoc_len_max, eoc_len_ideal);
	cte->log_info(cte, "%s: adaptive threshold: %d\n", this_test_name, adaptive_speed_threshold);

	bool failure = (dot_len_ideal <= 0
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
	cte->expect_op_int(cte, false, "==", failure, 0, "cw_rec_get_parameters_internal()");


	cte->expect_op_int(cte, dot_len_max, "<", dash_len_min, 0, "%s: max dot len < min dash len (%d/%d)", this_test_name, dot_len_max, dash_len_min);

	cte->expect_op_int(cte, dot_len_min, "<", dot_len_max, 0, "%s: dot len consistency A (%d/%d)", this_test_name, dot_len_min, dot_len_max);
	cte->expect_op_int(cte, dot_len_min, "<", dot_len_ideal, 0, "%s: dot len consistency B (%d/%d/%d)", this_test_name, dot_len_min, dot_len_ideal, dot_len_max);
	cte->expect_op_int(cte, dot_len_max, ">", dot_len_ideal, 0, "%s: dot len consistency C (%d/%d/%d)", this_test_name, dot_len_min, dot_len_ideal, dot_len_max);

	cte->expect_op_int(cte, dash_len_min, "<", dash_len_max, 0, "%s: dash len consistency A (%d/%d)", this_test_name, dash_len_min, dash_len_max);
	cte->expect_op_int(cte, dash_len_min, "<", dash_len_ideal, 0, "%s: dash len consistency B (%d/%d/%d)", this_test_name, dash_len_min, dash_len_ideal, dash_len_max);
	cte->expect_op_int(cte, dash_len_max, ">", dash_len_ideal, 0, "%s: dash len consistency c (%d/%d/%d)", this_test_name, dash_len_min, dash_len_ideal, dash_len_max);


	cte->expect_op_int(cte, eom_len_max, "<", eoc_len_min, 0, "%s: max eom len < min eoc len (%d/%d)", this_test_name, eom_len_max, eoc_len_min);

	cte->expect_op_int(cte, eom_len_min, "<", eom_len_max, 0, "%s: eom len consistency A (%d/%d)", this_test_name, eom_len_min, eom_len_max);
	cte->expect_op_int(cte, eom_len_min, "<", eom_len_ideal, 0, "%s: eom len consistency B (%d/%d/%d)", this_test_name, eom_len_min, eom_len_ideal, eom_len_max);
	cte->expect_op_int(cte, eom_len_max, ">", eom_len_ideal, 0, "%s: eom len consistency C (%d/%d/%d)", this_test_name, eom_len_min, eom_len_ideal, eom_len_max);

	cte->expect_op_int(cte, eoc_len_min, "<", eoc_len_max, 0, "%s: eoc len consistency A (%d/%d)", this_test_name, eoc_len_min, eoc_len_max);
	cte->expect_op_int(cte, eoc_len_min, "<", eoc_len_ideal, 0, "%s: eoc len consistency B (%d/%d/%d)", this_test_name, eoc_len_min, eoc_len_ideal, eoc_len_max);
	cte->expect_op_int(cte, eoc_len_max, ">", eoc_len_ideal, 0, "%s: eoc len consistency C (%d/%d/%d)", this_test_name, eoc_len_min, eoc_len_ideal, eoc_len_max);


	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   Parameter getters and setters are independent of audio system, so
   they can be tested just with CW_AUDIO_NULL.

   This function tests a single set of functions. This set is
   "special" because "get_value()" function returns float. Most of
   other "get_value()" functions in libcw return int.

   @reviewed on 2019-10-26
*/
int test_cw_rec_parameter_getters_setters_1(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	const char * this_test_name = "get/set param 1";

	cw_rec_t * rec = cw_rec_new();
	cte->assert2(cte, rec, "%s: failed to create new receiver\n", this_test_name);

	/* Test setting and getting of some basic parameters. */

	int off_limits = 10000;

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int * min, int * max);
		int (* set_new_value)(cw_rec_t * rec, int new_value);
		float (* get_value)(const cw_rec_t * rec);

		const int expected_min;   /* Expected value of minimum. */
		const int expected_max;   /* Expected value of maximum. */

		int readback_min; /* Value returned by 'get_limits()' function. */
		int readback_max; /* Value returned by 'get_limits()' function. */

		const char *name;
	} test_data[] = {
		{ cw_get_speed_limits, cw_rec_set_speed, cw_rec_get_speed, CW_SPEED_MIN, CW_SPEED_MAX, off_limits, -off_limits, "rec speed" },
		{ NULL,                NULL,             NULL,             0,            0,            0,          0,           NULL        }
	};

	bool get_failure = false;
	bool set_min_failure = false;
	bool set_max_failure = false;
	bool set_ok_failure = false;


	for (int i = 0; test_data[i].get_limits; i++) {

		int cwret = CW_FAILURE;
		int value = 0;

		/* Get limits of values to be tested. */
		test_data[i].get_limits(&test_data[i].readback_min, &test_data[i].readback_max);
		if (!cte->expect_op_int(cte, test_data[i].readback_min, "==", test_data[i].expected_min, 1, "%s: get min %s", this_test_name, test_data[i].name)) {
			get_failure = true;
			break;
		}
		if (!cte->expect_op_int(cte, test_data[i].readback_max, "==", test_data[i].expected_max, 1, "%s: get max %s", this_test_name, test_data[i].name)) {
			get_failure = true;
			break;
		}


		/* Test out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].readback_min - 1;
		cwret = test_data[i].set_new_value(rec, value);
		if (!cte->expect_op_int(cte, CW_FAILURE, "==", cwret, 1, "%s: setting %s value below minimum (cwret)", this_test_name, test_data[i].name)) {
			set_min_failure = true;
			break;
		}
		if (!cte->expect_op_int(cte, EINVAL, "==", errno, 1, "%s: setting %s value below minimum (errno)", this_test_name, test_data[i].name)) {
			set_min_failure = true;
			break;
		}


		/* Test out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].readback_max + 1;
		cwret = test_data[i].set_new_value(rec, value);
		if (!cte->expect_op_int(cte, CW_FAILURE, "==", cwret, 1, "%s: setting %s value above maximum (cwret)", this_test_name, test_data[i].name)) {
			set_max_failure = true;
			break;
		}
		if (!cte->expect_op_int(cte, EINVAL, "==", errno, 1, "%s: setting %s value above maximum (errno)", this_test_name, test_data[i].name)) {
			set_max_failure = true;
			break;
		}


		/* Test in-range values. Set with setter and then read back with getter. */
		errno = 0;
		for (int new_val = test_data[i].expected_min; new_val <= test_data[i].expected_max; new_val++) {
			test_data[i].set_new_value(rec, new_val);

			const float readback_value = test_data[i].get_value(rec);
			const float diff = readback_value - new_val;
			if (!cte->expect_op_double(cte, 0.01, ">", diff, 1, "%s: setting %s value in-range: %d (val)", this_test_name, test_data[i].name, new_val)) {
				set_ok_failure = true;
				break;
			}
			if (!cte->expect_op_int(cte, 0, "==", errno, 1, "%s: setting %s value in-range: %d (errno)", this_test_name, test_data[i].name, new_val)) {
				set_ok_failure = true;
				break;
			}
		}
		if (set_ok_failure) {
			break;
		}
	}

	cw_rec_delete(&rec);

	cte->expect_op_int(cte, false, "==", get_failure, 0, "%s: get", this_test_name);
	cte->expect_op_int(cte, false, "==", set_min_failure, 0, "%s: set value below min", this_test_name);
	cte->expect_op_int(cte, false, "==", set_max_failure, 0, "%s: set value above max", this_test_name);
	cte->expect_op_int(cte, false, "==", set_ok_failure, 0, "%s: set value in range", this_test_name);

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   Parameter getters and setters are independent of audio system, so
   they can be tested just with CW_AUDIO_NULL.

   This function tests a single set of functions. This set is
   "special" because "get_value()" function returns float. Most of
   other "get_value()" functions in libcw return int.

   @reviewed on 2019-10-26
*/
int test_cw_rec_parameter_getters_setters_2(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	const char * this_test_name = "get/set param 2";

	cw_rec_t * rec = cw_rec_new();
	cte->assert2(cte, rec, "%s: failed to create new receiver\n", this_test_name);

	/* Test setting and getting of some basic parameters. */

	int off_limits = 10000;

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int * min, int * max);
		int (* set_new_value)(cw_rec_t * rec, int new_value);
		int (* get_value)(const cw_rec_t * rec);

		const int expected_min;   /* Expected value of minimum. */
		const int expected_max;   /* Expected value of maximum. */

		int readback_min;   /* Value returned by 'get_limits()' function. */
		int readback_max;   /* Value returned by 'get_limits()' function. */

		const char *name;
	} test_data[] = {
		{ cw_get_tolerance_limits,  cw_rec_set_tolerance,  cw_rec_get_tolerance,  CW_TOLERANCE_MIN,  CW_TOLERANCE_MAX,  off_limits,  -off_limits,  "tolerance"     },
		{ NULL,                     NULL,                  NULL,                  0,                 0,                 0,            0,           NULL            }
	};

	bool get_failure = false;
	bool set_min_failure = false;
	bool set_max_failure = false;
	bool set_ok_failure = false;


	for (int i = 0; test_data[i].get_limits; i++) {

		int cwret = CW_FAILURE;
		int value = 0;

		/* Get limits of values to be tested. */
		test_data[i].get_limits(&test_data[i].readback_min, &test_data[i].readback_max);
		if (!cte->expect_op_int(cte, test_data[i].readback_min, "==", test_data[i].expected_min, 1, "%s: get min %s", this_test_name, test_data[i].name)) {
			get_failure = true;
			break;
		}
		if (!cte->expect_op_int(cte, test_data[i].readback_max, "==", test_data[i].expected_max, 1, "%s: get max %s", this_test_name, test_data[i].name)) {
			get_failure = true;
			break;
		}


		/* Test out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].readback_min - 1;
		cwret = test_data[i].set_new_value(rec, value);
		if (!cte->expect_op_int(cte, CW_FAILURE, "==", cwret, 1, "%s: setting %s value below minimum (cwret)", this_test_name, test_data[i].name)) {
			set_min_failure = true;
			break;
		}
		if (!cte->expect_op_int(cte, EINVAL, "==", errno, 1, "%s: setting %s value below minimum (errno)", this_test_name, test_data[i].name)) {
			set_min_failure = true;
			break;
		}



		/* Test out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].readback_max + 1;
		cwret = test_data[i].set_new_value(rec, value);
		if (!cte->expect_op_int(cte, CW_FAILURE, "==", cwret, 1, "%s: setting %s value above maximum (cwret)", this_test_name, test_data[i].name)) {
			set_max_failure = true;
			break;
		}
		if (!cte->expect_op_int(cte, EINVAL, "==", errno, 1, "%s: setting %s value above maximum (errno)", this_test_name, test_data[i].name)) {
			set_max_failure = true;
			break;
		}


		/* Test in-range values. Set with setter and then read back with getter. */
		errno = 0;
		for (int new_val = test_data[i].readback_min; new_val <= test_data[i].readback_max; new_val++) {
			test_data[i].set_new_value(rec, new_val);

			const int readback_value = test_data[i].get_value(rec);
			const int diff = readback_value - new_val;
			if (!cte->expect_op_int(cte, 1, ">", diff, 1, "%s: setting %s value in-range: %d (val)", this_test_name, test_data[i].name, new_val)) {
				set_ok_failure = true;
				break;
			}
			if (!cte->expect_op_int(cte, 0, "==", errno, 1, "%s: setting %s value in-range: %d (errno)", this_test_name, test_data[i].name, new_val)) {
				set_ok_failure = true;
				break;
			}
		}
		if (set_ok_failure) {
			break;
		}
	}

	cw_rec_delete(&rec);


	cte->expect_op_int(cte, false, "==", get_failure, 0, "%s: get", this_test_name);
	cte->expect_op_int(cte, false, "==", set_min_failure, 0, "%s: set value below min", this_test_name);
	cte->expect_op_int(cte, false, "==", set_max_failure, 0, "%s: set value above max", this_test_name);
	cte->expect_op_int(cte, false, "==", set_ok_failure, 0, "%s: set value in range", this_test_name);

	cte->print_test_footer(cte, __func__);

	return 0;
}
