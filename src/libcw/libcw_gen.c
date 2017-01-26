/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)

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

   \brief Generate pcm samples according to tones from tone queue, and
   send them to audio sink.

   Functions operating on one of core elements of libcw: a generator.

   Generator is an object that has access to audio sink (soundcard,
   console buzzer, null audio device) and that can generate dots and
   dashes using the audio sink.

   You can request generator to produce audio by using *_enqeue_*()
   functions.

   The inner workings of the generator seem to be quite simple:
   1. dequeue tone from tone queue
   2. recalculate tone length in microseconds into tone length in samples
   3. for every sample in tone, calculate sine wave sample and
      put it in generator's constant size buffer
   4. if buffer is full of sine wave samples, push it to audio sink
   5. since buffer is shorter than (almost) any tone, you will push
      the buffer multiple times per tone
   6. if you iterated over all samples in tone, but you still didn't
      fill up that last buffer, dequeue next tone from queue, go to #2
   7. if there are no more tones in queue, pad the buffer with silence,
      push the buffer to audio sink, and wait for signal from tone queue.

   Looks simple, right? But it's the little details that ruin it all.
   One of the details is tone's slopes.
*/




#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>
#include <errno.h>
#include <inttypes.h> /* uint32_t */

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif




#include "libcw_gen.h"
#include "libcw_rec.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_data.h"

#include "libcw_null.h"
#include "libcw_console.h"
#include "libcw_oss.h"




#ifndef M_PI  /* C99 may not define M_PI */
#define M_PI  3.14159265358979323846
#endif




/* From libcw_debug.c. */
extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




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




/* Generic constants - common for all audio systems (or not used in some of systems). */

static const long int CW_AUDIO_VOLUME_RANGE = (1 << 15);  /* 2^15 = 32768 */
static const int      CW_AUDIO_SLOPE_LEN = 5000;          /* Length of a single slope (rising or falling) in standard tone. [us] */

/* Shortest length of time (in microseconds) that is used by libcw for
   idle waiting and idle loops. If a libcw function needs to wait for
   something, or make an idle loop, it should call
   usleep(N * gen->quantum_len)

   This is also length of a single "forever" tone. */
static const int CW_AUDIO_QUANTUM_LEN_INITIAL = 100;  /* [us] */




static int    cw_gen_new_open_internal(cw_gen_t * gen, int audio_system, const char * device);
static void * cw_gen_dequeue_and_generate_internal(void * arg);
static int    cw_gen_calculate_sine_wave_internal(cw_gen_t * gen, cw_tone_t * tone);
static int    cw_gen_calculate_amplitude_internal(cw_gen_t * gen, const cw_tone_t * tone);
static int    cw_gen_write_to_soundcard_internal(cw_gen_t * gen, cw_tone_t * tone, bool is_empty_tone);

static int   cw_gen_enqueue_valid_character_internal(cw_gen_t * gen, char c);
static int   cw_gen_enqueue_valid_character_partial_internal(cw_gen_t * gen, char character);
static int   cw_gen_enqueue_representation_partial_internal(cw_gen_t * gen, const char * representation);

static void  cw_gen_recalculate_slopes_internal(cw_gen_t * gen);

static int   cw_gen_join_thread_internal(cw_gen_t * gen);
static void  cw_gen_empty_tone_calculate_samples_size_internal(const cw_gen_t * gen, cw_tone_t * tone);
static void  cw_gen_tone_calculate_samples_size_internal(const cw_gen_t * gen, cw_tone_t * tone);




/**
   \brief Start a generator

   Start given \p generator. As soon as there are tones enqueued in generator, the generator will start playing them.

   \reviewed on 2017-01-26

   \return CW_SUCCESS on success
   \return CW_FAILURE on errors
*/
int cw_gen_start(cw_gen_t * gen)
{
	gen->phase_offset = 0.0;

	/* This should be set to true before launching
	   cw_gen_dequeue_and_generate_internal(), because loop in the
	   function run only when the flag is set. */
	gen->do_dequeue_and_generate = true;

	/* This generator exists in client's application thread.
	   Generator's 'dequeue and generate' function will be a separate thread. */
	gen->client.thread_id = pthread_self();

	if (gen->audio_system != CW_AUDIO_NULL
	    && gen->audio_system != CW_AUDIO_CONSOLE
	    && gen->audio_system != CW_AUDIO_OSS
	    && gen->audio_system != CW_AUDIO_ALSA
	    && gen->audio_system != CW_AUDIO_PA) {

		gen->do_dequeue_and_generate = false;

		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw/gen: unsupported audio system %d", gen->audio_system);
		return CW_FAILURE;
	}


	/* cw_gen_dequeue_and_generate_internal() is THE
	   function that does the main job of generating
	   tones. */
	int rv = pthread_create(&gen->thread.id, &gen->thread.attr,
				cw_gen_dequeue_and_generate_internal,
				(void *) gen);
	if (rv != 0) {
		gen->do_dequeue_and_generate = false;

		cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw/gen: failed to create %s generator thread", cw_get_audio_system_label(gen->audio_system));
		return CW_FAILURE;
	} else {
		/* TODO: shouldn't we be doing it in generator's thread function? */
		gen->thread.running = true;

		/* FIXME: For some yet unknown reason we have to put
		   usleep() here, otherwise a generator may
		   work incorrectly */
		usleep(100000);
#ifdef LIBCW_WITH_DEV
		cw_dev_debug_print_generator_setup(gen);
#endif
		return CW_SUCCESS;
	}
}




/**
   \brief Set audio device name or path

   Set path to audio device, or name of audio device. The path/name
   will be associated with given generator \p gen, and used when opening
   audio device.

   Use this function only when setting up a generator.

   Function creates its own copy of input string.

   \reviewed on 2017-01-26

   \param gen - generator to be updated
   \param device - device to be assigned to generator \p gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on errors
*/
int cw_gen_set_audio_device_internal(cw_gen_t * gen, char const * device)
{
	/* This should be NULL, either because it has been
	   initialized statically as NULL, or set to
	   NULL by generator destructor */
	cw_assert (NULL == gen->audio_device, "libcw/gen: audio device already set\n");
	cw_assert (gen->audio_system != CW_AUDIO_NONE, "libcw/gen: audio system not set\n");

	if (gen->audio_system == CW_AUDIO_NONE) {
		gen->audio_device = (char *) NULL;
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw/gen: no audio system specified");
		return CW_FAILURE;
	}

	if (device) {
		gen->audio_device = strdup(device);
	} else {
		gen->audio_device = strdup(default_audio_devices[gen->audio_system]);
	}

	if (!gen->audio_device) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR, "libcw/gen: malloc()");
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}




/**
   \brief Silence the generator

   Force an audio sink currently used by generator \p gen to go
   silent.

   The function does not clear/flush tone queue, nor does it stop the
   generator. It just makes sure that audio sink (console / OSS / ALSA
   / PulseAudio) does not produce a sound of any frequency and any
   volume.

   You probably want to call cw_tq_flush_internal(gen->tq) before
   calling this function.

   \reviewed on 2017-01-26

   \param gen - generator using an audio sink that should be silenced

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure to silence an audio sink
*/
int cw_gen_silence_internal(cw_gen_t * gen)
{
	if (!gen) {
		/* This may happen because the process of finalizing
		   usage of libcw is rather complicated. This should
		   be somehow resolved. */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_WARNING,
			      "libcw/gen: called the function for NULL generator");
		return CW_SUCCESS;
	}

	if (!gen->thread.running) {
		/* Silencing a generator means enqueueing and generating
		   a tone with zero frequency.  We shouldn't do this
		   when a "dequeue-and-generate-a-tone" function is not
		   running (anymore). This is not an error situation,
		   so return CW_SUCCESS. */
		return CW_SUCCESS;
	}

	/* Somewhere there may be a key in "down" state and we need to
	   make it go "up", regardless of audio sink (even for
	   CDW_AUDIO_NULL, because that audio system can also be used with a key).
	   Otherwise the key may stay in "down" state forever. */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
	int status = cw_tq_enqueue_internal(gen->tq, &tone);

	if (gen->audio_system == CW_AUDIO_NULL
	    || gen->audio_system == CW_AUDIO_OSS
	    || gen->audio_system == CW_AUDIO_ALSA
	    || gen->audio_system == CW_AUDIO_PA) {

		/* Allow some time for playing the last tone. */
		usleep(2 * gen->quantum_len); /* TODO: this should be usleep(2 * tone->len). */

	} else if (gen->audio_system == CW_AUDIO_CONSOLE) {
		/* Sine wave generation should have been stopped
		   by a code generating dots/dashes, but
		   just in case...

		   TODO: is it still necessary after adding the
		   quantum of silence above? */
		cw_console_silence(gen);
	} else {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw/gen: called silence() function for generator without audio system specified");
	}

	if (gen->audio_system == CW_AUDIO_ALSA) {
		/* "Stop a PCM dropping pending frames. " */
		cw_alsa_drop(gen);
	}

	/* TODO: we just want to silence the generator, right? So we don't stop it.
	   This line of code has been disabled some time before 2017-01-26. */
	//gen->do_dequeue_and_generate = false;

	return status;
}




/**
   \brief Create new generator

   \reviewed on 2017-01-26

   testedin::test_cw_gen_new_delete()
*/
cw_gen_t * cw_gen_new(int audio_system, char const * device)
{
#ifdef LIBCW_WITH_DEV
	fprintf(stderr, "libcw build %s %s\n", __DATE__, __TIME__);
#endif

	cw_assert (audio_system != CW_AUDIO_NONE, "libcw/gen: can't create generator with audio system '%s'", cw_get_audio_system_label(audio_system));

	cw_gen_t * gen = (cw_gen_t *) malloc(sizeof (cw_gen_t));
	if (!gen) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR, "libcw/gen: malloc()");
		return (cw_gen_t *) NULL;
	}



	/* Tone queue. */
	{
		gen->tq = cw_tq_new_internal();
		if (!gen->tq) {
			cw_gen_delete(&gen);
			return (cw_gen_t *) NULL;
		} else {
			/* Sometimes tq needs to access a key associated with generator. */
			gen->tq->gen = gen;
		}
	}



	/* Parameters. */
	{
		/* Generator's basic parameters. */
		gen->send_speed = CW_SPEED_INITIAL;
		gen->frequency = CW_FREQUENCY_INITIAL;
		gen->volume_percent = CW_VOLUME_INITIAL;
		gen->volume_abs = (gen->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
		gen->gap = CW_GAP_INITIAL;
		gen->weighting = CW_WEIGHTING_INITIAL;


		/* Generator's timing parameters. */
		gen->dot_len = 0;
		gen->dash_len = 0;
		gen->eom_space_len = 0;
		gen->eoc_space_len = 0;
		gen->eow_space_len = 0;

		gen->additional_space_len = 0;
		gen->adjustment_space_len = 0;


		/* Generator's misc parameters. */
		gen->quantum_len = CW_AUDIO_QUANTUM_LEN_INITIAL;


		gen->parameters_in_sync = false;
	}



	/* Misc fields. */
	{
		/* Audio buffer and related items. */
		gen->buffer = NULL;
		gen->buffer_n_samples = -1;
		gen->buffer_sub_start = 0;
		gen->buffer_sub_stop  = 0;

		gen->sample_rate = -1;
		gen->phase_offset = -1;


		/* Tone parameters. */
		gen->tone_slope.len = CW_AUDIO_SLOPE_LEN;
		gen->tone_slope.shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		gen->tone_slope.amplitudes = NULL;
		gen->tone_slope.n_amplitudes = 0;


		/* Library's client. */
		gen->client.thread_id = -1;
		gen->client.name = (char *) NULL;


		/* CW key associated with this generator. */
		gen->key = (cw_key_t *) NULL;
	}


	/* pthread */
	{
		gen->thread.id = -1;
		pthread_attr_init(&gen->thread.attr);
		/* Thread must be joinable in order to make a safe call to
		   pthread_kill(thread_id, 0). pthreads are joinable by
		   default, but I take this explicit call as a good
		   opportunity to make this comment. */
		pthread_attr_setdetachstate(&gen->thread.attr, PTHREAD_CREATE_JOINABLE);
		gen->thread.running = false;

		/* TODO: doesn't this duplicate gen->thread.running flag? */
		gen->do_dequeue_and_generate = false;
	}


	/* Audio system. */
	{
		gen->audio_device = NULL;
		gen->audio_sink = -1;
		/* gen->audio_system = audio_system; */ /* We handle this field below. */
		gen->audio_device_is_open = false;
		gen->dev_raw_sink = -1;

		gen->open_device = NULL;
		gen->close_device = NULL;
		gen->write = NULL;


		/* Audio system - OSS. */
		gen->oss_version.x = -1;
		gen->oss_version.y = -1;
		gen->oss_version.z = -1;

		/* Audio system - ALSA. */
#ifdef LIBCW_WITH_ALSA
		gen->alsa_data.handle = NULL;
#endif

		/* Audio system - PulseAudio. */
#ifdef LIBCW_WITH_PULSEAUDIO
		gen->pa_data.s = NULL;

		gen->pa_data.ba.prebuf    = (uint32_t) -1;
		gen->pa_data.ba.tlength   = (uint32_t) -1;
		gen->pa_data.ba.minreq    = (uint32_t) -1;
		gen->pa_data.ba.maxlength = (uint32_t) -1;
		gen->pa_data.ba.fragsize  = (uint32_t) -1;
#endif

		int rv = cw_gen_new_open_internal(gen, audio_system, device);
		if (rv == CW_FAILURE) {
			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
				      "libcw/gen: failed to open audio sink for audio system '%s' and device '%s'", cw_get_audio_system_label(audio_system), device);
			cw_gen_delete(&gen);
			return (cw_gen_t *) NULL;
		}

		if (audio_system == CW_AUDIO_NULL
		    || audio_system == CW_AUDIO_CONSOLE) {

			; /* The two types of audio output don't require audio buffer. */
		} else {
			gen->buffer = (cw_sample_t *) malloc(gen->buffer_n_samples * sizeof (cw_sample_t));
			if (!gen->buffer) {
				cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
					      "libcw/gen: malloc()");
				cw_gen_delete(&gen);
				return (cw_gen_t *) NULL;
			}
		}

		/* Set slope that late, because it uses value of sample rate.
		   The sample rate value is set in
		   cw_gen_new_open_internal(). */
		rv = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, CW_AUDIO_SLOPE_LEN);
		if (rv == CW_FAILURE) {
			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
				      "libcw/gen: failed to set slope");
			cw_gen_delete(&gen);
			return (cw_gen_t *) NULL;
		}
	}

	return gen;
}




