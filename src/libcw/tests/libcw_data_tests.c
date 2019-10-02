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




#define MSG_PREFIX "libcw/data: "




/* For maximum length of 7, there should be 254 items:
   2^1 + 2^2 + 2^3 + ... + 2^7 */
#define REPRESENTATION_TABLE_SIZE ((2 << (CW_DATA_MAX_REPRESENTATION_LENGTH + 1)) - 1)




extern const cw_entry_t CW_TABLE[];




/**
   tests::cw_representation_to_hash_internal()

   The function builds every possible valid representation no longer
   than 7 chars, and then calculates a hash of the
   representation. Since a representation is valid, the tested
   function should calculate a valid hash.

   The function does not compare a representation and its hash to
   verify that patterns in representation and in hash match.

   TODO: add code that would compare the patterns of dots/dashes in
   representation against pattern of bits in hash.

   TODO: test calling the function with invalid representation.
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

	/* build table of all valid representations ("valid" as in "built
	   from dash and dot, no longer than CW_DATA_MAX_REPRESENTATION_LENGTH"). */
	long int rep = 0;
	for (unsigned int len = 1; len <= CW_DATA_MAX_REPRESENTATION_LENGTH; len++) {

		/* Build representations of all lengths, starting from
		   shortest (single dot or dash) and ending with the
		   longest representations. */

		unsigned int bit_vector_len = 2 << (len - 1);

		/* A representation of length "len" can have 2^len
		   distinct values. The "for" loop that we are in
		   iterates over these 2^len forms. */
		for (unsigned int bit_vector = 0; bit_vector < bit_vector_len; bit_vector++) {

			/* Turn every '0' into dot, and every '1' into dash. */
			for (unsigned int bit_pos = 0; bit_pos < len; bit_pos++) {
				unsigned int bit = bit_vector & (1 << bit_pos);
				input[rep][bit_pos] = bit ? '-' : '.';
				// fprintf(stderr, "rep = %x, bit pos = %d, bit = %d\n", bit_vector, bit_pos, bit);
			}

			input[rep][len] = '\0';
			//fprintf(stderr, "\ninput[%ld] = \"%s\"", rep, input[rep]);
			rep++;
		}
	}

	/* Compute hash for every valid representation. */
	for (int i = 0; i < rep; i++) {
		const uint8_t hash = cw_representation_to_hash_internal(input[i]);
		/* The function returns values in range CW_DATA_MIN_REPRESENTATION_HASH - CW_DATA_MAX_REPRESENTATION_HASH. */
		const bool failure = (hash < (uint8_t) CW_DATA_MIN_REPRESENTATION_HASH) || (hash > (uint8_t) CW_DATA_MAX_REPRESENTATION_HASH);
		if (!cte->expect_eq_int(cte, false, failure, "representation to hash: Invalid hash #%d: %u (min = %u, max = %u)\n", i, hash, CW_DATA_MIN_REPRESENTATION_HASH, CW_DATA_MAX_REPRESENTATION_HASH)) {
			break;
		}
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   tests::cw_representation_to_character_internal()
*/
int test_cw_representation_to_character_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	bool failure = false;

	/* The test is performed by comparing results of function
	   using fast lookup table, and function using direct
	   lookup. */

	for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {

		const int lookup = cw_representation_to_character_internal(cw_entry->representation);
		const int direct = cw_representation_to_character_direct_internal(cw_entry->representation);

		if (!cte->expect_eq_int_errors_only(cte, lookup, direct, "lookup vs. direct: '%s'", cw_entry->representation)) {
			failure = true;
			break;
		}
	}

	cte->expect_eq_int(cte, false, failure, "representation to character");

	cte->print_test_footer(cte, __func__);

	return 0;
}






