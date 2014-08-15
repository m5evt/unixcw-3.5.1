/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2014  Kamil Ignacak (acerion@wp.pl)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


/*
   Table of contents

   - Section:Morse code controls and timing parameters
   - Section:Finalization and cleanup
   - Section:Sending
   - Section:Receive tracking and statistics helpers
   - Section:Receiving
   - Section:Global variables
*/

#include "config.h"


#include <unistd.h>
#include <stdlib.h>

#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>  /* sqrt() */



#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif



/* After moving these definitions here, it turns out that they are no
   longer necessary, since they are redefinitions of definitions
   already existing in /usr/include/features.h */
#ifndef __FreeBSD__
// #define _BSD_SOURCE   /* usleep() */
// #define _POSIX_SOURCE /* sigaction() */
// #define _POSIX_C_SOURCE 200112L /* pthread_sigmask() */
#endif


#if defined(BSD)
# define ERR_NO_SUPPORT EPROTONOSUPPORT
#else
# define ERR_NO_SUPPORT EPROTO
#endif





#include "libcw.h"
#include "libcw_internal.h"
#include "libcw_data.h"
#include "libcw_utils.h"
#include "libcw_gen.h"
#include "libcw_signal.h"
#include "libcw_debug.h"





/* ******************************************************************** */
/*                            Section:Sending                           */
/* ******************************************************************** */
static int cw_send_element_internal(cw_gen_t *gen, char element);
static int cw_send_representation_internal(cw_gen_t *gen, const char *representation, bool partial);
static int cw_send_character_internal(cw_gen_t *gen, char character, int partial);





/* ******************************************************************** */
/*          Section:Morse code controls and timing parameters           */
/* ******************************************************************** */

/* Dot length magic number; from PARIS calibration, 1 Dot=1200000/WPM usec. */
enum { DOT_CALIBRATION = 1200000 };


/* Default initial values for library controls. */
enum {
	CW_REC_ADAPTIVE_INITIAL  = false,  /* Initial adaptive receive setting */
	CW_REC_INITIAL_THRESHOLD = (DOT_CALIBRATION / CW_SPEED_INITIAL) * 2,   /* Initial adaptive speed threshold */
	CW_REC_INITIAL_NOISE_THRESHOLD = (DOT_CALIBRATION / CW_SPEED_MAX) / 2  /* Initial noise filter threshold */
};





/* ******************************************************************** */
/*                 Section:Finalization and cleanup                     */
/* ******************************************************************** */
static void cw_finalization_clock_internal(void);





/* ******************************************************************** */
/*            Section:Receive tracking and statistics helpers           */
/* ******************************************************************** */
static void   cw_receiver_add_statistic_internal(cw_rec_t *rec, stat_type_t type, int usecs);
static double cw_receiver_get_statistic_internal(cw_rec_t *rec, stat_type_t type);

static void cw_reset_adaptive_average_internal(cw_tracking_t *tracking, int initial);
static void cw_update_adaptive_average_internal(cw_tracking_t *tracking, int element_len_usecs);
static int  cw_get_adaptive_average_internal(cw_tracking_t *tracking);





/* ******************************************************************** */
/*                           Section:Receiving                          */
/* ******************************************************************** */

/* "RS" stands for "Receiver State" */
enum {
	RS_IDLE,          /* Representation buffer is empty and ready to accept data. */
	RS_IN_TONE,       /* Mark */
	RS_AFTER_TONE,    /* Space */
	RS_END_CHAR,      /* After receiving a character, without error. */
	RS_END_WORD,      /* After receiving a word, without error. */
	RS_ERR_CHAR,      /* TODO: shouldn't it be RS_END_WORD_CHAR, as in, "After receiving a character, but with error"? */
	RS_ERR_WORD       /* TODO: shouldn't it be RS_END_WORD_ERR, as in, "After receiving a word, but with error"? */
};


static const char *cw_receiver_states[] = {
	"RS_IDLE",
	"RS_IN_TONE",
	"RS_AFTER_TONE",
	"RS_END_CHAR",
	"RS_END_WORD",
	"RS_ERR_CHAR",
	"RS_ERR_WORD"
};



cw_rec_t cw_receiver = { .state = RS_IDLE,

			     .speed = CW_SPEED_INITIAL,

			     .noise_spike_threshold = CW_REC_INITIAL_NOISE_THRESHOLD,
			     .is_adaptive_receive_enabled = CW_REC_ADAPTIVE_INITIAL,
			     .adaptive_receive_threshold = CW_REC_INITIAL_THRESHOLD,
			     .tolerance = CW_TOLERANCE_INITIAL,

			     .tone_start = { 0, 0 },
			     .tone_end =   { 0, 0 },

			     .representation_ind = 0,

			     .dot_length = 0,
			     .dash_length = 0,
			     .dot_range_minimum = 0,
			     .dot_range_maximum = 0,
			     .dash_range_minimum = 0,
			     .dash_range_maximum = 0,
			     .eoe_range_minimum = 0,
			     .eoe_range_maximum = 0,
			     .eoe_range_ideal = 0,
			     .eoc_range_minimum = 0,
			     .eoc_range_maximum = 0,
			     .eoc_range_ideal = 0,

			     .statistics_ind = 0,
			     .statistics = { {0, 0} },

			     .dot_tracking  = { {0}, 0, 0 },
			     .dash_tracking = { {0}, 0, 0 }
};





static void cw_receiver_set_adaptive_internal(cw_rec_t *rec, bool flag);
static int  cw_receiver_identify_tone_internal(cw_rec_t *rec, int element_len_usecs, char *representation);
static void cw_receiver_update_adaptive_tracking_internal(cw_rec_t *rec, int element_len_usecs, char element);
static int  cw_receiver_add_element_internal(cw_rec_t *rec, const struct timeval *timestamp, char element);





/* ******************************************************************** */
/*                    Section:Global variables                          */
/* ******************************************************************** */

extern cw_gen_t *cw_generator;

extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;





/* ******************************************************************** */
/*          Section:Morse code controls and timing parameters           */
/* ******************************************************************** */





/* Both generator and receiver contain a group of low-level timing
   parameters that should be recalculated (synchronized) on some
   events. This is a flag that allows us to decide whether it's time
   to recalculate the low-level parameters.

   TODO: this variable should be a field in either cw_gen_t or
   cw_receiver_t. */
static bool cw_is_in_sync = false;





/**
   \brief Get speed limits

   Get (through function's arguments) limits on speed of morse code that
   can be generated by generator.

   See CW_SPEED_MIN and CW_SPEED_MAX in libcw.h for values.

   testedin::test_cw_get_x_limits()

   \param min_speed - minimal allowed speed
   \param max_speed - maximal allowed speed
*/
void cw_get_speed_limits(int *min_speed, int *max_speed)
{
	if (min_speed) {
		*min_speed = CW_SPEED_MIN;
	}

	if (max_speed) {
		*max_speed = CW_SPEED_MAX;
	}
	return;
}





/**
   \brief Get frequency limits

   Get (through function's arguments) limits on frequency that can
   be generated by generator.

   See CW_FREQUENCY_MIN and CW_FREQUENCY_MAX in libcw.h for values.

   testedin::test_cw_get_x_limits()

   \param min_frequency - minimal allowed frequency
   \param max_frequency - maximal allowed frequency
*/
void cw_get_frequency_limits(int *min_frequency, int *max_frequency)
{
	if (min_frequency) {
		*min_frequency = CW_FREQUENCY_MIN;
	}

	if (max_frequency) {
		*max_frequency = CW_FREQUENCY_MAX;
	}
	return;
}





/**
   \brief Get volume limits

   Get (through function's arguments) limits on volume of sound
   generated by generator.

   See CW_VOLUME_MIN and CW_VOLUME_MAX in libcw.h for values.

   testedin::test_cw_get_x_limits()
   testedin::test_volume_functions()

   \param min_volume - minimal allowed volume
   \param max_volume - maximal allowed volume
*/
void cw_get_volume_limits(int *min_volume, int *max_volume)
{
	if (min_volume) {
		*min_volume = CW_VOLUME_MIN;
	}
	if (max_volume) {
		*max_volume = CW_VOLUME_MAX;
	}
	return;
}





/**
   \brief Get gap limits

   Get (through function's arguments) limits on gap in cw signal
   generated by generator.

   See CW_GAP_MIN and CW_GAP_MAX in libcw.h for values.

   testedin::test_cw_get_x_limits()

   \param min_gap - minimal allowed gap
   \param max_gap - maximal allowed gap
*/
void cw_get_gap_limits(int *min_gap, int *max_gap)
{
	if (min_gap) {
		*min_gap = CW_GAP_MIN;
	}
	if (max_gap) {
		*max_gap = CW_GAP_MAX;
	}
	return;
}





/**
   \brief Get tolerance limits

   Get (through function's arguments) limits on "tolerance" parameter
   of generator.

   See CW_TOLERANCE_MIN and CW_TOLERANCE_MAX in libcw.h for values.

   testedin::test_cw_get_x_limits()

   \param min_tolerance - minimal allowed tolerance
   \param max_tolerance - maximal allowed tolerance
*/
void cw_get_tolerance_limits(int *min_tolerance, int *max_tolerance)
{
	if (min_tolerance) {
		*min_tolerance = CW_TOLERANCE_MIN;
	}
	if (max_tolerance) {
		*max_tolerance = CW_TOLERANCE_MAX;
	}
	return;
}





/**
   \brief Get weighting limits

   Get (through function's arguments) limits on "weighting" parameter
   of generator.

   See CW_WEIGHTING_MIN and CW_WEIGHTING_MAX in libcw.h for values.

   testedin::test_cw_get_x_limits()

   \param min_weighting - minimal allowed weighting
   \param max_weighting - maximal allowed weighting
*/
void cw_get_weighting_limits(int *min_weighting, int *max_weighting)
{
	if (min_weighting) {
		*min_weighting = CW_WEIGHTING_MIN;
	}
	if (max_weighting) {
		*max_weighting = CW_WEIGHTING_MAX;
	}
	return;
}





