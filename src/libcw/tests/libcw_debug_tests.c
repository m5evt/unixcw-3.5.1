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




#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h> /* "PRIu32" */




#include "test_framework.h"

#include "libcw_debug.h"
#include "libcw_debug_tests.h"
#include "libcw_utils.h"
#include "libcw_key.h"
#include "libcw.h"
#include "libcw2.h"




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




/**
   \brief Test getting and setting of debug flags.

   tests::cw_debug_set_flags()
   tests::cw_debug_get_flags()
*/
int test_cw_debug_flags_internal(cw_test_executor_t * cte)
{
	cte->print_test_header(cte, __func__);

	/* Store current flags for period of tests. */
	uint32_t flags_backup = cw_debug_get_flags(&cw_debug_object);
	bool set_failure = true;
	bool get_failure = true;

	for (uint32_t i = 1; i <= CW_DEBUG_MASK; i++) {
		cw_debug_set_flags(&cw_debug_object, i);

		set_failure = !((&cw_debug_object)->flags & i);
		if (!cte->expect_eq_int_errors_only(cte, false, set_failure, "failed to set debug flag %"PRIu32"\n", i)) {
			set_failure = true;
			break;
		}

		get_failure = (cw_debug_get_flags(&cw_debug_object) != i);
		if (!cte->expect_eq_int_errors_only(cte, false, get_failure, "failed to get debug flag %"PRIu32"\n", i)) {
			get_failure = true;
			break;
		}
	}

	cte->expect_eq_int(cte, false, set_failure, "set");
	cte->expect_eq_int(cte, false, get_failure, "get");

	/* Restore original flags. */
	cw_debug_set_flags(&cw_debug_object, flags_backup);

	cte->print_test_footer(cte, __func__);

	return 0;
}
