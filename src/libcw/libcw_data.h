/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef H_LIBCW_DATA
#define H_LIBCW_DATA




#include <stdbool.h>
#include <stdint.h>




/* Functions handling representation of a character.
   Representation looks like this: ".-" for "a", "--.." for "z", etc. */
int          cw_representation_to_character_internal(const char * representation);
const char * cw_character_to_representation_internal(int c);




#ifdef LIBCW_UNIT_TESTS

#include "libcw_test.h"

unsigned int test_cw_representation_to_hash_internal(cw_test_stats_t * stats);
unsigned int test_cw_representation_to_character_internal(cw_test_stats_t * stats);
unsigned int test_cw_representation_to_character_internal_speed(cw_test_stats_t * stats);
unsigned int test_character_lookups_internal(cw_test_stats_t * stats);
unsigned int test_prosign_lookups_internal(cw_test_stats_t * stats);
unsigned int test_phonetic_lookups_internal(cw_test_stats_t * stats);
unsigned int test_validate_character_and_string_internal(cw_test_stats_t * stats);
unsigned int test_validate_representation_internal(cw_test_stats_t * stats);

#endif /* #ifdef LIBCW_UNIT_TESTS */




#endif /* #ifndef H_LIBCW_DATA */