/**
   \brief Synchronize send/receive parameters of the library

   Synchronize the dot, dash, end of element, end of character, and end
   of word timings and ranges to new values of Morse speed, "Farnsworth"
   gap, receive tolerance, or weighting.

   All the timing parameters are stored in \p gen and \p rec. The
   parameters for generator and receiver are almost completely
   independent. Almost.

   \param gen - generator variable, storing generation parameters
   \param rec - receiver variable, storing receiving parameters
*/
void cw_sync_parameters_internal(cw_gen_t *gen, cw_rec_t *rec)
{
	/* Do nothing if we are already synchronized with speed/gap. */
	if (cw_is_in_sync) {
		return;
	}


	/* Generator parameters */

	/* Set the length of a Dot to be a Unit with any weighting
	   adjustment, and the length of a Dash as three Dot lengths.
	   The weighting adjustment is by adding or subtracting a
	   length based on 50 % as a neutral weighting. */
	int unit_length = DOT_CALIBRATION / gen->send_speed;
	int weighting_length = (2 * (gen->weighting - 50) * unit_length) / 100;
	gen->dot_length = unit_length + weighting_length;
	gen->dash_length = 3 * gen->dot_length;

	/* An end of element length is one Unit, perhaps adjusted,
	   the end of character is three Units total, and end of
	   word is seven Units total.

	   The end of element length is adjusted by 28/22 times
	   weighting length to keep PARIS calibration correctly
	   timed (PARIS has 22 full units, and 28 empty ones).
	   End of element and end of character delays take
	   weightings into account. */
	gen->eoe_delay = unit_length - (28 * weighting_length) / 22;
	gen->eoc_delay = 3 * unit_length - gen->eoe_delay;
	gen->eow_delay = 7 * unit_length - gen->eoc_delay;
	gen->additional_delay = gen->gap * unit_length;

	/* For "Farnsworth", there also needs to be an adjustment
	   delay added to the end of words, otherwise the rhythm is
	   lost on word end.
	   I don't know if there is an "official" value for this,
	   but 2.33 or so times the gap is the correctly scaled
	   value, and seems to sound okay.

	   Thanks to Michael D. Ivey <ivey@gweezlebur.com> for
	   identifying this in earlier versions of libcw. */
	gen->adjustment_delay = (7 * gen->additional_delay) / 3;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_PARAMETERS, CW_DEBUG_INFO,
		      "libcw: send usec timings <%d [wpm]>: dot: %d, dash: %d, %d, %d, %d, %d, %d",
		      gen->send_speed, gen->dot_length, gen->dash_length,
		      gen->eoe_delay, gen->eoc_delay,
		      gen->eow_delay, gen->additional_delay, gen->adjustment_delay);





	/* Receiver parameters */

	/* First, depending on whether we are set for fixed speed or
	   adaptive speed, calculate either the threshold from the
	   receive speed, or the receive speed from the threshold,
	   knowing that the threshold is always, effectively, two dot
	   lengths.  Weighting is ignored for receive parameters,
	   although the core unit length is recalculated for the
	   receive speed, which may differ from the send speed. */
	unit_length = DOT_CALIBRATION / rec->speed;
	if (rec->is_adaptive_receive_enabled) {
		rec->speed = DOT_CALIBRATION
			/ (rec->adaptive_receive_threshold / 2);
	} else {
		rec->adaptive_receive_threshold = 2 * unit_length;
	}

	/* Calculate the basic receive dot and dash lengths. */
	rec->dot_length = unit_length;
	rec->dash_length = 3 * unit_length;

	/* Set the ranges of respectable timing elements depending
	   very much on whether we are required to adapt to the
	   incoming Morse code speeds. */
	if (rec->is_adaptive_receive_enabled) {
		/* For adaptive timing, calculate the Dot and
		   Dash timing ranges as zero to two Dots is a
		   Dot, and anything, anything at all, larger than
		   this is a Dash. */
		rec->dot_range_minimum = 0;
		rec->dot_range_maximum = 2 * rec->dot_length;
		rec->dash_range_minimum = rec->dot_range_maximum;
		rec->dash_range_maximum = INT_MAX;

		/* Make the inter-element gap be anything up to
		   the adaptive threshold lengths - that is two
		   Dots.  And the end of character gap is anything
		   longer than that, and shorter than five dots. */
		rec->eoe_range_minimum = rec->dot_range_minimum;
		rec->eoe_range_maximum = rec->dot_range_maximum;
		rec->eoc_range_minimum = rec->eoe_range_maximum;
		rec->eoc_range_maximum = 5 * rec->dot_length;

	} else {
		/* For fixed speed receiving, calculate the Dot
		   timing range as the Dot length +/- dot*tolerance%,
		   and the Dash timing range as the Dash length
		   including +/- dot*tolerance% as well. */
		int tolerance = (rec->dot_length * rec->tolerance) / 100;
		rec->dot_range_minimum = rec->dot_length - tolerance;
		rec->dot_range_maximum = rec->dot_length + tolerance;
		rec->dash_range_minimum = rec->dash_length - tolerance;
		rec->dash_range_maximum = rec->dash_length + tolerance;

		/* Make the inter-element gap the same as the Dot
		   range.  Make the inter-character gap, expected
		   to be three Dots, the same as Dash range at the
		   lower end, but make it the same as the Dash range
		   _plus_ the "Farnsworth" delay at the top of the
		   range.

		   Any gap longer than this is by implication
		   inter-word. */
		rec->eoe_range_minimum = rec->dot_range_minimum;
		rec->eoe_range_maximum = rec->dot_range_maximum;
		rec->eoc_range_minimum = rec->dash_range_minimum;
		rec->eoc_range_maximum = rec->dash_range_maximum
			/* NOTE: the only reference to generator
			   variables in code setting receiver
			   variables.  Maybe we could/should do a full
			   separation, and create
			   rec->additional_delay and
			   rec->adjustment_delay? */
			+ gen->additional_delay + gen->adjustment_delay;
	}

	/* For statistical purposes, calculate the ideal end of
	   element and end of character timings. */
	rec->eoe_range_ideal = unit_length;
	rec->eoc_range_ideal = 3 * unit_length;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_PARAMETERS, CW_DEBUG_INFO,
		      "libcw: receive usec timings <%d [wpm]>: dot: %d-%d [ms], dash: %d-%d [ms], %d-%d[%d], %d-%d[%d], thres: %d",
		      rec->speed,
		      rec->dot_range_minimum, rec->dot_range_maximum,
		      rec->dash_range_minimum, rec->dash_range_maximum,
		      rec->eoe_range_minimum, rec->eoe_range_maximum, rec->eoe_range_ideal,
		      rec->eoc_range_minimum, rec->eoc_range_maximum, rec->eoc_range_ideal,
		      rec->adaptive_receive_threshold);



	/* Set the "parameters in sync" flag. */
	cw_is_in_sync = true;

	return;
}





