/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011       Kamil Ignacak (acerion@wp.pl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _BSD_SOURCE   /* usleep() */
#define _POSIX_SOURCE /* signaction() */
#include "../config.h"

#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>

#include <math.h>
#ifndef M_PI  /* C99 may not define M_PI */
#define M_PI  3.14159265358979323846
#endif


#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#if defined(HAVE_SYS_KD_H)
# include <sys/kd.h>
#elif defined(HAVE_SYS_VTKD_H)
# include <sys/vtkd.h>
#elif defined(HAVE_SYS_KBIO_H)
# include <sys/kbio.h>
#endif

#if defined(HAVE_SYS_SOUNDCARD_H)
# include <sys/soundcard.h>
#elif defined(HAVE_SOUNDCARD_H)
# include <soundcard.h>
#else
# error "Neither sys/soundcard.h nor soundcard.h header file available"
#endif

#if defined(BSD)
# define ERR_NO_SUPPORT EPROTONOSUPPORT
#else
# define ERR_NO_SUPPORT EPROTO
#endif

#include "cwlib.h"
#include "../cwutils/copyright.h" /* I will get rid of this access path once I will update build system, promise ;) */



struct cw_gen_struct {
	cw_sample_t *buffer;
	int buffer_n_samples;
	/* none/console/OSS/ALSA */
	int audio_system;
	/* true/false */
	int audio_device_open;
	/* Path to console file, or path to OSS soundcard file,
	   or ALSA sound device name. */
	char *audio_device;
	/* output file descriptor for audio data (console, OSS) */
	int audio_sink;
	/* output handle for audio data (ALSA) */
	snd_pcm_t *alsa_handle;
	/* output file descriptor for debug data (console, OSS) */
	int debug_sink;


	int volume; /* this is the level of sound that you want to have */
	int frequency;   /* this is the frequency of sound that you want to generate */

	int sample_rate; /* set to the same value of sample rate as
			    you have used when configuring sound card */

	int slope; /* used to control initial and final phase of
		      non-zero-amplitude sine wave; slope/attack
		      makes it possible to start or end a wave
		      without clicks;
		      this field provides a very convenient way to
		      turn on/off a sound, just assign:
		      +CW_OSS_GENERATOR_SLOPE to turn sound on,
		      -CW_OSS_GENERATOR_SLOPE to turn sound off */

	/* start/stop flag;
	   set to 1 before creating generator;
	   set to 0 to stop generator; generator gets "destroyed"
	   handling the flag is wrapped in cw_oss_start_generator()
	   and cw_oss_stop_generator() */
	int generate;

	/* these are generator's internal state variables; */
	int amplitude; /* current amplitude of generated sine wave
			  (as in x(t) = A * sin(t)); in fixed/steady state
			  the amplitude is either zero or .volume */

	double phase_offset;
	double phase;


	/* Thread function is used to generate sine wave
	   and write the wave to audio sink. */
	pthread_t thread;
	pthread_attr_t thread_attr;
	int thread_error; /* 0 when no problems, errno when some error occurred */
};



static int  cw_open_device_oss(const char *device);
static void cw_close_device_oss(void);

static int  cw_open_device_console(const char *device);
static void cw_close_device_console(void);

static int  cw_open_device_alsa(const char *device);
static void cw_close_device_alsa(void);
static int cw_set_alsa_hw_params(snd_pcm_t *handle, snd_pcm_hw_params_t *params);
#ifdef CW_DEV
static int cw_print_alsa_params(snd_pcm_hw_params_t *params);
#endif
static int cw_set_audio_device(const char *device);

static void *cw_generator_write_sine_wave_oss(void *arg);
static void *cw_generator_write_sine_wave_alsa(void *arg);
static int   cw_generator_calculate_sine_wave(cw_gen_t *gen);
static int   cw_generator_calculate_amplitude(cw_gen_t *gen);

static int cw_sound_console_internal(int state);
static int cw_sound_soundcard_internal(int state);
static int cw_release_sound_internal(void);
static int cw_sound_internal(int frequency);
static bool cw_is_debugging_internal(unsigned int flag);
static int cw_open_device_oss_ioctls(int *fd, int *sample_rate);

static const char *cw_lookup_character_internal(char c);
static unsigned int cw_hash_representation_internal(const char *representation);
static char cw_lookup_representation_internal(const char *representation);
static const char *cw_lookup_procedural_character_internal(char c, int *is_usually_expanded);
static void cw_sync_parameters_internal(void);
static void cw_sigalrm_handler_internal(int signal_number);
static int cw_set_timer_internal(int usecs);
static int cw_request_timeout_internal(int usecs, void (*request_handler)(void));
static int cw_release_timeouts_internal(void);
static int cw_check_signal_mask_internal(void);
static int cw_block_signal_internal(int is_block);
static int cw_wait_for_signal_internal(void);
static void cw_finalization_clock_internal(void);
static void cw_schedule_finalization_internal(void);
static void cw_cancel_finalization_internal(void);
static void cw_interpose_signal_handler_internal(int signal_number);
static void cw_key_control_internal(int requested_key_state);
static int cw_get_tone_queue_length_internal(void);
static int cw_next_tone_queue_index_internal(int current);
static void cw_tone_queue_clock_internal(void);
static int cw_enqueue_tone_internal(int usecs, int frequency);
static int cw_send_element_internal(char element);
static int cw_send_representation_internal(const char *representation, int partial);
static int cw_send_character_internal(char c, int partial);


static void cw_set_adaptive_receive_internal(bool flag);
static int cw_validate_timestamp_internal(const struct timeval *timestamp, struct timeval *return_timestamp);
static int cw_compare_timestamps_internal(const struct timeval *earlier, const struct timeval *later);
static int cw_identify_receive_tone_internal(int element_usec, char *representation);
static void cw_update_adaptive_tracking_internal(int element_usec, char element);
static int cw_receive_buffer_element_internal(const struct timeval *timestamp, char element);
static void cw_keyer_clock_internal(void);
static void cw_straight_key_clock_internal(void);





/* Conditional compilation flags */
#define CW_MAIN                   0  /* for stand-alone compilation and tests of this file */
#define CW_DEV                    0  /* development support */
#define CW_OSS_SET_FRAGMENT       1  /* ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &param) */
#define CW_OSS_SET_POLICY         0  /* ioctl(fd, SNDCTL_DSP_POLICY, &param) */
#define CW_ALSA_HW_BUFFER_CONFIG  1  /* set up hw buffer parameters, doesn't work 100% correctly (yet) */


/* Generic constants - common for all audio systems (or not used in some of systems) */
static const unsigned int CW_AUDIO_SAMPLE_RATE_A = 44100;       /* Primary sound sample rate */
static const unsigned int CW_AUDIO_SAMPLE_RATE_B = 48000;       /* Secondary sound sample rate */
static const int          CW_AUDIO_CHANNELS = 1;                /* Sound in mono */
static const long int     CW_AUDIO_VOLUME_RANGE = (1 << 15);    /* 2^15 = 32768 */
// enum                    { CW_AUDIO_GENERATOR_BUF_SIZE = 128 };  /* Size of buffer storing samples, the value works well for both OSS and ALSA */
static const float        CW_AUDIO_GENERATOR_SLOPE_RATIO = 1.0;    /* ~1.0 for 44.1/48 kHz sample rate */

/* 0Hz = silent 'tone'. */
static const int CW_TONE_SILENT = 0;


/* Constants specific to OSS audio system configuration */
static const int CW_OSS_SETFRAGMENT = 7;              /* Sound fragment size, 2^7 samples */
static const int CW_OSS_SAMPLE_FORMAT = AFMT_S16_NE;  /* Sound format AFMT_S16_NE = signed 16 bit, native endianess; LE = Little endianess */


/* Constants specific to ALSA audio system configuration */
static const snd_pcm_format_t CW_ALSA_SAMPLE_FORMAT = SND_PCM_FORMAT_S16; /* "Signed 16 bit CPU endian"; I'm guessing that "CPU endian" == "native endianess" */


/* indexed by values of 'enum cw_audio_systems' */
static const char *default_audio_devices[] = {
	(char *) NULL,
	CW_DEFAULT_CONSOLE_DEVICE,
	CW_DEFAULT_OSS_DEVICE,
	CW_DEFAULT_ALSA_DEVICE };


/* main data container; this is a global variable in library file,
   so in future the variable must be moved from the file to client code;

   this is a global variable that should be converted into
   a function argument; this pointer should exist only in
   client's code, should initially be returned by new(), and
   deleted by delete();
   TODO: perform the conversion later, when you figure out
   ins and outs of the library */
static cw_gen_t *generator = NULL;




/**
   \brief Return version number of cwlib library

   Return the version number of the library.
   Version numbers (major and minor) are returned as an int,
   composed of major_version << 16 | minor_version.

   \return library's major and minor version number encoded as single int
*/
int cw_version(void)
{
	unsigned int major = 0, minor = 0;

	sscanf(PACKAGE_VERSION, "%u.%u", &major, &minor);
	return major << 16 | minor;
}





/**
   \brief Print cwlib's license text to stdout

   Function prints information about cwlib version, followed
   by short text presenting cwlib's copyright and license notice.
*/
void cw_license(void)
{
	printf("cwlib version %s\n", PACKAGE_VERSION);
	printf("%s\n", CW_COPYRIGHT);

	return;
}





/* ************************ */
/*         Debugging        */
/* ************************ */





/* Current debug flags setting; no debug unless requested. */
static unsigned int cw_debug_flags = 0;





/**
   \brief Set a value of internal debug flags variable

   Assign specified value to library's internal debug flags variable.
   Note that this function doesn't *append* given flag to the variable,
   it erases existing value and assigns new one. Use cw_get_debug_flags()
   if you want to OR new flag with existing ones.

   \param new_value - new value to be assigned to the library
*/
void cw_set_debug_flags(unsigned int new_value)
{
	cw_debug_flags = new_value;
}





/**
   \brief Get current library's debug flags

   Function returns value of library's internal debug variable.

   \return value of library's debug flags variable
*/
unsigned int cw_get_debug_flags(void)
{
	/* TODO: extract reading CWLIB_DEBUG env
	   variable to separate function. */

	static bool is_initialized = false;

	if (!is_initialized) {
		/* Do not overwrite any debug flags already set. */
		if (cw_debug_flags == 0) {

			/*
			 * Set the debug flags from CWLIB_DEBUG.  If it is an invalid
			 * numeric, treat it as 0; there is no error checking.
			 */
			const char *debug_value = getenv("CWLIB_DEBUG");
			if (debug_value) {
				cw_debug_flags = strtoul(debug_value, NULL, 0);
			}
		}

		is_initialized = true;
	}

	return cw_debug_flags;
}





/**
   \brief Check if given debug flag is set

   Function checks if a specified debug flag is set in internal
   variable of cwlib library.

   \param flag - flag to be checked.

   \return true if given flag is set
   \return false if given flag is not set
*/
bool cw_is_debugging_internal(unsigned int flag)
{
	return cw_get_debug_flags() & flag;
}





/* macro supporting multiple arguments */
#define cw_debug(flag, ...)				\
	{						\
		if (cw_is_debugging_internal(flag)) {	\
			fprintf(stderr, "cwlib: ");	\
			fprintf(stderr, __VA_ARGS__);	\
			fprintf(stderr, "\n");		\
		}					\
	}





/* Debugging message for library developer */
#if CW_DEV
#define cw_dev_debug(...)						\
	{								\
		fprintf(stderr, "cwlib: ");				\
		fprintf(stderr, "%s: %d: ", __func__, __LINE__);	\
		fprintf(stderr, __VA_ARGS__);				\
		fprintf(stderr, "\n");					\
	}
#else
#define cw_dev_debug(...) {}
#endif





/*---------------------------------------------------------------------*/
/*  Core Morse code data and lookup                                    */
/*---------------------------------------------------------------------*/

/*
 * Morse code characters table.  This table allows lookup of the Morse shape
 * of a given alphanumeric character.  Shapes are held as a string, with '-'
 * representing dash, and '.' representing dot.  The table ends with a NULL
 * entry.
 */
typedef struct
{
  const char character;              /* Character represented */
  const char *const representation;  /* Dot-dash shape of the character */
} cw_entry_t;

static const cw_entry_t CW_TABLE[] = {
  /* ASCII 7bit letters */
  {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},
  {'D', "-.."},   {'E', "."},     {'F', "..-."},
  {'G', "--."},   {'H', "...."},  {'I', ".."},
  {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
  {'M', "--"},    {'N', "-."},    {'O', "---"},
  {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},
  {'S', "..."},   {'T', "-"},     {'U', "..-"},
  {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
  {'Y', "-.--"},  {'Z', "--.."},

  /* Numerals */
  {'0', "-----"},  {'1', ".----"},  {'2', "..---"},
  {'3', "...--"},  {'4', "....-"},  {'5', "....."},
  {'6', "-...."},  {'7', "--..."},  {'8', "---.."},
  {'9', "----."},

  /* Punctuation */
  {'"', ".-..-."},  {'\'', ".----."},  {'$', "...-..-"},
  {'(', "-.--."},   {')', "-.--.-"},   {'+', ".-.-."},
  {',', "--..--"},  {'-', "-....-"},   {'.', ".-.-.-"},
  {'/', "-..-."},   {':', "---..."},   {';', "-.-.-."},
  {'=', "-...-"},   {'?', "..--.."},   {'_', "..--.-"},
  {'@', ".--.-."},

  /* ISO 8859-1 accented characters */
  {'\334', "..--"},    /* U with diaeresis */
  {'\304', ".-.-"},    /* A with diaeresis */
  {'\307', "-.-.."},   /* C with cedilla */
  {'\326', "---."},    /* O with diaeresis */
  {'\311', "..-.."},   /* E with acute */
  {'\310', ".-..-"},   /* E with grave */
  {'\300', ".--.-"},   /* A with grave */
  {'\321', "--.--"},   /* N with tilde */

  /* ISO 8859-2 accented characters */
  {'\252', "----"},    /* S with cedilla */
  {'\256', "--..-"},   /* Z with dot above */

  /* Non-standard procedural signal extensions to standard CW characters. */
  {'<', "...-.-"},     /* VA/SK, end of work */
  {'>', "-...-.-"},    /* BK, break */
  {'!', "...-."},      /* SN, understood */
  {'&', ".-..."},      /* AS, wait */
  {'^', "-.-.-"},      /* KA, starting signal */
  {'~', ".-.-.."},     /* AL, paragraph */

  /* Sentinel end of table value */
  {0, NULL}
};


/**
 * Returns the number of characters represented in the character lookup
 * table.
 */
int cw_get_character_count(void)
{
  static int character_count = 0;

  if (character_count == 0)
    {
      const cw_entry_t *cw_entry;

      for (cw_entry = CW_TABLE; cw_entry->character; cw_entry++)
        character_count++;
    }

  return character_count;
}


/**
 * Returns into list a string containing all of the Morse characters
 * represented in the table.  The length of list must be at least one greater
 * than the number of characters represented in the character lookup table,
 * returned by cw_get_character_count.
 */
void cw_list_characters(char *list)
{
  const cw_entry_t *cw_entry;
  int index;

  /* Append each table character to the output string. */
  index = 0;
  for (cw_entry = CW_TABLE; cw_entry->character; cw_entry++)
    list[index++] = cw_entry->character;

  list[index] = '\0';
}


/**
 * Returns the string length of the longest representation in the character
 * lookup table.
 */
int cw_get_maximum_representation_length(void)
{
  static int maximum_length = 0;

  if (maximum_length == 0)
    {
      const cw_entry_t *cw_entry;

      /* Traverse the main lookup table, finding the longest. */
      for (cw_entry = CW_TABLE; cw_entry->character; cw_entry++)
        {
          int length;

          length = (int) strlen (cw_entry->representation);
          if (length > maximum_length)
            maximum_length = length;
        }
    }

  return maximum_length;
}


/**
 * Look up the given character, and return the representation of that
 * character.  Returns NULL if there is no table entry for the given
 * character.
 */
const char *cw_lookup_character_internal(char c)
{
  static const cw_entry_t *lookup[UCHAR_MAX];  /* Fast lookup table */
  static bool is_initialized = false;

  const cw_entry_t *cw_entry;

  /*
   * If this is the first call, set up the fast lookup table to give direct
   * access to the CW table for a given character.
   */
  if (!is_initialized)
    {
      cw_debug (CW_DEBUG_LOOKUPS, "initialize fast lookup table");

      for (cw_entry = CW_TABLE; cw_entry->character; cw_entry++)
        lookup[(unsigned char) cw_entry->character] = cw_entry;

      is_initialized = true;
    }

  /*
   * There is no differentiation in the table between upper and lower case
   * characters; everything is held as uppercase.  So before we do the lookup,
   * we convert to ensure that both cases work.
   */
  c = toupper (c);

  /*
   * Now use the table to lookup the table entry.  Unknown characters return
   * NULL, courtesy of the fact that explicitly uninitialized static variables
   * are initialized to zero, so lookup[x] is NULL if it's not assigned to in
   * the above loop.
   */
  cw_entry = lookup[(unsigned char) c];

  if (cw_is_debugging_internal (CW_DEBUG_LOOKUPS))
    {
      if (cw_entry)
        fprintf (stderr, "cw: lookup '%c' returned <'%c':\"%s\">\n",
                 c, cw_entry->character, cw_entry->representation);
      else if (isprint (c))
        fprintf (stderr, "cw: lookup '%c' found nothing\n", c);
      else
        fprintf (stderr, "cw: lookup 0x%02x found nothing\n",
                 (unsigned char) c);
    }

  return cw_entry ? cw_entry->representation : NULL;
}


/**
 * Returns the string 'shape' of a given Morse code character.  The routine
 * returns true on success, and fills in the string pointer passed in.  On
 * error, it returns false and sets errno to ENOENT, indicating that the
 * character could not be found.  The length of representation must be at
 * least one greater than the longest representation held in the character
 * lookup table, returned by cw_get_maximum_representation_length.
 */
int cw_lookup_character(char c, char *representation)
{
  const char *retval;

  /* Lookup the character, and if found, return the string. */
  retval = cw_lookup_character_internal (c);
  if (retval)
    {
      if (representation)
        strcpy (representation, retval);
      return CW_SUCCESS;
    }

  /* Failed to find the requested character. */
  errno = ENOENT;
  return CW_FAILURE;
}


/**
 * Return a hash value, in the range 2-255, for a lookup table representation.
 * The routine returns 0 if no valid hash could be made from the string.  To
 * avoid casting the value a lot in the caller (we want to use it as an array
 * index), we actually return an unsigned int.
 *
 * This hash algorithm is designed ONLY for valid CW representations; that is,
 * strings composed of only '.' and '-', and in this case, strings shorter than
 * eight characters.  The algorithm simply turns the representation into a
 * 'bitmask', based on occurrences of '.' and '-'.  The first bit set in the
 * mask indicates the start of data (hence the 7-character limit).  This mask
 * is viewable as an integer in the range 2 (".") to 255 ("-------"), and can
 * be used as an index into a fast lookup array.
 */
unsigned int cw_hash_representation_internal(const char *representation)
{
  int length, index;
  unsigned int hash;

  /*
   * Our algorithm can handle only 7 characters of representation.  And we
   * insist on there being at least one character, too.
   */
  length = (int) strlen (representation);
  if (length > CHAR_BIT - 1 || length < 1)
    return 0;

  /*
   * Build up the hash based on the dots and dashes; start at 1, the sentinel
   * (start) bit.
   */
  hash = 1;
  for (index = 0; index < length; index++)
    {
      /* Left-shift everything so far. */
      hash <<= 1;

      /*
       * If the next element is a dash, OR in another bit.  If it is not a
       * dash or a dot, then there is an error in the representation string.
       */
      if (representation[index] == CW_DASH_REPRESENTATION)
        hash |= 1;
      else if (representation[index] != CW_DOT_REPRESENTATION)
        return 0;
    }

  return hash;
}


/**
 * Look up the given representation, and return the character that it
 * represents.  Returns zero if there is no table entry for the given
 * representation.
 */
char cw_lookup_representation_internal(const char *representation)
{
  static const cw_entry_t *lookup[UCHAR_MAX];  /* Fast lookup table */
  static bool is_complete = true;               /* Set to false if there are any
                                                  lookup table entries not in
                                                  the fast lookup table */
  static bool is_initialized = false;

  const cw_entry_t *cw_entry;
  unsigned int hash;

  /*
   * If this is the first call, set up the fast lookup table to give direct
   * access to the CW table for a hashed representation.
   */
  if (!is_initialized)
    {
      cw_debug (CW_DEBUG_LOOKUPS, "initialize hash lookup table");

      /*
       * For each main table entry, create a hash entry.  If the hashing
       * of any entry fails, note that the table is not complete and ignore
       * that entry for now (for the current lookup table, this should not
       * happen).  The hashed table speeds up lookups of representations by
       * a factor of 5-10.
       */
      for (cw_entry = CW_TABLE; cw_entry->character; cw_entry++)
        {
          hash = cw_hash_representation_internal (cw_entry->representation);
          if (hash)
            lookup[hash] = cw_entry;
          else
            is_complete = false;
        }

      if (!is_complete) {
	      cw_debug (CW_DEBUG_LOOKUPS, "hash lookup table incomplete");
      }

      is_initialized = true;
    }

  /* Hash the representation to get an index for the fast lookup. */
  hash = cw_hash_representation_internal (representation);

  /*
   * If the hashed lookup table is complete, we can simply believe any
   * hash value that came back.  That is, we just use what is at the index
   * 'hash', since this is either the entry we want, or NULL.
   */
  if (is_complete)
    cw_entry = lookup[hash];
  else
    {
      /*
       * If the hashed lookup table is not complete, the lookup might still
       * have found us the entry we are looking for.  Here, we'll check to
       * see if it did.
       */
      if (hash && lookup[hash] && lookup[hash]->representation
          && strcmp (lookup[hash]->representation, representation) == 0)
        {
          /* Found it in an incomplete table. */
          cw_entry = lookup[hash];
        }
      else
        {
          /*
           * We have no choice but to search the table entry by entry,
           * sequentially, from top to bottom.
           */
          for (cw_entry = CW_TABLE; cw_entry->character; cw_entry++)
            {
              if (strcmp (cw_entry->representation, representation) == 0)
                break;
            }

          /* If we got to the end of the table, return NULL. */
          cw_entry = cw_entry->character ? cw_entry : NULL;
        }
    }

  if (cw_is_debugging_internal (CW_DEBUG_LOOKUPS))
    {
      if (cw_entry)
        fprintf (stderr, "cw: lookup [0x%02x]'%s' returned <'%c':\"%s\">\n",
                 hash, representation,
                 cw_entry->character, cw_entry->representation);
      else
        fprintf (stderr, "cw: lookup [0x%02x]'%s' found nothing\n",
                 hash, representation);
    }

  return cw_entry ? cw_entry->character : 0;
}


/**
 * Checks that the given string is a valid Morse representation.  A valid
 * string is one composed of only '.' and '-' characters.  On success, the
 * routine returns true.  On error, it returns false, with errno set to EINVAL.
 */
int cw_check_representation(const char *representation)
{
  int index;

  /* Check the characters in representation. */
  for (index = 0; representation[index]; index++)
    {
      if (representation[index] != CW_DOT_REPRESENTATION
          && representation[index] != CW_DASH_REPRESENTATION)
        {
          errno = EINVAL;
          return CW_FAILURE;
        }
    }

  return CW_SUCCESS;
}


/**
 * Returns the character for a given Morse representation.  On success, the
 * routine returns true, and fills in char *c.  On error, it returns false,
 * and sets errno to EINVAL if any character of the representation is
 * invalid, or ENOENT to indicate that the representation could not be found.
 */
int cw_lookup_representation(const char *representation, char *c)
{
  char character;

  /* Check the characters in representation. */
  if (!cw_check_representation (representation))
    {
      errno = EINVAL;
      return CW_FAILURE;
    }

  /* Lookup the representation, and if found, return the character. */
  character = cw_lookup_representation_internal (representation);
  if (character)
    {
      if (c)
        *c = character;
      return CW_SUCCESS;
    }

  /* Failed to find the requested representation. */
  errno = ENOENT;
  return CW_FAILURE;
}


/*---------------------------------------------------------------------*/
/*  Extended Morse code data and lookup                                */
/*---------------------------------------------------------------------*/

/*
 * Ancilliary procedural signals table.  This table maps procedural signal
 * characters in the main table to their expansions, along with a flag noting
 * if the character is usually expanded for display.
 */
typedef struct
{
  const char character;           /* Character represented */
  const char *const expansion;    /* Procedural expansion of the character */
  const int is_usually_expanded;  /* If expanded display is usual */
} cw_prosign_entry_t;

static const cw_prosign_entry_t CW_PROSIGN_TABLE[] = {
  /* Standard procedural signals */
  {'"', "AF", false},   {'\'', "WG", false},  {'$', "SX", false},
  {'(', "KN", false},   {')', "KK", false},   {'+', "AR", false},
  {',', "MIM", false},  {'-', "DU", false},   {'.', "AAA", false},
  {'/', "DN", false},   {':', "OS", false},   {';', "KR", false},
  {'=', "BT", false},   {'?', "IMI", false},  {'_', "IQ", false},
  {'@', "AC", false},

  /* Non-standard procedural signal extensions to standard CW characters. */
  {'<', "VA", true},  /* VA/SK, end of work */
  {'>', "BK", true},  /* BK, break */
  {'!', "SN", true},  /* SN, understood */
  {'&', "AS", true},  /* AS, wait */
  {'^', "KA", true},  /* KA, starting signal */
  {'~', "AL", true},  /* AL, paragraph */

  /* Sentinel end of table value */
  {0, NULL, false}
};


/**
 * Returns the number of characters represented in the procedural signal
 * expansion lookup table.
 */
int cw_get_procedural_character_count(void)
{
  static int character_count = 0;

  if (character_count == 0)
    {
      const cw_prosign_entry_t *cw_prosign;

      for (cw_prosign = CW_PROSIGN_TABLE; cw_prosign->character; cw_prosign++)
        character_count++;
    }

  return character_count;
}


/**
 * Returns into list a string containing all of the Morse characters for which
 * procedural expansion are available.  The length of list must be at least
 * one greater than the number of characters represented in the procedural
 * signal expansion lookup table, returned by cw_get_procedural_character_count.
 */
void cw_list_procedural_characters(char *list)
{
  const cw_prosign_entry_t *cw_prosign;
  int index;

  /* Append each table character to the output string. */
  index = 0;
  for (cw_prosign = CW_PROSIGN_TABLE; cw_prosign->character; cw_prosign++)
    list[index++] = cw_prosign->character;

  list[index] = '\0';
}


/**
 * Returns the string length of the longest expansion in the procedural
 * signal expansion table.
 */
int cw_get_maximum_procedural_expansion_length(void)
{
  static int maximum_length = 0;

  if (maximum_length == 0)
    {
      const cw_prosign_entry_t *cw_prosign;

      /* Traverse the main lookup table, finding the longest. */
      for (cw_prosign = CW_PROSIGN_TABLE; cw_prosign->character; cw_prosign++)
        {
          int length;

          length = (int) strlen (cw_prosign->expansion);
          if (length > maximum_length)
            maximum_length = length;
        }
    }

  return maximum_length;
}


/**
 * Look up the given procedural character, and return the expansion of that
 * procedural character, with a display hint in is_usually_expanded.  Returns
 * NULL if there is no table entry for the given character.
 */
const char *cw_lookup_procedural_character_internal(char c, int *is_usually_expanded)
{
  static const cw_prosign_entry_t *lookup[UCHAR_MAX];  /* Fast lookup table */
  static bool is_initialized = false;

  const cw_prosign_entry_t *cw_prosign;

  /*
   * If this is the first call, set up the fast lookup table to give direct
   * access to the procedural expansions table for a given character.
   */
  if (!is_initialized)
    {
      cw_debug (CW_DEBUG_LOOKUPS, "initialize prosign fast lookup table");

      for (cw_prosign = CW_PROSIGN_TABLE; cw_prosign->character; cw_prosign++)
        lookup[(unsigned char) cw_prosign->character] = cw_prosign;

      is_initialized = true;
    }

  /*
   * Lookup the procedural signal table entry.  Unknown characters return
   * NULL.  All procedural signals are non-alphabetical, so no need to use
   * any uppercase coercion here.
   */
  cw_prosign = lookup[(unsigned char) c];

  if (cw_is_debugging_internal (CW_DEBUG_LOOKUPS))
    {
      if (cw_prosign)
        fprintf (stderr, "cw: prosign lookup '%c' returned <'%c':\"%s\":%d>\n",
                 c, cw_prosign->character,
                 cw_prosign->expansion, cw_prosign->is_usually_expanded);
      else if (isprint (c))
        fprintf (stderr, "cw: prosign lookup '%c' found nothing\n", c);
      else
        fprintf (stderr, "cw: prosign lookup 0x%02x found nothing\n",
                 (unsigned char) c);
    }

  /* If found, return any display hint and the expansion; otherwise, NULL. */
  if (cw_prosign)
    *is_usually_expanded = cw_prosign->is_usually_expanded;
  return cw_prosign ? cw_prosign->expansion : NULL;
}


/**
 * Returns the string expansion of a given Morse code procedural signal
 * character.  The routine returns true on success, filling in the string
 * pointer passed in and setting is_usuall_expanded to true as a display
 * hint for the caller.  On error, it returns false and sets errno to ENOENT,
 * indicating that the procedural signal character could not be found.  The
 * length of representation must be at least one greater than the longest
 * representation held in the procedural signal character lookup table,
 * returned by cw_get_maximum_procedural_expansion_length.
 */
int cw_lookup_procedural_character(char c, char *representation,
				   int *is_usually_expanded)
{
  const char *retval;
  int is_expanded;

  /* Lookup, and if found, return the string and display hint. */
  retval = cw_lookup_procedural_character_internal (c, &is_expanded);
  if (retval)
    {
      if (representation)
        strcpy (representation, retval);
      if (is_usually_expanded)
        *is_usually_expanded = is_expanded;
      return CW_SUCCESS;
    }

  /* Failed to find the requested procedural signal character. */
  errno = ENOENT;
  return CW_FAILURE;
}


/*
 * Phonetics table.  Not really CW, but it might be handy to have.  The
 * table contains ITU/NATO phonetics.
 */
static const char *const CW_PHONETICS[27] = {
  "Alfa", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel",
  "India", "Juliett", "Kilo", "Lima", "Mike", "November", "Oscar", "Papa",
  "Quebec", "Romeo", "Sierra", "Tango", "Uniform", "Victor", "Whiskey",
  "X-ray", "Yankee", "Zulu", NULL
};


/**
 * Returns the string length of the longest phonetic in the phonetics
 * lookup table.
 */
int cw_get_maximum_phonetic_length(void)
{
  static int maximum_length = 0;

  if (maximum_length == 0)
    {
      int phonetic;

      /* Traverse the main lookup table, finding the longest. */
      for (phonetic = 0; CW_PHONETICS[phonetic]; phonetic++)
        {
          int length;

          length = (int) strlen (CW_PHONETICS[phonetic]);
          if (length > maximum_length)
            maximum_length = length;
        }
    }

  return maximum_length;
}


/**
 * Returns the phonetic of a given character.  The routine returns true on
 * success, and fills in the string pointer passed in.  On error, it returns
 * false and sets errno to ENOENT, indicating that the character could not be
 * found.  The length of phonetic must be at least one greater than the
 * longest phonetic held in the phonetic lookup table, returned by
 * cw_get_maximum_phonetic_length.
 */
int cw_lookup_phonetic(char c, char *phonetic)
{
  /* Coerce to uppercase, and verify the input argument. */
  c = toupper (c);
  if (c >= 'A' && c <= 'Z')
    {
      if (phonetic)
        strcpy (phonetic, CW_PHONETICS[c - 'A']);
      return CW_SUCCESS;
    }

  /* No such phonetic. */
  errno = ENOENT;
  return CW_FAILURE;
}


/*---------------------------------------------------------------------*/
/* Morse code controls and timing parameters                           */
/*---------------------------------------------------------------------*/

/* Dot length magic number; from PARIS calibration, 1 Dot=1200000/WPM usec. */
enum { DOT_CALIBRATION = 1200000 };




/* Default initial values for library controls. */
enum
{
  CW_INITIAL_ADAPTIVE = false,    /* Initial adaptive receive setting */
  CW_INITIAL_THRESHOLD = (DOT_CALIBRATION / CW_SPEED_INITIAL) * 2,
                               /* Initial adaptive speed threshold */
  CW_INITIAL_NOISE_THRESHOLD = (DOT_CALIBRATION / CW_SPEED_MAX) / 2
                               /* Initial noise filter threshold */
};

/*
 * Library variables, indicating the user-selected parameters for generating
 * Morse code output and receiving Morse code input.  These values can be
 * set by client code; setting them may trigger a recalculation of the low
 * level timing values held and set below.
 */
static int cw_send_speed = CW_SPEED_INITIAL,
           cw_gap = CW_GAP_INITIAL,
           cw_receive_speed = CW_SPEED_INITIAL,
           cw_tolerance = CW_TOLERANCE_INITIAL,
           cw_weighting = CW_WEIGHTING_INITIAL,
           cw_noise_spike_threshold = CW_INITIAL_NOISE_THRESHOLD;
static bool cw_is_adaptive_receive_enabled = CW_INITIAL_ADAPTIVE;
/*
 * The following variables must be recalculated each time any of the above
 * Morse parameters associated with speeds, gap, tolerance, or threshold
 * change.  Keeping these in step means that we then don't have to spend time
 * calculating them on the fly.
 *
 * Since they have to be kept in sync, the problem of how to have them
 * calculated on first call if none of the above parameters has been
 * changed is taken care of with a synchronization flag.  Doing this saves
 * us from otherwise having to have a 'library initialize' function.
 */
static bool cw_is_in_sync = false;       /* Synchronization flag */
/* Sending parameters: */
static int cw_send_dot_length = 0,      /* Length of a send Dot, in usec */
           cw_send_dash_length = 0,     /* Length of a send Dash, in usec */
           cw_end_of_ele_delay = 0,     /* Extra delay at the end of element */
           cw_end_of_char_delay = 0,    /* Extra delay at the end of a char */
           cw_additional_delay = 0,     /* More delay at the end of a char */
           cw_end_of_word_delay = 0,    /* Extra delay at the end of a word */
           cw_adjustment_delay = 0,     /* More delay at the end of a word */
/* Receiving parameters: */
           cw_receive_dot_length = 0,   /* Length of a receive Dot, in usec */
           cw_receive_dash_length = 0,  /* Length of a receive Dash, in usec */
           cw_dot_range_minimum = 0,    /* Shortest dot period allowable */
           cw_dot_range_maximum = 0,    /* Longest dot period allowable */
           cw_dash_range_minimum = 0,   /* Shortest dot period allowable */
           cw_dash_range_maximum = 0,   /* Longest dot period allowable */
           cw_eoe_range_minimum = 0,    /* Shortest end of ele allowable */
           cw_eoe_range_maximum = 0,    /* Longest end of ele allowable */
           cw_eoe_range_ideal = 0,      /* Ideal end of ele, for stats */
           cw_eoc_range_minimum = 0,    /* Shortest end of char allowable */
           cw_eoc_range_maximum = 0,    /* Longest end of char allowable */
           cw_eoc_range_ideal = 0;      /* Ideal end of char, for stats */

/*
 * Library variable which is automatically maintained from the Morse input
 * stream, rather than being settable by the user.
 */
static int cw_adaptive_receive_threshold = CW_INITIAL_THRESHOLD;
                                        /* Initially 2-dot threshold for
                                           adaptive speed */





/**
   \brief Get speed limits

   Get (through function's arguments) limits on speed of morse code that
   can be generated by current generator.

   See CW_SPEED_MIN and CW_SPEED_MAX in cwlib.h for values.

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
   be generated by current generator.

   See CW_FREQUENCY_MIN and CW_FREQUENCY_MAX in cwlib.h for values.

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
   generated by current generator.

   See CW_VOLUME_MIN and CW_VOLUME_MAX in cwlib.h for values.

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
   generated by current generator.

   See CW_GAP_MIN and CW_GAP_MAX in cwlib.h for values.

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

   Get (through function's arguments) limits on 'tolerance' parameter
   of current generator.

   See CW_TOLERANCE_MIN and CW_TOLERANCE_MAX in cwlib.h for values.

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

   Get (through function's arguments) limits on 'weighting' parameter
   of current generator.

   See CW_WEIGHTING_MIN and CW_WEIGHTING_MAX in cwlib.h for values.

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
 * Synchronize the dot, dash, end of element, end of character, and end
 * of word timings and ranges to new values of Morse speed, 'Farnsworth'
 * gap, receive tolerance, or weighting.
 */
void cw_sync_parameters_internal(void)
{
  /* Do nothing if we are already synchronized with speed/gap. */
  if (!cw_is_in_sync)
    {
      int unit_length, weighting_length;

      /*
       * Send parameters:
       *
       * Set the length of a Dot to be a Unit with any weighting adjustment,
       * and the length of a Dash as three Dot lengths.  The weighting
       * adjustment is by adding or subtracting a length based on 50 % as a
       * neutral weighting.
       */
      unit_length = DOT_CALIBRATION / cw_send_speed;
      weighting_length = (2 * (cw_weighting - 50) * unit_length) / 100;
      cw_send_dot_length = unit_length + weighting_length;
      cw_send_dash_length = 3 * cw_send_dot_length;

      /*
       * An end of element length is one Unit, perhaps adjusted, the end of
       * character is three Units total, and end of word is seven Units total.
       *
       * The end of element length is adjusted by 28/22 times weighting
       * length to keep PARIS calibration correctly timed (PARIS has 22 full
       * units, and 28 empty ones).  End of element and end of character
       * delays take weightings into account.
       */
      cw_end_of_ele_delay = unit_length - (28 * weighting_length) / 22;
      cw_end_of_char_delay = 3 * unit_length - cw_end_of_ele_delay;
      cw_end_of_word_delay = 7 * unit_length - cw_end_of_char_delay;
      cw_additional_delay = cw_gap * unit_length;

      /*
       * For 'Farnsworth', there also needs to be an adjustment delay added
       * to the end of words, otherwise the rhythm is lost on word end.  I
       * don't know if there is an "official" value for this, but 2.33 or so
       * times the gap is the correctly scaled value, and seems to sound
       * okay.
       *
       * Thanks to Michael D. Ivey <ivey@gweezlebur.com> for identifying this
       * in earlier versions of cwlib.
       */
      cw_adjustment_delay = (7 * cw_additional_delay) / 3;

      cw_debug (CW_DEBUG_PARAMETERS, "send usec timings <%d>: %d, %d, %d, %d, %d, %d, %d",
		cw_send_speed, cw_send_dot_length, cw_send_dash_length,
		cw_end_of_ele_delay, cw_end_of_char_delay,
		cw_end_of_word_delay, cw_additional_delay, cw_adjustment_delay);

      /*
       * Receive parameters:
       *
       * First, depending on whether we are set for fixed speed or adaptive
       * speed, calculate either the threshold from the receive speed, or the
       * receive speed from the threshold, knowing that the threshold is
       * always, effectively, two dot lengths.  Weighting is ignored for
       * receive parameters, although the core unit length is recalculated
       * for the receive speed, which may differ from the send speed.
       */
      unit_length = DOT_CALIBRATION / cw_receive_speed;
      if (cw_is_adaptive_receive_enabled)
        cw_receive_speed = DOT_CALIBRATION
                           / (cw_adaptive_receive_threshold / 2);
      else
        cw_adaptive_receive_threshold = 2 * unit_length;

      /*
       * Calculate the basic receive dot and dash lengths.
       */
      cw_receive_dot_length = unit_length;
      cw_receive_dash_length = 3 * unit_length;

      /*
       * Set the ranges of respectable timing elements depending very much on
       * whether we are required to adapt to the incoming Morse code speeds.
       */
      if (cw_is_adaptive_receive_enabled)
        {
          /*
           * For adaptive timing, calculate the Dot and Dash timing ranges
           * as zero to two Dots is a Dot, and anything, anything at all,
           * larger than this is a Dash.
           */
          cw_dot_range_minimum = 0;
          cw_dot_range_maximum = 2 * cw_receive_dot_length;
          cw_dash_range_minimum = cw_dot_range_maximum;
          cw_dash_range_maximum = INT_MAX;

          /*
           * Make the inter-element gap be anything up to the adaptive
           * threshold lengths - that is two Dots.  And the end of character
           * gap is anything longer than that, and shorter than five dots.
           */
          cw_eoe_range_minimum = cw_dot_range_minimum;
          cw_eoe_range_maximum = cw_dot_range_maximum;
          cw_eoc_range_minimum = cw_eoe_range_maximum;
          cw_eoc_range_maximum = 5 * cw_receive_dot_length;
        }
      else
        {
          int tolerance;

          /*
           * For fixed speed receiving, calculate the Dot timing range as the
           * Dot length +/- dot*tolerance%, and the Dash timing range as the
           * Dash length including +/- dot*tolerance% as well.
           */
          tolerance = (cw_receive_dot_length * cw_tolerance) / 100;
          cw_dot_range_minimum = cw_receive_dot_length - tolerance;
          cw_dot_range_maximum = cw_receive_dot_length + tolerance;
          cw_dash_range_minimum = cw_receive_dash_length - tolerance;
          cw_dash_range_maximum = cw_receive_dash_length + tolerance;

          /*
           * Make the inter-element gap the same as the Dot range.  Make the
           * inter-character gap, expected to be three Dots, the same as Dash
           * range at the lower end, but make it the same as the Dash range
           * _plus_ the 'Farnsworth' delay at the top of the range.
           *
           * Any gap longer than this is by implication inter-word.
           */
          cw_eoe_range_minimum = cw_dot_range_minimum;
          cw_eoe_range_maximum = cw_dot_range_maximum;
          cw_eoc_range_minimum = cw_dash_range_minimum;
          cw_eoc_range_maximum = cw_dash_range_maximum
                                 + cw_additional_delay + cw_adjustment_delay;
        }

      /*
       * For statistical purposes, calculate the ideal end of element and
       * end of character timings.
       */
      cw_eoe_range_ideal = unit_length;
      cw_eoc_range_ideal = 3 * unit_length;

      cw_debug (CW_DEBUG_PARAMETERS, "receive usec timings <%d>: %d-%d, %d-%d, %d-%d[%d], %d-%d[%d], %d",
		cw_receive_speed,
		cw_dot_range_minimum, cw_dot_range_maximum,
		cw_dash_range_minimum, cw_dash_range_maximum,
		cw_eoe_range_minimum, cw_eoe_range_maximum, cw_eoe_range_ideal,
		cw_eoc_range_minimum, cw_eoc_range_maximum, cw_eoc_range_ideal,
		cw_adaptive_receive_threshold);

      /* Set the parameters in sync flag. */
      cw_is_in_sync = true;
    }
}


/**
 * Reset the library speed, frequency, volume, gap, tolerance, weighting,
 * adaptive receive, and noise spike threshold to their initial default values:
 * send/receive speed 12 WPM, volume 70 %, frequency 800 Hz, gap 0 dots,
 * tolerance 50 %, and weighting 50 %.
 */
void cw_reset_send_receive_parameters(void)
{
  cw_send_speed = CW_SPEED_INITIAL;
  generator->frequency = CW_FREQUENCY_INITIAL;
  generator->volume = CW_VOLUME_INITIAL;
  cw_gap = CW_GAP_INITIAL;
  cw_receive_speed = CW_SPEED_INITIAL;
  cw_tolerance = CW_TOLERANCE_INITIAL;
  cw_weighting = CW_WEIGHTING_INITIAL;
  cw_is_adaptive_receive_enabled = CW_INITIAL_ADAPTIVE;
  cw_noise_spike_threshold = CW_INITIAL_NOISE_THRESHOLD;

  /* Changes require resynchronization. */
  cw_is_in_sync = false;
  cw_sync_parameters_internal ();
}


/**
 * cw_set_[send_speed|receive_speed|frequency|volume|gap|tolerance|weighting]()
 * cw_get_[send_speed|receive_speed|frequency|volume|gap|tolerance|weighting]()
 *
 * Get and set routines for all the Morse code parameters available to
 * control the library.  Set routines return true on success, or false on
 * failure, with errno set to indicate the problem, usually EINVAL, except for
 * cw_set_receive_speed, which returns EINVAL if the new value is
 * invalid, or EPERM if the receive mode is currently set for adaptive
 * receive speed tracking.  Get routines simply return the current value.
 *
 * The default values of the parameters where none are explicitly set are
 * send/receive speed 12 WPM, volume 70 %, frequency 800 Hz, gap 0 dots,
 * tolerance 50 %, and weighting 50 %.  Note that volume settings are not
 * fully possible for the console speaker; in this case, volume settings
 * greater than zero indicate console speaker sound is on, and setting volume
 * to zero will turn off console speaker sound.
 */
int cw_set_send_speed(int new_value)
{
  if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }
  if (new_value != cw_send_speed)
    {
      cw_send_speed = new_value;

      /* Changes of send speed require resynchronization. */
      cw_is_in_sync = false;
      cw_sync_parameters_internal ();
    }

  return CW_SUCCESS;
}





/**
   See documentation of cw_set_send_speed() for more information.
*/
int cw_set_receive_speed(int new_value)
{
  if (cw_is_adaptive_receive_enabled)
    {
      errno = EPERM;
      return CW_FAILURE;
    }
  else
    {
      if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX)
        {
          errno = EINVAL;
          return CW_FAILURE;
        }
    }
  if (new_value != cw_receive_speed)
    {
      cw_receive_speed = new_value;

      /* Changes of receive speed require resynchronization. */
      cw_is_in_sync = false;
      cw_sync_parameters_internal ();
    }

  return CW_SUCCESS;
}




/**
   \brief Set frequency of current generator

   Set frequency of sound wave generated by current generator.
   The frequency must be within limits marked by CW_FREQUENCY_MIN
   and CW_FREQUENCY_MAX.

   The function sets errno to EINVAL on errors.

   \param new_value - new value of frequency to be associated with current generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_frequency(int new_value)
{
	if (new_value < CW_FREQUENCY_MIN || new_value > CW_FREQUENCY_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	} else {
		generator->frequency = new_value;
		return CW_SUCCESS;
	}
}





/**
   \brief Set volume of current generator

   Set volume of sound wave generated by current generator.
   The volume must be within limits marked by CW_VOLUME_MIN and CW_VOLUME_MAX.

   The function sets errno to EINVAL on errors.

   \param new_value - new value of volume to be associated with current generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_volume(int new_value)
{
	if (new_value < CW_VOLUME_MIN || new_value > CW_VOLUME_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	} else {
		generator->volume = new_value;
		return CW_SUCCESS;
	}
}





/**
   See documentation of cw_set_send_speed() for more information.
*/
int cw_set_gap(int new_value)
{
  if (new_value < CW_GAP_MIN || new_value > CW_GAP_MAX)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }
  if (new_value != cw_gap)
    {
      cw_gap = new_value;

      /* Changes of gap require resynchronization. */
      cw_is_in_sync = false;
      cw_sync_parameters_internal ();
    }

  return CW_SUCCESS;
}





