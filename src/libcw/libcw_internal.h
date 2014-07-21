#ifndef H_LIBCW_INTERNAL
#define H_LIBCW_INTERNAL


#include <stdbool.h>


#include "config.h"
#include "libcw.h"
#include "libcw_alsa.h"
#include "libcw_pa.h"


#ifndef M_PI  /* C99 may not define M_PI */
#define M_PI  3.14159265358979323846
#endif


#ifdef LIBCW_STANDALONE
/* Should be defined in command line for building "standalone" make target. */
#ifndef LIBCW_WITH_DEV
#define LIBCW_WITH_DEV
#endif
#endif


#ifdef LIBCW_WITH_DEV
#define CW_DEV_RAW_SINK           1  /* Create and use /tmp/cw_file.<audio system>.raw file with audio samples written as raw data. */
#define CW_DEV_RAW_SINK_MARKERS   0  /* Put markers in raw data saved to raw sink. */
#else
#define CW_DEV_RAW_SINK           0
#define CW_DEV_RAW_SINK_MARKERS   0
#endif


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


/* Receiver contains a fixed-length buffer for representation of received data.
   Capacity of the buffer is vastly longer than any practical representation.
   Don't know why, a legacy thing. */
enum { CW_REC_REPRESENTATION_CAPACITY = 256 };


/* Microseconds in a second, for struct timeval handling. */
enum { CW_USECS_PER_SEC = 1000000 };



/* Right now there is no function that would calculate number of tones
   representing given character or string, so there is no easy way to
   present exact relationship between capacity of tone queue and
   number of characters that it can hold.  TODO: perhaps we could
   write utility functions to do that calculation? */

/* TODO: create tests that validate correctness of handling of tone
   queue capacity. See if we really handle the capacity correctly. */


enum {
	/* Default and maximum values of two basic parameters of tone
	   queue: capacity and high water mark. The parameters can be
	   modified using suitable function. */

	/* Tone queue will accept at most "capacity" tones. */
	CW_TONE_QUEUE_CAPACITY_MAX = 3000,        /* ~= 5 minutes at 12 WPM */

	/* Tone queue will refuse to accept new tones (characters?) if
	   number of tones in queue (queue length) is already equal or
	   larger than queue's high water mark. */
	CW_TONE_QUEUE_HIGH_WATER_MARK_MAX = 2900
};




typedef struct cw_tone_struct cw_tone_t;
typedef struct cw_tone_queue_struct cw_tone_queue_t;
typedef struct cw_entry_struct cw_entry_t;
typedef struct cw_tracking_struct cw_tracking_t;


struct cw_tone_struct {
	/* Frequency of a tone. */
	int frequency;

	/* Duration of a tone, in microseconds. */
	int usecs;

	/* Duration of a tone, in samples.
	   This is a derived value, a function of usecs and sample rate. */

	/* TODO: come up with thought-out, consistent type system for
	   samples and usecs. The type system should take into
	   consideration very long duration of tones in QRSS. */
	int64_t n_samples;

	/* We need two indices to gen->buffer, indicating beginning and end
	   of a subarea in the buffer.
	   The subarea is not the same as gen->buffer for variety of reasons:
	    - buffer length is almost always smaller than length of a dash,
	      a dot, or inter-element space that we want to produce;
	    - moreover, length of a dash/dot/space is almost never an exact
	      multiple of length of a buffer;
            - as a result, a sound representing a dash/dot/space may start
	      and end anywhere between beginning and end of the buffer;

	   A workable solution is have a subarea of the buffer, a window,
	   into which we will write a series of fragments of calculated sound.

	   The subarea won't wrap around boundaries of the buffer. "stop"
	   will be no larger than "gen->buffer_n_samples - 1", and it will
	   never be smaller than "stop".

	   "start" and "stop" mark beginning and end of the subarea.
	   Very often (in the middle of the sound), "start" will be zero,
	   and "stop" will be "gen->buffer_n_samples - 1".

	   Sine wave (sometimes with amplitude = 0) will be calculated for
	   cells ranging from cell "start" to cell "stop", inclusive. */
	int sub_start;
	int sub_stop;

	/* a tone can start and/or end abruptly (which may result in
	   audible clicks), or its beginning and/or end can have form
	   of slopes (ramps), where amplitude increases/decreases less
	   abruptly than if there were no slopes;

	   using slopes reduces audible clicks at the beginning/end of
	   tone, and can be used to shape spectrum of a tone;

	   AFAIK most desired shape of a slope looks like sine wave;
	   most simple one is just a linear slope;

	   slope area should be integral part of a tone, i.e. it shouldn't
	   make the tone longer than usecs/n_samples;

	   a tone with rising and falling slope should have this length
	   (in samples):
	   slope_n_samples   +   (n_samples - 2 * slope_n_samples)   +   slope_n_samples

	   libcw allows following slope area scenarios (modes):
	   1. no slopes: tone shouldn't have any slope areas (i.e. tone
	      with constant amplitude);
	   1.a. a special case of this mode is silent tone - amplitude
	        of a tone is zero for whole duration of the tone;
	   2. tone has nothing more than a single slope area (rising or
	      falling); there is no area with constant amplitude;
	   3. a regular tone, with area of rising slope, then area with
	   constant amplitude, and then falling slope;

	   currently, if a tone has both slopes (rising and falling), both
	   slope areas have to have the same length; */
	int slope_iterator;     /* counter of samples in slope area */
	int slope_mode;         /* mode/scenario of slope */
	int slope_n_samples;    /* length of slope area */
};