/**
   \brief Reset send/receive parameters

   Reset the library speed, frequency, volume, gap, tolerance, weighting,
   adaptive receive, and noise spike threshold to their initial default
   values: send/receive speed 12 WPM, volume 70 %, frequency 800 Hz,
   gap 0 dots, tolerance 50 %, and weighting 50 %.
*/
void cw_reset_send_receive_parameters(void)
{
	cw_generator->send_speed = CW_SPEED_INITIAL;
	cw_generator->frequency = CW_FREQUENCY_INITIAL;
	cw_generator->volume_percent = CW_VOLUME_INITIAL;
	cw_generator->volume_abs = (cw_generator->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
	cw_generator->gap = CW_GAP_INITIAL;
	cw_generator->weighting = CW_WEIGHTING_INITIAL;

	cw_receiver.speed = CW_SPEED_INITIAL;
	cw_receiver.tolerance = CW_TOLERANCE_INITIAL;
	cw_receiver.is_adaptive_receive_enabled = CW_REC_ADAPTIVE_INITIAL;
	cw_receiver.noise_spike_threshold = CW_REC_INITIAL_NOISE_THRESHOLD;

	/* Changes require resynchronization. */
	cw_is_in_sync = false;
	cw_sync_parameters_internal(cw_generator, &cw_receiver);

	return;
}





/**
   \brief Set sending speed of generator

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal value
   of send speed.

   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param new_value - new value of send speed to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_send_speed(int new_value)
{
	if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != cw_generator->send_speed) {
		cw_generator->send_speed = new_value;

		/* Changes of send speed require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, &cw_receiver);
	}

	return CW_SUCCESS;
}





/**
   \brief Set receiving speed of receiver

   See documentation of cw_set_send_speed() for more information.

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of receive speed.
   errno is set to EINVAL if \p new_value is out of range.
   errno is set to EPERM if adaptive receive speed tracking is enabled.

   testedin::test_parameter_ranges()

   \param new_value - new value of receive speed to be assigned to receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_receive_speed(int new_value)
{
	if (cw_receiver.is_adaptive_receive_enabled) {
		errno = EPERM;
		return CW_FAILURE;
	} else {
		if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX) {
			errno = EINVAL;
			return CW_FAILURE;
		}
	}

	if (new_value != cw_receiver.speed) {
		cw_receiver.speed = new_value;

		/* Changes of receive speed require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, &cw_receiver);
	}

	return CW_SUCCESS;
}





/**
   \brief Set frequency of generator

   Set frequency of sound wave generated by generator.
   The frequency must be within limits marked by CW_FREQUENCY_MIN
   and CW_FREQUENCY_MAX.

   See libcw.h/CW_FREQUENCY_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of frequency.

   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param new_value - new value of frequency to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_frequency(int new_value)
{
	if (new_value < CW_FREQUENCY_MIN || new_value > CW_FREQUENCY_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	} else {
		cw_generator->frequency = new_value;
		return CW_SUCCESS;
	}
}





/**
   \brief Set volume of generator

   Set volume of sound wave generated by generator.
   The volume must be within limits marked by CW_VOLUME_MIN and CW_VOLUME_MAX.

   Note that volume settings are not fully possible for the console speaker.
   In this case, volume settings greater than zero indicate console speaker
   sound is on, and setting volume to zero will turn off console speaker
   sound.

   See libcw.h/CW_VOLUME_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of volume.
   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_volume_functions()
   testedin::test_parameter_ranges()

   \param new_value - new value of volume to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_volume(int new_value)
{
	if (new_value < CW_VOLUME_MIN || new_value > CW_VOLUME_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	} else {
		cw_generator->volume_percent = new_value;
		cw_generator->volume_abs = (cw_generator->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;

		cw_generator_set_tone_slope(cw_generator, -1, -1);

		return CW_SUCCESS;
	}
}





/**
   \brief Set sending gap of generator

   See libcw.h/CW_GAP_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of gap.
   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param new_value - new value of gap to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_gap(int new_value)
{
	if (new_value < CW_GAP_MIN || new_value > CW_GAP_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != cw_generator->gap) {
		cw_generator->gap = new_value;

		/* Changes of gap require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, &cw_receiver);
	}

	return CW_SUCCESS;
}





/**
   \brief Set tolerance for receiver

   See libcw.h/CW_TOLERANCE_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of tolerance.
   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param new_value - new value of tolerance to be assigned to receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_tolerance(int new_value)
{
	if (new_value < CW_TOLERANCE_MIN || new_value > CW_TOLERANCE_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != cw_receiver.tolerance) {
		cw_receiver.tolerance = new_value;

		/* Changes of tolerance require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, &cw_receiver);
	}

	return CW_SUCCESS;
}





/**
   \brief Set sending weighting for generator

   See libcw.h/CW_WEIGHTING_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of weighting.
   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param new_value - new value of weighting to be assigned for generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_weighting(int new_value)
{
	if (new_value < CW_WEIGHTING_MIN || new_value > CW_WEIGHTING_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != cw_generator->weighting) {
		cw_generator->weighting = new_value;

		/* Changes of weighting require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, &cw_receiver);
	}

	return CW_SUCCESS;
}





/**
   \brief Get sending speed from generator

   testedin::test_parameter_ranges()

   \return current value of the generator's send speed
*/
int cw_get_send_speed(void)
{
	return cw_generator->send_speed;
}





/**
   \brief Get receiving speed from receiver

   testedin::test_parameter_ranges()

   \return current value of the receiver's receive speed
*/
int cw_get_receive_speed(void)
{
	return cw_receiver.speed;
}





/**
   \brief Get frequency from generator

   Function returns "frequency" parameter of generator,
   even if the generator is stopped, or volume of generated sound is zero.

   testedin::test_parameter_ranges()

   \return current value of generator's frequency
*/
int cw_get_frequency(void)
{
	return cw_generator->frequency;
}





/**
   \brief Get sound volume from generator

   Function returns "volume" parameter of generator,
   even if the generator is stopped.

   testedin::test_volume_functions()
   testedin::test_parameter_ranges()

   \return current value of generator's sound volume
*/
int cw_get_volume(void)
{
	return cw_generator->volume_percent;
}





/**
   \brief Get sending gap from generator

   testedin::test_parameter_ranges()

   \return current value of generator's sending gap
*/
int cw_get_gap(void)
{
	return cw_generator->gap;
}





/**
   \brief Get tolerance from receiver

   testedin::test_parameter_ranges()

   \return current value of receiver's tolerance
*/
int cw_get_tolerance(void)
{
	return cw_receiver.tolerance;
}





/**
   \brief Get sending weighting from generator

   testedin::test_parameter_ranges()

   \return current value of generator's sending weighting
*/
int cw_get_weighting(void)
{
	return cw_generator->weighting;
}





/**
   \brief Get timing parameters for sending

   Return the low-level timing parameters calculated from the speed, gap,
   tolerance, and weighting set.  Parameter values are returned in
   microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   \param dot_usecs
   \param dash_usecs
   \param end_of_element_usecs
   \param end_of_character_usecs
   \param end_of_word_usecs
   \param additional_usecs
   \param adjustment_usecs
*/
void cw_get_send_parameters(int *dot_usecs, int *dash_usecs,
			    int *end_of_element_usecs,
			    int *end_of_character_usecs, int *end_of_word_usecs,
			    int *additional_usecs, int *adjustment_usecs)
{
	cw_sync_parameters_internal(cw_generator, &cw_receiver);

	if (dot_usecs)   *dot_usecs = cw_generator->dot_length;
	if (dash_usecs)  *dash_usecs = cw_generator->dash_length;

	if (end_of_element_usecs)    *end_of_element_usecs = cw_generator->eoe_delay;
	if (end_of_character_usecs)  *end_of_character_usecs = cw_generator->eoc_delay;
	if (end_of_word_usecs)       *end_of_word_usecs = cw_generator->eow_delay;

	if (additional_usecs)    *additional_usecs = cw_generator->additional_delay;
	if (adjustment_usecs)    *adjustment_usecs = cw_generator->adjustment_delay;

	return;
}





/**
   \brief Get timing parameters for receiving, and adaptive threshold

   Return the low-level timing parameters calculated from the speed, gap,
   tolerance, and weighting set.  Parameter values are returned in
   microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   \param dot_usecs
   \param dash_usecs
   \param dot_min_usecs
   \param dot_max_usecs
   \param dash_min_usecs
   \param dash_max_usecs
   \param end_of_element_min_usecs
   \param end_of_element_max_usecs
   \param end_of_element_ideal_usecs
   \param end_of_character_min_usecs
   \param end_of_character_max_usecs
   \param end_of_character_ideal_usecs
   \param adaptive_threshold
*/
void cw_get_receive_parameters(int *dot_usecs, int *dash_usecs,
			       int *dot_min_usecs, int *dot_max_usecs,
			       int *dash_min_usecs, int *dash_max_usecs,
			       int *end_of_element_min_usecs,
			       int *end_of_element_max_usecs,
			       int *end_of_element_ideal_usecs,
			       int *end_of_character_min_usecs,
			       int *end_of_character_max_usecs,
			       int *end_of_character_ideal_usecs,
			       int *adaptive_threshold)
{
	cw_sync_parameters_internal(cw_generator, &cw_receiver);

	if (dot_usecs)      *dot_usecs = cw_receiver.dot_length;
	if (dash_usecs)     *dash_usecs = cw_receiver.dash_length;
	if (dot_min_usecs)  *dot_min_usecs = cw_receiver.dot_range_minimum;
	if (dot_max_usecs)  *dot_max_usecs = cw_receiver.dot_range_maximum;
	if (dash_min_usecs) *dash_min_usecs = cw_receiver.dash_range_minimum;
	if (dash_max_usecs) *dash_max_usecs = cw_receiver.dash_range_maximum;

	if (end_of_element_min_usecs)     *end_of_element_min_usecs = cw_receiver.eoe_range_minimum;
	if (end_of_element_max_usecs)     *end_of_element_max_usecs = cw_receiver.eoe_range_maximum;
	if (end_of_element_ideal_usecs)   *end_of_element_ideal_usecs = cw_receiver.eoe_range_ideal;
	if (end_of_character_min_usecs)   *end_of_character_min_usecs = cw_receiver.eoc_range_minimum;
	if (end_of_character_max_usecs)   *end_of_character_max_usecs = cw_receiver.eoc_range_maximum;
	if (end_of_character_ideal_usecs) *end_of_character_ideal_usecs = cw_receiver.eoc_range_ideal;

	if (adaptive_threshold) *adaptive_threshold = cw_receiver.adaptive_receive_threshold;

	return;
}





/**
   \brief Set noise spike threshold for receiver

   Set the period shorter than which, on receive, received tones are ignored.
   This allows the receive tone functions to apply noise canceling for very
   short apparent tones.
   For useful results the value should never exceed the dot length of a dot at
   maximum speed: 20000 microseconds (the dot length at 60WPM).
   Setting a noise threshold of zero turns off receive tone noise canceling.

   The default noise spike threshold is 10000 microseconds.

   errno is set to EINVAL if \p new_value is out of range.

   \param new_value - new value of noise spike threshold to be assigned to receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_noise_spike_threshold(int new_value)
{
	if (new_value < 0) {
		errno = EINVAL;
		return CW_FAILURE;
	}
	cw_receiver.noise_spike_threshold = new_value;

	return CW_SUCCESS;
}





/**
   \brief Get noise spike threshold from receiver

   See documentation of cw_set_noise_spike_threshold() for more information

   \return current value of receiver's threshold
*/
int cw_get_noise_spike_threshold(void)
{
	return cw_receiver.noise_spike_threshold;
}





/* ******************************************************************** */
/*                 Section:Finalization and cleanup                     */
/* ******************************************************************** */





/* We prefer to close the soundcard after a period of library inactivity,
   so that other applications can use it.  Ten seconds seems about right.
   We do it in one-second timeouts so that any leaked pending timeouts from
   other facilities don't cause premature finalization. */
static const int CW_AUDIO_FINALIZATION_DELAY = 10000000;

 /* Counter counting down the number of clock calls before we finalize. */
static volatile bool cw_is_finalization_pending = false;
static volatile int cw_finalization_countdown = 0;

/* Use a mutex to suppress delayed finalizations on complete resets. */
static volatile bool cw_is_finalization_locked_out = false;





/**
   \brief Tick a finalization clock

   If finalization is pending, decrement the countdown, and if this reaches
   zero, we've waited long enough to release sound and timeouts.
*/
void cw_finalization_clock_internal(void)
{
	if (cw_is_finalization_pending) {
		/* Decrement the timeout countdown, and finalize if we reach zero. */
		cw_finalization_countdown--;
		if (cw_finalization_countdown <= 0) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
				      "libcw: finalization timeout, closing down");

			cw_sigalrm_restore_internal();
			// cw_gen_release_internal(&cw_generator);

			cw_is_finalization_pending = false;
			cw_finalization_countdown = 0;
		} else {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
				      "libcw: finalization countdown %d", cw_finalization_countdown);

			/* Request another timeout.  This results in a call to our
			   cw_finalization_cancel_internal below; to ensure that it doesn't
			   really cancel finalization, unset the pending flag, then set it
			   back again after reqesting the timeout. */
			cw_is_finalization_pending = false;
			cw_timer_run_with_handler_internal(CW_USECS_PER_SEC, NULL);
			cw_is_finalization_pending = true;
		}
	}

	return;
}





