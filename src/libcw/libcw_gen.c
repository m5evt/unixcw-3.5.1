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
   2. recalculate tone length in usecs into length in samples
   3. for every sample in tone, calculate sine wave sample and
      put it in generator's constant size buffer
   4. if buffer is full of sine wave samples, push it to audio sink
   5. since buffer is shorter than (almost) any tone, you will push
      the buffer multiple times per tone
   6. if you iterated over all samples in tone, but you still didn't
      fill up that last buffer, dequeue next tone from queue, go to #2
   7. if there are no more tones in queue, pad the buffer with silence,
      and push the buffer to audio sink.

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





static int   cw_gen_new_open_internal(cw_gen_t *gen, int audio_system, const char *device);
static void *cw_gen_dequeue_and_generate_internal(void *arg);
static int   cw_gen_calculate_sine_wave_internal(cw_gen_t *gen, cw_tone_t *tone);
static int   cw_gen_calculate_amplitude_internal(cw_gen_t *gen, cw_tone_t *tone);
static int   cw_gen_write_to_soundcard_internal(cw_gen_t *gen, cw_tone_t *tone, bool empty_tone);

static int   cw_gen_enqueue_valid_character_internal(cw_gen_t *gen, char c);
static int   cw_gen_enqueue_valid_character_no_eoc_space_internal(cw_gen_t *gen, char character);
static int   cw_gen_enqueue_representation_no_eoc_space_internal(cw_gen_t *gen, const char *representation);

static void  cw_gen_recalculate_slopes_internal(cw_gen_t *gen);

static int   cw_gen_join_thread_internal(cw_gen_t *gen);
static void  cw_gen_write_calculate_empty_tone_internal(cw_gen_t *gen, cw_tone_t *tone);
static void  cw_gen_write_calculate_tone_internal(cw_gen_t *gen, cw_tone_t *tone);



/**
   \brief Get a copy of readable label of current audio system

   Get a copy of human-readable string describing audio system
   associated currently with given \p gen.

   The function returns newly allocated pointer to one of following
   strings: "None", "Null", "Console", "OSS", "ALSA", "PulseAudio",
   "Soundcard".

   The returned pointer is owned by caller.

   Notice that the function returns a new pointer to newly allocated
   string. cw_gen_get_audio_system_label() returns a pointer to
   static string owned by library.

   \param gen - generator for which to check audio system label

   \return audio system's label
*/
char *cw_gen_get_audio_system_label(cw_gen_t *gen)
{
	char *s = strdup(cw_get_audio_system_label(gen->audio_system));
	if (!s) {
		cw_vdm ("failed to strdup() audio system label for audio system %d\n", gen->audio_system);
	}

	return s;
}





/**
   \brief Start a generator
*/
int cw_gen_start(cw_gen_t *gen)
{
	gen->phase_offset = 0.0;

	/* This should be set to true before launching
	   cw_gen_dequeue_and_generate_internal(), because loop in the
	   function run only when the flag is set. */
	gen->do_dequeue_and_generate = true;

	gen->client.thread_id = pthread_self();

	if (gen->audio_system == CW_AUDIO_NULL
	    || gen->audio_system == CW_AUDIO_CONSOLE
	    || gen->audio_system == CW_AUDIO_OSS
	    || gen->audio_system == CW_AUDIO_ALSA
	    || gen->audio_system == CW_AUDIO_PA) {

		/* cw_gen_dequeue_and_generate_internal() is THE
		   function that does the main job of generating
		   tones. */
		int rv = pthread_create(&gen->thread.id, &gen->thread.attr,
					cw_gen_dequeue_and_generate_internal,
					(void *) gen);
		if (rv != 0) {
			gen->do_dequeue_and_generate = false;

			cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: failed to create %s generator thread", cw_get_audio_system_label(gen->audio_system));
			return CW_FAILURE;
		} else {
			gen->thread.running = true;

			/* For some yet unknown reason you have to put
			   usleep() here, otherwise a generator may
			   work incorrectly */
			usleep(100000);
#ifdef LIBCW_WITH_DEV
			cw_dev_debug_print_generator_setup(gen);
#endif
			return CW_SUCCESS;
		}
	} else {
		gen->do_dequeue_and_generate = false;

		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: unsupported audio system %d", gen->audio_system);
		return CW_FAILURE;
	}
}





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
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: no audio system specified");
		return CW_FAILURE;
	}

	if (device) {
		gen->audio_device = strdup(device);
	} else {
		gen->audio_device = strdup(default_audio_devices[gen->audio_system]);
	}

	if (!gen->audio_device) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: malloc()");
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

   \param gen - generator using an audio sink that should be silenced

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure to silence an audio sink
*/
int cw_gen_silence_internal(cw_gen_t *gen)
{
	if (!gen) {
		/* this may happen because the process of finalizing
		   usage of libcw is rather complicated; this should
		   be somehow resolved */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_WARNING,
			      "libcw: called the function for NULL generator");
		return CW_SUCCESS;
	}

	if (!(gen->thread.running)) {
		/* Silencing a generator means enqueueing and generating
		   a tone with zero frequency.  We shouldn't do this
		   when a "dequeue-and-generate-a-tone" function is not
		   running (anymore). This is not an error situation,
		   so return CW_SUCCESS. */
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

	} else if (gen->audio_system == CW_AUDIO_OSS
		   || gen->audio_system == CW_AUDIO_ALSA
		   || gen->audio_system == CW_AUDIO_PA) {

		cw_tone_t tone;
		CW_TONE_INIT(&tone, 0, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
		status = cw_tq_enqueue_internal(gen->tq, &tone);

		/* allow some time for generating the last tone */
		usleep(2 * gen->quantum_len);
	} else {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: called silence() function for generator without audio system specified");
	}

	if (gen->audio_system == CW_AUDIO_ALSA) {
		/* "Stop a PCM dropping pending frames. " */
		cw_alsa_drop(gen);
	}

	//gen->do_dequeue_and_generate = false;

	return status;
}





