/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_GEN
#define H_LIBCW_GEN





#include "libcw.h"
#include "libcw_pa.h"
#include "libcw_alsa.h"
#include "libcw_tq.h"
#include "libcw_key.h"




/* Allowed values of cw_tone_t.slope_mode.  This is to decide whether
   a tone has slopes at all. If there are any slopes in a tone, there
   can be only rising slope (without falling slope), falling slope
   (without rising slope), or both slopes (i.e. standard slopes).
   These values don't tell anything about shape of slopes (unless you
   consider 'no slopes' a shape ;) ). */
#define CW_SLOPE_MODE_STANDARD_SLOPES   20
#define CW_SLOPE_MODE_NO_SLOPES         21
#define CW_SLOPE_MODE_RISING_SLOPE      22
#define CW_SLOPE_MODE_FALLING_SLOPE     23





/* This is used in libcw_gen and libcw_debug. */
#ifdef LIBCW_WITH_DEV
#define CW_DEV_RAW_SINK           1  /* Create and use /tmp/cw_file.<audio system>.raw file with audio samples written as raw data. */
#define CW_DEV_RAW_SINK_MARKERS   0  /* Put markers in raw data saved to raw sink. */
#else
#define CW_DEV_RAW_SINK           0
#define CW_DEV_RAW_SINK_MARKERS   0
#endif





/* Forward declarations of data types. */
struct cw_key_struct;




/* Generic constants - common for all audio systems (or not used in some of systems) */

static const long int   CW_AUDIO_VOLUME_RANGE = (1 << 15);    /* 2^15 = 32768 */
static const int        CW_AUDIO_SLOPE_USECS = 5000;          /* length of a single slope in standard tone */


/* smallest duration of time (in microseconds) that is used by libcw for
   idle waiting and idle loops; if a libcw function needs to wait for
   something, or make an idle loop, it should call usleep(N * CW_AUDIO_USECS_QUANTUM) */
#define CW_AUDIO_QUANTUM_USECS 100


/* this is a marker of "forever" tone:

   if a tone with duration ("usecs") set to this value is a last one on a
   tone queue, it should be constantly returned by dequeue function,
   without removing the tone - as long as it is a last tone on queue;

   adding new, "non-forever" tone to the queue results in permanent
   dequeuing "forever" tone and proceeding to newly added tone;
   adding new, "non-forever" tone ends generation of "forever" tone;

   the "forever" tone is useful for generating of tones of length unknown
   in advance; length of the tone will be N * (-CW_AUDIO_FOREVER_USECS),
   where N is number of dequeue operations before a non-forever tone is
   added to the queue;

   dequeue function recognizes the "forever" tone and acts as described
   above; there is no visible difference between dequeuing N tones of
   duration "-CW_AUDIO_QUANTUM_USECS", and dequeuing a tone of duration
   "CW_AUDIO_FOREVER_USECS" N times in a row; */
#define CW_AUDIO_FOREVER_USECS (-CW_AUDIO_QUANTUM_USECS)





struct cw_gen_struct {

	int  (* open_device)(cw_gen_t *gen);
	void (* close_device)(cw_gen_t *gen);
	int  (* write)(cw_gen_t *gen);

	/* Generator can only generate tones that were first put into
	   queue, and then dequeued. Here is a tone queue associated
	   with a generator. One tone queue per generator. One
	   generator per tone queue.

	   The tone queue should be created in generator's
	   constructor, and deleted in generator's destructor using
	   tone queue's own constructor and destructor functions - see
	   cw_tq module for declarations of these functions. */
	cw_tone_queue_t *tq;



	/* buffer storing sine wave that is calculated in "calculate sine
	   wave" cycles and sent to audio system (OSS, ALSA, PulseAudio);

	   the buffer should be always filled with valid data before sending
	   it to audio system (to avoid hearing garbage).

	   we should also send exactly buffer_n_samples samples to audio
	   system, in order to avoid situation when audio system waits for
	   filling its buffer too long - this would result in errors and
	   probably audible clicks; */
	cw_sample_t *buffer;

	/* size of data buffer, in samples;

	   the size may be restricted (min,max) by current audio system
	   (OSS, ALSA, PulseAudio); the audio system may also accept only
	   specific values of the size;

	   audio libraries may provide functions that can be used to query
	   for allowed audio buffer sizes;

	   the smaller the buffer, the more often you have to call function
	   writing data to audio system, which increases CPU usage;

	   the larger the buffer, the less responsive an application may
	   be to changes of audio data parameters (depending on application
	   type); */
	int buffer_n_samples;


