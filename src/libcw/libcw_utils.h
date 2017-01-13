/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef H_LIBCW_UTILS
#define H_LIBCW_UTILS





#include "config.h"

#include <sys/time.h>
#include <stdbool.h>





/* Microseconds in a second, for struct timeval handling. */
enum { CW_USECS_PER_SEC = 1000000 };

/* Nanoseconds in a second, for struct timespec. */
enum { CW_NSECS_PER_SEC = 1000000000 };




int cw_timestamp_compare_internal(const struct timeval *earlier, const struct timeval *later);
int cw_timestamp_validate_internal(struct timeval *out_timestamp, const struct timeval *in_timestamp);
void cw_usecs_to_timespec_internal(struct timespec *t, int usecs);
void cw_nanosleep_internal(struct timespec *n);

#if (defined(LIBCW_WITH_ALSA) || defined(LIBCW_WITH_PULSEAUDIO))
bool cw_dlopen_internal(const char *name, void **handle);
#endif





#ifdef LIBCW_UNIT_TESTS

#include "libcw_test.h"

unsigned int test_cw_timestamp_compare_internal(cw_test_stats_t * stats);
unsigned int test_cw_timestamp_validate_internal(cw_test_stats_t * stats);
unsigned int test_cw_usecs_to_timespec_internal(cw_test_stats_t * stats);
unsigned int test_cw_version_internal(cw_test_stats_t * stats);
unsigned int test_cw_license_internal(cw_test_stats_t * stats);
unsigned int test_cw_get_x_limits_internal(cw_test_stats_t * stats);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_UTILS */
