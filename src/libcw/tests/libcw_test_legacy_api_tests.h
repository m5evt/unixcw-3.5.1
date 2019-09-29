#ifndef _LIBCW_LEGACY_TESTS_H_
#define _LIBCW_LEGACY_TESTS_H_




#include "tests/libcw_test_framework.h"



/* Setup and teardown functions for test sets. */
int legacy_api_test_setup(cw_test_executor_t * cwe);
int legacy_api_test_teardown(cw_test_executor_t * cwe);



/* "Tone queue" topic. */
int test_cw_wait_for_tone(cw_test_executor_t * cte);
int test_cw_wait_for_tone_queue(cw_test_executor_t * cte);
int test_cw_queue_tone(cw_test_executor_t * cte);

int test_empty_tone_queue(cw_test_executor_t * cte);
int test_full_tone_queue(cw_test_executor_t * cte);

int test_tone_queue_callback(cw_test_executor_t * cte);

/* "Generator" topic. */
int test_volume_functions(cw_test_executor_t * cte);
int test_send_primitives(cw_test_executor_t * cte);
int test_send_character_and_string(cw_test_executor_t * cte);
int test_representations(cw_test_executor_t * cte);
int legacy_api_test_basic_gen_operations(cw_test_executor_t * cte);

/* "Morse key" topic. */
int test_iambic_key_dot(cw_test_executor_t * cte);
int test_iambic_key_dash(cw_test_executor_t * cte);
int test_iambic_key_alternating(cw_test_executor_t * cte);
int test_iambic_key_none(cw_test_executor_t * cte);
int legacy_api_test_straight_key(cw_test_executor_t * cte);


/* Other functions. */
int test_parameter_ranges(cw_test_executor_t * cte);
int test_cw_gen_forever_public(cw_test_executor_t * cte);

// int cw_test_delayed_release(cw_test_executor_t * cte);




#endif /* #ifndef _LIBCW_LEGACY_TESTS_H_ */
