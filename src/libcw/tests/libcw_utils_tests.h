/*
  This file is a part of unixcw project.  unixcw project is covered by
  GNU General Public License, version 2 or later.
*/

#ifndef _LIBCW_UTILS_TESTS_H_
#define _LIBCW_UTILS_TESTS_H_




#include "test_framework.h"




int test_cw_timestamp_compare_internal(cw_test_executor_t * cte);
int test_cw_timestamp_validate_internal(cw_test_executor_t * cte);
int test_cw_usecs_to_timespec_internal(cw_test_executor_t * cte);
int test_cw_version_internal(cw_test_executor_t * cte);
int test_cw_license_internal(cw_test_executor_t * cte);




#endif /* #ifndef _LIBCW_UTILS_TESTS_H_ */
