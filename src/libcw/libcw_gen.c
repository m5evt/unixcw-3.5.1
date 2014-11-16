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

   Generator is an object that has access to audio sink (soundcard,
   console buzzer, null audio device) and that can play dots and
   dashes using the audio sink.

   You can request generator to produce audio by using *_send_*()
   functions (these functions are still in libcw.c).
*/


#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <signal.h>

#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h> /* INT_MAX */
#endif

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif


#include "libcw_gen.h"
#include "libcw_rec.h"
#include "libcw_debug.h"
#include "libcw_console.h"
#include "libcw_key.h"
#include "libcw_utils.h"
#include "libcw_null.h"
#include "libcw_oss.h"
#include "libcw_key.h"
#include "libcw_signal.h"
#include "libcw_data.h"




#ifndef M_PI  /* C99 may not define M_PI */
#define M_PI  3.14159265358979323846
#endif





/* Main container for data related to generating audible Morse code.
   This is a global variable in library file, but in future the
   variable will be moved from the file to client code.

   This is a global variable that should be converted into a function
   argument; this pointer should exist only in client's code, should
   initially be returned by new(), and deleted by delete().

   TODO: perform the conversion later, when you figure out ins and
   outs of the library. */
cw_gen_t *cw_generator = NULL;





/* From libcw_rec.c. */
extern cw_rec_t cw_receiver;





/* From libcw_debug.c. */
extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;


/* From libcw_key.c. */
extern volatile cw_key_t cw_key;





/* Most of audio systems (excluding console) should be configured to
   have specific sample rate. Some audio systems (with connection with
   given hardware) can support several different sample rates. Values of
   supported sample rates are standardized. Here is a list of them to be
   used by this library.
   When the library configures given audio system, it tries if the system
   will accept a sample rate from the table, starting from the first one.
   If a sample rate is accepted, rest of sample rates is not tested anymore. */
const unsigned int cw_supported_sample_rates[] = {
	44100,
	48000,
	32000,
	22050,
	16000,
	11025,
	 8000,
	    0 /* guard */
};





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


static cw_gen_t *cw_gen_new_internal(int audio_system, const char *device);
static int       cw_gen_start_internal(cw_gen_t *gen);
static int       cw_gen_new_open_internal(cw_gen_t *gen, int audio_system, const char *device);
static void     *cw_gen_dequeue_and_play_internal(void *arg);
static int       cw_gen_calculate_sine_wave_internal(cw_gen_t *gen, cw_tone_t *tone);
static int       cw_gen_calculate_amplitude_internal(cw_gen_t *gen, cw_tone_t *tone);
static int       cw_gen_write_to_soundcard_internal(cw_gen_t *gen, int queue_state, cw_tone_t *tone);


static void cw_gen_reset_send_parameters_internal(cw_gen_t *gen);

static int cw_send_element_internal(cw_gen_t *gen, char element);
static int cw_send_representation_internal(cw_gen_t *gen, const char *representation, bool partial);
static int cw_send_character_internal(cw_gen_t *gen, char character, int partial);





/**
   \brief Get a readable label of current audio system

   The function returns one of following strings:
   None, Null, Console, OSS, ALSA, PulseAudio, Soundcard

   \return audio system's label
*/
const char *cw_generator_get_audio_system_label(void)
{
	return cw_get_audio_system_label(cw_generator->audio_system);
}





/**
   \brief Create new generator

   Allocate memory for new generator data structure, set up default values
   of some of the generator's properties.
   The function does not start the generator (generator does not produce
   a sound), you have to use cw_generator_start() for this.

   Notice that the function doesn't return a generator variable. There
   is at most one generator variable at any given time. You can't have
   two generators. In some future version of the library the function
   will return pointer to newly allocated generator, and then you
   could have as many of them as you want, but not yet.

   \p audio_system can be one of following: NULL, console, OSS, ALSA,
   PulseAudio, soundcard. See "enum cw_audio_systems" in libcw.h for
   exact names of symbolic constants.

   \param audio_system - audio system to be used by the generator
   \param device - name of audio device to be used; if NULL then library will use default device.
*/
int cw_generator_new(int audio_system, const char *device)
{
	cw_generator = cw_gen_new_internal(audio_system, device);
	if (!cw_generator) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: can't create generator");
		return CW_FAILURE;
	} else {
		/* For some (all?) applications a key needs to have
		   some generator associated with it. */
		cw_key_register_generator_internal(&cw_key, cw_generator);

		return CW_SUCCESS;
	}

}