/**
   \brief Create new generator

   testedin::test_cw_gen_new_delete()
*/
cw_gen_t *cw_gen_new(int audio_system, const char *device)
{
#ifdef LIBCW_WITH_DEV
	fprintf(stderr, "libcw build %s %s\n", __DATE__, __TIME__);
#endif

	cw_assert (audio_system != CW_AUDIO_NONE, "can't create generator with audio system \"NONE\"");

	cw_gen_t *gen = (cw_gen_t *) malloc(sizeof (cw_gen_t));
	if (!gen) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: malloc()");
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


		/* cw key associated with this generator. */
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

		gen->do_dequeue_and_generate = false;
	}


	/* Audio system. */
	{
		gen->audio_device = NULL;
		gen->audio_sink = -1;
		//gen->audio_system = audio_system;
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
				      "libcw: failed to open audio device for audio system '%s' and device '%s'", cw_get_audio_system_label(audio_system), device);
			cw_gen_delete(&gen);
			return (cw_gen_t *) NULL;
		}

		if (audio_system == CW_AUDIO_NULL
		    || audio_system == CW_AUDIO_CONSOLE) {

			; /* the two types of audio output don't require audio buffer */
		} else {
			gen->buffer = (cw_sample_t *) malloc(gen->buffer_n_samples * sizeof (cw_sample_t));
			if (!gen->buffer) {
				cw_debug_msg (&cw_debug_object, CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
					      "libcw: malloc()");
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
				      "libcw: failed to set slope");
			cw_gen_delete(&gen);
		return (cw_gen_t *) NULL;
		}
	}

	return gen;
}





/**
   \brief Delete a generator

   testedin::test_cw_gen_new_delete()
*/
void cw_gen_delete(cw_gen_t **gen)
{
	cw_assert (gen, "generator is NULL");

	if (!*gen) {
		return;
	}

	if ((*gen)->do_dequeue_and_generate) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
			      "libcw: you forgot to call cw_gen_stop()");
		cw_gen_stop(*gen);
	}


	fprintf(stderr, "attempting to delete tq when gen->thread.running = %d\n", (*gen)->thread.running);
	cw_tq_delete_internal(&((*gen)->tq));


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
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: WARNING: NULL function pointer, something went wrong");
	}

	pthread_attr_destroy(&((*gen)->thread.attr));

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

   Empty generator's tone queue.
   Silence generator's audio sink.
   Stop generator' "dequeue and generate" thread function.
   If the thread does not stop in one second, kill it.

   You have to use cw_gen_start() if you want to enqueue and
   generate tones with the same generator again.

   It seems that only silencing of generator's audio sink may fail,
   and this is when this function may return CW_FAILURE. Otherwise
   function returns CW_SUCCESS.

   \return CW_SUCCESS if all four actions completed (successfully)
   \return CW_FAILURE if any of the four actions failed (see note above)
*/
int cw_gen_stop(cw_gen_t *gen)
{
	if (!gen) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_WARNING,
			      "libcw: called the function for NULL generator");
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
	fprintf(stderr, "setting do_dequeue_and_generate to %d\n", gen->do_dequeue_and_generate);

	if (!gen->thread.running) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_INFO, "libcw: EXIT: seems that thread function was not started at all");

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

	   This is to force the loop to start new cycle, notice that
	   gen->do_dequeue_and_generate is false, and to get the thread
	   function to return (and thus to end the thread). */
#if 0
	pthread_mutex_lock(&gen->tq->wait_mutex);
	pthread_cond_broadcast(&gen->tq->wait_var);
	pthread_mutex_unlock(&gen->tq->wait_mutex);
#endif

	pthread_mutex_lock(&gen->tq->dequeue_mutex);
	pthread_cond_signal(&gen->tq->dequeue_var); /* Use pthread_cond_signal() because there is only one listener. */
	pthread_mutex_unlock(&gen->tq->dequeue_mutex);

#if 0   /* Original implementation using signals. */
	pthread_kill(gen->thread.id, SIGALRM);
#endif

	return cw_gen_join_thread_internal(gen);
}





int cw_gen_join_thread_internal(cw_gen_t *gen)
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
		cw_debug_msg (&cw_debug_object, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR, "libcw/gen: failed to join threads: \"%s\"", strerror(rv));
		return CW_FAILURE;
	}
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
	   src/cwutils/cw_common.c/cw_gen_new_from_config() */

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
   pushes them to audio output as long as
   generator->do_dequeue_and_generate is true.

   The generator must be fully configured before calling this
   function.

   \param arg - generator (casted to (void *)) to be used for generating tones

   \return NULL pointer
*/
void *cw_gen_dequeue_and_generate_internal(void *arg)
{
	cw_gen_t *gen = (cw_gen_t *) arg;

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, 0, CW_SLOPE_MODE_STANDARD_SLOPES);

	while (gen->do_dequeue_and_generate) {
		int tq_rv = cw_tq_dequeue_internal(gen->tq, &tone);
		if (tq_rv == CW_TQ_NDEQUEUED_IDLE) {

			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_INFO,
				      "libcw/tq: got CW_TQ_NDEQUEUED_IDLE");

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

#if 0                   /* Original implementation using signals. */
			/* TODO: can we / should we specify on which
			   signal exactly we are waiting for? */
			cw_signal_wait_internal();
#endif
			continue;
		}

		if (gen->key) {
			int state = CW_KEY_STATE_OPEN;

			if (tq_rv == CW_TQ_DEQUEUED) {
				state = tone.frequency ? CW_KEY_STATE_CLOSED : CW_KEY_STATE_OPEN;
			} else if (tq_rv == CW_TQ_NDEQUEUED_EMPTY) {
				state =  CW_KEY_STATE_OPEN;
			} else {
				cw_assert (0, "unexpected return value from dequeue(): %d", tq_rv);
			}
			cw_key_tk_set_value_internal(gen->key, state);


			cw_key_ik_increment_timer_internal(gen->key, tone.len);
		}


#ifdef LIBCW_WITH_DEV
		cw_debug_ev ((&cw_debug_object_ev), 0, tone.frequency ? CW_DEBUG_EVENT_TONE_HIGH : CW_DEBUG_EVENT_TONE_LOW);
#endif

		if (gen->audio_system == CW_AUDIO_NULL) {
			cw_null_write(gen, &tone);
		} else if (gen->audio_system == CW_AUDIO_CONSOLE) {
			cw_console_write(gen, &tone);
		} else {
			cw_gen_write_to_soundcard_internal(gen, &tone,
							   tq_rv == CW_TQ_NDEQUEUED_EMPTY);
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
		pthread_cond_broadcast(&gen->tq->wait_var);
		pthread_mutex_unlock(&gen->tq->wait_mutex);


#if 0           /* Original implementation using signals. */
		pthread_kill(gen->client.thread_id, SIGALRM);
#endif

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
		if (!cw_key_ik_update_graph_state_internal(gen->key)) {
			/* just try again, once */
			usleep(1000);
			cw_key_ik_update_graph_state_internal(gen->key);
		}

#ifdef LIBCW_WITH_DEV
		cw_debug_ev ((&cw_debug_object_ev), 0, tone.frequency ? CW_DEBUG_EVENT_TONE_LOW : CW_DEBUG_EVENT_TONE_HIGH);
#endif

	} /* while (gen->do_dequeue_and_generate) */

	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_INFO,
		      "libcw: EXIT: generator stopped (gen->do_dequeue_and_generate = %d)", gen->do_dequeue_and_generate);

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
	pthread_cond_broadcast(&gen->tq->wait_var);
	pthread_mutex_unlock(&gen->tq->wait_mutex);

