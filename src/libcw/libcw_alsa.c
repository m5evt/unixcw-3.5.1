/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)

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
   \file libcw_alsa.c

   \brief ALSA audio sink.
*/




#include "config.h"
#include "libcw_debug.h"




#define MSG_PREFIX "libcw/alsa: "




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




#ifdef LIBCW_WITH_ALSA




#include <dlfcn.h> /* dlopen() and related symbols */
#include <alsa/asoundlib.h>




#include "libcw.h"
#include "libcw_alsa.h"
#include "libcw_utils.h"
#include "libcw_gen.h"




#define CW_ALSA_HW_BUFFER_CONFIG  0  /* set up hw buffer/period parameters; unnecessary and probably harmful */




extern const unsigned int cw_supported_sample_rates[];




/* Constants specific to ALSA audio system configuration */
static const snd_pcm_format_t CW_ALSA_SAMPLE_FORMAT = SND_PCM_FORMAT_S16; /* "Signed 16 bit CPU endian"; I'm guessing that "CPU endian" == "native endianess" */


static int  cw_alsa_set_hw_params_internal(cw_gen_t *gen, snd_pcm_hw_params_t * hw_params);
static int  cw_alsa_dlsym_internal(void *handle);
static int  cw_alsa_write_internal(cw_gen_t *gen);
static int  cw_alsa_debug_evaluate_write_internal(cw_gen_t *gen, int rv);
static int  cw_alsa_open_device_internal(cw_gen_t *gen);
static void cw_alsa_close_device_internal(cw_gen_t *gen);


#ifdef LIBCW_WITH_DEV
static int cw_alsa_print_params_internal(snd_pcm_hw_params_t *hw_params);
#endif



/*
  FIXME: verify how this data structure is handled when there are
  many generators.
  How many times the structure is set?
  How many times it's closed?
  Is it closed for all generators when first of these generators is destroyed?
  Do we need a reference counter for this structure?
*/
static struct {
	void *handle;

	int (* snd_pcm_open)(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode);
	int (* snd_pcm_close)(snd_pcm_t *pcm);
	int (* snd_pcm_prepare)(snd_pcm_t *pcm);
	int (* snd_pcm_drop)(snd_pcm_t *pcm);
	snd_pcm_sframes_t (* snd_pcm_writei)(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);

	const char *(* snd_strerror)(int errnum);

	int (* snd_pcm_hw_params_malloc)(snd_pcm_hw_params_t **ptr);
	int (* snd_pcm_hw_params_any)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
	int (* snd_pcm_hw_params_set_format)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_format_t val);
	int (* snd_pcm_hw_params_set_rate_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
	int (* snd_pcm_hw_params_set_access)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_access_t _access);
	int (* snd_pcm_hw_params_set_channels)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val);
	int (* snd_pcm_hw_params)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
	int (* snd_pcm_hw_params_get_periods)(const snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
	int (* snd_pcm_hw_params_get_period_size)(const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *frames, int *dir);
	int (* snd_pcm_hw_params_get_period_size_min)(const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *frames, int *dir);
	int (* snd_pcm_hw_params_get_buffer_size)(const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val);
} cw_alsa = {
	.handle = NULL,

	.snd_pcm_open = NULL,
	.snd_pcm_close = NULL,
	.snd_pcm_prepare = NULL,
	.snd_pcm_drop = NULL,
	.snd_pcm_writei = NULL,

	.snd_strerror = NULL,

	.snd_pcm_hw_params_malloc = NULL,
	.snd_pcm_hw_params_any = NULL,
	.snd_pcm_hw_params_set_format = NULL,
	.snd_pcm_hw_params_set_rate_near = NULL,
	.snd_pcm_hw_params_set_access = NULL,
	.snd_pcm_hw_params_set_channels = NULL,
	.snd_pcm_hw_params = NULL,
	.snd_pcm_hw_params_get_periods = NULL,
	.snd_pcm_hw_params_get_period_size = NULL,
	.snd_pcm_hw_params_get_period_size_min = NULL,
	.snd_pcm_hw_params_get_buffer_size = NULL
};




