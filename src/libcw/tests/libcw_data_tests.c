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
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>




#include "test_framework.h"

#include "libcw_data.h"
#include "libcw_data_tests.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_key.h"
#include "libcw.h"
#include "libcw2.h"




/* For maximum length of 7, there should be 254 items:
   2^1 + 2^2 + 2^3 + ... + 2^7 */
#define REPRESENTATION_TABLE_SIZE ((1 << (CW_DATA_MAX_REPRESENTATION_LENGTH + 1)) - 2)




extern const cw_entry_t CW_TABLE[];
extern const char * test_valid_representations[];
extern const char * test_invalid_representations[];




/**
   tests::cw_representation_to_hash_internal()

   The function builds every possible well formed representation no
   longer than 7 chars, and then calculates a hash of the
   representation. Since a representation is well formed, the tested
   function should calculate a hash.

   The function does not compare a representation and its hash to
   verify that patterns in representation and in hash match.

   TODO: add code that would compare the patterns of dots/dashes in
   representation against pattern of bits in hash.

   TODO: test calling the function with malformed representation.

   @reviewed on 2019-10-12
*/
int test_cw_representation_to_hash_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Intended contents of input[] is something like that:
	  input[0]  = "."
	  input[1]  = "-"
	  input[2]  = ".."
	  input[3]  = "-."
	  input[4]  = ".-"
	  input[5]  = "--"
	  input[6]  = "..."
	  input[7]  = "-.."
	  input[8]  = ".-."
	  input[9]  = "--."
	  input[10] = "..-"
	  input[11] = "-.-"
	  input[12] = ".--"
	  input[13] = "---"
	  .
	  .
	  .
	  input[248] = ".-.----"
	  input[249] = "--.----"
	  input[250] = "..-----"
	  input[251] = "-.-----"
	  input[252] = ".------"
	  input[253] = "-------"
	*/
	char input[REPRESENTATION_TABLE_SIZE][CW_DATA_MAX_REPRESENTATION_LENGTH + 1];

	/* Build table of all well formed representations ("well
	   formed" as in "built from dash and dot, no longer than
	   CW_DATA_MAX_REPRESENTATION_LENGTH"). */
	long int rep_idx = 0;
	for (unsigned int rep_length = 1; rep_length <= CW_DATA_MAX_REPRESENTATION_LENGTH; rep_length++) {

		/* Build representations of all lengths, starting from
		   shortest (single dot or dash) and ending with the
		   longest representations. */

		unsigned int bit_vector_length = 1 << rep_length;

		/* A representation of length "rep_length" can have
		   2^rep_length distinct variants. The "for" loop that
		   we are in iterates over these 2^len variants.

		   E.g. bit vector (and representation) of length 2
		   has 4 variants:
		   ..
		   .-
		   -.
		   --
		*/
		for (unsigned int variant = 0; variant < bit_vector_length; variant++) {

			/* Turn every '0' in 'variant' into dot, and every '1' into dash. */
			for (unsigned int bit_pos = 0; bit_pos < rep_length; bit_pos++) {
				unsigned int bit = variant & (1 << bit_pos);
				input[rep_idx][bit_pos] = bit ? '-' : '.';
				// fprintf(stderr, "rep = %x, bit pos = %d, bit = %d\n", variant, bit_pos, bit);
			}

			input[rep_idx][rep_length] = '\0';
			//fprintf(stderr, "input[%ld] = \"%s\"\n", rep_idx, input[rep_idx]);
			rep_idx++;
		}
	}
	const long int n_representations = rep_idx;
	cte->expect_op_int(cte, n_representations, "==", REPRESENTATION_TABLE_SIZE, 0, "internal count of representations");


	/* Compute hash for every well formed representation. */
	bool failure = false;
	for (int i = 0; i < n_representations; i++) {
		const uint8_t hash = LIBCW_TEST_FUT(cw_representation_to_hash_internal)(input[i]);
		/* The function returns values in range CW_DATA_MIN_REPRESENTATION_HASH - CW_DATA_MAX_REPRESENTATION_HASH. */
		if (!cte->expect_between_int_errors_only(cte, CW_DATA_MIN_REPRESENTATION_HASH, hash, CW_DATA_MAX_REPRESENTATION_HASH, "representation to hash: hash #%d\n", i)) {
			failure = true;
			break;
		}
	}
	cte->expect_op_int(cte, false, "==", failure, 0, "representation to hash");


	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   Verify that our fast lookup of characters works correctly.

   The verification is performed by comparing results of function
   using fast lookup table with results of function using direct
   method.

   tests::cw_representation_to_character_internal()

   @reviewed on 2019-10-12