/**
   See documentation of cw_set_send_speed() for more information.
*/
int cw_set_tolerance(int new_value)
{
  if (new_value < CW_TOLERANCE_MIN || new_value > CW_TOLERANCE_MAX)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }
  if (new_value != cw_tolerance)
    {
      cw_tolerance = new_value;

      /* Changes of tolerance require resynchronization. */
      cw_is_in_sync = false;
      cw_sync_parameters_internal ();
    }

  return CW_SUCCESS;
}





/**
   See documentation of cw_set_send_speed() for more information.
*/
int cw_set_weighting(int new_value)
{
  if (new_value < CW_WEIGHTING_MIN || new_value > CW_WEIGHTING_MAX)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }
  if (new_value != cw_weighting)
    {
      cw_weighting = new_value;

      /* Changes of weighting require resynchronization. */
      cw_is_in_sync = false;
      cw_sync_parameters_internal ();
    }

  return CW_SUCCESS;
}





/**
   See documentation of cw_set_send_speed() for more information.
*/
int cw_get_send_speed(void)
{
  return cw_send_speed;
}





/**
   See documentation of cw_set_send_speed() for more information.
*/
int cw_get_receive_speed(void)
{
  return cw_receive_speed;
}





/**
   \brief Return frequency of current generator

   Function returns 'frequency' parameter of current generator,
   even if the generator is stopped, or volume of generated sound is zero.

   \return Frequency of current generator
*/
int cw_get_frequency(void)
{
	return generator->frequency;
}