/**
   \brief Check if it is possible to open ALSA output

   Function first tries to load ALSA library, and then does a test
   opening of ALSA output, but it closes it before returning.

   \reviewed on 2017-02-05

   \param device - name of ALSA device to be used; if NULL then library will use default device.

   \return true if opening ALSA output succeeded;
   \return false if opening ALSA output failed;
*/
bool cw_is_alsa_possible(const char *device)
{
	const char *library_name = "libasound.so.2";
	if (!cw_dlopen_internal(library_name, &(cw_alsa.handle))) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "is possible: can't access ALSA library '%s'", library_name);
		return false;
	}

	int rv = cw_alsa_dlsym_internal(cw_alsa.handle);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "is possible: failed to resolve ALSA symbol #%d, can't correctly load ALSA library", rv);
		dlclose(cw_alsa.handle);
		return false;
	}

	const char *dev = device ? device : CW_DEFAULT_ALSA_DEVICE;
	snd_pcm_t *alsa_handle = NULL;
	rv = cw_alsa.snd_pcm_open(&alsa_handle,
				  dev,                     /* name */
				  SND_PCM_STREAM_PLAYBACK, /* stream (playback/capture) */
				  0);                      /* mode, 0 | SND_PCM_NONBLOCK | SND_PCM_ASYNC */
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "is possible: can't open ALSA device '%s'", dev);
		dlclose(cw_alsa.handle);
		return false;
	} else {
		cw_alsa.snd_pcm_close(alsa_handle);
		return true;
	}
}




/**
   \brief Configure given generator to work with ALSA audio sink

   \reviewed on 2017-02-05

   \param gen - generator
   \param dev - ALSA device to use

   \return CW_SUCCESS
*/
int cw_alsa_configure(cw_gen_t *gen, const char *device)
{
	gen->audio_system = CW_AUDIO_ALSA;
	cw_gen_set_audio_device_internal(gen, device);

	gen->open_device  = cw_alsa_open_device_internal;
	gen->close_device = cw_alsa_close_device_internal;
	gen->write        = cw_alsa_write_internal;

	return CW_SUCCESS;
}




/**
   \brief Write generated samples to ALSA audio sink configured and opened for generator

   \reviewed on 2017-02-05

   \param gen - generator

   \return CW_SUCCESS on success
   \return CW_FAILURE otherwise
*/
int cw_alsa_write_internal(cw_gen_t *gen)
{
	assert (gen);
	assert (gen->audio_system == CW_AUDIO_ALSA);

	/* Send audio buffer to ALSA.
	   Size of correct and current data in the buffer is the same as
	   ALSA's period, so there should be no underruns */
	int rv = cw_alsa.snd_pcm_writei(gen->alsa_data.handle, gen->buffer, gen->buffer_n_samples);
	rv = cw_alsa_debug_evaluate_write_internal(gen, rv); /* TODO: fix reusing rv variable. */
	/*
	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
		      MSG_PREFIX "write: written %d/%d samples", rv, gen->buffer_n_samples);
	*/
	return rv;
}




/**
   \brief Open ALSA output, associate it with given generator

   You must use cw_gen_set_audio_device_internal() before calling
   this function. Otherwise generator \p gen won't know which device to open.

   \reviewed on 2017-02-05

   \param gen - generator

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_alsa_open_device_internal(cw_gen_t *gen)
{
	int rv = cw_alsa.snd_pcm_open(&gen->alsa_data.handle,
				      gen->audio_device,       /* name */
				      SND_PCM_STREAM_PLAYBACK, /* stream (playback/capture) */
				      0);                      /* mode, 0 | SND_PCM_NONBLOCK | SND_PCM_ASYNC */
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "open: can't open ALSA device '%s'", gen->audio_device);
		return CW_FAILURE;
	}
	/*
	rv = snd_pcm_nonblock(gen->alsa_data.handle, 0);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR, MSG_PREFIX "can't set block for ALSA handle");
		return CW_FAILURE;
	}
	*/

	/* TODO: move this to cw_alsa_set_hw_params_internal(),
	   deallocate hw_params. */
	snd_pcm_hw_params_t *hw_params = NULL;
	rv = cw_alsa.snd_pcm_hw_params_malloc(&hw_params);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "open: can't allocate memory for ALSA hw params");
		return CW_FAILURE;
	}

	rv = cw_alsa_set_hw_params_internal(gen, hw_params);
	if (rv != CW_SUCCESS) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "open: can't set ALSA hw params");
		return CW_FAILURE;
	}

	rv = cw_alsa.snd_pcm_prepare(gen->alsa_data.handle);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "open: can't prepare ALSA handler");
		return CW_FAILURE;
	}

	/* Get size for data buffer */
	snd_pcm_uframes_t frames; /* period size in frames */
	int dir = 1;
	rv = cw_alsa.snd_pcm_hw_params_get_period_size_min(hw_params, &frames, &dir);
	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
		      MSG_PREFIX "open: rv = %d, ALSA buffer size would be %u frames", rv, (unsigned int) frames);

	/* The linker (?) that I use on Debian links libcw against
	   old version of get_period_size(), which returns
	   period size as return value. This is a workaround. */
	if (rv > 1) {
		gen->buffer_n_samples = rv;
	} else {
		gen->buffer_n_samples = frames;
	}