#if 0   /* Original implementation using signals. */
	pthread_kill(gen->client.thread_id, SIGALRM);
#endif

	gen->thread.running = false;
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

   Actually "calculation" is a bit too big word. The function is just
   a three-level-deep decision tree, deciding which of precalculated
   values to return. There are no complicated arithmetical
   calculations being made each time the function is called, so the
   execution time should be pretty small.

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

   \param gen - generator used to generate a sine wave
   \param tone - tone being generated

   \return value of a sample of sine wave, a non-negative number
*/
int cw_gen_calculate_amplitude_internal(cw_gen_t *gen, cw_tone_t *tone)
{
#if 0
	int amplitude = 0;
	/* Blunt algorithm for calculating amplitude;
	   for debug purposes only. */
	if (tone->frequency) {
		amplitude = gen->volume_abs;
	} else {
		amplitude = 0;
	}

	return amplitude;
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
		cw_assert (0, "->sample_iterator out of bounds:\n"
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

   FIXME: first argument of this public function is gen, but no
   function provides access to generator variable.

   \param gen - generator for which to set tone slope parameters
   \param slope_shape - shape of slope: linear, raised cosine, sine, rectangular
   \param slope_len - length of slope [microseconds]

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_tone_slope(cw_gen_t *gen, int slope_shape, int slope_len)
{
	assert (gen);

	/* Handle conflicting values of arguments. */
	if (slope_shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR
	    && slope_len > 0) {

		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: requested a rectangular slope shape, but also requested slope len > 0");

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
	cw_assert (slope_n_samples >= 0, "negative slope_n_samples: %d", slope_n_samples);


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
					      "libcw: failed to realloc() table of slope amplitudes");
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

   TODO: consider writing unit test code for the function.

   \param gen - generator
*/
void cw_gen_recalculate_slopes_internal(cw_gen_t *gen)
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
			cw_assert (0, "we shouldn't be here, calculating rectangular slopes");

		} else {
			cw_assert (0, "unsupported slope shape %d", gen->tone_slope.shape);
		}
	}

	return;
}





/**
   \brief Write tone to soundcard
*/
int cw_gen_write_to_soundcard_internal(cw_gen_t *gen, cw_tone_t *tone, bool empty_tone)
{
	if (empty_tone) {
		/* No valid tone dequeued from tone queue. We need
		   samples to complete filling buffer, but they have
		   to be empty samples. */
		cw_gen_write_calculate_empty_tone_internal(gen, tone);
	} else {
		/* Valid tone dequeued from tone queue. Use it to
		   calculate samples in buffer. */
		cw_gen_write_calculate_tone_internal(gen, tone);
	}


	/* Total number of samples to write in a loop below. */
	int64_t samples_to_write = tone->n_samples;

#if 0
	fprintf(stderr, "++++ entering loop, tone->frequency = %d, buffer->n_samples = %d, tone->n_samples = %d, samples_to_write = %d\n",
		tone->frequency, gen->buffer_n_samples, tone->n_samples, samples_to_write);
	fprintf(stderr, "++++ entering loop, expected ~%f loops\n", 1.0 * samples_to_write / gen->buffer_n_samples);
	int debug_loop = 0;
#endif


	// cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: %lld samples, %d us, %d Hz", tone->n_samples, tone->len, gen->frequency);
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
			   buffer. We can't send an unready buffer to
			   audio sink. We will have to get more
			   samples to fill the buffer completely. */
			gen->buffer_sub_stop = gen->buffer_sub_start + samples_to_write - 1;
		}

		/* How many samples of audio buffer's subarea will be
		   calculated in a given cycle of "calculate sine
		   wave" code? */
		int buffer_sub_n_samples = gen->buffer_sub_stop - gen->buffer_sub_start + 1;


#if 0
		fprintf(stderr, "++++        loop #%d, buffer_sub_n_samples = %d\n", ++debug_loop, buffer_sub_n_samples);
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
			      "libcw: sub start: %d, sub stop: %d, sub len: %d, to calculate: %d", gen->buffer_sub_start, gen->buffer_sub_stop, buffer_sub_n_samples, samples_to_write);
#endif


		int calculated = cw_gen_calculate_sine_wave_internal(gen, tone);
		cw_assert (calculated == buffer_sub_n_samples,
			   "calculated wrong number of samples: %d != %d",
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
			cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "samples left = %d", samples_to_write);
		}
#endif

	} /* while (samples_to_write > 0) { */

	//fprintf(stderr, "++++ left loop, %d loops, samples left = %d\n", debug_loop, (int) samples_to_write);

	return 0;
}





void cw_gen_write_calculate_empty_tone_internal(cw_gen_t *gen, cw_tone_t *tone)
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





void cw_gen_write_calculate_tone_internal(cw_gen_t *gen, cw_tone_t *tone)
{
	/* Recalculate tone parameters from microseconds into
	   samples. After this point the samples will be all that
	   matters. */

	/* 100 * 10000 = 1.000.000 usecs per second. */
	tone->n_samples = gen->sample_rate / 100;
	tone->n_samples *= tone->len;
	tone->n_samples /= 10000;

	//fprintf(stderr, "++++ length of regular tone = %d [samples]\n", tone->n_samples);

	/* Length of a single slope (rising or falling). */
	int slope_n_samples= gen->sample_rate / 100;
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
		cw_assert (0, "unknown tone slope mode %d", tone->slope_mode);
	}

	tone->sample_iterator = 0;

	return;
}





/**
   \brief Set sending speed of generator

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal value
   of send speed.

   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param gen - generator for which to set the speed
   \param new_value - new value of send speed to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_speed(cw_gen_t *gen, int new_value)
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
   The frequency must be within limits marked by CW_FREQUENCY_MIN
   and CW_FREQUENCY_MAX.

   See libcw.h/CW_FREQUENCY_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of frequency.

   errno is set to EINVAL if \p new_value is out of range.

   \param gen - generator for which to set new frequency
   \param new_value - new value of frequency to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_frequency(cw_gen_t *gen, int new_value)
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
   The volume must be within limits marked by CW_VOLUME_MIN and CW_VOLUME_MAX.

   Note that volume settings are not fully possible for the console speaker.
   In this case, volume settings greater than zero indicate console speaker
   sound is on, and setting volume to zero will turn off console speaker
   sound.

   See libcw.h/CW_VOLUME_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of volume.
   errno is set to EINVAL if \p new_value is out of range.

   \param gen - generator for which to set a volume level
   \param new_value - new value of volume to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_volume(cw_gen_t *gen, int new_value)
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

   See libcw.h/CW_GAP_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of gap.
   errno is set to EINVAL if \p new_value is out of range.

   \param gen - generator for which to set gap
   \param new_value - new value of gap to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_gap(cw_gen_t *gen, int new_value)
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

   See libcw.h/CW_WEIGHTING_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of weighting.
   errno is set to EINVAL if \p new_value is out of range.

   \param gen - generator for which to set new weighting
   \param new_value - new value of weighting to be assigned for generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_weighting(cw_gen_t *gen, int new_value)
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

   \param gen - generator from which to get the parameter

   \return current value of the generator's send speed
*/
int cw_gen_get_speed(cw_gen_t *gen)
{
	return gen->send_speed;
}