/**
   \brief Deallocate generator

   Deallocate/destroy generator data structure created with call
   to cw_generator_new(). You can't start nor use the generator
   after the call to this function.
*/
void cw_generator_delete(void)
{
	cw_gen_delete_internal(&cw_generator);

	return;
}





/**
   \brief Start a generator

   Start producing tones using generator created with
   cw_generator_new(). The source of tones is a tone queue associated
   with the generator. If the tone queue is empty, the generator will
   wait for new tones to be queued.

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_generator_start(void)
{
	return cw_gen_start_internal(cw_generator);
}





/**
   \brief Start a generator
*/
int cw_gen_start_internal(cw_gen_t *gen)
{
	gen->phase_offset = 0.0;

	gen->generate = true;

	gen->client.thread_id = pthread_self();

	if (gen->audio_system == CW_AUDIO_NULL
	    || gen->audio_system == CW_AUDIO_CONSOLE
	    || gen->audio_system == CW_AUDIO_OSS
	    || gen->audio_system == CW_AUDIO_ALSA
	    || gen->audio_system == CW_AUDIO_PA) {

		/* cw_gen_dequeue_and_play_internal() is THE
		   function that does the main job of generating
		   tones. */
		int rv = pthread_create(&gen->thread.id, &gen->thread.attr,
					cw_gen_dequeue_and_play_internal,
					(void *) gen);
		if (rv != 0) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: failed to create %s generator thread", cw_get_audio_system_label(gen->audio_system));
			return CW_FAILURE;
		} else {
			/* for some yet unknown reason you have to
			   put usleep() here, otherwise a generator
			   may work incorrectly */
			usleep(100000);
#ifdef LIBCW_WITH_DEV
			cw_dev_debug_print_generator_setup(gen);
#endif
			return CW_SUCCESS;
		}
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: unsupported audio system %d", gen->audio_system);
	}

	return CW_FAILURE;
}





/**
   \brief Shut down a generator

   Silence tone generated by generator (level of generated sine wave is
   set to zero, with falling slope), and shut the generator down.

   The shutdown does not erase generator's configuration.

   If you want to have this generator running again, you have to call
   cw_generator_start().
*/
void cw_generator_stop(void)
{
	cw_gen_stop_internal(cw_generator);

	return;
}





/**
   \brief Return char string with console device path

   Returned pointer is owned by library.

   \return char string with current console device path
*/
const char *cw_get_console_device(void)
{
	return cw_generator->audio_device;
}





/**
   \brief Return char string with soundcard device name/path

   Returned pointer is owned by library.

   \return char string with current soundcard device name or device path
*/
const char *cw_get_soundcard_device(void)
{
	return cw_generator->audio_device;
}





#if 0
/* Not really needed. We may need to first stop generator, then do
   something else, and only then delete generator. */
/**
   \brief Stop and delete generator

   Stop and delete generator.
   This causes silencing current sound wave.

   \return CW_SUCCESS
*/
int cw_gen_release_internal(cw_gen_t **gen)
{
	cw_gen_stop_internal(*gen);
	cw_gen_delete_internal(gen);

	return CW_SUCCESS;
}
#endif





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
int cw_gen_set_audio_device_internal(cw_gen_t *gen, const char *device)
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