/**
   \brief Return volume of current generator

   Function returns 'volume' parameter of current generator,
   even if the generator is stopped.

   \return Volume of current generator
*/
int cw_get_volume(void)
{
	return generator->volume;
}





/**
   See documentation of cw_set_send_speed() for more information.
*/
int cw_get_gap(void)
{
	return cw_gap;
}





/**
   See documentation of cw_set_send_speed() for more information.
*/
int cw_get_tolerance(void)
{
	return cw_tolerance;
}





/**
   See documentation of cw_set_send_speed() for more information.
*/
int cw_get_weighting(void)
{
	return cw_weighting;
}





/**
 * Return the low-level timing parameters calculated from the speed, gap,
 * tolerance, and weighting set.  Parameter values are returned in
 * microseconds.  Use NULL for the pointer argument to any parameter value
 * not required.
 */
void cw_get_send_parameters(int *dot_usecs, int *dash_usecs,
			    int *end_of_element_usecs,
			    int *end_of_character_usecs, int *end_of_word_usecs,
			    int *additional_usecs, int *adjustment_usecs)
{
  cw_sync_parameters_internal ();
  if (dot_usecs)
    *dot_usecs = cw_send_dot_length;
  if (dash_usecs)
    *dash_usecs = cw_send_dash_length;
  if (end_of_element_usecs)
    *end_of_element_usecs = cw_end_of_ele_delay;
  if (end_of_character_usecs)
    *end_of_character_usecs = cw_end_of_char_delay;
  if (end_of_word_usecs)
    *end_of_word_usecs = cw_end_of_word_delay;
  if (additional_usecs)
    *additional_usecs = cw_additional_delay;
  if (adjustment_usecs)
    *adjustment_usecs = cw_adjustment_delay;
}





/**
   See documentation of cw_get_send_parameters() for more information
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
  cw_sync_parameters_internal ();
  if (dot_usecs)
    *dot_usecs = cw_receive_dot_length;
  if (dash_usecs)
    *dash_usecs = cw_receive_dash_length;
  if (dot_min_usecs)
    *dot_min_usecs = cw_dot_range_minimum;
  if (dot_max_usecs)
    *dot_max_usecs = cw_dot_range_maximum;
  if (dash_min_usecs)
    *dash_min_usecs = cw_dash_range_minimum;
  if (dash_max_usecs)
    *dash_max_usecs = cw_dash_range_maximum;
  if (end_of_element_min_usecs)
    *end_of_element_min_usecs = cw_eoe_range_minimum;
  if (end_of_element_max_usecs)
    *end_of_element_max_usecs = cw_eoe_range_maximum;
  if (end_of_element_ideal_usecs)
    *end_of_element_ideal_usecs = cw_eoe_range_ideal;
  if (end_of_character_min_usecs)
    *end_of_character_min_usecs = cw_eoc_range_minimum;
  if (end_of_character_max_usecs)
    *end_of_character_max_usecs = cw_eoc_range_maximum;
  if (end_of_character_ideal_usecs)
    *end_of_character_ideal_usecs = cw_eoc_range_ideal;
  if (adaptive_threshold)
    *adaptive_threshold = cw_adaptive_receive_threshold;
}


/**
 * Set and get the period shorter than which, on receive, received tones are
 * ignored.  This allows the receive tone functions to apply noise canceling
 * for very short apparent tones.  For useful results the value should never
 * exceed the dot length of a dot at maximum speed; 20,000 microseconds (the
 * dot length at 60WPM).  Setting a noise threshold of zero turns off receive
 * tone noise canceling.  The default noise spike threshold is 10,000
 * microseconds.
 */
int cw_set_noise_spike_threshold(int threshold)
{
  if (threshold < 0)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }
  cw_noise_spike_threshold = threshold;

  return CW_SUCCESS;
}





/**
   See documentation of cw_set_noise_spike_threshold() for more information
*/
int cw_get_noise_spike_threshold(void)
{
	return cw_noise_spike_threshold;
}





/*---------------------------------------------------------------------*/
/*  SIGALRM and timer handling                                         */
/*---------------------------------------------------------------------*/

/* Microseconds in a second, for struct timeval handling. */
static const int USECS_PER_SEC = 1000000;

/*
 * The library registers a single central SIGALRM handler.  This handler will
 * call all of the functions on a list sequentially on each SIGALRM received.
 * This is where the list is kept.
 */
enum { SIGALRM_HANDLERS = 32 };
static void (*cw_request_handlers[SIGALRM_HANDLERS])(void);

/*
 * Flag to tell us if the SIGALRM handler is installed, and a place to keep
 * the old SIGALRM disposition, so we can restore it when the library decides
 * it can stop handling SIGALRM for a while.
 */
static bool cw_is_sigalrm_handler_installed = false;
static struct sigaction cw_sigalrm_original_disposition;



/**
 * Common SIGALRM handler.  This function calls the signal handlers of the
 * library subsystems, expecting them to ignore unexpected calls.
 */
void cw_sigalrm_handler_internal(int signal_number)
{
  int handler;

  /* Avoid compiler warnings about unused argument. */
  signal_number = 0;

  /*
   * Call the known functions that are interested in this signal.  Stop on
   * the first free slot found; valid because the array is filled in order
   * from index 0, and there are no deletions.
   */
  for (handler = 0;
       handler < SIGALRM_HANDLERS && cw_request_handlers[handler]; handler++)
    {
      void (*request_handler)(void);

      request_handler = cw_request_handlers[handler];
      (*request_handler) ();
    }
}


/**
 * Convenience function to set the itimer for a single shot timeout after a
 * given number of microseconds.
 */
int cw_set_timer_internal(int usecs)
{
  int status;
  struct itimerval itimer;

  /* Set up a single shot timeout for the given period. */
  itimer.it_interval.tv_sec = 0;
  itimer.it_interval.tv_usec = 0;
  itimer.it_value.tv_sec = usecs / USECS_PER_SEC;
  itimer.it_value.tv_usec = usecs % USECS_PER_SEC;
  status = setitimer (ITIMER_REAL, &itimer, NULL);
  if (status == -1)
    {
      perror ("cw: setitimer");
      return CW_FAILURE;
    }

  return CW_SUCCESS;
}


/**
 * Install the SIGALRM handler, if not yet installed.  Add any given lower
 * level handler to the list of registered handlers.  Then set an itimer
 * to expire after the requested number of microseconds.
 */
int cw_request_timeout_internal(int usecs, void (*request_handler)(void))
{
  struct sigaction action;

  /* Install the SIGALRM handler if not currently installed. */
  if (!cw_is_sigalrm_handler_installed)
    {
      int status;

      /*
       * Register the SIGALRM handler routine, and keep the old information
       * so we can put it back when useful to do so.
       */
      action.sa_handler = cw_sigalrm_handler_internal;
      action.sa_flags = SA_RESTART;
      sigemptyset (&action.sa_mask);
      status = sigaction (SIGALRM, &action, &cw_sigalrm_original_disposition);
      if (status == -1)
        {
          perror ("cw: sigaction");
          return CW_FAILURE;
        }

      cw_is_sigalrm_handler_installed = true;
    }

  /*
   * If it's not already present, and one was given, add the request handler
   * address to the list of known handlers.
   */
  if (request_handler)
    {
      int handler;

      /*
       * Search for this handler, or the first free entry, stopping at the
       * last entry in the table even if it's not a match and not free.
       */
      for (handler = 0; handler < SIGALRM_HANDLERS - 1; handler++)
        {
          if (!cw_request_handlers[handler]
              || cw_request_handlers[handler] == request_handler)
            break;
        }

      /*
       * If the handler is already there, do no more.  Otherwise, if we ended
       * the search at an unused entry, add it to the list of lower level
       * handlers.
       */
      if (cw_request_handlers[handler] != request_handler)
        {
          if (cw_request_handlers[handler])
            {
              errno = ENOMEM;
              perror ("cw: overflow cw_request_handlers");
              return CW_FAILURE;
            }
          cw_request_handlers[handler] = request_handler;
        }
    }

  /*
   * The fact that we receive a call means that something is using timeouts
   * and sound, so make sure that any pending finalization doesn't happen.
   */
  cw_cancel_finalization_internal ();

  /*
   * Depending on the value of usec, either set an itimer, or send ourselves
   * SIGALRM right away.
   */
  if (usecs <= 0)
    {
      /* Send ourselves SIGALRM immediately. */
      if (raise (SIGALRM) != 0)
        {
          perror ("cw: raise");
          return CW_FAILURE;
        }
    }
  else
    {
      /*
       * Set the itimer to produce a single interrupt after the given
       * duration.
       */
      if (!cw_set_timer_internal (usecs))
        return CW_FAILURE;
    }

  return CW_SUCCESS;
}


/**
 * Uninstall the SIGALRM handler, if installed.  Return SIGALRM's disposition
 * for the system to the state we found it in before we installed our own
 * SIGALRM handler.
 */
int cw_release_timeouts_internal(void)
{
  /* Ignore the call if we haven't installed our handler. */
  if (cw_is_sigalrm_handler_installed)
    {
      int status;

      /* Cancel any pending itimer setting. */
      if (!cw_set_timer_internal (0))
        return CW_FAILURE;

      /* Put back the SIGALRM information saved earlier. */
      status = sigaction (SIGALRM, &cw_sigalrm_original_disposition, NULL);
      if (status == -1)
        {
          perror ("cw: sigaction");
          return CW_FAILURE;
        }

      cw_is_sigalrm_handler_installed = false;
    }

  return CW_SUCCESS;
}


/**
 * Check the signal mask of the process, and return an error, with errno
 * set to EDEADLK, if SIGALRM is blocked.
 */
int cw_check_signal_mask_internal(void)
{
  int status;
  sigset_t empty_set, current_set;

  /* Block a empty set of signals to obtain the current mask. */
  sigemptyset (&empty_set);
  status = sigprocmask (SIG_BLOCK, &empty_set, &current_set);
  if (status == -1)
    {
      perror ("cw: sigprocmask");
      return CW_FAILURE;
    }

  /* Check that SIGALRM is not blocked in the current mask. */
  if (sigismember (&current_set, SIGALRM))
    {
      errno = EDEADLK;
      return CW_FAILURE;
    }

  return CW_SUCCESS;
}


/**
 * Block SIGALRM for the duration of certain critical sections, or unblock
 * after; passed true to block SIGALRM, and false to unblock.
 */
int cw_block_signal_internal(int is_block)
{
  int status;
  sigset_t block_set;

  /* Block or unblock SIGALRM for the process. */
  sigemptyset (&block_set);
  sigaddset (&block_set, SIGALRM);
  status = sigprocmask (is_block ? SIG_BLOCK : SIG_UNBLOCK, &block_set, NULL);
  if (status == -1)
    {
      perror ("cw: sigprocmask");
      return CW_FAILURE;
    }

  return CW_SUCCESS;
}


/**
 * Blocks the callback from being called for a critical section of caller
 * code if is_block is true, and unblocks the callback if block is false.
 * Works by blocking SIGALRM; a block should always be matched by an unblock,
 * otherwise the tone queue will suspend forever.
 */
void cw_block_callback(int is_block)
{
  cw_block_signal_internal (is_block);
}


/**
 * Wait for a signal, usually a SIGALRM.  Assumes SIGALRM is not blocked.
 */
int cw_wait_for_signal_internal(void)
{
  int status;
  sigset_t empty_set, current_set;

  /* Block a empty set of signals to obtain the current mask. */
  sigemptyset (&empty_set);
  status = sigprocmask (SIG_BLOCK, &empty_set, &current_set);
  if (status == -1)
    {
      perror ("cw: sigprocmask");
      return CW_FAILURE;
    }

  /* Wait on the current mask. */
  status = sigsuspend (&current_set);
  if (status == -1 && errno != EINTR)
    {
      perror ("cw: sigsuspend");
      return CW_FAILURE;
    }

  return CW_SUCCESS;
}


/*---------------------------------------------------------------------*/
/*  Console and soundcard control                                      */
/*---------------------------------------------------------------------*/





/**
   \brief Set audio device name or path

   Set path to audio device, or name of audio device. The path/name
   will be associated with current generator, and used when opening
   audio device.

   Use this function only when setting up a generator.

   Function creates its own copy of input string.

   \param device - device to be associated with generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on errors
*/
int cw_set_audio_device(const char *device)
{
	/* this should be NULL, either because it has been
	   initialized statically as NULL, or set to
	   NULL by generator destructor */
	assert (!generator->audio_device);
	assert (generator->audio_system != CW_AUDIO_NONE);

	if (generator->audio_system == CW_AUDIO_NONE) {
		generator->audio_device = (char *) NULL;
		cw_dev_debug ("no audio system specified");
		return CW_FAILURE;
	}

	if (device) {
		generator->audio_device = strdup(device);
	} else {
		generator->audio_device = strdup(default_audio_devices[generator->audio_system]);
	}

	if (!generator->audio_device) {
		cw_debug (CW_DEBUG_SYSTEM, "error: malloc error\n");
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





/**
   \brief Return char string with console device path

   Returned pointer is owned by library.

   \return char string with current console device path
*/
const char *cw_get_console_device(void)
{
	return generator->audio_device;
}





/**
   \brief Return char string with soundcard device name/path

   Returned pointer is owned by library.

   \return char string with current soudcard device name or device path
*/
const char *cw_get_soundcard_device(void)
{
	return generator->audio_device;
}





/**
   \brief Start generating a sound using soundcard

   Start generating sound on soundcard with frequency depending on
   state of current generator. The function has a single argument
   'state'. The argument toggles between zero volume (state == 0)
   and full volume (frequency > 0).

   The function only initializes generation, you have to do another
   function call to change the tone generated.

   \param state - toggle between full volume and no volume

   \return CW_SUCCESS on success
   \return CW_FAILURE on errors
*/
int cw_sound_soundcard_internal(int state)
{
	if (generator->audio_system != CW_AUDIO_OSS
	    && generator->audio_system != CW_AUDIO_ALSA) {

		cw_dev_debug ("called the function for output other than sound card (%d)",
			generator->audio_system);

		/* Strictly speaking this should be CW_FAILURE, but this
		   is not a place and time to do anything more. The above
		   message printed to stderr should be enough to catch
		   problems during development phase */
		return CW_SUCCESS;
	}

	/* TODO:
	   With this new scheme of producing a sound the sound is
	   a bit longer than a dot (or a dash) by a time needed to
	   decrease amplitude of sine wave from 'volume' to zero.
	   When a timer signals that it's time to stop generating
	   a sound, the library proceeds to gradually decrease
	   amplitude of sine wave producing a falling slope. Length
	   of the slope is inversely proportional to
	   CW_AUDIO_GENERATOR_SLOPE.

	   This additional time used to generate falling slope is rather
	   small but cannot be tolerated. Thus I need to come up with
	   better way of ending a sound. Somehow the slope must appear
	   (i.e. the amplitude needs to start decreasing) *before*
	   expected end of sound. */

	/* use 'slope' to control amplitude of sine wave generator:
	   negative slope decreases amplitude to zero, positive slope
	   increases amplitude to current volume level */

	/* having slope dependent on volume helps us avoiding shorter
	   slopes at lower volumes; with constant slope it would take
	   shorter to switch from zero to max (and from max to zero),
	   which may result in pops at low volumes */
	int minimum_slope = 1;
	int slope = generator->volume ?
		CW_AUDIO_GENERATOR_SLOPE_RATIO * generator->volume :
		minimum_slope;
	if (state == CW_TONE_SILENT) { /* TONE_SILENT == 0, silence the sound */
		generator->slope = -slope;
	} else {
		generator->slope = slope;
	}

	return CW_SUCCESS;
}





/**
   \brief Stop and delete generator

   Stop and delete current generator.
   This causes silencing current sound wave.

   \return CW_SUCCESS
*/
int cw_release_sound_internal(void)
{
	cw_generator_stop();
	cw_generator_delete();

	return CW_SUCCESS;
}





/**
   \brief Start generating a sound

   Start generating sound with frequency depending on state of current
   generator. The function has a single argument 'frequency'. The name
   is old and meaningless, the argument now only toggles between
   zero volume (frequency == 0, or frequency == CW_TONE_SILENCE),
   and full volume (frequency > 0).

   Current generator decides if the sound will be played using
   soundcard or console buzzer.

   The function only initializes generation, you have to do another
   function call to change the tone generated.

   \param frequency - toggle between full volume and no volume

   \return CW_SUCCESS on success
   \return CW_FAILURE on errors
*/
int cw_sound_internal(int frequency)
{
	/* If silence requested, then ignore the call. */
	if (cw_is_debugging_internal(CW_DEBUG_SILENT)) {
		return CW_SUCCESS;
	}

	if (!generator) {
		/* this may happen because the process of finalizing
		   usage of cwlib is rather complicated; this should
		   be somehow resolved */
		cw_dev_debug ("called the function for NULL generator");
		return CW_SUCCESS;
	}

	int state = frequency == CW_TONE_SILENT ? 0 : 1;
	int status = CW_SUCCESS;

	if (generator->audio_system == CW_AUDIO_OSS
	    || generator->audio_system == CW_AUDIO_ALSA) {

		status = cw_sound_soundcard_internal(state);
	} else if (generator->audio_system == CW_AUDIO_CONSOLE) {
		status = cw_sound_console_internal(state);
	} else {
		;
	}

	return status;
}





/*---------------------------------------------------------------------*/
/*  Finalization and cleanup                                           */
/*---------------------------------------------------------------------*/

/*
 * We prefer to close the soundcard after a period of library inactivity,
 * so that other applications can use it.  Ten seconds seems about right.
 * We do it in one-second timeouts so that any leaked pending timeouts from
 * other facilities don't cause premature finalization.
 */
static const int FINALIZATION_DELAY = 10000000;

/* Counter counting down the number of clock calls before we finalize. */
static volatile bool cw_is_finalization_pending = false;
static volatile int cw_finalization_countdown = 0;

/* Use a mutex to suppress delayed finalizations on complete resets. */
static volatile bool cw_is_finalization_locked_out = false;

/*
 * Array of callbacks registered for convenience signal handling.  They're
 * initialized dynamically to SIG_DFL (if SIG_DFL is not NULL, which it
 * seems that it is in most cases).
 */
static void (*cw_signal_callbacks[RTSIG_MAX]) (int);


/**
 * If finalization is pending, decrement the countdown, and if this reaches
 * zero, we've waited long enough to release sound and timeouts.
 */
void cw_finalization_clock_internal(void)
{
  if (cw_is_finalization_pending)
    {
      /* Decrement the timeout countdown, and finalize if we reach zero. */
      cw_finalization_countdown--;
      if (cw_finalization_countdown <= 0)
        {
	  cw_debug (CW_DEBUG_FINALIZATION, "finalization timeout, closing down");

          cw_release_timeouts_internal ();
          // cw_release_sound_internal ();

          cw_is_finalization_pending = false;
          cw_finalization_countdown = 0;
        }
      else
        {
	  cw_debug (CW_DEBUG_FINALIZATION, "finalization countdown %d", cw_finalization_countdown);

          /* Request another timeout.  This results in a call to our
           * cw_cancel_finalization_internal below; to ensure that it doesn't
           * really cancel finalization, unset the pending flag, then set it
           * back again after reqesting the timeout.
           */
          cw_is_finalization_pending = false;
          cw_request_timeout_internal (USECS_PER_SEC, NULL);
          cw_is_finalization_pending = true;
        }
    }
}


/**
 * Set the finalization pending flag, and request a timeout to call the
 * finalization function after a delay of a few seconds.  Cancel any pending
 * finalization on noting other library activity, indicated by a call from
 * the timeout request function telling us that it is setting a timeout.
 */
void cw_schedule_finalization_internal(void)
{
  if (!cw_is_finalization_locked_out && !cw_is_finalization_pending)
    {
      cw_request_timeout_internal (USECS_PER_SEC,
                                   cw_finalization_clock_internal);

      /*
       * Set the flag and countdown last; calling cw_request_timeout_internal
       * above results in a call to our cw_cancel_finalization, which clears
       * the flag and countdown if we set them early.
       */
      cw_is_finalization_pending = true;
      cw_finalization_countdown = FINALIZATION_DELAY / USECS_PER_SEC;

      cw_debug (CW_DEBUG_FINALIZATION, "finalization scheduled");
    }
}





/**
   See documentation of cw_schedule_finalization_internal() for more information
*/
void cw_cancel_finalization_internal(void)
{
  if (cw_is_finalization_pending)
    {
      /* Cancel pending finalization and return to doing nothing. */
      cw_is_finalization_pending = false;
      cw_finalization_countdown = 0;

      cw_debug (CW_DEBUG_FINALIZATION, "finalization canceled");
    }
}





/**
 * Reset all library features to their default states.  Clears the tone
 * queue, receive buffers and retained state information, any current
 * keyer activity, and any straight key activity, returns to silence, and
 * closes soundcard and console devices.  This function is suitable for
 * calling from an application exit handler.
 */
void cw_complete_reset(void)
{
  /*
   * If the finalizer thinks it's pending, stop it, then temporarily lock
   * out finalizations.
   */
  cw_cancel_finalization_internal ();
  cw_is_finalization_locked_out = true;

  /* Silence sound, and shutdown use of the sound devices. */
  //cw_sound_soundcard_internal (CW_TONE_SILENT);
  cw_release_sound_internal ();
  cw_release_timeouts_internal ();

  /* Call the reset functions for each subsystem. */
  cw_reset_tone_queue ();
  cw_reset_receive ();
  cw_reset_keyer ();
  cw_reset_straight_key ();

  /* Now we can re-enable delayed finalizations. */
  cw_is_finalization_locked_out = false;
}


/**
 * Signal handler function registered when cw_register_signal_handler is
 * requested.  Resets the library, and then either calls any supplied
 * sub-handler, exits (if SIG_DFL) or continues (if SIG_IGN).
 */
void cw_interpose_signal_handler_internal(int signal_number)
{
  void (*callback_func) (int);

  cw_debug (CW_DEBUG_FINALIZATION, "caught signal %d", signal_number);

  /* Reset the library and retrieve the signal's handler. */
  cw_complete_reset ();
  callback_func = cw_signal_callbacks[signal_number];

  /* The default action is to stop the process; exit(1) seems to cover it. */
  if (callback_func == SIG_DFL)
    exit (EXIT_FAILURE);

  /* If we didn't exit, invoke any additional handler callback function. */
  if (callback_func != SIG_IGN)
    (*callback_func) (signal_number);
}


/**
 * Register a signal handler and optional callback function for signals.  On
 * receipt of that signal, all library features will be reset to their default
 * states.  Following the reset, if callback_func is a function pointer, the
 * function is called; if it is SIG_DFL, the library calls exit(); and if it
 * is SIG_IGN, the library returns from the signal handler.  This is a
 * convenience function for clients that need to clean up library on signals,
 * with either exit, continue, or an additional function call; in effect, a
 * wrapper round a restricted form of sigaction.  The signal_number argument
 * indicates which signal to catch.  Returns true if the signal handler
 * installs correctly, false otherwise, with errno set to EINVAL if
 * signal_number is invalid or if a handler is already installed for that
 * signal, or to the sigaction error code.
 */
int cw_register_signal_handler(int signal_number,
			       void (*callback_func) (int))
{
  static bool is_initialized = false;

  struct sigaction action, original_disposition;
  int status;

  /* On first call, initialize all signal_callbacks to SIG_DFL. */
  if (!is_initialized)
    {
      int index;

      for (index = 0; index < RTSIG_MAX; index++)
        cw_signal_callbacks[index] = SIG_DFL;

      is_initialized = true;
    }

  /* Reject invalid signal numbers, and SIGALRM, which we use internally. */
  if (signal_number < 0 || signal_number >= RTSIG_MAX
      || signal_number == SIGALRM)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }

  /* Install our handler as the actual handler. */
  action.sa_handler = cw_interpose_signal_handler_internal;
  action.sa_flags = SA_RESTART;
  sigemptyset (&action.sa_mask);
  status = sigaction (signal_number, &action, &original_disposition);
  if (status == -1)
    {
      perror ("cw: sigaction");
      return CW_FAILURE;
    }

  /* If we trampled another handler, replace it and return false. */
  if (!(original_disposition.sa_handler == cw_interpose_signal_handler_internal
      || original_disposition.sa_handler == SIG_DFL
      || original_disposition.sa_handler == SIG_IGN))
    {
      status = sigaction (signal_number, &original_disposition, NULL);
      if (status == -1)
        {
          perror ("cw: sigaction");
          return CW_FAILURE;
        }

      errno = EINVAL;
      return CW_FAILURE;
    }

  /* Save the callback function (it may validly be SIG_DFL or SIG_IGN). */
  cw_signal_callbacks[signal_number] = callback_func;
  return CW_SUCCESS;
}


/**
 * Removes a signal handler interception previously registered with
 * cw_register_signal_handler.  Returns true if the signal handler uninstalls
 * correctly, false otherwise, with errno set to EINVAL or to the sigaction
 * error code.
 */
int cw_unregister_signal_handler(int signal_number)
{
  struct sigaction action, original_disposition;
  int status;

  /* As above, reject unacceptable signal numbers. */
  if (signal_number < 0 || signal_number >= RTSIG_MAX
      || signal_number == SIGALRM)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }

  /* See if the current handler was put there by us. */
  status = sigaction (signal_number, NULL, &original_disposition);
  if (status == -1)
    {
      perror ("cw: sigaction");
      return CW_FAILURE;
    }
  if (original_disposition.sa_handler != cw_interpose_signal_handler_internal)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }

  /* Remove the signal handler by resetting to SIG_DFL. */
  action.sa_handler = SIG_DFL;
  action.sa_flags = 0;
  sigemptyset (&action.sa_mask);
  status = sigaction (signal_number, &action, NULL);
  if (status == -1)
    {
      perror ("cw: sigaction");
      return CW_FAILURE;
    }

  /* Reset the callback entry for tidiness. */
  cw_signal_callbacks[signal_number] = SIG_DFL;
  return CW_SUCCESS;
}


