/*
  This file is a part of unixcw project.  unixcw project is covered by
  GNU General Public License, version 2 or later.
*/

#ifndef _LIBCW_DATA_TESTS_H_
#define _LIBCW_DATA_TESTS_H_




#include "test_framework.h"




int test_cw_representation_to_hash_internal(cw_test_executor_t * cte);
int test_cw_representation_to_character_internal(cw_test_executor_t * cte);
int test_cw_representation_to_character_internal_speed(cw_test_executor_t * cte);
int test_character_lookups_internal(cw_test_executor_t * cte);
int test_prosign_lookups_internal(cw_test_executor_t * cte);
int test_phonetic_lookups_internal(cw_test_executor_t * cte);
int test_validate_character_internal(cw_test_executor_t * cte);
int test_validate_string_internal(cw_test_executor_t * cte);
int test_validate_representation_internal(cw_test_executor_t * cte);




#endif /* #ifndef _LIBCW_DATA_TESTS_H_ */
