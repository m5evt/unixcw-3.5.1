
#include "libcw_utils_tests.h"
#include "libcw_data_tests.h"
#include "libcw_debug_tests.h"
#include "libcw_tq_tests.h"
#include "libcw_gen_tests.h"
#include "libcw_key_tests.h"
#include "libcw_rec_tests.h"




cw_test_function_t cw_unit_tests_other_s[] = {

	/* cw_utils module */
	test_cw_timestamp_compare_internal,
	test_cw_timestamp_validate_internal,
	test_cw_usecs_to_timespec_internal,
	test_cw_version_internal,
	test_cw_license_internal,
	test_cw_get_x_limits_internal,

	/* cw_data module */
	test_cw_representation_to_hash_internal,
	test_cw_representation_to_character_internal,
	test_cw_representation_to_character_internal_speed,
	test_character_lookups_internal,
	test_prosign_lookups_internal,
	test_phonetic_lookups_internal,
	test_validate_character_and_string_internal,
	test_validate_representation_internal,

	/* cw_debug module */
	test_cw_debug_flags_internal,

	NULL
};



/* Tests that are dependent on a sound system being configured.
   Tone queue module functions */
cw_test_function_t cw_unit_tests_tq[] = {
	test_cw_tq_test_capacity_1,
	test_cw_tq_test_capacity_2,
	test_cw_tq_wait_for_level_internal,
	test_cw_tq_is_full_internal,
	test_cw_tq_enqueue_dequeue_internal,
#if 0
	test_cw_tq_enqueue_args_internal,
#endif
	test_cw_tq_enqueue_internal_2,

	test_cw_tq_new_delete_internal,
	test_cw_tq_get_capacity_internal,
	test_cw_tq_length_internal,
	test_cw_tq_prev_index_internal,
	test_cw_tq_next_index_internal,
	test_cw_tq_callback,
	test_cw_tq_operations_1,
	test_cw_tq_operations_2,
	test_cw_tq_operations_3,

	NULL
};




/* Tests that are dependent on a sound system being configured.
   Generator module functions. */
cw_test_function_t cw_unit_tests_gen[] = {
	test_cw_gen_set_tone_slope,
	test_cw_gen_tone_slope_shape_enums,
	test_cw_gen_new_delete,
	test_cw_gen_get_timing_parameters_internal,
	test_cw_gen_parameter_getters_setters,
	test_cw_gen_volume_functions,
	test_cw_gen_enqueue_primitives,
	test_cw_gen_enqueue_representations,
	test_cw_gen_enqueue_character_and_string,
	test_cw_gen_forever_internal,

	NULL
};




/* 'key' module. */
cw_test_function_t cw_unit_tests_key[] = {
	test_keyer,
	test_straight_key,

	NULL
};




cw_test_function_t cw_unit_tests_rec1[] = {
	test_cw_rec_get_parameters,
	test_cw_rec_parameter_getters_setters_1,
	test_cw_rec_parameter_getters_setters_2,
	test_cw_rec_identify_mark_internal,
	test_cw_rec_test_with_base_constant,
	test_cw_rec_test_with_random_constant,
	test_cw_rec_test_with_random_varying,

#if 0
	test_cw_get_receive_parameters,
#endif

	NULL
};
