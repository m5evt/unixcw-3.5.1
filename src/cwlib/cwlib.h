/* vi: set ts=2 shiftwidth=2 expandtab:
 *
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
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

#ifndef _CWLIB_H
#define _CWLIB_H

#include <sys/time.h>  /* For struct timeval */

#if defined(__cplusplus)
extern "C"
{
#endif

/* supported audio sound systems */
enum {
	CW_AUDIO_NONE = 0,
	CW_AUDIO_OSS = 1
};


typedef struct {
	int sound_system;

	int volume; /* this is the level of sound that you want to have */
	int frequency;   /* this is the frequency of sound that you want to generate */

	int sample_rate; /* set to the same value of sample rate as
			    you have used when configuring sound card */
	int audio_sink; /* output file for audio data (OSS) */
	int debug_sink; /* output file for debug data */

	int slope; /* used to control initial and final phase of
		      non-zero-amplitude sine wave; slope/attack
		      makes it possible to start or end a wave
		      without clicks;
		      this field provides a very convenient way to
		      turn on/off a sound, just assign:
		      +CW_OSS_GENERATOR_SLOPE to turn sound on,
		      -CW_OSS_GENERATOR_SLOPE to turn sound off */

	/* start/stop flag;
	   set to 1 before creating generator;
	   set to 0 to stop generator; generator gets "destroyed"
	   handling the flag is wrapped in cw_oss_start_generator()
	   and cw_oss_stop_generator() */
	int generate;

	/* these are generator's internal state variables; */
	int amplitude; /* current amplitude of generated sine wave
			  (as in x(t) = A * sin(t)); in fixed/steady state
			  the amplitude is either zero or .volume */

	double phase_offset;
	double phase;

	pthread_t thread;

} cw_gen_t;





/*
 * Representation characters for Dot and Dash.  Only the following
 * characters are permitted in Morse representation strings.
 */
enum { CW_DOT_REPRESENTATION = '.', CW_DASH_REPRESENTATION = '-' };

/* Debug levels definitions. */
enum
{ CW_DEBUG_SILENT = 1 << 0,          /* Suppresses KIOCSOUND ioctls */
  CW_DEBUG_KEYING = 1 << 1,          /* Print out keying control data */
  CW_DEBUG_SOUND = 1 << 2,           /* Print out tone generation data */
  CW_DEBUG_TONE_QUEUE = 1 << 3,      /* Print out tone queue data */
  CW_DEBUG_PARAMETERS = 1 << 4,      /* Print out timing parameters */
  CW_DEBUG_RECEIVE_STATES = 1 << 5,  /* Print out receive state changes */
  CW_DEBUG_KEYER_STATES = 1 << 6,    /* Print out keyer information */
  CW_DEBUG_STRAIGHT_KEY = 1 << 7,    /* Print out straight key information */
  CW_DEBUG_LOOKUPS = 1 << 8,         /* Print out table lookup results */
  CW_DEBUG_FINALIZATION = 1 << 9,    /* Print out finalization actions */
  CW_DEBUG_MASK = (1 << 10) - 1      /* Bit mask of used debug bits */
};

/* CW library function prototypes. */
extern int cw_version (void);
extern void cw_license (void);
extern int cw_generator_new(int audio_system);
extern void cw_generator_delete(void);
extern int  cw_generator_start(void);
extern void cw_generator_stop(void);

extern void cw_set_debug_flags (unsigned int new_value);
extern unsigned int cw_get_debug_flags (void);
extern int cw_get_character_count (void);
extern void cw_list_characters (char *list);
extern int cw_get_maximum_representation_length (void);
extern int cw_lookup_character (char c, char *representation);
extern int cw_check_representation (const char *representation);
extern int cw_lookup_representation (const char *representation, char *c);
extern int cw_get_procedural_character_count (void);
extern void cw_list_procedural_characters (char *list);
extern int cw_get_maximum_procedural_expansion_length (void);
extern int cw_lookup_procedural_character (char c, char *representation,
                                           int *is_usually_expanded);
extern int cw_get_maximum_phonetic_length (void);
extern int cw_lookup_phonetic (char c, char *phonetic);
extern void cw_get_speed_limits (int *min_speed, int *max_speed);
extern void cw_get_frequency_limits (int *min_frequency, int *max_frequency);
extern void cw_get_volume_limits (int *min_volume, int *max_volume);
extern void cw_get_gap_limits (int *min_gap, int *max_gap);
extern void cw_get_tolerance_limits (int *min_tolerance, int *max_tolerance);
extern void cw_get_weighting_limits (int *min_weighting, int *max_weighting);
extern void cw_reset_send_receive_parameters (void);
extern int cw_set_send_speed (int new_value);
extern int cw_set_receive_speed (int new_value);
extern int cw_set_frequency (int new_value);
extern int cw_set_volume (int new_value);
extern int cw_set_gap (int new_value);
extern int cw_set_tolerance (int new_value);
extern int cw_set_weighting (int new_value);
extern int cw_get_send_speed (void);
extern int cw_get_receive_speed (void);
extern int cw_get_frequency (void);
extern int cw_get_volume (void);
extern int cw_get_gap (void);
extern int cw_get_tolerance (void);
extern int cw_get_weighting (void);
extern void cw_get_send_parameters (int *dot_usecs, int *dash_usecs,
                                    int *end_of_element_usecs,
                                    int *end_of_character_usecs,
                                    int *end_of_word_usecs,
                                    int *additional_usecs,
                                    int *adjustment_usecs);
extern void cw_get_receive_parameters (int *dot_usecs, int *dash_usecs,
                                       int *dot_min_usecs, int *dot_max_usecs,
                                       int *dash_min_usecs, int *dash_max_usecs,
                                       int *end_of_element_min_usecs,
                                       int *end_of_element_max_usecs,
                                       int *end_of_element_ideal_usecs,
                                       int *end_of_character_min_usecs,
                                       int *end_of_character_max_usecs,
                                       int *end_of_character_ideal_usecs,
                                       int *adaptive_threshold);
extern int cw_set_noise_spike_threshold (int threshold);
extern int cw_get_noise_spike_threshold (void);
extern void cw_block_callback (int is_block);
extern void cw_set_console_file (const char *new_value);
extern const char *cw_get_console_file (void);
extern void cw_set_soundcard_file (const char *new_value);
extern const char *cw_get_soundcard_file (void);
extern void cw_set_soundmixer_file (const char *new_value);
extern const char *cw_get_soundmixer_file (void);
extern int cw_is_soundcard_possible (void);
extern int cw_is_console_possible (void);
extern void cw_set_console_sound (int sound_state);
extern int cw_get_console_sound (void);
extern void cw_set_soundcard_sound (int sound_state);
extern int cw_get_soundcard_sound (void);
extern void cw_complete_reset (void);
extern int cw_register_signal_handler (int signal_number,
                                       void (*callback_func) (int));
extern int cw_unregister_signal_handler (int signal_number);
extern void cw_register_keying_callback (void (*callback_func) (void*, int),
                                         void *callback_arg);
extern int cw_register_tone_queue_low_callback (void (*callback_func) (void*),
                                                void *callback_arg, int level);
extern int cw_is_tone_busy (void);
extern int cw_wait_for_tone (void);
extern int cw_wait_for_tone_queue (void);
extern int cw_wait_for_tone_queue_critical (int level);
extern int cw_is_tone_queue_full (void);
extern int cw_get_tone_queue_capacity (void);
extern int cw_get_tone_queue_length (void);
extern void cw_flush_tone_queue (void);
extern int cw_queue_tone (int usecs, int frequency);
extern void cw_reset_tone_queue (void);
extern int cw_send_dot (void);
extern int cw_send_dash (void);
extern int cw_send_character_space (void);
extern int cw_send_word_space (void);
extern int cw_send_representation (const char *representation);
extern int cw_send_representation_partial (const char *representation);
extern int cw_check_character (char c);
extern int cw_send_character (char c);
extern int cw_send_character_partial (char c);
extern int cw_check_string (const char *string);
extern int cw_send_string (const char *string);
extern void cw_get_receive_statistics (double *dot_sd, double *dash_sd,
                                       double *element_end_sd,
                                       double *character_end_sd);
extern void cw_reset_receive_statistics (void);
extern void cw_enable_adaptive_receive (void);
extern void cw_disable_adaptive_receive (void);
extern int cw_get_adaptive_receive_state (void);
extern int cw_start_receive_tone (const struct timeval *timestamp);
extern int cw_end_receive_tone (const struct timeval *timestamp);
extern int cw_receive_buffer_dot (const struct timeval *timestamp);
extern int cw_receive_buffer_dash (const struct timeval *timestamp);
extern int cw_receive_representation (const struct timeval *timestamp,
                                      char *representation,
                                      int *is_end_of_word, int *is_error);
extern int cw_receive_character (const struct timeval *timestamp,
                                 char *c, int *is_end_of_word, int *is_error);
extern void cw_clear_receive_buffer (void);
extern int cw_get_receive_buffer_capacity (void);
extern int cw_get_receive_buffer_length (void);
extern void cw_reset_receive (void);
extern void cw_enable_iambic_curtis_mode_b (void);
extern void cw_disable_iambic_curtis_mode_b (void);
extern int cw_get_iambic_curtis_mode_b_state (void);
extern int cw_notify_keyer_paddle_event (int dot_paddle_state,
                                         int dash_paddle_state);
extern int cw_notify_keyer_dot_paddle_event (int dot_paddle_state);
extern int cw_notify_keyer_dash_paddle_event (int dash_paddle_state);
extern void cw_get_keyer_paddles (int *dot_paddle_state,
                                  int *dash_paddle_state);
extern void cw_get_keyer_paddle_latches (int *dot_paddle_latch_state,
                                         int *dash_paddle_latch_state);
extern int cw_is_keyer_busy (void);
extern int cw_wait_for_keyer_element (void);
extern int cw_wait_for_keyer (void);
extern void cw_reset_keyer (void);
extern int cw_notify_straight_key_event (int key_state);
extern int cw_get_straight_key_state (void);
extern int cw_is_straight_key_busy (void);
extern void cw_reset_straight_key (void);

#if defined(__cplusplus)
}
#endif
#endif  /* _CWLIB_H */