/*---------------------------------------------------------------------*/
/*  Keying control                                                     */
/*---------------------------------------------------------------------*/

/*
 * External keying function.  It may be useful for a client to have this
 * library control an external keying device, for example, an oscillator,
 * or a transmitter.  Here is where we keep the address of a function that
 * is passed to us for this purpose, and a void* argument for it.
 */
static void (*cw_kk_key_callback)(void*, int) = NULL;
static void *cw_kk_key_callback_arg = NULL;


/**
 * Register a function that should be called when a tone state changes from
 * key-up to key-down, or vice-versa.  The first argument passed out to the
 * registered function is the supplied callback_arg, if any.  The second
 * argument passed out is the key state: true for down, false for up.  Calling
 * this routine with an NULL function address disables keying callbacks.  Any
 * callback supplied will be called in signal handler context.
 */
void cw_register_keying_callback(void (*callback_func)(void*, int),
				 void *callback_arg)
{
  cw_kk_key_callback = callback_func;
  cw_kk_key_callback_arg = callback_arg;
}


/**
 * Control function that calls any requested keying callback only when there
 * is a change of keying state.  This function filters successive key-down
 * or key-up actions into a single action.
 */
void cw_key_control_internal(int requested_key_state)
{
  static bool current_key_state = false;  /* Maintained key control state */

  if (current_key_state != requested_key_state)
    {
      cw_debug (CW_DEBUG_KEYING, "keying state %d->%d", current_key_state, requested_key_state);

      /* Set the new keying state, and call any requested callback. */
      current_key_state = requested_key_state;
      if (cw_kk_key_callback)
        (*cw_kk_key_callback) (cw_kk_key_callback_arg, current_key_state);
    }
}


/*---------------------------------------------------------------------*/
/*  Tone queue                                                         */
/*---------------------------------------------------------------------*/

/*
 * Tone queue.  This is a circular list of tone durations and frequencies
 * pending, and a pair of indexes, tail (enqueue) and head (dequeue) to
 * manage additions and asynchronous sending.
 */
enum
{ TONE_QUEUE_CAPACITY = 3000,        /* ~= 5 minutes at 12 WPM */
  TONE_QUEUE_HIGH_WATER_MARK = 2900  /* Refuse characters if <100 free */
};
typedef struct
{
  int usecs;      /* Tone duration in usecs */
  int frequency;  /* Frequency of the tone */
} cw_queued_tone_t;

static volatile cw_queued_tone_t cw_tone_queue[TONE_QUEUE_CAPACITY];
static volatile int cw_tq_tail = 0,  /* Tone queue tail index */
                    cw_tq_head = 0;  /* Tone queue head index */

/*
 * It's useful to have the tone queue dequeue function call a client-supplied
 * callback routine when the amount of data in the queue drops below a
 * defined low water mark.  This routine can then refill the buffer, as
 * required.
 */
static volatile int cw_tq_low_water_mark = 0;
static void (*cw_tq_low_water_callback)(void*) = NULL;
static void *cw_tq_low_water_callback_arg = NULL;


/**
 * Return the count of tones currently held in the circular tone buffer, and
 * advance a tone queue index, including circular wrapping.
 */
int cw_get_tone_queue_length_internal(void)
{
  return cw_tq_tail >= cw_tq_head
         ? cw_tq_tail - cw_tq_head
         : cw_tq_tail - cw_tq_head + TONE_QUEUE_CAPACITY;
}





/**
   See documentation of cw_get_tone_queue_length_internal() for more information
*/
int cw_next_tone_queue_index_internal(int current)
{
  return (current + 1) % TONE_QUEUE_CAPACITY;
}





/*
 * The CW tone queue functions implement the following state graph:
 *
 *          (queue empty)
 *        +-------------------------------+
 *        |                               |
 *        v    (queue started)            |
 * --> QS_IDLE ---------------> QS_BUSY --+
 *                              ^     |
 *                              |     |
 *                              +-----+
 *                          (queue not empty)
 */
static volatile enum { QS_IDLE, QS_BUSY } cw_dequeue_state = QS_IDLE;


/**
 * Signal handler for itimer.  Dequeue a tone request, and send the ioctl to
 * generate the tone.  If the queue is empty when we get the signal, then
 * we're at the end of the work list, so set the dequeue state to idle and
 * return.
 */
void cw_tone_queue_clock_internal(void)
{
  /* Decide what to do based on the current state. */
  switch (cw_dequeue_state)
    {
    /* Ignore calls if our state is idle. */
    case QS_IDLE:
      return;

    /*
     * If busy, dequeue the next tone, or if no more tones, go to the idle
     * state.
     */
    case QS_BUSY:
      if (cw_tq_head != cw_tq_tail)
        {
          int usecs, frequency, queue_length;

          /*
           * Get the current queue length.  Later on, we'll compare with the
           * length after we've scanned over every tone we can omit, and use it
           * to see if we've crossed the low water mark, if any.
           */
          queue_length = cw_get_tone_queue_length_internal ();

          /*
           * Advance over the tones list until we find the first tone with a
           * duration of more than zero usecs, or until the end of the list.
           */
          do
            {
              cw_tq_head = cw_next_tone_queue_index_internal (cw_tq_head);
            }
          while (cw_tq_head != cw_tq_tail
                 && cw_tone_queue[cw_tq_head].usecs == 0);

          /* Dequeue the next tone to send. */
          usecs = cw_tone_queue[cw_tq_head].usecs;
          frequency = cw_tone_queue[cw_tq_head].frequency;

	  cw_debug (CW_DEBUG_TONE_QUEUE, "dequeue tone %d usec, %d Hz", usecs, frequency);

          /*
           * Start the tone.  If the ioctl fails, there's nothing we can do at
           * this point, in the way of returning error codes.
           */
          cw_sound_internal (frequency);

          /*
           * Notify the key control function that there might have been a
           * change of keying state (and then again, there might not have
           * been -- it will sort this out for us).
           */
          cw_key_control_internal (frequency != CW_TONE_SILENT);

          /*
           * If microseconds is zero, leave it at that.  This way, a queued
           * tone of 0 usec implies leaving the sound in this state, and 0
           * usec and 0 frequency leaves silence.
           */
          if (usecs > 0)
            {
              /*
               * Request a timeout.  If it fails, there's little we can do at
               * this point.  But it shouldn't fail.
               */
              cw_request_timeout_internal (usecs, NULL);
            }
          else
            {
              /* Autonomous dequeuing has finished for the moment. */
              cw_dequeue_state = QS_IDLE;
              cw_schedule_finalization_internal ();
            }

          /*
           * If there is a low water mark callback registered, and if we passed
           * under the water mark, call the callback here.  We want to be sure
           * to call this late in the processing, especially after setting the
           * state to idle, since the most likely action of this routine is to
           * queue tones, and we don't want to play with the state here after
           * that.
           */
          if (cw_tq_low_water_callback)
            {
              /*
               * If the length we originally calculated was above the low water
               * mark, and the one we have now is below or equal to it, call
               * the callback.
               */
              if (queue_length > cw_tq_low_water_mark
                  && cw_get_tone_queue_length_internal ()
                     <= cw_tq_low_water_mark)
                (*cw_tq_low_water_callback) (cw_tq_low_water_callback_arg);
            }
        }
      else
        {
          /*
           * This is the end of the last tone on the queue, and since we got a
           * signal we know that it had a usec greater than zero.  So this is
           * the time to return to silence.
           */
          cw_sound_internal (CW_TONE_SILENT);

          /* Notify the keying control function, as above. */
          cw_key_control_internal (false);

          /*
           * Set state to idle, indicating that autonomous dequeueing has
           * finished for the moment.  We need this set whenever the queue
           * indexes are equal and there is no pending itimeout.
           */
          cw_dequeue_state = QS_IDLE;
          cw_schedule_finalization_internal ();
        }
    }
}


/**
 * Enqueue a tone for specified frequency and number of microseconds.  This
 * routine adds the new tone to the queue, and if necessary starts the
 * itimer process to have the tone sent.  The routine returns true on success.
 * If the tone queue is full, the routine returns false, with errno set to
 * EAGAIN.  If the iambic keyer or straight key are currently busy, the
 * routine returns false, with errno set to EBUSY.
 */
int cw_enqueue_tone_internal(int usecs, int frequency)
{
  int new_tq_tail;

  /*
   * If the keyer or straight key are busy, return an error.  This is because
   * they use the sound card/console tones and key control, and will interfere
   * with us if we try to use them at the same time.
   */
  if (cw_is_keyer_busy () || cw_is_straight_key_busy ())
    {
      errno = EBUSY;
      return CW_FAILURE;
    }

  /* Get the new value of the queue tail index. */
  new_tq_tail = cw_next_tone_queue_index_internal (cw_tq_tail);

  /*
   * If the new value is bumping against the head index, then the queue
   * is currently full, so return EAGAIN.
   */
  if (new_tq_tail == cw_tq_head)
    {
      errno = EAGAIN;
      return CW_FAILURE;
    }

  cw_debug (CW_DEBUG_TONE_QUEUE, "enqueue tone %d usec, %d Hz", usecs, frequency);

  /* Set the new tail index, and enqueue the new tone. */
  cw_tq_tail = new_tq_tail;
  cw_tone_queue[cw_tq_tail].usecs = usecs;
  cw_tone_queue[cw_tq_tail].frequency = frequency;

  /*
   * If there is currently no autonomous dequeue happening, kick off the
   * itimer process.
   */
  if (cw_dequeue_state == QS_IDLE)
    {
      cw_dequeue_state = QS_BUSY;
      cw_request_timeout_internal (0, cw_tone_queue_clock_internal);
    }

  return CW_SUCCESS;
}


/**
 * Registers a function to be called automatically by the dequeue routine
 * whenever the tone queue falls to a given level; callback_arg may be used
 * to give a value passed back on callback calls.  A NULL function pointer
 * suppresses callbacks.  On success, the routine returns true.  If level is
 * invalid, the routine returns false with errno set to EINVAL.  Any callback
 * supplied will be called in signal handler context.
 */
int cw_register_tone_queue_low_callback(void (*callback_func)(void*),
					void *callback_arg, int level)
{
  if (level < 0 || level >= TONE_QUEUE_CAPACITY - 1)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }

  /* Store the function and low water mark level. */
  cw_tq_low_water_mark = level;
  cw_tq_low_water_callback = callback_func;
  cw_tq_low_water_callback_arg = callback_arg;

  return CW_SUCCESS;
}


/**
 * Indicates if the tone sender is busy; returns true if there are still
 * entries in the tone queue, false if the queue is empty.
 */
bool cw_is_tone_busy(void)
{
	return cw_dequeue_state != QS_IDLE;
}


/**
 * Wait for the current tone to complete.  The routine returns true on
 * success.  If called with SIGALRM blocked, the routine returns false, with
 * errno set to EDEADLK, to avoid indefinite waits.
 */
int cw_wait_for_tone(void)
{
  int status, check_tq_head;

  /* Check that SIGALRM is not blocked. */
  status = cw_check_signal_mask_internal ();
  if (!status)
    return CW_FAILURE;

  /* Wait for the tail index to change or the dequeue to go idle. */
  check_tq_head = cw_tq_head;
  while (cw_tq_head == check_tq_head && cw_dequeue_state != QS_IDLE)
    cw_wait_for_signal_internal ();

  return CW_SUCCESS;
}


/**
 * Wait for the tone queue to drain.  The routine returns true on success.
 * If called with SIGALRM blocked, the routine returns false, with errno set
 * to EDEADLK, to avoid indefinite waits.
 */
int cw_wait_for_tone_queue(void)
{
  int status;

  /* Check that SIGALRM is not blocked. */
  status = cw_check_signal_mask_internal ();
  if (!status)
    return CW_FAILURE;

  /* Wait until the dequeue indicates it's hit the end of the queue. */
  while (cw_dequeue_state != QS_IDLE)
    cw_wait_for_signal_internal ();

  return CW_SUCCESS;
}


/**
 * Wait for the tone queue to drain until only as many tones as given
 * in level remain queued.  This routine is for use by programs that want
 * to optimize themselves to avoid the cleanup that happens when the tone
 * queue drains completely; such programs have a short time in which to
 * add more tones to the queue.  The routine returns true on success.  If
 * called with SIGALRM blocked, the routine returns false, with errno set to
 * EDEADLK, to avoid indefinite waits.
 */
int cw_wait_for_tone_queue_critical(int level)
{
  int status;

  /* Check that SIGALRM is not blocked. */
  status = cw_check_signal_mask_internal ();
  if (!status)
    return CW_FAILURE;

  /* Wait until the queue length is at or below criticality. */
  while (cw_get_tone_queue_length_internal () > level)
    cw_wait_for_signal_internal ();

  return CW_SUCCESS;
}


/**
 * Indicates if the tone queue is full, returning true if full, false if not.
 */
bool cw_is_tone_queue_full(void)
{
	/* If advancing would meet the tail index, return true. */
	return cw_next_tone_queue_index_internal(cw_tq_tail) == cw_tq_head;
}


/**
 * Returns the number of entries the tone queue can accommodate.
 */
int cw_get_tone_queue_capacity(void)
{
  /*
   * Since the head and tail indexes cannot be equal, the perceived capacity
   * for the client is always one less than the actual declared queue size.
   */
  return TONE_QUEUE_CAPACITY - 1;
}


/**
 * Returns the number of entries currently pending in the tone queue.
 */
int cw_get_tone_queue_length(void)
{
  return cw_get_tone_queue_length_internal ();
}


/**
 * Cancel all pending queued tones, and return to silence.  If there is a
 * tone in progress, the function will wait until this last one has
 * completed, then silence the tones.
 *
 * This function may be called with SIGALRM blocked, in which case it
 * will empty the queue as best it can, then return without waiting for
 * the final tone to complete.  In this case, it may not be possible to
 * guarantee silence after the call.
 */
void cw_flush_tone_queue(void)
{
  /* Empty the queue, by setting the head to the tail. */
  cw_tq_head = cw_tq_tail;

  /* If we can, wait until the dequeue goes idle. */
  if (cw_check_signal_mask_internal ())
    cw_wait_for_tone_queue ();

  /*
   * Force silence on the speaker anyway, and stop any background soundcard
   * tone generation.
   */
  cw_sound_internal (CW_TONE_SILENT);
  cw_schedule_finalization_internal ();
}


/**
 * Provides primitive access to simple tone generation.  This routine queues
 * a tone of given duration and frequency.  The routine returns true on
 * success.  If usec or frequency are invalid, it returns false with errno
 * set to EINVAL.  If the sound card, console speaker, or keying function are
 * busy, it returns false with errno set to EBUSY.  If the tone queue is full,
 * it returns false with errno set to EAGAIN.
 */
int cw_queue_tone(int usecs, int frequency)
{
  /*
   * Check the arguments given for realistic values.  Note that we do nothing
   * here to protect the caller from setting up neverending (0 usecs) tones,
   * if that's what they want to do.
   */
  if (usecs < 0 || frequency < 0
      || frequency < CW_FREQUENCY_MIN || frequency > CW_FREQUENCY_MAX)
    {
      errno = EINVAL;
      return CW_FAILURE;
    }

  return cw_enqueue_tone_internal (usecs, frequency);
}


/**
 * Cancel all pending queued tones, reset any queue low callback registered,
 * and return to silence.  This function is suitable for calling from an
 * application exit handler.
 */
void cw_reset_tone_queue(void)
{
  /* Empty the queue, and force state to idle. */
  cw_tq_head = cw_tq_tail;
  cw_dequeue_state = QS_IDLE;

  /* Reset low water mark details to their initial values. */
  cw_tq_low_water_mark = 0;
  cw_tq_low_water_callback = NULL;
  cw_tq_low_water_callback_arg = NULL;

  /* Silence sound and stop any background soundcard tone generation. */
  cw_sound_internal (CW_TONE_SILENT);
  cw_schedule_finalization_internal ();

  cw_debug (CW_DEBUG_TONE_QUEUE, "tone queue reset");
}


/*---------------------------------------------------------------------*/
/*  Sending                                                            */
/*---------------------------------------------------------------------*/

/**
 * Low level primitive to send a tone element of the given type, followed
 * by the standard inter-element silence.
 */
int cw_send_element_internal(char element)
{
  int status;

  /* Synchronize low-level timings if required. */
  cw_sync_parameters_internal ();

  /* Send either a dot or a dash element, depending on representation. */
  if (element == CW_DOT_REPRESENTATION)
    status = cw_enqueue_tone_internal (cw_send_dot_length, generator->frequency);
  else if (element == CW_DASH_REPRESENTATION)
    status = cw_enqueue_tone_internal (cw_send_dash_length, generator->frequency);
  else
    {
      errno = EINVAL;
      status = CW_FAILURE;
    }
  if (!status)
    return CW_FAILURE;

  /* Send the inter-element gap. */
  status = cw_enqueue_tone_internal (cw_end_of_ele_delay, CW_TONE_SILENT);
  if (!status)
    return CW_FAILURE;

  return CW_SUCCESS;
}


/**
 * cw_send_[dot|dash|character_space|word_space]()
 *
 * Low level primitives, available to send single dots, dashes, character
 * spaces, and word spaces.  The dot and dash routines always append the
 * normal inter-element gap after the tone sent.  The cw_send_character_space
 * routine sends space timed to exclude the expected prior dot/dash
 * inter-element gap.  The cw_send_word_space routine sends space timed to
 * exclude both the expected prior dot/dash inter-element gap and the prior
 * end of character space.  These functions return true on success, or false
 * with errno set to EBUSY or EAGAIN on error.
 */
int cw_send_dot(void)
{
  return cw_send_element_internal (CW_DOT_REPRESENTATION);
}





/**
   See documentation of cw_send_dot() for more information
*/
int cw_send_dash(void)
{
  return cw_send_element_internal (CW_DASH_REPRESENTATION);
}





/**
   See documentation of cw_send_dot() for more information
*/
int cw_send_character_space(void)
{
  /* Synchronize low-level timing parameters. */
  cw_sync_parameters_internal ();

  /*
   * Delay for the standard end of character period, plus any additional
   * inter-character gap
   */
  return cw_enqueue_tone_internal (cw_end_of_char_delay + cw_additional_delay,
                                   CW_TONE_SILENT);
}





/**
   See documentation of cw_send_dot() for more information
*/
int cw_send_word_space(void)
{
  /* Synchronize low-level timing parameters. */
  cw_sync_parameters_internal ();

  /*
   * Send silence for the word delay period, plus any adjustment that may be
   * needed at end of word.
   */
  return cw_enqueue_tone_internal (cw_end_of_word_delay + cw_adjustment_delay,
                                   CW_TONE_SILENT);
}





/**
 * Send the given string as dots and dashes, adding the post-character
 * gap.
 */
int cw_send_representation_internal(const char *representation, int partial)
{
  int index;

  /*
   * Before we let this representation loose on tone generation, we'd really
   * like to know that all of its tones will get queued up successfully.  The
   * right way to do this is to calculate the number of tones in our
   * representation, then check that the space exists in the tone queue.
   * However, since the queue is comfortably long, we can get away with just
   * looking for a high water mark.
   */
  if (cw_get_tone_queue_length () >= TONE_QUEUE_HIGH_WATER_MARK)
    {
      errno = EAGAIN;
      return CW_FAILURE;
    }

  /* Sound the elements of the CW equivalent. */
  for (index = 0; representation[index] != '\0'; index++)
    {
      int status;

      /*
       * Send a tone of dot or dash length, followed by the normal, standard,
       * inter-element gap.
       */
      status = cw_send_element_internal (representation[index]);
      if (!status)
        return CW_FAILURE;
    }

  /*
   * If this representation is stated as being 'partial', then suppress any
   * and all end of character delays.
   */
  if (!partial)
    {
      int status;

      status = cw_send_character_space ();
      if (!status)
        return CW_FAILURE;
    }

  return CW_SUCCESS;
}


/**
 * Checks, then sends the given string as dots and dashes.  The representation
 * passed in is assumed to be a complete Morse character; that is, all post-
 * character delays will be added when the character is sent.  On success,
 * the routine returns true.  On error, it returns false, with errno set to
 * EINVAL if any character of the representation is invalid, EBUSY if the
 * sound card, console speaker, or keying system is busy, or EAGAIN if the
 * tone queue is full, or if there is insufficient space to queue the tones
 * for the representation.
 */
int cw_send_representation(const char *representation)
{
  if (!cw_check_representation (representation))
    {
      errno = EINVAL;
      return CW_FAILURE;
    }

  return cw_send_representation_internal (representation, false);
}