/**
   \brief Get frequency from generator

   Function returns "frequency" parameter of generator,
   even if the generator is stopped, or volume of generated sound is zero.

   \param gen - generator from which to get the parameter

   \return current value of generator's frequency
*/
int cw_gen_get_frequency(cw_gen_t *gen)
{
	return gen->frequency;
}





/**
   \brief Get sound volume from generator

   Function returns "volume" parameter of generator,
   even if the generator is stopped.

   \param gen - generator from which to get the parameter

   \return current value of generator's sound volume
*/
int cw_gen_get_volume(cw_gen_t *gen)
{
	return gen->volume_percent;
}








/**
   \brief Get sending gap from generator

   \param gen - generator from which to get the parameter

   \return current value of generator's sending gap
*/
int cw_gen_get_gap(cw_gen_t *gen)
{
	return gen->gap;
}





/**
   \brief Get sending weighting from generator

   \param gen - generator from which to get the parameter

   \return current value of generator's sending weighting
*/
int cw_gen_get_weighting(cw_gen_t *gen)
{
	return gen->weighting;
}





/**
   \brief Get timing parameters for sending

   Return the low-level timing parameters calculated from the speed, gap,
   tolerance, and weighting set.  Parameter values are returned in
   microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   \param gen
   \param dot_len
   \param dash_len
   \param eom_space_len
   \param eoc_space_len
   \param eow_space_len
   \param additional_space_len
   \param adjustment_space_len
*/
void cw_gen_get_timing_parameters_internal(cw_gen_t *gen,
					   int *dot_len,
					   int *dash_len,
					   int *eom_space_len,
					   int *eoc_space_len,
					   int *eow_space_len,
					   int *additional_space_len, int *adjustment_space_len)
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

   Function sets errno to EINVAL if an argument is invalid, and
   returns CW_FAILURE.

   Function also returns CW_FAILURE if adding the element to queue of
   tones failed.

   \param gen - generator to be used to enqueue a mark and inter-mark space
   \param mark - mark to send: Dot (CW_DOT_REPRESENTATION) or Dash (CW_DASH_REPRESENTATION)

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_gen_enqueue_mark_internal(cw_gen_t *gen, char mark)
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

	if (!status) {
		return CW_FAILURE;
	}

	/* Send the inter-mark space. */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, gen->eom_space_len, CW_SLOPE_MODE_NO_SLOPES);
	if (!cw_tq_enqueue_internal(gen->tq, &tone)) {
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





/**
   \brief Enqueue inter-character space

   The function enqueues space of length 2 Units. The function is
   intended to be used after inter-mark space has already been enqueued.

   In such situation standard inter-mark space (one Unit) and enqueued
   two Units form a full standard inter-character space (three Units).

   Inter-character adjustment space is added at the end.

   \param gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_eoc_space_internal(cw_gen_t *gen)
{
	/* Synchronize low-level timing parameters. */
	cw_gen_sync_parameters_internal(gen);

	/* Delay for the standard end of character period, plus any
	   additional inter-character gap */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, gen->eoc_space_len + gen->additional_space_len, CW_SLOPE_MODE_NO_SLOPES);
	return cw_tq_enqueue_internal(gen->tq, &tone);
}





/**
   \brief Enqueue space character

   The function should be used to enqueue a regular ' ' character.

   The function enqueues space of length 5 Units. The function is
   intended to be used after inter-mark space and inter-character
   space have already been enqueued.

   In such situation standard inter-mark space (one Unit) and
   inter-character space (two Units) and regular space (five units)
   form a full standard end-of-word space (seven Units).

   Inter-word adjustment space is added at the end.

   \param gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_eow_space_internal(cw_gen_t *gen)
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
	   passing tone queue level from 2 to 1, we split the eow
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
	int n = 1; /* No division. Old situation causing an error in
		      client applications. */
#else
	int n = 2; /* "small integer value" - used to have more tones per eow space. */
#endif
	CW_TONE_INIT(&tone, 0, gen->eow_space_len / n, CW_SLOPE_MODE_NO_SLOPES);
	for (int i = 0; i < n; i++) {
		int rv = cw_tq_enqueue_internal(gen->tq, &tone);
		if (rv) {
			enqueued++;
		} else {
			return CW_FAILURE;
		}
	}

	CW_TONE_INIT(&tone, 0, gen->adjustment_space_len, CW_SLOPE_MODE_NO_SLOPES);
	int rv = cw_tq_enqueue_internal(gen->tq, &tone);
	if (rv) {
		enqueued++;
	} else {
		return CW_FAILURE;
	}

	cw_debug_msg (&cw_debug_object, CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
		      "libcw: enqueued %d tones per eow space, tq len = %zu",
		      enqueued, cw_tq_length_internal(gen->tq));

	return CW_SUCCESS;
}





/**
   \brief Enqueue the given representation

   Function enqueues given \p representation using given \p generator.
   *Every* mark from the \p representation is followed by a standard
   inter-mark space.

   Function does not enqueue inter-character space at the end of
   representation (i.e. after the last inter-mark space). This means
   that there is only one inter-mark space enqueued at the end of the
   representation.

   Function sets errno to EAGAIN if there is not enough space in tone
   queue to enqueue \p representation.

   Representation is not validated by this function. This means that
   the function allows caller to do some neat tricks, but it also
   means that the function can be abused.

   \param gen - generator used to enqueue the representation
   \param representation - representation to enqueue

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_gen_enqueue_representation_no_eoc_space_internal(cw_gen_t *gen, const char *representation)
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
   \brief Look up and enqueue a given ASCII character as Morse code

   After enqueueing last Mark (Dot or Dash) comprising a character, an
   inter-mark space is enqueued.  Inter-character space is not
   enqueued after that last inter-mark space.

   _valid_character_ in function's name means that the function
   expects the character \p c to be valid (\p c should be validated by
   caller before passing it to the function).

   Function sets errno to ENOENT if \p c is not a recognized character.

   \param gen - generator to be used to enqueue character
   \param c - character to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_valid_character_no_eoc_space_internal(cw_gen_t *gen, char c)
{
	if (!gen) {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: no generator available");
		return CW_FAILURE;
	}

	/* ' ' character (i.e. regular space) is a special case. */
	if (c == ' ') {
		return cw_gen_enqueue_eow_space_internal(gen);
	}

	const char *r = cw_character_to_representation_internal(c);

	/* This shouldn't happen since we are in _valid_character_ function... */
	cw_assert (r, "libcw/data: failed to find representation for character '%c'/%hhx", c, c);

	/* ... but fail gracefully anyway. */
	if (!r) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	if (!cw_gen_enqueue_representation_no_eoc_space_internal(gen, r)) {
		return CW_FAILURE;
	}

	/* No inter-character space here. */

	return CW_SUCCESS;
}