/**
   \brief Delete a generator

   \reviewed on 2017-01-26

   testedin::test_cw_gen_new_delete()
*/
void cw_gen_delete(cw_gen_t ** gen)
{
	cw_assert (gen, "libcw/gen: generator is NULL");

	if (!*gen) {
		return;
	}

	if ((*gen)->do_dequeue_and_generate) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
			      "libcw/gen: you forgot to call cw_gen_stop()");
		cw_gen_stop(*gen);
	}


	cw_tq_delete_internal(&(*gen)->tq);


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
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_WARNING, "libcw/gen: WARNING: 'close' function pointer is NULL, something went wrong");
	}

	pthread_attr_destroy(&(*gen)->thread.attr);

	free((*gen)->client.name);
	(*gen)->client.name = NULL;

	free((*gen)->tone_slope.amplitudes);
	(*gen)->tone_slope.amplitudes = NULL;

	(*gen)->audio_system = CW_AUDIO_NONE;

	free(*gen);
	*gen = NULL;

	return;
}




/**
   \brief Stop a generator

   1. Empty generator's tone queue.
   2. Silence generator's audio sink.
   3. Stop generator' "dequeue and generate" thread function.
   4. If the thread does not stop in one second, kill it.

   You have to use cw_gen_start() if you want to enqueue and
   generate tones with the same generator again.

   The function may return CW_FAILURE only when silencing of
   generator's audio sink fails.
   Otherwise function returns CW_SUCCESS.

   \reviewed on 2017-01-26

   \param gen - generator to stop

   \return CW_SUCCESS if all three (four) actions completed (successfully)
   \return CW_FAILURE if any of the actions failed (see note above)
*/
int cw_gen_stop(cw_gen_t * gen)
{
	if (!gen) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_WARNING,
			      "libcw/gen: called the function for NULL generator");
		/* Not really a runtime error, so return
		   CW_SUCCESS. */
		return CW_SUCCESS;
	}


	/* FIXME: Something goes wrong when cw_gen_stop() is called
	   from signal handler. pthread_cond_destroy() hangs because
	   there is an interrupted pthread_cond_wait() in frame
	   #8. Signalling it won't help because even if a condition
	   variable is signalled, the function won't be able to
	   continue. Stopping of generator, especially in emergency
	   situations, needs to be re-thought.

	   This is probably fixed by not calling
	   pthread_cond_destroy() in cw_tq_delete_internal(). */

	/*
#0  __pthread_cond_destroy (cond=0x1b130f0) at pthread_cond_destroy.c:77
#1  0x00007f15b393179d in cw_tq_delete_internal (tq=0x1b13118) at libcw_tq.c:219
#2  0x00007f15b392e2ca in cw_gen_delete (gen=0x1b13118, gen@entry=0x6069e0 <generator>) at libcw_gen.c:608
#3  0x000000000040207f in cw_atexit () at cw.c:668
#4  0x00007f15b35b6bc9 in __run_exit_handlers (status=status@entry=0, listp=0x7f15b39225a8 <__exit_funcs>,
    run_list_atexit=run_list_atexit@entry=true) at exit.c:82
#5  0x00007f15b35b6c15 in __GI_exit (status=status@entry=0) at exit.c:104
#6  0x00000000004020d7 in signal_handler (signal_number=2) at cw.c:686
#7  <signal handler called>
#8  pthread_cond_wait@@GLIBC_2.3.2 () at ../nptl/sysdeps/unix/sysv/linux/x86_64/pthread_cond_wait.S:185
#9  0x00007f15b3931f3b in cw_tq_wait_for_level_internal (tq=0x1af5be0, level=level@entry=1) at libcw_tq.c:964
#10 0x00007f15b392f938 in cw_gen_wait_for_queue_level (gen=<optimized out>, level=level@entry=1) at libcw_gen.c:2701
#11 0x000000000040241e in send_cw_character (c=c@entry=102, is_partial=is_partial@entry=0) at cw.c:501
#12 0x0000000000401d3d in parse_stream (stream=0x7f15b39234e0 <_IO_2_1_stdin_>) at cw.c:538
#13 main (argc=<optimized out>, argv=<optimized out>) at cw.c:652
	 */

	cw_tq_flush_internal(gen->tq);

	int rv = cw_gen_silence_internal(gen);
	if (rv != CW_SUCCESS) {
		return CW_FAILURE;
	}

	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_INFO,
		      "libcw/gen: gen->do_dequeue_and_generate = false");

	gen->do_dequeue_and_generate = false;
	fprintf(stderr, "libcw/gen: setting do_dequeue_and_generate to %d\n", gen->do_dequeue_and_generate);

	if (!gen->thread.running) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_INFO, "libcw/gen: EXIT: seems that thread function was not started at all");

		/* Don't call pthread_kill() on non-initialized
		   thread.id. The generator wasn't even started, so
		   let's return CW_SUCCESS. */

		/* TODO: what about code that doesn't use signals?
		   Should we return here? */
		return CW_SUCCESS;
	}


	/* "while (gen->do_dequeue_and_generate)" loop in thread function
	   may be in a state where dequeue() function returned IDLE
	   state, and the loop is waiting for new tone.

	   This is to force the loop to start new cycle, make the loop
	   notice that gen->do_dequeue_and_generate is false, and to
	   get the thread function to return (and thus to end the
	   thread). */

#if 0 /* This was disabled some time before 2017-01-19. */
	pthread_mutex_lock(&gen->tq->wait_mutex);
	pthread_cond_broadcast(&gen->tq->wait_var);
	pthread_mutex_unlock(&gen->tq->wait_mutex);
#endif

	pthread_mutex_lock(&gen->tq->dequeue_mutex);
	/* Use pthread_cond_signal() because there is only one listener: loop in generator's thread function. */
	pthread_cond_signal(&gen->tq->dequeue_var);
	pthread_mutex_unlock(&gen->tq->dequeue_mutex);

#if 0   /* Original implementation using signals. */  /* This was disabled some time before 2017-01-19. */
	pthread_kill(gen->thread.id, SIGALRM);
#endif

	return cw_gen_join_thread_internal(gen);
}




/**
   \brief Wrapper for pthread_join() and debug code

   \reviewed on 2017-01-26

   \param gen

   \return CW_SUCCESS if joining succeeded
   \return CW_FAILURE otherwise
*/
int cw_gen_join_thread_internal(cw_gen_t * gen)
{
	/* TODO: this comment may no longer be true and necessary.
	   Sleep a bit to postpone closing a device.  This way we can
	   avoid a situation when "do_dequeue_and_generate" is set to false
	   and device is being closed while a new buffer is being
	   prepared, and while write() tries to write this new buffer
	   to already closed device.

	   Without this sleep(), writei() from ALSA library may
	   return "File descriptor in bad state" error - this
	   happened when writei() tried to write to closed ALSA
	   handle.

	   The delay also allows the generator function thread to stop
	   generating tone (or for tone queue to get out of CW_TQ_IDLE
	   state) and exit before we resort to killing generator
	   function thread. */
	struct timespec req = { .tv_sec = 1, .tv_nsec = 0 };
	cw_nanosleep_internal(&req);


#define CW_DEBUG_TIMING_JOIN 1

#if CW_DEBUG_TIMING_JOIN   /* Debug code to measure how long it takes to join threads. */
	struct timeval before, after;
	gettimeofday(&before, NULL);
#endif


	int rv = pthread_join(gen->thread.id, NULL);


#if CW_DEBUG_TIMING_JOIN   /* Debug code to measure how long it takes to join threads. */
	gettimeofday(&after, NULL);
	cw_debug_msg (&cw_debug_object, CW_DEBUG_GENERATOR, CW_DEBUG_INFO, "libcw/gen: joining thread took %d us", cw_timestamp_compare_internal(&before, &after));
#endif


	if (rv == 0) {
		gen->thread.running = false;
		return CW_SUCCESS;
	} else {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR, "libcw/gen: failed to join threads: '%s'", strerror(rv));
		return CW_FAILURE;
	}
}