struct cw_gen_struct {

	int  (* open_device)(cw_gen_t *gen);
	void (* close_device)(cw_gen_t *gen);
	int  (* write)(cw_gen_t *gen);

	/* generator can only generate tones that were first put
	   into queue, and then dequeued */
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

	/* how many samples of audio buffer will be calculated in a given
	   cycle of "calculate sine wave" code? */
	int samples_calculated;

	/* how many samples are still left to calculate to completely
	   fill audio buffer in given cycle? */
	int64_t samples_left;

	/* Some parameters of tones (and of tones' slopes) are common
	   for all tones generated in given time by a
	   generator. Therefore the generator should contain this
	   struct.

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
};







/* Receive timing statistics.
   A circular buffer of entries indicating the difference between the
   actual and the ideal timing for a receive element, tagged with the
   type of statistic held, and a circular buffer pointer.
   STAT_NONE must be zero so that the statistics buffer is initially empty. */
typedef enum {
	STAT_NONE = 0,
	STAT_DOT,
	STAT_DASH,
	STAT_END_ELEMENT,
	STAT_END_CHARACTER
} stat_type_t;

typedef struct {
	stat_type_t type;  /* Record type */
	int delta;         /* Difference between actual and ideal timing */
} cw_statistics_t;

/* TODO: what is the relationship between this constant and CW_REC_REPRESENTATION_CAPACITY?
   Both have value of 256. Coincidence? I don't think so. */
enum { CW_REC_STATISTICS_CAPACITY = 256 };





/* Adaptive speed tracking for receiving. */
enum { CW_REC_AVERAGE_ARRAY_LENGTH = 4 };

/* A moving averages structure, comprising a small array of element
   lengths, a circular index into the array, and a running sum of
   elements for efficient calculation of moving averages. */
struct cw_tracking_struct {
	int buffer[CW_REC_AVERAGE_ARRAY_LENGTH];  /* Buffered element lengths */
	int cursor;                               /* Circular buffer cursor */
	int sum;                                  /* Running sum */
}; /* typedef cw_tracking_t */





typedef struct {
	/* State of receiver state machine. */
	int state;

	int speed;

	int noise_spike_threshold;
	bool is_adaptive_receive_enabled;

	/* Library variable which is automatically maintained from the Morse input
	   stream, rather than being settable by the user.
	   Initially 2-dot threshold for adaptive speed */
	int adaptive_receive_threshold;


	/* Setting this value may trigger a recalculation of some low
	   level timing parameters. */
	int tolerance;

	/* Retained tone start and end timestamps. */
	struct timeval tone_start;
	struct timeval tone_end;

	/* Buffer for received representation (dots/dashes). This is a
	   fixed-length buffer, filled in as tone on/off timings are
	   taken. The buffer is vastly longer than any practical
	   representation.

	   Along with it we maintain a cursor indicating the current
	   write position. */
	char representation[CW_REC_REPRESENTATION_CAPACITY];
	int representation_ind;



	/* Receiver timing parameters */
	/* These are basic timing parameters which should be
	   recalculated each time client code demands changing some
	   higher-level parameter of receiver. */
	int dot_length;           /* Length of a dot, in usec */
	int dash_length;          /* Length of a dash, in usec */
	int dot_range_minimum;    /* Shortest dot period allowable */
	int dot_range_maximum;    /* Longest dot period allowable */
	int dash_range_minimum;   /* Shortest dot period allowable */
	int dash_range_maximum;   /* Longest dot period allowable */
	int eoe_range_minimum;    /* Shortest end of element allowable */
	int eoe_range_maximum;    /* Longest end of element allowable */
	int eoe_range_ideal;      /* Ideal end of element, for stats */
	int eoc_range_minimum;    /* Shortest end of char allowable */
	int eoc_range_maximum;    /* Longest end of char allowable */
	int eoc_range_ideal;      /* Ideal end of char, for stats */


	/* Receiver statistics */
	cw_statistics_t statistics[CW_REC_STATISTICS_CAPACITY];
	int statistics_ind;


	/* Receiver speed tracking */
	cw_tracking_t dot_tracking;
	cw_tracking_t dash_tracking;

} cw_rec_t;


/* From libcw.c, needed in libcw_tq.c. */
void cw_key_set_state_internal(int key_state);
bool cw_sigalrm_is_blocked_internal(void);
int  cw_signal_wait_internal(void);


#if (defined(LIBCW_WITH_ALSA) || defined(LIBCW_WITH_PULSEAUDIO))
bool cw_dlopen_internal(const char *name, void **handle);
#endif
int cw_generator_set_audio_device_internal(cw_gen_t *gen, const char *device);
void cw_usecs_to_timespec_internal(struct timespec *t, int usecs);
void cw_nanosleep_internal(struct timespec *n);

/* From libcwtest.c */
int  cw_test_args(int argc, char *const argv[], char *sound_systems, size_t systems_max);
void cw_test_print_help(const char *progname);

static const int cw_test_print_width = 75;

#define CW_TEST_PRINT_TEST_RESULT(m_failure, m_n) {			\
		printf("%*s\n", (cw_test_print_width - m_n), m_failure ? "failure" : "success"); \
	}

#define CW_TEST_PRINT_FUNCTION_COMPLETED(m_func_name) {			\
		int m = printf("libcw: %s(): ", m_func_name);		\
		printf("%*s\n\n", cw_test_print_width - m, "completed");	\
	}

#endif /* #ifndef H_LIBCW_INTERNAL */
