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
		LIBCW_TEST_SET_VALID,
		LIBCW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_OTHER, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			/* cw_utils topic */
			LIBCW_TEST_FUNCTION_INSERT(test_cw_timestamp_compare_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_timestamp_validate_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_usecs_to_timespec_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_version_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_license_internal),

			/* cw_debug topic */
			LIBCW_TEST_FUNCTION_INSERT(test_cw_debug_flags_internal),

			LIBCW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		LIBCW_TEST_SET_VALID,
		LIBCW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_DATA, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			/* cw_data topic */
			LIBCW_TEST_FUNCTION_INSERT(test_cw_representation_to_hash_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_representation_to_character_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_representation_to_character_internal_speed),
			LIBCW_TEST_FUNCTION_INSERT(test_character_lookups_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_prosign_lookups_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_phonetic_lookups_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_validate_character_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_validate_string_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_validate_representation_internal),

			LIBCW_TEST_FUNCTION_INSERT(NULL),
		}

	},
	{
		LIBCW_TEST_SET_VALID,
		LIBCW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_TQ, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. All sound systems are included in tests of tq, because sometimes a running gen is necessary. */

		{
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_test_capacity_A),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_test_capacity_B),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_wait_for_level_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_is_full_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_enqueue_dequeue_internal),
#if 0
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_enqueue_args_internal),
#endif
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_enqueue_internal_B),

			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_new_delete_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_get_capacity_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_length_internal_1),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_prev_index_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_next_index_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_callback),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_gen_operations_A),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_gen_operations_B),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_tq_operations_C),

			LIBCW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		LIBCW_TEST_SET_VALID,
		LIBCW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_GEN, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_new_delete),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_new_start_delete),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_new_stop_delete),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_new_start_stop_delete),

			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_set_tone_slope),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_tone_slope_shape_enums),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_get_timing_parameters_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_parameter_getters_setters),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_volume_functions),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_enqueue_primitives),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_enqueue_representations),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_enqueue_character),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_enqueue_string),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_gen_forever_internal),

			LIBCW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		LIBCW_TEST_SET_VALID,
		LIBCW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_KEY, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			LIBCW_TEST_FUNCTION_INSERT(test_keyer),
			LIBCW_TEST_FUNCTION_INSERT(test_straight_key),

			LIBCW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		LIBCW_TEST_SET_VALID,
		LIBCW_TEST_API_MODERN,

		{ LIBCW_TEST_TOPIC_REC, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			LIBCW_TEST_FUNCTION_INSERT(test_cw_rec_get_receive_parameters),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_rec_parameter_getters_setters_1),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_rec_parameter_getters_setters_2),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_rec_identify_mark_internal),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_rec_test_with_constant_speeds),
			LIBCW_TEST_FUNCTION_INSERT(test_cw_rec_test_with_varying_speeds),

			LIBCW_TEST_FUNCTION_INSERT(NULL)
		}
	},


	/* "Import" test sets from another file. That file is shared
	   between two test binaries. */
#include "libcw_legacy_api_sets.inc"


	/* Guard. */
	{
		LIBCW_TEST_SET_INVALID,
		LIBCW_TEST_API_MODERN, /* This field doesn't matter here, test set is invalid. */

		{ LIBCW_TEST_TOPIC_MAX },
		{ LIBCW_TEST_SOUND_SYSTEM_MAX },
		{
			LIBCW_TEST_FUNCTION_INSERT(NULL)
		}
	}
};