int test_cw_representation_to_character_internal_speed(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Testing speed gain between function with direct lookup, and
	   function with fast lookup table.  Test is preformed by
	   running each function N times with timer started before the
	   N runs and stopped after N runs. */

	int N = 1000;

	struct timeval start;
	struct timeval stop;


	gettimeofday(&start, NULL);
	for (int i = 0; i < N; i++) {
		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			__attribute__((unused)) int rv = cw_representation_to_character_internal(cw_entry->representation);
		}
	}
	gettimeofday(&stop, NULL);

	int lookup = cw_timestamp_compare_internal(&start, &stop);



	gettimeofday(&start, NULL);
	for (int i = 0; i < N; i++) {
		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			__attribute__((unused)) int rv = cw_representation_to_character_direct_internal(cw_entry->representation);
		}
	}
	gettimeofday(&stop, NULL);

	int direct = cw_timestamp_compare_internal(&start, &stop);


	float gain = 1.0 * direct / lookup;
	bool failure = gain < 1.1;
	cte->expect_eq_int(cte, false, failure, "lookup speed gain: %.2f", gain);

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
*/
int test_character_lookups_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	bool failure = true;

	/* Test: get number of characters known to libcw. */
	{
		/* libcw doesn't define a constant describing the
		   number of known/supported/recognized characters,
		   but there is a function calculating the number. One
		   thing is certain: the number is larger than
		   zero. */
		const int extracted_count = cw_get_character_count();
		failure = (extracted_count <= 0);
		cte->expect_eq_int(cte, false, failure, "character count (%d):", extracted_count);
	}


	char charlist[UCHAR_MAX + 1];
	/* Test: get list of characters supported by libcw. */
	{
		/* Of course length of the list must match the
		   character count returned by library. */

		const int extracted_count = cw_get_character_count();

		cw_list_characters(charlist);
		fprintf(out_file, MSG_PREFIX "list of characters: %s\n", charlist);
		const int extracted_len = (int) strlen(charlist);

		cte->expect_eq_int(cte, extracted_len, extracted_count, "character count = %d, list length = %d", extracted_count, extracted_len);
	}



	/* Test: get maximum length of a representation (a string of dots/dashes). */
	{
		/* This test is rather not related to any other, but
		   since we are doing tests of other functions related
		   to representations, let's do this as well. */

		int rep_len = cw_get_maximum_representation_length();
		failure = (rep_len <= 0);
		cte->expect_eq_int(cte, false, failure, "maximum representation length (%d):", rep_len);
	}



	/* Test: character <--> representation lookup. */
	{
		bool c2r_failure = false;
		bool r2c_failure = false;
		bool two_way_failure = false;

		/* For each character, look up its representation, the
		   look up each representation in the opposite
		   direction. */

		for (int i = 0; charlist[i] != '\0'; i++) {

			char * representation = cw_character_to_representation(charlist[i]);
			if (!cte->expect_valid_pointer_errors_only(cte, representation, "character lookup: character to representation failed for #%d (char '%c')\n", i, charlist[i])) {
				c2r_failure = true;
				break;
			}

			/* Here we convert the representation into an output char 'c'. */
			char c = cw_representation_to_character(representation);
			r2c_failure = (0 == c);
			if (!cte->expect_eq_int_errors_only(cte, false, r2c_failure, "representation to character failed for #%d (representation '%s')\n", i, representation)) {
				r2c_failure = true;
				break;
			}

			/* Compare output char with input char. */
			if (!cte->expect_eq_int_errors_only(cte, c, charlist[i], "character lookup: two-way lookup for #%d ('%c' -> '%s' -> '%c')\n", i, charlist[i], representation, c)) {
				two_way_failure = true;
				break;
			}

			free(representation);
			representation = NULL;
		}

		cte->expect_eq_int(cte, false, c2r_failure, "character lookup: char to representation");
		cte->expect_eq_int(cte, false, r2c_failure, "character lookup: representation to char:");
		cte->expect_eq_int(cte, false, two_way_failure, "character lookup: two-way lookup");
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
*/
int test_prosign_lookups_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Collect and print out a list of characters in the
	   procedural signals expansion table. */

	int count = 0; /* Number of prosigns. */
	bool failure = true;

	/* Test: get number of prosigns known to libcw. */
	{
		count = cw_get_procedural_character_count();
		failure = (count <= 0);
		cte->expect_eq_int(cte, false, failure, "procedural character count (%d):", count);
	}



	char charlist[UCHAR_MAX + 1];
	/* Test: get list of characters supported by libcw. */
	{
		cw_list_procedural_characters(charlist);
		fprintf(out_file, MSG_PREFIX "list of procedural characters: %s\n", charlist);
		const int extracted_len = (int) strlen(charlist);

		const int extracted_count = cw_get_procedural_character_count();

		cte->expect_eq_int(cte, extracted_count, extracted_len, "procedural character count = %d, list length = %d", extracted_count, extracted_len);
	}



	/* Test: expansion length. */
	{
		int exp_len = cw_get_maximum_procedural_expansion_length();
		failure = (exp_len <= 0);
		cte->expect_eq_int(cte, false, failure, "maximum procedural expansion length (%d):", (int) exp_len);
	}



	/* Test: lookup. */
	{
		/* For each procedural character, look up its
		   expansion and check for two or three characters,
		   and a true/false assignment to the display hint. */

		bool lookup_failure = false;
		bool check_failure = false;

		for (int i = 0; charlist[i] != '\0'; i++) {
			char expansion[256];
			int is_usually_expanded = -1;

			const int cwret = cw_lookup_procedural_character(charlist[i], expansion, &is_usually_expanded);
			if (!cte->expect_eq_int_errors_only(cte, CW_SUCCESS, cwret, "procedural character lookup: lookup of character '%c' (#%d)", charlist[i], i)) {
				lookup_failure = true;
				break;
			}


			/* TODO: comment, please. */
			if ((strlen(expansion) != 2 && strlen(expansion) != 3)
			    || is_usually_expanded == -1) {

				// cte->expect_eq_int_errors_only(cte, ); // TODO: implement
				check_failure = true;
				fprintf(out_file, MSG_PREFIX "procedural character lookup: expansion check failed (#%d)\n", i);
				break;
			}
		}

		cte->expect_eq_int(cte, false, lookup_failure, "procedural character lookup: lookup");
		cte->expect_eq_int(cte, false, check_failure, "procedural character lookup: lookup check");
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   tests::cw_get_maximum_phonetic_length()
   tests::cw_lookup_phonetic()
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
		int len = cw_get_maximum_phonetic_length();
		bool failure = (len <= 0);
		cte->expect_eq_int(cte, false, failure, "phonetic lookup: maximum phonetic length (%d):", len);
	}


	/* Test: lookup of phonetic + reverse lookup. */
	{
		bool lookup_failure = true;
		bool reverse_failure = true;

		for (int i = 0; i < UCHAR_MAX; i++) {
			char phonetic[256];

			int status = cw_lookup_phonetic((char) i, phonetic);
			lookup_failure = (status != (bool) isalpha(i));
			if (!cte->expect_eq_int_errors_only(cte, false, lookup_failure, "phonetic lookup: lookup of phonetic '%c' (#%d) failed\n", (char ) i, i)) {
				lookup_failure = true;
				break;
			}

			if (status && (bool) isalpha(i)) {
				/* We have looked up a letter, it has
				   a phonetic.  Almost by definition,
				   the first letter of phonetic should
				   be the same as the looked up
				   letter. */
				reverse_failure = (phonetic[0] != toupper((char) i));
				if (!cte->expect_eq_int_errors_only(cte, false, reverse_failure, "phonetic lookup: reverse lookup failed for phonetic \"%s\" ('%c' / #%d)\n", phonetic, (char) i, i)) {
					reverse_failure = true;
					break;
				}
			}
		}

		cte->expect_eq_int(cte, false, lookup_failure, "phonetic lookup: lookup");
		cte->expect_eq_int(cte, false, reverse_failure, "phonetic lookup: reverse lookup");
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   Validate all supported characters, first each characters individually, then as a string.

   tests::cw_character_is_valid()
   tests::cw_string_is_valid()
*/
int test_validate_character_and_string_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Test: validation of individual characters. */
	{
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
				const bool is_valid = cw_character_is_valid(i);

				if (!cte->expect_eq_int_errors_only(cte, true, is_valid, "validate character: valid character '<backspace>' / #%d not recognized as valid\n", i)) {
					failure_valid = true;
					break;
				}
			} else if (i == ' ' || (i != 0 && strchr(charlist, toupper(i)) != NULL)) {

				/* Here we have a valid character, that is
				   recognized/supported as 'sendable' by
				   libcw.  cw_character_is_valid() should
				   confirm it. */
				const bool is_valid = cw_character_is_valid(i);
				if (!cte->expect_eq_int_errors_only(cte, true, is_valid, "validate character: valid character '%c' / #%d not recognized as valid\n", (char ) i, i)) {
					failure_valid = true;
					break;
				}
			} else {
				/* The 'i' character is not
				   recognized/supported by libcw.
				   cw_character_is_valid() should return false
				   to signify that the char is invalid. */
				const bool is_valid = cw_character_is_valid(i);
				if (!cte->expect_eq_int_errors_only(cte, false, is_valid, "validate character: invalid character '%c' / #%d recognized as valid\n", (char ) i, i)) {
					failure_invalid = true;
					break;
				}
			}
		}

		cte->expect_eq_int(cte, false, failure_valid, "validate character: valid characters");
		cte->expect_eq_int(cte, false, failure_invalid, "validate character: invalid characters:");
	}



	/* Test: validation of string as a whole. */
	{
		bool are_we_valid = false;
		/* Check the whole charlist item as a single string,
		   then check a known invalid string. */

		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);
		are_we_valid = cw_string_is_valid(charlist);
		cte->expect_eq_int(cte, true, are_we_valid, "validate string: valid string");

		/* Test invalid string. */
		are_we_valid = cw_string_is_valid("%INVALID%");
		cte->expect_eq_int(cte, false, are_we_valid, "validate string: invalid string");
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}