*/
int test_cw_representation_to_character_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	bool failure = false;

	for (const cw_entry_t * cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {

		const int char_fast_lookup = LIBCW_TEST_FUT(cw_representation_to_character_internal)(cw_entry->representation);
		const int char_direct = LIBCW_TEST_FUT(cw_representation_to_character_direct_internal)(cw_entry->representation);

		if (!cte->expect_op_int(cte, char_fast_lookup, "==", char_direct, "fast lookup vs. direct method: '%s'", cw_entry->representation)) {
			failure = true;
			break;
		}
	}

	cte->expect_op_int(cte, false, "==", failure, 0, "representation to character");

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   Testing speed gain between function using direct method, and
   function with fast lookup table.  Test is preformed by using timer
   to see how much time it takes to execute a function N times.

   @reviewed on 2019-10-12
*/
int test_cw_representation_to_character_internal_speed(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);


	const int N = 1000;

	struct timeval start;
	struct timeval stop;


	gettimeofday(&start, NULL);
	for (int i = 0; i < N; i++) {
		for (const cw_entry_t * cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			__attribute__((unused)) int rv = cw_representation_to_character_internal(cw_entry->representation);
		}
	}
	gettimeofday(&stop, NULL);
	const int fast_lookup = cw_timestamp_compare_internal(&start, &stop);



	gettimeofday(&start, NULL);
	for (int i = 0; i < N; i++) {
		for (const cw_entry_t * cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			__attribute__((unused)) int rv = cw_representation_to_character_direct_internal(cw_entry->representation);
		}
	}
	gettimeofday(&stop, NULL);
	const int direct = cw_timestamp_compare_internal(&start, &stop);


	const float gain = 1.0 * direct / fast_lookup;
	bool failure = gain < 1.1;
	cte->expect_op_int(cte, false, "==", failure, 0, "lookup speed gain: %.2f", gain);

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   \brief Test functions looking up characters and their representation.

   tests::cw_get_character_count()
   tests::cw_list_characters()
   tests::cw_get_maximum_representation_length()
   tests::cw_character_to_representation()
   tests::cw_representation_to_character()

   @reviewed on 2019-10-12
*/
int test_character_lookups_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);


	/* Test: get number of characters known to libcw. */
	{
		/* libcw doesn't define a constant describing the
		   number of known/supported/recognized characters,
		   but there is a function calculating the number. One
		   thing is certain: the number is larger than
		   zero. */
		const int extracted_count = LIBCW_TEST_FUT(cw_get_character_count)();
		cte->expect_op_int(cte, 0, "<", extracted_count, 0, "character count (%d)", extracted_count);
	}


	char charlist[UCHAR_MAX + 1] = { 0 };
	/* Test: get list of characters supported by libcw. */
	{
		/* Of course length of the list must match the
		   character count returned by library. */

		LIBCW_TEST_FUT(cw_list_characters)(charlist);

		const int extracted_count = cw_get_character_count();
		const int extracted_len = (int) strlen(charlist);

		cte->log_info(cte, "list of characters: %s\n", charlist);

		cte->expect_op_int(cte, extracted_len, "==", extracted_count, 0, "character count = %d, list length = %d", extracted_count, extracted_len);
	}



	/* Test: get maximum length of a representation (a string of dots/dashes). */
	int max_rep_length = 0;
	{
		/* This test is rather not related to any other, but
		   since we are doing tests of other functions related
		   to representations, let's do this as well. */

		max_rep_length = LIBCW_TEST_FUT(cw_get_maximum_representation_length)();
		cte->expect_op_int(cte, 0, "<", max_rep_length, 0, "maximum representation length (%d)", max_rep_length);
	}



	/* Test: character <--> representation lookup. */
	{
		bool c2r_failure = false;
		bool r2c_failure = false;
		bool two_way_failure = false;
		bool length_failure = false;

		/* For each character, look up its representation, the
		   look up each representation in the opposite
		   direction. */

		for (int i = 0; charlist[i] != '\0'; i++) {

			char * representation = LIBCW_TEST_FUT(cw_character_to_representation)(charlist[i]);
			if (!cte->expect_valid_pointer_errors_only(cte, representation, "character lookup: character to representation for #%d (char '%c')\n", i, charlist[i])) {
				c2r_failure = true;
				break;
			}

			/* Here we convert the representation back into a character. */
			char character = LIBCW_TEST_FUT(cw_representation_to_character)(representation);
			if (!cte->expect_op_int(cte, 0, "!=", character, 1, "representation to character failed for #%d (representation '%s')\n", i, representation)) {
				r2c_failure = true;
				break;
			}

			/* Compare output char with input char. */
			if (!cte->expect_op_int(cte, character, "==", charlist[i], 1, "character lookup: two-way lookup for #%d ('%c' -> '%s' -> '%c')\n", i, charlist[i], representation, character)) {
				two_way_failure = true;
				break;
			}

			const int length = (int) strlen(representation);
			const int rep_length_lower = 1; /* A representation will have at least one character. */
			const int rep_length_upper = max_rep_length;
			if (!cte->expect_between_int_errors_only(cte, rep_length_lower, length, rep_length_upper, "character lookup: representation length of character '%c' (#%d)", charlist[i], i)) {
				length_failure = true;
				break;
			}

			free(representation);
			representation = NULL;
		}

		cte->expect_op_int(cte, false, "==", c2r_failure, 0, "character lookup: char to representation");
		cte->expect_op_int(cte, false, "==", r2c_failure, 0, "character lookup: representation to char");
		cte->expect_op_int(cte, false, "==", two_way_failure, 0, "character lookup: two-way lookup");
		cte->expect_op_int(cte, false, "==", length_failure, 0, "character lookup: length");
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   \brief Test functions looking up procedural characters and their representation.

   tests::cw_get_procedural_character_count()
   tests::cw_list_procedural_characters()
   tests::cw_get_maximum_procedural_expansion_length()
   tests::cw_lookup_procedural_character()

   @revieded on 2019-10-12
*/
int test_prosign_lookups_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Collect and print out a list of characters in the
	   procedural signals expansion table. */

	int count = 0; /* Number of prosigns. */

	/* Test: get number of prosigns known to libcw. */
	{
		count = LIBCW_TEST_FUT(cw_get_procedural_character_count)();
		cte->expect_op_int(cte, 0, "<", count, true, "procedural character count (%d):", count);
	}



	char procedural_characters[UCHAR_MAX + 1] = { 0 };
	/* Test: get list of characters supported by libcw. */
	{
		LIBCW_TEST_FUT(cw_list_procedural_characters)(procedural_characters); /* TODO: we need a version of the function that accepts size of buffer as argument. */
		cte->log_info(cte, "list of procedural characters: %s\n", procedural_characters);

		const int extracted_len = (int) strlen(procedural_characters);
		const int extracted_count = cw_get_procedural_character_count();

		cte->expect_op_int(cte, extracted_count, "==", extracted_len, 0, "procedural character count = %d, list length = %d", extracted_count, extracted_len);
	}



	/* Test: expansion length. */
	int max_expansion_length = 0;
	{
		max_expansion_length = LIBCW_TEST_FUT(cw_get_maximum_procedural_expansion_length)();
		cte->expect_op_int(cte, 0, "<", max_expansion_length, 0, "maximum procedural expansion length (%d)", max_expansion_length);
	}



	/* Test: lookup. */
	{
		/* For each procedural character, look up its
		   expansion, verify its length, and check a
		   true/false assignment to the display hint. */

		bool lookup_failure = false;
		bool length_failure = false;
		bool expansion_failure = false;

		for (int i = 0; procedural_characters[i] != '\0'; i++) {
			char expansion[256] = { 0 };
			int is_usually_expanded = -1; /* This value should be set by libcw to either 0 (false) or 1 (true). */

			const int cwret = LIBCW_TEST_FUT(cw_lookup_procedural_character)(procedural_characters[i], expansion, &is_usually_expanded);
			if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "procedural character lookup: lookup of character '%c' (#%d)", procedural_characters[i], i)) {
				lookup_failure = true;
				break;
			}

			const int length = (int) strlen(expansion);

			if (!cte->expect_between_int_errors_only(cte, 2, length, max_expansion_length, "procedural character lookup: expansion length of character '%c' (#%d)", procedural_characters[i], i)) {
				length_failure = true;
				break;
			}

			/* Check if call to tested function has modified the flag. */
			if (!cte->expect_op_int(cte, -1, "!=", is_usually_expanded, 1, "procedural character lookup: expansion hint of character '%c' ((#%d)\n", procedural_characters[i], i)) {
				expansion_failure = true;
				break;
			}
		}

		cte->expect_op_int(cte, false, "==", lookup_failure, 0, "procedural character lookup: lookup");
		cte->expect_op_int(cte, false, "==", length_failure, 0, "procedural character lookup: length");
		cte->expect_op_int(cte, false, "==", expansion_failure, 0, "procedural character lookup: expansion flag");
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   tests::cw_get_maximum_phonetic_length()
   tests::cw_lookup_phonetic()

   @reviewed on 2019-10-12
