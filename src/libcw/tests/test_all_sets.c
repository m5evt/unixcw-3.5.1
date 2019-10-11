/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */





#include "libcw_utils_tests.h"
#include "libcw_data_tests.h"
#include "libcw_debug_tests.h"
#include "libcw_tq_tests.h"
#include "libcw_gen_tests.h"
#include "libcw_key_tests.h"
#include "libcw_rec_tests.h"

#include "test_framework.h"

#include "libcw_legacy_api_tests.h"
#include "libcw_test_tq_short_space.h"




cw_test_set_t cw_test_sets[] = {
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_OTHER, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			/* cw_utils topic */
			CW_TEST_FUNCTION_INSERT(test_cw_timestamp_compare_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_timestamp_validate_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_usecs_to_timespec_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_version_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_license_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_get_x_limits_internal),

			/* cw_debug topic */
			CW_TEST_FUNCTION_INSERT(test_cw_debug_flags_internal),

			CW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_DATA, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			/* cw_data topic */
			CW_TEST_FUNCTION_INSERT(test_cw_representation_to_hash_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_representation_to_character_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_representation_to_character_internal_speed),
			CW_TEST_FUNCTION_INSERT(test_character_lookups_internal),
			CW_TEST_FUNCTION_INSERT(test_prosign_lookups_internal),
			CW_TEST_FUNCTION_INSERT(test_phonetic_lookups_internal),
			CW_TEST_FUNCTION_INSERT(test_validate_character_internal),
			CW_TEST_FUNCTION_INSERT(test_validate_string_internal),
			CW_TEST_FUNCTION_INSERT(test_validate_representation_internal),

			CW_TEST_FUNCTION_INSERT(NULL),
		}

	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_TQ, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. All sound systems are included in tests of tq, because sometimes a running gen is necessary. */

		{
			CW_TEST_FUNCTION_INSERT(test_cw_tq_test_capacity_A),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_test_capacity_B),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_wait_for_level_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_is_full_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_enqueue_dequeue_internal),
#if 0
			CW_TEST_FUNCTION_INSERT(test_cw_tq_enqueue_args_internal),
#endif
			CW_TEST_FUNCTION_INSERT(test_cw_tq_enqueue_internal_B),

			CW_TEST_FUNCTION_INSERT(test_cw_tq_new_delete_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_get_capacity_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_length_internal_1),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_prev_index_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_next_index_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_callback),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_gen_operations_A),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_gen_operations_B),
			CW_TEST_FUNCTION_INSERT(test_cw_tq_operations_C),

			CW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_GEN, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			CW_TEST_FUNCTION_INSERT(test_cw_gen_new_delete),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_new_start_delete),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_new_stop_delete),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_new_start_stop_delete),

			CW_TEST_FUNCTION_INSERT(test_cw_gen_set_tone_slope),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_tone_slope_shape_enums),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_get_timing_parameters_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_parameter_getters_setters),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_volume_functions),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_enqueue_primitives),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_enqueue_representations),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_enqueue_character),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_enqueue_string),
			CW_TEST_FUNCTION_INSERT(test_cw_gen_forever_internal),

			CW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_KEY, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			CW_TEST_FUNCTION_INSERT(test_keyer),
			CW_TEST_FUNCTION_INSERT(test_straight_key),

			CW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_REC, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			CW_TEST_FUNCTION_INSERT(test_cw_rec_get_parameters),
			CW_TEST_FUNCTION_INSERT(test_cw_rec_parameter_getters_setters_1),
			CW_TEST_FUNCTION_INSERT(test_cw_rec_parameter_getters_setters_2),
			CW_TEST_FUNCTION_INSERT(test_cw_rec_identify_mark_internal),
			CW_TEST_FUNCTION_INSERT(test_cw_rec_test_with_base_constant),
			CW_TEST_FUNCTION_INSERT(test_cw_rec_test_with_random_constant),
			CW_TEST_FUNCTION_INSERT(test_cw_rec_test_with_random_varying),

#if 0
			CW_TEST_FUNCTION_INSERT(test_cw_get_receive_parameters),
#endif
			CW_TEST_FUNCTION_INSERT(NULL)
		}
	},


	/* "Import" test sets from another file. That file is shared
	   between two test binaries. */
#include "libcw_legacy_api_sets.inc"


	/* Guard. */
	{
		CW_TEST_SET_INVALID,
		CW_TEST_API_MODERN, /* This field doesn't matter here, test set is invalid. */

		{ LIBCW_TEST_TOPIC_MAX },
		{ LIBCW_TEST_SOUND_SYSTEM_MAX },
		{
			CW_TEST_FUNCTION_INSERT(NULL)
		}
	}
};