/**
 * Check, then send the given string as dots and dashes.  The representation
 * passed in is assumed to be only part of a larger Morse representation;
 * that is, no post-character delays will be added when the character is sent.
 * On success, the routine returns true.  On error, it returns false, with
 * errno set to EINVAL if any character of the representation is invalid,
 * EBUSY if the sound card, console speaker, or keying system is busy, or
 * EAGAIN if the tone queue is full, or if there is insufficient space to
 * queue the tones for the representation.
 */
int cw_send_representation_partial(const char *representation)
{
  if (!cw_check_representation (representation))
    {
      errno = ENOENT;
      return CW_FAILURE;
    }

  return cw_send_representation_internal (representation, true);
}


/**
 * Lookup, and send a given ASCII character as cw.  If 'partial' is set, the
 * end of character delay is not appended to the Morse sent. On success,
 * the routine returns true, otherwise it returns an error.
 */
int cw_send_character_internal(char c, int partial)
{
  int status;
  const char *representation;

  /* Handle space special case; delay end-of-word and return. */
  if (c == ' ')
    return cw_send_word_space ();

  /* Lookup the character, and sound it. */
  representation = cw_lookup_character_internal (c);
  if (!representation)
    {
      errno = ENOENT;
      return CW_FAILURE;
    }

  status = cw_send_representation_internal (representation, partial);
  if (!status)
    return CW_FAILURE;

  return CW_SUCCESS;
}


/**
 * Checks that the given character is validly sendable in Morse.  If it is,
 * the routine returns true.  If not, the routine returns false, with errno
 * set to ENOENT.
 */
int cw_check_character(char c)
{
  /*
   * If the character is the space special-case, or if not, but it is in the
   * lookup table, return success.
   */
  if (c == ' ' || cw_lookup_character_internal (c))
    return CW_SUCCESS;

  errno = ENOENT;
  return CW_FAILURE;
}


/**
 * Lookup, and send a given ASCII character as Morse.  The end of character
 * delay is appended to the Morse sent. On success, the routine returns true.
 * On error, it returns false, with errno set to ENOENT if the given character
 * is not a valid Morse character, EBUSY if the sound card, console speaker,
 * or keying system is busy, or EAGAIN if the tone queue is full, or if
 * there is insufficient space to queue the tones for the representation.
 *
 * This routine returns as soon as the character has been successfully
 * queued for send; that is, almost immediately.  The actual sending happens
 * in background processing.  See cw_wait_for_tone and cw_wait_for_tone_queue
 * for ways to check the progress of sending.
 */
int cw_send_character(char c)
{
  if (!cw_check_character (c))
    {
      errno = ENOENT;
      return CW_FAILURE;
    }

  return cw_send_character_internal (c, false);
}


/**
 * Lookup, and send a given ASCII character as Morse.  The end of character
 * delay is not appended to the Morse sent by the function, to support the
 * formation of combination characters. On success, the routine returns true.
 * On error, it returns false, with errno set to ENOENT if the given character
 * is not a valid Morse character, EBUSY if the sound card, console speaker,
 * or keying system is busy, or EAGAIN if the tone queue is full, or if
 * there is insufficient space to queue the tones for the representation.
 *
 * This routine queues its arguments for background processing.  See
 * cw_send_character for details of how to check the queue status.
 */
int cw_send_character_partial(char c)
{
  if (!cw_check_character (c))
    {
      errno = ENOENT;
      return CW_FAILURE;
    }

  return cw_send_character_internal (c, true);
}


/**
 * Checks that each character in the given string is validly sendable in Morse.
 * On success, the routine returns true.  On error, it returns false, with
 * errno set to EINVAL.
 */
int cw_check_string(const char *string)
{
  int index;

  /*
   * Check that each character in the string has a Morse representation, or
   * is the space special case.
   */
  for (index = 0; string[index] != '\0'; index++)
    {
      if (!(string[index] == ' '
            || cw_lookup_character_internal (string[index])))
        {
          errno = EINVAL;
          return CW_FAILURE;
        }
    }

  return CW_SUCCESS;
}


/**
 * Send a given ASCII string as cw.  On success, the routine returns true.
 * On error, it returns false, with errno set to ENOENT if any character in
 * the string is not a valid Morse character, EBUSY if the sound card, console
 * speaker, or keying system is in use by the iambic keyer or the straight
 * key, or EAGAIN if the tone queue is full.  If the tone queue runs out
 * of space part way through queueing the string, the function returns EAGAIN.
 * However, an indeterminate number of the characters from the string will
 * have already been queued.  For safety, clients can ensure the tone queue
 * is empty before queueing a string, or use cw_send_character() if they
 * need finer control.
 *
 * This routine queues its arguments for background processing.  See
 * cw_send_character for details of how to check the queue status.
 */
int cw_send_string(const char *string)
{
  int index;

  /* Check the string is composed of sendable characters. */
  if (!cw_check_string (string))
    {
      errno = ENOENT;
      return CW_FAILURE;
    }

  /* Send every character in the string. */
  for (index = 0; string[index] != '\0'; index++)
    {
      int status;

      status = cw_send_character_internal (string[index], false);
      if (!status)
        return CW_FAILURE;
    }

  return CW_SUCCESS;
}


/*---------------------------------------------------------------------*/
/*  Receive tracking and statistics helpers                            */
/*---------------------------------------------------------------------*/

/*
 * Receive adaptive speed tracking.  A moving averages structure, comprising
 * a small array of element lengths, a circular index into the array, and a
 * a running sum of elements for efficient calculation of moving averages.
 */
enum { AVERAGE_ARRAY_LENGTH = 4 };
typedef struct {
  int buffer[AVERAGE_ARRAY_LENGTH];  /* Buffered element lengths */
  int cursor;                        /* Circular buffer cursor */
  int sum;                           /* Running sum */
} cw_tracking_t;

static cw_tracking_t cw_dot_tracking = { {0}, 0, 0 },
                     cw_dash_tracking = { {0}, 0, 0 };

static void cw_reset_adaptive_average_internal(cw_tracking_t *tracking, int initial);
static void cw_update_adaptive_average_internal(cw_tracking_t *tracking, int element_usec);
static int cw_get_adaptive_average_internal(cw_tracking_t *tracking);

/**
 * Moving average functions for smoothed tracking of dot and dash lengths.
 */
void cw_reset_adaptive_average_internal(cw_tracking_t *tracking, int initial)
{
  int element;

  for (element = 0; element < AVERAGE_ARRAY_LENGTH; element++)
    tracking->buffer[element] = initial;
  tracking->sum = initial * AVERAGE_ARRAY_LENGTH;
  tracking->cursor = 0;
}





/**
   See documentation of cw_reset_adaptive_average_internal() for more information
*/
void cw_update_adaptive_average_internal(cw_tracking_t *tracking, int element_usec)
{
  tracking->sum += element_usec - tracking->buffer[tracking->cursor];
  tracking->buffer[tracking->cursor++] = element_usec;
  tracking->cursor %= AVERAGE_ARRAY_LENGTH;
}





/**
   See documentation of cw_reset_adaptive_average_internal() for more information
*/
int cw_get_adaptive_average_internal(cw_tracking_t *tracking)
{
  return tracking->sum / AVERAGE_ARRAY_LENGTH;
}





/*
 * Receive timing statistics.  A circular buffer of entries indicating the
 * difference between the actual and the ideal timing for a receive element,
 * tagged with the type of statistic held, and a circular buffer pointer.
 * STAT_NONE must be zero so that the statistics buffer is initially empty.
 */
typedef enum
{ STAT_NONE = 0, STAT_DOT, STAT_DASH, STAT_END_ELEMENT, STAT_END_CHARACTER
} stat_type_t;

typedef struct {
  stat_type_t type;  /* Record type */
  int delta;         /* Difference between actual and ideal timing */
} cw_statistics_t;

enum { STATISTICS_ARRAY_LENGTH = 256 };
static cw_statistics_t
  cw_receive_statistics[STATISTICS_ARRAY_LENGTH] = { {0, 0} };
static int cw_statistics_cursor = 0;

static void cw_add_receive_statistic_internal(stat_type_t type, int usecs);
static double cw_get_receive_statistic_internal(stat_type_t type);

/**
 * Add an element timing with a given statistic type to the circular
 * statistics buffer.  The buffer stores only the delta from the ideal value;
 * the ideal is inferred from the type passed in.
 */
void cw_add_receive_statistic_internal(stat_type_t type, int usecs)
{
  int delta;

  /* Synchronize low-level timings if required. */
  cw_sync_parameters_internal ();

  /* Calculate delta as difference between usec and the ideal value. */
  delta = usecs - ((type == STAT_DOT) ? cw_receive_dot_length
                 : (type == STAT_DASH) ? cw_receive_dash_length
                 : (type == STAT_END_ELEMENT) ? cw_eoe_range_ideal
                 : (type == STAT_END_CHARACTER) ? cw_eoc_range_ideal : usecs);

  /* Add this statistic to the buffer. */
  cw_receive_statistics[cw_statistics_cursor].type = type;
  cw_receive_statistics[cw_statistics_cursor++].delta = delta;
  cw_statistics_cursor %= STATISTICS_ARRAY_LENGTH;
}


/**
 * Calculate and return one given timing statistic type.  If no records of
 * that type were found, return 0.0.
 */
double cw_get_receive_statistic_internal(stat_type_t type)
{
  double sum_of_squares;
  int count, cursor;

  /*
   * Sum and count elements matching the given type.  A cleared buffer always
   * begins refilling at element zero, so to optimize we can stop on the first
   * unoccupied slot in the circular buffer.
   */
  sum_of_squares = 0.0;
  count = 0;
  for (cursor = 0; cursor < STATISTICS_ARRAY_LENGTH; cursor++)
    {
      if (cw_receive_statistics[cursor].type == type)
        {
          int delta;

          delta = cw_receive_statistics[cursor].delta;
          sum_of_squares += (double) delta * (double) delta;
          count++;
        }
      else if (cw_receive_statistics[cursor].type == STAT_NONE)
        break;
    }

  /* Return the standard deviation, or zero if no matching elements. */
  return count > 0 ? sqrt (sum_of_squares / (double) count) : 0.0;
}


/**
 * Calculate and return receive timing statistics.  These statistics may be
 * used to obtain a measure of the accuracy of received CW.  The values
 * dot_sd and dash_sd contain the standard deviation of dot and dash lengths
 * from the ideal values, and element_end_sd and character_end_sd the
 * deviations for inter element and inter character spacing.  Statistics are
 * held for all timings in a 256 element circular buffer.  If any statistic
 * cannot be calculated, because no records for it exist, the returned value
 * is 0.0.  Use NULL for the pointer argument to any statistic not required.
 */
void cw_get_receive_statistics(double *dot_sd, double *dash_sd,
			       double *element_end_sd, double *character_end_sd)
{
  if (dot_sd)
    *dot_sd = cw_get_receive_statistic_internal (STAT_DOT);
  if (dash_sd)
    *dash_sd = cw_get_receive_statistic_internal (STAT_DASH);
  if (element_end_sd)
    *element_end_sd = cw_get_receive_statistic_internal (STAT_END_ELEMENT);
  if (character_end_sd)
    *character_end_sd = cw_get_receive_statistic_internal (STAT_END_CHARACTER);
}


/**
 * Clear the receive statistics buffer, removing all records from it and
 * returning it to its initial default state.
 */
void cw_reset_receive_statistics(void)
{
  int cursor;

  for (cursor = 0; cursor < STATISTICS_ARRAY_LENGTH; cursor++)
    {
      cw_receive_statistics[cursor].type = STAT_NONE;
      cw_receive_statistics[cursor].delta = 0;
    }
  cw_statistics_cursor = 0;
}


/*---------------------------------------------------------------------*/
/*  Receiving                                                          */
/*---------------------------------------------------------------------*/

/*
 * Receive buffering.  This is a fixed-length representation, filled in
 * as tone on/off timings are taken.  The buffer is vastly longer than
 * any practical representation, and along with it we maintain a cursor
 * indicating the current write position.
 */
enum { RECEIVE_CAPACITY = 256 };
static char cw_receive_representation_buffer[RECEIVE_CAPACITY];
static int cw_rr_current = 0;

/* Retained tone start and end timestamps. */
static struct timeval cw_rr_start_timestamp = {0, 0},
                      cw_rr_end_timestamp = {0, 0};

/**
 * Set the value of the flag that controls whether, on receive, the receive
 * functions do fixed speed receive, or track the speed of the received Morse
 * code by adapting to the input stream.
 */
void cw_set_adaptive_receive_internal(bool flag)
{
  /* Look for change of adaptive receive state. */
  if ((cw_is_adaptive_receive_enabled && !flag)
      || (!cw_is_adaptive_receive_enabled && flag))
    {
      cw_is_adaptive_receive_enabled = flag;

      /* Changing the flag forces a change in low-level parameters. */
      cw_is_in_sync = false;
      cw_sync_parameters_internal ();

      /*
       * If we have just switched to adaptive mode, (re-)initialize the
       * averages array to the current dot/dash lengths, so that initial
       * averages match the current speed.
       */
      if (cw_is_adaptive_receive_enabled)
        {
          cw_reset_adaptive_average_internal (&cw_dot_tracking,
                                              cw_receive_dot_length);
          cw_reset_adaptive_average_internal (&cw_dash_tracking,
                                              cw_receive_dash_length);
        }
    }
}


/**
 * Enable/disable/get adaptive receive speeds.  If adaptive speed tracking
 * is enabled, the receive functions will attempt to automatically adjust
 * the receive speed setting to match the speed of the incoming Morse code.
 * If it is disabled, the receive functions will use fixed speed settings,
 * and reject incoming Morse which is not at the expected speed.  The
 * cw_get_adaptive_receive_state function returns true if adaptive speed
 * tracking is enabled, false otherwise.  Adaptive speed tracking uses a
 * moving average of the past four elements as its baseline for tracking
 * speeds.  The default state is adaptive tracking disabled.
 */
void cw_enable_adaptive_receive(void)
{
  cw_set_adaptive_receive_internal (true);
}





/**
   See documentation of cw_enable_adaptive_receive() for more information
*/
void cw_disable_adaptive_receive(void)
{
  cw_set_adaptive_receive_internal (false);
}





/**
   See documentation of cw_enable_adaptive_receive() for more information
*/
bool cw_get_adaptive_receive_state(void)
{
	return cw_is_adaptive_receive_enabled;
}





/**
 * If an input timestamp is given, validate it for correctness, and if valid,
 * copy it into return_timestamp and return true.  If invalid, return false
 * with errno set to EINVAL.  If an input timestamp is not given (NULL), return
 * true with the current system time in return_timestamp.
 */
int cw_validate_timestamp_internal(const struct timeval *timestamp,
				   struct timeval *return_timestamp)
{
  if (timestamp)
    {
      if (timestamp->tv_sec < 0 || timestamp->tv_usec < 0
          || timestamp->tv_usec >= USECS_PER_SEC)
        {
          errno = EINVAL;
          return CW_FAILURE;
        }
      *return_timestamp = *timestamp;
    }
  else
    {
      if (gettimeofday (return_timestamp, NULL) != 0)
        {
          perror ("cw: gettimeofday");
          return CW_FAILURE;
        }
    }
  return CW_SUCCESS;
}


/*
 * The CW receive functions implement the following state graph:
 *
 *        +----------------- RS_ERR_WORD <-------------------+
 *        |(clear)                ^                          |
 *        |           (delay=long)|                          |
 *        |                       |                          |
 *        +----------------- RS_ERR_CHAR <---------+         |
 *        |(clear)                ^  |             |         |
 *        |                       |  +-------------+         |(error,
 *        |                       |   (delay=short)          | delay=long)
 *        |    (error,delay=short)|                          |
 *        |                       |  +-----------------------+
 *        |                       |  |
 *        +--------------------+  |  |
 *        |             (noise)|  |  |
 *        |                    |  |  |
 *        v    (start tone)    |  |  |  (end tone,noise)
 * --> RS_IDLE ------------> RS_IN_TONE ------------> RS_AFTER_TONE <------- +
 *     |  ^                           ^               | |    | ^ |           |
 *     |  |          (delay=short)    +---------------+ |    | | +-----------+
 *     |  |        +--------------+     (start tone)    |    | |  (not ready,
 *     |  |        |              |                     |    | |   buffer dot,
 *     |  |        +-------> RS_END_CHAR <--------------+    | |   buffer dash)
 *     |  |                   |   |       (delay=short)      | |
 *     |  +-------------------+   |                          | |
 *     |  |(clear)                |                          | |
 *     |  |           (delay=long)|                          | |
 *     |  |                       v                          | |
 *     |  +----------------- RS_END_WORD <-------------------+ |
 *     |   (clear)                        (delay=long)         |(buffer dot,
 *     |                                                       | buffer dash)
 *     +-------------------------------------------------------+
 */
static enum
{ RS_IDLE, RS_IN_TONE, RS_AFTER_TONE, RS_END_CHAR, RS_END_WORD, RS_ERR_CHAR,
  RS_ERR_WORD
}
cw_receive_state = RS_IDLE;


/**
 * Compare two timestamps, and return the difference between them in
 * microseconds, taking care to clamp values which would overflow an int.
 * This routine always returns a positive integer in the range 0 to INT_MAX.
 */
int cw_compare_timestamps_internal(const struct timeval *earlier,
				   const struct timeval *later)
{
  int delta_usec;

  /*
   * Compare the timestamps, taking care on overflows.
   *
   * At 4 WPM, the dash length is 3*(1200000/4)=900,000 usecs, and the word
   * gap is 2,100,000 usecs.  With the maximum Farnsworth additional delay,
   * the word gap extends to 20,100,000 usecs.  This fits into an int with a
   * lot of room to spare, in fact, an int can represent 2,147,483,647 usecs,
   * or around 33 minutes.  This is way, way longer than we'd ever want to
   * differentiate, so if by some chance we see timestamps farther apart than
   * this, and it ought to be very, very unlikely, then we'll clamp the
   * return value to INT_MAX with a clear conscience.
   *
   * Note: passing nonsensical or bogus timevals in may result in unpredict-
   * able results.  Nonsensical includes timevals with -ve tv_usec, -ve
   * tv_sec, tv_usec >= 1,000,000, etc.  To help in this, we check all
   * incoming timestamps for 'well-formedness'.  However, we assume the
   * gettimeofday() call always returns good timevals.  All in all, timeval
   * could probably be a better thought-out structure.
   */

  /* Calculate an initial delta, possibly with overflow. */
  delta_usec = (later->tv_sec - earlier->tv_sec) * USECS_PER_SEC
               + later->tv_usec - earlier->tv_usec;

  /* Check specifically for overflow, and clamp if it did. */
  if ((later->tv_sec - earlier->tv_sec) > (INT_MAX / USECS_PER_SEC) + 1
      || delta_usec < 0)
    delta_usec = INT_MAX;

  return delta_usec;
}


/**
 * Called on the start of a receive tone.  If the timestamp is NULL, the
 * current time is used.  On success, the routine returns true.   On error,
 * it returns false, with errno set to ERANGE if the call is directly after
 * another cw_start_receive_tone call or if an existing received character
 * has not been cleared from the buffer, or EINVAL if the timestamp passed
 * in is invalid.
 */
int cw_start_receive_tone(const struct timeval *timestamp)
{
  /*
   * If the receive state is not idle or after a tone, this is a state error.
   * A receive tone start can only happen while we are idle, or in the middle
   * of a character.
   */
  if (cw_receive_state != RS_IDLE && cw_receive_state != RS_AFTER_TONE)
    {
      errno = ERANGE;
      return CW_FAILURE;
    }

  /* Validate and save the timestamp, or get one and then save it. */
  if (!cw_validate_timestamp_internal (timestamp, &cw_rr_start_timestamp))
    return CW_FAILURE;

  /*
   * If we are in the after tone state, we can measure the inter-element
   * gap by comparing the start timestamp with the last end one, guaranteed
   * set by getting to the after tone state via cw_end_receive tone, or in
   * extreme cases, by cw_receive_buffer_element_internal.
   *
   * Do that, then, and update the relevant statistics.
   */
  if (cw_receive_state == RS_AFTER_TONE)
    {
      int space_usec;

      space_usec = cw_compare_timestamps_internal (&cw_rr_end_timestamp,
                                                   &cw_rr_start_timestamp);
      cw_add_receive_statistic_internal (STAT_END_ELEMENT, space_usec);
    }

  /* Set state to indicate we are inside a tone. */
  cw_receive_state = RS_IN_TONE;

  cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);

  return CW_SUCCESS;
}


/**
 * Analyses a tone using the ranges provided by the low level timing
 * parameters.  On success, it returns true and sends back either a dot or
 * a dash in representation.  On error, it returns false with errno set to
 * ENOENT if the tone is not recognizable as either a dot or a dash,
 * and sets the receive state to one of the error states, depending on
 * the tone length passed in.
 *
 * Note; for adaptive timing, the tone should _always_ be recognized as
 * a dot or a dash, because the ranges will have been set to cover 0 to
 * INT_MAX.
 */
int cw_identify_receive_tone_internal(int element_usec, char *representation)
{
  /* Synchronize low level timings if required */
  cw_sync_parameters_internal ();

  /* If the timing was, within tolerance, a dot, return dot to the caller.  */
  if (element_usec >= cw_dot_range_minimum
      && element_usec <= cw_dot_range_maximum)
    {
      *representation = CW_DOT_REPRESENTATION;
      return CW_SUCCESS;
    }

  /* Do the same for a dash. */
  if (element_usec >= cw_dash_range_minimum
      && element_usec <= cw_dash_range_maximum)
    {
      *representation = CW_DASH_REPRESENTATION;
      return CW_SUCCESS;
    }

  /*
   * This element is not a dot or a dash, so we have an error case.  Depending
   * on the timestamp difference, we pick which of the error states to move
   * to, and move to it.  The comparison is against the expected end-of-char
   * delay.  If it's larger, then fix at word error, otherwise settle on char
   * error.
   *
   * Note that we should never reach here for adaptive timing receive.
   */
  cw_receive_state = element_usec > cw_eoc_range_maximum
                     ? RS_ERR_WORD : RS_ERR_CHAR;

  cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);

  /* Return ENOENT to the caller. */
  errno = ENOENT;
  return CW_FAILURE;
}


/**
 * Updates the averages of dot and dash lengths, and recalculates the
 * adaptive threshold for the next receive tone.
 */
void cw_update_adaptive_tracking_internal(int element_usec, char element)
{
  int average_dot, average_dash;

  /* We are not going to tolerate being called in fixed speed mode. */
  if (!cw_is_adaptive_receive_enabled)
    return;

  /*
   * We will update the information held for either dots or dashes.  Which we
   * pick depends only on what the representation of the character was
   * identified as earlier.
   */
  if (element == CW_DOT_REPRESENTATION)
    cw_update_adaptive_average_internal (&cw_dot_tracking, element_usec);
  else if (element == CW_DASH_REPRESENTATION)
    cw_update_adaptive_average_internal (&cw_dash_tracking, element_usec);

  /*
   * Recalculate the adaptive threshold from the values currently held in the
   * moving averages.  The threshold is calculated as (avg dash length -
   * avg dot length) / 2 + avg dot_length.
   */
  average_dot = cw_get_adaptive_average_internal (&cw_dot_tracking);
  average_dash = cw_get_adaptive_average_internal (&cw_dash_tracking);
  cw_adaptive_receive_threshold = (average_dash - average_dot) / 2
                                  + average_dot;

  /*
   * Resynchronize the low level timing data following recalculation.  If the
   * resultant recalculated speed is outside the limits, clamp the speed to
   * the limit value and recalculate again.
   *
   * Resetting the speed directly really means unsetting adaptive mode,
   * resyncing to calculate the new threshold, which unfortunately recalcu-
   * lates everything else according to fixed speed; so, we then have to reset
   * adaptive and resyncing one more time, to get all other timing parameters
   * back to where they should be.
   */
  cw_is_in_sync = false;
  cw_sync_parameters_internal ();
  if (cw_receive_speed < CW_SPEED_MIN || cw_receive_speed > CW_SPEED_MAX)
    {
      cw_receive_speed = cw_receive_speed < CW_SPEED_MIN
                         ? CW_SPEED_MIN : CW_SPEED_MAX;
      cw_is_adaptive_receive_enabled = false;
      cw_is_in_sync = false;
      cw_sync_parameters_internal ();
      cw_is_adaptive_receive_enabled = true;
      cw_is_in_sync = false;
      cw_sync_parameters_internal ();
    }
}


