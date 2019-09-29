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




#include "libcw_test_legacy_api_tests.h"



/*
  FIXME: creating and deleting of generator has been removed from test
  driver, so now each test set will have to have a function for setup
  and teardown:

  setup:
   	int rv = cw_generator_new(cte->current_sound_system, NULL);
	if (rv != 1) {
		cte->log_err(cte, "Can't create generator, stopping the test\n");
		return -1;
	}
	rv = cw_generator_start();
	if (rv != 1) {
		cte->log_err(cte, "Can't start generator, stopping the test\n");
		cw_generator_delete();
		return -1;
	}


  teardown:
	sleep(1);
	cw_generator_stop();
	sleep(1);
	cw_generator_delete();
*/




cw_test_set_t cw_all_tests[] = {
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_TQ, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			test_cw_wait_for_tone,
			test_cw_wait_for_tone_queue,
			test_cw_queue_tone,
			test_empty_tone_queue,
			test_full_tone_queue,
			test_tone_queue_callback,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_GEN, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			test_volume_functions,
			test_send_primitives,
			test_send_character_and_string,
			test_representations,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_KEY, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			test_iambic_key_dot,
			test_iambic_key_dash,
			test_iambic_key_alternating,
			test_iambic_key_none,
			test_straight_key,

			NULL,
		}
	},
	{
		CW_TEST_SET_VALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_OTHER, LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ CW_AUDIO_NULL, CW_AUDIO_CONSOLE, CW_AUDIO_OSS, CW_AUDIO_ALSA, CW_AUDIO_PA, LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			test_parameter_ranges,
			test_cw_gen_forever_public,

			//cw_test_delayed_release,
			//cw_test_signal_handling, /* FIXME - not sure why this test fails :( */

			NULL,
		}
	},


	/* Guard. */
	{
		CW_TEST_SET_INVALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			NULL,
		}
	}
};
