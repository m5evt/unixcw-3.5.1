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
/*                    Section:Global variables                          */
/* ******************************************************************** */

extern cw_gen_t *cw_generator;
extern cw_rec_t  cw_receiver;

extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;






/* Both generator and receiver contain a group of low-level timing
   parameters that should be recalculated (synchronized) on some
   events. This is a flag that allows us to decide whether it's time
   to recalculate the low-level parameters.

   TODO: this variable should be a field in either cw_gen_t or
   cw_receiver_t. */
bool cw_is_in_sync = false;





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