	/* We need two indices to gen->buffer, indicating beginning
	   and end of a subarea in the buffer.  The subarea is not
	   exactly the same as gen->buffer for variety of reasons:
	   - buffer length is almost always smaller than length of a
	   Mark or Space (in samples) that we want to produce;
	   - moreover, length of a Mark/Space (in samples) is almost
	   never an exact multiple of length of a buffer;
	   - as a result, a sound representing a Mark/Space may start
	   and end anywhere between beginning and end of the buffer;

	   A workable solution is to have a subarea of the buffer, a
	   window, into which we will write a series of fragments of
	   calculated sound.

	   "start" and "stop" mark beginning and end of the subarea
	   (inclusive: samples indexed by "start" and "stop" are part
	   of the subarea).

	   The subarea shall not wrap around boundaries of the buffer:
	   "stop" shall be no larger than "gen->buffer_n_samples - 1",
	   and it shall never be smaller than "start". "start" will be
	   somewhere between zero and "stop", inclusive.

	   "start" == "stop" is a valid situation - length of subarea
	   is then one.

	   Very often (in the middle of a long tone), "start" will be
	   zero, and "stop" will be "gen->buffer_n_samples - 1".

	   Sine wave (sometimes with amplitude = 0) will be calculated
	   for subarea's cells (samples) ranging from cell "start"
	   to cell "stop", inclusive. */
	int buffer_sub_start;
	int buffer_sub_stop;


	/* Some parameters of tones (and of tones' slopes) are common
	   for all tones generated in given time by a generator.
	   Therefore the generator should contain this struct.

	   Other parameters, such as tone's duration or frequency, are
	   strictly related to tones - you won't find them here. */
	struct {
		/* Depending on sample rate, sending speed, and user
		   preferences, length of slope of tones generated by
		   generator may vary, but once set, it is constant
		   for all generated tones (until next change of
		   sample rate, sending speed, etc.).

		   This is why we have the slope length in generator.

		   n_amplitudes declared a bit below in this struct is
		   a secondary parameter, derived from
		   length_usecs. */
		int length_usecs;

		/* Linear/raised cosine/sine/rectangle. */
		int shape;

		/* Table of amplitudes of every PCM sample of tone's
		   slope.

		   The values in amplitudes[] change from zero to max
		   (at least for any sane slope shape), so naturally
		   they can be used in forming rising slope. However
		   they can be used in forming falling slope as well -
		   just iterate the table from end to beginning. */
		float *amplitudes;

		/* This is a secondary parameter, derived from
		   length_usecs. n_amplitudes is useful when iterating
		   over amplitudes[] or reallocing the
		   amplitudes[]. */
		int n_amplitudes;
	} tone_slope;


	/* none/null/console/OSS/ALSA/PulseAudio */
	int audio_system;

	bool audio_device_is_open;

	/* Path to console file, or path to OSS soundcard file,
	   or ALSA sound device name, or PulseAudio device name
	   (it may be unused for PulseAudio) */
	char *audio_device;

	/* output file descriptor for audio data (console, OSS) */
	int audio_sink;

#ifdef LIBCW_WITH_ALSA
	/* Data used by ALSA. */
	cw_alsa_data_t alsa_data;
#endif

#ifdef LIBCW_WITH_PULSEAUDIO
	/* Data used by PulseAudio. */
	cw_pa_data_t pa_data;
#endif

	struct {
		int x;
		int y;
		int z;
	} oss_version;

	/* output file descriptor for debug data (console, OSS, ALSA, PulseAudio) */
	int dev_raw_sink;

	int send_speed;
	int gap;
	int volume_percent; /* level of sound in percents of maximum allowable level */
	int volume_abs;     /* level of sound in absolute terms; height of PCM samples */
	int frequency;   /* this is the frequency of sound that you want to generate */

	int sample_rate; /* set to the same value of sample rate as
			    you have used when configuring sound card */

	/* start/stop flag;
	   set to true before creating generator;
	   set to false to stop generator; generator is then "destroyed";
	   usually the flag is set by specific functions */
	bool generate;

	/* used to calculate sine wave;
	   phase offset needs to be stored between consecutive calls to
	   function calculating consecutive fragments of sine wave */
	double phase_offset;

	struct {
		/* generator thread function is used to generate sine wave
		   and write the wave to audio sink */
		pthread_t      id;
		pthread_attr_t attr;
	} thread;

	struct {
		/* main thread, existing from beginning to end of main process run;
		   the variable is used to send signals to main app thread; */
		pthread_t thread_id;
		char *name;
	} client;


	int weighting;            /* Dot/dash weighting */

	/* These are basic timing parameters which should be
	   recalculated each time client code demands changing some
	   higher-level parameter of generator (e.g. changing of
	   sending speed). */
	int dot_length;           /* Length of a dot, in usec */
        int dash_length;          /* Length of a dash, in usec */
        int eoe_delay;            /* End of element delay, extra delay at the end of element */
	int eoc_delay;            /* End of character delay, extra delay at the end of a char */
	int eow_delay;            /* End of word delay, extra delay at the end of a word */
	int additional_delay;     /* More delay at the end of a char */
	int adjustment_delay;     /* More delay at the end of a word */


	/* Key that has a generator associated with it. Can be NULL in
	   some applications (?).  Set using
	   cw_key_register_generator_internal(). */
	volatile struct cw_key_struct *key;
};





void cw_gen_stop_internal(cw_gen_t *gen);
void cw_gen_delete_internal(cw_gen_t **gen);
int  cw_gen_set_audio_device_internal(cw_gen_t *gen, const char *device);
int  cw_gen_silence_internal(cw_gen_t *gen);
//int cw_gen_release_internal(cw_gen_t **gen);

void cw_generator_delete_internal(void);




#endif /* #ifndef H_LIBCW_GEN */
