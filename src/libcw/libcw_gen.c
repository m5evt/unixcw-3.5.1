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


/**
   \file libcw_gen.c

   Functions operating on one of core elements of libcw: a generator.
*/


#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <signal.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif


#include "libcw_internal.h"
#include "libcw_gen.h"
#include "libcw_debug.h"
#include "libcw_console.h"
#include "libcw_key.h"
#include "libcw_utils.h"
#include "libcw_null.h"
#include "libcw_oss.h"





#ifndef M_PI  /* C99 may not define M_PI */
#define M_PI  3.14159265358979323846
#endif





/* From libcw_debug.c. */
extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;





/* Every audio system opens an audio device: a default device, or some
   other device. Default devices have their default names, and here is
   a list of them. It is indexed by values of "enum cw_audio_systems". */
static const char *default_audio_devices[] = {
	(char *) NULL,          /* CW_AUDIO_NONE */
	CW_DEFAULT_NULL_DEVICE, /* CW_AUDIO_NULL */
	CW_DEFAULT_CONSOLE_DEVICE,
	CW_DEFAULT_OSS_DEVICE,
	CW_DEFAULT_ALSA_DEVICE,
	CW_DEFAULT_PA_DEVICE,
	(char *) NULL }; /* just in case someone decided to index the table with CW_AUDIO_SOUNDCARD */





static int cw_gen_new_open_internal(cw_gen_t *gen, int audio_system, const char *device);
static int cw_gen_calculate_sine_wave_internal(cw_gen_t *gen, cw_tone_t *tone);
static int cw_gen_calculate_amplitude_internal(cw_gen_t *gen, cw_tone_t *tone);
static int cw_gen_write_to_soundcard_internal(cw_gen_t *gen, int queue_state, cw_tone_t *tone);





/**
   \brief Set audio device name or path

   Set path to audio device, or name of audio device. The path/name
   will be associated with given generator \p gen, and used when opening
   audio device.

   Use this function only when setting up a generator.

   Function creates its own copy of input string.

   \param gen - generator to be updated
   \param device - device to be assigned to generator \p gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on errors
*/
int cw_generator_set_audio_device_internal(cw_gen_t *gen, const char *device)
{
	/* this should be NULL, either because it has been
	   initialized statically as NULL, or set to
	   NULL by generator destructor */
	assert (!gen->audio_device);
	assert (gen->audio_system != CW_AUDIO_NONE);

	if (gen->audio_system == CW_AUDIO_NONE) {
		gen->audio_device = (char *) NULL;
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: no audio system specified");
		return CW_FAILURE;
	}

	if (device) {
		gen->audio_device = strdup(device);
	} else {
		gen->audio_device = strdup(default_audio_devices[gen->audio_system]);
	}

	if (!gen->audio_device) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: malloc()");
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





/**
   \brief Silence the generator

   Force the generator \p to go silent.
   Function stops the generator as well, but does not flush its queue.

   \param gen - generator to be silenced

   \return CW_SUCCESS on success
   \return CW_FAILURE on errors
*/
int cw_gen_silence_internal(cw_gen_t *gen)
{
	if (!gen) {
		/* this may happen because the process of finalizing
		   usage of libcw is rather complicated; this should
		   be somehow resolved */
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING,
			      "libcw: called the function for NULL generator");
		return CW_SUCCESS;
	}

	int status = CW_SUCCESS;

	if (gen->audio_system == CW_AUDIO_NULL) {
		; /* pass */
	} else if (gen->audio_system == CW_AUDIO_CONSOLE) {
		/* sine wave generation should have been stopped
		   by a code generating dots/dashes, but
		   just in case... */
		cw_console_silence(gen);
		status = CW_FAILURE;

	} else if (gen->audio_system == CW_AUDIO_OSS
		   || gen->audio_system == CW_AUDIO_ALSA
		   || gen->audio_system == CW_AUDIO_PA) {

		cw_tone_t tone;
		tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
		tone.frequency = 0;
		tone.usecs = CW_AUDIO_QUANTUM_USECS;
		status = cw_tone_queue_enqueue_internal(gen->tq, &tone);

		/* allow some time for playing the last tone */
		usleep(2 * CW_AUDIO_QUANTUM_USECS);
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: called silence() function for generator without audio system specified");
	}

	if (gen->audio_system == CW_AUDIO_ALSA) {
		/* "Stop a PCM dropping pending frames. " */
		cw_alsa_drop(gen);
	}

	//gen->generate = false;

	return status;
}