/**
  Set the finalization pending flag, and request a timeout to call the
  finalization function after a delay of a few seconds.
*/
void cw_finalization_schedule_internal(void)
{
	if (!cw_is_finalization_locked_out && !cw_is_finalization_pending) {
		cw_timer_run_with_handler_internal(CW_USECS_PER_SEC,
						   cw_finalization_clock_internal);

		/* Set the flag and countdown last; calling cw_timer_run_with_handler()
		 * above results in a call to our cw_finalization_cancel_internal(),
		 which clears the flag and countdown if we set them early. */
		cw_is_finalization_pending = true;
		cw_finalization_countdown = CW_AUDIO_FINALIZATION_DELAY / CW_USECS_PER_SEC;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
			      "libcw: finalization scheduled");
	}

	return;
}





/**
   Cancel any pending finalization on noting other library activity,
   indicated by a call from the timeout request function telling us
   that it is setting a timeout.
*/
void cw_finalization_cancel_internal(void)
{
	if (cw_is_finalization_pending)  {
		/* Cancel pending finalization and return to doing nothing. */
		cw_is_finalization_pending = false;
		cw_finalization_countdown = 0;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
			      "libcw: finalization canceled");
	}

	return;
}





/**
   \brief Reset all library features to their default states

   Clears the tone queue, receive buffers and retained state information,
   any current keyer activity, and any straight key activity, returns to
   silence, and closes soundcard and console devices.  This function is
   suitable for calling from an application exit handler.
*/
void cw_complete_reset(void)
{
	/* If the finalizer thinks it's pending, stop it, then temporarily
	   lock out finalizations. */
	cw_finalization_cancel_internal();
	cw_is_finalization_locked_out = true;

	cw_gen_stop_internal(cw_generator);

	/* Call the reset functions for each subsystem. */
	cw_reset_tone_queue();
	cw_reset_receive();
	cw_reset_keyer();
	cw_reset_straight_key();

	cw_gen_delete_internal(&cw_generator);
	cw_sigalrm_restore_internal();

	/* Now we can re-enable delayed finalizations. */
	cw_is_finalization_locked_out = false;

	return;
}





/* ******************************************************************** */
/*                            Section:Sending                           */
/* ******************************************************************** */