#if CW_DEV_RAW_SINK
	gen->dev_raw_sink = open("/tmp/cw_file.alsa.raw", O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK);
	if (gen->dev_raw_sink == -1) {
		fprintf(stderr, MSG_PREFIX "open: failed to open dev raw sink file: '%s'\n", strerror(errno));
	}
#endif

	return CW_SUCCESS;
}




/**
   \brief Close ALSA device associated with given generator

   \reviewed on 2017-02-05

   \param gen
*/
void cw_alsa_close_device_internal(cw_gen_t *gen)
{
	/* "Stop a PCM dropping pending frames. " */
	cw_alsa.snd_pcm_drop(gen->alsa_data.handle);
	cw_alsa.snd_pcm_close(gen->alsa_data.handle);

	gen->audio_device_is_open = false;

	if (cw_alsa.handle) {
		dlclose(cw_alsa.handle);
	}

#if CW_DEV_RAW_SINK
	if (gen->dev_raw_sink != -1) {
		close(gen->dev_raw_sink);
		gen->dev_raw_sink = -1;
	}
#endif
	return;
}




/**
   \brief Handle value returned by ALSA's write function (snd_pcm_writei)

   If specific errors occurred during write, audio sink is reset by this function.

   \reviewed on 2017-02-05

   \param gen - generator
   \param rv - value returned by snd_pcm_writei()

   \return CW_SUCCESS if write was successful
   \return CW_FAILURE otherwise
*/
int cw_alsa_debug_evaluate_write_internal(cw_gen_t *gen, int rv)
{
	if (rv == -EPIPE) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING,
			      MSG_PREFIX "write: underrun");
		cw_alsa.snd_pcm_prepare(gen->alsa_data.handle); /* Reset audio sink. */

	} else if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING,
			      MSG_PREFIX "write: writei: %s", cw_alsa.snd_strerror(rv));
		cw_alsa.snd_pcm_prepare(gen->alsa_data.handle);  /* Reset audio sink. */

	} else if (rv != gen->buffer_n_samples) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING,
			      MSG_PREFIX "short write, %d != %d", rv, gen->buffer_n_samples);
	} else {
		return CW_SUCCESS;
	}

	return CW_FAILURE;
}