cw_gen_t *cw_gen_new_internal(int audio_system, const char *device)
{
#ifdef LIBCW_WITH_DEV
	fprintf(stderr, "libcw build %s %s\n", __DATE__, __TIME__);
#endif

	cw_gen_t *gen = (cw_gen_t *) malloc(sizeof (cw_gen_t));
	if (!gen) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: malloc()");
		return NULL;
	}

	gen->tq = cw_tq_new_internal();
	gen->tq->gen = gen;

	gen->audio_device = NULL;
	//gen->audio_system = audio_system;
	gen->audio_device_is_open = false;
	gen->dev_raw_sink = -1;
	gen->send_speed = CW_SPEED_INITIAL,
	gen->frequency = CW_FREQUENCY_INITIAL;
	gen->volume_percent = CW_VOLUME_INITIAL;
	gen->volume_abs = (gen->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
	gen->gap = CW_GAP_INITIAL;
	gen->buffer = NULL;
	gen->buffer_n_samples = -1;

	gen->oss_version.x = -1;
	gen->oss_version.y = -1;
	gen->oss_version.z = -1;

	gen->client.name = (char *) NULL;

	gen->tone_slope.length_usecs = CW_AUDIO_SLOPE_USECS;
	gen->tone_slope.shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
	gen->tone_slope.amplitudes = NULL;
	gen->tone_slope.n_amplitudes = 0;

#ifdef LIBCW_WITH_PULSEAUDIO
	gen->pa_data.s = NULL;

	gen->pa_data.ba.prebuf    = (uint32_t) -1;
	gen->pa_data.ba.tlength   = (uint32_t) -1;
	gen->pa_data.ba.minreq    = (uint32_t) -1;
	gen->pa_data.ba.maxlength = (uint32_t) -1;
	gen->pa_data.ba.fragsize  = (uint32_t) -1;
#endif

	gen->open_device = NULL;
	gen->close_device = NULL;
	gen->write = NULL;

	pthread_attr_init(&gen->thread.attr);
	pthread_attr_setdetachstate(&gen->thread.attr, PTHREAD_CREATE_DETACHED);

	gen->weighting = CW_WEIGHTING_INITIAL;

	gen->dot_length = 0;
	gen->dash_length = 0;
	gen->eoe_delay = 0;
	gen->eoc_delay = 0;
	gen->additional_delay = 0;
	gen->eow_delay = 0;
	gen->adjustment_delay = 0;


	gen->key = (cw_key_t *) NULL;


	int rv = cw_gen_new_open_internal(gen, audio_system, device);
	if (rv == CW_FAILURE) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: failed to open audio device for audio system '%s' and device '%s'", cw_get_audio_system_label(audio_system), device);
		cw_gen_delete_internal(&gen);
		return CW_FAILURE;
	}

	if (audio_system == CW_AUDIO_NULL
	    || audio_system == CW_AUDIO_CONSOLE) {

		; /* the two types of audio output don't require audio buffer */
	} else {
		gen->buffer = (cw_sample_t *) malloc(gen->buffer_n_samples * sizeof (cw_sample_t));
		if (!gen->buffer) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: malloc()");
			cw_gen_delete_internal(&gen);
			return CW_FAILURE;
		}
	}

	/* Set slope that late, because it uses value of sample rate.
	   The sample rate value is set in
	   cw_gen_new_open_internal(). */
	rv = cw_generator_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, CW_AUDIO_SLOPE_USECS);
	if (rv == CW_FAILURE) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: failed to set slope");
		cw_gen_delete_internal(&gen);
		return CW_FAILURE;
	}

	cw_sigalrm_install_top_level_handler_internal();

	return gen;
}





void cw_gen_delete_internal(cw_gen_t **gen)
{
	cw_assert (gen, "\"gen\" argument can't be NULL\n");

	if (!*gen) {
		return;
	}

	if ((*gen)->generate) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
			      "libcw: you forgot to call cw_generator_stop()");
		cw_gen_stop_internal(*gen);
	}

	/* Wait for "write" thread to end accessing output
	   file descriptor. I have come up with value 500
	   after doing some experiments.

	   FIXME: magic number. I think that we can come up
	   with algorithm for calculating the value. */
	usleep(500);

	free((*gen)->audio_device);
	(*gen)->audio_device = NULL;

	free((*gen)->buffer);
	(*gen)->buffer = NULL;

	if ((*gen)->close_device) {
		(*gen)->close_device(*gen);
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: WARNING: NULL function pointer, something went wrong");
	}

	pthread_attr_destroy(&((*gen)->thread.attr));

	free((*gen)->client.name);
	(*gen)->client.name = NULL;

	free((*gen)->tone_slope.amplitudes);
	(*gen)->tone_slope.amplitudes = NULL;

	cw_tq_delete_internal(&((*gen)->tq));

	(*gen)->audio_system = CW_AUDIO_NONE;

	free(*gen);
	*gen = NULL;

	return;
}