/**
   \brief Open audio system

   A wrapper for code trying to open audio device specified by
   \p audio_system.  Open audio system will be assigned to given
   generator. Caller can also specify audio device to use instead
   of a default one.

   \reviewed on 2017-01-26

   \param gen - freshly created generator
   \param audio_system - audio system to open and assign to the generator
   \param device - name of audio device to be used instead of a default one

   \return CW_SUCCESS on success
   \return CW_FAILURE otherwise
*/
int cw_gen_new_open_internal(cw_gen_t * gen, int audio_system, char const * device)
{
	/* FIXME: this functionality is partially duplicated in
	   src/cwutils/cw_common.c/cw_gen_new_from_config() */

	/* This function deliberately checks all possible values of
	   audio system name in separate 'if' clauses before it gives
	   up and returns CW_FAILURE. PA/OSS/ALSA are combined with
	   SOUNDCARD, so I have to check all three of them (because \p
	   audio_system may be set to SOUNDCARD). And since I check
	   the three in separate 'if' clauses, I can check all other
	   values of audio system as well. */

	if (audio_system == CW_AUDIO_NULL) {

		char const * dev = device ? device : default_audio_devices[CW_AUDIO_NULL];
		if (cw_is_null_possible(dev)) {
			cw_null_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_PA
	    || audio_system == CW_AUDIO_SOUNDCARD) {

		char const * dev = device ? device : default_audio_devices[CW_AUDIO_PA];
		if (cw_is_pa_possible(dev)) {
			cw_pa_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_OSS
	    || audio_system == CW_AUDIO_SOUNDCARD) {

		char const * dev = device ? device : default_audio_devices[CW_AUDIO_OSS];
		if (cw_is_oss_possible(dev)) {
			cw_oss_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_ALSA
	    || audio_system == CW_AUDIO_SOUNDCARD) {

		char const * dev = device ? device : default_audio_devices[CW_AUDIO_ALSA];
		if (cw_is_alsa_possible(dev)) {
			cw_alsa_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_CONSOLE) {

		char const * dev = device ? device : default_audio_devices[CW_AUDIO_CONSOLE];
		if (cw_is_console_possible(dev)) {
			cw_console_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	/* There is no next audio system type to try. */
	return CW_FAILURE;
}




/**
   \brief Dequeue tones and push them to audio output

   This is a thread function.

   Function dequeues tones from tone queue associated with generator
   and then sends them to preconfigured audio output (soundcard, NULL
   or console).

   Function dequeues tones (or waits for new tones in queue) and
   pushes them to audio output as long as
   generator->do_dequeue_and_generate is true.

   The generator must be fully configured before creating thread with
   this function.

   \reviewed on 2017-01-26

   \param arg - generator (casted to (void *)) to be used for generating tones

   \return NULL pointer
*/
void * cw_gen_dequeue_and_generate_internal(void * arg)
{
	cw_gen_t * gen = (cw_gen_t *) arg;

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, 0, CW_SLOPE_MODE_STANDARD_SLOPES);

	int dequeued_prev = CW_FAILURE; /* Status of previous call to dequeue(). */
	int dequeued_now = CW_FAILURE; /* Status of current call to dequeue(). */

	while (gen->do_dequeue_and_generate) {
		dequeued_now = cw_tq_dequeue_internal(gen->tq, &tone);
		if (!dequeued_now && !dequeued_prev) {

			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_INFO,
				      "libcw/gen: queue is idle");

			/* We won't get here while there are some
			   accumulated tones in queue, because
			   cw_tq_dequeue_internal() will be handling
			   them just fine without any need for
			   synchronization or wait().  Only after the
			   queue has been completely drained, we will
			   be forced to wait() here.

			   It's much better to wait only sometimes
			   after cw_tq_dequeue_internal() than wait
			   always before cw_tq_dequeue_internal().

			   We are waiting for kick from enqueue()
			   function informing that a new tone appeared
			   in tone queue.

			   The kick may also come from cw_gen_stop()
			   that gently asks this function to stop
			   idling and nicely return. */

			pthread_mutex_lock(&(gen->tq->dequeue_mutex));
			pthread_cond_wait(&gen->tq->dequeue_var, &gen->tq->dequeue_mutex);
			pthread_mutex_unlock(&(gen->tq->dequeue_mutex));

#if 0                   /* Original implementation using signals. */ /* This code has been disabled some time before 2017-01-19. */
			/* TODO: can we / should we specify on which
			   signal exactly we are waiting for? */
			cw_signal_wait_internal();
#endif
			continue;
		}

		bool is_empty_tone = !dequeued_now && dequeued_prev;

		if (gen->key) {
			int state = CW_KEY_STATE_OPEN;

			if ((dequeued_now && dequeued_prev) || (dequeued_now && !dequeued_prev)) {
				/* Flag combinations 1 and 2.
				   A valid tone has been dequeued just now. */
				state = tone.frequency ? CW_KEY_STATE_CLOSED : CW_KEY_STATE_OPEN;

			} else if (!dequeued_now && dequeued_prev) {
				/* Flag combination 3.
				   Tone queue just went empty. No tone == no sound. */
				state = CW_KEY_STATE_OPEN;

			} else {
				/* !dequeued_now && !dequeued_prev */
				/* Flag combination 4.
				   Tone queue continues to be empty.
				   This combination was handled right
				   after cw_tq_dequeue_internal(), we
				   should be waiting there for kick
				   from tone queue.  Us being here is
				   an error. */
				cw_assert (0, "libcw/gen: uncaught combination of flags: dequeued_now = %d, dequeued_prev = %d",
					   dequeued_now, dequeued_prev);
			}
			cw_key_tk_set_value_internal(gen->key, state);


			cw_key_ik_increment_timer_internal(gen->key, tone.len);
		}
		dequeued_prev = dequeued_now;


#ifdef LIBCW_WITH_DEV
		cw_debug_ev (&cw_debug_object_ev, 0, tone.frequency ? CW_DEBUG_EVENT_TONE_HIGH : CW_DEBUG_EVENT_TONE_LOW);
#endif

		if (gen->audio_system == CW_AUDIO_NULL) {
			cw_null_write(gen, &tone);
		} else if (gen->audio_system == CW_AUDIO_CONSOLE) {
			cw_console_write(gen, &tone);
		} else {
			cw_gen_write_to_soundcard_internal(gen, &tone, is_empty_tone);
		}

		/*
		  When sending text from text input, the signal:
		   - allows client code to observe moment when state of tone
		     queue is "low/critical"; client code then can add more
		     characters to the queue; the observation is done using
		     cw_tq_wait_for_level_internal();

		   - allows client code to observe any dequeue event
                     by waiting for signal in
                     cw_tq_wait_for_tone_internal();
		*/

		//fprintf(stderr, "libcw/tq:       sending signal on dequeue, target thread id = %ld\n", gen->client.thread_id);

		pthread_mutex_lock(&gen->tq->wait_mutex);
		/* There may be many listeners, so use broadcast(). */
		pthread_cond_broadcast(&gen->tq->wait_var);
		pthread_mutex_unlock(&gen->tq->wait_mutex);


#if 0           /* Original implementation using signals. */ /* This code has been disabled some time before 2017-01-19. */
		pthread_kill(gen->client.thread_id, SIGALRM);
#endif

		/* Generator may be used by iambic keyer to measure
		   periods of time (lengths of Mark and Space) - this
		   is achieved by enqueueing Marks and Spaces by keyer
		   in generator. A soundcard playing samples is
		   surprisingly good at measuring time intervals.

		   At this point the generator has finished generating
		   a tone of specified length. A duration of Mark or
		   Space has elapsed. Inform iambic keyer that the
		   tone it has enqueued has elapsed. The keyer may
		   want to change its state.

		   (Whether iambic keyer has enqueued any tones or
		   not, and whether it is waiting for the
		   notification, is a different story. We will let the
		   iambic keyer function called below to decide what
		   to do with the notification. If keyer is in idle
		   state, it will ignore the notification.)

		   Notice that this mechanism is needed only for
		   iambic keyer. Inner workings of straight key are
		   much more simple, the straight key doesn't need to
		   use generator as a timer. */
		if (!cw_key_ik_update_graph_state_internal(gen->key)) {
			/* just try again, once */
			usleep(1000);
			cw_key_ik_update_graph_state_internal(gen->key);
		}

#ifdef LIBCW_WITH_DEV
		cw_debug_ev (&cw_debug_object_ev, 0, tone.frequency ? CW_DEBUG_EVENT_TONE_LOW : CW_DEBUG_EVENT_TONE_HIGH);
#endif

	} /* while (gen->do_dequeue_and_generate) */

	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_INFO,
		      "libcw/gen: EXIT: generator stopped (gen->do_dequeue_and_generate = %d)", gen->do_dequeue_and_generate);

	/* Some functions in client thread may be waiting for the last
	   notification from the generator thread to continue/finalize
	   their business. Let's send that notification right before
	   exiting. */

	/* This small delay before sending the notification turns out
	   to be helpful.

	   TODO: this is one of most mysterious comments in this code
	   base. What was I thinking? */
	struct timespec req = { .tv_sec = 0, .tv_nsec = CW_NSECS_PER_SEC / 2 };
	cw_nanosleep_internal(&req);

	pthread_mutex_lock(&gen->tq->wait_mutex);
	/* There may be many listeners, so use broadcast(). */
	pthread_cond_broadcast(&gen->tq->wait_var);
	pthread_mutex_unlock(&gen->tq->wait_mutex);

#if 0   /* Original implementation using signals. */ /* This code has been disabled some time before 2017-01-19. */
	pthread_kill(gen->client.thread_id, SIGALRM);
#endif

	gen->thread.running = false;
	return NULL;
}




/**
   \brief Calculate a fragment of sine wave

   Calculate a fragment of sine wave, as many samples as can be fitted
   in generator buffer's subarea.

   The function calculates values of (gen->buffer_sub_stop - gen->buffer_sub_start + 1)
   samples and puts them into gen->buffer[], starting from
   gen->buffer[gen->buffer_sub_start].

   The function takes into account all state variables from gen,
   so initial phase of new fragment of sine wave in the buffer matches
   ending phase of a sine wave generated in previous call.

   \reviewed on 2017-01-24

   \param gen - generator that generates sine wave
   \param tone - generated tone

   \return number of calculated samples
*/
int cw_gen_calculate_sine_wave_internal(cw_gen_t * gen, cw_tone_t * tone)
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
	  calculated fragment of sine wave.
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

		tone->sample_iterator++;

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

   Actually "calculation" is a bit too big word. The function just
   makes a decision which of precalculated values to return. There are
   no complicated arithmetical calculations being made each time the
   function is called, so the execution time should be pretty small.

   The precalcuated values depend on some factors, so the values
   should be re-calculated each time these factors change. See
   cw_gen_set_tone_slope() for list of these factors.

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

   \reviewed on 2017-01-24

   \param gen - generator used to generate a sine wave
   \param tone - tone being generated

   \return value of a sample of sine wave, a non-negative number
*/
int cw_gen_calculate_amplitude_internal(cw_gen_t * gen, cw_tone_t const * tone)
{
#if 0   /* Blunt algorithm for calculating amplitude. For debug purposes only. */

	return tone->frequency ? gen->volume_abs : 0;

#else

	if (tone->frequency <= 0) {
		return 0;
	}


	int amplitude = 0;

	/* Every tone, regardless of slope mode (CW_SLOPE_MODE_*), has
	   three components. It has rising slope + plateau + falling
	   slope.

	   There can be four variants of rising and falling slope
	   length, just as there are four CW_SLOPE_MODE_* values.

	   There can be also tones with zero-length plateau, and there
	   can be also tones with zero-length slopes. */

	if (tone->sample_iterator < tone->rising_slope_n_samples) {
		/* Beginning of tone, rising slope. */
		int i = tone->sample_iterator;
		amplitude = gen->tone_slope.amplitudes[i];
		assert (amplitude >= 0);

	} else if (tone->sample_iterator >= tone->rising_slope_n_samples
		   && tone->sample_iterator < tone->n_samples - tone->falling_slope_n_samples) {

		/* Middle of tone, plateau, constant amplitude. */
		amplitude = gen->volume_abs;
		assert (amplitude >= 0);

	} else if (tone->sample_iterator >= tone->n_samples - tone->falling_slope_n_samples) {
		/* Falling slope. */
		int i = tone->n_samples - tone->sample_iterator - 1;
		assert (i >= 0);
		amplitude = gen->tone_slope.amplitudes[i];
		assert (amplitude >= 0);

	} else {
		cw_assert (0, "libcw/gen: ->sample_iterator out of bounds:\n"
			   "tone->sample_iterator: %d\n"
			   "tone->n_samples: %"PRId64"\n"
			   "tone->rising_slope_n_samples: %d\n"
			   "tone->falling_slope_n_samples: %d\n",
			   tone->sample_iterator,
			   tone->n_samples,
			   tone->rising_slope_n_samples,
			   tone->falling_slope_n_samples);
	}

	assert (amplitude >= 0);
	return amplitude;
#endif
}




/**
   \brief Set parameters of tones generated by generator

   Most of variables related to slope of tones is in tone data type,
   but there are still some variables that are generator-specific, as
   they are common for all tones.  This function sets two of these
   variables.


   A: If you pass to function conflicting values of \p slope_shape and
   \p slope_len, the function will return CW_FAILURE. These
   conflicting values are rectangular slope shape and larger than zero
   slope length. You just can't have rectangular slopes that have
   non-zero length.


   B: If you pass to function '-1' as value of both \p slope_shape and
   \p slope_len, the function won't change any of the related two
   generator's parameters.


   C1: If you pass to function '-1' as value of either \p slope_shape
   or \p slope_len, the function will attempt to set only this
   generator's parameter that is different than '-1'.

   C2: However, if selected slope shape is rectangular, function will
   set generator's slope length to zero, even if value of \p
   slope_len is '-1'.


   D: Notice that the function allows non-rectangular slope shape with
   zero length of the slopes. The slopes will be non-rectangular, but
   just unusually short.


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

   \reviewed on 2017-01-24

   \param gen - generator for which to set tone slope parameters
   \param slope_shape - shape of slope: linear, raised cosine, sine, rectangular
   \param slope_len - length of slope [microseconds]

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_tone_slope(cw_gen_t * gen, int slope_shape, int slope_len)
{
	assert (gen);

	/* Handle conflicting values of arguments. */
	if (slope_shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR
	    && slope_len > 0) {

		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw/gen: requested a rectangular slope shape, but also requested slope len > 0");

		return CW_FAILURE;
	}

	/* Assign new values from arguments. */
	if (slope_shape != -1) {
		gen->tone_slope.shape = slope_shape;
	}
	if (slope_len != -1) {
		gen->tone_slope.len = slope_len;
	}


	/* Override of slope length. */
	if (slope_shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR) {
		gen->tone_slope.len = 0;
	}


	int slope_n_samples = ((gen->sample_rate / 100) * gen->tone_slope.len) / 10000;
	cw_assert (slope_n_samples >= 0, "libcw/gen: negative slope_n_samples: %d", slope_n_samples);


	/* Reallocate the table of slope amplitudes only when necessary.

	   In practice the function will be called foremost when user
	   changes volume of tone (and then the function may be
	   called several times in a row if volume is changed in
	   steps). In such situation the size of amplitudes table
	   doesn't change. */

	if (gen->tone_slope.n_amplitudes != slope_n_samples) {

		 /* Remember that slope_n_samples may be zero. In that
		    case realloc() would equal to free(). We don't
		    want to have NULL ->amplitudes, so don't modify
		    ->amplitudes for zero-length slopes.  Since with
		    zero-length slopes we won't be referring to
		    ->amplitudes[], it is ok that the table will not
		    be up-to-date. */

		if (slope_n_samples > 0) {
			gen->tone_slope.amplitudes = realloc(gen->tone_slope.amplitudes, sizeof(float) * slope_n_samples);
			if (!gen->tone_slope.amplitudes) {
				cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
					      "libcw/gen: failed to realloc() table of slope amplitudes");
				return CW_FAILURE;
			}
		}

		gen->tone_slope.n_amplitudes = slope_n_samples;
	}

	cw_gen_recalculate_slopes_internal(gen);

	return CW_SUCCESS;
}




/**
   \brief Recalculate amplitudes of PCM samples that form tone's slopes

   \reviewed on 2017-01-24

   TODO: consider writing unit test code for the function.

   \param gen - generator
*/
void cw_gen_recalculate_slopes_internal(cw_gen_t * gen)
{
	/* The values in amplitudes[] change from zero to max (at
	   least for any sane slope shape), so naturally they can be
	   used in forming rising slope. However they can be used in
	   forming falling slope as well - just iterate the table from
	   end to beginning. */


	for (int i = 0; i < gen->tone_slope.n_amplitudes; i++) {

		if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_LINEAR) {
			gen->tone_slope.amplitudes[i] = 1.0 * gen->volume_abs * i / gen->tone_slope.n_amplitudes;

		} else if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_SINE) {
			float radian = i * (M_PI / 2.0) / gen->tone_slope.n_amplitudes;
			gen->tone_slope.amplitudes[i] = sin(radian) * gen->volume_abs;

		} else if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RAISED_COSINE) {
			float radian = i * M_PI / gen->tone_slope.n_amplitudes;
			gen->tone_slope.amplitudes[i] = (1 - ((1 + cos(radian)) / 2)) * gen->volume_abs;

		} else if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR) {
			/* CW_TONE_SLOPE_SHAPE_RECTANGULAR is covered
			   before entering this "for" loop. */
			cw_assert (0, "libcw/gen: we shouldn't be here, calculating rectangular slopes");

		} else {
			cw_assert (0, "libcw/gen: unsupported slope shape %d", gen->tone_slope.shape);
		}
	}

	return;
}




