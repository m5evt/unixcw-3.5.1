/*
  This file is a part of unixcw project.  unixcw project is covered by
  GNU General Public License, version 2 or later.
*/

#ifndef _LIBCW_REC_TESTS_H_
#define _LIBCW_REC_TESTS_H_




#include "test_framework.h"




int test_cw_rec_with_base_data_fixed(void);
int test_cw_rec_with_random_data_fixed(void);
int test_cw_rec_with_random_data_adaptive(void);
int test_cw_get_receive_parameters(void);

int test_cw_rec_identify_mark_internal(cw_test_executor_t * cte);
int test_cw_rec_test_with_constant_speeds(cw_test_executor_t * cte);
int test_cw_rec_test_with_varying_speeds(cw_test_executor_t * cte);
int test_cw_rec_get_parameters(cw_test_executor_t * cte);
int test_cw_rec_parameter_getters_setters_1(cw_test_executor_t * cte);
int test_cw_rec_parameter_getters_setters_2(cw_test_executor_t * cte);




#endif /* #ifndef _LIBCW_REC_TESTS_H_ */
