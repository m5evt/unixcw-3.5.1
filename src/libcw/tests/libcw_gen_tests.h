#ifndef _LIBCW_GEN_TESTS_H_
#define _LIBCW_GEN_TESTS_H_




#include "tests/libcw_test_utils.h"




unsigned int test_cw_gen_set_tone_slope(cw_test_stats_t * stats);
unsigned int test_cw_gen_tone_slope_shape_enums(cw_test_stats_t * stats);
unsigned int test_cw_gen_new_delete(cw_test_stats_t * stats);
unsigned int test_cw_gen_forever_internal(cw_test_stats_t * stats);
unsigned int test_cw_gen_get_timing_parameters_internal(cw_test_stats_t * stats);
unsigned int test_cw_gen_parameter_getters_setters(cw_test_stats_t * stats);
unsigned int test_cw_gen_volume_functions(cw_test_stats_t * stats);
unsigned int test_cw_gen_enqueue_primitives(cw_test_stats_t * stats);
unsigned int test_cw_gen_enqueue_representations(cw_test_stats_t * stats);
unsigned int test_cw_gen_enqueue_character_and_string(cw_test_stats_t * stats);




/*

   "forever" feature is not a part of public api, so in theory it
   shouldn't be tested in libcw_test_public, but the libcw_test_public
   is able to perform tests with different audio sinks, whereas
   libcw_test_internal only uses NULL audio sink.

   So libcw_test_internal does basic tests ("does it work at all?"),
   and libcw_test_public does full test.
*/
unsigned int test_cw_gen_forever_sub(cw_test_stats_t * stats, int seconds, int audio_system, const char *audio_device);




#endif /* #ifndef _LIBCW_GEN_TESTS_H_ */
