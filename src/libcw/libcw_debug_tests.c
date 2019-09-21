#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h> /* "PRIu32" */




#include "libcw_data.h"
#include "libcw_data_tests.h"
#include "libcw_test.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_key.h"
#include "libcw.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/debug: "




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




/**
   \brief Test getting and setting of debug flags.

   tests::cw_debug_set_flags()
   tests::cw_debug_get_flags()
*/
unsigned int test_cw_debug_flags_internal(cw_test_stats_t * stats)
{
	/* Store current flags for period of tests. */
	uint32_t flags_backup = cw_debug_get_flags(&cw_debug_object);
	bool set_failure = true;
	bool get_failure = true;
	int n = 0;

	for (uint32_t i = 1; i <= CW_DEBUG_MASK; i++) {
		cw_debug_set_flags(&cw_debug_object, i);

		set_failure = !((&cw_debug_object)->flags & i);
		if (set_failure) {
			fprintf(out_file, MSG_PREFIX "failed to set debug flag %"PRIu32"\n", i);
			break;
		}

		get_failure = (cw_debug_get_flags(&cw_debug_object) != i);
		if (get_failure) {
			fprintf(out_file, MSG_PREFIX "failed to get debug flag %"PRIu32"\n", i);
			break;
		}
	}

	set_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "set:");
	CW_TEST_PRINT_TEST_RESULT (set_failure, n);

	get_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "get:");
	CW_TEST_PRINT_TEST_RESULT (get_failure, n);

	/* Restore original flags. */
	cw_debug_set_flags(&cw_debug_object, flags_backup);

	return 0;
}