/**
   \brief Set up hardware buffer parameters of ALSA sink

   \reviewed on 2017-02-05

   \param gen - generator with ALSA handle set up
   \param params - allocated hw params data structure to be used

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_alsa_set_hw_params_internal(cw_gen_t *gen, snd_pcm_hw_params_t *hw_params)
{
	/* Get current hw configuration. */
	int rv = cw_alsa.snd_pcm_hw_params_any(gen->alsa_data.handle, hw_params);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "set hw params: can't get current hw params: %s", cw_alsa.snd_strerror(rv));
		return CW_FAILURE;
	}


	/* Set the sample format */
	rv = cw_alsa.snd_pcm_hw_params_set_format(gen->alsa_data.handle, hw_params, CW_ALSA_SAMPLE_FORMAT);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "set hw params: can't set sample format: %s", cw_alsa.snd_strerror(rv));
		return CW_FAILURE;
	}


	int dir = 0;

	/* Set the sample rate (may set/influence/modify "period size"). */
	unsigned int rate = 0;
	bool success = false;
	rv = 0;
	for (int i = 0; cw_supported_sample_rates[i]; i++) {
		rate = cw_supported_sample_rates[i];
		rv = cw_alsa.snd_pcm_hw_params_set_rate_near(gen->alsa_data.handle, hw_params, &rate, &dir);
		if (!rv) {
			if (rate != cw_supported_sample_rates[i]) {
				cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING, MSG_PREFIX "imprecise sample rate:");
				cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING, MSG_PREFIX "asked for: %u", cw_supported_sample_rates[i]);
				cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING, MSG_PREFIX "got:       %u", rate);
			}
			success = true;
			gen->sample_rate = rate;
			break;
		}
	}

	if (!success) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "set hw params: can't get sample rate: %s", cw_alsa.snd_strerror(rv));
		return CW_FAILURE;
	} else {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
			      MSG_PREFIX "set hw params: sample rate: %d", gen->sample_rate);
	}

	/* Set PCM access type */
	rv = cw_alsa.snd_pcm_hw_params_set_access(gen->alsa_data.handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "set hw params: can't set access type: %s", cw_alsa.snd_strerror(rv));
		return CW_FAILURE;
	}

	/* Set number of channels */
	rv = cw_alsa.snd_pcm_hw_params_set_channels(gen->alsa_data.handle, hw_params, CW_AUDIO_CHANNELS);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "set hw params: can't set number of channels: %s", cw_alsa.snd_strerror(rv));
		return CW_FAILURE;
	}


	/* Don't try to over-configure ALSA, it would be a pointless
	   exercise. See comment from this article, starting
	   with "This is my soundcard initialization function":
	   http://stackoverflow.com/questions/3345083/correctly-sizing-alsa-buffers-weird-api
	   Poster sets basic audio playback parameters (channels, sampling
	   rate, sample format), saves the config (with snd_pcm_hw_params()),
	   and then only queries ALSA handle for period size and period
	   time.

	   It turns out that it works in our case: basic hw configuration
	   plus getting period size (I don't need period time).

	   Period size seems to be the most important, and most useful
	   data that I need from configured ALSA handle - this is the
	   size of audio buffer which I can fill with my data and send
	   it down to ALSA internals (possibly without worrying about
	   underruns; if I understand correctly - if I send to ALSA
	   chunks of data of proper size then I don't have to worry
	   about underruns). */

#if CW_ALSA_HW_BUFFER_CONFIG && defined(HAVE_SND_PCM_HW_PARAMS_TEST_BUFFER_SIZE) && defined(HAVE_SND_PCM_HW_PARAMS_TEST_PERIODS)

	/*
	  http://equalarea.com/paul/alsa-audio.html:

	  Buffer size:
	  This determines how large the hardware buffer is.
	  It can be specified in units of time or frames.

	  Interrupt interval:
	  This determines how many interrupts the interface will generate
	  per complete traversal of its hardware buffer. It can be set
	  either by specifying a number of periods, or the size of a
	  period. Since this determines the number of frames of space/data
	  that have to accumulate before the interface will interrupt
	  the computer. It is central in controlling latency.

	  http://www.alsa-project.org/main/index.php/FramesPeriods

	  "
	  "frame" represents the unit, 1 frame = # channels x sample_bytes.
	  In case of stereo, 2 bytes per sample, 1 frame corresponds to 2 channels x 2 bytes = 4 bytes.

	  "periods" is the number of periods in a ring-buffer.
	  In OSS, called "fragments".

	  So,
	  - buffer_size = period_size * periods
	  - period_bytes = period_size * bytes_per_frame
	  - bytes_per_frame = channels * bytes_per_sample

	  The "period" defines the frequency to update the status,
	  usually via the invocation of interrupts.  The "period_size"
	  defines the frame sizes corresponding to the "period time".
	  This term corresponds to the "fragment size" on OSS.  On major
	  sound hardwares, a ring-buffer is divided to several parts and
	  an irq is issued on each boundary. The period_size defines the
	  size of this chunk."

	  OSS            ALSA           definition
	  fragment       period         basic chunk of data sent to hw buffer

	*/

	{
		/* Test and attempt to set buffer size */

		snd_pcm_uframes_t accepted = 0; /* buffer size in frames  */
		dir = 0;
		for (snd_pcm_uframes_t val = 0; val < 10000; val++) {
			rv = cw_alsa.snd_pcm_hw_params_test_buffer_size(gen->alsa_data.handle, hw_params, val);
			if (rv == 0) {
				cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
					      MSG_PREFIX "set hw params: accepted buffer size: %u", (unsigned int) accepted);
				/* Accept only the smallest available buffer size */
				accepted = val;
				break;
			}
		}

		if (accepted > 0) {
			rv = cw_alsa.snd_pcm_hw_params_set_buffer_size(gen->alsa_data.handle, hw_params, accepted);
			if (rv < 0) {
				cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
					      MSG_PREFIX "set hw params: can't set accepted buffer size %u: %s", (unsigned int) accepted, cw_alsa.snd_strerror(rv));
			}
		} else {
			cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
				      MSG_PREFIX "set hw params: no accepted buffer size");
		}
	}

	{
		/* Test and attempt to set number of periods */

		dir = 0;
		unsigned int accepted = 0; /* Number of periods per buffer. */
		/* This limit should be enough, "accepted" on my machine is 8. */
		const unsigned int n_periods_max = 30;
		for (unsigned int val = 1; val < n_periods_max; val++) {
			rv = cw_alsa.snd_pcm_hw_params_test_periods(gen->alsa_data.handle, hw_params, val, dir);
			if (rv == 0) {
				accepted = val;
				cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
					      MSG_PREFIX "set hw params: accepted number of periods: %d", accepted);
			}
		}
		if (accepted > 0) {
			rv = cw_alsa.snd_pcm_hw_params_set_periods(gen->alsa_data.handle, hw_params, accepted, dir);
			if (rv < 0) {
				cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
					      MSG_PREFIX "set hw params: can't set accepted number of periods %d: %s", accepted, cw_alsa.snd_strerror(rv));
			}
		} else {
			cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
				      MSG_PREFIX "set hw params: no accepted number of periods");
		}
	}

	{
		/* Test period size */
		dir = 0;
		for (snd_pcm_uframes_t val = 0; val < 100000; val++) {
			rv = cw_alsa.snd_pcm_hw_params_test_period_size(gen->alsa_data.handle, hw_params, val, dir);
			if (rv == 0) {
				cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
					      MSG_PREFIX "set hw params: accepted period size: %lu", val);
				// break; /* TODO: do we break here or not? */
			}
		}
	}

	{
		/* Test buffer time */
		dir = 0;
		for (unsigned int val = 0; val < 100000; val++) {
			rv = cw_alsa.snd_pcm_hw_params_test_buffer_time(gen->alsa_data.handle, hw_params, val, dir);
			if (rv == 0) {
				cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
					      MSG_PREFIX "set hw params: accepted buffer time: %d", val);
				// break; /* TODO: do we break here or not? */
			}
		}
	}
