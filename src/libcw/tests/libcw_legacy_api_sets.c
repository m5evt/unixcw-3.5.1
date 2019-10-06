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




#include "libcw_legacy_api_tests.h"
#include "libcw_test_tq_short_space.h"




cw_test_set_t cw_test_sets[] = {

	/* "Import" test sets from another file. That file is shared
	   between two test binaries. */
#include "libcw_legacy_api_sets.inc"

	/* Guard. */
	{
		CW_TEST_SET_INVALID,
		CW_TEST_API_LEGACY,

		{ LIBCW_TEST_TOPIC_MAX }, /* Topics. */
		{ LIBCW_TEST_SOUND_SYSTEM_MAX }, /* Sound systems. */

		{
			CW_TEST_FUNCTION_INSERT(NULL),
		}
	}
};