/**
 * Called on the end of a receive tone.  If the timestamp is NULL, the
 * current time is used.  On success, the routine adds a dot or dash to
 * the receive representation buffer, and returns true.  On error, it
 * returns false, with errno set to ERANGE if the call was not preceded by
 * a cw_start_receive_tone call, EINVAL if the timestamp passed in is not
 * valid, ENOENT if the tone length was out of bounds for the permissible
 * dot and dash lengths and fixed speed receiving is selected, ENOMEM if
 * the representation buffer is full, or EAGAIN if the tone was shorter
 * than the threshold for noise and was therefore ignored.
 */
int cw_end_receive_tone(const struct timeval *timestamp)
{
  int status, element_usec;
  char representation;
  struct timeval saved_end_timestamp;

  /* The receive state is expected to be inside a tone. */
  if (cw_receive_state != RS_IN_TONE)
    {
      errno = ERANGE;
      return CW_FAILURE;
    }

  /*
   * Take a safe copy of the current end timestamp, in case we need to put
   * it back if we decide this tone is really just noise.
   */
  saved_end_timestamp = cw_rr_end_timestamp;

  /* Save the timestamp passed in, or get one. */
  if (!cw_validate_timestamp_internal (timestamp, &cw_rr_end_timestamp))
    return CW_FAILURE;

  /* Compare the timestamps to determine the length of the tone. */
  element_usec = cw_compare_timestamps_internal (&cw_rr_start_timestamp,
                                                 &cw_rr_end_timestamp);

  /*
   * If the tone length is shorter than any noise canceling threshold that
   * has been set, then ignore this tone.  This means reverting to the state
   * before the call to cw_start_receive_tone.  Now, by rights, we should use
   * an extra state, RS_IN_FIRST_TONE, say, so that we know whether to go
   * back to the idle state, or to after tone.  But to make things a touch
   * simpler, here we can just look at the current receive buffer pointer.
   * If it's zero, we came from idle, otherwise we came from after tone.
   */
  if (cw_noise_spike_threshold > 0
      && element_usec <= cw_noise_spike_threshold)
    {
      cw_receive_state = cw_rr_current == 0 ? RS_IDLE : RS_AFTER_TONE;

      /*
       * Put the end tone timestamp back to how it was when we came in to
       * the routine.
       */
      cw_rr_end_timestamp = saved_end_timestamp;

      cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);

      errno = EAGAIN;
      return CW_FAILURE;
    }

  /*
   * At this point, we have to make a decision about the element just
   * received.  We'll use a routine that compares ranges to tell us what it
   * thinks this element is.  If it can't decide, it will hand us back an
   * error which we return to the caller.  Otherwise, it returns a character,
   * dot or dash, for us to buffer.
   */
  status = cw_identify_receive_tone_internal (element_usec, &representation);
  if (!status)
    return CW_FAILURE;

  /*
   * Update the averaging buffers so that the adaptive tracking of received
   * Morse speed stays up to date.  But only do this if we have set adaptive
   * receiving; don't fiddle about trying to track for fixed speed receive.
   */
  if (cw_is_adaptive_receive_enabled)
    cw_update_adaptive_tracking_internal (element_usec, representation);

  /*
   * Update dot and dash timing statistics.  It may seem odd to do this after
   * calling cw_update_adaptive_tracking_internal, rather than before, as
   * this function changes the ideal values we're measuring against.  But if
   * we're on a speed change slope, the adaptive tracking smoothing will
   * cause the ideals to lag the observed speeds.  So by doing this here, we
   * can at least ameliorate this effect, if not eliminate it.
   */
  if (representation == CW_DOT_REPRESENTATION)
    cw_add_receive_statistic_internal (STAT_DOT, element_usec);
  else
    cw_add_receive_statistic_internal (STAT_DASH, element_usec);

  /* Add the representation character to the receive buffer. */
  cw_receive_representation_buffer[cw_rr_current++] = representation;

  /*
   * We just added a representation to the receive buffer.  If it's full,
   * then we have to do something, even though it's unlikely.  What we'll do
   * is make a unilateral declaration that if we get this far, we go to
   * end-of-char error state automatically.
   */
  if (cw_rr_current == RECEIVE_CAPACITY - 1)
    {
      cw_receive_state = RS_ERR_CHAR;

      cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);

      errno = ENOMEM;
      return CW_FAILURE;
    }

  /* All is well.  Move to the more normal after-tone state. */
  cw_receive_state = RS_AFTER_TONE;

  cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);

  return CW_SUCCESS;
}


/**
 * Adds either a dot or a dash to the receive representation buffer.  If
 * the timestamp is NULL, the current timestamp is used.  The receive state
 * is updated as if we had just received a call to cw_end_receive_tone.
 */
int cw_receive_buffer_element_internal(const struct timeval *timestamp,
				       char element)
{
  /*
   * The receive state is expected to be idle or after a tone in order to
   * use this routine.
   */
  if (cw_receive_state != RS_IDLE && cw_receive_state != RS_AFTER_TONE)
    {
      errno = ERANGE;
      return CW_FAILURE;
    }

  /*
   * This routine functions as if we have just seen a tone end, yet without
   * really seeing a tone start.  To keep timing information for routines
   * that come later, we need to make sure that the end of tone timestamp is
   * set here.  This is because the receive representation routine looks at
   * the time since the last end of tone to determine whether we are at the
   * end of a word, or just at the end of a character.  It doesn't matter that
   * the start of tone timestamp is never set - this is just for timing the
   * tone length, and we don't need to do that since we've already been told
   * whether this is a dot or a dash.
   */
  if (!cw_validate_timestamp_internal (timestamp, &cw_rr_end_timestamp))
    return CW_FAILURE;

  /* Add the element to the receive representation buffer. */
  cw_receive_representation_buffer[cw_rr_current++] = element;

  /*
   * We just added an element to the receive buffer.  As above, if it's full,
   * then we have to do something, even though it's unlikely to actually be
   * full.
   */
  if (cw_rr_current == RECEIVE_CAPACITY - 1)
    {
      cw_receive_state = RS_ERR_CHAR;

      cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);

      errno = ENOMEM;
      return CW_FAILURE;
    }

  /*
   * Since we effectively just saw the end of a tone, move to the after-tone
   * state.
   */
  cw_receive_state = RS_AFTER_TONE;

  cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);

  return CW_SUCCESS;
}


/**
   \brief Add a dot to the receive representation buffer.

 * If the timestamp is NULL, the current timestamp is used.  These routines
 * are for callers that have already determined whether a dot or dash was
 * received by a method other than calling the routines cw_start_receive_tone
 * and cw_end_receive_tone.  On success, the relevant element is added to
 * the receive representation buffer.  On error, the routines return false,
 * with errno set to ERANGE if preceded by a cw_start_receive_tone call
 * with no matching cw_end_receive_tone or if an error condition currently
 * exists within the receive buffer, or ENOMEM if the receive representation
 * buffer is full.
 */
int cw_receive_buffer_dot(const struct timeval *timestamp)
{
  return cw_receive_buffer_element_internal (timestamp, CW_DOT_REPRESENTATION);
}





/**
   \brief Add a dash to the receive representation buffer.

   See documentation of cw_receive_buffer_dot() for more information
*/
int cw_receive_buffer_dash(const struct timeval *timestamp)
{
  return cw_receive_buffer_element_internal (timestamp, CW_DASH_REPRESENTATION);
}


/**
 * Returns the current buffered representation from the receive buffer.
 * On success, the function returns true, and fills in representation with the
 * contents of the current representation buffer.  On error, it returns false,
 * with errno set to ERANGE if not preceded by a cw_end_receive_tone call,
 * a prior successful cw_receive_representation call, or a prior
 * cw_receive_buffer_dot or cw_receive_buffer_dash, EINVAL if the timestamp
 * passed in is invalid, or EAGAIN if the call is made too early to determine
 * whether a complete representation has yet been placed in the buffer
 * (that is, less than the inter-character gap period elapsed since the last
 * cw_end_receive_tone or cw_receive_buffer_dot/dash call).  is_end_of_word
 * indicates that the delay after the last tone received is longer that the
 * inter-word gap, and is_error indicates that the representation was
 * terminated by an error condition.
 */
int cw_receive_representation(const struct timeval *timestamp,
			      char *representation, bool *is_end_of_word,
			      bool *is_error)
{
  int space_usec;
  struct timeval now_timestamp;

  /*
   * If the the receive state indicates that we have in our possession a
   * completed representation at the end of word, just [re-]return it.
   */
  if (cw_receive_state == RS_END_WORD || cw_receive_state == RS_ERR_WORD)
    {
      if (is_end_of_word)
        *is_end_of_word = true;
      if (is_error)
        *is_error = (cw_receive_state == RS_ERR_WORD);
      *representation = '\0';
      strncat (representation, cw_receive_representation_buffer, cw_rr_current);
      return CW_SUCCESS;
    }

  /*
   * If the receive state is also not end-of-char, and also not after a tone,
   * then we are idle or in a tone; in these cases, we return ERANGE.
   */
  if (cw_receive_state != RS_AFTER_TONE
      && cw_receive_state != RS_END_CHAR && cw_receive_state != RS_ERR_CHAR)
    {
      errno = ERANGE;
      return CW_FAILURE;
    }

  /*
   * We now know the state is after a tone, or end-of-char, perhaps with
   * error.  For all three of these cases, we're going to [re-]compare the
   * timestamp with the end of tone timestamp.  This could mean that in the
   * case of end-of-char, we revise our opinion on later calls to end-of-word.
   * This is correct, since it models reality.
   */

  /*
   * If we weren't supplied with one, get the current timestamp for comparison
   * against the latest end timestamp.
   */
  if (!cw_validate_timestamp_internal (timestamp, &now_timestamp))
    return CW_FAILURE;

  /*
   * Now we need to compare the timestamps to determine the length of the
   * inter-tone gap.
   */
  space_usec = cw_compare_timestamps_internal (&cw_rr_end_timestamp,
                                               &now_timestamp);

  /* Synchronize low level timings if required */
  cw_sync_parameters_internal ();

  /*
   * If the timing was, within tolerance, a character space, then that is
   * what we'll call it.  In this case, we complete the representation and
   * return it.
   */
  if (space_usec >= cw_eoc_range_minimum
      && space_usec <= cw_eoc_range_maximum)
    {
      /*
       * If state is after tone, we can validly move at this point to end of
       * char.  If it's not, then we're at end char or at end char with error
       * already, so leave it.  On moving, update timing statistics for an
       * identified end of character.
       */
      if (cw_receive_state == RS_AFTER_TONE)
        {
          cw_add_receive_statistic_internal (STAT_END_CHARACTER, space_usec);
          cw_receive_state = RS_END_CHAR;
        }

      cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);

      /* Return the representation buffered. */
      if (is_end_of_word)
        *is_end_of_word = false;
      if (is_error)
        *is_error = (cw_receive_state == RS_ERR_CHAR);
      *representation = '\0';
      strncat (representation, cw_receive_representation_buffer, cw_rr_current);
      return CW_SUCCESS;
    }

  /*
   * If the timing indicated a word space, again we complete the representation
   * and return it.  In this case, we also need to inform the client that this
   * looked like the end of a word, not just a character.  And, we don't care
   * about the maximum period, only that it exceeds the low end of the range.
   */
  if (space_usec > cw_eoc_range_maximum)
    {
      /*
       * In this case, we have a transition to an end of word case.  If we
       * were sat in an error case, we need to move to the correct end of word
       * state, otherwise, at after tone, we go safely to the non-error end
       * of word.
       */
      cw_receive_state = cw_receive_state == RS_ERR_CHAR
                         ? RS_ERR_WORD : RS_END_WORD;

      cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);

      /* Return the representation buffered. */
      if (is_end_of_word)
        *is_end_of_word = true;
      if (is_error)
        *is_error = (cw_receive_state == RS_ERR_WORD);
      *representation = '\0';
      strncat (representation, cw_receive_representation_buffer, cw_rr_current);
      return CW_SUCCESS;
    }

  /*
   * If none of these conditions holds, then we cannot yet make a judgement
   * on what we have in the buffer, so return EAGAIN.
   */
  errno = EAGAIN;
  return CW_FAILURE;
}


/**
 * Returns the current buffered character from the representation buffer.
 * On success, the function returns true, and fills char *c with the contents
 * of the current representation buffer, translated into a character.  On
 * error, it returns false, with errno set to ERANGE if not preceded by a
 * cw_end_receive_tone call, a prior successful cw_receive_character
 * call, or a cw_receive_buffer_dot or cw_receive_buffer dash call, EINVAL
 * if the timestamp passed in is invalid, or EAGAIN if the call is made too
 * early to determine whether a complete character has yet been placed in the
 * buffer (that is, less than the inter-character gap period elapsed since
 * the last cw_end_receive_tone or cw_receive_buffer_dot/dash call).
 * is_end_of_word indicates that the delay after the last tone received is
 * longer that the inter-word gap, and is_error indicates that the character
 * was terminated by an error condition.
 */
int cw_receive_character(const struct timeval *timestamp,
			 char *c, bool *is_end_of_word, bool *is_error)
{
  int status;
  bool end_of_word, error;
  char character, representation[RECEIVE_CAPACITY + 1];

  /* See if we can obtain a representation from the receive routines. */
  status = cw_receive_representation (timestamp, representation,
                                      &end_of_word, &error);
  if (!status)
    return CW_FAILURE;

  /* Look up the representation using the lookup functions. */
  character = cw_lookup_representation_internal (representation);
  if (!character)
    {
      errno = ENOENT;
      return CW_FAILURE;
    }

  /* If we got this far, all is well, so return what we uncovered. */
  if (c)
    *c = character;
  if (is_end_of_word)
    *is_end_of_word = end_of_word;
  if (is_error)
    *is_error = error;
  return CW_SUCCESS;
}


/**
 * Clears the receive representation buffer to receive tones again.  This
 * routine must be called after successful, or terminating,
 * cw_receive_representation or cw_receive_character calls, to clear the
 * states and prepare the buffer to receive more tones.
 */
void cw_clear_receive_buffer(void)
{
  cw_rr_current = 0;
  cw_receive_state = RS_IDLE;

  cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d", cw_receive_state);
}


/**
 * Returns the number of entries the receive buffer can accommodate.  The
 * maximum number of character written out by cw_receive_representation is
 * the capacity + 1, the extra character being used for the terminating
 * NUL.
 */
int cw_get_receive_buffer_capacity(void)
{
  return RECEIVE_CAPACITY;
}


/**
 * Returns the number of elements currently pending in the receive buffer.
 */
int cw_get_receive_buffer_length(void)
{
  return cw_rr_current;
}


/**
 * Clear the receive representation buffer, statistics, and any retained
 * receive state.  This function is suitable for calling from an application
 * exit handler.
 */
void cw_reset_receive(void)
{
  cw_rr_current = 0;
  cw_receive_state = RS_IDLE;

  cw_reset_receive_statistics ();

  cw_debug (CW_DEBUG_RECEIVE_STATES, "receive state ->%d (reset)", cw_receive_state);
}


/*---------------------------------------------------------------------*/
/*  Iambic keyer                                                       */
/*---------------------------------------------------------------------*/

/*
 * Iambic keyer status.  The keyer functions maintain the current known state
 * of the paddles, and latch false-to-true transitions while busy, to form the
 * iambic effect.  For Curtis mode B, the keyer also latches any point where
 * both paddle states are true at the same time.
 */
static volatile bool cw_ik_dot_paddle = false,      /* Dot paddle state */
	cw_ik_dash_paddle = false,     /* Dash paddle state */
	cw_ik_dot_latch = false,       /* Dot false->true latch */
	cw_ik_dash_latch = false,      /* Dash false->true latch */
	cw_ik_curtis_b_latch = false;  /* Curtis Dot&&Dash latch */

/*
 * Iambic keyer "Curtis" mode A/B selector.  Mode A and mode B timings differ
 * slightly, and some people have a preference for one or the other.  Mode A
 * is a bit less timing-critical, so we'll make that the default.
 */
static volatile bool cw_ik_curtis_mode_b = false;

/**
 * Normally, the iambic keying functions will emulate Curtis 8044 Keyer
 * mode A.  In this mode, when both paddles are pressed together, the last
 * dot or dash being sent on release is completed, and nothing else is sent.
 * In mode B, when both paddles are pressed together, the last dot or dash
 * being sent on release is completed, then an opposite element is also sent.
 * Some operators prefer mode B, but timing is more critical in this mode.
 * The default mode is Curtis mode A.
 */
void cw_enable_iambic_curtis_mode_b(void)
{
  cw_ik_curtis_mode_b = true;
}





/**
   See documentation of cw_enable_iambic_curtis_mode_b() for more information
*/
void cw_disable_iambic_curtis_mode_b(void)
{
  cw_ik_curtis_mode_b = false;
}





/**
   See documentation of cw_enable_iambic_curtis_mode_b() for more information
*/
int cw_get_iambic_curtis_mode_b_state(void)
{
  return cw_ik_curtis_mode_b;
}





/*
 * The CW keyer functions implement the following state graph:
 *
 *        +-----------------------------------------------------+
 *        |          (all latches clear)                        |
 *        |                                     (dot latch)     |
 *        |                          +--------------------------+
 *        |                          |                          |
 *        |                          v                          |
 *        |      +-------------> KS_IN_DOT_[A|B] -------> KS_AFTER_DOT_[A|B]
 *        |      |(dot paddle)       ^            (delay)       |
 *        |      |                   |                          |(dash latch/
 *        |      |                   +------------+             | _B)
 *        v      |                                |             |
 * --> KS_IDLE --+                   +--------------------------+
 *        ^      |                   |            |
 *        |      |                   |            +-------------+(dot latch/
 *        |      |                   |                          | _B)
 *        |      |(dash paddle)      v            (delay)       |
 *        |      +-------------> KS_IN_DASH_[A|B] -------> KS_AFTER_DASH_[A|B]
 *        |                          ^                          |
 *        |                          |                          |
 *        |                          +--------------------------+
 *        |                                     (dash latch)    |
 *        |          (all latches clear)                        |
 *        +-----------------------------------------------------+
 */
static volatile enum
{ KS_IDLE, KS_IN_DOT_A, KS_IN_DASH_A, KS_AFTER_DOT_A, KS_AFTER_DASH_A,
  KS_IN_DOT_B, KS_IN_DASH_B, KS_AFTER_DOT_B, KS_AFTER_DASH_B
}
cw_keyer_state = KS_IDLE;


/**
 * Informs the internal keyer states that the itimer expired, and we received
 * SIGALRM.
 */
void cw_keyer_clock_internal(void)
{
  /* Synchronize low level timing parameters if required. */
  cw_sync_parameters_internal ();

  /* Decide what to do based on the current state. */
  switch (cw_keyer_state)
    {
    /* Ignore calls if our state is idle. */
    case KS_IDLE:
      return;

    /*
     * If we were in a dot, turn off tones and begin the after-dot delay.  Do
     * much the same if we are in a dash.  No routine status checks are made
     * since we are in a signal handler, and can't readily return error codes
     * to the client.
     */
    case KS_IN_DOT_A:
    case KS_IN_DOT_B:
      cw_sound_internal (CW_TONE_SILENT);
      cw_key_control_internal (false);
      cw_request_timeout_internal (cw_end_of_ele_delay, NULL);
      cw_keyer_state = cw_keyer_state == KS_IN_DOT_A
                       ? KS_AFTER_DOT_A : KS_AFTER_DOT_B;

      cw_debug (CW_DEBUG_KEYER_STATES, "keyer ->%d", cw_keyer_state);
      break;

    case KS_IN_DASH_A:
    case KS_IN_DASH_B:
      cw_sound_internal (CW_TONE_SILENT);
      cw_key_control_internal (false);
      cw_request_timeout_internal (cw_end_of_ele_delay, NULL);
      cw_keyer_state = cw_keyer_state == KS_IN_DASH_A
                       ? KS_AFTER_DASH_A : KS_AFTER_DASH_B;

      cw_debug (CW_DEBUG_KEYER_STATES, "keyer ->%d", cw_keyer_state);
      break;

    /*
     * If we have just finished a dot or a dash and its post-element delay,
     * then reset the latches as appropriate.  Next, if in a _B state, go
     * straight to the opposite element state.  If in an _A state, check the
     * latch states; if the opposite latch is set true, then do the iambic
     * thing and alternate dots and dashes.  If the same latch is true,
     * repeat.  And if nothing is true, then revert to idling.
     */
    case KS_AFTER_DOT_A:
    case KS_AFTER_DOT_B:
      if (!cw_ik_dot_paddle)
        cw_ik_dot_latch = false;
      if (cw_keyer_state == KS_AFTER_DOT_B)
        {
          cw_sound_internal (generator->frequency);
          cw_key_control_internal (true);
          cw_request_timeout_internal (cw_send_dash_length, NULL);
          cw_keyer_state = KS_IN_DASH_A;
        }
      else if (cw_ik_dash_latch)
        {
          cw_sound_internal (generator->frequency);
          cw_key_control_internal (true);
          cw_request_timeout_internal (cw_send_dash_length, NULL);
          if (cw_ik_curtis_b_latch)
            {
              cw_ik_curtis_b_latch = false;
              cw_keyer_state = KS_IN_DASH_B;
            }
          else
            cw_keyer_state = KS_IN_DASH_A;
        }
      else if (cw_ik_dot_latch)
        {
          cw_sound_internal (generator->frequency);
          cw_key_control_internal (true);
          cw_request_timeout_internal (cw_send_dot_length, NULL);
          cw_keyer_state = KS_IN_DOT_A;
        }
      else
        {
          cw_keyer_state = KS_IDLE;
          cw_schedule_finalization_internal ();
        }

      cw_debug (CW_DEBUG_KEYER_STATES, "keyer ->%d", cw_keyer_state);
      break;

    case KS_AFTER_DASH_A:
    case KS_AFTER_DASH_B:
      if (!cw_ik_dash_paddle)
        cw_ik_dash_latch = false;
      if (cw_keyer_state == KS_AFTER_DASH_B)
        {
          cw_sound_internal (generator->frequency);
          cw_key_control_internal (true);
          cw_request_timeout_internal (cw_send_dot_length, NULL);
          cw_keyer_state = KS_IN_DOT_A;
        }
      else if (cw_ik_dot_latch)
        {
          cw_sound_internal (generator->frequency);
          cw_key_control_internal (true);
          cw_request_timeout_internal (cw_send_dot_length, NULL);
          if (cw_ik_curtis_b_latch)
            {
              cw_ik_curtis_b_latch = false;
              cw_keyer_state = KS_IN_DOT_B;
            }
          else
            cw_keyer_state = KS_IN_DOT_A;
        }
      else if (cw_ik_dash_latch)
        {
          cw_sound_internal (generator->frequency);
          cw_key_control_internal (true);
          cw_request_timeout_internal (cw_send_dash_length, NULL);
          cw_keyer_state = KS_IN_DASH_A;
        }
      else
        {
          cw_keyer_state = KS_IDLE;
          cw_schedule_finalization_internal ();
        }

      cw_debug (CW_DEBUG_KEYER_STATES, "keyer ->%d", cw_keyer_state);
      break;
    }
}


