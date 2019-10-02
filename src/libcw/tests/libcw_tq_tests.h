/*
  This file is a part of unixcw project.  unixcw project is covered by
  GNU General Public License, version 2 or later.
*/

#ifndef _LIBCW_TQ_TESTS_H_
#define _LIBCW_TQ_TESTS_H_




#include "test_framework.h"

#include "libcw_gen.h"




int test_cw_tq_init_internal(void);
int test_cw_tq_enqueue_internal_2(cw_test_executor_t * cte);
int test_cw_tq_test_capacity_1(cw_test_executor_t * cte);
int test_cw_tq_test_capacity_2(cw_test_executor_t * cte);
int test_cw_tq_wait_for_level_internal(cw_test_executor_t * cte);
int test_cw_tq_is_full_internal(cw_test_executor_t * cte);
int test_cw_tq_enqueue_dequeue_internal(cw_test_executor_t * cte);
int test_cw_tq_enqueue_args_internal(cw_test_executor_t * cte);
int test_cw_tq_new_delete_internal(cw_test_executor_t * cte);
int test_cw_tq_get_capacity_internal(cw_test_executor_t * cte);
int test_cw_tq_length_internal(cw_test_executor_t * cte);
int test_cw_tq_callback(cw_test_executor_t * cte);
int test_cw_tq_prev_index_internal(cw_test_executor_t * cte);
int test_cw_tq_next_index_internal(cw_test_executor_t * cte);
int test_cw_tq_operations_1(cw_test_executor_t * cte);
int test_cw_tq_operations_2(cw_test_executor_t * cte);
int test_cw_tq_operations_3(cw_test_executor_t * cte);




#endif /* #ifndef _LIBCW_TQ_TESTS_H_ */
