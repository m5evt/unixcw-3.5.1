#ifndef _LIBCW_LEGACY_TESTS_H_
#define _LIBCW_LEGACY_TESTS_H_




#include "tests/libcw_test_framework.h"




/* Tone queue module. */
void test_cw_wait_for_tone(cw_test_executor_t * executor);
void test_cw_wait_for_tone_queue(cw_test_executor_t * executor);
void test_cw_queue_tone(cw_test_executor_t * executor);

void test_empty_tone_queue(cw_test_executor_t * executor);
void test_full_tone_queue(cw_test_executor_t * executor);

void test_tone_queue_callback(cw_test_executor_t * executor);

/* Generator module. */
void test_volume_functions(cw_test_executor_t * executor);
void test_send_primitives(cw_test_executor_t * executor);
void test_send_character_and_string(cw_test_executor_t * executor);
void test_representations(cw_test_executor_t * executor);

/* Morse key module. */
void test_iambic_key_dot(cw_test_executor_t * executor);
void test_iambic_key_dash(cw_test_executor_t * executor);
void test_iambic_key_alternating(cw_test_executor_t * executor);
void test_iambic_key_none(cw_test_executor_t * executor);
void test_straight_key(cw_test_executor_t * executor);


/* Other functions. */
void test_parameter_ranges(cw_test_executor_t * executor);
void test_cw_gen_forever_public(cw_test_executor_t * executor);

// void cw_test_delayed_release(cw_test_executor_t * executor);




#endif /* #ifndef _LIBCW_LEGACY_TESTS_H_ */