/**
 * Informs the internal keyer states that the keyer paddles have changed
 * state.  The new paddle states are recorded, and if either transition from
 * false to true, paddle latches, for iambic functions, are also set.
 * On success, the routine returns true.  On error, it returns false, with
 * errno set to EBUSY if the tone queue or straight key are using the sound
 * card, console speaker, or keying system.
 *
 * If appropriate, this routine starts the keyer functions sending the
 * relevant element.  Element send and timing occurs in the background, so
 * this routine returns almost immediately.  See cw_keyer_element_wait and
 * cw_keyer_wait for details about how to check the current status of
 * iambic keyer background processing.
 */
int cw_notify_keyer_paddle_event(int dot_paddle_state,
				 int dash_paddle_state)
{
  /*
   * If the tone queue or the straight key are busy, this is going to conflict
   * with our use of the sound card, console sounder, and keying system.  So
   * return an error status in this case.
   */
  if (cw_is_straight_key_busy () || cw_is_tone_busy ())
    {
      errno = EBUSY;
      return CW_FAILURE;
    }

  /* Clean up and save the paddle states passed in. */
  cw_ik_dot_paddle = (dot_paddle_state != 0);
  cw_ik_dash_paddle = (dash_paddle_state != 0);

  /*
   * Update the paddle latches if either paddle goes true.  The latches are
   * checked in the signal handler, so if the paddles go back to false during
   * this element, the item still gets actioned.  The signal handler is also
   * responsible for clearing down the latches.
   */
  if (cw_ik_dot_paddle)
    cw_ik_dot_latch = true;
  if (cw_ik_dash_paddle)
    cw_ik_dash_latch = true;

  /*
   * If in Curtis mode B, make a special check for both paddles true at the
   * same time.  This flag is checked by the signal handler, to determine
   * whether to add mode B trailing timing elements.
   */
  if (cw_ik_curtis_mode_b && cw_ik_dot_paddle && cw_ik_dash_paddle)
    cw_ik_curtis_b_latch = true;

  cw_debug (CW_DEBUG_KEYER_STATES, "keyer paddles %d,%d, latches %d,%d, curtis_b %d",
	    cw_ik_dot_paddle, cw_ik_dash_paddle,
	    cw_ik_dot_latch, cw_ik_dash_latch, cw_ik_curtis_b_latch);

  /* If the current state is idle, give the state process a nudge. */
  if (cw_keyer_state == KS_IDLE)
    {
      if (cw_ik_dot_paddle)
        {
          /* Pretend we just finished a dash. */
          cw_keyer_state = cw_ik_curtis_b_latch
                           ? KS_AFTER_DASH_B : KS_AFTER_DASH_A;
          cw_request_timeout_internal (0, cw_keyer_clock_internal);
        }
      else if (cw_ik_dash_paddle)
        {
          /* Pretend we just finished a dot. */
          cw_keyer_state = cw_ik_curtis_b_latch
                           ? KS_AFTER_DOT_B : KS_AFTER_DOT_A;
          cw_request_timeout_internal (0, cw_keyer_clock_internal);
        }
    }

  cw_debug (CW_DEBUG_KEYER_STATES, "keyer ->%d", cw_keyer_state);

  return CW_SUCCESS;
}


/**
 * Convenience functions to alter the state of just one of the two iambic
 * keyer paddles.  The other paddle state of the paddle pair remains unchanged.
 *
 * See cw_keyer_paddle_event for details of iambic keyer background processing,
 * and how to check its status.
 */
int cw_notify_keyer_dot_paddle_event(int dot_paddle_state)
{
  return cw_notify_keyer_paddle_event (dot_paddle_state, cw_ik_dash_paddle);
}


/**
   See documentation of cw_notify_keyer_dot_paddle_event() for more information
*/
int cw_notify_keyer_dash_paddle_event(int dash_paddle_state)
{
  return cw_notify_keyer_paddle_event (cw_ik_dot_paddle, dash_paddle_state);
}


/**
 * Returns the current saved states of the two paddles.
 */
void cw_get_keyer_paddles(int *dot_paddle_state, int *dash_paddle_state)
{
  if (dot_paddle_state)
    *dot_paddle_state = cw_ik_dot_paddle;
  if (dash_paddle_state)
    *dash_paddle_state = cw_ik_dash_paddle;
}


/**
 * Returns the current saved states of the two paddle latches.  A paddle
 * latches is set to true when the paddle state becomes true, and is
 * cleared if the paddle state is false when the element finishes sending.
 */
void cw_get_keyer_paddle_latches(int *dot_paddle_latch_state,
				 int *dash_paddle_latch_state)
{
  if (dot_paddle_latch_state)
    *dot_paddle_latch_state = cw_ik_dot_latch;
  if (dash_paddle_latch_state)
    *dash_paddle_latch_state = cw_ik_dash_latch;
}





/**
   \brief Check if a keyer is busy

   \return true if keyer is busy
   \return false if keyer is not busy
*/
bool cw_is_keyer_busy(void)
{
	return cw_keyer_state != KS_IDLE;
}





/**
 * Waits until the end of the current element, dot or dash, from the keyer.
 * This routine returns true on success.  On error, it returns false, with
 * errno set to EDEADLK if SIGALRM is blocked.
 */
int cw_wait_for_keyer_element(void)
{
  int status;

  /* Check that SIGALRM is not blocked. */
  status = cw_check_signal_mask_internal ();
  if (!status)
    return CW_FAILURE;

  /*
   * First wait for the state to move to idle (or just do nothing if it's
   * not), or to one of the after- states.
   */
  while (cw_keyer_state != KS_IDLE
         && cw_keyer_state != KS_AFTER_DOT_A
         && cw_keyer_state != KS_AFTER_DOT_B
         && cw_keyer_state != KS_AFTER_DASH_A
         && cw_keyer_state != KS_AFTER_DASH_B)
    cw_wait_for_signal_internal ();

  /*
   * Now wait for the state to move to idle (unless it is, or was, already),
   * or one of the in- states, at which point we know we're actually at the
   * end of the element we were in when we entered this routine.
   */
  while (cw_keyer_state != KS_IDLE
         && cw_keyer_state != KS_IN_DOT_A
         && cw_keyer_state != KS_IN_DOT_B
         && cw_keyer_state != KS_IN_DASH_A
         && cw_keyer_state != KS_IN_DASH_B)
    cw_wait_for_signal_internal ();

  return CW_SUCCESS;
}


/**
 * Waits for the current keyer cycle to complete.  The routine returns true on
 * success.  On error, it returns false, with errno set to EDEADLK if SIGALRM
 * is blocked or if either paddle state is true.
 */
int cw_wait_for_keyer(void)
{
  int status;

  /* Check that SIGALRM is not blocked. */
  status = cw_check_signal_mask_internal ();
  if (!status)
    return CW_FAILURE;

  /*
   * Check that neither paddle is true; if either is, then the signal cycle
   * is going to continue forever, and we'll never return from this routine.
   */
  if (cw_ik_dot_paddle || cw_ik_dash_paddle)
    {
      errno = EDEADLK;
      return CW_FAILURE;
    }

  /* Wait for the keyer state to go idle. */
  while (cw_keyer_state != KS_IDLE)
    cw_wait_for_signal_internal ();

  return CW_SUCCESS;
}


/**
 * Clear all keyer latches and paddle states, return to Curtis 8044 Keyer
 * mode A, and return to silence.  This function is suitable for calling from
 * an application exit handler.
 */
void cw_reset_keyer(void)
{
  cw_ik_dot_paddle = false;
  cw_ik_dash_paddle = false;
  cw_ik_dot_latch = false;
  cw_ik_dash_latch = false;
  cw_ik_curtis_b_latch = false;
  cw_ik_curtis_mode_b = false;

  cw_keyer_state = KS_IDLE;

  /* Silence sound and stop any background soundcard tone generation. */
  cw_sound_internal (CW_TONE_SILENT);
  cw_schedule_finalization_internal ();

  cw_debug (CW_DEBUG_KEYER_STATES, "keyer ->%d (reset)", cw_keyer_state);
}


/*---------------------------------------------------------------------*/
/*  Straight key                                                       */
/*---------------------------------------------------------------------*/

/*
 * Period of constant tone generation after which we need another timeout,
 * to ensure that the soundcard doesn't run out of data.
 */
static const int STRAIGHT_KEY_TIMEOUT = 500000;

/* Straight key status; just a key-up or key-down indication. */
static volatile bool cw_sk_key_down = false;


/**
 * Soundcard tone data is only buffered to last about a second on each
 * cw_generate_sound_internal() call, and holding down the straight key for
 * longer than this could cause a soundcard data underrun.  To guard against
 * this, a timeout is generated every half-second or so while the straight
 * key is down.  The timeout generates a chunk of sound data for the soundcard.
 */
void cw_straight_key_clock_internal(void)
{
  if (cw_sk_key_down)
    {
      /* Generate a quantum of tone data, and request another timeout. */
      /* cw_generate_sound_internal (); */
      cw_request_timeout_internal (STRAIGHT_KEY_TIMEOUT, NULL);
    }
}


/**
 * Informs the library that the straight key has changed state.  This routine
 * returns true on success.  On error, it returns false, with errno set to
 * EBUSY if the tone queue or iambic keyer are using the sound card, console
 * speaker, or keying control system.  If key_state indicates no change of
 * state, the call is ignored.
 */
int cw_notify_straight_key_event(int key_state)
{
  /*
   * If the tone queue or the keyer are busy, we can't use the sound card,
   * console sounder, or the key control system.
   */
  if (cw_is_tone_busy () || cw_is_keyer_busy ())
    {
      errno = EBUSY;
      return CW_FAILURE;
    }

  /* If the key state did not change, ignore the call. */
  if ((cw_sk_key_down && !key_state) || (!cw_sk_key_down && key_state))
    {
      /* Save the new key state. */
      cw_sk_key_down = (key_state != 0);

      cw_debug (CW_DEBUG_STRAIGHT_KEY, "straight key state ->%s", cw_sk_key_down ? "DOWN" : "UP");

      /*
       * Do tones and keying, and set up timeouts and soundcard activities to
       * match the new key state.
       */
      if (cw_sk_key_down)
        {
          cw_sound_internal (generator->frequency);
          cw_key_control_internal (true);

          /* Start timeouts to keep soundcard tones running. */
          cw_request_timeout_internal (STRAIGHT_KEY_TIMEOUT,
                                       cw_straight_key_clock_internal);
        }
      else
        {
          cw_sound_internal (CW_TONE_SILENT);
          cw_key_control_internal (false);

          /*
           * Indicate that we have finished with timeouts, and also with the
           * soundcard too.  There's no way of knowing when straight keying
           * is completed, so the only thing we can do here is to schedule
           * release on each key up event.
           */
          cw_schedule_finalization_internal ();
        }
    }

  return CW_SUCCESS;
}


/**
 * Returns the current saved state of the straight key; true if the key is
 * down, false if up.
 */
int cw_get_straight_key_state(void)
{
  return cw_sk_key_down;
}


/**
 * Returns true if the straight key is busy, false if not.  This routine is
 * just a pseudonym for cw_get_straight_key_state, and exists to fill a hole
 * in the API naming conventions.
 */
bool cw_is_straight_key_busy(void)
{
	return cw_sk_key_down;
}


/**
 * Clears the straight key state, and returns to silence.  This function is
 * suitable for calling from an application exit handler.
 */
void cw_reset_straight_key(void)
{
  cw_sk_key_down = false;

  /* Silence sound and stop any background soundcard tone generation. */
  cw_sound_internal (CW_TONE_SILENT);
  cw_schedule_finalization_internal ();

  cw_debug (CW_DEBUG_STRAIGHT_KEY, "straight key state ->UP (reset)");
}





/* ************************ */
/*    Generator - generic   */
/* ************************ */





static const char *cw_audio_system_labels[] = {
	"None",
	"Console",
	"OSS",
	"ALSA",
	"Soundcard" };





/**
   \brief Get a readable label of current audio system

   The function returns one of following strings:
   None, Console, OSS, ALSA, Soundcard

   \return audio system's label
*/
const char *cw_generator_get_audio_system_label(void)
{
	return cw_audio_system_labels[generator->audio_system];
}





/**
   \brief Create new generator

   Allocate memory for new generator data structure, set up default values
   of some of the generator's properties.
   The function does not start the generator (generator does not produce
   a sound), you have to use cw_generator_start() for this.

   \param audio_system - audio system to be used by the generator (console, OSS, ALSA, soundcard, see 'enum cw_audio_systems')
   \param device - name of audio device to be used; if NULL then library will use default device.
*/
int cw_generator_new(int audio_system, const char *device)
{
	generator = (cw_gen_t *) malloc(sizeof (cw_gen_t));
	if (!generator) {
		cw_debug (CW_DEBUG_SYSTEM, "error: malloc");
		return CW_FAILURE;
	}

	generator->audio_device = NULL;
	generator->audio_system = audio_system;
	generator->audio_device_open = 0;
	generator->debug_sink = -1;
	generator->frequency = CW_FREQUENCY_INITIAL;
	generator->volume = CW_VOLUME_INITIAL;
	generator->buffer = NULL;
	generator->buffer_n_samples = -1;


	pthread_attr_init(&(generator->thread_attr));
	pthread_attr_setdetachstate(&(generator->thread_attr), PTHREAD_CREATE_DETACHED);
	generator->thread_error = 0;

	cw_set_audio_device(device);

	int rv = CW_FAILURE;
	if (audio_system == CW_AUDIO_CONSOLE) {
		rv = cw_open_device_console(generator->audio_device);
	} else if (audio_system == CW_AUDIO_OSS) {
		rv = cw_open_device_oss(generator->audio_device);
	} else if (audio_system == CW_AUDIO_ALSA) {
		rv = cw_open_device_alsa(generator->audio_device);
	} else {
		cw_dev_debug ("unsupported audio system");
		rv = CW_FAILURE;
	}

	if (rv == CW_SUCCESS) {
		if (audio_system == CW_AUDIO_CONSOLE) {
			/* console output does not require audio buffer */
			return CW_SUCCESS;
		}

		generator->buffer = (cw_sample_t *) malloc(generator->buffer_n_samples * sizeof (cw_sample_t));
		if (generator->buffer != NULL) {
			return CW_SUCCESS;
		} else {
			cw_debug (CW_DEBUG_SYSTEM, "error: malloc");
		}
	}

	return CW_FAILURE;
}





/**
   \brief Deallocate generator

   Deallocate/destroy generator data structure created with call
   to cw_generator_new(). You can't start nor use the generator
   after the call to this function.
*/
void cw_generator_delete(void)
{
	if (generator) {
		/* Wait for "write" thread to end accessing output
		   file descriptor. I have come up with value 500
		   after doing some experiments. */
		usleep(500);

		if (generator->audio_device) {
			free(generator->audio_device);
			generator->audio_device = NULL;
		}
		if (generator->buffer) {
			free(generator->buffer);
			generator->buffer = NULL;
		}

		if (generator->audio_system == CW_AUDIO_CONSOLE) {
			cw_close_device_console();
		} else if (generator->audio_system == CW_AUDIO_OSS) {
			cw_close_device_oss();
		} else if (generator->audio_system == CW_AUDIO_ALSA) {
			cw_close_device_alsa();
		} else {
			cw_dev_debug ("missed audio system %d", generator->audio_system);
		}

		pthread_attr_destroy(&(generator->thread_attr));

		generator->audio_system = CW_AUDIO_NONE;
		free(generator);
		generator = NULL;
	}
	return;
}





/**
   \brief Start a generator

   Start producing sound using generator created with
   cw_generator_new().

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_generator_start(void)
{
	generator->phase_offset = 0.0;
	generator->phase = 0.0;

	/* both values being zero here means that generator
	   has started working, but its output is silence;
	   set .slope to a positive value to start generating
	   a sound with non-zero amplitude */
	generator->slope = 0;
	generator->amplitude = 0;

	generator->generate = 1;

	if (generator->audio_system == CW_AUDIO_CONSOLE) {
		; /* no thread needed for generating sound on console */
	} else if (generator->audio_system == CW_AUDIO_OSS) {
		int rv = pthread_create(&(generator->thread), &(generator->thread_attr),
					cw_generator_write_sine_wave_oss,
					(void *) generator);
		if (rv != 0) {
			cw_debug (CW_DEBUG_SYSTEM, "error: failed to create OSS generator thread\n");
			return CW_FAILURE;
		} else {
			/* for some yet unknown reason you have to
			   put usleep() here, otherwise a generator
			   may work incorrectly */
			usleep(100000);
			return CW_SUCCESS;
		}
	} else if (generator->audio_system == CW_AUDIO_ALSA) {
		int rv = pthread_create(&(generator->thread), &(generator->thread_attr),
					cw_generator_write_sine_wave_alsa,
					(void *) generator);
		if (rv != 0) {
			cw_debug (CW_DEBUG_SYSTEM, "error: failed to create ALSA generator thread\n");
			return CW_FAILURE;
		} else {
			/* for some yet unknown reason you have to
			   put usleep() here, otherwise a generator
			   may work incorrectly */
			usleep(100000);
			return CW_SUCCESS;
		}
	} else {
		cw_dev_debug ("unsupported audio system %d", generator->audio_system);
	}

	return CW_SUCCESS;
}





/**
   \brief Shut down a generator

   Silence tone generated by generator (level of generated sine wave is
   set to zero, with falling slope), and shut the generator down. If you
   want to use the generator again, you have to call cw_generator_start().
*/
void cw_generator_stop(void)
{
	if (!generator) {
		cw_dev_debug ("called the function for NULL generator");
		return;
	}

	if (generator->audio_system == CW_AUDIO_CONSOLE) {
		/* sine wave generation should have been stopped
		   by a code generating dots/dashes, but
		   just in case... */
		ioctl(generator->audio_sink, KIOCSOUND, 0);
	} else if (generator->audio_system == CW_AUDIO_OSS
		   || generator->audio_system == CW_AUDIO_ALSA) {

		cw_sound_soundcard_internal(CW_TONE_SILENT);

		/* time needed between initiating stop sequence and
		   ending write() to device and closing the device */
		int usleep_time = generator->sample_rate / (2 * generator->buffer_n_samples);
		usleep_time /= 1000000;
		usleep(usleep_time * 1.2);

		generator->generate = 0;

		/* Sleep some more to postpone closing a device.
		   This way we can avoid a situation when 'generate' is set
		   to zero and device is being closed while a new buffer is
		   being prepared, and while write() tries to write this
		   new buffer to already closed device.

		   Without this usleep(), writei() from ALSA library may
		   return "File descriptor in bad state" error - this
		   happened when writei() tried to write to closed ALSA
		   handle. */
		usleep(10000);
	} else {
		cw_dev_debug ("called stop() function for generator without audio system specified");
	}

	return;
}





/**
   \brief Calculate a fragment of sine wave

   Calculate a fragment of sine wave, as many samples as can be
   fitted in generator's buffer. There will be gen->buffer_n_samples
   samples put into gen->buffer, starting from gen->buffer[0].

   The function takes into account all state variables from gen,
   so initial phase of new fragment of sine wave in the buffer matches
   ending phase of a sine wave generated in current call.

   \param gen - current generator

   \return CW_SUCCESS
*/
int cw_generator_calculate_sine_wave(cw_gen_t *gen)
{
	int i = 0;
	double phase = 0.0;

	for (i = 0; i < gen->buffer_n_samples; i++) {
		double phase = (2.0 * M_PI
				* (double) gen->frequency * (double) i
				/ (double) gen->sample_rate)
			+ gen->phase_offset;
		int amplitude = cw_generator_calculate_amplitude(gen);

		gen->buffer[i] = amplitude * sin(phase);
	}

	/* Compute the phase of the last generated sample
	   (or is it phase of first sample in next series?). */
	phase = (2.0 * M_PI
		 * (double) gen->frequency * (double) i
		 / (double) gen->sample_rate)
		+ gen->phase_offset;

	/* Extract the normalized phase offset. */
	int n_periods = floor(phase / (2.0 * M_PI));
	gen->phase_offset = phase - n_periods * 2.0 * M_PI;

	return CW_SUCCESS;
}





/**
   \brief Calculate value of a sample of sine wave

   \param gen - generator used to generate a sine wave

   \return value of a sample of sine wave, a non-negative number
*/
int cw_generator_calculate_amplitude(cw_gen_t *gen)
{
	int volume = (gen->volume * CW_AUDIO_VOLUME_RANGE) / 100;
	if (gen->slope == 0) {
		;
	} else if (gen->slope < 0) {
		if (gen->amplitude > 0) {
			gen->amplitude += gen->slope; /* yes, += */
		} else if (gen->amplitude < 0) {
			gen->amplitude = 0;
			gen->slope = 0;
		} else { /* gen->amplitude == 0 */
			gen->slope = 0;
		}
	} else { /* gen->slope > 0 */
		if (gen->amplitude < volume) {
			gen->amplitude += gen->slope;
		} else if (gen->amplitude > volume) {
			gen->amplitude = volume;
			gen->slope = 0;
		} else { /* gen->amplitude == volume; */
			gen->slope = 0;
		}
	}

	/* because CW_AUDIO_VOLUME_RANGE may not be exact multiple
	   of gen->slope, gen->amplitude may be sometimes out
	   of range; this may produce audible clicks;
	   remove values out of range */
	if (gen->amplitude > CW_AUDIO_VOLUME_RANGE) {
		gen->amplitude = CW_AUDIO_VOLUME_RANGE;
	} else if (gen->amplitude < 0) {
		gen->amplitude = 0;
	} else {
		;
	}

	return gen->amplitude;
}





/* ************************ */
/*    Console output        */
/* ************************ */





/*
 * Clock tick rate used for KIOCSOUND console ioctls.  This value is taken
 * from linux/include/asm-i386/timex.h, included here for portability.
 */
static const int KIOCSOUND_CLOCK_TICK_RATE = 1193180;





/**
   \brief Check if it is possible to open console output

   Function does a test opening and test writing to console device,
   but it closes it before returning.

   The function tests that the given console file exists, and that it
   will accept the KIOCSOUND ioctl.  It unconditionally returns false
   on platforms that do no support the KIOCSOUND ioctl.

   Call to ioctl will fail if calling code doesn't have root privileges.

   This is the only place where we ask if KIOCSOUND is defined, so client
   code must call this function whenever it wants to use console output,
   as every other function called to perform console operations will
   happily assume that it is allowed to perform such operations.

   \param device - name of console device to be used; if NULL then library will use default device.

   \return true if opening console output succeeded;
   \return false if opening console output failed;
*/
bool cw_is_console_possible(const char *device)
{
#if defined(KIOCSOUND)
	/* no need to allocate space for device path, just a
	   pointer (to a memory allocated somewhere else by
	   someone else) will be sufficient in local scope */
	const char *dev = device ? device : CW_DEFAULT_CONSOLE_DEVICE;

	int fd = open(dev, O_WRONLY);
	if (fd == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: open(%s): %s\n", dev, strerror(errno));
		return CW_FAILURE;
	}

	int rv = ioctl(fd, KIOCSOUND, 0);
	close(fd);
	if (rv == -1) {
		/* console device can be opened, even with WRONLY perms, but,
		   if you aren't root user, you can't call ioctl()s on it,
		   and - as a result - can't generate sound on the device */
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
#else
	return CW_FAILURE;
#endif
}





/**
   \brief Open console PC speaker device

   The function doesn't check if ioctl(fd, KIOCSOUND, ...) works,
   the client code must use cw_is_console_possible() instead, prior
   to calling this function.

   \param device - name of console device to be used; if NULL then library will use default device.

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_open_device_console(const char *device)
{
#ifndef KIOCSOUND
	assert (0); /* You should have called cw_is_console_possible(). Bad developer, bad! */
#endif
	assert (device);

	if (generator->audio_device_open) {
		/* Ignore the call if the console device is already open. */
		return CW_SUCCESS;
	}

	int console = open(device, O_WRONLY);
	if (console == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: open(%s): \"%s\"", device, strerror(errno));
		return CW_FAILURE;
        } else {
		cw_dev_debug ("open successfully, console = %d", console);
	}

	generator->audio_sink = console;
	generator->audio_device_open = 1;

	return CW_SUCCESS;
}