/**
   \brief Look up and enqueue a given ASCII character as Morse code

   After enqueueing last Mark (Dot or Dash) comprising a character, an
   inter-mark space is enqueued.  Inter-character space is enqueued
   after that last inter-mark space.

   _valid_character_ in function's name means that the function
   expects the character \p c to be valid (\p c should be validated by
   caller before passing it to the function).

   Function sets errno to ENOENT if \p character is not a recognized character.

   \param gen - generator to be used to enqueue character
   \param c - character to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_valid_character_internal(cw_gen_t *gen, char c)
{
	if (!cw_gen_enqueue_valid_character_no_eoc_space_internal(gen, c)) {
		return CW_FAILURE;
	}

	if (!cw_gen_enqueue_eoc_space_internal(gen)) {
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/**
   \brief Look up and enqueue a given ASCII character as Morse

   Inter-mark + inter-character delay is appended at the end of
   enqueued Marks.

   On success the function returns CW_SUCCESS.
   On failure the function returns CW_FAILURE and sets errno.

   errno is set to ENOENT if the given character \p c is not a valid
   Morse character.
   errno is set to EBUSY if current audio sink or keying system is
   busy.
   errno is set to EAGAIN if the generator's tone queue is full, or if
   there is insufficient space to queue the tones for the character.

   This routine returns as soon as the character and trailing spaces
   have been successfully queued for sending (that is, almost
   immediately).  The actual sending happens in background processing.
   See cw_gen_wait_for_tone_internal() and cw_gen_wait_for_queue() for
   ways to check the progress of sending.

   TODO: add cw_gen_wait_for_tone_internal().

   \param gen - generator to enqueue the character to
   \param c - character to enqueue in generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_character(cw_gen_t *gen, char c)
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
   \brief Look up and enqueue a given ASCII character as Morse code

   "partial" means that the inter-character space is not appended at
   the end of Marks and Spaces enqueued in generator (but the last
   inter-mark space is). This enables the formation of combination
   characters by client code.

   On success the function returns CW_SUCCESS.
   On failure the function returns CW_FAILURE and sets errno.

   errno is set to ENOENT if the given character \p c is not a valid
   Morse character.
   errno is set to EBUSY if the audio sink or keying system is busy.
   errno is set to EAGAIN if the tone queue is full, or if there is
   insufficient space to queue the tones for the character.

   This routine returns as soon as the character and trailing spaces
   have been successfully queued for sending (that is, almost
   immediately).  The actual sending happens in background processing.
   See cw_gen_wait_for_tone_internal() and cw_gen_wait_for_queue() for
   ways to check the progress of sending.

   \param gen - generator to use
   \param c - character to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_character_parital(cw_gen_t *gen, char c)
{
	if (!cw_character_is_valid(c)) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	if (!cw_gen_enqueue_valid_character_no_eoc_space_internal(gen, c)) {
		return CW_FAILURE;
	}

	/* _partial(): don't enqueue eoc space. */

	return CW_SUCCESS;
}