#endif /* #if CW_ALSA_HW_BUFFER_CONFIG */

	/* Save hw parameters to device */
	rv = cw_alsa.snd_pcm_hw_params(gen->alsa_data.handle, hw_params);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "set hw params: can't save hw parameters: %s", cw_alsa.snd_strerror(rv));
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}




#ifdef LIBCW_WITH_DEV




/**
   \brief Debug function printing ALSA setup

   \reviewed on 2017-02-05

   \param hw_params - ALSA configuration; TODO: do we need to pass this argument? Can't it be function's local variable?
*/
int cw_alsa_print_params_internal(snd_pcm_hw_params_t *hw_params)
{
	unsigned int val = 0;
	int dir = 0;

	int rv = cw_alsa.snd_pcm_hw_params_get_periods(hw_params, &val, &dir);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "can't get 'periods': %s", cw_alsa.snd_strerror(rv));
	} else {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
			      MSG_PREFIX "'periods' = %u", val);
	}

	snd_pcm_uframes_t period_size = 0;
	rv = cw_alsa.snd_pcm_hw_params_get_period_size(hw_params, &period_size, &dir);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "can't get 'period size': %s", cw_alsa.snd_strerror(rv));
	} else {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
			      MSG_PREFIX "'period size' = %u", (unsigned int) period_size);
	}

	snd_pcm_uframes_t buffer_size;
	rv = cw_alsa.snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);
	if (rv < 0) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      MSG_PREFIX "can't get buffer size: %s", cw_alsa.snd_strerror(rv));
	} else {
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
			      MSG_PREFIX "'buffer size' = %u", (unsigned int) buffer_size);
	}

	return CW_SUCCESS;
}




#endif // #ifdef LIBCW_WITH_DEV