/**
   \brief Write tone to soundcard

   \reviewed on 2017-01-24

   \param gen
   \param tone - tone dequeued from queue (if dequeueing was successful); must always be non-NULL
   \param is_empty_tone - true if dequeueing was not successful (if no tone was dequeued), false otherwise

   \return 0
*/
int cw_gen_write_to_soundcard_internal(cw_gen_t * gen, cw_tone_t * tone, bool is_empty_tone)
{
	cw_assert (tone, "libcw/gen: 'tone' argument should always be non-NULL, even when dequeueing failed");

	if (is_empty_tone) {
		/* No valid tone dequeued from tone queue. 'tone'
		   argument doesn't represent a valid tone. We need
		   samples to complete filling buffer, but they have
		   to be empty samples. */
		cw_gen_empty_tone_calculate_samples_size_internal(gen, tone);
	} else {
		/* Valid tone dequeued from tone queue. Use it to
		   calculate samples in buffer. */
		cw_gen_tone_calculate_samples_size_internal(gen, tone);
	}
	/* After the calculations above, we can use 'tone' to generate
	   samples in the same way, regardless of state of tone queue
	   (regardless of what the tone queue returned in last call).

	   Simply look at tone's frequency and tone's samples count. */


	/* Total number of samples to write in a loop below. */
	int64_t samples_to_write = tone->n_samples;

#if 0   /* Debug code. */
	int n_loops = 0;
	const double n_loops_expected = floor(1.0 * samples_to_write / gen->buffer_n_samples); /* In reality number of loops executed is sometimes n_loops_expected, but mostly it's n_loops_expected+1. */
	fprintf(stderr, "libcw/gen: entering loop (~%.1g), tone->frequency = %d, buffer->n_samples = %d, samples_to_write = %d\n",
		n_loops_expected, tone->frequency, gen->buffer_n_samples, samples_to_write);

#endif

	// cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw/gen: %lld samples, %d us, %d Hz", tone->n_samples, tone->len, gen->frequency);
	while (samples_to_write > 0) {

		int64_t free_space = gen->buffer_n_samples - gen->buffer_sub_start;
		if (samples_to_write > free_space) {
			/* There will be some tone samples left for
			   next iteration of this loop.  But buffer in
			   this iteration will be ready to be pushed
			   to audio sink. */
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
			   buffer. We can't send an under-filled buffer to
			   audio sink. We will have to get more
			   samples to fill the buffer completely. */
			gen->buffer_sub_stop = gen->buffer_sub_start + samples_to_write - 1;
		}

		/* How many samples of audio buffer's subarea will be
		   calculated in a given cycle of "calculate sine
		   wave" code, i.e. in current iteration of the 'while' loop? */
		const int buffer_sub_n_samples = gen->buffer_sub_stop - gen->buffer_sub_start + 1;


#if 0           /* Debug code. */

		fprintf(stderr, "libcw/gen:        loop #%d, buffer_sub_n_samples = %d\n", ++n_loops, buffer_sub_n_samples);
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
			      "libcw/gen: sub start: %d, sub stop: %d, sub size: %d / %d", gen->buffer_sub_start, gen->buffer_sub_stop, buffer_sub_n_samples, samples_to_write);
#endif

		const int calculated = cw_gen_calculate_sine_wave_internal(gen, tone);
		cw_assert (calculated == buffer_sub_n_samples, "libcw/gen: calculated wrong number of samples: %d != %d", calculated, buffer_sub_n_samples);

		if (gen->buffer_sub_stop == gen->buffer_n_samples - 1) {

			/* We have a buffer full of samples. The
			   buffer is ready to be pushed to audio
			   sink. */
			gen->write(gen);
#if CW_DEV_RAW_SINK
			cw_dev_debug_raw_sink_write_internal(gen);
#endif
			gen->buffer_sub_start = 0;
			gen->buffer_sub_stop = 0;
		} else {
			/* #needmoresamples
			   There is still some space left in the
			   buffer, go fetch new tone from tone
			   queue. */

			gen->buffer_sub_start = gen->buffer_sub_stop + 1;

			cw_assert (gen->buffer_sub_start <= gen->buffer_n_samples - 1, "libcw/gen: sub start out of range: sub start = %d, buffer n samples = %d", gen->buffer_sub_start, gen->buffer_n_samples);
		}

		samples_to_write -= buffer_sub_n_samples;

#if 0           /* Debug code. */

		if (samples_to_write < 0) {
			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw/gen: samples left = %d", samples_to_write);
		}
#endif

	} /* while (samples_to_write > 0) { */

#if 0   /* Debug code. */
	fprintf(stderr, "libcw/gen: left loop, %d / %.1f loops, samples left = %d\n", n_loops, n_loops_expected, (int) samples_to_write);
#endif

	return 0;
}




/**
   \brief Construct empty tone with correct/needed values of samples count

   The function sets values tone->..._n_samples fields of empty \p
   tone based on information from \p gen (i.e. looking on how many
   samples of silence need to be "created").  The sample count values
   are set in a way that allows filling remainder of generator's
   buffer with silence.

   After this point tone length should not be used - it's the samples count that is correct.

   \reviewed on 2017-01-22

   \param gen
   \param tone - tone for which to calculate samples count.
*/
void cw_gen_empty_tone_calculate_samples_size_internal(cw_gen_t const * gen, cw_tone_t * tone)
{
	/* All tones have been already dequeued from tone queue.

	   \p tone does not represent a valid tone to generate. At
	   first sight there is no need to write anything to
	   soundcard. But...

	   It may happen that during previous call to the function
	   there were too few samples in a tone to completely fill a
	   buffer (see #needmoresamples tag below).

	   We need to fill the buffer until it is full and ready to be
	   sent to audio sink.

	   Since there are no new tones for which we could generate
	   samples, we need to generate silence samples.

	   Padding the buffer with silence seems to be a good idea (it
	   will work regardless of value (Mark/Space) of last valid
	   tone). We just need to know how many samples of the silence
	   to produce.

	   Number of these samples will be stored in
	   samples_to_write. */

	/* We don't have a valid tone, so let's construct a fake one
	   for purposes of padding. */

	/* Required length of padding space is from end of last buffer
	   subarea to end of buffer. */
	tone->n_samples = gen->buffer_n_samples - (gen->buffer_sub_stop + 1);;

	tone->len = 0;         /* This value matters no more, because now we only deal with samples. */
	tone->frequency = 0;   /* This fake tone is a piece of silence. */

	/* The silence tone used for padding doesn't require any
	   slopes. A slope falling to silence has been already
	   provided by last non-fake and non-silent tone. */
	tone->slope_mode = CW_SLOPE_MODE_NO_SLOPES;
	tone->rising_slope_n_samples = 0;
	tone->falling_slope_n_samples = 0;

	tone->sample_iterator = 0;

	//fprintf(stderr, "++++ length of padding silence = %d [samples]\n", tone->n_samples);

	return;
}




/**
   \brief Recalculate non-empty tone parameters from microseconds into samples

   The function sets tone->..._n_samples fields of non-empty \p tone
   based on other information from \p tone and from \p gen.

   After this point tone length should not be used - it's the samples count that is correct.

   \reviewed on 2017-01-22

   \param gen
   \param tone - tone for which to calculate samples count.
*/
void cw_gen_tone_calculate_samples_size_internal(cw_gen_t const * gen, cw_tone_t * tone)
{

	/* 100 * 10000 = 1.000.000 usecs per second. */
	tone->n_samples = gen->sample_rate / 100;
	tone->n_samples *= tone->len;
	tone->n_samples /= 10000;

	//fprintf(stderr, "libcw/gen: length of regular tone = %d [samples]\n", tone->n_samples);

	/* Length of a single slope (rising or falling). */
	int slope_n_samples = gen->sample_rate / 100;
	slope_n_samples *= gen->tone_slope.len;
	slope_n_samples /= 10000;

	if (tone->slope_mode == CW_SLOPE_MODE_RISING_SLOPE) {
		tone->rising_slope_n_samples = slope_n_samples;
		tone->falling_slope_n_samples = 0;

	} else if (tone->slope_mode == CW_SLOPE_MODE_FALLING_SLOPE) {
		tone->rising_slope_n_samples = 0;
		tone->falling_slope_n_samples = slope_n_samples;

	} else if (tone->slope_mode == CW_SLOPE_MODE_STANDARD_SLOPES) {
		tone->rising_slope_n_samples = slope_n_samples;
		tone->falling_slope_n_samples = slope_n_samples;

	} else if (tone->slope_mode == CW_SLOPE_MODE_NO_SLOPES) {
		tone->rising_slope_n_samples = 0;
		tone->falling_slope_n_samples = 0;

	} else {
		cw_assert (0, "libcw/gen: unknown tone slope mode %d", tone->slope_mode);
	}

	tone->sample_iterator = 0;

	return;
}




/**
   \brief Set sending speed of generator

   See libcw2.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal value
   of send speed.

   \errno EINVAL - \p new_value is out of range.

   \reviewed on 2017-01-21

   testedin::test_cw_gen_parameter_getters_setters()

   \param gen - generator for which to set the speed
   \param new_value - new value of send speed to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_speed(cw_gen_t * gen, int new_value)
{
	if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != gen->send_speed) {
		gen->send_speed = new_value;

		/* Changes of send speed require resynchronization. */
		gen->parameters_in_sync = false;
		cw_gen_sync_parameters_internal(gen);
	}

	return CW_SUCCESS;
}




/**
   \brief Set frequency of generator

   Set frequency of sound wave generated by generator.
   See libcw2.h/CW_FREQUENCY_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of frequency.

   \errno EINVAL - \p new_value is out of range.

   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-01-21

   \param gen - generator for which to set new frequency
   \param new_value - new value of frequency to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_frequency(cw_gen_t * gen, int new_value)
{
	if (new_value < CW_FREQUENCY_MIN || new_value > CW_FREQUENCY_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	} else {
		gen->frequency = new_value;
		return CW_SUCCESS;
	}
}




/**
   \brief Set volume of generator

   Set volume of sound wave generated by generator.
   See libcw2.h/CW_VOLUME_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of volume.

   Note that volume settings are not fully possible for the console speaker.
   In this case, volume settings greater than zero indicate console speaker
   sound is on, and setting volume to zero will turn off console speaker
   sound.

   \errno EINVAL - \p new_value is out of range.

   testedin::test_cw_gen_volume_functions()
   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-01-21

   \param gen - generator for which to set a volume level
   \param new_value - new value of volume to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_volume(cw_gen_t * gen, int new_value)
{
	if (new_value < CW_VOLUME_MIN || new_value > CW_VOLUME_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	} else {
		gen->volume_percent = new_value;
		gen->volume_abs = (gen->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;

		cw_gen_set_tone_slope(gen, -1, -1);

		return CW_SUCCESS;
	}
}




/**
   \brief Set sending gap of generator

   See libcw2.h/CW_GAP_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of gap.

   \errno EINVAL - \p new_value is out of range.

   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-01-21

   \param gen - generator for which to set gap
   \param new_value - new value of gap to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_gap(cw_gen_t * gen, int new_value)
{
	if (new_value < CW_GAP_MIN || new_value > CW_GAP_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != gen->gap) {
		gen->gap = new_value;
		/* Changes of gap require resynchronization. */
		gen->parameters_in_sync = false;
		cw_gen_sync_parameters_internal(gen);
	}

	return CW_SUCCESS;
}