void cw_gen_stop_internal(cw_gen_t *gen)
{
	if (!gen) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING,
			      "libcw: called the function for NULL generator");
		return;
	}

	cw_tq_flush_internal(gen->tq);

	cw_gen_silence_internal(gen);

	gen->generate = false;

	/* this is to wake up cw_signal_wait_internal() function
	   that may be waiting for signal in while() loop in thread
	   function; */
	pthread_kill(gen->thread.id, SIGALRM);

	/* Sleep a bit to postpone closing a device.
	   This way we can avoid a situation when "generate" is set
	   to zero and device is being closed while a new buffer is
	   being prepared, and while write() tries to write this
	   new buffer to already closed device.

	   Without this usleep(), writei() from ALSA library may
	   return "File descriptor in bad state" error - this
	   happened when writei() tried to write to closed ALSA
	   handle.

	   The delay also allows the generator function thread to stop
	   generating tone and exit before we resort to killing generator
	   function thread. */
	struct timespec req = { .tv_sec = 1, .tv_nsec = 0 };
	cw_nanosleep_internal(&req);

	/* check if generator thread is still there */
	int rv = pthread_kill(gen->thread.id, 0);
	if (rv == 0) {
		/* thread function didn't return yet; let's help it a bit */
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING, "libcw: EXIT: forcing exit of thread function");
		rv = pthread_kill(gen->thread.id, SIGKILL);
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING, "libcw: EXIT: pthread_kill() returns %d/%s", rv, strerror(rv));
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_INFO, "libcw: EXIT: seems that thread function exited voluntarily");
	}

	return;
}





/**
  \brief Open audio system

  A wrapper for code trying to open audio device specified by
  \p audio_system.  Open audio system will be assigned to given
  generator. Caller can also specify audio device to use instead
  of a default one.

  \param gen - freshly created generator
  \param audio_system - audio system to open and assign to the generator
  \param device - name of audio device to be used instead of a default one

  \return CW_SUCCESS on success
  \return CW_FAILURE otherwise
*/
int cw_gen_new_open_internal(cw_gen_t *gen, int audio_system, const char *device)
{
	/* FIXME: this functionality is partially duplicated in
	   src/cwutils/cw_common.c/cw_generator_new_from_config() */

	/* This function deliberately checks all possible values of
	   audio system name in separate 'if' clauses before it gives
	   up and returns CW_FAILURE. PA/OSS/ALSA are combined with
	   SOUNDCARD, so I have to check all three of them (because \p
	   audio_system may be set to SOUNDCARD). And since I check
	   the three in separate 'if' clauses, I can check all other
	   values of audio system as well. */

	if (audio_system == CW_AUDIO_NULL) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_NULL];
		if (cw_is_null_possible(dev)) {
			cw_null_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_PA
	    || audio_system == CW_AUDIO_SOUNDCARD) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_PA];
		if (cw_is_pa_possible(dev)) {
			cw_pa_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_OSS
	    || audio_system == CW_AUDIO_SOUNDCARD) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_OSS];
		if (cw_is_oss_possible(dev)) {
			cw_oss_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_ALSA
	    || audio_system == CW_AUDIO_SOUNDCARD) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_ALSA];
		if (cw_is_alsa_possible(dev)) {
			cw_alsa_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_CONSOLE) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_CONSOLE];
		if (cw_is_console_possible(dev)) {
			cw_console_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	/* there is no next audio system type to try */
	return CW_FAILURE;
}





