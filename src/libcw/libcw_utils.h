/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef H_LIBCW_UTILS
#define H_LIBCW_UTILS




#include "config.h"

#include <sys/time.h>




/* Microseconds in a second, for struct timeval handling. */
enum { CW_USECS_PER_SEC = 1000000 };

/* Nanoseconds in a second, for struct timespec. */
enum { CW_NSECS_PER_SEC = 1000000000 };




int cw_timestamp_compare_internal(const struct timeval *earlier, const struct timeval *later);
int cw_timestamp_validate_internal(struct timeval *out_timestamp, const volatile struct timeval *in_timestamp);
void cw_usecs_to_timespec_internal(struct timespec *t, int usecs);
void cw_nanosleep_internal(const struct timespec *n);

#if (defined(LIBCW_WITH_ALSA) || defined(LIBCW_WITH_PULSEAUDIO))
#include <stdbool.h>

bool cw_dlopen_internal(const char *name, void **handle);
#endif

void cw_finalization_schedule_internal(void);
void cw_finalization_cancel_internal(void);


#ifdef LIBCW_UNIT_TESTS
#define CW_STATIC_FUNC
#else
#define CW_STATIC_FUNC static
#endif




#endif /* #ifndef H_LIBCW_UTILS */
