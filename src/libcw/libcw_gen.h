/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef H_LIBCW_GEN
#define H_LIBCW_GEN





#include "libcw.h"
#include "libcw_alsa.h"
#include "libcw_key.h"
#include "libcw_pa.h"
#include "libcw_tq.h"





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

/* Length of a single slope (rising or falling) in standard tone. [us] */
#define CW_AUDIO_SLOPE_LEN            5000



/* Symbolic name for inter-mark space. */
enum { CW_SYMBOL_SPACE = ' ' };




/* This is used in libcw_gen and libcw_debug. */
#ifdef LIBCW_WITH_DEV
#define CW_DEV_RAW_SINK           1  /* Create and use /tmp/cw_file.<audio system>.raw file with audio samples written as raw data. */
#define CW_DEV_RAW_SINK_MARKERS   0  /* Put markers in raw data saved to raw sink. */
#else
#define CW_DEV_RAW_SINK           0
#define CW_DEV_RAW_SINK_MARKERS   0
#endif




struct cw_gen_struct {

	/* Tone queue. */

	/* Generator can only generate tones that were first put into
	   queue, and then dequeued. Here is a tone queue associated
	   with a generator. One tone queue per generator. One
	   generator per tone queue.

	   The tone queue should be created in generator's
	   constructor, and deleted in generator's destructor using
	   tone queue's own constructor and destructor functions - see
	   cw_tq module for declarations of these functions. */
	cw_tone_queue_t *tq;




	/* Parameters. */

	/* Generator's basic parameters. */
	int send_speed;     /* [wpm] */
	int frequency;      /* The frequency of generated sound. [Hz] */
	int volume_percent; /* Level of sound in percents of maximum allowable level. */
	int volume_abs;     /* Level of sound in absolute terms; height of PCM samples. */
	int gap;            /* Inter-mark gap. [number of dot lengths]. */
	int weighting;      /* Dot/dash weighting. */




	/* Generator's timing parameters. */
	/* These are basic timing parameters which should be
	   recalculated each time client code demands changing some
	   higher-level parameter of generator (e.g. changing of
	   sending speed). */
	/* Watch out for "additional" key word.
	   WARNING: notice how the eoc and eow spaces are
	   calculated. They aren't full 3 units and 7 units. They are
	   2 units (which takes into account preceding eom space
	   length), and 5 units (which takes into account preceding
	   eom *and* eoc space length). So these two lengths are
	   *additional* ones, i.e. in addition to (already existing)
	   eom and/or eoc space.  Whether this is good or bad idea to
	   calculate them like this is a separate topic. Just be aware
	   of this fact.

	   Search the word "*additional*" in libcw_gen.c for
	   implementation */
	int dot_len;              /* Length of a dot Mark. [us] */
	int dash_len;             /* Length of a dash Mark. [us] */
	int eom_space_len;        /* Length of inter-mark space (i.e. the Space). [us] */
	int eoc_space_len;        /* Length of *additional* inter-character space. [us] */
	int eow_space_len;        /* Length of *additional* inter-word space. [us] */

	int additional_space_len; /* Length of additional space at the end of a character. [us] */
	int adjustment_space_len; /* Length of adjustment space at the end of a word. [us] */




	/* Generator's misc parameters. */
	/* Shortest length of tone. Right now it's initialized in
	   constructor and never changed afterwards, but in future it
	   may change - the value may be dynamically modified for
	   performance reasons. The longer the quantum length, the
	   less often some functions need to be called. */
	int quantum_len;




	/* After changing sending speed, gap or weighting, some
	   generator's internal parameters need to be recalculated.
	   This is a flag that shows when this needs to be done. */
	bool parameters_in_sync;




	/* Misc fields. */

	/* Audio buffer and related items. */
	/* Buffer storing sine wave that is calculated in "calculate sine
	   wave" cycles and sent to audio system (OSS, ALSA, PulseAudio).

	   The buffer should be always filled with valid data before sending
	   it to audio system (to avoid hearing garbage).

	   We should also send exactly buffer_n_samples samples to audio
	   system, in order to avoid situation when audio system waits for
	   filling its buffer too long - this would result in errors and
	   probably audible clicks. */
	cw_sample_t *buffer;

	/* Size of data buffer, in samples.

	   The size may be restricted (min,max) by current audio system
	   (OSS, ALSA, PulseAudio); the audio system may also accept only
	   specific values of the size.

	   Audio libraries may provide functions that can be used to query
	   for allowed audio buffer sizes.

	   The smaller the buffer, the more often you have to call function
	   writing data to audio system, which increases CPU usage.

	   The larger the buffer, the less responsive an application may
	   be to changes of audio data parameters (depending on application
	   type). */
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

	int sample_rate; /* set to the same value of sample rate as
			    you have used when configuring sound card */

	/* Used to calculate sine wave.
	   Phase offset needs to be stored between consecutive calls to
	   function calculating consecutive fragments of sine wave. */
	double phase_offset;



