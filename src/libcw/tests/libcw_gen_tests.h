/*
  This file is a part of unixcw project.  unixcw project is covered by
  GNU General Public License, version 2 or later.
*/

#ifndef _LIBCW_GEN_TESTS_H_
#define _LIBCW_GEN_TESTS_H_




#include "test_framework.h"




int test_cw_gen_new_delete(cw_test_executor_t * cte);
int test_cw_gen_new_start_delete(cw_test_executor_t * cte);
int test_cw_gen_new_stop_delete(cw_test_executor_t * cte);
int test_cw_gen_new_start_stop_delete(cw_test_executor_t * cte);

int test_cw_gen_set_tone_slope(cw_test_executor_t * cte);
int test_cw_gen_tone_slope_shape_enums(cw_test_executor_t * cte);
int test_cw_gen_forever_internal(cw_test_executor_t * cte);
int test_cw_gen_get_timing_parameters_internal(cw_test_executor_t * cte);
int test_cw_gen_parameter_getters_setters(cw_test_executor_t * cte);
int test_cw_gen_volume_functions(cw_test_executor_t * cte);
int test_cw_gen_enqueue_primitives(cw_test_executor_t * cte);
int test_cw_gen_enqueue_representations(cw_test_executor_t * cte);
int test_cw_gen_enqueue_character(cw_test_executor_t * cte);
int test_cw_gen_enqueue_string(cw_test_executor_t * cte);




/*
   "forever" feature is not a part of public api, so in theory it
   shouldn't be tested in libcw_test_public, but the libcw_test_public
   is able to perform tests with different audio sinks, whereas
   libcw_test_internal only uses NULL audio sink. TODO: fix this.

   So libcw_test_internal does basic tests ("does it work at all?"),
   and libcw_test_public does full test.
*/
int test_cw_gen_forever_sub(cw_test_executor_t * cte, int seconds, int audio_system, const char *audio_device);




#endif /* #ifndef _LIBCW_GEN_TESTS_H_ */
