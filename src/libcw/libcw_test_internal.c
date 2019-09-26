/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2017  Kamil Ignacak (acerion@wp.pl)

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
#include <sys/time.h> /* gettimeofday() */
#include <assert.h>




#include "libcw.h"
#include "libcw_debug.h"
#include "libcw_null.h"
#include "libcw_console.h"
#include "libcw_oss.h"
#include "libcw_key.h"
#include "libcw_tq.h"
#include "libcw_gen.h"


/* Tested modules. */
#include "libcw_tq_internal.h"
#include "libcw_tq_tests.h"
#include "libcw_gen_internal.h"
#include "libcw_gen_tests.h"
#include "libcw_rec_internal.h"
#include "libcw_rec_tests.h"
//#include "libcw_data_internal.h"
#include "libcw_data_tests.h"
//#include "libcw_debug_internal.h"
#include "libcw_debug_tests.h"
//#include "libcw_utils_internal.h"
#include "libcw_utils_tests.h"
//#include "libcw_key_internal.h"
#include "libcw_key_tests.h"





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_dev;




/* ******************************************************************** */
/*                 Unit tests for internal functions                    */
/* ******************************************************************** */




/* Unit tests for internal functions (and also some public functions)
   defined in libcw.c.

   See also libcw_test_public.c and libcw_test_simple_gen.c. */



typedef unsigned int (*cw_test_function_t)(void);
typedef unsigned int (*cw_test_function2_t)(cw_test_stats_t * stats);

static cw_test_function_t cw_unit_tests[] = {



	NULL
};



static cw_test_function2_t cw_unit_tests2[] = {

};




int main(void)
{
	fprintf(stderr, "libcw unit tests\n\n");


	struct timeval tv;
	gettimeofday(&tv, NULL);
	srand((int) tv.tv_usec);

	//cw_debug_set_flags(&cw_debug_object, CW_DEBUG_RECEIVE_STATES);
	//cw_debug_object.level = CW_DEBUG_INFO;

	cw_debug_set_flags(&cw_debug_object_dev, CW_DEBUG_RECEIVE_STATES | CW_DEBUG_TONE_QUEUE | CW_DEBUG_GENERATOR | CW_DEBUG_KEYING);
	cw_debug_object_dev.level = CW_DEBUG_DEBUG;

	int i = 0;
	while (cw_unit_tests[i]) {
		cw_unit_tests[i]();
		i++;
	}

	cw_test_stats_t stats = { 0 };
	i = 0;
	while (cw_unit_tests2[i]) {
		cw_unit_tests2[i](&stats);
		i++;
	}
	fprintf(stderr, "successes: %d, failures: %d\n", stats.successes, stats.failures);


	/* "make check" facility requires this message to be
	   printed on stdout; don't localize it */
	fprintf(stdout, "\nlibcw: test result: success\n\n");


	return 0;
}