/**
   \brief Validating representations of characters

   tests::cw_representation_is_valid()
*/
int test_validate_representation_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Test: validating valid representations. */
	{
		int rv1 = cw_representation_is_valid(".-.-.-");
		int rv2 = cw_representation_is_valid(".-");
		int rv3 = cw_representation_is_valid("---");
		int rv4 = cw_representation_is_valid("...-");

		bool failure = (rv1 != CW_SUCCESS) || (rv2 != CW_SUCCESS) || (rv3 != CW_SUCCESS) || (rv4 != CW_SUCCESS);
		cte->expect_eq_int(cte, false, failure, "validate representation: valid (%d/%d/%d/%d):", rv1, rv2, rv3, rv4);
	}


	/* Test: validating invalid representations. */
	{
		int rv1 = cw_representation_is_valid("INVALID");
		int rv2 = cw_representation_is_valid("_._");
		int rv3 = cw_representation_is_valid("-_-");

		bool failure = (rv1 == CW_SUCCESS) || (rv2 == CW_SUCCESS) || (rv3 == CW_SUCCESS);
		cte->expect_eq_int(cte, false, failure, "validate representation: invalid (%d/%d/%d):", rv1, rv2, rv3);
	}

	cte->print_test_footer(cte, __func__);

	return 0;
}
