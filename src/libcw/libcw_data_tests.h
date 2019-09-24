#ifndef _LIBCW_DATA_TESTS_H_
#define _LIBCW_DATA_TESTS_H_




#include "tests/libcw_test_utils.h"




unsigned int test_cw_representation_to_hash_internal(cw_test_stats_t * stats);
unsigned int test_cw_representation_to_character_internal(cw_test_stats_t * stats);
unsigned int test_cw_representation_to_character_internal_speed(cw_test_stats_t * stats);
unsigned int test_character_lookups_internal(cw_test_stats_t * stats);
unsigned int test_prosign_lookups_internal(cw_test_stats_t * stats);
unsigned int test_phonetic_lookups_internal(cw_test_stats_t * stats);
unsigned int test_validate_character_and_string_internal(cw_test_stats_t * stats);
unsigned int test_validate_representation_internal(cw_test_stats_t * stats);




#endif /* #ifndef _LIBCW_DATA_TESTS_H_ */
