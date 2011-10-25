/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011       Kamil Ignacak (acerion@wp.pl.)
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


/* Code that is common for all _applications_ from unixcw package.
   Wrappers for some cwlib functions, that probably don't belong
   to cwlib.c. */


#include <stdio.h>  /* fprintf(stderr, ...) */
#include <stdlib.h> /* malloc() / free() */

#include "cwlib.h"
#include "cw_common.h"

static void cw_generator_apply_config(cw_config_t *config);


enum { FALSE = 0, TRUE = !FALSE };


cw_config_t *cw_config_new(void)
{
	cw_config_t *config = (cw_config_t *) malloc(sizeof (cw_config_t));
	if (!config) {
		fprintf(stderr, "cwlib: can't allocate memory for configuration\n");
		return NULL;
	}

	config->audio_system = CW_AUDIO_NONE;
	config->audio_device = NULL;
	config->send_speed = CW_SPEED_INITIAL;
	config->frequency = CW_FREQUENCY_INITIAL;
	config->volume = CW_VOLUME_INITIAL;
	config->gap = CW_GAP_INITIAL;
	config->weighting = CW_WEIGHTING_INITIAL;
	config->practice_time = CW_PRACTICE_TIME_INITIAL;
	config->input_file = NULL;
	config->output_file = NULL;

	config->is_cw = 0;
	config->has_practice_time = 0;
	config->has_outfile = 0;

	config->do_echo = TRUE;
	config->do_errors = TRUE;
	config->do_commands = TRUE;
	config->do_combinations = TRUE;
	config->do_comments = TRUE;

	return config;
}



void cw_config_delete(cw_config_t **config)
{
	if (*config) {
		if ((*config)->audio_device) {
			free((*config)->audio_device);
			(*config)->audio_device = NULL;
		}
		if ((*config)->input_file) {
			free((*config)->input_file);
			(*config)->input_file = NULL;
		}
		if ((*config)->output_file) {
			free((*config)->output_file);
			(*config)->output_file = NULL;
		}
		free(*config);
		*config = NULL;
	}

	return;
}




/*
 * cw_config_is_valid()
 *
 * Check consistency and correctness of configuration.
 */
int cw_config_is_valid(cw_config_t *config)
{
	/* Deal with odd argument combinations. */
        if (config->audio_device) {
		if (config->audio_system == CW_AUDIO_SOUNDCARD) {
			fprintf(stderr, "cwlib: a device has been specified for 'soundcard' argument\n");
			fprintf(stderr, "cwlib: a device can be specified only for 'console', 'oss' or 'alsa'\n");
			return CW_FAILURE;
		} else {
			; /* audio_system is one that accepts custom "audio device" */
		}
	} else {
		; /* no custom "audio device" specified, a default will be used */
	}

	return CW_SUCCESS;
}






/* A wrapper for common functionality.
   For a lack of better place I put it in this file */
int cw_generator_new_from_config(cw_config_t *config, const char *argv0)
{

	if (config->audio_system == CW_AUDIO_NONE
	    || config->audio_system == CW_AUDIO_OSS
	    || config->audio_system == CW_AUDIO_SOUNDCARD) {

		if (cw_is_oss_possible(config->audio_device)) {
			if (cw_generator_new(CW_AUDIO_OSS, config->audio_device)) {
				cw_generator_apply_config(config);
				return CW_SUCCESS;
			} else {
				fprintf(stderr,
					"%s: failed to open OSS output with device \"%s\"\n",
					argv0, cw_get_soundcard_device());
			}
		}
		/* fall through to try with next audio system type */
	}


	if (config->audio_system == CW_AUDIO_NONE
	    || config->audio_system == CW_AUDIO_ALSA
	    || config->audio_system == CW_AUDIO_SOUNDCARD) {

		if (cw_is_alsa_possible(config->audio_device)) {
			if (cw_generator_new(CW_AUDIO_ALSA, config->audio_device)) {
				cw_generator_apply_config(config);
				return CW_SUCCESS;
			} else {
				fprintf(stderr,
					"%s: failed to open ALSA output with device \"%s\"\n",
					argv0, cw_get_soundcard_device());
			}
		}
		/* fall through to try with next audio system type */
	}


	if (config->audio_system == CW_AUDIO_NONE
	    || config->audio_system == CW_AUDIO_CONSOLE) {

		if (cw_is_console_possible(config->audio_device)) {
			if (cw_generator_new(CW_AUDIO_CONSOLE, config->audio_device)) {
				cw_generator_apply_config(config);
				return CW_SUCCESS;
			} else {
				fprintf(stderr,
					"%s: failed to open console output with device \"%s\"\n",
					argv0, cw_get_soundcard_device());
			}
		}
		/* fall through to try with next audio system type */
	}

	/* there is no next audio system type to try */
	return CW_FAILURE;
}





void cw_generator_apply_config(cw_config_t *config)
{
	cw_set_frequency(config->frequency);
	cw_set_volume(config->volume);
	cw_set_send_speed(config->send_speed);
	cw_set_gap(config->gap);
	cw_set_weighting(config->weighting);

	return;
}





void cw_start_beep(void)
{
	cw_flush_tone_queue();
	cw_queue_tone(20000, 500);
	cw_queue_tone(20000, 1000);
	cw_wait_for_tone_queue();
	return;
}





void cw_end_beep(void)
{
      cw_flush_tone_queue();
      cw_queue_tone(20000, 500);
      cw_queue_tone(20000, 1000);
      cw_queue_tone(20000, 500);
      cw_queue_tone(20000, 1000);
      cw_wait_for_tone_queue();
      return;
}


