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


#include "config.h"


#define _BSD_SOURCE   /* usleep() */
#define _POSIX_SOURCE /* sigaction() */
#define _POSIX_C_SOURCE 200112L /* pthread_sigmask() */


#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#include "libcw_internal.h"
#include "libcw_debug.h"



extern const char *cw_audio_system_labels[];

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





/* Current debug flags setting; no debug unless requested. */
unsigned int cw_debug_flags = CW_DEBUG_SYSTEM; //CW_DEBUG_TONE_QUEUE; //CW_DEBUG_KEYER_STATES | CW_DEBUG_KEYER_STATES_VERBOSE | CW_DEBUG_STRAIGHT_KEY | CW_DEBUG_KEYING; // | CW_DEBUG_TONE_QUEUE;





/**
   \brief Set a value of internal debug flags variable

   Assign specified value to library's internal debug flags variable.
   Note that this function doesn't *append* given flag to the variable,
   it erases existing value and assigns new one. Use cw_get_debug_flags()
   if you want to OR new flag with existing ones.

   \param new_value - new value to be assigned to the library
*/
void cw_set_debug_flags(unsigned int new_value)
{
	cw_debug_flags = new_value;
	return;
}





/**
   \brief Get current library's debug flags

   Function returns value of library's internal debug variable.

   \return value of library's debug flags variable
*/
unsigned int cw_get_debug_flags(void)
{
	/* TODO: extract reading LIBCW_DEBUG env
	   variable to separate function. */

	static bool is_initialized = false;

	if (!is_initialized) {
		/* Do not overwrite any debug flags already set. */
		if (cw_debug_flags == 0) {

			/*
			 * Set the debug flags from LIBCW_DEBUG.  If it is an invalid
			 * numeric, treat it as 0; there is no error checking.
			 */
			const char *debug_value = getenv("LIBCW_DEBUG");
			if (debug_value) {
				cw_debug_flags = strtoul(debug_value, NULL, 0);
			}
		}

		is_initialized = true;
	}

	return cw_debug_flags;
}





/**
   \brief Check if given debug flag is set

   Function checks if a specified debug flag is set in internal
   variable of libcw library.

   \param flag - flag to be checked.

   \return true if given flag is set
   \return false if given flag is not set
*/
bool cw_is_debugging_internal(unsigned int flag)
{
	return cw_get_debug_flags() & flag;
}





#ifdef LIBCW_WITH_DEV





void cw_dev_debug_print_generator_setup(cw_gen_t *gen)
{
	fprintf(stderr, "audio system:         %s\n",     cw_audio_system_labels[gen->audio_system]);
	if (gen->audio_system == CW_AUDIO_OSS) {
		fprintf(stderr, "OSS version           %X.%X.%X\n",
			gen->oss_version.x, gen->oss_version.y, gen->oss_version.z);
	}
	fprintf(stderr, "audio device:         \"%s\"\n",  gen->audio_device);
	fprintf(stderr, "sample rate:          %d Hz\n",  gen->sample_rate);

#ifdef LIBCW_WITH_PULSEAUDIO
	if (gen->audio_system == CW_AUDIO_PA) {
		fprintf(stderr, "PulseAudio latency:   %llu us\n", (unsigned long long int) gen->pa_data.latency_usecs);

		if (gen->pa_data.ba.prebuf == (uint32_t) -1) {
			fprintf(stderr, "PulseAudio prebuf:    (not set)\n");
		} else {
			fprintf(stderr, "PulseAudio prebuf:    %u bytes\n", (uint32_t) gen->pa_data.ba.prebuf);
		}

		if (gen->pa_data.ba.tlength == (uint32_t) -1) {
			fprintf(stderr, "PulseAudio tlength:   (not set)\n");
		} else {
			fprintf(stderr, "PulseAudio tlength:   %u bytes\n", (uint32_t) gen->pa_data.ba.tlength);
		}

		if (gen->pa_data.ba.minreq == (uint32_t) -1) {
			fprintf(stderr, "PulseAudio minreq:    (not set)\n");
		} else {
			fprintf(stderr, "PulseAudio minreq:    %u bytes\n", (uint32_t) gen->pa_data.ba.minreq);
		}

		if (gen->pa_data.ba.maxlength == (uint32_t) -1) {
			fprintf(stderr, "PulseAudio maxlength: (not set)\n");
		} else {
			fprintf(stderr, "PulseAudio maxlength: %u bytes\n", (uint32_t) gen->pa_data.ba.maxlength);
		}

#if 0	        /* not relevant to playback */
		if (gen->pa_data.ba.fragsize == (uint32_t) -1) {
			fprintf(stderr, "PulseAudio fragsize:  (not set)\n");
		} else {
			fprintf(stderr, "PulseAudio fragsize:  %u bytes\n", (uint32_t) gen->pa_data.ba.fragsize);
		}
#endif

	}
#endif // #ifdef LIBCW_WITH_PULSEAUDIO

	fprintf(stderr, "send speed:           %d wpm\n", gen->send_speed);
	fprintf(stderr, "volume:               %d %%\n",  gen->volume_percent);
	fprintf(stderr, "frequency:            %d Hz\n",  gen->frequency);
	fprintf(stderr, "audio buffer size:    %d\n",     gen->buffer_n_samples);

	fprintf(stderr, "debug sink file:      %s\n", gen->dev_raw_sink != -1 ? "yes" : "no");

	return;
}





int cw_dev_debug_raw_sink_write_internal(cw_gen_t *gen)
{
	if (gen->audio_system == CW_AUDIO_NONE
	    || gen->audio_system == CW_AUDIO_NULL
	    || gen->audio_system == CW_AUDIO_CONSOLE) {

		return CW_SUCCESS;
	}

	if (gen->dev_raw_sink != -1) {
#if CW_DEV_RAW_SINK_MARKERS
		/* FIXME: this will cause memory access error at
		   the end, when generator is destroyed in the
		   other thread */
		gen->buffer[0] = 0x7fff;
		gen->buffer[1] = 0x7fff;
		gen->buffer[samples - 2] = 0x8000;
		gen->buffer[samples - 1] = 0x8000;
#endif

		int n_bytes = sizeof (gen->buffer[0]) * gen->buffer_n_samples;

		int rv = write(gen->dev_raw_sink, gen->buffer, n_bytes);
		if (rv == -1) {
			cw_dev_debug ("ERROR: write error: %s (gen->dev_raw_sink = %ld, gen->buffer = %ld, n_bytes = %d)", strerror(errno), (long) gen->dev_raw_sink, (long) gen->buffer, n_bytes);
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}





#endif /* #ifdef LIBCW_WITH_DEV */