	/* Tone parameters. */
	/* Some parameters of tones (and of tones' slopes) are common
	   for all tones generated in given time by a generator.
	   Therefore the generator should contain this struct.

	   Other parameters, such as tone's length or frequency, are
	   strictly related to tones - you won't find them here. */
	struct {
		/* Depending on sample rate, sending speed, and user
		   preferences, length of slope of tones generated by
		   generator may vary, but once set, it is constant
		   for all generated tones (until next change of
		   sample rate, sending speed, etc.).

		   This is why we have the slope length in generator.

		   n_amplitudes declared a bit below in this struct is
		   a secondary parameter, derived from ->len. */
		int len; /* [us] */

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
		   ->len. n_amplitudes is useful when iterating over
		   ->amplitudes[] or reallocing the ->amplitudes[]. */
		int n_amplitudes;
	} tone_slope;



	/* Library's client. */
	struct {
		/* Main thread, existing from beginning to end of main process run.
		   The variable is used to send signals to main app thread. */
		pthread_t thread_id;
		char *name;
	} client;



	/* cw key associated with this generator. */
	/* Key that has a generator associated with it.

	   Standalone generator will have this set to NULL. But
	   generator that is used by a key will have this set to
	   non-NULL value with
	   cw_key_register_generator(). Remember that the key
	   needs to have a generator, not the other way around. */
	volatile struct cw_key_struct *key;




	/* pthread */

	/* Properties of generator's thread function is that is used
	   to generate sine wave and write the wave to audio sink
	   (cw_gen_dequeue_and_generate_internal()). */
	struct {
		pthread_t      id;
		pthread_attr_t attr;

		/* Call to pthread_create(&id, ...) executed and
		   succeeded? If not, don't call pthread_kill(). I
		   can't check value of thread.id, because pthread_t
		   data type is opaque.

		   This flag is a bit different than
		   cw_gen_t->do_dequeue_and_generate.  Setting
		   ->do_dequeue_and_generate signals intent to run a loop
		   deqeueing tones in
		   cw_gen_dequeue_and_generate_internal().  Setting
		   ->thread.running means that thread function
		   cw_gen_dequeue_and_generate_internal() was launched
		   successfully. */
		bool running;
	} thread;

	/* start/stop flag.
	   Set to true before running dequeue_and_play thread
	   function.
	   Set to false to stop generator and return from
	   dequeue_and_generate thread function. */
	bool do_dequeue_and_generate;




	/* Audio system. */

	/* Path to console file, or path to OSS soundcard file,
	   or ALSA sound device name, or PulseAudio device name
	   (it may be unused for PulseAudio) */
	char *audio_device;

	/* Output file descriptor for debug data (console, OSS, ALSA,
	   PulseAudio). */
	int audio_sink;

	/* none/null/console/OSS/ALSA/PulseAudio */
	int audio_system;

	bool audio_device_is_open;

	/* Output file descriptor for debug data (console, OSS, ALSA,
	   PulseAudio). */
	int dev_raw_sink;

	int  (* open_device)(cw_gen_t *gen);
	void (* close_device)(cw_gen_t *gen);
	int  (* write)(cw_gen_t *gen);


	/* Audio system - OSS. */
	struct {
		int x;
		int y;
		int z;
	} oss_version;

#ifdef LIBCW_WITH_ALSA
	/* Data used by ALSA. */
	cw_alsa_data_t alsa_data;
#endif

#ifdef LIBCW_WITH_PULSEAUDIO
	/* Data used by PulseAudio. */
	cw_pa_data_t pa_data;
#endif

};





void cw_gen_get_timing_parameters_internal(cw_gen_t *gen, int *dot_len, int *dash_len, int *eom_space_len, int *eoc_space_len, int *eow_space_len, int *additional_space_len, int *adjustment_space_len);





/* Generator's 'enqueue' primitives. */
int cw_gen_enqueue_mark_internal(cw_gen_t *gen, char mark, bool is_first);
int cw_gen_enqueue_eoc_space_internal(cw_gen_t *gen);
int cw_gen_enqueue_eow_space_internal(cw_gen_t *gen);

/* These are also 'enqueue' primitives, but are intended to be used on
   hardware key events. 'key' is a verb here. */
int cw_gen_enqueue_begin_mark_internal(cw_gen_t *gen);
int cw_gen_enqueue_begin_space_internal(cw_gen_t *gen);
int cw_gen_enqueue_partial_symbol_internal(cw_gen_t *gen, char symbol);





int cw_gen_enqueue_representation_partial_internal(cw_gen_t * gen, const char * representation);
int cw_gen_enqueue_valid_character_internal(cw_gen_t * gen, char c);
int cw_gen_enqueue_character_partial(cw_gen_t * gen, char c);




int   cw_gen_set_audio_device_internal(cw_gen_t *gen, const char *device);
int   cw_gen_silence_internal(cw_gen_t *gen);
char *cw_gen_get_audio_system_label_internal(cw_gen_t *gen);

void cw_generator_delete_internal(void);

void cw_gen_reset_parameters_internal(cw_gen_t *gen);
void cw_gen_sync_parameters_internal(cw_gen_t *gen);



#endif /* #ifndef H_LIBCW_GEN */