/**
   \brief Send an element

   Low level primitive to send a tone element of the given type, followed
   by the standard inter-element silence.

   Function sets errno to EINVAL if an argument is invalid, and returns
   CW_FAILURE.
   Function also returns failure if adding the element to queue of elements
   failed.

   \param gen - generator to be used to send an element
   \param element - element to send - dot (CW_DOT_REPRESENTATION) or dash (CW_DASH_REPRESENTATION)

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_send_element_internal(cw_gen_t *gen, char element)
{
	int status;

	/* Synchronize low-level timings if required. */
	cw_sync_parameters_internal(gen, &cw_receiver);

	/* Send either a dot or a dash element, depending on representation. */
	if (element == CW_DOT_REPRESENTATION) {
		cw_tone_t tone;
		tone.slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES;
		tone.usecs = gen->dot_length;
		tone.frequency = gen->frequency;
		status = cw_tone_queue_enqueue_internal(gen->tq, &tone);
	} else if (element == CW_DASH_REPRESENTATION) {
		cw_tone_t tone;
		tone.slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES;
		tone.usecs = gen->dash_length;
		tone.frequency = gen->frequency;
		status = cw_tone_queue_enqueue_internal(gen->tq, &tone);
	} else {
		errno = EINVAL;
		status = CW_FAILURE;
	}

	if (!status) {
		return CW_FAILURE;
	}

	/* Send the inter-element gap. */
	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
	tone.usecs = gen->eoe_delay;
	tone.frequency = 0;
	if (!cw_tone_queue_enqueue_internal(gen->tq, &tone)) {
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





/**
   cw_send_[dot|dash|character_space|word_space]()

   Low level primitives, available to send single dots, dashes, character
   spaces, and word spaces.  The dot and dash routines always append the
   normal inter-element gap after the tone sent.  The cw_send_character_space
   routine sends space timed to exclude the expected prior dot/dash
   inter-element gap.  The cw_send_word_space routine sends space timed to
   exclude both the expected prior dot/dash inter-element gap and the prior
   end of character space.  These functions return true on success, or false
   with errno set to EBUSY or EAGAIN on error.

   testedin::test_send_primitives()
*/
int cw_send_dot(void)
{
	return cw_send_element_internal(cw_generator, CW_DOT_REPRESENTATION);
}





/**
   See documentation of cw_send_dot() for more information

   testedin::test_send_primitives()
*/
int cw_send_dash(void)
{
	return cw_send_element_internal(cw_generator, CW_DASH_REPRESENTATION);
}





/**
   See documentation of cw_send_dot() for more information

   testedin::test_send_primitives()
*/
int cw_send_character_space(void)
{
	/* Synchronize low-level timing parameters. */
	cw_sync_parameters_internal(cw_generator, &cw_receiver);

	/* Delay for the standard end of character period, plus any
	   additional inter-character gap */
	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
	tone.usecs = cw_generator->eoc_delay + cw_generator->additional_delay;
	tone.frequency = 0;
	return cw_tone_queue_enqueue_internal(cw_generator->tq, &tone);
}





/**
   See documentation of cw_send_dot() for more information

   testedin::test_send_primitives()
*/
int cw_send_word_space(void)
{
	/* Synchronize low-level timing parameters. */
	cw_sync_parameters_internal(cw_generator, &cw_receiver);

	/* Send silence for the word delay period, plus any adjustment
	   that may be needed at end of word. */
#if 1

	/* Let's say that 'tone queue low watermark' is one element
	  (i.e. one tone).

	  In order for tone queue to recognize that a 'low tone queue'
	  callback needs to be called, the level in tq needs to drop
	  from 2 to 1.

	  Almost every queued character guarantees that there will be
	  at least two tones, e.g for 'E' it is dash + following
	  space. But what about a ' ' character?

	  With the code in second branch of '#if', there is only one
	  tone, and the tone queue manager can't recognize when the
	  level drops from 2 to 1 (and thus the 'low tone queue'
	  callback won't be called).

	  The code in first branch of '#if' enqueues ' ' as two tones
	  (both of them silent). With code in the first branch
	  active, the tone queue works correctly with 'low tq
	  watermark' = 1.

	  If tone queue manager could recognize that the only tone
	  that has been enqueued is a single-tone space, then the code
	  in first branch would not be necessary. However, this
	  'recognize a single space character in tq' is a very special
	  case, and it's hard to justify its implementation.

	  WARNING: queueing two tones instead of one may lead to
	  additional, unexpected and unwanted delay. This may
	  negatively influence correctness of timing. */

	/* Queue space character as two separate tones. */

	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
	tone.usecs = cw_generator->eow_delay;
	tone.frequency = 0;
	int a = cw_tone_queue_enqueue_internal(cw_generator->tq, &tone);

	int b = CW_FAILURE;

	if (a == CW_SUCCESS) {
		tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
		tone.usecs = cw_generator->adjustment_delay;
		tone.frequency = 0;
		b = cw_tone_queue_enqueue_internal(cw_generator->tq, &tone);
	}

	return a && b;
#else
	/* Queue space character as a single tone. */

	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
	tone.usecs = cw_generator->eow_delay + cw_generator->adjustment_delay;
	tone.frequency = 0;

	return cw_tone_queue_enqueue_internal(cw_generator->tq, &tone);
#endif
}





/**
   Send the given string as dots and dashes, adding the post-character gap.

   Function sets EAGAIN if there is not enough space in tone queue to
   enqueue \p representation.

   \param gen
   \param representation
   \param partial

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_send_representation_internal(cw_gen_t *gen, const char *representation, bool partial)
{
	/* Before we let this representation loose on tone generation,
	   we'd really like to know that all of its tones will get queued
	   up successfully.  The right way to do this is to calculate the
	   number of tones in our representation, then check that the space
	   exists in the tone queue. However, since the queue is comfortably
	   long, we can get away with just looking for a high water mark.  */
	if ((uint32_t) cw_get_tone_queue_length() >= gen->tq->high_water_mark) {
		errno = EAGAIN;
		return CW_FAILURE;
	}

	/* Sound the elements of the CW equivalent. */
	for (int i = 0; representation[i] != '\0'; i++) {
		/* Send a tone of dot or dash length, followed by the
		   normal, standard, inter-element gap. */
		if (!cw_send_element_internal(gen, representation[i])) {
			return CW_FAILURE;
		}
	}

	/* If this representation is stated as being "partial", then
	   suppress any and all end of character delays.*/
	if (!partial) {
		if (!cw_send_character_space()) {
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}





/**
   \brief Check, then send the given string as dots and dashes.

   The representation passed in is assumed to be a complete Morse
   character; that is, all post-character delays will be added when
   the character is sent.

   On success, the routine returns CW_SUCCESS.
   On failure, it returns CW_FAILURE, with errno set to EINVAL if any
   character of the representation is invalid, EBUSY if the sound card,
   console speaker, or keying system is busy, or EAGAIN if the tone
   queue is full, or if there is insufficient space to queue the tones
   or the representation.

   testedin::test_representations()

   \param representation - representation to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_representation(const char *representation)
{
	if (!cw_representation_is_valid(representation)) {
		errno = EINVAL;
		return CW_FAILURE;
	} else {
		return cw_send_representation_internal(cw_generator, representation, false);
	}
}





/**
   \brief Check, then send the given string as dots and dashes

   The \p representation passed in is assumed to be only part of a larger
   Morse representation; that is, no post-character delays will be added
   when the character is sent.

   On success, the routine returns CW_SUCCESS.
   On failure, it returns CW_FAILURE, with errno set to EINVAL if any
   character of the representation is invalid, EBUSY if the sound card,
   console speaker, or keying system is busy, or EAGAIN if the tone queue
   is full, or if there is insufficient space to queue the tones for
   the representation.

   testedin::test_representations()
*/
int cw_send_representation_partial(const char *representation)
{
	if (!cw_representation_is_valid(representation)) {
		errno = ENOENT;
		return CW_FAILURE;
	} else {
		return cw_send_representation_internal(cw_generator, representation, true);
	}
}





/**
   \brief Lookup, and send a given ASCII character as Morse code

   If "partial" is set, the end of character delay is not appended to the
   Morse code sent.

   Function sets errno to ENOENT if \p character is not a recognized character.

   \param gen - generator to be used to send character
   \param character - character to send
   \param partial

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_character_internal(cw_gen_t *gen, char character, int partial)
{
	if (!gen) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: no generator available");
		return CW_FAILURE;
	}

	/* Handle space special case; delay end-of-word and return. */
	if (character == ' ') {
		return cw_send_word_space();
	}

	/* Lookup the character, and sound it. */
	const char *representation = cw_character_to_representation_internal(character);
	if (!representation) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	if (!cw_send_representation_internal(gen, representation, partial)) {
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





/**
   \brief Checks that the given character is validly sendable in Morse

   Function sets errno to ENOENT on failure.

   testedin::test_validate_character_and_string()

   \param c - character to check

   \return CW_SUCCESS if character is valid
   \return CW_FAILURE if character is invalid
*/
bool cw_character_is_valid(char c)
{
	/* If the character is the space special-case, or it is in the
	   lookup table, return success. */
	if (c == ' ' || cw_character_to_representation_internal(c)) {
		return CW_SUCCESS;
	} else {
		errno = ENOENT;
		return CW_FAILURE;
	}
}



int cw_check_character(char c)
{
	return (int) cw_character_is_valid(c);
}




/**
   \brief Lookup, and send a given ASCII character as Morse

   The end of character delay is appended to the Morse sent.

   On success, the routine returns CW_SUCCESS.
   On failure, it returns CW_FAILURE, with errno set to ENOENT if the given
   character \p c is not a valid Morse character, EBUSY if the sound card,
   console speaker, or keying system is busy, or EAGAIN if the tone queue
   is full, or if there is insufficient space to queue the tones for the
   character.

   This routine returns as soon as the character has been successfully
   queued for sending; that is, almost immediately.  The actual sending
   happens in background processing.  See cw_wait_for_tone() and
   cw_wait_for_tone_queue() for ways to check the progress of sending.

   testedin::test_send_character_and_string()

   \param c - character to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_character(char c)
{
	if (!cw_character_is_valid(c)) {
		errno = ENOENT;
		return CW_FAILURE;
	} else {
		return cw_send_character_internal(cw_generator, c, false);
	}
}





/**
   \brief Lookup, and send a given ASCII character as Morse code

   "partial" means that the "end of character" delay is not appended
   to the Morse code sent by the function, to support the formation of
   combination characters.

   On success, the routine returns CW_SUCCESS.
   On error, it returns CW_FAILURE, with errno set to ENOENT if the
   given character \p is not a valid Morse character, EBUSY if the sound
   card, console speaker, or keying system is busy, or EAGAIN if the
   tone queue is full, or if there is insufficient space to queue the
   tones for the character.

   This routine queues its arguments for background processing.  See
   cw_send_character() for details of how to check the queue status.

   \param c - character to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_character_partial(char c)
{
	if (!cw_character_is_valid(c)) {
		errno = ENOENT;
		return CW_FAILURE;
	} else {
		return cw_send_character_internal(cw_generator, c, true);
	}
}





/**
   \brief Validate a string

   Check that each character in the given string is valid and can be
   sent by libcw as a Morse character.

   Function sets errno to EINVAL on failure

   testedin::test_validate_character_and_string()

   \param string - string to check

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
bool cw_string_is_valid(const char *string)
{
	/* Check that each character in the string has a Morse
	   representation, or - as a special case - is a space character. */
	for (int i = 0; string[i] != '\0'; i++) {
		if (!(string[i] == ' '
		      || cw_character_to_representation_internal(string[i]))) {

			errno = EINVAL;
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}





int cw_check_string(const char *string)
{
	return cw_string_is_valid(string);
}





/**
   \brief Send a given ASCII string in Morse code

   errno is set to ENOENT if any character in the string is not a valid
   Morse character, EBUSY if the sound card, console speaker, or keying
   system is in use by the iambic keyer or the straight key, or EAGAIN
   if the tone queue is full. If the tone queue runs out of space part
   way through queueing the string, the function returns EAGAIN.
   However, an indeterminate number of the characters from the string will
   have already been queued.  For safety, clients can ensure the tone queue
   is empty before queueing a string, or use cw_send_character() if they
   need finer control.

   This routine queues its arguments for background processing, the
   actual sending happens in background processing. See
   cw_wait_for_tone() and cw_wait_for_tone_queue() for ways to check
   the progress of sending.

   testedin::test_send_character_and_string()

   \param string - string to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_string(const char *string)
{
	/* Check the string is composed of sendable characters. */
	if (!cw_string_is_valid(string)) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	/* Send every character in the string. */
	for (int i = 0; string[i] != '\0'; i++) {
		if (!cw_send_character_internal(cw_generator, string[i], false))
			return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/* ******************************************************************** */
/*            Section:Receive tracking and statistics helpers           */
/* ******************************************************************** */





/**
   \brief Reset tracking data structure

   Moving average function for smoothed tracking of dot and dash lengths.

   \param tracking - tracking data structure
   \param initial - initial value to be put in table of tracking data structure
*/
void cw_reset_adaptive_average_internal(cw_tracking_t *tracking, int initial)
{
	for (int i = 0; i < CW_REC_AVERAGE_ARRAY_LENGTH; i++) {
		tracking->buffer[i] = initial;
	}

	tracking->sum = initial * CW_REC_AVERAGE_ARRAY_LENGTH;
	tracking->cursor = 0;

	return;
}





/**
   \brief Add new "length of element" value to tracking data structure

   Moving average function for smoothed tracking of dot and dash lengths.

   \param tracking - tracking data structure
   \param element_len_usec - new "length of element" value to add to tracking data
*/
void cw_update_adaptive_average_internal(cw_tracking_t *tracking, int element_len_usecs)
{
	tracking->sum += element_len_usecs - tracking->buffer[tracking->cursor];
	tracking->buffer[tracking->cursor++] = element_len_usecs;
	tracking->cursor %= CW_REC_AVERAGE_ARRAY_LENGTH;

	return;
}





/**
   \brief Get average sum from tracking data structure

   \param tracking - tracking data structure

   \return average sum
*/
int cw_get_adaptive_average_internal(cw_tracking_t *tracking)
{
	return tracking->sum / CW_REC_AVERAGE_ARRAY_LENGTH;
}





/**
   \brief Add an element timing to statistics

   Add an element timing with a given statistic type to the circular
   statistics buffer.  The buffer stores only the delta from the ideal
   value; the ideal is inferred from the type passed in.

   \p type may be: STAT_DOT or STAT_DASH or STAT_END_ELEMENT or STAT_END_CHARACTER

   \param rec - receiver
   \param type - element type
   \param usecs - timing of an element
*/
void cw_receiver_add_statistic_internal(cw_rec_t *rec, stat_type_t type, int usecs)
{
	/* Synchronize low-level timings if required. */
	cw_sync_parameters_internal(cw_generator, rec);

	/* Calculate delta as difference between usec and the ideal value. */
	int delta = usecs - ((type == STAT_DOT) ? rec->dot_length
			     : (type == STAT_DASH) ? rec->dash_length
			     : (type == STAT_END_ELEMENT) ? rec->eoe_range_ideal
			     : (type == STAT_END_CHARACTER) ? rec->eoc_range_ideal : usecs);

	/* Add this statistic to the buffer. */
	rec->statistics[rec->statistics_ind].type = type;
	rec->statistics[rec->statistics_ind++].delta = delta;
	rec->statistics_ind %= CW_REC_STATISTICS_CAPACITY;

	return;
}





/**
   \brief Calculate and return one given timing statistic type

   \p type may be: STAT_DOT or STAT_DASH or STAT_END_ELEMENT or STAT_END_CHARACTER

   \param rec - receiver
   \param type - type of statistics

   \return 0.0 if no record of given type were found
   \return timing statistics otherwise
*/
double cw_receiver_get_statistic_internal(cw_rec_t *rec, stat_type_t type)
{
	/* Sum and count elements matching the given type.  A cleared
	   buffer always begins refilling at element zero, so to optimize
	   we can stop on the first unoccupied slot in the circular buffer. */
	double sum_of_squares = 0.0;
	int count = 0;
	for (int i = 0; i < CW_REC_STATISTICS_CAPACITY; i++) {
		if (rec->statistics[i].type == type) {
			int delta = rec->statistics[i].delta;
			sum_of_squares += (double) delta * (double) delta;
			count++;
		} else if (rec->statistics[i].type == STAT_NONE) {
			break;
		}
	}

	/* Return the standard deviation, or zero if no matching elements. */
	return count > 0 ? sqrt (sum_of_squares / (double) count) : 0.0;
}





/**
   \brief Calculate and return receive timing statistics

   These statistics may be used to obtain a measure of the accuracy of
   received CW.  The values \p dot_sd and \p dash_sd contain the standard
   deviation of dot and dash lengths from the ideal values, and
   \p element_end_sd and \p character_end_sd the deviations for inter
   element and inter character spacing.  Statistics are held for all
   timings in a 256 element circular buffer.  If any statistic cannot
   be calculated, because no records for it exist, the returned value
   is 0.0.  Use NULL for the pointer argument to any statistic not required.

   \param dot_sd
   \param dash_sd
   \param element_end_sd
   \param character_end_sd
*/
void cw_get_receive_statistics(double *dot_sd, double *dash_sd,
			       double *element_end_sd, double *character_end_sd)
{
	if (dot_sd) {
		*dot_sd = cw_receiver_get_statistic_internal(&cw_receiver, STAT_DOT);
	}
	if (dash_sd) {
		*dash_sd = cw_receiver_get_statistic_internal(&cw_receiver, STAT_DASH);
	}
	if (element_end_sd) {
		*element_end_sd = cw_receiver_get_statistic_internal(&cw_receiver, STAT_END_ELEMENT);
	}
	if (character_end_sd) {
		*character_end_sd = cw_receiver_get_statistic_internal(&cw_receiver, STAT_END_CHARACTER);
	}
	return;
}





/**
   \brief Clear the receive statistics buffer

   Clear the receive statistics buffer by removing all records from it and
   returning it to its initial default state.
*/
void cw_reset_receive_statistics(void)
{
	for (int i = 0; i < CW_REC_STATISTICS_CAPACITY; i++) {
		cw_receiver.statistics[i].type = STAT_NONE;
		cw_receiver.statistics[i].delta = 0;
	}
	cw_receiver.statistics_ind = 0;

	return;
}





/* ******************************************************************** */
/*                           Section:Receiving                          */
/* ******************************************************************** */





/*
 * The CW receive functions implement the following state graph:
 *
 *        +----------------- RS_ERR_WORD <-----------------------+
 *        |(clear)                ^                              |
 *        |           (delay=long)|                              |
 *        |                       |                              |
 *        +----------------- RS_ERR_CHAR <-------------+         |
 *        |(clear)                ^  |                 |         |
 *        |                       |  +-----------------+         |(error,
 *        |                       |   (delay=short)              | delay=long)
 *        |    (error,delay=short)|                              |
 *        |                       |  +---------------------------+
 *        |                       |  |
 *        +--------------------+  |  |
 *        |             (noise)|  |  |
 *        |                    |  |  |
 *        v    (start tone)    |  |  |  (end tone,noise)
 * --> RS_IDLE ------------> RS_IN_TONE ----------------> RS_AFTER_TONE <------- +
 *     |  ^                           ^                   | |    | ^ |           |
 *     |  |                           |                   | |    | | |           |
 *     |  |          (delay=short)    +-------------------+ |    | | +-----------+
 *     |  |        +--------------+     (start tone)        |    | |   (not ready,
 *     |  |        |              |                         |    | |    buffer dot,
 *     |  |        +-------> RS_END_CHAR <------------------+    | |    buffer dash)
 *     |  |                   |   |       (delay=short)          | |
 *     |  +-------------------+   |                              | |
 *     |  |(clear)                |                              | |
 *     |  |           (delay=long)|                              | |
 *     |  |                       v                              | |
 *     |  +----------------- RS_END_WORD <-----------------------+ |
 *     |   (clear)                        (delay=long)             |(buffer dot,
 *     |                                                           | buffer dash)
 *     +-----------------------------------------------------------+
 */





/**
   \brief Set value of "adaptive receive enabled" flag for a receiver

   Set the value of the flag of \p receiver that controls whether, on
   receive, the receive functions do fixed speed receive, or track the
   speed of the received Morse code by adapting to the input stream.

   \param rec - receiver for which to set the flag
   \param flag - flag value to set
*/
void cw_receiver_set_adaptive_internal(cw_rec_t *rec, bool flag)
{
	/* Look for change of adaptive receive state. */
	if (rec->is_adaptive_receive_enabled != flag) {

		rec->is_adaptive_receive_enabled = flag;

		/* Changing the flag forces a change in low-level parameters. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, rec);

		/* If we have just switched to adaptive mode, (re-)initialize
		   the averages array to the current dot/dash lengths, so
		   that initial averages match the current speed. */
		if (rec->is_adaptive_receive_enabled) {
			cw_reset_adaptive_average_internal(&rec->dot_tracking, rec->dot_length);
			cw_reset_adaptive_average_internal(&rec->dash_tracking, rec->dash_length);
		}
	}

	return;
}





/**
   \brief Enable adaptive receive speed tracking

   If adaptive speed tracking is enabled, the receive functions will
   attempt to automatically adjust the receive speed setting to match
   the speed of the incoming Morse code. If it is disabled, the receive
   functions will use fixed speed settings, and reject incoming Morse
   which is not at the expected speed.

   Adaptive speed tracking uses a moving average of the past four elements
   as its baseline for tracking speeds.  The default state is adaptive speed
   tracking disabled.
*/
void cw_enable_adaptive_receive(void)
{
	cw_receiver_set_adaptive_internal(&cw_receiver, true);
	return;
}





/**
   \brief Disable adaptive receive speed tracking

   See documentation of cw_enable_adaptive_receive() for more information
*/
void cw_disable_adaptive_receive(void)
{
	cw_receiver_set_adaptive_internal(&cw_receiver, false);
	return;
}





/**
   \brief Get adaptive receive speed tracking flag

   The function returns state of "adaptive receive enabled" flag.
   See documentation of cw_enable_adaptive_receive() for more information

   \return true if adaptive speed tracking is enabled
   \return false otherwise
*/
bool cw_get_adaptive_receive_state(void)
{
	return cw_receiver.is_adaptive_receive_enabled;
}





/**
   \brief Mark beginning of receive tone

   Called on the start of a receive tone.  If the \p timestamp is NULL, the
   current timestamp is used as beginning of tone.

   The function should be called by client application when pressing a
   key down (closing a circuit) has been detected by client
   application.

   On error the function returns CW_FAILURE, with errno set to ERANGE if
   the call is directly after another cw_start_receive_tone() call or if
   an existing received character has not been cleared from the buffer,
   or EINVAL if the timestamp passed in is invalid.

   \param timestamp - time stamp of "key down" event

   \return CW_SUCCESS on success
   \return CW_FAILURE otherwise (with errno set)
*/
int cw_start_receive_tone(const struct timeval *timestamp)
{
	/* If the receive state is not idle or after a tone, this is
	   a state error.  A receive tone start can only happen while
	   we are idle, or in the middle of a character. */
	if (cw_receiver.state != RS_IDLE && cw_receiver.state != RS_AFTER_TONE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Validate and save the timestamp, or get one and then save it. */
	if (!cw_timestamp_validate_internal(&cw_receiver.tone_start, timestamp)) {
		return CW_FAILURE;
	}

	/* If this function has been called while received is in "after
	   tone" state, we can measure the inter-element gap (between
	   previous tone and this tone) by comparing the start
	   timestamp with the last end one, guaranteed set by getting
	   to the after tone state via cw_end_receive tone(), or in
	   extreme cases, by cw_receiver_add_element_internal().

	   Do that, then, and update the relevant statistics. */
	if (cw_receiver.state == RS_AFTER_TONE) {
		int space_len_usec = cw_timestamp_compare_internal(&cw_receiver.tone_end,
								   &cw_receiver.tone_start);
		cw_receiver_add_statistic_internal(&cw_receiver, STAT_END_ELEMENT, space_len_usec);
	}

	/* Set state to indicate we are inside a tone. We don't know
	   yet if it will be recognized as valid tone. */
	cw_receiver.state = RS_IN_TONE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

	return CW_SUCCESS;
}





/**
   \brief Analyze a tone and identify it as an element

   Identify an element (dot/dash) represented by a duration of mark.
   The duration is provided in \p element_len_usecs.

   Identification is done using the ranges provided by the low level
   timing parameters.

   On success function returns CW_SUCCESS and sends back either a dot
   or a dash through \p representation.

   On failure it returns CW_FAILURE with errno set to ENOENT if the
   tone is not recognizable as either a dot or a dash, and sets the
   receiver state to one of the error states, depending on the element
   length passed in.

   Note: for adaptive timing, the element should _always_ be
   recognized as a dot or a dash, because the ranges will have been
   set to cover 0 to INT_MAX.

   \param rec - receiver
   \param element_len_usecs - length of element to analyze
   \param representation - variable to store identified element (output variable)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receiver_identify_tone_internal(cw_rec_t *rec, int element_len_usecs, /* out */ char *representation)
{
	cw_assert (representation, "Output parameter is NULL");

	/* Synchronize low level timings if required */
	cw_sync_parameters_internal(cw_generator, rec);

	/* If the timing was, within tolerance, a dot, return dot to
	   the caller.  */
	if (element_len_usecs >= rec->dot_range_minimum
	    && element_len_usecs <= rec->dot_range_maximum) {

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: mark '%d [us]' recognized as DOT (limits: %d - %d [us])",
			      element_len_usecs, rec->dot_range_minimum, rec->dot_range_maximum);

		*representation = CW_DOT_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* Do the same for a dash. */
	if (element_len_usecs >= rec->dash_range_minimum
	    && element_len_usecs <= rec->dash_range_maximum) {

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: mark '%d [us]' recognized as DASH (limits: %d - %d [us])",
			      element_len_usecs, rec->dash_range_minimum, rec->dash_range_maximum);

		*representation = CW_DASH_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* This element is not a dot or a dash, so we have an error
	   case. */
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw: unrecognized element, mark len = %d [us]", element_len_usecs);
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw: dot limits: %d - %d [us]", rec->dot_range_minimum, rec->dot_range_maximum);
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
		      "libcw: dash limits: %d - %d [us]", rec->dash_range_minimum, rec->dash_range_maximum);

	/* We should never reach here when in adaptive timing receive
	   mode. */
	if (rec->is_adaptive_receive_enabled) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: unrecognized element in adaptive receive");
	}



	/* TODO: making decision about current state of receiver is
	   out of scope of this function. Move the part below to
	   separate function. */

	/* If we cannot send back through \p representation any result,
	   let's move to either "in error after character" or "in
	   error after word" state, which is an "in space" state.

	   We will treat \p element_len_usecs as length of space.

	   Depending on the length of space, we pick which of the
	   error states to move to, and move to it.  The comparison is
	   against the expected end-of-char delay.  If it's larger,
	   then fix at word error, otherwise settle on char error.

	   TODO: reconsider this for a moment: the function has been
	   called because client code has received a *mark*, not a
	   space. Are we sure that we now want to treat the
	   element_len_usecs as length of *space*? And do we want to
	   move to either RS_ERR_WORD or RS_ERR_CHAR pretending that
	   this is a length of *space*? */
	rec->state = element_len_usecs > rec->eoc_range_maximum
		? RS_ERR_WORD : RS_ERR_CHAR;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[rec->state]);



	/* Return ENOENT to the caller. */
	errno = ENOENT;
	return CW_FAILURE;
}





/**
   \brief Update adaptive tracking data

   Function updates the averages of dot and dash lengths, and recalculates
   the adaptive threshold for the next receive tone.

   \param rec - receiver
   \param element_len_usecs
   \param element
*/
void cw_receiver_update_adaptive_tracking_internal(cw_rec_t *rec, int element_len_usecs, char element)
{
	/* We are not going to tolerate being called in fixed speed mode. */
	if (!rec->is_adaptive_receive_enabled) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_WARNING,
			      "Called \"adaptive\" function when receiver is not in adaptive mode\n");
		return;
	}

	/* We will update the information held for either dots or dashes.
	   Which we pick depends only on what the representation of the
	   character was identified as earlier. */
	if (element == CW_DOT_REPRESENTATION) {
		cw_update_adaptive_average_internal(&rec->dot_tracking, element_len_usecs);
	} else if (element == CW_DASH_REPRESENTATION) {
		cw_update_adaptive_average_internal(&rec->dash_tracking, element_len_usecs);
	} else {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "Unknown element %d\n", element);
		return;
	}

	/* Recalculate the adaptive threshold from the values currently
	   held in the moving averages.  The threshold is calculated as
	   (avg dash length - avg dot length) / 2 + avg dot_length. */
	int average_dot = cw_get_adaptive_average_internal(&rec->dot_tracking);
	int average_dash = cw_get_adaptive_average_internal(&rec->dash_tracking);
	rec->adaptive_receive_threshold = (average_dash - average_dot) / 2
		+ average_dot;

	/* Resynchronize the low level timing data following recalculation.
	   If the resultant recalculated speed is outside the limits,
	   clamp the speed to the limit value and recalculate again.

	   Resetting the speed directly really means unsetting adaptive mode,
	   resyncing to calculate the new threshold, which unfortunately
	   recalculates everything else according to fixed speed; so, we
	   then have to reset adaptive and resyncing one more time, to get
	   all other timing parameters back to where they should be. */
	cw_is_in_sync = false;
	cw_sync_parameters_internal(cw_generator, rec);
	if (rec->speed < CW_SPEED_MIN || rec->speed > CW_SPEED_MAX) {
		rec->speed = rec->speed < CW_SPEED_MIN
			? CW_SPEED_MIN : CW_SPEED_MAX;
		rec->is_adaptive_receive_enabled = false;
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, rec);

		rec->is_adaptive_receive_enabled = true;
		cw_is_in_sync = false;
		cw_sync_parameters_internal(cw_generator, rec);
	}

	return;
}





