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



/**
   \file libcw_debug.c

   \brief An attempt to do some smart debugging facility to libcw and
   applications using the library.

*/


#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#include "libcw_debug.h"


struct {
	int flag;
	const char *message;
} cw_debug_events[] = {
	{ CW_DEBUG_EVENT_TONE_LOW,        "CW_DEBUG_EVENT_TONE_LOW"        },
	{ CW_DEBUG_EVENT_TONE_MID,        "CW_DEBUG_EVENT_TONE_MID"        },
	{ CW_DEBUG_EVENT_TONE_HIGH,       "CW_DEBUG_EVENT_TONE_HIGH"       },

	{ CW_DEBUG_EVENT_TQ_JUST_EMPTIED, "CW_DEBUG_EVENT_TQ_JUST_EMPTIED" },
	{ CW_DEBUG_EVENT_TQ_NONEMPTY,     "CW_DEBUG_EVENT_TQ_NONEMPTY"     },
	{ CW_DEBUG_EVENT_TQ_STILL_EMPTY,  "CW_DEBUG_EVENT_TQ_STILL_EMPTY"  }
};





/**
   \brief Create new debug object

   Function accepts "stdout" and "stderr" as output file names,
   in addition to regular disk files.

   \param filename - name of output file

   \return debug object on success
   \return NULL on failure
*/
cw_debug_t *cw_debug2_new(const char *filename)
{
	cw_debug_t *debug = (cw_debug_t *) malloc(sizeof (cw_debug_t));
	if (!debug) {
		fprintf(stderr, "ERROR: failed to allocate debug object\n");
		return (cw_debug_t *) NULL;
	}

	if (!strcmp(filename, "stderr")) {
		debug->file = stderr;
	} else if (!strcmp(filename, "stdout")) {
		debug->file = stdout;
	} else if (filename) {
		debug->file = fopen(filename, "w");
		if (!debug->file) {
			fprintf(stderr, "ERROR: failed to open debug file \"%s\"\n", filename);
			free(debug);
			debug = (cw_debug_t *) NULL;

			return (cw_debug_t *) NULL;
		}
	} else {
		;
	}

	debug->n = 0;
	debug->n_max = CW_DEBUG_N_EVENTS_MAX;
	debug->flags = 0;

	return debug;
}





/**
   \brief Delete debug object

   Flush all events still stored in the debug object, and delete the object.
   Function sets \p *debug to NULL after deleting the object.

   \param debug - pointer to debug object to delete
*/
void cw_debug2_delete(cw_debug_t **debug)
{
	if (!debug) {
		fprintf(stderr, "ERROR: %s(): NULL pointer to debug object\n", __func__);
		return;
	}

	if (!*debug) {
		fprintf(stderr, "WARNING: %s(): NULL debug object\n", __func__);
		return;
	}

	cw_debug2_flush(*debug);

	if ((*debug)->file != 0 && (*debug)->file != stdout && (*debug)->file != stderr) {
		fclose((*debug)->file);
		(*debug)->file = 0;
	}

	free(*debug);
	*debug = (cw_debug_t *) NULL;

	return;
}





/**
   \brief Store an event in debug object

   \param debug - debug object
   \param flag - unused
   \param event - event ID
*/
void cw_debug2(cw_debug_t *debug, int flag, int event)
{
	if (!debug) {
		return;
	}

	if ((debug->flags & flag) != flag) {
		return;
	}

	struct timeval now;
	gettimeofday(&now, NULL);

	debug->events[debug->n].event = event;
	debug->events[debug->n].sec = (long long int) now.tv_sec;
	debug->events[debug->n].usec = (long long int) now.tv_usec;

	debug->n++;

	if (debug->n >= debug->n_max) {
		cw_debug2_flush(debug);
		debug->n = 0;
	}

	return;
}





/**
   \brief Write all events from the debug object to a file

   Function writes all events stored in the \p debug object to file
   associated with the object, and removes the events.

   List of events is preceded with "FLUSH START\n" line, and
   followed by "FLUSH END\n" line.

   \param debug - debug object
*/
void cw_debug2_flush(cw_debug_t *debug)
{
	if (debug->n <= 0) {
		return;
	}

	long long int diff = debug->events[debug->n - 1].sec - debug->events[0].sec;
	diff = debug->events[debug->n - 1].sec - diff - 1;

	fprintf(debug->file, "FLUSH START\n");
	for (int i = 0; i < debug->n; i++) {
		fprintf(debug->file, "libcwevent:\t%06lld%06lld\t%s\n",
			debug->events[i].sec - diff, debug->events[i].usec,
			cw_debug_events[debug->events[i].event].message);
	}
	fprintf(debug->file, "FLUSH END\n");

	fflush(debug->file);

	return;
}