/**
   \brief Set sending weighting for generator

   See libcw2.h/CW_WEIGHTING_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of weighting.

   \errno EINVAL - \p new_value is out of range.

   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-01-21

   \param gen - generator for which to set new weighting
   \param new_value - new value of weighting to be assigned for generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_weighting(cw_gen_t * gen, int new_value)
{
	if (new_value < CW_WEIGHTING_MIN || new_value > CW_WEIGHTING_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != gen->weighting) {
		gen->weighting = new_value;

		/* Changes of weighting require resynchronization. */
		gen->parameters_in_sync = false;
		cw_gen_sync_parameters_internal(gen);
	}

	return CW_SUCCESS;
}




/**
   \brief Get sending speed from generator

   Returned value is in range CW_SPEED_MIN-CW_SPEED_MAX [wpm].

   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-01-21

   \param gen - generator from which to get the parameter

   \return current value of the generator's send speed
*/
int cw_gen_get_speed(cw_gen_t const * gen)
{
	return gen->send_speed;
}




/**
   \brief Get frequency from generator

   Function returns "frequency" parameter of generator,
   even if the generator is stopped, or volume of generated sound is zero.

   Returned value is in range CW_FREQUENCY_MIN-CW_FREQUENCY_MAX [Hz].

   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-01-21

   \param gen - generator from which to get the parameter

   \return current value of generator's frequency
*/
int cw_gen_get_frequency(cw_gen_t const * gen)
{
	return gen->frequency;
}




/**
   \brief Get sound volume from generator

   Function returns "volume" parameter of generator, even if the
   generator is stopped.  Returned value is in range
   CW_VOLUME_MIN-CW_VOLUME_MAX [%].

   testedin::test_cw_gen_volume_functions()
   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-01-21

   \param gen - generator from which to get the parameter

   \return current value of generator's sound volume
*/
int cw_gen_get_volume(cw_gen_t const * gen)
{
	return gen->volume_percent;
}




/**
   \brief Get sending gap from generator

   Returned value is in range CW_GAP_MIN-CW_GAP_MAX.

   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-01-21

   \param gen - generator from which to get the parameter

   \return current value of generator's sending gap
*/
int cw_gen_get_gap(cw_gen_t const * gen)
{
	return gen->gap;
}




/**
   \brief Get sending weighting from generator

   Returned value is in range CW_WEIGHTING_MIN-CW_WEIGHTING_MAX.

   testedin::test_cw_gen_parameter_getters_setters()

   \reviewed on 2017-01-21

   \param gen - generator from which to get the parameter

   \return current value of generator's sending weighting
*/
int cw_gen_get_weighting(cw_gen_t const * gen)
{
	return gen->weighting;
}




/**
   \brief Get timing parameters for sending

   Return the low-level timing parameters calculated from the speed,
   gap, tolerance, and weighting set.  Units of returned parameter
   values are microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   testedin::test_cw_gen_get_timing_parameters_internal()

   \reviewed on 2017-01-21

   \param gen
   \param dot_len
   \param dash_len
   \param eom_space_len
   \param eoc_space_len
   \param eow_space_len
   \param additional_space_len
   \param adjustment_space_len
*/
void cw_gen_get_timing_parameters_internal(cw_gen_t * gen,
					   int * dot_len,
					   int * dash_len,
					   int * eom_space_len,
					   int * eoc_space_len,
					   int * eow_space_len,
					   int * additional_space_len, int * adjustment_space_len)
{
	cw_gen_sync_parameters_internal(gen);

	if (dot_len)   *dot_len = gen->dot_len;
	if (dash_len)  *dash_len = gen->dash_len;

	if (eom_space_len)   *eom_space_len = gen->eom_space_len;
	if (eoc_space_len)   *eoc_space_len = gen->eoc_space_len;
	if (eow_space_len)   *eow_space_len = gen->eow_space_len;

	if (additional_space_len)    *additional_space_len = gen->additional_space_len;
	if (adjustment_space_len)    *adjustment_space_len = gen->adjustment_space_len;

	return;
}




/**
   \brief Enqueue a mark (Dot or Dash)

   Low level primitive to enqueue a tone for mark of the given type, followed
   by the standard inter-mark space.

   \errno EINVAL - \p mark argument is invalid.

   \errno EAGAIN - generator's tone queue is full.

   testedin::test_cw_gen_enqueue_primitives()

   \reviewed on 2017-01-21

   \param gen - generator to be used to enqueue a mark and inter-mark space
   \param mark - mark to send: Dot (CW_DOT_REPRESENTATION) or Dash (CW_DASH_REPRESENTATION)

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_gen_enqueue_mark_internal(cw_gen_t * gen, char mark)
{
	int status;

	/* Synchronize low-level timings if required. */
	cw_gen_sync_parameters_internal(gen);
	/* TODO: do we need to synchronize here receiver as well? */

	/* Send either a dot or a dash mark, depending on representation. */
	if (mark == CW_DOT_REPRESENTATION) {
		cw_tone_t tone;
		CW_TONE_INIT(&tone, gen->frequency, gen->dot_len, CW_SLOPE_MODE_STANDARD_SLOPES);
		status = cw_tq_enqueue_internal(gen->tq, &tone);
	} else if (mark == CW_DASH_REPRESENTATION) {
		cw_tone_t tone;
		CW_TONE_INIT(&tone, gen->frequency, gen->dash_len, CW_SLOPE_MODE_STANDARD_SLOPES);
		status = cw_tq_enqueue_internal(gen->tq, &tone);
	} else {
		errno = EINVAL;
		status = CW_FAILURE;
	}

	if (CW_SUCCESS != status) {
		return CW_FAILURE;
	}

	/* Send the inter-mark space. */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, gen->eom_space_len, CW_SLOPE_MODE_NO_SLOPES);
	if (CW_SUCCESS != cw_tq_enqueue_internal(gen->tq, &tone)) {
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}




/**
   \brief Enqueue inter-character space

   The function enqueues space of length 2 Units. The function is
   intended to be used after inter-mark space has already been enqueued.

   In such situation standard inter-mark space (one Unit) and two
   Units enqueued by this function form a full standard
   inter-character space (three Units).

   Inter-character adjustment space is added at the end.

   testedin::test_cw_gen_enqueue_primitives()

   \reviewed on 2017-01-21

   \param gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_eoc_space_internal(cw_gen_t * gen)
{
	/* Synchronize low-level timing parameters. */
	cw_gen_sync_parameters_internal(gen);

	/* Enqueue standard inter-character space, plus any additional inter-character gap. */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, gen->eoc_space_len + gen->additional_space_len, CW_SLOPE_MODE_NO_SLOPES);
	return cw_tq_enqueue_internal(gen->tq, &tone);
}




/**
   \brief Enqueue space character (' ') in generator, to be sent using Morse code

   The function should be used to enqueue a regular ' ' character.

   The function enqueues space of length 5 Units. The function is
   intended to be used after inter-mark space and inter-character
   space have already been enqueued.

   In such situation standard inter-mark space (one Unit) and
   inter-character space (two Units) and regular space (five units)
   form a full standard inter-word space (seven Units).

   Inter-word adjustment space is added at the end.

   testedin::test_cw_gen_enqueue_primitives()

   \reviewed on 2017-01-21

   \param gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_eow_space_internal(cw_gen_t * gen)
{
	/* Synchronize low-level timing parameters. */
	cw_gen_sync_parameters_internal(gen);

	/* Send silence for the word delay period, plus any adjustment
	   that may be needed at end of word. Make it in two tones,
	   and here is why.

	   Let's say that 'tone queue low watermark' is one element
	   (i.e. one tone).

	   In order for tone queue to recognize that a 'low tone
	   queue' callback needs to be called, the level in tq needs
	   to drop from 2 to 1.

	   Almost every queued character guarantees that there will be
	   at least two tones, e.g for 'E' it is dash + following
	   space. But what about a ' ' character?

	   If we enqueue ' ' character as single tone, there is only one
	   tone in tone queue, and the tone queue manager can't
	   recognize when the level drops from 2 to 1 (and thus the
	   'low level' callback won't be called).

	   If we enqueue ' ' character as two separate tones (as we do
	   this in this function), the tone queue manager can
	   recognize level dropping from 2 to 1. Then the passing of
	   critical level can be noticed, and "low level" callback can
	   be called.

	   BUT: Sometimes the first tone is dequeued before/during the
	   second one is enqueued, and we can't recognize 2->1 event.

	   So, to be super-sure that there is a recognizable event of
	   passing tone queue level from 2 to 1, we split the inter-word
	   space into N parts and enqueue them. This way we have N + 1
	   tones per space, and client applications that rely on low
	   level threshold == 1 can correctly work when enqueueing
	   spaces.

	   At 60 wpm length of gen->eow_space_len is 100000 [us], so
	   it's large enough to safely divide it by small integer
	   value. */

	int enqueued = 0;

	cw_tone_t tone;
#if 0
	/* This section is incorrect. Enable this section only for
	   tests.  This section "implements" a bug that was present in
	   libcw until version 6.4.1 and that is now tested by
	   src/libcw/tests/libcw_test_tq_short_space.c */
	const int n = 1; /* No division. Old situation causing an error in
		      client applications. */
#else
	const int n = 2; /* "small integer value" - used to have more tones per eow space. */
#endif
	CW_TONE_INIT(&tone, 0, gen->eow_space_len / n, CW_SLOPE_MODE_NO_SLOPES);
	for (int i = 0; i < n; i++) {
		if (CW_SUCCESS != cw_tq_enqueue_internal(gen->tq, &tone)) {
			return CW_FAILURE;
		}
		enqueued++;
	}

	CW_TONE_INIT(&tone, 0, gen->adjustment_space_len, CW_SLOPE_MODE_NO_SLOPES);
	if (CW_SUCCESS != cw_tq_enqueue_internal(gen->tq, &tone)) {
		return CW_FAILURE;
	}
	enqueued++;

	cw_debug_msg (&cw_debug_object, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw/gen: enqueued %d tones per iw space, tq len = %zu", enqueued, cw_tq_length_internal(gen->tq));

	return CW_SUCCESS;
}




/**
   \brief Enqueue the given representation in generator, to be sent using Morse code

   Function enqueues given \p representation using given \p generator.
   *Every* mark from the \p representation is followed by a standard
   inter-mark space.

   Representation is not validated by this function.

   _partial_ in function's name means that the inter-character space is
   not appended at the end of Marks and Spaces enqueued in generator
   (but the last inter-mark space is).

   \errno EAGAIN - there is not enough space in tone queue to enqueue
   \p representation.

   testedin::test_cw_gen_enqueue_representations()

   \reviewed on 2017-01-21

   \param gen - generator used to enqueue the representation
   \param representation - representation to enqueue

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_gen_enqueue_representation_partial_internal(cw_gen_t * gen, char const * representation)
{
	/* Before we let this representation loose on tone generation,
	   we'd really like to know that all of its tones will get queued
	   up successfully.  The right way to do this is to calculate the
	   number of tones in our representation, then check that the space
	   exists in the tone queue. However, since the queue is comfortably
	   long, we can get away with just looking for a high water mark.  */
	if (cw_tq_length_internal(gen->tq) >= gen->tq->high_water_mark) {
		errno = EAGAIN;
		return CW_FAILURE;
	}

	/* Enqueue the marks. Every mark is followed by inter-mark
	   space. */
	for (int i = 0; representation[i] != '\0'; i++) {
		if (!cw_gen_enqueue_mark_internal(gen, representation[i])) {
			return CW_FAILURE;
		}
	}

	/* No inter-character space added here. */

	return CW_SUCCESS;
}




/**
   \brief Enqueue a given valid ASCII character in generator, to be sent using Morse code

   _valid_character_ in function's name means that the function
   expects the character \p c to be valid (\p c should be validated by
   caller before passing it to the function).

   _partial_ in function's name means that the inter-character space is
   not appended at the end of Marks and Spaces enqueued in generator
   (but the last inter-mark space is).

   \errno ENOENT - character \p c is not a recognized character.

   \reviewed on 2017-01-21

   \param gen - generator to be used to enqueue character
   \param c - character to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_valid_character_partial_internal(cw_gen_t * gen, char c)
{
	if (!gen) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw/gen: no generator available");
		return CW_FAILURE;
	}

	/* ' ' character (i.e. regular space) is a special case. */
	if (c == ' ') {
		return cw_gen_enqueue_eow_space_internal(gen);
	}

	const char * r = cw_character_to_representation_internal(c);

	/* This shouldn't happen since we are in _valid_character_ function... */
	cw_assert (r, "libcw/gen: failed to find representation for character '%c'/%hhx", c, c);

	/* ... but fail gracefully anyway. */
	if (!r) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	if (!cw_gen_enqueue_representation_partial_internal(gen, r)) {
		return CW_FAILURE;
	}

	/* No inter-character space here. */

	return CW_SUCCESS;
}




