/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2013  Kamil Ignacak (acerion@wp.pl)
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




#if defined(__cplusplus)
extern "C"
{
#endif


#define CW_DEBUG_N_EVENTS_MAX (1024 * 128)

typedef struct {

	uint32_t flags;  /* Unused at the moment. */

	int n;       /* Event counter. */
	int n_max;   /* Flush threshold. */

	/* Current debug level. */
	int level;

	/* Human-readable labels for debug levels. */
	const char **level_labels;

	struct {
		uint32_t event;   /* Event ID. One of values from enum below. */
		long long sec;    /* Time of registering the event - second. */
		long long usec;   /* Time of registering the event - microsecond. */
	} events[CW_DEBUG_N_EVENTS_MAX];

} cw_debug_t;



void     cw_debug_set_flags(cw_debug_t *debug_object, uint32_t flags);
uint32_t cw_debug_get_flags(cw_debug_t *debug_object);
void     cw_debug_print_flags(cw_debug_t *debug_object);
bool     cw_debug_has_flag(cw_debug_t *debug_object, uint32_t flag);
void     cw_debug_event_internal(cw_debug_t *debug_object, uint32_t flag, uint32_t event, const char *func, int line);



void     cw_set_debug_flags(uint32_t flags)    __attribute__ ((deprecated));
uint32_t cw_get_debug_flags(void)              __attribute__ ((deprecated));



#define cw_debug_msg(debug_object, flag, debug_level, ...) {		\
	if (debug_level >= debug_object->level) {			\
		if (debug_object->flags & flag) {			\
			fprintf(stderr, "%s:", debug_object->level_labels[debug_level]); \
			if (debug_level == CW_DEBUG_DEBUG) {		\
				fprintf(stderr, "%s: %d: ", __func__, __LINE__); \
			}						\
			fprintf(stderr, __VA_ARGS__);			\
			fprintf(stderr, "\n");				\
		}							\
	}								\
}





#define cw_debug_ev(debug_object, flag, event)				\
	{								\
		cw_debug_event_internal(debug_object, flag, event, __func__, __LINE__); \
	}





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
