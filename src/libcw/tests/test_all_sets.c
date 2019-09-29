
#include "libcw_utils_tests.h"
#include "libcw_data_tests.h"
#include "libcw_debug_tests.h"
#include "libcw_tq_tests.h"
#include "libcw_gen_tests.h"
#include "libcw_key_tests.h"
#include "libcw_rec_tests.h"

#include "test_framework.h"

#include "libcw_legacy_api_tests.h"




cw_test_set_t cw_test_sets[] = {
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_OTHER, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			/* cw_utils topic */
			test_cw_timestamp_compare_internal,
			test_cw_timestamp_validate_internal,
			test_cw_usecs_to_timespec_internal,
			test_cw_version_internal,
			test_cw_license_internal,
			test_cw_get_x_limits_internal,

			/* cw_data topic */
			test_cw_representation_to_hash_internal,
			test_cw_representation_to_character_internal,
			test_cw_representation_to_character_internal_speed,
			test_character_lookups_internal,
			test_prosign_lookups_internal,
			test_phonetic_lookups_internal,
			test_validate_character_and_string_internal,
			test_validate_representation_internal,

			/* cw_debug topic */
			test_cw_debug_flags_internal,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_TQ, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
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
#if 0
			test_cw_tq_callback,
			test_cw_tq_operations_1,
			test_cw_tq_operations_2,
#endif
			test_cw_tq_operations_3,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_GEN, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
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

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_KEY, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			test_keyer,
			test_straight_key,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_REC, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
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
			NULL,
		}
	},


	/* "Import" test sets from another file. That file is shared
	   between two test binaries. */
#include "libcw_test_legacy_api_test_sets.inc"


	/* Guard. */
	{
		CW_TEST_SET_INVALID,
		CW_TEST_API_MODERN, /* This field doesn't matter here, test set is invalid. */

		{ LIBCW_TEST_TOPIC_MAX },
		{ LIBCW_TEST_SOUND_SYSTEM_MAX },
		{
			NULL
		}
	}
};