/**
   \brief Enqueue a given valid ASCII character in generator, to be sent using Morse code

   After enqueueing last Mark (Dot or Dash) comprising a character, an
   inter-mark space is enqueued.  Inter-character space is enqueued
   after that last inter-mark space.

   _valid_character_ in function's name means that the function
   expects the character \p c to be valid (\p c should be validated by
   caller before passing it to the function).

   \errno ENOENT - character \p c is not a recognized character.

   \reviewed on 2017-01-20

   \param gen - generator to be used to enqueue character
   \param c - character to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_valid_character_internal(cw_gen_t * gen, char c)
{
	if (!cw_gen_enqueue_valid_character_partial_internal(gen, c)) {
		return CW_FAILURE;
	}

	if (!cw_gen_enqueue_eoc_space_internal(gen)) {
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}




/**
   \brief Enqueue a given ASCII character in generator, to be sent using Morse code

   Inter-mark + inter-character delay is appended at the end of
   enqueued Marks.

   \errno ENOENT - the given character \p c is not a valid Morse
   character.

   \errno EBUSY - generator's audio sink or keying system is busy.

   \errno EAGAIN - generator's tone queue is full, or there is
   insufficient space to queue the tones for the character.

   This routine returns as soon as the character and trailing spaces
   (inter-mark and inter-character spaces) have been successfully
   queued for sending/playing by the generator, without waiting for
   generator to even start playing the character.  The actual sending
   happens in background processing. See cw_gen_wait_for_tone() and
   cw_gen_wait_for_queue_level() for ways to check the progress of
   sending.

   testedin::test_cw_gen_enqueue_character_and_string()

   \reviewed on 2017-01-20

   \param gen - generator to enqueue the character to
   \param c - character to enqueue in generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_character(cw_gen_t * gen, char c)
{
	if (!cw_character_is_valid(c)) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	/* This function adds eoc space at the end of character. */
	if (!cw_gen_enqueue_valid_character_internal(gen, c)) {
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}




/**
   \brief Enqueue a given ASCII character in generator, to be sent using Morse code

   "partial" means that the inter-character space is not appended at
   the end of Marks and Spaces enqueued in generator (but the last
   inter-mark space is). This enables the formation of combination
   characters by client code.

   This routine returns as soon as the character has been successfully
   queued for sending/playing by the generator, without waiting for
   generator to even start playing the character. The actual sending
   happens in background processing.  See cw_gen_wait_for_tone() and
   cw_gen_wait_for_queue() for ways to check the progress of sending.

   \reviewed on 2017-01-20

   \errno ENOENT - given character \p c is not a valid Morse
   character.

   \errno EBUSY - generator's audio sink or keying system is busy.

   \errno EAGAIN - generator's tone queue is full, or there is
   insufficient space to queue the tones for the character.

   \param gen - generator to use
   \param c - character to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_character_partial(cw_gen_t * gen, char c)
{
	if (!cw_character_is_valid(c)) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	if (!cw_gen_enqueue_valid_character_partial_internal(gen, c)) {
		return CW_FAILURE;
	}

	/* _partial(): don't enqueue eoc space. */

	return CW_SUCCESS;
}




/**
   \brief Enqueue a given ASCII string in generator, to be sent using Morse code

   For safety, clients can ensure the tone queue is empty before
   queueing a string, or use cw_gen_enqueue_character() if they
   need finer control.

   This routine returns as soon as the string has been successfully
   queued for sending/playing by the generator, without waiting for
   generator to even start playing the string. The actual
   playing/sending happens in background. See cw_gen_wait_for_tone()
   and cw_gen_wait_for_queue() for ways to check the progress of
   sending.

   testedin::test_cw_gen_enqueue_character_and_string()

   \errno ENOENT - \p string argument is invalid (one or more
   characters in the string is not a valid Morse character). No tones
   from such string are going to be enqueued.

   \errno EBUSY  - generator's audio sink or keying system is busy

   \errno EAGAIN - generator's tone queue is full or the tone queue
   is likely to run out of space part way through queueing the string.
   However, an indeterminate number of the characters from the string
   will have already been queued.

   \reviewed on 2017-01-20

   \param gen - generator to use
   \param string - string to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_string(cw_gen_t * gen, char const * string)
{
	/* Check that the string is composed of valid characters. */
	if (!cw_string_is_valid(string)) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	/* Send every character in the string. */
	for (int i = 0; string[i] != '\0'; i++) {

		/* This function adds eoc space at the end of character. */
		if (!cw_gen_enqueue_valid_character_internal(gen, string[i])) {
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}




/**
   \brief Reset generator's essential parameters to their initial values

   You need to call cw_gen_sync_parameters_internal() after call to this function.

   \reviewed on 2017-01-20

   \param gen
*/
void cw_gen_reset_parameters_internal(cw_gen_t * gen)
{
	cw_assert (gen, "libcw/gen: generator is NULL");

	gen->send_speed = CW_SPEED_INITIAL;
	gen->frequency = CW_FREQUENCY_INITIAL;
	gen->volume_percent = CW_VOLUME_INITIAL;
	gen->volume_abs = (gen->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
	gen->gap = CW_GAP_INITIAL;
	gen->weighting = CW_WEIGHTING_INITIAL;

	gen->parameters_in_sync = false;

	return;

}




/**
   \brief Synchronize generator's low level timing parameters

   \param gen - generator
*/
void cw_gen_sync_parameters_internal(cw_gen_t * gen)
{
	cw_assert (gen, "libcw/gen: generator is NULL");

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
	gen->dot_len = unit_length + weighting_length;
	gen->dash_len = 3 * gen->dot_len;

	/* End-of-mark space length is one Unit, perhaps adjusted.
	   End-of-character space length is three Units total.
	   End-of-word space length is seven Units total.

	   WARNING: notice how the eoc and eow spaces are
	   calculated. They aren't full 3 units and 7 units. They are
	   2 units (which takes into account preceding eom space
	   length), and 5 units (which takes into account preceding
	   eom *and* eoc space length). So these two lengths are
	   *additional* ones, i.e. in addition to (already existing)
	   eom and/or eoc space.  Whether this is good or bad idea to
	   calculate them like this is a separate topic. Just be aware
	   of this fact.

	   The end-of-mark length is adjusted by 28/22 times
	   weighting length to keep PARIS calibration correctly
	   timed (PARIS has 22 full units, and 28 empty ones).
	   End-of-mark and end of character delays take
	   weightings into account. */
	gen->eom_space_len = unit_length - (28 * weighting_length) / 22;  /* End-of-mark space, a.k.a. regular inter-mark space. */
	gen->eoc_space_len = 3 * unit_length - gen->eom_space_len;
	gen->eow_space_len = 7 * unit_length - gen->eoc_space_len;
	gen->additional_space_len = gen->gap * unit_length;

	/* For "Farnsworth", there also needs to be an adjustment
	   delay added to the end of words, otherwise the rhythm is
	   lost on word end.
	   I don't know if there is an "official" value for this,
	   but 2.33 or so times the gap is the correctly scaled
	   value, and seems to sound okay.

	   Thanks to Michael D. Ivey <ivey@gweezlebur.com> for
	   identifying this in earlier versions of libcw. */
	gen->adjustment_space_len = (7 * gen->additional_space_len) / 3;

	cw_debug_msg (&cw_debug_object, CW_DEBUG_PARAMETERS, CW_DEBUG_INFO,
		      "libcw/gen: send usec timings <%d [wpm]>: dot: %d, dash: %d, %d, %d, %d, %d, %d",
		      gen->send_speed, gen->dot_len, gen->dash_len,
		      gen->eom_space_len, gen->eoc_space_len,
		      gen->eow_space_len, gen->additional_space_len, gen->adjustment_space_len);

	/* Generator parameters are now in sync. */
	gen->parameters_in_sync = true;

	return;
}




/**
   Helper function intended to hide details of tone queue and of
   enqueueing a tone from cw_key module.

   The function should be called only on "key down" (begin mark) event
   from hardware straight key.

   The function is called in very specific context, see cw_key module
   for details.

   \reviewed on 2017-01-20

   \param gen - generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_begin_mark_internal(cw_gen_t * gen)
{
	/* In case of straight key we don't know at all how long the
	   tone should be (we don't know for how long the straight key
	   will be closed).

	   Let's enqueue a beginning of mark (rising slope) +
	   "forever" (constant) tone. The constant tone will be generated
	   until key goes into CW_KEY_STATE_OPEN state. */

	cw_tone_t tone;
	CW_TONE_INIT(&tone, gen->frequency, gen->tone_slope.len, CW_SLOPE_MODE_RISING_SLOPE);
	int rv = cw_tq_enqueue_internal(gen->tq, &tone);

	if (rv != CW_SUCCESS) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_ERROR,
			      "libcw/gen: enqueue begin mark: failed to enqueue rising slope: '%s'", strerror(errno));
	}

	/* If there was an error during enqueue of rising slope of
	   mark, assume that it was a transient error, and proceed to
	   enqueueing forever tone. Only after we fail to enqueue the
	   "main" tone, we are allowed to return failure to caller. */
	CW_TONE_INIT(&tone, gen->frequency, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
	tone.is_forever = true;
	rv = cw_tq_enqueue_internal(gen->tq, &tone);

	if (rv != CW_SUCCESS) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_ERROR,
			      "libcw/gen: enqueue begin mark: failed to enqueue forever tone: '%s'", strerror(errno));
	}

	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      "libcw/gen: enqueue begin mark: tone queue len = %zu", cw_tq_length_internal(gen->tq));

	return rv;
}




/**
   Helper function intended to hide details of tone queue and of
   enqueueing a tone from cw_key module.

   The function should be called only on "key up" (begin space) event
   from hardware straight key.

   The function is called in very specific context, see cw_key module
   for details.

   \reviewed on 2017-01-20

   \param gen - generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_begin_space_internal(cw_gen_t * gen)
{
	if (gen->audio_system == CW_AUDIO_CONSOLE) {
		/* FIXME: I think that enqueueing tone is not just a
		   matter of generating it using generator, but also a
		   matter of timing events using generator. Enqueueing
		   tone here and dequeueing it later will be used to
		   control state of a key. How does enqueueing a
		   quantum tone influences the key state? */


		/* Generate just a bit of silence, just to switch
		   buzzer from generating a sound to being silent. */
		cw_tone_t tone;
		CW_TONE_INIT(&tone, 0, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
		return cw_tq_enqueue_internal(gen->tq, &tone);
	} else {
		/* For soundcards a falling slope with volume from max
		   to zero should be enough, but... */
		cw_tone_t tone;
		CW_TONE_INIT(&tone, gen->frequency, gen->tone_slope.len, CW_SLOPE_MODE_FALLING_SLOPE);
		int rv = cw_tq_enqueue_internal(gen->tq, &tone);

		if (rv == CW_SUCCESS) {
			/* ... but on some occasions, on some
			   platforms, some sound systems may need to
			   constantly generate "silent" tone. These four
			   lines of code are just for them.

			   FIXME: what occasions? what platforms? what sound systems?

			   It would be better to avoid queueing silent
			   "forever" tone because this increases CPU
			   usage. It would be better to simply not to
			   queue any new tones after "falling slope"
			   tone. Silence after the last falling slope
			   would simply last on itself until there is
			   new tone in queue to dequeue. */
			CW_TONE_INIT(&tone, 0, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
			tone.is_forever = true;
			rv = cw_tq_enqueue_internal(gen->tq, &tone);
		}

		return rv;
	}
}




/**
   Helper function intended to hide details of tone queue and of
   enqueueing a tone from cw_key module.

   The function should be called on hardware key events only. Since we
   enqueue symbols, we know that they have limited, specified
   length. This means that the function should be called for events
   from iambic keyer.

   'Partial' means without any end-of-mark spaces.

   The function is called in very specific context, see cw_key module
   for details.

   \reviewed on 2017-01-20

   \errno EINVAL - invalid values of tone parameters
   \errno EAGAIN - tone queue is full

   \param gen - generator
   \param symbol - symbol to enqueue (Space/Dot/Dash)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_partial_symbol_internal(cw_gen_t * gen, char symbol)
{
	cw_tone_t tone = { 0 };

	if (symbol == CW_DOT_REPRESENTATION) {
		CW_TONE_INIT(&tone, gen->frequency, gen->dot_len, CW_SLOPE_MODE_STANDARD_SLOPES);

	} else if (symbol == CW_DASH_REPRESENTATION) {
		CW_TONE_INIT(&tone, gen->frequency, gen->dash_len, CW_SLOPE_MODE_STANDARD_SLOPES);

	} else if (symbol == CW_SYMBOL_SPACE) {
		CW_TONE_INIT(&tone, 0, gen->eom_space_len, CW_SLOPE_MODE_NO_SLOPES);

	} else {
		cw_assert (0, "libcw/gen: unknown key symbol '%d'", symbol);
	}

	return cw_tq_enqueue_internal(gen->tq, &tone);
}




/**
   \brief Wait for generator's tone queue to drain until only as many tones as given in \p level remain queued

   This function is for use by programs that want to optimize
   themselves to avoid the cleanup that happens when generator's tone
   queue drains completely. Such programs have a short time in which
   to add more tones to the queue.

   The function returns when queue's level is equal or lower than \p
   level.  If at the time of function call the level of queue is
   already equal or lower than \p level, function returns immediately.

   \reviewed on 2017-01-20

   \param gen - generator on which to wait
   \param level - level in queue, at which to return

   \return CW_SUCCESS
*/
int cw_gen_wait_for_queue_level(cw_gen_t * gen, size_t level)
{
	return cw_tq_wait_for_level_internal(gen->tq, level);
}