/**
   \brief Mark end of tone

   The function should be called by client application when releasing
   a key (opening a circuit) has been detected by client application.

   If the \p timestamp is NULL, the current time is used as timestamp
   of end of tone.

   On success, the routine adds a dot or dash to the receiver's
   representation buffer, and returns CW_SUCCESS.

   On failure, it returns CW_FAIURE, with errno set to:
   ERANGE if the call was not preceded by a cw_start_receive_tone() call,
   EINVAL if the timestamp passed in is not valid,
   ENOENT if the tone length was out of bounds for the permissible
   dot and dash lengths and fixed speed receiving is selected,
   ENOMEM if the receiver's representation buffer is full,
   EAGAIN if the tone was shorter than the threshold for noise and was
   therefore ignored.

   \param timestamp - time stamp of "key up" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_end_receive_tone(const struct timeval *timestamp)
{
	/* The receive state is expected to be inside a tone. */
	if (cw_receiver.state != RS_IN_TONE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Take a safe copy of the current end timestamp, in case we need
	   to put it back if we decide this tone is really just noise. */
	struct timeval saved_end_timestamp = cw_receiver.tone_end;

	/* Save the timestamp passed in, or get one. */
	if (!cw_timestamp_validate_internal(&cw_receiver.tone_end, timestamp)) {
		return CW_FAILURE;
	}

	/* Compare the timestamps to determine the length of the tone. */
	int element_len_usecs = cw_timestamp_compare_internal(&cw_receiver.tone_start,
							      &cw_receiver.tone_end);


	if (cw_receiver.noise_spike_threshold > 0
	    && element_len_usecs <= cw_receiver.noise_spike_threshold) {

		/* This pair of start()/stop() calls is just a noise,
		   ignore it.

		   Revert to state of receiver as it was before
		   complementary cw_start_receive_tone(). After call
		   to start() the state was changed to RS_IN_TONE, but
		   what state it was before call to start()?

		   Check position in representation buffer to see in
		   which state the receiver was *before* start()
		   function call, and restore this state. */
		cw_receiver.state = cw_receiver.representation_ind == 0 ? RS_IDLE : RS_AFTER_TONE;

		/* Put the end tone timestamp back to how it was when we
		   came in to the routine. */
		cw_receiver.tone_end = saved_end_timestamp;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw: '%d [us]' tone identified as spike noise (threshold = '%d [us]')",
			      element_len_usecs, cw_receiver.noise_spike_threshold);
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

		errno = EAGAIN;
		return CW_FAILURE;
	}


	/* This was not a noise. At this point, we have to make a
	   decision about the element just received.  We'll use a
	   routine that compares ranges to tell us what it thinks this
	   element is.  If it can't decide, it will hand us back an
	   error which we return to the caller.  Otherwise, it returns
	   a mark (dot or dash), for us to buffer. */
	char representation;
	int status = cw_receiver_identify_tone_internal(&cw_receiver, element_len_usecs, &representation);
	if (!status) {
		return CW_FAILURE;
	}

	/* Update the averaging buffers so that the adaptive tracking of
	   received Morse speed stays up to date.  But only do this if we
	   have set adaptive receiving; don't fiddle about trying to track
	   for fixed speed receive. */
	if (cw_receiver.is_adaptive_receive_enabled) {
		cw_receiver_update_adaptive_tracking_internal(&cw_receiver, element_len_usecs, representation);
	}

	/* Update dot and dash timing statistics.  It may seem odd to do
	   this after calling cw_receiver_update_adaptive_tracking_internal(),
	   rather than before, as this function changes the ideal values we're
	   measuring against.  But if we're on a speed change slope, the
	   adaptive tracking smoothing will cause the ideals to lag the
	   observed speeds.  So by doing this here, we can at least
	   ameliorate this effect, if not eliminate it. */
	if (representation == CW_DOT_REPRESENTATION) {
		cw_receiver_add_statistic_internal(&cw_receiver, STAT_DOT, element_len_usecs);
	} else {
		cw_receiver_add_statistic_internal(&cw_receiver, STAT_DASH, element_len_usecs);
	}

	/* Add the representation character to the receiver's buffer. */
	cw_receiver.representation[cw_receiver.representation_ind++] = representation;

	/* We just added a representation to the receive buffer.  If it's
	   full, then we have to do something, even though it's unlikely.
	   What we'll do is make a unilateral declaration that if we get
	   this far, we go to end-of-char error state automatically. */
	if (cw_receiver.representation_ind == CW_REC_REPRESENTATION_CAPACITY - 1) {
		cw_receiver.state = RS_ERR_CHAR;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: receiver's representation buffer is full");

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* All is well.  Move to the more normal after-tone state. */
	cw_receiver.state = RS_AFTER_TONE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

	return CW_SUCCESS;
}