/**
   \brief Dequeue tones and push them to audio output

   Function dequeues tones from tone queue associated with generator
   and then sends them to preconfigured audio output (soundcard, NULL
   or console).

   Function dequeues tones (or waits for new tones in queue) and
   pushes them to audio output as long as generator->generate is true.

   The generator must be fully configured before calling this
   function.

   \param arg - generator (casted to (void *)) to be used for generating tones

   \return NULL pointer
*/
void *cw_generator_dequeue_and_play_internal(void *arg)
{
	cw_gen_t *gen = (cw_gen_t *) arg;

	/* Usually the code that queues tones only sets .frequency,
	   .usecs. and .slope_mode. Values of rest of fields will be
	   calculated in lower-level code. */
	cw_tone_t tone =
		{ .frequency = 0,
		  .usecs     = 0,
		  .n_samples = 0,

		  .sub_start = 0,
		  .sub_stop  = 0,

		  .slope_iterator  = 0,
		  .slope_mode      = CW_SLOPE_MODE_STANDARD_SLOPES,
		  .slope_n_samples = 0 };

	gen->samples_left = 0;
	gen->samples_calculated = 0;

	// POSSIBLE ALTERNATIVE IMPLEMENTATION: int old_state = QS_IDLE;

	while (gen->generate) {
		int state = cw_tone_queue_dequeue_internal(gen->tq, &tone);
		if (state == CW_TQ_STILL_EMPTY) {
		// POSSIBLE ALTERNATIVE IMPLEMENTATION: if (state == QS_IDLE && old_state == QS_IDLE) {

			/* Tone queue has been totally drained with
			   previous call to dequeue(). No point in
			   making next iteration of while() and
			   calling the function again. So don't call
			   it, wait for signal from enqueue() function
			   informing that a new tone appeared in tone
			   queue. */

			/* TODO: can we / should we specify on which
			   signal exactly we are waiting for? */
			cw_signal_wait_internal();
			//usleep(CW_AUDIO_QUANTUM_USECS);
			continue;
		}

		// POSSIBLE ALTERNATIVE IMPLEMENTATION: old_state = state;

		cw_key_ik_increment_timer_internal(gen->key, tone.usecs);

#ifdef LIBCW_WITH_DEV
		cw_debug_ev ((&cw_debug_object_ev), 0, tone.frequency ? CW_DEBUG_EVENT_TONE_HIGH : CW_DEBUG_EVENT_TONE_LOW);
#endif

		if (gen->audio_system == CW_AUDIO_NULL) {
			cw_null_write(gen, &tone);
		} else if (gen->audio_system == CW_AUDIO_CONSOLE) {
			cw_console_write(gen, &tone);
		} else {
			cw_gen_write_to_soundcard_internal(gen, state, &tone);
		}

		/*
		  When sending text from text input, the signal:
		   - allows client code to observe moment when state of tone
		     queue is "low/critical"; client code then can add more
		     characters to the queue; the observation is done using
		     cw_wait_for_tone_queue_critical();
		   - ...

		 */
		pthread_kill(gen->client.thread_id, SIGALRM);

		/* Generator may be used by iambic keyer to measure
		   periods of time (lengths of Mark and Space) - this
		   is achieved by enqueueing Marks and Spaces by keyer
		   in generator.

		   At this point the generator has finished generating
		   a tone of specified length. A duration of Mark or
		   Space has elapsed. Inform iambic keyer that the
		   tone it has enqueued has elapsed.

		   (Whether iambic keyer has enqueued any tones or
		   not, and whether it is waiting for the
		   notification, is a different story. We will let the
		   iambic keyer function called below to decide what
		   to do with the notification. If keyer is in idle
		   graph state, it will ignore the notification.)

		   Notice that this mechanism is needed only for
		   iambic keyer. Inner workings of straight key are
		   much more simple, the straight key doesn't need to
		   use generator as a timer. */

		/* FIXME: There is a big problem:
		   cw_gen_write_to_soundcard_internal() call made above may
		   be pretty good at telling sound card to produce
		   tones of specific length, but surely is not the
		   best, the most precise source of timing needed to
		   control iambic keyer.

		   While lengths of tones passed to the function are
		   precise, and tones produced by soundcard are also
		   quite precise, the time of execution of the
		   function is not constant. I've noticed a situation,
		   when first call to the function (after dequeueing
		   first tone from tq) can take ~1000 us, and all
		   following tones last roughly the same as
		   tone.usecs, which can be 1-2 orders of magnitude
		   more.

		   We need to find another place to make the call to
		   cw_key_ik_update_graph_state_internal(), or at
		   least pass to it some reliable source of timing.

		   INFO to FIXME: it seems that this problem has been
		   fixed with call to
		   cw_key_ik_increment_timer_internal() above,
		   and all the other new or changed code in libcw and
		   xcwcp that is related to keyer's timer. */

		if (!cw_key_ik_update_graph_state_internal(gen->key)) {
			/* just try again, once */
			usleep(1000);
			cw_key_ik_update_graph_state_internal(gen->key);
		}

#ifdef LIBCW_WITH_DEV
		cw_debug_ev ((&cw_debug_object_ev), 0, tone.frequency ? CW_DEBUG_EVENT_TONE_LOW : CW_DEBUG_EVENT_TONE_HIGH);
#endif

	} /* while(gen->generate) */

	cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_INFO,
		      "libcw: EXIT: generator stopped (gen->generate = %d)", gen->generate);

	/* Some functions in client thread may be waiting for the last
	   SIGALRM from the generator thread to continue/finalize their
	   business. Let's send the SIGALRM right before exiting.
	   This small delay before sending signal turns out to be helpful. */
	struct timespec req = { .tv_sec = 0, .tv_nsec = 500000000 };
	cw_nanosleep_internal(&req);

	pthread_kill(gen->client.thread_id, SIGALRM);
	return NULL;
}