/**
   \brief Enqueue a given ASCII string in Morse code

   errno is set to ENOENT if any character in the string is not a
   valid Morse character.

   errno is set to EBUSY if audio sink or keying system is busy.

   errno is set to EAGAIN if the tone queue is full or if the tone
   queue runs out of space part way through queueing the string.
   However, an indeterminate number of the characters from the string
   will have already been queued.

   For safety, clients can ensure the tone queue is empty before
   queueing a string, or use cw_gen_enqueue_character() if they
   need finer control.

   This routine returns as soon as the character and trailing spaces
   have been successfully queued for sending (that is, almost
   immediately).  The actual sending happens in background processing.
   See cw_gen_wait_for_tone_internal() and cw_gen_wait_for_queue() for
   ways to check the progress of sending.

   \param gen - generator to use
   \param string - string to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_string(cw_gen_t *gen, const char *string)
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

  \param gen
*/
void cw_gen_reset_parameters_internal(cw_gen_t *gen)
{
	cw_assert (gen, "generator is NULL");

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
		      "libcw: send usec timings <%d [wpm]>: dot: %d, dash: %d, %d, %d, %d, %d, %d",
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

   \param gen - generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_begin_mark_internal(cw_gen_t *gen)
{
	/* In case of straight key we don't know at all how long the
	   tone should be (we don't know for how long the key will be
	   closed).

	   Let's enqueue a beginning of mark (rising slope) +
	   "forever" (constant) tone. The constant tone will be generated
	   until function receives CW_KEY_STATE_OPEN key state. */

	cw_tone_t tone;
	CW_TONE_INIT(&tone, gen->frequency, gen->tone_slope.len, CW_SLOPE_MODE_RISING_SLOPE);
	int rv = cw_tq_enqueue_internal(gen->tq, &tone);

	if (rv == CW_SUCCESS) {

		CW_TONE_INIT(&tone, gen->frequency, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
		tone.is_forever = true;
		rv = cw_tq_enqueue_internal(gen->tq, &tone);

		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
			      "libcw: tone queue: len = %zu", cw_tq_length_internal(gen->tq));
	}

	return rv;
}





/**
   Helper function intended to hide details of tone queue and of
   enqueueing a tone from cw_key module.

   The function should be called only on "key up" (begin space) event
   from hardware straight key.

   The function is called in very specific context, see cw_key module
   for details.

   \param gen - generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_begin_space_internal(cw_gen_t *gen)
{
	if (gen->audio_system == CW_AUDIO_CONSOLE) {
		/* FIXME: I think that enqueueing tone is not just a
		   matter of generating it using generator, but also a
		   matter of timing events using generator. Enqueueing
		   tone here and dequeueing it later will be used to
		   control state of a key. So if we switch state of
		   key just for quantum_len usecs, then there may be a
		   problem. */


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

   'Pure' means without any end-of-mark spaces.

   The function is called in very specific context, see cw_key module
   for details.

   \param gen - generator
   \param symbol - symbol to enqueue (Space/Dot/Dash)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_enqueue_pure_symbol_internal(cw_gen_t *gen, char symbol)
{
	cw_tone_t tone;

	if (symbol == CW_DOT_REPRESENTATION) {
		CW_TONE_INIT(&tone, gen->frequency, gen->dot_len, CW_SLOPE_MODE_STANDARD_SLOPES);

	} else if (symbol == CW_DASH_REPRESENTATION) {
		CW_TONE_INIT(&tone, gen->frequency, gen->dash_len, CW_SLOPE_MODE_STANDARD_SLOPES);

	} else if (symbol == CW_SYMBOL_SPACE) {
		CW_TONE_INIT(&tone, 0, gen->eom_space_len, CW_SLOPE_MODE_NO_SLOPES);

	} else {
		cw_assert (0, "unknown key symbol '%d'", symbol);
	}

	return cw_tq_enqueue_internal(gen->tq, &tone);
}





/**
   \brief Wait for generator's tone queue to drain until only as many tones as given in level remain queued

   This function is for use by programs that want to optimize
   themselves to avoid the cleanup that happens when generator's tone
   queue drains completely. Such programs have a short time in which
   to add more tones to the queue.

   The function returns CW_SUCCESS on success.

   \param gen - generator on which to wait
   \param level - low level in queue, at which to return

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_wait_for_queue_level(cw_gen_t *gen, size_t level)
{
	return cw_tq_wait_for_level_internal(gen->tq, level);
}





/**
   \brief Cancel all pending queued tones in a generator, and return to silence

   If there is a tone in progress, the function will wait until this
   last one has completed, then silence the tones.

   \param gen - generator to flush
*/
void cw_gen_flush_queue(cw_gen_t *gen)
{
	/* This function locks and unlocks mutex. */
	cw_tq_flush_internal(gen->tq);

	/* Force silence on the speaker anyway, and stop any background
	   soundcard tone generation. */
	cw_gen_silence_internal(gen);

	return;
}






/**
   \brief Return char string with console device path

   Returned pointer is owned by library.

   \param gen

   \return char string with current console device path
*/
const char *cw_gen_get_console_device(cw_gen_t *gen)
{
	cw_assert (gen, "gen is NULL");
	return gen->audio_device;
}





/**
   \brief Return char string with soundcard device name/path

   Returned pointer is owned by library.

   \param gen

   \return char string with current soundcard device name or device path
*/
const char *cw_gen_get_soundcard_device(cw_gen_t *gen)
{
	cw_assert (gen, "gen is NULL");
	return gen->audio_device;
}





/**
   \brief Wait for generator's tone queue to drain

   testedin::test_tone_queue_1()
   testedin::test_tone_queue_2()
   testedin::test_tone_queue_3()

   \param gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_wait_for_queue(cw_gen_t *gen)
{
	return cw_tq_wait_for_tone_queue_internal(gen->tq);
}





size_t cw_gen_get_queue_length(cw_gen_t *gen)
{
	return cw_tq_length_internal(gen->tq);
}





int cw_gen_register_low_level_callback(cw_gen_t *gen, cw_queue_low_callback_t callback_func, void *callback_arg, size_t level)
{
	return cw_tq_register_low_level_callback_internal(gen->tq, callback_func, callback_arg, level);
}





int cw_gen_wait_for_tone(cw_gen_t *gen)
{
	return cw_tq_wait_for_tone_internal(gen->tq);
}





bool cw_gen_is_queue_full(cw_gen_t *gen)
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
unsigned int test_cw_gen_new_delete(void)
{
	int p = fprintf(stdout, "libcw/gen: cw_gen_new/start/stop/delete():\n");
	fflush(stdout);

	/* Arbitrary number of calls to a set of tested functions. */
	int n = 100;

	/* new() + delete() */
	for (int i = 0; i < n; i++) {
		fprintf(stderr, "libcw/gen: generator test 1/4, loop #%d/%d\n", i, n);

		cw_gen_t *gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		cw_assert (gen, "failed to initialize generator (loop #%d)", i);

		/* Try to access some fields in cw_gen_t just to be
		   sure that the gen has been allocated properly. */
		cw_assert (gen->buffer_sub_start == 0, "buffer_sub_start in new generator is not at zero");
		gen->buffer_sub_stop = gen->buffer_sub_start + 10;
		cw_assert (gen->buffer_sub_stop == 10, "buffer_sub_stop didn't store correct new value");

		cw_assert (gen->client.name == (char *) NULL, "initial value of generator's client name is not NULL");

		cw_assert (gen->tq, "tone queue is NULL");

		cw_gen_delete(&gen);
		cw_assert (gen == NULL, "delete() didn't set the pointer to NULL (loop #%d)", i);
	}


	n = 5;


	/* new() + start() + delete() (skipping stop() on purpose). */
	for (int i = 0; i < n; i++) {
		fprintf(stderr, "libcw/gen: generator test 2/4, loop #%d/%d\n", i, n);

		cw_gen_t *gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		cw_assert (gen, "failed to initialize generator (loop #%d)", i);

		int rv = cw_gen_start(gen);
		cw_assert (rv, "failed to start generator (loop #%d)", i);

		cw_gen_delete(&gen);
		cw_assert (gen == NULL, "delete() didn't set the pointer to NULL (loop #%d)", i);
	}


	/* new() + stop() + delete() (skipping start() on purpose). */
	fprintf(stderr, "libcw/gen: generator test 3/4\n");
	for (int i = 0; i < n; i++) {
		cw_gen_t *gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		cw_assert (gen, "failed to initialize generator (loop #%d)", i);

		int rv = cw_gen_stop(gen);
		cw_assert (rv, "failed to stop generator (loop #%d)", i);

		cw_gen_delete(&gen);
		cw_assert (gen == NULL, "delete() didn't set the pointer to NULL (loop #%d)", i);
	}


	/* Inner loop limit. */
	int m = n;


	/* new() + start() + stop() + delete() */
	for (int i = 0; i < n; i++) {
		fprintf(stderr, "libcw/gen: generator test 4/4, loop #%d/%d\n", i, n);

		cw_gen_t *gen = cw_gen_new(CW_AUDIO_NULL, NULL);
		cw_assert (gen, "failed to initialize generator (loop #%d)", i);

		for (int j = 0; j < m; j++) {
			int rv = cw_gen_start(gen);
			cw_assert (rv, "failed to start generator (loop #%d-%d)", i, j);

			rv = cw_gen_stop(gen);
			cw_assert (rv, "failed to stop generator (loop #%d-%d)", i, j);
		}

		cw_gen_delete(&gen);
		cw_assert (gen == NULL, "delete() didn't set the pointer to NULL (loop #%d)", i);
	}


	p = fprintf(stdout, "libcw/gen: cw_gen_new/start/stop/delete():");
	CW_TEST_PRINT_TEST_RESULT(false, p);
	fflush(stdout);

	return 0;
}