*/
int test_phonetic_lookups_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* For each ASCII character, look up its phonetic and check
	   for a string that start with this character, if alphabetic,
	   and false otherwise. */

	/* Test: check that maximum phonetic length is larger than
	   zero. */
	{
		const int length = LIBCW_TEST_FUT(cw_get_maximum_phonetic_length)();
		const bool failure = (length <= 0);
		cte->expect_op_int(cte, false, "==", failure, 0, "phonetic lookup: maximum phonetic length (%d)", length);
	}


	/* Test: lookup of phonetic + reverse lookup. */
	{
		bool lookup_failure = false;
		bool reverse_failure = false;

		/* Notice that We go here through all possible values
		   of char, not through all values returned from
		   cw_list_characters(). */
		for (int i = 0; i < UCHAR_MAX; i++) {
			char phonetic[sizeof ("VeryLongPhoneticString")] = { 0 };

			const int cwret = LIBCW_TEST_FUT(cw_lookup_phonetic)((char) i, phonetic); /* TODO: we need a version of the function that accepts size argument. */
			const bool is_alpha = (bool) isalpha(i);;
			if (CW_SUCCESS == cwret) {
				/*
				  Library claims that 'i' is a byte
				  that has a phonetic (e.g. 'F' ->
				  "Foxtrot").

				  Let's verify this using result of
				  isalpha().
				*/
				if (!cte->expect_op_int(cte, true, "==", is_alpha, 1, "phonetic lookup (A): lookup of phonetic for '%c' (#%d)\n", (char) i, i)) {
					lookup_failure = true;
					break;
				}
			} else {
				/*
				  Library claims that 'i' is a byte
				  that doesn't have a phonetic.

				  Let's verify this using result of
				  isalpha().
				*/
				const bool is_alpha = (bool) isalpha(i);
				if (!cte->expect_op_int(cte, false, "==", is_alpha, 1, "phonetic lookup (B): lookup of phonetic for '%c' (#%d)\n", (char) i, i)) {
					lookup_failure = true;
					break;
				}
			}


			if (CW_SUCCESS == cwret && is_alpha) {
				/* We have looked up a letter, it has
				   a phonetic.  Almost by definition,
				   the first letter of phonetic should
				   be the same as the looked up
				   letter. */
				reverse_failure = (phonetic[0] != toupper((char) i));
				if (!cte->expect_op_int(cte, false, "==", reverse_failure, 1, "phonetic lookup: reverse lookup for phonetic \"%s\" ('%c' / #%d)\n", phonetic, (char) i, i)) {
					reverse_failure = true;
					break;
				}
			}
		}

		cte->expect_op_int(cte, false, "==", lookup_failure, 0, "phonetic lookup: lookup");
		cte->expect_op_int(cte, false, "==", reverse_failure, 0, "phonetic lookup: reverse lookup");
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   Validate all supported characters individually

   tests::cw_character_is_valid()

   @reviewed on 2019-10-11
*/
int test_validate_character_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Test: validation of individual characters. */

	bool failure_valid = false;
	bool failure_invalid = false;

	char charlist[UCHAR_MAX + 1];
	cw_list_characters(charlist);

	for (int i = 0; i < UCHAR_MAX; i++) {
		if (i == '\b') {
			/* Here we have a valid character, that is
			   not 'sendable' but can be handled by libcw
			   nevertheless. cw_character_is_valid() should
			   confirm it. */
			const bool is_valid = LIBCW_TEST_FUT(cw_character_is_valid)(i);

			if (!cte->expect_op_int(cte, true, "==", is_valid, 1, "validate character: valid character '<backspace>' / #%d not recognized as valid\n", i)) {
				failure_valid = true;
				break;
			}
		} else if (i == ' ' || (i != 0 && strchr(charlist, toupper(i)) != NULL)) {

			/* Here we have a valid character, that is
			   recognized/supported as 'sendable' by
			   libcw.  cw_character_is_valid() should
			   confirm it. */
			const bool is_valid = LIBCW_TEST_FUT(cw_character_is_valid)(i);
			if (!cte->expect_op_int(cte, true, "==", is_valid, 1, "validate character: valid character '%c' / #%d not recognized as valid\n", (char ) i, i)) {
				failure_valid = true;
				break;
			}
		} else {
			/* The 'i' character is not
			   recognized/supported by libcw.
			   cw_character_is_valid() should return false
			   to signify that the char is invalid. */
			const bool is_valid = LIBCW_TEST_FUT(cw_character_is_valid)(i);
			if (!cte->expect_op_int(cte, false, "==", is_valid, 1, "validate character: invalid character '%c' / #%d recognized as valid\n", (char ) i, i)) {
				failure_invalid = true;
				break;
			}
		}
	}

	cte->expect_op_int(cte, false, "==", failure_valid, 0, "validate character: valid characters");
	cte->expect_op_int(cte, false, "==", failure_invalid, 0, "validate character: invalid characters");

	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   Validate all supported characters placed in a string

   tests::cw_string_is_valid()

   @reviewed on 2019-10-11
*/
int test_validate_string_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Test: validation of string as a whole. */

	bool are_we_valid = false;
	/* Check the whole charlist item as a single string,
	   then check a known invalid string. */


	char charlist[UCHAR_MAX + 1];
	cw_list_characters(charlist);
	are_we_valid = LIBCW_TEST_FUT(cw_string_is_valid)(charlist);
	cte->expect_op_int(cte, true, "==", are_we_valid, 0, "validate string: valid string");


	/* Test invalid string. */
	are_we_valid = LIBCW_TEST_FUT(cw_string_is_valid)("%INVALID%");
	cte->expect_op_int(cte, false, "==", are_we_valid, 0, "validate string: invalid string");


	cte->print_test_footer(cte, __func__);

	return 0;
}




/**
   \brief Validating representations of characters

   tests::cw_representation_is_valid()

   @reviewed on 2019-10-11
*/
int test_validate_representation_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Test: validating valid representations. */
	{
		int i = 0;
		bool failure = false;
		while (test_valid_representations[i]) {
			const int cwret = LIBCW_TEST_FUT(cw_representation_is_valid)(test_valid_representations[i]);
			if (!cte->expect_op_int(cte, CW_SUCCESS, "==", cwret, 1, "valid representations (i = %d)", i)) {
				failure = false;
				break;
			}
			i++;
		}
		cte->expect_op_int(cte, false, "==", failure, 0, "valid representations");
	}


	/* Test: validating invalid representations. */
	{
		int i = 0;
		bool failure = false;
		while (test_invalid_representations[i]) {
			const int cwret = LIBCW_TEST_FUT(cw_representation_is_valid)(test_invalid_representations[i]);
			if (!cte->expect_op_int(cte, CW_FAILURE, "==", cwret, 1, "invalid representations (i = %d)", i)) {
				failure = false;
				break;
			}
			i++;
		}
		cte->expect_op_int(cte, false, "==", failure, 0, "invalid representations");
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}