/**
   \brief Calculate a fragment of sine wave

   Calculate a fragment of sine wave, as many samples as can be fitted
   in generator's buffer. There will be gen->buffer_n_samples samples
   calculated and put into gen->buffer[], starting from
   gen->buffer[0].

   The function takes into account all state variables from gen,
   so initial phase of new fragment of sine wave in the buffer matches
   ending phase of a sine wave generated in previous call.

   \param gen - generator that generates sine wave
   \param tone - generated tone

   \return position in buffer at which a last sample has been saved
*/
int cw_gen_calculate_sine_wave_internal(cw_gen_t *gen, cw_tone_t *tone)
{
	assert (tone->sub_stop <= gen->buffer_n_samples);

	/* We need two separate iterators to correctly generate sine wave:
	    -- i -- for iterating through output buffer, it can travel
	            between buffer cells indexed by start and stop;
	    -- j -- for calculating phase of a sine wave; it always has to
	            start from zero for every calculated fragment (i.e. for
		    every call of this function);

	  Initial/starting phase of generated fragment is always retained
	  in gen->phase_offset, it is the only "memory" of previously
	  calculated fragment of sine wave (to be precise: it stores phase
	  of last sample in previously calculated fragment).
	  Therefore iterator used to calculate phase of sine wave can't have
	  the memory too. Therefore it has to always start from zero for
	  every new fragment of sine wave. Therefore j.	*/

	double phase = 0.0;
	int i = 0, j = 0;

	for (i = tone->sub_start, j = 0; i <= tone->sub_stop; i++, j++) {
		phase = (2.0 * M_PI
				* (double) tone->frequency * (double) j
				/ (double) gen->sample_rate)
			+ gen->phase_offset;
		int amplitude = cw_gen_calculate_amplitude_internal(gen, tone);

		gen->buffer[i] = amplitude * sin(phase);
		if (tone->slope_iterator >= 0) {
			tone->slope_iterator++;
		}
	}

	phase = (2.0 * M_PI
		 * (double) tone->frequency * (double) j
		 / (double) gen->sample_rate)
		+ gen->phase_offset;

	/* "phase" is now phase of the first sample in next fragment to be
	   calculated.
	   However, for long fragments this can be a large value, well
	   beyond <0; 2*Pi) range.
	   The value of phase may further accumulate in different
	   calculations, and at some point it may overflow. This would
	   result in an audible click.

	   Let's bring back the phase from beyond <0; 2*Pi) range into the
	   <0; 2*Pi) range, in other words lets "normalize" it. Or, in yet
	   other words, lets apply modulo operation to the phase.

	   The normalized phase will be used as a phase offset for next
	   fragment (during next function call). It will be added phase of
	   every sample calculated in next function call. */

	int n_periods = floor(phase / (2.0 * M_PI));
	gen->phase_offset = phase - n_periods * 2.0 * M_PI;

	return i;
}





