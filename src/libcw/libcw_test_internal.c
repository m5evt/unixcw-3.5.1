/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2014  Kamil Ignacak (acerion@wp.pl)

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


#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "libcw.h"

#include "libcw_gen.h"
#include "libcw_rec.h"
#include "libcw_debug.h"
#include "libcw_data.h"
#include "libcw_tq.h"
#include "libcw_utils.h"

#include "libcw_null.h"
#include "libcw_console.h"
#include "libcw_oss.h"





extern cw_debug_t cw_debug_object;





/* ******************************************************************** */
/*                 Unit tests for internal functions                    */
/* ******************************************************************** */





/* Unit tests for internal functions (and also some public functions)
   defined in libcw.c.

   For unit tests of library's public interfaces see libcwtest.c. */

#include <stdio.h>
#include <assert.h>





typedef unsigned int (*cw_test_function_t)(void);

static cw_test_function_t cw_unit_tests[] = {
	test_cw_representation_to_hash_internal,
	test_cw_representation_to_character_internal,
	test_cw_representation_to_character_internal_speed,
	// test_cw_forever,

	test_cw_tq_new_internal,
	test_cw_tone_queue_get_capacity_internal,
	test_cw_tone_queue_prev_index_internal,
	test_cw_tone_queue_next_index_internal,
	test_cw_tone_queue_length_internal,
	test_cw_tone_queue_enqueue_internal,
	test_cw_tone_queue_dequeue_internal,
	test_cw_tone_queue_is_full_internal,
	test_cw_tone_queue_test_capacity1,
	test_cw_tone_queue_test_capacity2,

	test_cw_timestamp_compare_internal,
	test_cw_timestamp_validate_internal,

	test_cw_usecs_to_timespec_internal,

	test_cw_rec_identify_mark_internal,
	test_cw_rec_fixed_receive_1,

	NULL
};




int main(void)
{
	fprintf(stderr, "libcw unit tests for library's internal functions\n\n");

	cw_debug_set_flags(&cw_debug_object, CW_DEBUG_RECEIVE_STATES);
	cw_debug_object.level = CW_DEBUG_INFO;

	int i = 0;
	while (cw_unit_tests[i]) {
		cw_unit_tests[i]();
		i++;
	}

	/* "make check" facility requires this message to be
	   printed on stdout; don't localize it */
	fprintf(stdout, "\nlibcw: test result: success\n\n");


	return 0;
}
