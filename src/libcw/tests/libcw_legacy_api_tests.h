/*
  This file is a part of unixcw project.  unixcw project is covered by
  GNU General Public License, version 2 or later.
*/

#ifndef _LIBCW_LEGACY_TESTS_H_
#define _LIBCW_LEGACY_TESTS_H_




#include "test_framework.h"



/* Setup and teardown functions for test sets. */
int legacy_api_test_setup(cw_test_executor_t * cwe);
int legacy_api_test_teardown(cw_test_executor_t * cwe);



/* "Tone queue" topic. */
int legacy_api_test_cw_wait_for_tone(cw_test_executor_t * cte);
int legacy_api_test_cw_wait_for_tone_queue(cw_test_executor_t * cte);
int legacy_api_test_cw_queue_tone(cw_test_executor_t * cte);

int legacy_api_test_empty_tone_queue(cw_test_executor_t * cte);
int legacy_api_test_full_tone_queue(cw_test_executor_t * cte);

int legacy_api_test_tone_queue_callback(cw_test_executor_t * cte);

/* "Generator" topic. */
int legacy_api_test_volume_functions(cw_test_executor_t * cte);
int legacy_api_test_send_primitives(cw_test_executor_t * cte);
int legacy_api_test_send_character_and_string(cw_test_executor_t * cte);
int legacy_api_test_representations(cw_test_executor_t * cte);
int legacy_api_test_basic_gen_operations(cw_test_executor_t * cte);

/* "Morse key" topic. */
int legacy_api_test_iambic_key_dot(cw_test_executor_t * cte);
int legacy_api_test_iambic_key_dash(cw_test_executor_t * cte);
int legacy_api_test_iambic_key_alternating(cw_test_executor_t * cte);
int legacy_api_test_iambic_key_none(cw_test_executor_t * cte);
int legacy_api_test_straight_key(cw_test_executor_t * cte);


/* Other functions. */
int legacy_api_test_parameter_ranges(cw_test_executor_t * cte);
int legacy_api_test_cw_gen_forever_public(cw_test_executor_t * cte);

// int legacy_api_cw_test_delayed_release(cw_test_executor_t * cte);




#endif /* #ifndef _LIBCW_LEGACY_TESTS_H_ */
