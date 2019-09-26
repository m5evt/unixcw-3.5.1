#ifndef _TEST_ALL_SETS_H_
#define _TEST_ALL_SETS_H_




#include <libcw_gen.h>
#include <libcw_key.h>

#include "libcw_test_framework.h"




typedef unsigned int (*cw_test_function_stats_t)(cw_test_stats_t * stats);
typedef unsigned int (*cw_test_function_stats_key_t)(cw_key_t * key, cw_test_stats_t * stats);
typedef unsigned int (*cw_test_function_stats_gen_t)(cw_gen_t * gen, cw_test_stats_t * stats);
typedef unsigned int (*cw_test_function_stats_tq_t)(cw_gen_t * gen, cw_test_stats_t * stats);




#endif /* #ifndef _TEST_ALL_SETS_H_ */
