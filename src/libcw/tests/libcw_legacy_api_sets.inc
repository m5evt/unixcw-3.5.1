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
			legacy_api_test_setup,

			legacy_api_test_cw_wait_for_tone,
			legacy_api_test_cw_wait_for_tone_queue,
			legacy_api_test_cw_queue_tone,
			legacy_api_test_empty_tone_queue,
			legacy_api_test_full_tone_queue,
			legacy_api_test_tone_queue_callback,

			legacy_api_test_teardown,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_GEN, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			legacy_api_test_setup,

			legacy_api_test_volume_functions,
			legacy_api_test_send_primitives,
			legacy_api_test_send_character_and_string,
			legacy_api_test_representations,
			legacy_api_test_teardown,

			/* This function creates and deletes a
			   generator on its own, so it has to be put
			   after legacy_api_test_teardown() that
			   deletes a generator. */
			legacy_api_test_basic_gen_operations,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_KEY, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			legacy_api_test_setup,

			legacy_api_test_iambic_key_dot,
			legacy_api_test_iambic_key_dash,
			legacy_api_test_iambic_key_alternating,
			legacy_api_test_iambic_key_none,
			legacy_api_test_straight_key,

			legacy_api_test_teardown,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_OTHER, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			legacy_api_test_setup,

			legacy_api_test_parameter_ranges,
			legacy_api_test_cw_gen_forever_public,
			//legacy_api_cw_test_delayed_release,
			//legacy_api_cw_test_signal_handling, /* FIXME - not sure why this test fails :( */

			legacy_api_test_teardown,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_OTHER, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			/* This test does its own generator setup and deconfig. */
			legacy_api_test_tq_short_space,

			NULL,
		}
	},