/**
   \brief Add dot or dash to receiver's representation buffer

   Function adds an \p element (either a dot or a dash) to the
   receiver's representation buffer.

   Since we can't add an element to the buffer without any
   accompanying timing information, the function also accepts
   \p timestamp of the "end of element" event.  If the \p timestamp
   is NULL, the timestamp for current time is used.

   The receiver's state is updated as if we had just received a call
   to cw_end_receive_tone().

   \param rec - receiver
   \param timestamp - timestamp of "end of element" event
   \param element - element to be inserted into receiver's representation buffer

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receiver_add_element_internal(cw_rec_t *rec, const struct timeval *timestamp, char element)
{
	/* The receiver's state is expected to be idle or after a tone in
	   order to use this routine. */
	if (rec->state != RS_IDLE && rec->state != RS_AFTER_TONE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* This routine functions as if we have just seen a tone end,
	   yet without really seeing a tone start.

	   It doesn't matter that we don't know timestamp of start of
	   this tone: start timestamp would be needed only to
	   determine tone length and element type (dot/dash). But
	   since the element type has been determined by \p element,
	   we don't need timestamp for start of element.

	   What does matter is timestamp of end of this tone. This is
	   because the receiver representation routines that may be
	   called later look at the time since the last end of tone
	   to determine whether we are at the end of a word, or just
	   at the end of a character. */
	if (!cw_timestamp_validate_internal(&rec->tone_end, timestamp)) {
		return CW_FAILURE;
	}

	/* Add the element to the receiver's representation buffer. */
	rec->representation[rec->representation_ind++] = element;

	/* We just added an element to the receiver's buffer.  As
	   above, if it's full, then we have to do something, even
	   though it's unlikely to actually be full. */
	if (rec->representation_ind == CW_REC_REPRESENTATION_CAPACITY - 1) {
		rec->state = RS_ERR_CHAR;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_ERROR,
			      "libcw: receiver's representation buffer is full");

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[rec->state]);

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* Since we effectively just saw the end of a tone, move to
	   the after-tone state. */
	rec->state = RS_AFTER_TONE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[rec->state]);

	return CW_SUCCESS;
}





