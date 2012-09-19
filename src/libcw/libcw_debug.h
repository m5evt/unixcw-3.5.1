/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2012  Kamil Ignacak (acerion@wp.pl)
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


#ifndef H_LIBCW_DEBUG
#define H_LIBCW_DEBUG


#include <stdbool.h>


#include "libcw_internal.h"


#if defined(__cplusplus)
extern "C"
{
#endif


#define CW_DEBUG_N_EVENTS_MAX (1024 * 128)

typedef struct {

	FILE *file; /* File to which events will be written. */

	int flags;  /* Unused at the moment. */

	struct {
		int event;        /* Event ID. One of values from enum below. */
		long long sec;    /* Time of registering the event - second. */
		long long usec;   /* Time of registering the event - microsecond. */
	} events[CW_DEBUG_N_EVENTS_MAX];

	int n;       /* Event counter. */
	int n_max;   /* Flush threshold. */
} cw_debug_t;



cw_debug_t *cw_debug2_new(const char *filename);
void        cw_debug2_delete(cw_debug_t **debug);
void        cw_debug2(cw_debug_t *debug, int flag, int event);
void        cw_debug2_flush(cw_debug_t *debug);
bool        cw_is_debugging_internal(unsigned int flag);


/* macro supporting multiple arguments */
#define cw_debug(flag, ...)				\
	{						\
		if (cw_is_debugging_internal(flag)) {	\
			fprintf(stderr, "libcw: ");	\
			fprintf(stderr, __VA_ARGS__);	\
			fprintf(stderr, "\n");		\
		}					\
	}


	/* Debugging message for library developer */
#ifdef LIBCW_WITH_DEV
#define cw_dev_debug(...)						\
	{								\
		fprintf(stderr, "libcw: ");				\
		fprintf(stderr, "%s: %d: ", __func__, __LINE__);	\
		fprintf(stderr, __VA_ARGS__);				\
		fprintf(stderr, "\n");					\
	}
#else
#define cw_dev_debug(...) {}
#endif


enum {
	CW_DEBUG_EVENT_TONE_LOW  = 0,         /* Tone with non-zero frequency. */
	CW_DEBUG_EVENT_TONE_MID,              /* A state between LOW and HIGH, probably unused. */
	CW_DEBUG_EVENT_TONE_HIGH,             /* Tone with zero frequency. */
	CW_DEBUG_EVENT_TQ_JUST_EMPTIED,       /* A last tone from libcw's queue of tones has been dequeued, making the queue empty. */
	CW_DEBUG_EVENT_TQ_NONEMPTY,           /* A tone from libcw's queue of tones has been dequeued, but the queue is still non-empty. */
	CW_DEBUG_EVENT_TQ_STILL_EMPTY         /* libcw's queue of tones has been asked for tone, but there were no tones on the queue. */
};


#if CW_DEV_RAW_SINK
int  cw_dev_debug_raw_sink_write_internal(cw_gen_t *gen);
void cw_dev_debug_print_generator_setup(cw_gen_t *gen);
#endif


#if defined(__cplusplus)
}
#endif

#endif  /* H_LIBCW_DEBUG */