/**
   \brief Cancel all pending queued tones in a generator, and return to silence

   If there is a tone in progress, the function will wait until this
   last one has completed, then silence the tones.

   \param gen - generator to flush
*/
void cw_gen_flush_queue(cw_gen_t * gen)
{
	/* This function locks and unlocks mutex. */
	cw_tq_flush_internal(gen->tq);

	/* Force silence on the speaker anyway, and stop any background
	   soundcard tone generation. */
	cw_gen_silence_internal(gen);

	return;
}




/**
   \brief Return char string with generator's audio device path/name

   Returned pointer is owned by library.

   \reviewed on 2017-01-20

   \param gen

   \return char string with generator's audio device path/name
*/
char const * cw_gen_get_audio_device(cw_gen_t const * gen)
{
	cw_assert (gen, "libcw/gen: generator is NULL");
	return gen->audio_device;
}




/**
   \brief Get id of audio system used by given generator (one of enum cw_audio_system values)

   You can use cw_get_audio_system_label() to get string corresponding
   to value returned by this function.

   \reviewed on 2017-01-20

   \param gen - generator from which to get audio system

   \return audio system's id
*/
int cw_gen_get_audio_system(cw_gen_t const * gen)
{
	cw_assert (gen, "libcw/gen: generator is NULL");
	return gen->audio_system;
}




/**
   \reviewed on 2017-01-20
*/
size_t cw_gen_get_queue_length(cw_gen_t const * gen)
{
	return cw_tq_length_internal(gen->tq);
}




/**
   \reviewed on 2017-01-20
*/
int cw_gen_register_low_level_callback(cw_gen_t * gen, cw_queue_low_callback_t callback_func, void * callback_arg, size_t level)
{
	return cw_tq_register_low_level_callback_internal(gen->tq, callback_func, callback_arg, level);
}




int cw_gen_wait_for_tone(cw_gen_t * gen)
{
	return cw_tq_wait_for_tone_internal(gen->tq);
}




/**
   \reviewed on 2017-01-20
*/
bool cw_gen_is_queue_full(cw_gen_t const * gen)
{
	return cw_tq_is_full_internal(gen->tq);
}




/* *** Unit tests *** */





#ifdef LIBCW_UNIT_TESTS


#include "libcw_test.h"





/**
   tests::cw_gen_new()
   tests::cw_gen_delete()
*/
unsigned int test_cw_gen_new_delete(__attribute__((unused)) cw_gen_t * unused, cw_test_stats_t * stats)
{
	/* Arbitrary number of calls to a set of tested functions. */
	int max = 100;

	bool failure = true;
	int n = 0;

	/* new() + delete() */
	for (int i = 0; i < max; i++) {
		fprintf(stderr, "libcw/gen:new/delete: generator test 1/4, loop #%d/%d\n", i, max);

		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		failure = (NULL == gen);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/delete: failed to initialize generator (loop #%d)", i);
			break;
		}

		/* Try to access some fields in cw_gen_t just to be sure that the gen has been allocated properly. */
		failure = (gen->buffer_sub_start != 0);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/delete: buffer_sub_start in new generator is not at zero");
			break;
		}

		gen->buffer_sub_stop = gen->buffer_sub_start + 10;
		failure = (gen->buffer_sub_stop != 10);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/delete: buffer_sub_stop didn't store correct new value");
			break;
		}

		failure = (gen->client.name != (char *) NULL);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/delete: initial value of generator's client name is not NULL");
			break;
		}

		failure = (gen->tq == NULL);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/delete: tone queue is NULL");
			break;
		}

		cw_gen_delete(&gen);
		failure = (gen != NULL);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/delete: delete() didn't set the pointer to NULL (loop #%d)", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw/gen:new/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	max = 5;

	/* new() + start() + delete() (skipping stop() on purpose). */
	for (int i = 0; i < max; i++) {
		fprintf(stderr, "libcw/gen:new/start/delete: generator test 2/4, loop #%d/%d\n", i, max);

		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		failure = (gen == NULL);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/start/delete: failed to initialize generator (loop #%d)", i);
			break;
		}

		failure = (CW_SUCCESS != cw_gen_start(gen));
		if (failure) {
			fprintf(out_file, "libcw/gen:new/start/delete: failed to start generator (loop #%d)", i);
			break;
		}

		cw_gen_delete(&gen);
		failure = (gen != NULL);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/start/delete: delete() didn't set the pointer to NULL (loop #%d)", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw/gen:new/start/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* new() + stop() + delete() (skipping start() on purpose). */
	fprintf(stderr, "libcw/gen:new/stop/delete: generator test 3/4\n");
	for (int i = 0; i < max; i++) {
		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		failure = (gen == NULL);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/stop/delete: failed to initialize generator (loop #%d)", i);
			break;
		}

		failure = (CW_SUCCESS != cw_gen_stop(gen));
		if (failure) {
			fprintf(out_file, "libcw/gen:new/stop/delete: failed to stop generator (loop #%d)", i);
			break;
		}

		cw_gen_delete(&gen);
		failure = (gen != NULL);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/stop/delete: delete() didn't set the pointer to NULL (loop #%d)", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw/gen:new/stop/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* new() + start() + stop() + delete() */
	for (int i = 0; i < max; i++) {
		fprintf(stderr, "libcw/gen:new/start/stop/delete: generator test 4/4, loop #%d/%d\n", i, max);

		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		failure = (gen == NULL);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/start/stop/delete: failed to initialize generator (loop #%d)", i);
			break;
		}

		int sub_max = max;

		for (int j = 0; j < sub_max; j++) {
			failure = (CW_SUCCESS != cw_gen_start(gen));
			if (failure) {
				fprintf(out_file, "libcw/gen:new/start/stop/delete: failed to start generator (loop #%d-%d)", i, j);
				break;
			}

			failure = (CW_SUCCESS != cw_gen_stop(gen));
			if (failure) {
				fprintf(out_file, "libcw/gen:new/start/stop/delete: failed to stop generator (loop #%d-%d)", i, j);
				break;
			}
		}
		if (failure) {
			break;
		}

		cw_gen_delete(&gen);
		failure = (gen != NULL);
		if (failure) {
			fprintf(out_file, "libcw/gen:new/start/stop/delete: delete() didn't set the pointer to NULL (loop #%d)", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw/gen:new/start/stop/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	return 0;
}




unsigned int test_cw_gen_set_tone_slope(__attribute__((unused)) cw_gen_t * unused, cw_test_stats_t * stats)
{
	int audio_system = CW_AUDIO_NULL;
	bool failure = true;
	int n = 0;

	/* Test 0: test property of newly created generator. */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "libcw/gen:set slope: failed to initialize generator in test 0");

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_RAISED_COSINE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: initial shape (%d):", gen->tone_slope.shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != CW_AUDIO_SLOPE_LEN);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: initial length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_gen_delete(&gen);
	}



	/* Test A: pass conflicting arguments.

	   "A: If you pass to function conflicting values of \p
	   slope_shape and \p slope_len, the function will return
	   CW_FAILURE. These conflicting values are rectangular slope
	   shape and larger than zero slope length. You just can't
	   have rectangular slopes that have non-zero length." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "libcw/gen:set slope: failed to initialize generator in test A");

		failure = (CW_SUCCESS == cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 10));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: conflicting arguments:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_gen_delete(&gen);
	}



	/* Test B: pass '-1' as both arguments.

	   "B: If you pass to function '-1' as value of both \p
	   slope_shape and \p slope_len, the function won't change
	   any of the related two generator's parameters." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "libcw/gen:set slope: failed to initialize generator in test B");

		int shape_before = gen->tone_slope.shape;
		int len_before = gen->tone_slope.len;

		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, -1, -1));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: set tone slope -1 -1:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != shape_before);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope -1 -1: shape (%d / %d)", shape_before, gen->tone_slope.shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != len_before);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope -1 -1: len (%d / %d):", len_before, gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_gen_delete(&gen);
	}



	/* Test C1

	   "C1: If you pass to function '-1' as value of either \p
	   slope_shape or \p slope_len, the function will attempt to
	   set only this generator's parameter that is different than
	   '-1'." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "libcw/gen:set slope: failed to initialize generator in test C1");


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;

		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: N -1: initial shape (%d / %d):", gen->tone_slope.shape, expected_shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: N -1: initial length (%d / %d):", gen->tone_slope.len, expected_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_LINEAR;
		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, expected_shape, -1));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: N -1: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		/* At this point only slope shape should be updated. */
		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: N -1: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: N -1: preserved length:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Set only new slope length. */
		expected_len = 30;
		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, -1, expected_len));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: -1 N: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		/* At this point only slope length should be updated
		   (compared to previous function call). */
		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: -1 N: get (%d / %d):", gen->tone_slope.len, expected_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: -1 N: preserved shape:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		cw_gen_delete(&gen);
	}



	/* Test C2

	   "C2: However, if selected slope shape is rectangular,
	   function will set generator's slope length to zero, even if
	   value of \p slope_len is '-1'." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "libcw/gen:set slope: failed to initialize generator in test C2");


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;

		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: initial shape (%d / %d):", gen->tone_slope.shape, expected_shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: initial length (%d / %d):", gen->tone_slope.len, expected_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_RECTANGULAR;
		expected_len = 0; /* Even though we won't pass this to function, this is what we expect to get after this call. */
		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, expected_shape, -1));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: set rectangular:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* At this point slope shape AND slope length should
		   be updated (slope length is updated only because of
		   requested rectangular slope shape). */
		failure = (gen->tone_slope.shape != expected_shape);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: set rectangular: shape (%d/ %d):", gen->tone_slope.shape, expected_shape);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		failure = (gen->tone_slope.len != expected_len);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: set rectangular: length (%d / %d):", gen->tone_slope.len, expected_len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		cw_gen_delete(&gen);
	}



	/* Test D

	   "D: Notice that the function allows non-rectangular slope
	   shape with zero length of the slopes. The slopes will be
	   non-rectangular, but just unusually short." */
	{
		cw_gen_t * gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "libcw/gen:set slope: failed to initialize generator in test D");


		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_LINEAR, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: LINEAR 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_LINEAR);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: LINEAR 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: LINEAR 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: RAISED_COSINE 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_RAISED_COSINE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: RAISED_COSINE 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: RAISED_COSINE 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_SINE, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: SINE 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_SINE);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: SINE 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: SINE 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: RECTANGULAR 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_RECTANGULAR);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: RECTANGULAR 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: RECTANGULAR 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		failure = (CW_SUCCESS != cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_LINEAR, 0));
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: LINEAR 0: set:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.shape != CW_TONE_SLOPE_SHAPE_LINEAR);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: LINEAR 0: get:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		failure = (gen->tone_slope.len != 0);
		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen:set slope: LINEAR 0: length (%d):", gen->tone_slope.len);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		cw_gen_delete(&gen);
	}


	return 0;
}





/* Test some assertions about CW_TONE_SLOPE_SHAPE_*

   Code in this file depends on the fact that these values are
   different than -1. I think that ensuring that they are in general
   small, non-negative values is a good idea.

   I'm testing these values to be sure that when I get a silly idea to
   modify them, the test will catch this modification.
*/
unsigned int test_cw_gen_tone_slope_shape_enums(__attribute__((unused)) cw_gen_t * unused, cw_test_stats_t * stats)
{
	bool failure = CW_TONE_SLOPE_SHAPE_LINEAR < 0
		|| CW_TONE_SLOPE_SHAPE_RAISED_COSINE < 0
		|| CW_TONE_SLOPE_SHAPE_SINE < 0
		|| CW_TONE_SLOPE_SHAPE_RECTANGULAR < 0;

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw/gen:slope shape enums:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}