/**
   \brief Calculate value of a single sample of sine wave

   This function calculates an amplitude (a value) of a single sample
   in sine wave PCM data.

   Actually "calculation" is a bit too big word. The function is just
   a three-level-deep decision tree, deciding which of precalculated
   values to return. There are no complicated arithmetical
   calculations being made each time the function is called, so the
   execution time should be pretty small.

   The precalcuated values depend on some factors, so the values
   should be re-calculated each time these factors change. See
   cw_generator_set_tone_slope() for list of these factors.

   A generator stores some of information needed to get an amplitude
   of every sample in a sine wave - this is why we have \p gen.  If
   tone's slopes are non-rectangular, the length of slopes is defined
   in generator. If a tone is non-silent, the volume is also defined
   in generator.

   However, decision tree for getting the amplitude also depends on
   some parameters that are strictly bound to tone, such as what is
   the shape of slopes for a given tone - this is why we have \p tone.
   The \p also stores iterator of samples - this is how we know for
   which sample to calculate the amplitude.

   \param gen - generator used to generate a sine wave
   \param tone - tone being generated

   \return value of a sample of sine wave, a non-negative number
*/
int cw_gen_calculate_amplitude_internal(cw_gen_t *gen, cw_tone_t *tone)
{
	int amplitude = 0;
#if 0
	/* blunt algorithm for calculating amplitude;
	   for debug purposes only */
	if (tone->frequency) {
		amplitude = gen->volume_abs;
	} else {
		amplitude = 0;
	}

	return amplitude;
#else

	if (tone->frequency > 0) {
		if (tone->slope_mode == CW_SLOPE_MODE_RISING_SLOPE) {
			if (tone->slope_iterator < tone->slope_n_samples) {
				int i = tone->slope_iterator;
				//amplitude = 1.0 * gen->volume_abs * i / tone->slope_n_samples;
				amplitude = gen->tone_slope.amplitudes[i];
				//cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: 1: slope: %d, amp: %d", tone->slope_iterator, amplitude);
			} else {
				amplitude = gen->volume_abs;
				assert (amplitude >= 0);
			}
		} else if (tone->slope_mode == CW_SLOPE_MODE_FALLING_SLOPE) {
			if (tone->slope_iterator > tone->n_samples - tone->slope_n_samples + 1) {
				int i = tone->n_samples - tone->slope_iterator - 1;
				assert (i >= 0);
				//amplitude = 1.0 * gen->volume_abs * i / tone->slope_n_samples;
				amplitude = gen->tone_slope.amplitudes[i];
				//cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: 2: slope: %d, amp: %d", tone->slope_iterator, amplitude);
				assert (amplitude >= 0);
			} else {
				amplitude = gen->volume_abs;
				assert (amplitude >= 0);
			}
		} else if (tone->slope_mode == CW_SLOPE_MODE_NO_SLOPES) {
			amplitude = gen->volume_abs;
			assert (amplitude >= 0);
		} else { // tone->slope_mode == CW_SLOPE_MODE_STANDARD_SLOPES
			/* standard algorithm for generating slopes:
			   single, finite tone with:
			    - rising slope at the beginning,
			    - a period of wave with constant amplitude,
			    - falling slope at the end. */
			if (tone->slope_iterator >= 0 && tone->slope_iterator < tone->slope_n_samples) {
				/* beginning of tone, produce rising slope */
				int i = tone->slope_iterator;
				//amplitude = 1.0 * gen->volume_abs * i / tone->slope_n_samples;
				amplitude = gen->tone_slope.amplitudes[i];
				//cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: rising slope: i = %d, amp = %d", tone->slope_iterator, amplitude);
				assert (amplitude >= 0);
			} else if (tone->slope_iterator >= tone->slope_n_samples && tone->slope_iterator < tone->n_samples - tone->slope_n_samples) {
				/* middle of tone, constant amplitude */
				amplitude = gen->volume_abs;
				assert (amplitude >= 0);
			} else if (tone->slope_iterator >= tone->n_samples - tone->slope_n_samples) {
				/* falling slope */
				int i = tone->n_samples - tone->slope_iterator - 1;
				assert (i >= 0);
				//amplitude = 1.0 * gen->volume_abs * i / tone->slope_n_samples;
				amplitude = gen->tone_slope.amplitudes[i];
				//cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: falling slope: i = %d, amp = %d", tone->slope_iterator, amplitude);
				assert (amplitude >= 0);
			} else {
				;
				assert (amplitude >= 0);
			}
		}
	} else {
		amplitude = 0;
	}

	assert (amplitude >= 0); /* will fail if calculations above are modified */

#endif

#if 0 /* no longer necessary since calculation of amplitude,
	 implemented above guarantees that amplitude won't be
	 less than zero, and amplitude slightly larger than
	 volume is not an issue */

	/* because CW_AUDIO_VOLUME_RANGE may not be exact multiple
	   of gen->slope, amplitude may be sometimes out
	   of range; this may produce audible clicks;
	   remove values out of range */
	if (amplitude > CW_AUDIO_VOLUME_RANGE) {
		amplitude = CW_AUDIO_VOLUME_RANGE;
	} else if (amplitude < 0) {
		amplitude = 0;
	} else {
		;
	}
#endif

	return amplitude;
}





