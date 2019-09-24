#ifndef _LIBCW_REC_TESTS_H_
#define _LIBCW_REC_TESTS_H_




#include "tests/libcw_test_utils.h"




unsigned int test_cw_rec_with_base_data_fixed(void);
unsigned int test_cw_rec_with_random_data_fixed(void);
unsigned int test_cw_rec_with_random_data_adaptive(void);
unsigned int test_cw_get_receive_parameters(void);

unsigned int test_cw_rec_identify_mark_internal(cw_test_stats_t * stats);
unsigned int test_cw_rec_test_with_base_constant(cw_test_stats_t * stats);
unsigned int test_cw_rec_test_with_random_constant(cw_test_stats_t * stats);
unsigned int test_cw_rec_test_with_random_varying(cw_test_stats_t * stats);
unsigned int test_cw_rec_get_parameters(cw_test_stats_t * stats);
unsigned int test_cw_rec_parameter_getters_setters_1(cw_test_stats_t * stats);
unsigned int test_cw_rec_parameter_getters_setters_2(cw_test_stats_t * stats);




#endif /* #ifndef _LIBCW_REC_TESTS_H_ */
