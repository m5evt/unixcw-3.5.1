/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_UTILS
#define H_LIBCW_UTILS





#include "config.h"

#include <sys/time.h>
#include <stdbool.h>





/* Microseconds in a second, for struct timeval handling. */
enum { CW_USECS_PER_SEC = 1000000 };





int cw_timestamp_compare_internal(const struct timeval *earlier, const struct timeval *later);
int cw_timestamp_validate_internal(struct timeval *out_timestamp, const struct timeval *in_timestamp);
void cw_usecs_to_timespec_internal(struct timespec *t, int usecs);
void cw_nanosleep_internal(struct timespec *n);

#if (defined(LIBCW_WITH_ALSA) || defined(LIBCW_WITH_PULSEAUDIO))
bool cw_dlopen_internal(const char *name, void **handle);
#endif





#ifdef LIBCW_UNIT_TESTS

unsigned int test_cw_forever(void);
unsigned int test_cw_timestamp_compare_internal(void);
unsigned int test_cw_timestamp_validate_internal(void);
unsigned int test_cw_usecs_to_timespec_internal(void);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_UTILS */
