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

			test_cw_wait_for_tone,
			test_cw_wait_for_tone_queue,
			test_cw_queue_tone,
			test_empty_tone_queue,
			test_full_tone_queue,
			test_tone_queue_callback,

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

			test_volume_functions,
			test_send_primitives,
			test_send_character_and_string,
			test_representations,

			legacy_api_test_teardown,

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

			test_iambic_key_dot,
			test_iambic_key_dash,
			test_iambic_key_alternating,
			test_iambic_key_none,
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

			test_parameter_ranges,
			test_cw_gen_forever_public,
			//cw_test_delayed_release,
			//cw_test_signal_handling, /* FIXME - not sure why this test fails :( */

			legacy_api_test_teardown,

			NULL,
		}
	},