/**
   \brief Set parameters of tones generated by generator

   Most of variables related to slope of tones is in tone data type,
   but there are still some variables that are generator-specific, as
   they are common for all tones.  This function sets these
   variables.

   One of the variables is a table of amplitudes for every point in
   slope. Values in the table are generated only once, when parameters
   of the slope change. This saves us from re-calculating amplitudes
   of slope for every tone. With the table at hand we can simply look
   up an amplitude of point of slope in the table of amplitudes.

   You can pass -1 as value of \p slope_shape or \p slope_usecs, the
   function will then either resolve correct values of its arguments,
   or will leave related parameters of slope unchanged.

   The function should be called every time one of following
   parameters change:

   \li shape of slope,
   \li length of slope,
   \li generator's sample rate,
   \li generator's volume.

   There are four supported shapes of slopes:
   \li linear (the only one supported by libcw until version 4.1.1),
   \li raised cosine (supposedly the most desired shape),
   \li sine,
   \li rectangular.

   Use CW_TONE_SLOPE_SHAPE_* symbolic names as values of \p slope_shape.

   FIXME: first argument of this public function is gen, but no
   function provides access to generator variable.

   \param gen - generator for which to set tone slope parameters
   \param slope_shape - shape of slope: linear, raised cosine, sine, rectangular
   \param slope_usecs - length of slope, in microseconds

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_generator_set_tone_slope(cw_gen_t *gen, int slope_shape, int slope_usecs)
{
	 assert (gen);

	 if (slope_shape != -1) {
		 gen->tone_slope.shape = slope_shape;
	 }

	 if (slope_usecs != -1) {
		 gen->tone_slope.length_usecs = slope_usecs;
	 }

	 if (slope_usecs == 0) {
		 if (slope_shape != -1 && slope_shape != CW_TONE_SLOPE_SHAPE_RECTANGULAR) {
			 cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
				       "libcw: specified a non-rectangular slope shape, but slope len == 0");
			 assert (0);
		 }

		 gen->tone_slope.shape = CW_TONE_SLOPE_SHAPE_RECTANGULAR;
		 gen->tone_slope.length_usecs = 0;

		 return CW_SUCCESS;
	 }

	 if (slope_shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR) {
		 if (slope_usecs > 0) {
			 cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
				       "libcw: specified a rectangular slope shape, but slope len != 0");
			 assert (0);
		 }

		 gen->tone_slope.shape = slope_shape;
		 gen->tone_slope.length_usecs = 0;

		 return CW_SUCCESS;
	 }

	 int slope_n_samples = ((gen->sample_rate / 100) * gen->tone_slope.length_usecs) / 10000;
	 assert (slope_n_samples);
	 if (slope_n_samples > 1000 * 1000) {
		 /* Let's be realistic: if slope is longer than 1M
		    samples, there is something wrong.
		    At sample rate = 48kHz this would mean 20 seconds
		    of slope. */
		 return CW_FAILURE;
	 }

	 /* TODO: from this point until end of function is a code
	    re-calculating amplitudes table (possibly reallocating it
	    as well). Consider moving it to separate function, and
	    perhaps writing unit test code for it. */

	 /* In theory we could reallocate the table every time the
	    function is called.  In practice the function may be most
	    often called when user changes volume of tone (and then
	    the function may be called several times in a row if volume
	    is changed in steps), and in such circumstances the size
	    of amplitudes table doesn't change.

	    So to save some time we do this check in "if ()". */

	 if (gen->tone_slope.n_amplitudes != slope_n_samples) {
		 gen->tone_slope.amplitudes = realloc(gen->tone_slope.amplitudes, sizeof(float) * slope_n_samples);
		 if (!gen->tone_slope.amplitudes) {
			 cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
				       "libcw: realloc()");
			 return CW_FAILURE;
		 }
		 gen->tone_slope.n_amplitudes = slope_n_samples;
	 }

	 /* Recalculate amplitudes of PCM samples that form tone's
	    slopes.

	    The values in amplitudes[] change from zero to max (at
	    least for any sane slope shape), so naturally they can be
	    used in forming rising slope. However they can be used in
	    forming falling slope as well - just iterate the table
	    from end to beginning. */
	 for (int i = 0; i < slope_n_samples; i++) {

		 if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_LINEAR) {
			 gen->tone_slope.amplitudes[i] = 1.0 * gen->volume_abs * i / slope_n_samples;

		 } else if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_SINE) {
			 float radian = i * (M_PI / 2.0)  / slope_n_samples;
			 gen->tone_slope.amplitudes[i] = sin(radian) * gen->volume_abs;


		 } else if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RAISED_COSINE) {
			 float radian = i * M_PI / slope_n_samples;
			 gen->tone_slope.amplitudes[i] = (1 - ((1 + cos(radian)) / 2)) * gen->volume_abs;

		 } else {
			 /* CW_TONE_SLOPE_SHAPE_RECTANGULAR is covered
			    before entering this "for" loop. */
			 cw_assert (0, "Unsupported slope shape %d", gen->tone_slope.shape);
		 }
	 }

	 return CW_SUCCESS;
}