unsigned int test_cw_gen_forever_internal(cw_gen_t * gen, cw_test_stats_t * stats)
{
	const int seconds = 3;

	sleep(1);

	cw_tone_t tone;
	/* Just some acceptable values. */
	int len = 100; /* [us] */
	int freq = 500;

	CW_TONE_INIT(&tone, freq, len, CW_SLOPE_MODE_RISING_SLOPE);
	int rv1 = cw_tq_enqueue_internal(gen->tq, &tone);

	CW_TONE_INIT(&tone, freq, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
	tone.is_forever = true;
	int rv2 = cw_tq_enqueue_internal(gen->tq, &tone);

#ifdef __FreeBSD__  /* Tested on FreeBSD 10. */
	/* Separate path for FreeBSD because for some reason signals
	   badly interfere with value returned through second arg to
	   nanolseep().  Try to run the section in #else under FreeBSD
	   to see what happens - value returned by nanosleep() through
	   "rem" will be increasing. */
	fprintf(stderr, "enter any character to end \"forever\" tone\n");
	char c;
	scanf("%c", &c);
#else
	struct timespec t;
	cw_usecs_to_timespec_internal(&t, seconds * CW_USECS_PER_SEC);
	cw_nanosleep_internal(&t);
#endif

	/* Silence the generator. */
	CW_TONE_INIT(&tone, 0, len, CW_SLOPE_MODE_FALLING_SLOPE);
	int rv3 = cw_tq_enqueue_internal(gen->tq, &tone);

	bool failure = (rv1 != CW_SUCCESS || rv2 != CW_SUCCESS || rv3 != CW_SUCCESS);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw/gen: forever tone:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}




/**
   tests::cw_gen_get_timing_parameters_internal()
*/
unsigned int test_cw_gen_get_timing_parameters_internal(cw_gen_t * gen, cw_test_stats_t * stats)
{
	int initial = -5;

	int dot_len = initial;
	int dash_len = initial;
	int eom_space_len = initial;
	int eoc_space_len = initial;
	int eow_space_len = initial;
	int additional_space_len = initial;
	int adjustment_space_len = initial;


	cw_gen_reset_parameters_internal(gen);
	/* Reset requires resynchronization. */
	cw_gen_sync_parameters_internal(gen);


	cw_gen_get_timing_parameters_internal(gen,
					      &dot_len,
					      &dash_len,
					      &eom_space_len,
					      &eoc_space_len,
					      &eow_space_len,
					      &additional_space_len,
					      &adjustment_space_len);

	bool failure = (dot_len == initial)
		|| (dash_len == initial)
		|| (eom_space_len == initial)
		|| (eoc_space_len == initial)
		|| (eow_space_len == initial)
		|| (additional_space_len == initial)
		|| (adjustment_space_len == initial);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw/gen: get timing parameters:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}




/**
   \brief Test setting and getting of some basic parameters

   tests::cw_get_speed_limits()
   tests::cw_get_frequency_limits()
   tests::cw_get_volume_limits()
   tests::cw_get_gap_limits()
   tests::cw_get_weighting_limits()

   tests::cw_gen_set_speed()
   tests::cw_gen_set_frequency()
   tests::cw_gen_set_volume()
   tests::cw_gen_set_gap()
   tests::cw_gen_set_weighting()

   tests::cw_gen_get_speed()
   tests::cw_gen_get_frequency()
   tests::cw_gen_get_volume()
   tests::cw_gen_get_gap()
   tests::cw_gen_get_weighting()
*/
unsigned int test_cw_gen_parameter_getters_setters(cw_gen_t * gen, cw_test_stats_t * stats)
{
	int off_limits = 10000;

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int * min, int * max);
		int (* set_new_value)(cw_gen_t * gen, int new_value);
		int (* get_value)(cw_gen_t const * gen);

		int min; /* Minimal acceptable value of parameter. */
		int max; /* Maximal acceptable value of parameter. */

		const char *name;
	} test_data[] = {
		{ cw_get_speed_limits,      cw_gen_set_speed,      cw_gen_get_speed,      off_limits,  -off_limits,  "speed"      },
		{ cw_get_frequency_limits,  cw_gen_set_frequency,  cw_gen_get_frequency,  off_limits,  -off_limits,  "frequency"  },
		{ cw_get_volume_limits,     cw_gen_set_volume,     cw_gen_get_volume,     off_limits,  -off_limits,  "volume"     },
		{ cw_get_gap_limits,        cw_gen_set_gap,        cw_gen_get_gap,        off_limits,  -off_limits,  "gap"        },
		{ cw_get_weighting_limits,  cw_gen_set_weighting,  cw_gen_get_weighting,  off_limits,  -off_limits,  "weighting"  },
		{ NULL,                     NULL,                  NULL,                      0,                 0,  NULL         }
	};


	for (int i = 0; test_data[i].get_limits; i++) {

		int value = 0;
		bool failure = false;
		int n = 0;

		/* Test getting limits of values to be tested. */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);

		failure = (test_data[i].min <= -off_limits) || (test_data[i].max >= off_limits);

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen: get %s limits:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test setting out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].min - 1;
		failure = (CW_SUCCESS == test_data[i].set_new_value(gen, value)) || (errno != EINVAL);

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen: set %s below limit:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test setting out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].max + 1;
		failure = (CW_SUCCESS == test_data[i].set_new_value(gen, value)) || (errno != EINVAL);

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen: set %s above limit:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test setting in-range values. Set with setter and then read back with getter. */
		failure = false;
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			failure = (CW_SUCCESS != test_data[i].set_new_value(gen, j));
			if (failure) {
				break;
			}

			failure = (test_data[i].get_value(gen) != j);
			if (failure) {
				break;
			}
		}

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen: set %s within limits:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	return 0;
}




/**
   \brief Test control of volume

   Fill tone queue with short tones, then check that we can move the
   volume through its entire range.  Flush the queue when complete.

   tests::cw_get_volume_limits()
   tests::cw_gen_set_volume()
   tests::cw_gen_get_volume()
*/
unsigned int test_cw_gen_volume_functions(cw_gen_t * gen, cw_test_stats_t * stats)
{
	int cw_min = -1, cw_max = -1;

	/* Test: get range of allowed volumes. */
	{
		cw_get_volume_limits(&cw_min, &cw_max);

		bool failure = cw_min != CW_VOLUME_MIN
			|| cw_max != CW_VOLUME_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw/gen: cw_get_volume_limits(): %d, %d", cw_min, cw_max);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: decrease volume from max to low. */
	{
		/* Fill the tone queue with valid tones. */
		while (!cw_gen_is_queue_full(gen)) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, 440, 100000, CW_SLOPE_MODE_STANDARD_SLOPES);
			cw_tq_enqueue_internal(gen->tq, &tone);
		}

		bool set_failure = false;
		bool get_failure = false;

		/* TODO: why call the cw_gen_wait_for_tone() at the
		   beginning and end of loop's body? */
		for (int i = cw_max; i >= cw_min; i -= 10) {
			cw_gen_wait_for_tone(gen);
			if (CW_SUCCESS != cw_gen_set_volume(gen, i)) {
				set_failure = true;
				break;
			}

			if (cw_gen_get_volume(gen) != i) {
				get_failure = true;
				break;
			}

			cw_gen_wait_for_tone(gen);
		}

		set_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw/gen: cw_gen_set_volume() (down):");
		CW_TEST_PRINT_TEST_RESULT (set_failure, n);

		get_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen: cw_gen_get_volume() (down):");
		CW_TEST_PRINT_TEST_RESULT (get_failure, n);
	}




	/* Test: increase volume from zero to high. */
	{
		/* Fill tone queue with valid tones. */
		while (!cw_gen_is_queue_full(gen)) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, 440, 100000, CW_SLOPE_MODE_STANDARD_SLOPES);
			cw_tq_enqueue_internal(gen->tq, &tone);
		}

		bool set_failure = false;
		bool get_failure = false;

		/* TODO: why call the cw_gen_wait_for_tone() at the
		   beginning and end of loop's body? */
		for (int i = cw_min; i <= cw_max; i += 10) {
			cw_gen_wait_for_tone(gen);
			if (CW_SUCCESS != cw_gen_set_volume(gen, i)) {
				set_failure = true;
				break;
			}

			if (cw_gen_get_volume(gen) != i) {
				get_failure = true;
				break;
			}
			cw_gen_wait_for_tone(gen);
		}

		set_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw/gen: cw_gen_set_volume() (up):");
		CW_TEST_PRINT_TEST_RESULT (set_failure, n);

		get_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw/gen: cw_gen_get_volume() (up):");
		CW_TEST_PRINT_TEST_RESULT (get_failure, n);
	}

	cw_gen_wait_for_tone(gen);
	cw_tq_flush_internal(gen->tq);

	return 0;
}




/**
   \brief Test enqueueing and playing most basic elements of Morse code

   tests::cw_gen_enqueue_mark_internal()
   tests::cw_gen_enqueue_eoc_space_internal()
   tests::cw_gen_enqueue_eow_space_internal()
*/
unsigned int test_cw_gen_enqueue_primitives(cw_gen_t * gen, cw_test_stats_t * stats)
{
	int N = 20;

	/* Test: sending dot. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (CW_SUCCESS != cw_gen_enqueue_mark_internal(gen, CW_DOT_REPRESENTATION)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw/gen: cw_gen_enqueue_mark_internal(CW_DOT_REPRESENTATION):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending dash. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (CW_SUCCESS != cw_gen_enqueue_mark_internal(gen, CW_DASH_REPRESENTATION)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw/gen: cw_gen_enqueue_mark_internal(CW_DASH_REPRESENTATION):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending inter-character space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (CW_SUCCESS != cw_gen_enqueue_eoc_space_internal(gen)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw/gen: cw_gen_enqueue_eoc_space_internal():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending inter-word space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (CW_SUCCESS != cw_gen_enqueue_eow_space_internal(gen)) {
				failure = true;
				break;
			}
		}
		cw_gen_wait_for_tone(gen);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw/gen: cw_gen_enqueue_eow_space_internal():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	return 0;
}




/**
   \brief Test playing representations of characters

   tests::cw_gen_enqueue_representation_partial_internal()
*/
unsigned int test_cw_gen_enqueue_representations(cw_gen_t * gen, cw_test_stats_t * stats)
{
	/* Representation is valid when it contains dots and dashes
	   only.  cw_gen_enqueue_representation_partial_internal()
	   doesn't care about correct mapping of representation to a
	   character. */

	/* Test: sending valid representations. */
	{
		bool failure = (CW_SUCCESS != cw_gen_enqueue_representation_partial_internal(gen, ".-.-.-"))
			|| (CW_SUCCESS != cw_gen_enqueue_representation_partial_internal(gen, ".-"))
			|| (CW_SUCCESS != cw_gen_enqueue_representation_partial_internal(gen, "---"))
			|| (CW_SUCCESS != cw_gen_enqueue_representation_partial_internal(gen, "...-"));

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw/gen: cw_gen_enqueue_representation_partial_internal(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending invalid representations. */
	{
		bool failure = (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "INVALID"))
			|| (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "_._T"))
			|| (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "_.A_."))
			|| (CW_SUCCESS == cw_gen_enqueue_representation_partial_internal(gen, "S-_-"));

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw/gen: cw_gen_enqueue_representation_partial_internal(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_gen_wait_for_tone(gen);

	struct timespec req = { .tv_sec = 3, .tv_nsec = 0 };
	cw_nanosleep_internal(&req);

	return 0;
}




/**
   Send all supported characters: first as individual characters, and then as a string.

   tests::cw_gen_enqueue_character()
   tests::cw_gen_enqueue_string()
*/
unsigned int test_cw_gen_enqueue_character_and_string(cw_gen_t * gen, cw_test_stats_t * stats)
{
	/* Test: sending all supported characters as individual characters. */
	{
		char charlist[UCHAR_MAX + 1];
		bool failure = false;

		/* Send all the characters from the charlist individually. */
		cw_list_characters(charlist);
		fprintf(out_file, "libcw/gen: cw_enqueue_character(<valid>):\n"
			"libcw/gen:     ");
		for (int i = 0; charlist[i] != '\0'; i++) {
			fprintf(out_file, "%c", charlist[i]);
			fflush(out_file);
			if (CW_SUCCESS != cw_gen_enqueue_character(gen, charlist[i])) {
				failure = true;
				break;
			}
			cw_gen_wait_for_queue_level(gen, 0);
		}

		fprintf(out_file, "\n");

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file,"libcw/gen: cw_gen_enqueue_character(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending invalid character. */
	{
		bool failure = CW_SUCCESS == cw_gen_enqueue_character(gen, 0);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw/gen: cw_gen_enqueue_character(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending all supported characters as single string. */
	{
		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);

		/* Send the complete charlist as a single string. */
		fprintf(out_file, "libcw/gen: cw_gen_enqueue_string(<valid>):\n"
			"libcw/gen:     %s\n", charlist);
		bool failure = CW_SUCCESS != cw_gen_enqueue_string(gen, charlist);

		while (cw_gen_get_queue_length(gen) > 0) {
			fprintf(out_file, "libcw/gen: tone queue length %-6zu\r", cw_gen_get_queue_length(gen));
			fflush(out_file);
			cw_gen_wait_for_tone(gen);
		}
		fprintf(out_file, "libcw:gen tone queue length %-6zu\n", cw_gen_get_queue_length(gen));
		cw_gen_wait_for_queue_level(gen, 0);

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw/gen: cw_gen_enqueue_string(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending invalid string. */
	{
		bool failure = CW_SUCCESS == cw_gen_enqueue_string(gen, "%INVALID%");

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, "libcw/gen: cw_gen_enqueue_string(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	return 0;
}




#endif /* #ifdef LIBCW_UNIT_TESTS */