/**
   \brief Resolve/get symbols from ALSA library

   Function resolves/gets addresses of few ALSA functions used by
   libcw and stores them in cw_alsa global variable.

   On failure the function returns negative value, different for every
   symbol that the funciton failed to resolve. Function stops and returns
   on first failure.

   \reviewed on 2017-02-05

   \param handle - handle to open ALSA library

   \return 0 on success
   \return negative value on failure
*/
static int cw_alsa_dlsym_internal(void *handle)
{
	*(void **) &(cw_alsa.snd_pcm_open)    = dlsym(handle, "snd_pcm_open");
	if (!cw_alsa.snd_pcm_open)    { return -1; }

	*(void **) &(cw_alsa.snd_pcm_close)   = dlsym(handle, "snd_pcm_close");
	if (!cw_alsa.snd_pcm_close)   { return -2; }

	*(void **) &(cw_alsa.snd_pcm_prepare) = dlsym(handle, "snd_pcm_prepare");
	if (!cw_alsa.snd_pcm_prepare) { return -3; }

	*(void **) &(cw_alsa.snd_pcm_drop)    = dlsym(handle, "snd_pcm_drop");
	if (!cw_alsa.snd_pcm_drop)    { return -4; }

	*(void **) &(cw_alsa.snd_pcm_writei)  = dlsym(handle, "snd_pcm_writei");
	if (!cw_alsa.snd_pcm_writei)  { return -5; }

	*(void **) &(cw_alsa.snd_strerror) = dlsym(handle, "snd_strerror");
	if (!cw_alsa.snd_strerror) { return -10; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_malloc)               = dlsym(handle, "snd_pcm_hw_params_malloc");
	if (!cw_alsa.snd_pcm_hw_params_malloc)              { return -20; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_any)                  = dlsym(handle, "snd_pcm_hw_params_any");
	if (!cw_alsa.snd_pcm_hw_params_any)                 { return -21; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_set_format)           = dlsym(handle, "snd_pcm_hw_params_set_format");
	if (!cw_alsa.snd_pcm_hw_params_set_format)          { return -22; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_set_rate_near)        = dlsym(handle, "snd_pcm_hw_params_set_rate_near");
	if (!cw_alsa.snd_pcm_hw_params_set_rate_near)       { return -23; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_set_access)           = dlsym(handle, "snd_pcm_hw_params_set_access");
	if (!cw_alsa.snd_pcm_hw_params_set_access)          { return -24; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_set_channels)         = dlsym(handle, "snd_pcm_hw_params_set_channels");
	if (!cw_alsa.snd_pcm_hw_params_set_channels)        { return -25; }

	*(void **) &(cw_alsa.snd_pcm_hw_params)                      = dlsym(handle, "snd_pcm_hw_params");
	if (!cw_alsa.snd_pcm_hw_params)                     { return -26; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_get_periods)          = dlsym(handle, "snd_pcm_hw_params_get_periods");
	if (!cw_alsa.snd_pcm_hw_params_get_periods)         { return -27; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_get_period_size)      = dlsym(handle, "snd_pcm_hw_params_get_period_size");
	if (!cw_alsa.snd_pcm_hw_params_get_period_size)     { return -28; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_get_period_size_min)  = dlsym(handle, "snd_pcm_hw_params_get_period_size_min");
	if (!cw_alsa.snd_pcm_hw_params_get_period_size_min) { return -29; }

	*(void **) &(cw_alsa.snd_pcm_hw_params_get_buffer_size)      = dlsym(handle, "snd_pcm_hw_params_get_buffer_size");
	if (!cw_alsa.snd_pcm_hw_params_get_buffer_size)     { return -30; }

	return 0;
}





/**
   \brief Call ALSA's snd_pcm_drop() function for given generator

   \reviewed on 2017-02-05

   \param gen - generator
*/
void cw_alsa_drop(cw_gen_t *gen)
{
	cw_alsa.snd_pcm_drop(gen->alsa_data.handle);

	return;
}




#else /* #ifdef LIBCW_WITH_ALSA */




#include <stdbool.h>
#include "libcw_alsa.h"




bool cw_is_alsa_possible(__attribute__((unused)) const char *device)
{
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
		      MSG_PREFIX "This audio system has been disabled during compilation");
	return false;
}




int cw_alsa_configure(__attribute__((unused)) cw_gen_t *gen, __attribute__((unused)) const char *device)
{
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
		      MSG_PREFIX "This audio system has been disabled during compilation");
	return CW_FAILURE;
}




void cw_alsa_drop(__attribute__((unused)) cw_gen_t *gen)
{
	return;
}




#endif /* #ifdef LIBCW_WITH_ALSA */