/**
   \brief Create new generator
*/
cw_gen_t *cw_gen_new_internal(int audio_system, const char *device)
{
#ifdef LIBCW_WITH_DEV
	fprintf(stderr, "libcw build %s %s\n", __DATE__, __TIME__);
#endif

	cw_assert (audio_system != CW_AUDIO_NONE, "can't create generator with audio system \"NONE\"");

	cw_gen_t *gen = (cw_gen_t *) malloc(sizeof (cw_gen_t));
	if (!gen) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: malloc()");
		return NULL;
	}

	gen->tq = cw_tq_new_internal();
	if (!gen->tq) {
		cw_gen_delete_internal(&gen);
		return CW_FAILURE;
	} else {
	gen->tq->gen = gen;
	}

	gen->audio_device = NULL;
	//gen->audio_system = audio_system;
	gen->audio_device_is_open = false;
	gen->dev_raw_sink = -1;


	/* Essential sending parameters. */
	gen->send_speed = CW_SPEED_INITIAL,
	gen->frequency = CW_FREQUENCY_INITIAL;
	gen->volume_percent = CW_VOLUME_INITIAL;
	gen->volume_abs = (gen->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
	gen->gap = CW_GAP_INITIAL;
	gen->weighting = CW_WEIGHTING_INITIAL;


	gen->parameters_in_sync = false;


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


	gen->dot_length = 0;
	gen->dash_length = 0;
	gen->eoe_delay = 0;
	gen->eoc_delay = 0;
	gen->additional_delay = 0;
	gen->eow_delay = 0;
	gen->adjustment_delay = 0;

	gen->buffer_sub_start = 0;
	gen->buffer_sub_stop  = 0;




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





/**
   \brief Delete a generator
*/
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





/**
   \brief Delete a generator - wrapper used in libcw_utils.c
*/
void cw_generator_delete_internal(void)
{
	if (cw_generator) {
		cw_gen_delete_internal(&cw_generator);
	}

	return;
}





/**
   \brief Stop a generator
*/
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
void *cw_gen_dequeue_and_play_internal(void *arg)
{
	cw_gen_t *gen = (cw_gen_t *) arg;

	/* Usually the code that queues tones only sets .frequency,
	   .usecs. and .slope_mode. Values of rest of fields will be
	   calculated in lower-level code. */
	cw_tone_t tone =
		{ .frequency = 0,
		  .usecs     = 0,
		  .slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES,

		  .n_samples       = 0,
		  .slope_iterator  = 0,
		  .slope_n_samples = 0 };

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
   in generator buffer's subarea.

   There will be (gen->buffer_sub_stop - gen->buffer_sub_start + 1)
   samples calculated and put into gen->buffer[], starting from
   gen->buffer[gen->buffer_sub_start].

   The function takes into account all state variables from gen,
   so initial phase of new fragment of sine wave in the buffer matches
   ending phase of a sine wave generated in previous call.

   \param gen - generator that generates sine wave
   \param tone - generated tone

   \return number of calculated samples
*/
int cw_gen_calculate_sine_wave_internal(cw_gen_t *gen, cw_tone_t *tone)
{
	assert (gen->buffer_sub_stop <= gen->buffer_n_samples);

	/* We need two separate iterators to correctly generate sine wave:
	    -- i -- for iterating through output buffer (generator
	            buffer's subarea), it can travel between buffer
	            cells delimited by start and stop (inclusive);
	    -- t -- for calculating phase of a sine wave; 't' always has to
	            start from zero for every calculated subarea (i.e. for
		    every call of this function);

	  Initial/starting phase of generated fragment is always retained
	  in gen->phase_offset, it is the only "memory" of previously
	  calculated fragment of sine wave (to be precise: it stores phase
	  of last sample in previously calculated fragment).
	  Therefore iterator used to calculate phase of sine wave can't have
	  the memory too. Therefore it has to always start from zero for
	  every new fragment of sine wave. Therefore a separate t. */

	double phase = 0.0;
	int t = 0;

	for (int i = gen->buffer_sub_start; i <= gen->buffer_sub_stop; i++) {
		phase = (2.0 * M_PI
				* (double) tone->frequency * (double) t
				/ (double) gen->sample_rate)
			+ gen->phase_offset;
		int amplitude = cw_gen_calculate_amplitude_internal(gen, tone);

		gen->buffer[i] = amplitude * sin(phase);
		if (tone->slope_iterator >= 0) {
			tone->slope_iterator++;
		}

		t++;
	}

	phase = (2.0 * M_PI
		 * (double) tone->frequency * (double) t
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

	return t;
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





/**
   \brief Write tone to soundcard
*/
int cw_gen_write_to_soundcard_internal(cw_gen_t *gen, int queue_state, cw_tone_t *tone)
{
	assert (queue_state != CW_TQ_STILL_EMPTY);

	if (queue_state == CW_TQ_JUST_EMPTIED) {
		/* All tones have been already dequeued from tone
		   queue.

		   \p tone does not represent a valid tone to play. At
		   first sight there is no need to write anything to
		   soundcard. But...

		   It may happen that during previous call to the
		   function there were too few samples in a tone to
		   completely fill a buffer (see #needmoresamples tag
		   below).

		   We need to fill the buffer until it is full and
		   ready to be sent to audio sink.

		   Padding the buffer with silence seems to be a good
		   idea (it will work regardless of value (Mark/Space)
		   of last valid tone). We just need to know how many
		   samples of the silence to produce.

		   Number of these samples will be stored in
		   samples_to_write. */

		/* Required length of padding space is from end of
		   last buffer subarea to end of buffer. */
		tone->n_samples = gen->buffer_n_samples - (gen->buffer_sub_stop + 1);;

		tone->usecs = 0;       /* This value matters no more, because now we only deal with samples. */
		tone->frequency = 0;   /* This fake tone is a piece of silence. */

		/* The silence tone used for padding doesn't require
		   any slopes. A slope falling to silence has been
		   already provided by last non-fake and non-silent
		   tone. */
		tone->slope_mode = CW_SLOPE_MODE_NO_SLOPES;
		tone->slope_iterator = -1;
		tone->slope_n_samples = 0;

		//fprintf(stderr, "++++ length of padding silence = %d [samples]\n", tone->n_samples);

	} else { /* queue_state == CW_TQ_NONEMPTY */

		if (tone->slope_mode == CW_SLOPE_MODE_RISING_SLOPE
		    || tone->slope_mode == CW_SLOPE_MODE_FALLING_SLOPE
		    || tone->slope_mode == CW_SLOPE_MODE_STANDARD_SLOPES) {

			/* A regular tone with slope(s). */
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

		/* About calculations above:
		   100 * 10000 = 1.000.000 usecs per second. */

		//fprintf(stderr, "++++ length of regular tone = %d [samples]\n", tone->n_samples);
	}


	/* Total number of samples to write in a loop below. */
	int64_t samples_to_write = tone->n_samples;

#if 0
	fprintf(stderr, "++++ entering loop, tone->frequency = %d, buffer->n_samples = %d, tone->n_samples = %d, samples_to_write = %d\n",
		tone->frequency, gen->buffer_n_samples, tone->n_samples, samples_to_write);
	fprintf(stderr, "++++ entering loop, expected ~%f loops\n", 1.0 * samples_to_write / gen->buffer_n_samples);
	int debug_loop = 0;
#endif


	// cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: %lld samples, %d usecs, %d Hz", tone->n_samples, tone->usecs, gen->frequency);
	while (samples_to_write > 0) {

		int64_t free_space = gen->buffer_n_samples - gen->buffer_sub_start;
		if (samples_to_write > free_space) {
			/* There will be some tone samples left for
			   next iteration of this loop.  But this
			   buffer will be ready to be pushed to audio
			   sink. */
			gen->buffer_sub_stop = gen->buffer_n_samples - 1;
		} else if (samples_to_write == free_space) {
			/* How nice, end of tone samples aligns with
			   end of buffer (last sample of tone will be
			   placed in last cell of buffer).

			   But the result is the same - a full buffer
			   ready to be pushed to audio sink. */
			gen->buffer_sub_stop = gen->buffer_n_samples - 1;
		} else {
			/* There will be too few samples to fill a
			   buffer. We can't send an unready buffer to
			   audio sink. We will have to somehow pad the
			   buffer. */
			gen->buffer_sub_stop = gen->buffer_sub_start + samples_to_write - 1;
		}

		/* How many samples of audio buffer's subarea will be
		   calculated in a given cycle of "calculate sine
		   wave" code? */
		int buffer_sub_n_samples = gen->buffer_sub_stop - gen->buffer_sub_start + 1;


#if 0
		fprintf(stderr, "++++        loop #%d, buffer_sub_n_samples = %d\n", ++debug_loop, buffer_sub_n_samples);
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
			      "libcw: sub start: %d, sub stop: %d, sub len: %d, to calculate: %d", gen->buffer_sub_start, gen->buffer_sub_stop, buffer_sub_n_samples, samples_to_write);
#endif


		int calculated = cw_gen_calculate_sine_wave_internal(gen, tone);
		cw_assert (calculated == buffer_sub_n_samples,
			   "calculated wrong number of samples: %d != %d\n",
			   calculated, buffer_sub_n_samples);


		if (gen->buffer_sub_stop == gen->buffer_n_samples - 1) {

			/* We have a buffer full of samples. The
			   buffer is ready to be pushed to audio
			   sink. */
			gen->write(gen);
			gen->buffer_sub_start = 0;
			gen->buffer_sub_stop = 0;
#if CW_DEV_RAW_SINK
			cw_dev_debug_raw_sink_write_internal(gen);
#endif
		} else {
			/* #needmoresamples
			   There is still some space left in the
			   buffer, go fetch new tone from tone
			   queue. */

			gen->buffer_sub_start = gen->buffer_sub_stop + 1;

			cw_assert (gen->buffer_sub_start <= gen->buffer_n_samples - 1,
				   "sub start out of range: sub start = %d, buffer n samples = %d",
				   gen->buffer_sub_start, gen->buffer_n_samples);
		}

		samples_to_write -= buffer_sub_n_samples;

#if 0
		if (samples_to_write < 0) {
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "samples left = %d", samples_to_write);
		}
#endif

	} /* while (samples_to_write > 0) { */

	//fprintf(stderr, "++++ left loop, %d loops, samples left = %d\n", debug_loop, (int) samples_to_write);

	return 0;
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
		cw_generator->parameters_in_sync = false;
		cw_gen_sync_parameters_internal(cw_generator);
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
		cw_generator->parameters_in_sync = false;
		cw_gen_sync_parameters_internal(cw_generator);


		/* Ideally generator and receiver should have their
		   own, separate cw_set_gap() functions. Unfortunately
		   this is not the case (for now) so gap should be set
		   here for receiver as well.

		   TODO: add set_gap() function for receiver.*/

		cw_receiver.gap = new_value;
		/* Changes of gap require resynchronization. */
		cw_receiver.parameters_in_sync = false;
		cw_rec_sync_parameters_internal(&cw_receiver);
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
		cw_generator->parameters_in_sync = false;
		cw_gen_sync_parameters_internal(cw_generator);
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
	cw_gen_sync_parameters_internal(cw_generator);

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
	cw_gen_sync_parameters_internal(gen);
	/* TODO: do we need to synchronize here receiver as well? */

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
	cw_gen_sync_parameters_internal(cw_generator);

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
	cw_gen_sync_parameters_internal(cw_generator);

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
/*               Section:Reset and synchronize parameters               */
/* ******************************************************************** */





/**
   \brief Reset send/receive parameters

   Reset the library speed, frequency, volume, gap, tolerance, weighting,
   adaptive receive, and noise spike threshold to their initial default
   values: send/receive speed 12 WPM, volume 70 %, frequency 800 Hz,
   gap 0 dots, tolerance 50 %, and weighting 50 %.
*/
void cw_reset_send_receive_parameters(void)
{
	cw_gen_reset_send_parameters_internal(cw_generator);
	cw_rec_reset_receive_parameters_internal(&cw_receiver);

	/* Reset requires resynchronization. */
	cw_generator->parameters_in_sync = false;
	cw_receiver.parameters_in_sync = false;

	cw_gen_sync_parameters_internal(cw_generator);
	cw_rec_sync_parameters_internal(&cw_receiver);

	return;
}






/**
  \brief Reset essential sending parameters to their initial values
*/
void cw_gen_reset_send_parameters_internal(cw_gen_t *gen)
{
	cw_assert (gen, "generator is NULL");

	gen->send_speed = CW_SPEED_INITIAL;
	gen->frequency = CW_FREQUENCY_INITIAL;
	gen->volume_percent = CW_VOLUME_INITIAL;
	gen->volume_abs = (cw_generator->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
	gen->gap = CW_GAP_INITIAL;
	gen->weighting = CW_WEIGHTING_INITIAL;

	return;

}





void cw_gen_sync_parameters_internal(cw_gen_t *gen)
{
	cw_assert (gen, "generator is NULL");

	/* Do nothing if we are already synchronized. */
	if (gen->parameters_in_sync) {
		return;
	}

	/* Set the length of a Dot to be a Unit with any weighting
	   adjustment, and the length of a Dash as three Dot lengths.
	   The weighting adjustment is by adding or subtracting a
	   length based on 50 % as a neutral weighting. */
	int unit_length = CW_DOT_CALIBRATION / gen->send_speed;
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

	/* Generator parameters are now in sync. */
	gen->parameters_in_sync = true;

	return;
}