/**
   \brief Close console device associated with current generator
*/
void cw_close_device_console(void)
{
	close(generator->audio_sink);
	generator->audio_sink = -1;
	generator->audio_device_open = 0;

	cw_debug (CW_DEBUG_SOUND, "console closed");

	return;
}





/**
   \brief Start generating a sound using console PC speaker

   The function calls the KIOCSOUND ioctl to start a particular tone.
   Once started, the console tone generation needs no maintenance.

   The function only initializes generation, you have to do another
   function call to change the tone generated.

   \param state - flag deciding if a sound should be generated (> 0) or not (== 0)

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_sound_console_internal(int state)
{
	/*
	 * Calculate the correct argument for KIOCSOUND.  There's nothing we
	 * can do to control the volume, but if we find the volume is set to
	 * zero, the one thing we can do is to just turn off tones.  A bit
	 * crude, but perhaps just slightly better than doing nothing.
	 */
	int argument = 0;
	if (generator->volume > 0 && state) {
		argument = KIOCSOUND_CLOCK_TICK_RATE / generator->frequency;
	}

	cw_debug (CW_DEBUG_SOUND, "KIOCSOUND arg = %d (switch: %d, frequency: %d Hz, volume: %d %%)",
		  argument, state, generator->frequency, generator->volume);

	/* Call the ioctl, and return any error status. */
	if (ioctl(generator->audio_sink, KIOCSOUND, argument) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl KIOCSOUND: \"%s\"\n", strerror(errno));
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





/* ************************ */
/*       OSS output         */
/* ************************ */





/**
   \brief Check if it is possible to open OSS output

   Function does a test opening and test configuration of OSS output,
   but it closes it before returning.

   \param device - name of OSS device to be used; if NULL then library will use default device.

   \return true if opening OSS output succeeded;
   \return false if opening OSS output failed;
*/
bool cw_is_oss_possible(const char *device)
{
	const char *dev = device ? device : CW_DEFAULT_OSS_DEVICE;
	/* Open the given soundcard device file, for write only. */
	int soundcard = open(dev, O_WRONLY);
	if (soundcard == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: cwlib: open(%s): \"%s\"", dev, strerror(errno));
		return false;
        }

	int parameter = 0;
	if (ioctl(soundcard, OSS_GETVERSION, &parameter) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl OSS_GETVERSION");
		close(soundcard);
		return false;
        } else {
		cw_dev_debug ("OSS version %X.%X.%X",
			(parameter & 0xFF0000) >> 16,
			(parameter & 0x00FF00) >> 8,
			(parameter & 0x0000FF) >> 0);
	}

	/*
	  http://manuals.opensound.com/developer/OSS_GETVERSION.html:
	  about OSS_GETVERSION ioctl:
	  "This ioctl call returns the version number OSS API used in
	  the current system. Applications can use this information to
	  find out if the OSS version is new enough to support the
	  features required by the application. However this methods
	  should be used with great care. Usually it's recommended
	  that applications check availability of each ioctl() by
	  calling it and by checking if the call returned errno=EINVAL."

	  So, we call all necessary ioctls to be 100% sure that all
	  needed features are available. cw_open_device_oss_ioctls()
	  doesn't specifically look for EINVAL, it only checks return
	  values from ioctl() and returns CW_FAILURE if one of ioctls()
	  returns -1. */
	int dummy;
	int rv = cw_open_device_oss_ioctls(&soundcard, &dummy);
	close(soundcard);
	if (rv != CW_SUCCESS) {
		cw_debug (CW_DEBUG_SYSTEM, "error: one or more OSS ioctl() calls failed");
		return false;
	} else {
		return true;
	}
}





/**
   \brief Open OSS output, associate it with current generator

   \param device - name of OSS device to be used; if NULL then library will use default device.

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_open_device_oss(const char *device)
{
	/* Open the given soundcard device file, for write only. */
	int soundcard = open(device, O_WRONLY);
	if (soundcard == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: open(%s): \"%s\"\n", device, strerror(errno));
		return CW_FAILURE;
        }

	int rv = cw_open_device_oss_ioctls(&soundcard, &(generator->sample_rate));
	if (rv != CW_SUCCESS) {
		cw_debug (CW_DEBUG_SYSTEM, "error: one or more OSS ioctl() calls failed\n");
		close(soundcard);
		return CW_FAILURE;
	}

	int size = 0;
	/* Get fragment size in bytes, may be different than requested
	   with ioctl(..., SNDCTL_DSP_SETFRAGMENT), and, in particular,
	   can be different than 2^N. */
	if ((rv = ioctl(soundcard, SNDCTL_DSP_GETBLKSIZE, &size)) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_GETBLKSIZE): \"%s\"\n", strerror(errno));
		close(soundcard);
		return CW_FAILURE;
        }

	if ((size & 0x0000ffff) != (1 << CW_OSS_SETFRAGMENT)) {
		cw_debug (CW_DEBUG_SYSTEM, "error: OSS fragment size not set, %d\n", size);
		close(soundcard);
		return CW_FAILURE;
        } else {
		cw_dev_debug ("OSS fragment size = %d", size);
	}
	generator->buffer_n_samples = size;


	/* Note sound as now open for business. */
	generator->audio_device_open = 1;
	generator->audio_sink = soundcard;

	generator->debug_sink = open("/tmp/cw_file.raw", O_WRONLY | O_NONBLOCK);

	return CW_SUCCESS;
}





/**
   \brief Perform all necessary ioctl calls on OSS file descriptor

   Wrapper function for ioctl calls that need to be done when configuring
   file descriptor \param fd for OSS playback.

   \param fd - file descriptor of open OSS file;
   \param sample_rate - sample rate configured by ioctl calls (output parameter)

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_open_device_oss_ioctls(int *fd, int *sample_rate)
{
	int parameter = 0; /* ignored */
	if (ioctl(*fd, SNDCTL_DSP_SYNC, &parameter) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_SYNC): \"%s\"\n", strerror(errno));
		return CW_FAILURE;
        }

	parameter = 0; /* ignored */
	if (ioctl(*fd, SNDCTL_DSP_POST, &parameter) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_POST): \"%s\"\n", strerror(errno));
		return CW_FAILURE;
        }

	/* Set the audio format to 8-bit unsigned. */
	parameter = CW_OSS_SAMPLE_FORMAT;
	if (ioctl(*fd, SNDCTL_DSP_SETFMT, &parameter) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_SETFMT): \"%s\"\n", strerror(errno));
		return CW_FAILURE;
        }
	if (parameter != CW_OSS_SAMPLE_FORMAT) {
		cw_debug (CW_DEBUG_SYSTEM, "error: sample format not supported\n");
		return CW_FAILURE;
        }

	/* Set up mono mode - a single audio channel. */
	parameter = CW_AUDIO_CHANNELS;
	if (ioctl(*fd, SNDCTL_DSP_CHANNELS, &parameter) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_CHANNELS): \"%s\"\n", strerror(errno));
		return CW_FAILURE;
        }
	if (parameter != CW_AUDIO_CHANNELS) {
		cw_debug (CW_DEBUG_SYSTEM, "error: number of channels not supported\n");
		return CW_FAILURE;
        }

	/*
	 * Set up a standard sampling rate based on the notional correct value,
	 * and retain the one we actually get in the library variable.
	 */
	unsigned int rate = CW_AUDIO_SAMPLE_RATE_A;
	if (ioctl(*fd, SNDCTL_DSP_SPEED, &rate) == -1) {
		rate = CW_AUDIO_SAMPLE_RATE_B;
		if (ioctl(*fd, SNDCTL_DSP_SPEED, &rate) == -1) {
			cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_SPEED): \"%s\"\n", strerror(errno));
			return CW_FAILURE;
		}
        }
	if (rate != CW_AUDIO_SAMPLE_RATE_A && rate != CW_AUDIO_SAMPLE_RATE_B) {
		cw_dev_debug ("warning: imprecise sample rate: %d", rate);
	}


	*sample_rate = rate;


	audio_buf_info buff;
	if (ioctl(*fd, SNDCTL_DSP_GETOSPACE, &buff) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_GETOSPACE): \"%s\"\n", strerror(errno));
		return CW_FAILURE;
        } else {
		/*
		fprintf(stderr, "before:\n");
		fprintf(stderr, "buff.fragments = %d\n", buff.fragments);
		fprintf(stderr, "buff.fragsize = %d\n", buff.fragsize);
		fprintf(stderr, "buff.bytes = %d\n", buff.bytes);
		fprintf(stderr, "buff.fragstotal = %d\n", buff.fragstotal);
		*/
	}


#if CW_OSS_SET_FRAGMENT
	/*
	 * Live a little dangerously, by trying to set the fragment size of the
	 * card.  We'll try for a relatively short fragment of 128 bytes.  This
	 * gives us a little better granularity over the amounts of audio data
	 * we write periodically to the soundcard output buffer.  We may not get
	 * the requested fragment size, and may be stuck with the default.  The
	 * argument has the format 0xMMMMSSSS - fragment size is 2^SSSS, and
	 * setting 0x7fff for MMMM allows as many fragments as the driver can
	 * support.
	 */
	/* parameter = 0x7fff << 16 | CW_OSS_SETFRAGMENT; */
	parameter = 0x0032 << 16 | CW_OSS_SETFRAGMENT;

	if (ioctl(*fd, SNDCTL_DSP_SETFRAGMENT, &parameter) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_SETFRAGMENT): \"%s\"\n", strerror(errno));
		return CW_FAILURE;
        }
	cw_debug (CW_DEBUG_SOUND, "fragment size is %d", parameter & 0x0000ffff);

	/* Query fragment size just to get the driver buffers set. */
	if (ioctl(*fd, SNDCTL_DSP_GETBLKSIZE, &parameter) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_GETBLKSIZE): \"%s\"\n", strerror(errno));
		return CW_FAILURE;
        }

	if (parameter != (1 << CW_OSS_SETFRAGMENT)) {
		cw_debug (CW_DEBUG_SYSTEM, "error: OSS fragment size not set, %d\n", parameter);
        }

#endif
#if CW_OSS_SET_POLICY
	parameter = 5;
	if (ioctl(*fd, SNDCTL_DSP_POLICY, &parameter) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_DSP_POLICY): \"%s\"\n", strerror(errno));
		return CW_FAILURE;
        }
#endif

	if (ioctl(*fd, SNDCTL_DSP_GETOSPACE, &buff) == -1) {
		cw_debug (CW_DEBUG_SYSTEM, "error: ioctl(SNDCTL_GETOSPACE): \"%s\"\n", strerror(errno));
		return CW_FAILURE;
        } else {
		/*
		fprintf(stderr, "after:\n");
		fprintf(stderr, "buff.fragments = %d\n", buff.fragments);
		fprintf(stderr, "buff.fragsize = %d\n", buff.fragsize);
		fprintf(stderr, "buff.bytes = %d\n", buff.bytes);
		fprintf(stderr, "buff.fragstotal = %d\n", buff;3R.fragstotal);
		*/
	}

	return CW_SUCCESS;
}





/**
   \brief Close OSS device associated with current generator
*/
void cw_close_device_oss(void)
{
	close(generator->audio_sink);
	generator->audio_sink = -1;
	generator->audio_device_open = 0;

	if (generator->debug_sink != -1) {
		close(generator->debug_sink);
		generator->debug_sink = -1;
	}

	return;
}





/**
   \brief Write a constant sine wave to OSS output

   \param arg - current generator (casted to (void *)

   \return NULL pointer
*/
void *cw_generator_write_sine_wave_oss(void *arg)
{
	cw_gen_t *gen = (cw_gen_t *) arg;

	int n_bytes = sizeof (gen->buffer[0]) * gen->buffer_n_samples;
	while (gen->generate) {
		cw_generator_calculate_sine_wave(gen);
		if (write(gen->audio_sink, gen->buffer, n_bytes) != n_bytes) {
			gen->thread_error = errno;
			cw_debug (CW_DEBUG_SYSTEM, "error: audio write (OSS): %s\n", strerror(errno));
			return NULL;
		}
		if (gen->debug_sink != -1) {
			write(gen->debug_sink, gen->buffer, n_bytes);
		}
	} /* while() */

	return NULL;
}





/* ************************ */
/*       ALSA output        */
/* ************************ */





/**
   \brief Check if it is possible to open ALSA output

   Function does a test opening of ALSA output, but it closes it
   before returning.

   \param device - name of ALSA device to be used; if NULL then library will use default device.

   \return true if opening ALSA output succeeded;
   \return false if opening ALSA output failed;
*/
bool cw_is_alsa_possible(const char *device)
{
	const char *dev = device ? device : CW_DEFAULT_ALSA_DEVICE;
	snd_pcm_t *alsa_handle;
	int rv = snd_pcm_open(&alsa_handle,
			      dev,                     /* name */
			      SND_PCM_STREAM_PLAYBACK, /* stream (playback/capture) */
			      0);                      /* mode, 0 | SND_PCM_NONBLOCK | SND_PCM_ASYNC */
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't open ALSA device \"%s\"\n", dev);
		return false;
	} else {
		snd_pcm_close(alsa_handle);
		return true;
	}
}





/**
   \brief Open ALSA output, associate it with current generator

   \param device - name of ALSA device to be used; if NULL then library will use default device.

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_open_device_alsa(const char *device)
{
	int rv = snd_pcm_open(&(generator->alsa_handle),
			      device,                  /* name */
			      SND_PCM_STREAM_PLAYBACK, /* stream (playback/capture) */
			      0);                      /* mode, 0 | SND_PCM_NONBLOCK | SND_PCM_ASYNC */
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't open ALSA device \"%s\"\n", device);
		return CW_FAILURE;
	}

	/* TODO: move this to cw_set_alsa_hw_params(),
	   deallocate hw_params */
	snd_pcm_hw_params_t *hw_params = NULL;
	rv = snd_pcm_hw_params_malloc(&hw_params);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't allocate memory for ALSA hw params\n");
		return CW_FAILURE;
	}

	rv = cw_set_alsa_hw_params(generator->alsa_handle, hw_params);
	if (rv != CW_SUCCESS) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't set ALSA hw params\n");
		return CW_FAILURE;
	}

	rv = snd_pcm_prepare(generator->alsa_handle);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't prepare ALSA handler\n");
		return CW_FAILURE;
	}

	/* Get size for data buffer */
	snd_pcm_uframes_t frames; /* period size in frames */
	int dir = 1;
	rv = snd_pcm_hw_params_get_period_size(hw_params, &frames, &dir);
	cw_dev_debug ("rv = %d, ALSA buffer size would be %u frames", rv, (unsigned int) frames);

	/* The linker (?) that I use on Debian links cwlib against
	   old version of get_period_size(), which returns
	   period size as return value. This is a workaround. */
	if (rv > 1) {
		generator->buffer_n_samples = rv;
	} else {
		generator->buffer_n_samples = frames;
	}
	cw_dev_debug ("ALSA buf size %u", (unsigned int) generator->buffer_n_samples);

	return CW_SUCCESS;
}





/**
   \brief Close ALSA device associated with current generator
*/
void cw_close_device_alsa(void)
{
	snd_pcm_drain(generator->alsa_handle);
	snd_pcm_close(generator->alsa_handle);

	generator->audio_device_open = 0;

	if (generator->debug_sink != -1) {
		close(generator->debug_sink);
		generator->debug_sink = -1;
	}

	return;
}





/**
   \brief Write a constant sine wave to ALSA output

   \param arg - current generator (casted to (void *)

   \return NULL pointer
*/
void *cw_generator_write_sine_wave_alsa(void *arg)
{
	cw_gen_t *gen = (cw_gen_t *) arg;

	int n_bytes = sizeof (gen->buffer[0]) * gen->buffer_n_samples;
	while (gen->generate) {
		cw_generator_calculate_sine_wave(gen);

		int rv = snd_pcm_writei(gen->alsa_handle, gen->buffer, gen->buffer_n_samples);
		if (rv == -EPIPE) {
			/* EPIPE means underrun */
			cw_debug (CW_DEBUG_SYSTEM, "ALSA: underrun");
			snd_pcm_prepare(gen->alsa_handle);
		} else if (rv < 0) {
			cw_debug (CW_DEBUG_SYSTEM, "ALSA: writei: %s\n", snd_strerror(rv));
		}  else if (rv != gen->buffer_n_samples) {
			cw_debug (CW_DEBUG_SYSTEM, "ALSA: short write, %d != %d", rv, gen->buffer_n_samples);
		} else {
			;
		}

		if (gen->debug_sink != -1) {
			write(gen->debug_sink, gen->buffer, n_bytes);
		}
	} /* while() */

	return NULL;
}





/**
   \brief Set up hardware buffer parameters of ALSA sink

   \param handle - ALSA handle to configure
   \param params - allocated hw params data structure to be used

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_set_alsa_hw_params(snd_pcm_t *handle, snd_pcm_hw_params_t *params)
{
	/* Get current hw configuration. */
	int rv = snd_pcm_hw_params_any(handle, params);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't get current hw params: %s\n", snd_strerror(rv));
		return CW_FAILURE;
	}


	/* Set the sample format */
	rv = snd_pcm_hw_params_set_format(handle, params, CW_ALSA_SAMPLE_FORMAT);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't set sample format: %s\n", snd_strerror(rv));
		return CW_FAILURE;
	}


	int dir = 0;

	int sample_rates[] = {
		CW_AUDIO_SAMPLE_RATE_A,
		CW_AUDIO_SAMPLE_RATE_B,
		-1 /* guard */
	};

	/* Set the sample rate (may set/influence/modify 'period size') */
	int success = 0;
	int i = 0;
	while (sample_rates[i] != -1) {
		rv = snd_pcm_hw_params_test_rate(handle, params, sample_rates[i], dir);
		if (rv == 0) {
			rv = snd_pcm_hw_params_set_rate(handle, params, sample_rates[i], dir);
			if (rv == 0) {
				success = 1;
				break;
			}
		}
		i++;
	}

	if (!success) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't set sample rate\n");
		return CW_FAILURE;
	} else {
		generator->sample_rate = sample_rates[i];
	}

	/* Set PCM access type */
	rv = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't set access type: %s\n", snd_strerror(rv));
		return CW_FAILURE;
	}

	/* Set number of channels */
	rv = snd_pcm_hw_params_set_channels(handle, params, CW_AUDIO_CHANNELS);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't set number of channels: %s\n", snd_strerror(rv));
		return CW_FAILURE;
	}


#if CW_ALSA_HW_BUFFER_CONFIG

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
			rv = snd_pcm_hw_params_test_buffer_size(handle, params, val);
			if (rv == 0) {
				cw_dev_debug ("accepted buffer size: %u", (unsigned int) accepted);
				/* Accept only the smallest available buffer size */
				accepted = val;
				break;
			}
		}

		if (accepted > 0) {
			rv = snd_pcm_hw_params_set_buffer_size(handle, params, accepted);
			if (rv < 0) {
				cw_debug (CW_DEBUG_SYSTEM, "error: can't set accepted buffer size %u: %s\n", (unsigned int) accepted, snd_strerror(rv));
			}
		} else {
			cw_debug (CW_DEBUG_SYSTEM, "error: no accepted buffer size\n");
		}
	}

	{
		/* Test and attempt to set number of periods */

		dir = 0;
		unsigned int accepted = 0; /* number of periods per buffer */
		/* this limit should be enough, 'accepted' on my machine is 8 */
		const unsigned int n_periods_max = 30;
		for (unsigned int val = 1; val < n_periods_max; val++) {
			rv = snd_pcm_hw_params_test_periods(handle, params, val, dir);
			if (rv == 0) {
				accepted = val;
				cw_dev_debug ("accepted number of periods: %d", accepted);
			}
		}
		if (accepted > 0) {
			rv = snd_pcm_hw_params_set_periods(handle, params, accepted, dir);
			if (rv < 0) {
				cw_dev_debug ("can't set accepted number of periods %d: %s", accepted, snd_strerror(rv));
			}
		} else {
			cw_debug (CW_DEBUG_SYSTEM, "error: no accepted number of periods\n");
		}
	}
#if 0
	{
		/* Test period size */
		dir = 0;
		for (snd_pcm_uframes_t val = 0; val < 100000; val++) {
			rv = snd_pcm_hw_params_test_period_size(handle, params, val, dir);
			if (rv == 0) {
				fprintf(stderr, "cwlib: accepted period size: %d\n", val);
				// break;
			}
		}
	}

	{
		/* Test buffer time */
		dir = 0;
		for (unsigned int val = 0; val < 100000; val++) {
			rv = snd_pcm_hw_params_test_buffer_time(handle, params, val, dir);
			if (rv == 0) {
				fprintf(stderr, "cwlib: accepted buffer time: %d\n", val);
				// break;
			}
		}
	}
#endif

#endif

	/* Save hw parameters to device */
	rv = snd_pcm_hw_params(handle, params);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't save hw parameters: %s\n", snd_strerror(rv));
		return CW_FAILURE;
	} else {
		/* Get size for data buffer */
		snd_pcm_uframes_t frames; /* period size in frames */
		int dir = 0;
		snd_pcm_hw_params_get_period_size(params, &frames, &dir);
		cw_dev_debug ("%d, ALSA buffer size would be %u frames", rv, (unsigned int) frames);
		return CW_SUCCESS;
	}
}





#if CW_DEV

/* debug function */
int cw_print_alsa_params(snd_pcm_hw_params_t *params)
{
	unsigned int val = 0;
	int dir = 0;

	int rv = snd_pcm_hw_params_get_periods(params, &val, &dir);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't get 'periods': %s", snd_strerror(rv));
	} else {
		cw_dev_debug ("'periods' = %u", val);
	}

	snd_pcm_uframes_t period_size = 0;
	rv = snd_pcm_hw_params_get_period_size(params, &period_size, &dir);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't get 'period size': %s", snd_strerror(rv));
	} else {
		cw_dev_debug ("'period size' = %u", (unsigned int) period_size);
	}

	snd_pcm_uframes_t buffer_size;
	rv = snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	if (rv < 0) {
		cw_debug (CW_DEBUG_SYSTEM, "error: can't get buffer size: %s", snd_strerror(rv));
	} else {
		cw_dev_debug ("'buffer size' = %u", (unsigned int) buffer_size);
	}

	return CW_SUCCESS;
}

#endif





/* ************************ */
/*          main()          */
/* ************************ */


#if CW_MAIN


typedef bool (*predicate_t)(const char *device);
static void main_helper(int audio_system, const char *name, const char *device, predicate_t predicate);





/* for stand-alone testing */
int main(void)
{
	main_helper(CW_AUDIO_ALSA,    "ALSA",    CW_DEFAULT_ALSA_DEVICE,    cw_is_alsa_possible);
	main_helper(CW_AUDIO_CONSOLE, "console", CW_DEFAULT_CONSOLE_DEVICE, cw_is_console_possible);
	main_helper(CW_AUDIO_OSS,     "OSS",     CW_DEFAULT_OSS_DEVICE,     cw_is_oss_possible);

	return 0;
}





void main_helper(int audio_system, const char *name, const char *device, predicate_t predicate)
{
	int rv = CW_FAILURE;

	rv = predicate(device);
	if (rv == CW_SUCCESS) {
		rv = cw_generator_new(audio_system, device);
		if (rv == CW_SUCCESS) {
			cw_reset_send_receive_parameters();
			cw_set_send_speed(22);
			cw_generator_start();

			cw_send_string("morse");
			cw_wait_for_tone_queue();

			cw_generator_stop();
			cw_generator_delete();
		} else {
			cw_debug (CW_DEBUG_SYSTEM, "error: can't create %s generator\n", name);
		}
	} else {
		cw_debug (CW_DEBUG_SYSTEM, "error: %s output is not available\n", name);
	}
}


#endif