unsigned int test_cw_gen_set_tone_slope(void)
{
	int p = fprintf(stdout, "libcw/gen: cw_gen_set_tone_slope():");

	int audio_system = CW_AUDIO_NULL;

	/* Test 0: test property of newly created generator. */
	{
		cw_gen_t *gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test 0");


		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RAISED_COSINE,
			   "new generator has unexpected initial slope shape %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == CW_AUDIO_SLOPE_LEN,
			   "new generator has unexpected initial slope length %d", gen->tone_slope.len);


		cw_gen_delete(&gen);
	}



	/* Test A: pass conflicting arguments.

	   "A: If you pass to function conflicting values of \p
	   slope_shape and \p slope_len, the function will return
	   CW_FAILURE. These conflicting values are rectangular slope
	   shape and larger than zero slope length. You just can't
	   have rectangular slopes that have non-zero length." */
	{
		cw_gen_t *gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test A");


		int rv = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 10);
		cw_assert (!rv, "function accepted conflicting arguments");


		cw_gen_delete(&gen);
	}



	/* Test B: pass '-1' as both arguments.

	   "B: If you pass to function '-1' as value of both \p
	   slope_shape and \p slope_len, the function won't change
	   any of the related two generator's parameters." */
	{
		cw_gen_t *gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test B");


		int shape_before = gen->tone_slope.shape;
		int len_before = gen->tone_slope.len;

		int rv = cw_gen_set_tone_slope(gen, -1, -1);
		cw_assert (rv, "failed to set tone slope");

		cw_assert (gen->tone_slope.shape == shape_before,
			   "tone slope shape changed from %d to %d", shape_before, gen->tone_slope.shape);

		cw_assert (gen->tone_slope.len == len_before,
			   "tone slope length changed from %d to %d", len_before, gen->tone_slope.len);


		cw_gen_delete(&gen);
	}



	/* Test C1

	   "C1: If you pass to function '-1' as value of either \p
	   slope_shape or \p slope_len, the function will attempt to
	   set only this generator's parameter that is different than
	   '-1'." */
	{
		cw_gen_t *gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test C1");


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;


		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "new generator has unexpected initial slope shape %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "new generator has unexpected initial slope length %d", gen->tone_slope.len);


		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_LINEAR;
		int rv = cw_gen_set_tone_slope(gen, expected_shape, -1);
		cw_assert (rv, "failed to set linear slope shape with unchanged slope length");

		/* At this point only slope shape should be updated. */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "failed to set new shape of slope; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "failed to preserve slope length; length is %d", gen->tone_slope.len);


		/* Set only new slope length. */
		expected_len = 30;
		rv = cw_gen_set_tone_slope(gen, -1, expected_len);
		cw_assert (rv, "failed to set positive slope length with unchanged slope shape");

		/* At this point only slope length should be updated
		   (compared to previous function call). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "failed to preserve shape of slope; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "failed to set new slope length; length is %d", gen->tone_slope.len);


		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_SINE;
		rv = cw_gen_set_tone_slope(gen, expected_shape, -1);
		cw_assert (rv, "failed to set new slope shape with unchanged slope length");

		/* At this point only slope shape should be updated
		   (compared to previous function call). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "failed to set new shape of slope; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "failed to preserve slope length; length is %d", gen->tone_slope.len);


		cw_gen_delete(&gen);
	}



	/* Test C2

	   "C2: However, if selected slope shape is rectangular,
	   function will set generator's slope length to zero, even if
	   value of \p slope_len is '-1'." */
	{
		cw_gen_t *gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test C2");


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;


		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "new generator has unexpected initial slope shape %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "new generator has unexpected initial slope length %d", gen->tone_slope.len);


		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_RECTANGULAR;
		expected_len = 0; /* Even though we won't pass this to function, this is what we expect to get after this call. */
		int rv = cw_gen_set_tone_slope(gen, expected_shape, -1);
		cw_assert (rv, "failed to set rectangular slope shape with unchanged slope length");

		/* At this point slope shape AND slope length should
		   be updated (slope length is updated only because of
		   requested rectangular slope shape). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "failed to set new shape of slope; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "failed to get expected slope length; length is %d", gen->tone_slope.len);


		cw_gen_delete(&gen);
	}



	/* Test D

	   "D: Notice that the function allows non-rectangular slope
	   shape with zero length of the slopes. The slopes will be
	   non-rectangular, but just unusually short." */
	{
		cw_gen_t *gen = cw_gen_new(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test D");


		int rv = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_LINEAR, 0);
		cw_assert (rv, "failed to set linear slope with zero length");
		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_LINEAR,
			   "failed to set linear slope shape; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == 0,
			   "failed to set zero slope length; length is %d", gen->tone_slope.len);


		rv = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, 0);
		cw_assert (rv, "failed to set raised cosine slope with zero length");
		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RAISED_COSINE,
			   "failed to set raised cosine slope shape; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == 0,
			   "failed to set zero slope length; length is %d", gen->tone_slope.len);


		rv = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_SINE, 0);
		cw_assert (rv, "failed to set sine slope with zero length");
		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_SINE,
			   "failed to set sine slope shape; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == 0,
			   "failed to set zero slope length; length is %d", gen->tone_slope.len);


		rv = cw_gen_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 0);
		cw_assert (rv, "failed to set rectangular slope with zero length");
		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR,
			   "failed to set rectangular slope shape; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == 0,
			   "failed to set zero slope length; length is %d", gen->tone_slope.len);


		cw_gen_delete(&gen);
	}


	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/* Test some assertions about CW_TONE_SLOPE_SHAPE_*

   Code in this file depends on the fact that these values are
   different than -1. I think that ensuring that they are in general
   small, non-negative values is a good idea.

   I'm testing these values to be sure that when I get a silly idea to
   modify them, the test will catch this modification.
*/
unsigned int test_cw_gen_tone_slope_shape_enums(void)
{
	int p = fprintf(stdout, "libcw/gen: CW_TONE_SLOPE_SHAPE_*:");

	cw_assert (CW_TONE_SLOPE_SHAPE_LINEAR >= 0,        "CW_TONE_SLOPE_SHAPE_LINEAR is negative: %d",        CW_TONE_SLOPE_SHAPE_LINEAR);
	cw_assert (CW_TONE_SLOPE_SHAPE_RAISED_COSINE >= 0, "CW_TONE_SLOPE_SHAPE_RAISED_COSINE is negative: %d", CW_TONE_SLOPE_SHAPE_RAISED_COSINE);
	cw_assert (CW_TONE_SLOPE_SHAPE_SINE >= 0,          "CW_TONE_SLOPE_SHAPE_SINE is negative: %d",          CW_TONE_SLOPE_SHAPE_SINE);
	cw_assert (CW_TONE_SLOPE_SHAPE_RECTANGULAR >= 0,   "CW_TONE_SLOPE_SHAPE_RECTANGULAR is negative: %d",   CW_TONE_SLOPE_SHAPE_RECTANGULAR);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/* Version of test_cw_gen_forever() to be used in libcw_test_internal
   test executable.

   It's not a test of a "forever" function, but of "forever"
   functionality.
*/
unsigned int test_cw_gen_forever_internal(void)
{
	int seconds = 2;
	int p = fprintf(stdout, "libcw/gen: forever tone (%d seconds):", seconds);
	fflush(stdout);

	unsigned int rv = test_cw_gen_forever_sub(2, CW_AUDIO_NULL, (const char *) NULL);
	cw_assert (rv == 0, "\"forever\" test failed");

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





unsigned int test_cw_gen_forever_sub(int seconds, int audio_system, const char *audio_device)
{
	cw_gen_t *gen = cw_gen_new(audio_system, audio_device);
	cw_assert (gen, "ERROR: failed to create generator\n");
	cw_gen_start(gen);
	sleep(1);

	cw_tone_t tone;
	/* Just some acceptable values. */
	int len = 100; /* [us] */
	int freq = 500;

	CW_TONE_INIT(&tone, freq, len, CW_SLOPE_MODE_RISING_SLOPE);
	cw_tq_enqueue_internal(gen->tq, &tone);

	CW_TONE_INIT(&tone, freq, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
	tone.is_forever = true;
	int rv = cw_tq_enqueue_internal(gen->tq, &tone);

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

	CW_TONE_INIT(&tone, freq, len, CW_SLOPE_MODE_FALLING_SLOPE);
	rv = cw_tq_enqueue_internal(gen->tq, &tone);
	cw_assert (rv, "failed to enqueue last tone");

	cw_gen_delete(&gen);

	return 0;
}





/* cw_gen_get_timing_parameters_internal() is independent of audio
   system, so it should be ok to test it with CW_AUDIO_NULL only. */
unsigned int test_cw_gen_get_timing_parameters_internal(void)
{
	int p = fprintf(stdout, "libcw/gen: test_cw_gen_get_timing_parameters_internal:");
	fflush(stdout);

	int initial = -5;

	int dot_len = initial;
	int dash_len = initial;
	int eom_space_len = initial;
	int eoc_space_len = initial;
	int eow_space_len = initial;
	int additional_space_len = initial;
	int adjustment_space_len = initial;


	cw_gen_t *gen = cw_gen_new(CW_AUDIO_NULL, NULL);
	cw_assert (gen, "failed to create new generator");

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

	cw_assert (dot_len != initial, "failed to get dot_len, is now %d", dot_len);
	cw_assert (dash_len != initial, "failed to get dash_len, is now %d", dash_len);
	cw_assert (eom_space_len != initial, "failed to get eom_space_len, is now %d", eom_space_len);
	cw_assert (eoc_space_len != initial, "failed to get eoc_space_len, is now %d", eoc_space_len);
	cw_assert (eow_space_len != initial, "failed to get eow_space_len, is now %d", eow_space_len);
	cw_assert (additional_space_len != initial, "failed to get additional_space_len, is now %d", additional_space_len);
	cw_assert (adjustment_space_len != initial, "failed to get adjustment_space_len, is now %d", adjustment_space_len);


	cw_gen_delete(&gen);


	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/* Parameter getters and setters are independent of audio system, so
   they can be tested just with CW_AUDIO_NULL.  This is even more true
   for limit getters, which don't require a generator at all. */
unsigned int test_cw_gen_parameter_getters_setters(void)
{
	int p = fprintf(stdout, "libcw/gen: basic parameter getters and setters:");
	fflush(stdout);

	cw_gen_t *gen = cw_gen_new(CW_AUDIO_NULL, "");
	cw_assert (gen, "failed to create new generator");

	/* Test setting and getting of some basic parameters. */

	int off_limits = 10000;

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int *min, int *max);
		int (* set_new_value)(cw_gen_t *gen, int new_value);
		int (* get_value)(cw_gen_t *gen);

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

		int status;
		int value = 0;

		/* Get limits of values to be tested. */
		/* Notice that getters of parameter limits are tested
		   in test_cw_get_x_limits(). */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);

		cw_assert (test_data[i].min > -off_limits, "%s: failed to get low limit, returned value = %d", test_data[i].name, test_data[i].min);
		cw_assert (test_data[i].max <  off_limits, "%s: failed to get high limit, returned value = %d", test_data[i].name, test_data[i].max);



		/* Test out-of-range value lower than minimum. */
		errno = 0;
		value = test_data[i].min - 1;
		status = test_data[i].set_new_value(gen, value);

		cw_assert (status == CW_FAILURE, "%s: setting value below minimum succeeded\n"
			   "minimum is %d, attempted value is %d",
			   test_data[i].name, test_data[i].min, value);
		cw_assert (errno == EINVAL, "%s: setting value below minimum didn't result in EINVAL\n"
			   "minimum is %d, attempted value is %d",
			   test_data[i].name, test_data[i].min, value);



		/* Test out-of-range value higher than maximum. */
		errno = 0;
		value = test_data[i].max + 1;
		status = test_data[i].set_new_value(gen, value);

		cw_assert (status == CW_FAILURE, "%s: setting value above minimum succeeded\n"
			   "maximum is %d, attempted value is %d",
			   test_data[i].name, test_data[i].min, value);
		cw_assert (errno == EINVAL, "%s: setting value above maximum didn't result in EINVAL\n"
			   "maximum is %d, attempted value is %d",
			   test_data[i].name, test_data[i].min, value);



		/* Test in-range values. Set with setter and then read back with getter. */
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			test_data[i].set_new_value(gen, j);

			cw_assert (test_data[i].get_value(gen) == j, "%s: setting value in-range failed for value = %d", test_data[i].name, j);
		}
	}


	cw_gen_delete(&gen);


	CW_TEST_PRINT_TEST_RESULT(false, p);


	return 0;
}





#endif /* #ifdef LIBCW_UNIT_TESTS */