/**
   \brief Add a dot to the receiver's representation buffer

   Documentation for both cw_receive_buffer_dot() and cw_receive_buffer_dash():

   Since we can't add an element to the buffer without any
   accompanying timing information, the functions accepts \p timestamp
   of the "end of element" event.  If the \p timestamp is NULL, the
   current timestamp is used.

   These routines are for client code that has already determined
   whether a dot or dash was received by a method other than calling
   the routines cw_start_receive_tone() and cw_end_receive_tone().

   On success, the relevant element is added to the receiver's
   representation buffer.

   On failure, the routines return CW_FAILURE, with errno set to
   ERANGE if preceded by a cw_start_receive_tone call with no matching
   cw_end_receive_tone or if an error condition currently exists
   within the receiver's buffer, or ENOMEM if the receiver's
   representation buffer is full.

   \param timestamp - timestamp of "end of dot" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_buffer_dot(const struct timeval *timestamp)
{
	return cw_receiver_add_element_internal(&cw_receiver, timestamp, CW_DOT_REPRESENTATION);
}





/**
   \brief Add a dash to the receiver's representation buffer

   See documentation of cw_receive_buffer_dot() for more information.

   \param timestamp - timestamp of "end of dash" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_buffer_dash(const struct timeval *timestamp)
{
	return cw_receiver_add_element_internal(&cw_receiver, timestamp, CW_DASH_REPRESENTATION);
}





/**
   \brief Get the current buffered representation from the receiver's representation buffer

   On success the function fills in \p representation with the
   contents of the current representation buffer and returns
   CW_SUCCESS.

   On failure, it returns CW_FAILURE and sets errno to:
   ERANGE if not preceded by a cw_end_receive_tone call, a prior
   successful cw_receive_representation call, or a prior
   cw_receive_buffer_dot or cw_receive_buffer_dash,
   EINVAL if the timestamp passed in is invalid,
   EAGAIN if the call is made too early to determine whether a
   complete representation has yet been placed in the buffer (that is,
   less than the inter-character gap period elapsed since the last
   cw_end_receive_tone or cw_receive_buffer_dot/dash call). This is
   not a *hard* error, just an information that the caller should try
   to get the representation later.

   \p is_end_of_word indicates that the delay after the last tone
   received is longer that the inter-word gap.

   \p is_error indicates that the representation was terminated by an
   error condition.

   TODO: the function should be called cw_receiver_get_representation().

   TODO: why we pass \p timestamp to the function when all we want
   from it is a representation from representation buffer? It seems
   that the function can be called with timestamp for last event,
   i.e. on end of last space. "last space" may mean space after last
   element (dot/dash) in a character, or a space between two
   characters, or (a bit different case) a space between
   words. Timestamp for end of space would be the same timestamp as
   for beginning of new tone (?).

   testedin::test_helper_receive_tests()

   \param timestamp - timestamp of event that ends "end-of-character" space or "end-of-word" space
   \param representation - buffer for representation (output parameter)
   \param is_end_of_word - buffer for "is end of word" state (output parameter)
   \param is_error - buffer for "error" state (output parameter)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_representation(const struct timeval *timestamp,
			      /* out */ char *representation,
			      /* out */ bool *is_end_of_word, /* out */ bool *is_error)
{

	/* If the receiver's state indicates that receiver's
	   representation buffer stores a completed representation at
	   the end of word, just return the representation via \p
	   representation.

	   Repeated calls of the function when receiver is in this
	   state would simply return the same representation over and
	   over again.

	   Notice that the state of receiver at this point is settled,
	   so \p timestamp is uninteresting. We don't expect it to
	   hold any useful information that could influence state of
	   receiver or content of representation buffer. */
	if (cw_receiver.state == RS_END_WORD
	    || cw_receiver.state == RS_ERR_WORD) {

		if (is_end_of_word) {
			*is_end_of_word = true;
		}
		if (is_error) {
			*is_error = (cw_receiver.state == RS_ERR_WORD);
		}
		*representation = '\0'; /* TODO: why do this? */
		strncat(representation, cw_receiver.representation, cw_receiver.representation_ind);
		return CW_SUCCESS;
	}


	if (cw_receiver.state == RS_IDLE
	    || cw_receiver.state == RS_IN_TONE) {

		/* Not a good time to call this function. */
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Four receiver states were covered above, so we are left
	   with these three: */
	cw_assert (cw_receiver.state == RS_AFTER_TONE
		   || cw_receiver.state == RS_END_CHAR
		   || cw_receiver.state == RS_ERR_CHAR,

		   "Unknown receiver state %d", cw_receiver.state);

	/* We now know the state is after a tone, or end-of-char,
	   perhaps with error.  For all three of these cases, we're
	   going to [re-]compare the \p timestamp with the tone_end
	   timestamp saved in receiver.

	   This could mean that in the case of end-of-char, we revise
	   our opinion on later calls to end-of-word. This is correct,
	   since it models reality. */

	/* If we weren't supplied with one, get the current timestamp
	   for comparison against the tone_end timestamp saved in
	   receiver. */
	struct timeval now_timestamp;
	if (!cw_timestamp_validate_internal(&now_timestamp, timestamp)) {
		return CW_FAILURE;
	}

	/* Now we need to compare the timestamps to determine the length
	   of the inter-tone gap. */
	int space_len_usecs = cw_timestamp_compare_internal(&cw_receiver.tone_end,
							    &now_timestamp);

	if (space_len_usecs == INT_MAX) {
		// fprintf(stderr, "space len == INT_MAX\n");
		errno = EAGAIN;
		return CW_FAILURE;
	}

	/* Synchronize low level timings if required */
	cw_sync_parameters_internal(cw_generator, &cw_receiver);


	if (space_len_usecs >= cw_receiver.eoc_range_minimum
	    && space_len_usecs <= cw_receiver.eoc_range_maximum) {

		/* The space is, within tolerance, a character
		   space. A representation of complete character is
		   now in representation buffer, we can return the
		   representation via parameter. */

		if (cw_receiver.state == RS_AFTER_TONE) {
			/* A character space after a tone means end of
			   character. Update receiver state. On
			   updating the state, update timing
			   statistics for an identified end of
			   character as well. */
			cw_receiver_add_statistic_internal(&cw_receiver, STAT_END_CHARACTER, space_len_usecs);
			cw_receiver.state = RS_END_CHAR;
		} else {
			/* We are already in RS_END_CHAR or
			   RS_ERR_CHAR, so nothing to do. */
		}

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

		/* Return the representation from receiver's buffer. */
		if (is_end_of_word) {
			*is_end_of_word = false;
		}
		if (is_error) {
			*is_error = (cw_receiver.state == RS_ERR_CHAR);
		}
		*representation = '\0'; /* TODO: why do this? */
		strncat(representation, cw_receiver.representation, cw_receiver.representation_ind);
		return CW_SUCCESS;
	}

	/* If the length of space indicated a word space, again we
	   have a complete representation and can return it.  In this
	   case, we also need to inform the client that this looked
	   like the end of a word, not just a character.

	   Any space length longer than eoc_range_maximum is, almost
	   by definition, an "end of word" space. */
	if (space_len_usecs > cw_receiver.eoc_range_maximum) {

		/* The space is a word space. Update receiver state,
		   remember to preserve error state (if any). */
		cw_receiver.state = cw_receiver.state == RS_ERR_CHAR
			? RS_ERR_WORD : RS_END_WORD;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

		/* Return the representation from receiver's buffer. */
		if (is_end_of_word) {
			*is_end_of_word = true;
		}
		if (is_error) {
			*is_error = (cw_receiver.state == RS_ERR_WORD);
		}
		*representation = '\0'; /* TODO: why do this? */
		strncat(representation, cw_receiver.representation, cw_receiver.representation_ind);
		return CW_SUCCESS;
	}

	/* The space - judging by \p timestamp - is neither an
	   inter-character space, nor inter-word space. If none of
	   these conditions holds, then we cannot *yet* make a
	   judgement on what we have in the buffer, so return
	   EAGAIN. */
	errno = EAGAIN;
	return CW_FAILURE;
}





/**
   \brief Get a current character

   Function returns the character currently stored in receiver's
   representation buffer.

   On success the function returns CW_SUCCESS, and fills \p c with the
   contents of the current representation buffer, translated into a
   character.

   On failure the function returns CW_FAILURE, with errno set to:

   ERANGE if not preceded by a cw_end_receive_tone() call, a prior
   successful cw_receive_character() call, or a
   cw_receive_buffer_dot() or cw_receive_buffer_dash() call,
   EINVAL if the timestamp passed in is invalid, or
   EAGAIN if the call is made too early to determine whether a
   complete character has yet been placed in the buffer (that is, less
   than the inter-character gap period elapsed since the last
   cw_end_receive_tone() or cw_receive_buffer_dot/dash call).
   ENOENT if character stored in receiver cannot be recognized as valid

   \p is_end_of_word indicates that the delay after the last tone
   received is longer that the inter-word gap.

   \p is_error indicates that the character was terminated by an error
   condition.

   testedin::test_helper_receive_tests()

   \param timestamp - timestamp of event that ends "end-of-character" space or "end-of-word" space
   \param c - buffer for character (output parameter)
   \param is_end_of_word - buffer for "is end of word" state (output parameter)
   \param is_error - buffer for "error" state (output parameter)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_character(const struct timeval *timestamp,
			 /* out */ char *c,
			 /* out */ bool *is_end_of_word, /* out */ bool *is_error)
{
	bool end_of_word, error;
	char representation[CW_REC_REPRESENTATION_CAPACITY + 1];

	/* See if we can obtain a representation from receiver. */
	int status = cw_receive_representation(timestamp, representation,
					       &end_of_word, &error);
	if (!status) {
		return CW_FAILURE;
	}

	/* Look up the representation using the lookup functions. */
	char character = cw_representation_to_character_internal(representation);
	if (!character) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	/* If we got this far, all is well, so return what we received. */
	if (c) {
		*c = character;
	}
	if (is_end_of_word) {
		*is_end_of_word = end_of_word;
	}
	if (is_error) {
		*is_error = error;
	}
	return CW_SUCCESS;
}





/**
   \brief Clear receiver's representation buffer

   Clears the receiver's representation buffer, resets receiver's
   internal state. This prepares the receiver to receive tones again.

   This routine must be called after successful, or terminating,
   cw_receive_representation() or cw_receive_character() calls, to
   clear the states and prepare the buffer to receive more tones.
*/
void cw_clear_receive_buffer(void)
{
	cw_receiver.representation_ind = 0;
	cw_receiver.state = RS_IDLE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s", cw_receiver_states[cw_receiver.state]);

	return;
}





/**
   \brief Get the number of elements (dots/dashes) the receiver's buffer can accommodate

   The maximum number of elements written out by cw_receive_representation()
   is the capacity + 1, the extra character being used for the terminating
   NUL.

   \return number of elements that can be stored in receiver's representation buffer
*/
int cw_get_receive_buffer_capacity(void)
{
	return CW_REC_REPRESENTATION_CAPACITY;
}





/**
   \brief Get the number of elements (dots/dashes) currently pending in the cw_receiver's representation buffer

   testedin::test_helper_receive_tests()

   \return number of elements in receiver's representation buffer
*/
int cw_get_receive_buffer_length(void)
{
	return cw_receiver.representation_ind;
}





/**
   \brief Clear receive data

   Clear the receiver's representation buffer, statistics, and any
   retained receiver's state.  This function is suitable for calling
   from an application exit handler.
*/
void cw_reset_receive(void)
{
	cw_receiver.representation_ind = 0;
	cw_receiver.state = RS_IDLE;

	cw_reset_receive_statistics();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state -> %s (reset)", cw_receiver_states[cw_receiver.state]);

	return;
}
