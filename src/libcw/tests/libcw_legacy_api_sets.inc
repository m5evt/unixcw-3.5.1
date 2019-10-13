/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_TQ, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			CW_TEST_FUNCTION_INSERT(legacy_api_test_setup),

			CW_TEST_FUNCTION_INSERT(legacy_api_test_cw_wait_for_tone),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_cw_wait_for_tone_queue),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_cw_queue_tone),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_empty_tone_queue),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_full_tone_queue),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_tone_queue_callback),

			CW_TEST_FUNCTION_INSERT(legacy_api_test_teardown),

			CW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_GEN, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			CW_TEST_FUNCTION_INSERT(legacy_api_test_setup),

			CW_TEST_FUNCTION_INSERT(legacy_api_test_volume_functions),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_send_primitives),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_send_character_and_string),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_representations),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_teardown),

			/* This function creates and deletes a
			   generator on its own, so it has to be put
			   after legacy_api_test_teardown() that
			   deletes a generator. */
			CW_TEST_FUNCTION_INSERT(legacy_api_test_basic_gen_operations),

			CW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_KEY, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			CW_TEST_FUNCTION_INSERT(legacy_api_test_setup),

			CW_TEST_FUNCTION_INSERT(legacy_api_test_iambic_key_dot),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_iambic_key_dash),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_iambic_key_alternating),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_iambic_key_none),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_straight_key),

			CW_TEST_FUNCTION_INSERT(legacy_api_test_teardown),

			CW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_OTHER, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			CW_TEST_FUNCTION_INSERT(legacy_api_test_setup),

			CW_TEST_FUNCTION_INSERT(legacy_api_test_low_level_gen_parameters),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_parameter_ranges),
			CW_TEST_FUNCTION_INSERT(legacy_api_test_cw_gen_forever_public),
			//CW_TEST_FUNCTION_INSERT(legacy_api_cw_test_delayed_release),
			//CW_TEST_FUNCTION_INSERT(legacy_api_cw_test_signal_handling), /* FIXME - not sure why this test fails :( */

			CW_TEST_FUNCTION_INSERT(legacy_api_test_teardown),

			CW_TEST_FUNCTION_INSERT(NULL),
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_OTHER, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			/* This test does its own generator setup and deconfig. */
			CW_TEST_FUNCTION_INSERT(legacy_api_test_tq_short_space),

			CW_TEST_FUNCTION_INSERT(NULL),
		}
	},