int cw_gen_write_to_soundcard_internal(cw_gen_t *gen, int queue_state, cw_tone_t *tone)
{
	assert (queue_state != CW_TQ_STILL_EMPTY);

	if (queue_state == CW_TQ_JUST_EMPTIED) {
		/* all tones have been dequeued from tone queue,
		   but it may happen that not all "buffer_n_samples"
		   samples were calculated, only "samples_calculated"
		   samples.
		   We need to fill the buffer until it is full and
		   ready to be sent to audio sink.
		   We need to calculate value of samples_left
		   to proceed. */
		gen->samples_left = gen->buffer_n_samples - gen->samples_calculated;

		tone->slope_iterator = -1;
		tone->slope_mode = CW_SLOPE_MODE_NO_SLOPES;
		tone->frequency = 0;

	} else { /* queue_state == CW_TQ_NONEMPTY */

		if (tone->slope_mode == CW_SLOPE_MODE_RISING_SLOPE
		    || tone->slope_mode == CW_SLOPE_MODE_FALLING_SLOPE
		    || tone->slope_mode == CW_SLOPE_MODE_STANDARD_SLOPES) {

			tone->slope_iterator = 0;

		} else if (tone->slope_mode == CW_SLOPE_MODE_NO_SLOPES) {
			if (tone->usecs == CW_AUDIO_FOREVER_USECS) {
				tone->usecs = CW_AUDIO_QUANTUM_USECS;
				tone->slope_iterator = -1;
			}
		} else {
			cw_assert (0, "tone->slope_mode = %d", tone->slope_mode);
		}

		/* Length of a tone in samples:
		   - whole standard tone, with rising slope, steady
		     state and falling slope (slopes' length may be
		     zero), or
		   - a part of longer, "forever" tone: either a
		     fragment being rising slope, or falling slope, or
		     "no slopes" fragment in between.

		   Either way - a total length of dequeued tone,
		   converted from microseconds to samples. */
		tone->n_samples = gen->sample_rate / 100;
		tone->n_samples *= tone->usecs;
		tone->n_samples /= 10000;

		/* Length in samples of a single slope (rising or falling)
		   in standard tone of limited, known in advance length. */
		tone->slope_n_samples = gen->sample_rate / 100;
		tone->slope_n_samples *= gen->tone_slope.length_usecs;
		tone->slope_n_samples /= 10000;

		/* About calculations above: 100 * 10000 = 1.000.000
		   usecs per second. */


		/* Total number of samples to write in a loop below. */
		gen->samples_left = tone->n_samples;
	}



	// cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: %lld samples, %d usecs, %d Hz", tone->n_samples, tone->usecs, gen->frequency);
	while (gen->samples_left > 0) {
		if (tone->sub_start + gen->samples_left >= gen->buffer_n_samples) {
			tone->sub_stop = gen->buffer_n_samples - 1;
		} else {
			tone->sub_stop = tone->sub_start + gen->samples_left - 1;
		}
		gen->samples_calculated = tone->sub_stop - tone->sub_start + 1;
		gen->samples_left -= gen->samples_calculated;

#if 0
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
			      "libcw: start: %d, stop: %d, calculated: %d, to calculate: %d", tone->sub_start, tone->sub_stop, gen->samples_calculated, gen->samples_left);
		if (gen->samples_left < 0) {
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "samples left = %d", gen->samples_left);
		}
#endif

		cw_gen_calculate_sine_wave_internal(gen, tone);
		if (tone->sub_stop + 1 == gen->buffer_n_samples) {

			/* We have a buffer full of samples. The
			   buffer is ready to be pushed to audio
			   sink. */
			gen->write(gen);
			tone->sub_start = 0;
#if CW_DEV_RAW_SINK
			cw_dev_debug_raw_sink_write_internal(gen);
#endif
		} else {
			/* there is still some space left in the
			   buffer, go fetch new tone from tone queue */
			tone->sub_start = tone->sub_stop + 1;
		}
	} /* while (gen->samples_left > 0) { */

	return 0;
}
