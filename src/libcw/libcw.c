/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2013  Kamil Ignacak (acerion@wp.pl)

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


/*
   Table of contents

   - Section:Debugging
   - Section:Core Morse code data and lookup
   - Section:Extended Morse code data and lookup (procedural signals)
   - Section:Phonetic alphabet
   - Section:Morse code controls and timing parameters
   - Section:SIGALRM and timer handling
   - Section:General control of console buzzer and of soundcard
   - Section:Finalization and cleanup
   - Section:Keying control
   - Section:Tone queue
   - Section:Sending
   - Section:Receive tracking and statistics helpers
   - Section:Receiving
   - Section:Iambic keyer
   - Section:Straight key
   - Section:Generator - generic
   - Section:Soundcard
   - Section:main() function for testing purposes
   - Section:Unit tests for internal functions
   - Section:Global variables
*/


#include "config.h"


#define _BSD_SOURCE   /* usleep() */
#define _POSIX_SOURCE /* sigaction() */
#define _POSIX_C_SOURCE 200112L /* pthread_sigmask() */


#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
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

#include <dlfcn.h> /* dlopen() and related symbols */



#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif


/* http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=403043 */
#if defined(NSIG)             /* Debian GNU/Linux: signal.h; Debian kFreeBSD: signal.h (libc0.1-dev_2.13-21_kfreebsd-i386.deb) */
#define CW_SIG_MAX (NSIG)
#elif defined(_NSIG)          /* Debian GNU/Linux: asm-generic/signal.h; Debian kFreeBSD: i386-kfreebsd-gnu/bits/signum.h->signal.h (libc0.1-dev_2.13-21_kfreebsd-i386.deb) */
#define CW_SIG_MAX (_NSIG)
#elif defined(RTSIG_MAX)      /* Debian GNU/Linux: linux/limits.h */
#define CW_SIG_MAX ((RTSIG_MAX)+1)
#else
#error "unknown number of signals"
#endif



#if defined(BSD)
# define ERR_NO_SUPPORT EPROTONOSUPPORT
#else
# define ERR_NO_SUPPORT EPROTO
#endif


#include "libcw.h"

#include "libcw_internal.h"
#include "libcw_null.h"
#include "libcw_console.h"
#include "libcw_oss.h"

#include "copyright.h"
#include "libcw_debug.h"





/* ******************************************************************** */
/*      Section:General control of console buzzer and of soundcard      */
/* ******************************************************************** */


static int cw_generator_silence_internal(cw_gen_t *gen);
static int cw_generator_release_internal(void);
static int cw_generator_new_open_internal(cw_gen_t *gen, int audio_system, const char *device);





/* ******************************************************************** */
/*                     Section:Generator - generic                      */
/* ******************************************************************** */





static void *cw_generator_write_sine_wave_internal(void *arg);
static int   cw_generator_calculate_sine_wave_internal(cw_gen_t *gen, cw_tone_t *tone);
static int   cw_generator_calculate_amplitude_internal(cw_gen_t *gen, cw_tone_t *tone);





/* ******************************************************************** */
/*                         Section:Soundcard                            */
/* ******************************************************************** */
static int cw_soundcard_write_internal(cw_gen_t *gen, int queue_state, cw_tone_t *tone);





/* ******************************************************************** */
/*              Section:Core Morse code data and lookup                 */
/* ******************************************************************** */
/* functions handling representation of a character;
   representation looks like this: ".-" for "a", "--.." for "z", etc. */
static bool         cw_representation_lookup_init_internal(const cw_entry_t *lookup[]);
static int          cw_representation_to_character_internal(const char *representation);
static unsigned int cw_representation_to_hash_internal(const char *representation);
static const char  *cw_character_to_representation_internal(int c);





/* ******************************************************************** */
/*   Section:Extended Morse code data and lookup (procedural signals)   */
/* ******************************************************************** */
static const char *cw_lookup_procedural_character_internal(int c, bool *is_usually_expanded);





/* ******************************************************************** */
/*                 Section:SIGALRM and timer handling                   */
/* ******************************************************************** */
static int  cw_timer_run_internal(int usecs);
static int  cw_timer_run_with_handler_internal(int usecs, void (*sigalrm_handler)(void));
static void cw_sigalrm_handlers_caller_internal(int signal_number);
static bool cw_sigalrm_is_blocked_internal(void);
static int  cw_sigalrm_block_internal(bool block);
static int  cw_sigalrm_restore_internal(void);
static int  cw_sigalrm_install_top_level_handler_internal(void);
static int  cw_signal_wait_internal(void);
static void cw_signal_main_handler_internal(int signal_number);





/* ******************************************************************** */
/*                         Section:Tone queue                           */
/* ******************************************************************** */
/* Tone queue - a circular list of tone durations and frequencies pending,
   and a pair of indexes, tail (enqueue) and head (dequeue) to manage
   additions and asynchronous sending.

   The CW tone queue functions implement the following state graph:

                     (queue empty)
            +-------------------------------+
            |                               |
            v    (queue started)            |
   ----> QS_IDLE ---------------> QS_BUSY --+
                                  ^     |
                                  |     |
                                  +-----+
                              (queue not empty)
*/

static int cw_tone_queue_init_internal(cw_tone_queue_t *tq);
static uint32_t cw_tone_queue_length_internal(cw_tone_queue_t *tq);
static int cw_tone_queue_prev_index_internal(int current);
static int cw_tone_queue_next_index_internal(int current);
static int cw_tone_queue_enqueue_internal(cw_tone_queue_t *tq, cw_tone_t *tone);
static int cw_tone_queue_dequeue_internal(cw_tone_queue_t *tq, cw_tone_t *tone);





/* return values from cw_tone_queue_dequeue_internal() */
#define CW_TQ_JUST_EMPTIED 0
#define CW_TQ_STILL_EMPTY  1
#define CW_TQ_NONEMPTY     2





/* ******************************************************************** */
/*                            Section:Sending                           */
/* ******************************************************************** */
static int cw_send_element_internal(cw_gen_t *gen, char element);
static int cw_send_representation_internal(cw_gen_t *gen, const char *representation, bool partial);
static int cw_send_character_internal(cw_gen_t *gen, char character, int partial);





/* ******************************************************************** */
/*                         Section:Debugging                            */
/* ******************************************************************** */




extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;






/* ******************************************************************** */
/*          Section:Morse code controls and timing parameters           */
/* ******************************************************************** */
static void cw_sync_parameters_internal(cw_gen_t *gen);





/* ******************************************************************** */
/*                       Section:Keying control                         */
/* ******************************************************************** */
/* Code maintaining state of a key, and handling changes of key state.
   A key can be in two states:
   \li open - a physical key with electric contacts open, no sound or
   continuous wave is generated;
   \li closed - a physical key with electric contacts closed, a sound
   or continuous wave is generated;

   Key type is not specified. This code maintains state of any type
   of key: straight key, cootie key, iambic key. All that matters is
   state of contacts (open/closed).

   The concept of "key" is extended to a software generator (provided
   by this library) that generates Morse code wave from text input.
   This means that key is closed when a tone (element) is generated,
   and key is open when there is inter-tone (inter-element) space.

   Client code can register - using cw_register_keying_callback() -
   a client callback function. The function will be called every time the
   state of a key changes. */

static void cw_key_set_state_internal(int requested_key_state);
static void cw_key_straight_key_generate_internal(cw_gen_t *gen, int requested_key_state);
static void cw_key_iambic_keyer_generate_internal(cw_gen_t *gen, int requested_key_state, int usecs);





/* ******************************************************************** */
/*                 Section:Finalization and cleanup                     */
/* ******************************************************************** */
static void cw_finalization_schedule_internal(void);
static void cw_finalization_clock_internal(void);
static void cw_finalization_cancel_internal(void);





/* ******************************************************************** */
/*            Section:Receive tracking and statistics helpers           */
/* ******************************************************************** */
static void cw_reset_adaptive_average_internal(cw_tracking_t *tracking, int initial);
static void cw_update_adaptive_average_internal(cw_tracking_t *tracking, int element_usec);
static int  cw_get_adaptive_average_internal(cw_tracking_t *tracking);





/* ******************************************************************** */
/*                           Section:Receiving                          */
/* ******************************************************************** */
static void cw_receive_set_adaptive_internal(bool flag);
static int  cw_receive_identify_tone_internal(int element_usec, char *representation);
static void cw_receive_update_adaptive_tracking_internal(int element_usec, char element);
static int  cw_receive_add_element_internal(const struct timeval *timestamp, char element);
static int  cw_timestamp_validate_internal(const struct timeval *timestamp, struct timeval *return_timestamp);
static int  cw_timestamp_compare_internal(const struct timeval *earlier, const struct timeval *later);





/* ******************************************************************** */
/*                        Section:Iambic keyer                          */
/* ******************************************************************** */
static int cw_keyer_update_internal(void);




#if 0 /* unused */
/* ******************************************************************** */
/*                        Section:Straight key                          */
/* ******************************************************************** */
static void cw_straight_key_clock_internal(void);
#endif




/* ******************************************************************** */
/*                    Section:Global variables                          */
/* ******************************************************************** */

/* main data container; this is a global variable in library file,
   so in future the variable must be moved from the file to client code;

   this is a global variable that should be converted into
   a function argument; this pointer should exist only in
   client's code, should initially be returned by new(), and
   deleted by delete();
   TODO: perform the conversion later, when you figure out
   ins and outs of the library */
static cw_gen_t *generator = NULL;


/* Tone queue associated with a generator.
   Every generator should have a tone queue from which to draw/dequeue
   tones to play. Since generator is a global variable, so is tone queue
   (at least for now). */
static cw_tone_queue_t cw_tone_queue;


/* Every audio system opens an audio device: a default device, or some
   other device. Default devices have their default names, and here is
   a list of them. It is indexed by values of "enum cw_audio_systems". */
const char *default_audio_devices[] = {
	(char *) NULL,          /* CW_AUDIO_NONE */
	CW_DEFAULT_NULL_DEVICE, /* CW_AUDIO_NULL */
	CW_DEFAULT_CONSOLE_DEVICE,
	CW_DEFAULT_OSS_DEVICE,
	CW_DEFAULT_ALSA_DEVICE,
	CW_DEFAULT_PA_DEVICE,
	(char *) NULL }; /* just in case someone decided to index the table with CW_AUDIO_SOUNDCARD */


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


/* Human-readable labels of audio systems.
   Indexed by values of "enum cw_audio_systems". */
const char *cw_audio_system_labels[] = {
	"None",
	"Null",
	"Console",
	"OSS",
	"ALSA",
	"PulseAudio",
	"Soundcard" };



static bool lock = false;


/* FIXME: Provide all three parts of library version. */
static unsigned int major = 4, minor = 0;



/**
   \brief Return version number of libcw library

   Return the version number of the library.
   Version numbers (major and minor) are returned as an int,
   composed of major_version << 16 | minor_version.

   \return library's major and minor version number encoded as single int
*/
int cw_version(void)
{
	return major << 16 | minor;
}





/**
   \brief Print libcw's license text to stdout

   Function prints information about libcw version, followed
   by short text presenting libcw's copyright and license notice.
*/
void cw_license(void)
{
	printf("libcw version %d.%d\n", major, minor);
	printf("%s\n", CW_COPYRIGHT);

	return;
}





/**
   \brief Get a readable label of given audio system

   The function returns one of following strings:
   None, Null, Console, OSS, ALSA, PulseAudio, Soundcard

   \param audio_system - ID of audio system to look up

   \return audio system's label
*/
const char *cw_get_audio_system_label(int audio_system)
{
	return cw_audio_system_labels[audio_system];
}





/* ******************************************************************** */
/*              Section:Core Morse code data and lookup                 */
/* ******************************************************************** */





struct cw_entry_struct{
	const char character;              /* Character represented */
	const char *const representation;  /* Dot-dash shape of the character */
}; // typedef cw_entry_t;





/*
 * Morse code characters table.  This table allows lookup of the Morse shape
 * of a given alphanumeric character.  Shapes are held as a string, with "-"
 * representing dash, and "." representing dot.  The table ends with a NULL
 * entry.
 */

static const cw_entry_t CW_TABLE[] = {
	/* ASCII 7bit letters */
	{'A', ".-"  },  {'B', "-..."},  {'C', "-.-."},
	{'D', "-.." },  {'E', "."   },  {'F', "..-."},
	{'G', "--." },  {'H', "...."},  {'I', ".."  },
	{'J', ".---"},  {'K', "-.-" },  {'L', ".-.."},
	{'M', "--"  },  {'N', "-."  },  {'O', "---" },
	{'P', ".--."},  {'Q', "--.-"},  {'R', ".-." },
	{'S', "..." },  {'T', "-"   },  {'U', "..-" },
	{'V', "...-"},  {'W', ".--" },  {'X', "-..-"},
	{'Y', "-.--"},  {'Z', "--.."},

	/* Numerals */
	{'0', "-----"},  {'1', ".----"},  {'2', "..---"},
	{'3', "...--"},  {'4', "....-"},  {'5', "....."},
	{'6', "-...."},  {'7', "--..."},  {'8', "---.."},
	{'9', "----."},

	/* Punctuation */
	{'"', ".-..-."},  {'\'', ".----."},  {'$', "...-..-"},
	{'(', "-.--." },  {')',  "-.--.-"},  {'+', ".-.-."  },
	{',', "--..--"},  {'-',  "-....-"},  {'.', ".-.-.-" },
	{'/', "-..-." },  {':',  "---..."},  {';', "-.-.-." },
	{'=', "-...-" },  {'?',  "..--.."},  {'_', "..--.-" },
	{'@', ".--.-."},

	/* ISO 8859-1 accented characters */
	{'\334', "..--" },   /* U with diaeresis */
	{'\304', ".-.-" },   /* A with diaeresis */
	{'\307', "-.-.."},   /* C with cedilla */
	{'\326', "---." },   /* O with diaeresis */
	{'\311', "..-.."},   /* E with acute */
	{'\310', ".-..-"},   /* E with grave */
	{'\300', ".--.-"},   /* A with grave */
	{'\321', "--.--"},   /* N with tilde */

	/* ISO 8859-2 accented characters */
	{'\252', "----" },   /* S with cedilla */
	{'\256', "--..-"},   /* Z with dot above */

	/* Non-standard procedural signal extensions to standard CW characters. */
	{'<', "...-.-" },    /* VA/SK, end of work */
	{'>', "-...-.-"},    /* BK, break */
	{'!', "...-."  },    /* SN, understood */
	{'&', ".-..."  },    /* AS, wait */
	{'^', "-.-.-"  },    /* KA, starting signal */
	{'~', ".-.-.." },    /* AL, paragraph */

	/* Sentinel end of table value */
	{0, NULL}
};





/**
   \brief Return the number of characters present in character lookup table

   Return the number of characters that are known to libcw.
   The number only includes alphanumeric characters, punctuation, and following
   procedural characters: VA/SK, BK, SN, AS, KA, AL.

   \return number of known characters
*/
int cw_get_character_count(void)
{
	static int character_count = 0;

	if (character_count == 0) {
		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			character_count++;
		}
	}

	return character_count;
}





/**
   \brief Get list of characters present in character lookup table

   Function provides a string containing all of the characters represented
   in library's lookup table.
   The list only includes alphanumeric characters, punctuation, and following
   procedural characters: VA/SK, BK, SN, AS, KA, AL.

   \p list should be allocated by caller. The length of \p list must be at
   least one greater than the number of characters represented in the
   character lookup table, returned by cw_get_character_count().

   \param list - pointer to space to be filled by function
*/
void cw_list_characters(char *list)
{
	/* Append each table character to the output string. */
	int i = 0;
	for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
		list[i++] = cw_entry->character;
	}

	list[i] = '\0';

	return;
}





/**
   \brief Get length of the longest representation

   Function returns the string length of the longest representation in the
   character lookup table.

   \return a positive number - length of the longest representation
*/
int cw_get_maximum_representation_length(void)
{
	static int maximum_length = 0;

	if (maximum_length == 0) {
		/* Traverse the main lookup table, finding the longest representation. */
		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			int length = (int) strlen (cw_entry->representation);
			if (length > maximum_length) {
				maximum_length = length;
			}
		}
	}

	return maximum_length;
}





/**
   \brief Return representation of given character

   Look up the given character \p c, and return the representation of
   that character.  Return NULL if there is no table entry for the given
   character. Otherwise return pointer to static string with representation
   of character. The string is owned by library.

   \param c - character to look up

   \return pointer to string with representation of character on success
   \return NULL on failure
*/
const char *cw_character_to_representation_internal(int c)
{
	static const cw_entry_t *lookup[UCHAR_MAX];  /* Fast lookup table */
	static bool is_initialized = false;

	/* If this is the first call, set up the fast lookup table to give
	   direct access to the CW table for a given character. */
	if (!is_initialized) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_LOOKUPS, CW_DEBUG_INFO,
			      "libcw: initialize fast lookup table");

		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			lookup[(unsigned char) cw_entry->character] = cw_entry;
		}

		is_initialized = true;
	}

	/* There is no differentiation in the table between upper and lower
	   case characters; everything is held as uppercase.  So before we
	   do the lookup, we convert to ensure that both cases work. */
	c = toupper(c);

	/* Now use the table to lookup the table entry.  Unknown characters
	   return NULL, courtesy of the fact that explicitly uninitialized
	   static variables are initialized to zero, so lookup[x] is NULL
	   if it's not assigned to in the above loop. */
	const cw_entry_t *cw_entry = lookup[(unsigned char) c];

	if (cw_debug_has_flag((&cw_debug_object), CW_DEBUG_LOOKUPS)) {
		if (cw_entry) {
			fprintf (stderr, "libcw: lookup '%c' returned <'%c':\"%s\">\n",
				 c, cw_entry->character, cw_entry->representation);
		} else if (isprint (c)) {
			fprintf (stderr, "libcw: lookup '%c' found nothing\n", c);
		} else {
			fprintf (stderr, "libcw: lookup 0x%02x found nothing\n",
				 (unsigned char) c);
		}
	}

	return cw_entry ? cw_entry->representation : NULL;
}





/**
   \brief Get representation of a given character

   The function is depreciated, use cw_character_to_representation() instead.

   Return the string "shape" of a given Morse code character.  The routine
   returns CW_SUCCESS on success, and fills in the string pointer passed in.
   On error, it returns CW_FAILURE and sets errno to ENOENT, indicating that
   the character could not be found.

   The length of \p representation must be at least one greater than the
   longest representation held in the character lookup table, returned by
   cw_get_maximum_representation_length().

   \param c - character to look up
   \param representation - pointer to space for representation of character

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_lookup_character(char c, char *representation)
{
	/* Lookup the character, and if found, return the string. */
	const char *retval = cw_character_to_representation_internal(c);
	if (retval) {
		if (representation) {
			strcpy(representation, retval);
		}
		return CW_SUCCESS;
	}

	/* Failed to find the requested character. */
	errno = ENOENT;
	return CW_FAILURE;
}





/**
   \brief Get representation of a given character

   On success return representation of a given character.
   Returned pointer is owned by caller of the function.

   On errors function returns NULL and sets errno:
   ENOENT indicates that the character could not be found.
   ENOMEM indicates that character has been found, but function failed
   to strdup() representation.

   \param c - character to look up

   \return pointer to freshly allocated representation on success
   \return NULL on failure
*/
char *cw_character_to_representation(int c)
{
	/* Lookup the character, and if found, return the string */
	const char *representation = cw_character_to_representation_internal(c);
	if (representation) {
		char *r = strdup(representation);
		if (r) {
			return r;
		} else {
			errno = ENOMEM;
			return NULL;
		}
	} else {
		/* Failed to find the requested character */
		errno = ENOENT;
		return NULL;
	}
}





/**
   \brief Return a hash value of a character representation

   Return a hash value, in the range 2-255, for a character representation.
   The routine returns 0 if no valid hash could be made from the string.

   This hash algorithm is designed ONLY for valid CW representations; that is,
   strings composed of only "." and "-", and in this case, strings no longer
   than seven characters.  The algorithm simply turns the representation into
   a "bitmask", based on occurrences of "." and "-".  The first bit set in the
   mask indicates the start of data (hence the 7-character limit).  This mask
   is viewable as an integer in the range 2 (".") to 255 ("-------"), and can
   be used as an index into a fast lookup array.

   \param representation - string representing a character

   \return non-zero value for valid representation
   \return zero for invalid representation
*/
unsigned int cw_representation_to_hash_internal(const char *representation)
{
	/* Our algorithm can handle only 7 characters of representation.
	   And we insist on there being at least one character, too.  */
	int length = (int) strlen (representation);
	if (length > CHAR_BIT - 1 || length < 1) {
		return 0;
	}

	/* Build up the hash based on the dots and dashes; start at 1,
	   the sentinel * (start) bit. */
	unsigned int hash = 1;
	for (int i = 0; i < length; i++) {
		/* Left-shift everything so far. */
		hash <<= 1;

		/* If the next element is a dash, OR in another bit.
		   If it is not a dash or a dot, then there is an error in
		   the representation string. */
		if (representation[i] == CW_DASH_REPRESENTATION) {
			hash |= 1;
		} else if (representation[i] != CW_DOT_REPRESENTATION) {
			return 0;
		} else {
			;
		}
	}

	return hash;
}





/**
   \brief Return character corresponding to given representation

   Look up the given \p representation, and return the character that it
   represents.

   \param representation - representation of a character to look up

   FIXME: function should be able to return zero as non-error value.

   \return zero if there is no character for given representation
   \return non-zero character corresponding to given representation otherwise
*/
int cw_representation_to_character_internal(const char *representation)
{
	static const cw_entry_t *lookup[UCHAR_MAX];   /* Fast lookup table */
	static bool is_complete = true;               /* Set to false if there are any
							 lookup table entries not in
							 the fast lookup table */
	static bool is_initialized = false;

	/* If this is the first call, set up the fast lookup table to give direct
	   access to the CW table for a hashed representation. */
	if (!is_initialized) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_LOOKUPS, CW_DEBUG_INFO,
			      "libcw: initialize hash lookup table");
		is_complete = cw_representation_lookup_init_internal(lookup);
		is_initialized = true;
	}

	/* Hash the representation to get an index for the fast lookup. */
	unsigned int hash = cw_representation_to_hash_internal(representation);

	const cw_entry_t *cw_entry = NULL;
	/* If the hashed lookup table is complete, we can simply believe any
	   hash value that came back.  That is, we just use what is at the index
	   "hash", since this is either the entry we want, or NULL. */
	if (is_complete) {
		cw_entry = lookup[hash];
	} else {
		/* impossible, since test_cw_representation_to_hash_internal()
		   passes without problems */
		/* TODO: add debug message */

		/* If the hashed lookup table is not complete, the lookup
		   might still have found us the entry we are looking for.
		   Here, we'll check to see if it did. */
		if (hash && lookup[hash] && lookup[hash]->representation
		    && strcmp(lookup[hash]->representation, representation) == 0) {
			/* Found it in an incomplete table. */
			cw_entry = lookup[hash];
		} else {
			/* We have no choice but to search the table entry
			   by entry, sequentially, from top to bottom. */
			for (cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
				if (strcmp(cw_entry->representation, representation) == 0) {
					break;
				}
			}

			/* If we got to the end of the table, return zero. */
			cw_entry = cw_entry->character ? cw_entry : 0;
		}
	}

	if (cw_debug_has_flag((&cw_debug_object), CW_DEBUG_LOOKUPS)) {
		if (cw_entry) {
			fprintf (stderr, "libcw: lookup [0x%02x]'%s' returned <'%c':\"%s\">\n",
				 hash, representation,
				 cw_entry->character, cw_entry->representation);
		} else {
			fprintf (stderr, "libcw: lookup [0x%02x]'%s' found nothing\n",
				 hash, representation);
		}
	}

	return cw_entry ? cw_entry->character : 0;
}





/**
   \brief Initialize representation lookup table

   Initialize \p lookup table with values from CW_TABLE (of type cw_entry_t).
   The table is indexed with hashed representations of cw_entry_t->representation
   strings.

   \p lookup table must be large enough to store all entries, caller must
   make sure that the condition is met.

   If all representations from CW_TABLE have valid hashes, and all entries
   from CW_TABLE have been put into \p lookup, the function returns true.
   Otherwise it returns false.

   \param lookup - lookup table to be initialized

   \return true on success
   \return false otherwise
*/
bool cw_representation_lookup_init_internal(const cw_entry_t *lookup[])
{
	bool is_complete = true;

	/* For each main table entry, create a hash entry.  If the hashing
	   of any entry fails, note that the table is not complete and ignore
	   that entry for now (for the current lookup table, this should not
	   happen).  The hashed table speeds up lookups of representations by
	   a factor of 5-10. */
	for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
		unsigned int hash = cw_representation_to_hash_internal(cw_entry->representation);
		if (hash) {
			lookup[hash] = cw_entry;
		} else {
			is_complete = false;
		}
        }

	if (!is_complete) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_LOOKUPS, CW_DEBUG_WARNING,
			      "libcw: hash lookup table incomplete");
	}

	return is_complete;
}





/**
   \brief Check if representation of a character is valid

   This function is depreciated, use cw_representation_is_valid() instead.

   Check that the given string is a valid Morse representation.
   A valid string is one composed of only "." and "-" characters.

   If representation is invalid, function returns CW_FAILURE and sets
   errno to EINVAL.

   \param representation - representation of a character to check

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_check_representation(const char *representation)
{
	bool v = cw_representation_is_valid(representation);
	return v ? CW_SUCCESS : CW_FAILURE;
}





/**
   \brief Check if representation of a character is valid

   Check that the given string is a valid Morse representation.
   A valid string is one composed of only "." and "-" characters.
   This means that the function checks if representation is error-free,
   and not whether the representation represents existing/defined
   character.

   If representation is invalid, function returns false and sets
   errno to EINVAL.

   \param representation - representation of a character to check

   \return true on success
   \return false on failure
*/
bool cw_representation_is_valid(const char *representation)
{
	/* Check the characters in representation. */
	for (int i = 0; representation[i]; i++) {

		if (representation[i] != CW_DOT_REPRESENTATION
		    && representation[i] != CW_DASH_REPRESENTATION) {

			errno = EINVAL;
			return false;
		}
	}

	return true;
}





/**
   \brief Get the character for a given Morse representation

   This function is depreciated, use cw_representation_to_character() instead.

   Function checks \p representation, and if it is valid and
   represents a known character, function returns CW_SUCCESS. Additionally,
   if \p c is non-NULL, function puts the looked up character in \p c.

   \p c should be allocated by caller. Function assumes that \p c being NULL
   pointer is a valid situation, and can return CW_SUCCESS in such situation.

   On error, function returns CW_FAILURE. errno is set to EINVAL if any
   character of the representation is invalid, or ENOENT to indicate that
   the representation could not be found.

   \param representation - representation of a character to look up
   \param c - location where to put looked up character

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_lookup_representation(const char *representation, char *c)
{
	/* Check the characters in representation. */
	if (!cw_representation_is_valid(representation)) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Lookup the representation, and if found, return the character. */
	char character = cw_representation_to_character_internal(representation);
	if (character) {
		if (c) {
			*c = character;
		}
		return CW_SUCCESS;
	}

	/* Failed to find the requested representation. */
	errno = ENOENT;
	return CW_FAILURE;
}





/**
   \brief Return the character for a given Morse representation

   Function checks \p representation, and if it is valid and represents
   a known character, function returns the character (a non-zero value).

   On error, function returns zero. errno is set to EINVAL if any
   character of the representation is invalid, or ENOENT to indicate that
   the representation could not be found.

   \param representation - representation of a character to look up

   \return non-zero character on success
   \return zero on failure
*/
int cw_representation_to_character(const char *representation)
{
	/* Check the characters in representation. */
	if (!cw_representation_is_valid(representation)) {
		errno = EINVAL;
		return 0;
	}

	/* Lookup the representation, and if found, return the character. */
	int c = cw_representation_to_character_internal(representation);
	if (c) {
		return c;
	} else {
		/* Failed to find the requested representation. */
		errno = ENOENT;
		return 0;
	}
}





/* ******************************************************************** */
/*   Section:Extended Morse code data and lookup (procedural signals)   */
/* ******************************************************************** */





/* Ancillary procedural signals table.  This table maps procedural signal
   characters in the main table to their expansions, along with a flag noting
   if the character is usually expanded for display. */
typedef struct {
	const char character;            /* Character represented */
	const char *const expansion;     /* Procedural expansion of the character */
	const bool is_usually_expanded;  /* If expanded display is usual */
} cw_prosign_entry_t;





static const cw_prosign_entry_t CW_PROSIGN_TABLE[] = {
	/* Standard procedural signals */
	{'"', "AF",  false},   {'\'', "WG", false},  {'$', "SX",  false},
	{'(', "KN",  false},   {')', "KK",  false},  {'+', "AR",  false},
	{',', "MIM", false},   {'-', "DU",  false},  {'.', "AAA", false},
	{'/', "DN",  false},   {':', "OS",  false},  {';', "KR",  false},
	{'=', "BT",  false},   {'?', "IMI", false},  {'_', "IQ",  false},
	{'@', "AC",  false},

	/* Non-standard procedural signal extensions to standard CW characters. */
	{'<', "VA", true},  /* VA/SK, end of work */
	{'>', "BK", true},  /* BK, break */
	{'!', "SN", true},  /* SN, understood */
	{'&', "AS", true},  /* AS, wait */
	{'^', "KA", true},  /* KA, starting signal */
	{'~', "AL", true},  /* AL, paragraph */

	/* Sentinel end of table value */
	{0,   NULL, false}
};





/**
   \brief Get number of procedural signals

   \return the number of characters represented in the procedural signal expansion lookup table
*/
int cw_get_procedural_character_count(void)
{
	static int character_count = 0;

	if (character_count == 0) {

		for (const cw_prosign_entry_t *e = CW_PROSIGN_TABLE; e->character; e++) {
			character_count++;
		}
	}

	return character_count;
}





/**
   \brief Get list of characters for which procedural expansion is available

   Function returns into \p list a string containing all of the Morse
   characters for which procedural expansion is available.  The length
   of \p list must be at least by one greater than the number of
   characters represented in the procedural signal expansion lookup
   table, returned by cw_get_procedural_character_count().

   \p list is managed by caller

   \param list - space for returned characters
*/
void cw_list_procedural_characters(char *list)
{
	/* Append each table character to the output string. */
	int i = 0;
	for (const cw_prosign_entry_t *e = CW_PROSIGN_TABLE; e->character; e++) {
		list[i++] = e->character;
	}

	list[i] = '\0';

	return;
}





/**
   \brief Get length of the longest procedural expansion

   Function returns the string length of the longest expansion
   in the procedural signal expansion table.

   \return length
*/
int cw_get_maximum_procedural_expansion_length(void)
{
	static size_t maximum_length = 0;

	if (maximum_length == 0) {
		/* Traverse the main lookup table, finding the longest. */
		for (const cw_prosign_entry_t *e = CW_PROSIGN_TABLE; e->character; e++) {
			size_t length = strlen(e->expansion);
			if (length > maximum_length) {
				maximum_length = length;
			}
		}
	}

	return (int) maximum_length;
}





/**
   \brief Return information related to a procedural character

   Function looks up the given procedural character \p c, and returns the
   expansion of that procedural character, with a display hint in
   \p is_usually_expanded.

   \param c - character to look up
   \param is_usually_expanded - output, display hint

   \return expansion of input character on success
   \return NULL if there is no table entry for the given character
*/
const char *cw_lookup_procedural_character_internal(int c, bool *is_usually_expanded)
{
	static const cw_prosign_entry_t *lookup[UCHAR_MAX];  /* Fast lookup table */
	static bool is_initialized = false;

	/* If this is the first call, set up the fast lookup table to
	   give direct access to the procedural expansions table for
	   a given character. */
	if (!is_initialized) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_LOOKUPS, CW_DEBUG_INFO,
			      "libcw: initialize prosign fast lookup table");

		for (const cw_prosign_entry_t *e = CW_PROSIGN_TABLE; e->character; e++) {
			lookup[(unsigned char) e->character] = e;

			is_initialized = true;
		}
	}

	/* Lookup the procedural signal table entry.  Unknown characters
	   return NULL.  All procedural signals are non-alphabetical, so no
	   need to use any uppercase coercion here. */
	const cw_prosign_entry_t *cw_prosign = lookup[(unsigned char) c];

	if (cw_debug_has_flag((&cw_debug_object), CW_DEBUG_LOOKUPS)) {
		if (cw_prosign) {
			fprintf(stderr, "libcw: prosign lookup '%c' returned <'%c':\"%s\":%d>\n",
				c, cw_prosign->character,
				cw_prosign->expansion, cw_prosign->is_usually_expanded);
		} else if (isprint(c)) {
			fprintf(stderr, "libcw: prosign lookup '%c' found nothing\n", c);
		} else {
			fprintf(stderr, "libcw: prosign lookup 0x%02x found nothing\n",
				(unsigned char) c);
		}
	}

	/* If found, return any display hint and the expansion; otherwise, NULL. */
	if (cw_prosign) {
		*is_usually_expanded = cw_prosign->is_usually_expanded;
		return cw_prosign->expansion;
	} else {
		return NULL;
	}
}





/**
   \brief Get the string expansion of a given Morse code procedural signal character

   On success the function
   - fills \p expansion with the string expansion of a given Morse code
   procedural signal character \p c;
   - sets is_usuall_expanded to true as a display hint for the caller;
   - returns CW_SUCCESS.

   \p expansion is managed by caller. The length of \p expansion must
   be at least by one greater than the longest expansion held in
   the procedural signal character lookup table, as returned by
   cw_get_maximum_procedural_expansion_length().

   If procedural signal character \p c cannot be found, the function sets
   errno to ENOENT and returns CW_FAILURE.

   \param c - character to look up
   \param expansion - output, space to fill with expansion of the character
   \param is_usually_expanded - visual hint

   \return CW_FAILURE on failure (errno is set to ENOENT)
   \return CW_SUCCESS on success
*/
int cw_lookup_procedural_character(char c, char *expansion, int *is_usually_expanded)
{
	bool is_expanded;

	/* Lookup, and if found, return the string and display hint. */
	const char *retval = cw_lookup_procedural_character_internal(c, &is_expanded);
	if (retval) {
		if (expansion) {
			strcpy(expansion, retval);
		}
		if (is_usually_expanded) {
			*is_usually_expanded = is_expanded;
		}
		return CW_SUCCESS;
	}

	/* Failed to find the requested procedural signal character. */
	errno = ENOENT;
	return CW_FAILURE;
}





/* ******************************************************************** */
/*                     Section:Phonetic alphabet                        */
/* ******************************************************************** */





/* Phonetics table.  Not really CW, but it might be handy to have.
   The table contains ITU/NATO phonetics. */
static const char *const CW_PHONETICS[27] = {
	"Alfa",
	"Bravo",
	"Charlie",
	"Delta",
	"Echo",
	"Foxtrot",
	"Golf",
	"Hotel",
	"India",
	"Juliett",
	"Kilo",
	"Lima",
	"Mike",
	"November",
	"Oscar",
	"Papa",
	"Quebec",
	"Romeo",
	"Sierra",
	"Tango",
	"Uniform",
	"Victor",
	"Whiskey",
	"X-ray",
	"Yankee",
	"Zulu",
	NULL /* guard */
};





/**
   \brief Get maximum length of a phonetic

   \return the string length of the longest phonetic in the phonetics lookup table
 */
int cw_get_maximum_phonetic_length(void)
{
	static size_t maximum_length = 0;

	if (maximum_length == 0) {
		/* Traverse the main lookup table, finding the longest. */
		for (int phonetic = 0; CW_PHONETICS[phonetic]; phonetic++) {
			size_t length = strlen(CW_PHONETICS[phonetic]);
			if (length > maximum_length) {
				maximum_length = length;
			}
		}
	}

	return (int) maximum_length;
}





/**
   \brief Get the phonetic of a given character

   On success the routine fills in the string pointer passed in with the
   phonetic of given character \c.

   The length of phonetic must be at least one greater than the longest
   phonetic held in the phonetic lookup table, as returned by
   cw_get_maximum_phonetic_length().

   If character cannot be found, the function sets errno to ENOENT.

   \param c - character to look up
   \param phonetic - output, space for phonetic of a character

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_lookup_phonetic(char c, char *phonetic)
{
	/* Coerce to uppercase, and verify the input argument. */
	c = toupper(c);
	if (c >= 'A' && c <= 'Z') {
		if (phonetic) {
			strcpy(phonetic, CW_PHONETICS[c - 'A']);
			return CW_SUCCESS;
		}
	}

	/* No such phonetic. */
	errno = ENOENT;
	return CW_FAILURE;
}





/* ******************************************************************** */
/*          Section:Morse code controls and timing parameters           */
/* ******************************************************************** */





/* Dot length magic number; from PARIS calibration, 1 Dot=1200000/WPM usec. */
enum { DOT_CALIBRATION = 1200000 };


/* Default initial values for library controls. */
enum {
	CW_ADAPTIVE_INITIAL  = false,  /* Initial adaptive receive setting */
	CW_INITIAL_THRESHOLD = (DOT_CALIBRATION / CW_SPEED_INITIAL) * 2,   /* Initial adaptive speed threshold */
	CW_INITIAL_NOISE_THRESHOLD = (DOT_CALIBRATION / CW_SPEED_MAX) / 2  /* Initial noise filter threshold */
};



/* Library variables, indicating the user-selected parameters for generating
   Morse code output and receiving Morse code input.  These values can be
   set by client code; setting them may trigger a recalculation of the low
   level timing values held and set below. */
static int cw_receive_speed = CW_SPEED_INITIAL,
	cw_tolerance = CW_TOLERANCE_INITIAL,
	cw_weighting = CW_WEIGHTING_INITIAL,
	cw_noise_spike_threshold = CW_INITIAL_NOISE_THRESHOLD;
static bool cw_is_adaptive_receive_enabled = CW_ADAPTIVE_INITIAL;


/* The following variables must be recalculated each time any of the above
   Morse parameters associated with speeds, gap, tolerance, or threshold
   change.  Keeping these in step means that we then don't have to spend time
   calculating them on the fly.

   Since they have to be kept in sync, the problem of how to have them
   calculated on first call if none of the above parameters has been
   changed is taken care of with a synchronization flag.  Doing this saves
   us from otherwise having to have a "library initialize" function. */
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


/* Library variable which is automatically maintained from the Morse input
   stream, rather than being settable by the user.
   Initially 2-dot threshold for adaptive speed */
static int cw_adaptive_receive_threshold = CW_INITIAL_THRESHOLD;





/**
   \brief Get speed limits

   Get (through function's arguments) limits on speed of morse code that
   can be generated by current generator.

   See CW_SPEED_MIN and CW_SPEED_MAX in libcw.h for values.

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

   See CW_FREQUENCY_MIN and CW_FREQUENCY_MAX in libcw.h for values.

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

   See CW_VOLUME_MIN and CW_VOLUME_MAX in libcw.h for values.

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

   See CW_GAP_MIN and CW_GAP_MAX in libcw.h for values.

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

   Get (through function's arguments) limits on "tolerance" parameter
   of current generator.

   See CW_TOLERANCE_MIN and CW_TOLERANCE_MAX in libcw.h for values.

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

   Get (through function's arguments) limits on "weighting" parameter
   of current generator.

   See CW_WEIGHTING_MIN and CW_WEIGHTING_MAX in libcw.h for values.

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
   \brief Synchronize send/receive parameters of the library

   Synchronize the dot, dash, end of element, end of character, and end
   of word timings and ranges to new values of Morse speed, "Farnsworth"
   gap, receive tolerance, or weighting.

   Part of the parameters is a global variable in the file, and part
   of them is stored in \p gen variable (which, at this moment, is also
   a global variable).

   \param gen - variable storing some of the parameters
*/
void cw_sync_parameters_internal(cw_gen_t *gen)
{
	/* Do nothing if we are already synchronized with speed/gap. */
	if (cw_is_in_sync) {
		return;
	}

	/* Send parameters:

	   Set the length of a Dot to be a Unit with any weighting
	   adjustment, and the length of a Dash as three Dot lengths.
	   The weighting adjustment is by adding or subtracting a
	   length based on 50 % as a neutral weighting. */
	int unit_length = DOT_CALIBRATION / gen->send_speed;
	int weighting_length = (2 * (cw_weighting - 50) * unit_length) / 100;
	cw_send_dot_length = unit_length + weighting_length;
	cw_send_dash_length = 3 * cw_send_dot_length;

	/* An end of element length is one Unit, perhaps adjusted,
	   the end of character is three Units total, and end of
	   word is seven Units total.

	   The end of element length is adjusted by 28/22 times
	   weighting length to keep PARIS calibration correctly
	   timed (PARIS has 22 full units, and 28 empty ones).
	   End of element and end of character delays take
	   weightings into account. */
	cw_end_of_ele_delay = unit_length - (28 * weighting_length) / 22;
	cw_end_of_char_delay = 3 * unit_length - cw_end_of_ele_delay;
	cw_end_of_word_delay = 7 * unit_length - cw_end_of_char_delay;
	cw_additional_delay = gen->gap * unit_length;

	/* For "Farnsworth", there also needs to be an adjustment
	   delay added to the end of words, otherwise the rhythm is
	   lost on word end.
	   I don't know if there is an "official" value for this,
	   but 2.33 or so times the gap is the correctly scaled
	   value, and seems to sound okay.

	   Thanks to Michael D. Ivey <ivey@gweezlebur.com> for
	   identifying this in earlier versions of libcw. */
	cw_adjustment_delay = (7 * cw_additional_delay) / 3;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_PARAMETERS, CW_DEBUG_INFO,
		      "libcw: send usec timings <%d>: %d, %d, %d, %d, %d, %d, %d",
		      gen->send_speed, cw_send_dot_length, cw_send_dash_length,
		      cw_end_of_ele_delay, cw_end_of_char_delay,
		      cw_end_of_word_delay, cw_additional_delay, cw_adjustment_delay);


	/* Receive parameters:

	   First, depending on whether we are set for fixed speed or
	   adaptive speed, calculate either the threshold from the
	   receive speed, or the receive speed from the threshold,
	   knowing that the threshold is always, effectively, two dot
	   lengths.  Weighting is ignored for receive parameters,
	   although the core unit length is recalculated for the
	   receive speed, which may differ from the send speed. */
	unit_length = DOT_CALIBRATION / cw_receive_speed;
	if (cw_is_adaptive_receive_enabled) {
		cw_receive_speed = DOT_CALIBRATION
			/ (cw_adaptive_receive_threshold / 2);
	} else {
		cw_adaptive_receive_threshold = 2 * unit_length;
	}

	/* Calculate the basic receive dot and dash lengths. */
	cw_receive_dot_length = unit_length;
	cw_receive_dash_length = 3 * unit_length;

	/* Set the ranges of respectable timing elements depending
	   very much on whether we are required to adapt to the
	   incoming Morse code speeds. */
	if (cw_is_adaptive_receive_enabled) {
		/* For adaptive timing, calculate the Dot and
		   Dash timing ranges as zero to two Dots is a
		   Dot, and anything, anything at all, larger than
		   this is a Dash. */
		cw_dot_range_minimum = 0;
		cw_dot_range_maximum = 2 * cw_receive_dot_length;
		cw_dash_range_minimum = cw_dot_range_maximum;
		cw_dash_range_maximum = INT_MAX;

		/* Make the inter-element gap be anything up to
		   the adaptive threshold lengths - that is two
		   Dots.  And the end of character gap is anything
		   longer than that, and shorter than five dots. */
		cw_eoe_range_minimum = cw_dot_range_minimum;
		cw_eoe_range_maximum = cw_dot_range_maximum;
		cw_eoc_range_minimum = cw_eoe_range_maximum;
		cw_eoc_range_maximum = 5 * cw_receive_dot_length;

	} else {
		/* For fixed speed receiving, calculate the Dot
		   timing range as the Dot length +/- dot*tolerance%,
		   and the Dash timing range as the Dash length
		   including +/- dot*tolerance% as well. */
		int tolerance = (cw_receive_dot_length * cw_tolerance) / 100;
		cw_dot_range_minimum = cw_receive_dot_length - tolerance;
		cw_dot_range_maximum = cw_receive_dot_length + tolerance;
		cw_dash_range_minimum = cw_receive_dash_length - tolerance;
		cw_dash_range_maximum = cw_receive_dash_length + tolerance;

		/* Make the inter-element gap the same as the Dot
		   range.  Make the inter-character gap, expected
		   to be three Dots, the same as Dash range at the
		   lower end, but make it the same as the Dash range
		   _plus_ the "Farnsworth" delay at the top of the
		   range.

		   Any gap longer than this is by implication
		   inter-word. */
		cw_eoe_range_minimum = cw_dot_range_minimum;
		cw_eoe_range_maximum = cw_dot_range_maximum;
		cw_eoc_range_minimum = cw_dash_range_minimum;
		cw_eoc_range_maximum = cw_dash_range_maximum
			+ cw_additional_delay + cw_adjustment_delay;
	}

	/* For statistical purposes, calculate the ideal end of
	   element and end of character timings. */
	cw_eoe_range_ideal = unit_length;
	cw_eoc_range_ideal = 3 * unit_length;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_PARAMETERS, CW_DEBUG_INFO,
		      "libcw: receive usec timings <%d>: %d-%d, %d-%d, %d-%d[%d], %d-%d[%d], %d",
		      cw_receive_speed,
		      cw_dot_range_minimum, cw_dot_range_maximum,
		      cw_dash_range_minimum, cw_dash_range_maximum,
		      cw_eoe_range_minimum, cw_eoe_range_maximum, cw_eoe_range_ideal,
		      cw_eoc_range_minimum, cw_eoc_range_maximum, cw_eoc_range_ideal,
		      cw_adaptive_receive_threshold);

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
	generator->send_speed = CW_SPEED_INITIAL;
	generator->frequency = CW_FREQUENCY_INITIAL;
	generator->volume_percent = CW_VOLUME_INITIAL;
	generator->volume_abs = (generator->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
	generator->gap = CW_GAP_INITIAL;

	cw_receive_speed = CW_SPEED_INITIAL;
	cw_tolerance = CW_TOLERANCE_INITIAL;
	cw_weighting = CW_WEIGHTING_INITIAL;
	cw_is_adaptive_receive_enabled = CW_ADAPTIVE_INITIAL;
	cw_noise_spike_threshold = CW_INITIAL_NOISE_THRESHOLD;

	/* Changes require resynchronization. */
	cw_is_in_sync = false;
	cw_sync_parameters_internal (generator);

	return;
}





/**
   \brief Set sending speed

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal value
   of send speed.

   errno is set to EINVAL if \p new_value is out of range.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_send_speed(int new_value)
{
	if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != generator->send_speed) {
		generator->send_speed = new_value;

		/* Changes of send speed require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(generator);
	}

	return CW_SUCCESS;
}





/**
   \brief Set receiving speed

   See documentation of cw_set_send_speed() for more information.

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of receive speed.
   errno is set to EINVAL if \p new_value is out of range.
   errno is set to EPERM if adaptive receive speed tracking is enabled.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_receive_speed(int new_value)
{
	if (cw_is_adaptive_receive_enabled) {
		errno = EPERM;
		return CW_FAILURE;
	} else {
		if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX) {
			errno = EINVAL;
			return CW_FAILURE;
		}
	}

	if (new_value != cw_receive_speed) {
		cw_receive_speed = new_value;

		/* Changes of receive speed require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(generator);
	}

	return CW_SUCCESS;
}




/**
   \brief Set frequency of current generator

   Set frequency of sound wave generated by current generator.
   The frequency must be within limits marked by CW_FREQUENCY_MIN
   and CW_FREQUENCY_MAX.

   See libcw.h/CW_FREQUENCY_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of frequency.

   errno is set to EINVAL if \p new_value is out of range.

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

   Note that volume settings are not fully possible for the console speaker.
   In this case, volume settings greater than zero indicate console speaker
   sound is on, and setting volume to zero will turn off console speaker
   sound.

   See libcw.h/CW_VOLUME_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of volume.
   errno is set to EINVAL if \p new_value is out of range.

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
		generator->volume_percent = new_value;
		generator->volume_abs = (generator->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;

		cw_generator_set_tone_slope(generator, -1, -1);

		return CW_SUCCESS;
	}
}





/**
   \brief Set sending gap

   See libcw.h/CW_GAP_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of gap.
   errno is set to EINVAL if \p new_value is out of range.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_gap(int new_value)
{
	if (new_value < CW_GAP_MIN || new_value > CW_GAP_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != generator->gap) {
		generator->gap = new_value;

		/* Changes of gap require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(generator);
	}

	return CW_SUCCESS;
}





/**
   \brief Set tolerance

   See libcw.h/CW_TOLERANCE_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of tolerance.
   errno is set to EINVAL if \p new_value is out of range.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_tolerance(int new_value)
{
	if (new_value < CW_TOLERANCE_MIN || new_value > CW_TOLERANCE_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != cw_tolerance) {
		cw_tolerance = new_value;

		/* Changes of tolerance require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(generator);
	}

	return CW_SUCCESS;
}





/**
   \brief Set sending weighting

   See libcw.h/CW_WEIGHTING_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of weighting.
   errno is set to EINVAL if \p new_value is out of range.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_weighting(int new_value)
{
	if (new_value < CW_WEIGHTING_MIN || new_value > CW_WEIGHTING_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != cw_weighting) {
		cw_weighting = new_value;

		/* Changes of weighting require resynchronization. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(generator);
	}

	return CW_SUCCESS;
}





/**
   \brief Get sending speed

   \return current value of the parameter
*/
int cw_get_send_speed(void)
{
	return generator->send_speed;
}





/**
   \brief Get receiving speed

   \return current value of the parameter
*/
int cw_get_receive_speed(void)
{
	return cw_receive_speed;
}





/**
   \brief Get frequency of current generator

   Function returns "frequency" parameter of current generator,
   even if the generator is stopped, or volume of generated sound is zero.

   \return Frequency of current generator
*/
int cw_get_frequency(void)
{
	return generator->frequency;
}





/**
   \brief Get volume of current generator

   Function returns "volume" parameter of current generator,
   even if the generator is stopped.

   \return Volume of current generator
*/
int cw_get_volume(void)
{
	return generator->volume_percent;
}





/**
   \brief Get sending gap

   \return current value of the parameter
*/
int cw_get_gap(void)
{
	return generator->gap;
}





/**
   \brief Get tolerance

   \return current value of the parameter
*/
int cw_get_tolerance(void)
{
	return cw_tolerance;
}





/**
   \brief Get sending weighting

   \return current value of the parameter
*/
int cw_get_weighting(void)
{
	return cw_weighting;
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
	cw_sync_parameters_internal(generator);

	if (dot_usecs)   *dot_usecs = cw_send_dot_length;
	if (dash_usecs)  *dash_usecs = cw_send_dash_length;

	if (end_of_element_usecs)    *end_of_element_usecs = cw_end_of_ele_delay;
	if (end_of_character_usecs)  *end_of_character_usecs = cw_end_of_char_delay;
	if (end_of_word_usecs)       *end_of_word_usecs = cw_end_of_word_delay;

	if (additional_usecs)    *additional_usecs = cw_additional_delay;
	if (adjustment_usecs)    *adjustment_usecs = cw_adjustment_delay;

	return;
}





/**
   \brief Get timing parameters for sending, and adaptive threshold

   Return the low-level timing parameters calculated from the speed, gap,
   tolerance, and weighting set.  Parameter values are returned in
   microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   \param dot_usecs
   \param dash_usecs
   \param dot_min_usecs
   \param dot_max_usecs
   \param dash_min_usecs
   \param dash_max_usecs
   \param end_of_element_min_usecs
   \param end_of_element_max_usecs
   \param end_of_element_ideal_usecs
   \param end_of_character_min_usecs
   \param end_of_character_max_usecs
   \param end_of_character_ideal_usecs
   \param adaptive_threshold
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
	cw_sync_parameters_internal(generator);

	if (dot_usecs)      *dot_usecs = cw_receive_dot_length;
	if (dash_usecs)     *dash_usecs = cw_receive_dash_length;
	if (dot_min_usecs)  *dot_min_usecs = cw_dot_range_minimum;
	if (dot_max_usecs)  *dot_max_usecs = cw_dot_range_maximum;
	if (dash_min_usecs) *dash_min_usecs = cw_dash_range_minimum;
	if (dash_max_usecs) *dash_max_usecs = cw_dash_range_maximum;

	if (end_of_element_min_usecs)     *end_of_element_min_usecs = cw_eoe_range_minimum;
	if (end_of_element_max_usecs)     *end_of_element_max_usecs = cw_eoe_range_maximum;
	if (end_of_element_ideal_usecs)   *end_of_element_ideal_usecs = cw_eoe_range_ideal;
	if (end_of_character_min_usecs)   *end_of_character_min_usecs = cw_eoc_range_minimum;
	if (end_of_character_max_usecs)   *end_of_character_max_usecs = cw_eoc_range_maximum;
	if (end_of_character_ideal_usecs) *end_of_character_ideal_usecs = cw_eoc_range_ideal;

	if (adaptive_threshold) *adaptive_threshold = cw_adaptive_receive_threshold;

	return;
}





/**
   \brief Set noise spike threshold

   Set the period shorter than which, on receive, received tones are ignored.
   This allows the receive tone functions to apply noise canceling for very
   short apparent tones.
   For useful results the value should never exceed the dot length of a dot at
   maximum speed: 20,000 microseconds (the dot length at 60WPM).
   Setting a noise threshold of zero turns off receive tone noise canceling.

   The default noise spike threshold is 10,000 microseconds.

   errno is set to EINVAL if \p new_value is out of range.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_noise_spike_threshold(int new_value)
{
	if (new_value < 0) {
		errno = EINVAL;
		return CW_FAILURE;
	}
	cw_noise_spike_threshold = new_value;

	return CW_SUCCESS;
}





/**
   \brief Get noise spike threshold

   See documentation of cw_set_noise_spike_threshold() for more information

   \return current value of the parameter
*/
int cw_get_noise_spike_threshold(void)
{
	return cw_noise_spike_threshold;
}





/* ******************************************************************** */
/*                 Section:SIGALRM and timer handling                   */
/* ******************************************************************** */





/* Microseconds in a second, for struct timeval handling. */
static const int USECS_PER_SEC = 1000000;


/* The library keeps a single central non-sparse list of SIGALRM signal
   handlers. The handler functions will be called sequentially on each
   SIGALRM received. */
enum { CW_SIGALRM_HANDLERS_MAX = 32 };
static void (*cw_sigalrm_handlers[CW_SIGALRM_HANDLERS_MAX])(void);


/*
 * Flag to tell us if the SIGALRM handler is installed, and a place to keep
 * the old SIGALRM disposition, so we can restore it when the library decides
 * it can stop handling SIGALRM for a while.
 */
static bool cw_is_sigalrm_handlers_caller_installed = false;
static struct sigaction cw_sigalrm_original_disposition;





/**
   \brief Call handlers of SIGALRM signal

   This function calls the SIGALRM signal handlers of the library
   subsystems, expecting them to ignore unexpected calls.

   The handlers are kept in cw_sigalrm_handlers[] table, and can be added
   to the table with cw_timer_run_with_handler_internal().

   SIGALRM is sent to a process every time an itimer timer expires.
   The timer is set with cw_timer_run_internal().
*/
void cw_sigalrm_handlers_caller_internal(__attribute__((unused)) int signal_number)
{
	/* Call the known functions that are interested in SIGALRM signal.
	   Stop on the first free slot found; valid because the array is
	   filled in order from index 0, and there are no deletions. */
	for (int handler = 0;
	     handler < CW_SIGALRM_HANDLERS_MAX && cw_sigalrm_handlers[handler]; handler++) {

		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_INTERNAL, CW_DEBUG_DEBUG,
			      "libcw: SIGALRM handler #%d", handler);

		(cw_sigalrm_handlers[handler])();
	}

	return;
}





/**
   \brief Set up a timer for specified number of microseconds

   Convenience function to set the itimer for a single shot timeout after
   a given number of microseconds. SIGALRM is sent to caller process when the
   timer expires.

   \param usecs - time in microseconds

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_timer_run_internal(int usecs)
{
	struct itimerval itimer;

	/* Set up a single shot timeout for the given period. */
	itimer.it_interval.tv_sec = 0;
	itimer.it_interval.tv_usec = 0;
	itimer.it_value.tv_sec = usecs / USECS_PER_SEC;
	itimer.it_value.tv_usec = usecs % USECS_PER_SEC;
	int status = setitimer(ITIMER_REAL, &itimer, NULL);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: setitimer(%d): %s", usecs, strerror(errno));
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/**
   \brief Register SIGALRM handler(s), and send SIGALRM signal

   Install top level handler of SIGALRM signal (cw_sigalrm_handlers_caller_internal())
   if it is not already installed.

   Register given \p sigalrm_handler lower level handler, if not NULL and
   if not yet registered.
   Then send SIGALRM signal after delay equal to \p usecs microseconds.

   \param usecs - time for itimer
   \param sigalrm_handler - SIGALRM handler to register

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_timer_run_with_handler_internal(int usecs, void (*sigalrm_handler)(void))
{
	if (!cw_sigalrm_install_top_level_handler_internal()) {
		return CW_FAILURE;
	}

	/* If it's not already present, and one was given, add address
	   of the lower level SIGALRM handler to the list of known
	   handlers. */
	if (sigalrm_handler) {
		int handler;

		/* Search for this handler, or the first free entry,
		   stopping at the last entry in the table even if it's
		   not a match and not free. */
		for (handler = 0; handler < CW_SIGALRM_HANDLERS_MAX - 1; handler++) {
			if (!cw_sigalrm_handlers[handler]
			    || cw_sigalrm_handlers[handler] == sigalrm_handler) {

				break;
			}
		}

		/* If the handler is already there, do no more.  Otherwise,
		   if we ended the search at an unused entry, add it to
		   the list of lower level handlers. */
		if (cw_sigalrm_handlers[handler] != sigalrm_handler) {
			if (cw_sigalrm_handlers[handler]) {
				errno = ENOMEM;
				cw_debug_msg ((&cw_debug_object), CW_DEBUG_INTERNAL, CW_DEBUG_ERROR,
					      "libcw: overflow cw_sigalrm_handlers");
				return CW_FAILURE;
			} else {
				cw_sigalrm_handlers[handler] = sigalrm_handler;
			}
		}
	}

	/* The fact that we receive a call means that something is using
	   timeouts and sound, so make sure that any pending finalization
	   doesn't happen. */
	cw_finalization_cancel_internal();

	/* Depending on the value of usec, either set an itimer, or send
	   ourselves SIGALRM right away. */
	if (usecs <= 0) {
		/* Send ourselves SIGALRM immediately. */
#if 1
		if (pthread_kill(generator->thread.id, SIGALRM) != 0) {
#else
		if (raise(SIGALRM) != 0) {
#endif
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: raise()");
			return CW_FAILURE;
		}
	} else {
		/* Set the itimer to produce a single interrupt after the
		   given duration. */
		if (!cw_timer_run_internal(usecs)) {
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}





int cw_sigalrm_install_top_level_handler_internal(void)
{
	if (!cw_is_sigalrm_handlers_caller_installed) {
		/* Install the main SIGALRM handler routine (a.k.a. top level
		   SIGALRM handler) - a function that calls all registered
		   lower level handlers), and keep the  old information
		   (disposition) so we can put it back when useful to do so. */

		struct sigaction action;
		action.sa_handler = cw_sigalrm_handlers_caller_internal;
		action.sa_flags = SA_RESTART;
		sigemptyset(&action.sa_mask);

		int status = sigaction(SIGALRM, &action, &cw_sigalrm_original_disposition);
		if (status == -1) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: sigaction(): %s", strerror(errno));
			return CW_FAILURE;
		}

		cw_is_sigalrm_handlers_caller_installed = true;
	}
	return CW_SUCCESS;
}





/**
   \brief Uninstall the SIGALRM handler, if installed

   Restores SIGALRM's disposition for the system to the state we found
   it in before we installed our own SIGALRM handler.

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_sigalrm_restore_internal(void)
{
	/* Ignore the call if we haven't installed our handler. */
	if (cw_is_sigalrm_handlers_caller_installed) {
		/* Cancel any pending itimer setting. */
		if (!cw_timer_run_internal(0)) {
			return CW_FAILURE;
		}

		/* Put back the SIGALRM information saved earlier. */
		int status = sigaction(SIGALRM, &cw_sigalrm_original_disposition, NULL);
		if (status == -1) {
			perror ("libcw: sigaction");
			return CW_FAILURE;
		}

		cw_is_sigalrm_handlers_caller_installed = false;
	}

	return CW_SUCCESS;
}





/**
   \brief Check if SIGALRM is currently blocked

   Check the signal mask of the process, and return false, with errno
   set to EDEADLK, if SIGALRM is blocked.
   If function returns true, but errno is set, the function has failed
   to check if SIGALRM is blocked.

   \return true if SIGALRM is currently blocked (errno is zero)
   \return true on errors (errno is set by system call that failed)
   \return false if SIGALRM is currently not blocked
*/
bool cw_sigalrm_is_blocked_internal(void)
{
	sigset_t empty_set, current_set;

	/* Prepare empty set of signals */
	int status = sigemptyset(&empty_set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigemptyset(): %s", strerror(errno));
		return true;
	}

	/* Block an empty set of signals to obtain the current mask. */
	status = sigprocmask(SIG_BLOCK, &empty_set, &current_set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigprocmask(): %s", strerror(errno));
		return true;
	}

	/* Check that SIGALRM is not blocked in the current mask. */
	if (sigismember(&current_set, SIGALRM)) {
		errno = 0;
		return true;
	} else {
		return false;
	}
}





/**
   \brief Block or unblock SIGALRM signal

   Function blocks or unblocks SIGALRM.
   It may be used to block the signal for the duration of certain
   critical sections, and to unblock the signal afterwards.

   \param block - pass true to block SIGALRM, and false to unblock it

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_sigalrm_block_internal(bool block)
{
	sigset_t set;

	/* Prepare empty set of signals */
	int status = sigemptyset(&set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigemptyset(): %s", strerror(errno));
		return CW_FAILURE;
	}

	/* Add single signal to the set */
	status = sigaddset(&set, SIGALRM);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigaddset(): %s", strerror(errno));
		return CW_FAILURE;
	}

	/* Block or unblock SIGALRM for the process using the set of signals */
	status = pthread_sigmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: pthread_sigmask(): %s", strerror(errno));
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/**
   \brief Block the callback from being called

   Function blocks the callback from being called for a critical section of
   caller code if \p block is true, and unblocks the callback if \p block is
   false.

   Function works by blocking SIGALRM; a block should always be matched by
   an unblock, otherwise the tone queue will suspend forever.

   \param block - pass 1 to block SIGALRM, and 0 to unblock it
*/
void cw_block_callback(int block)
{
	cw_sigalrm_block_internal((bool) block);
	return;
}





/**
   \brief Wait for a signal, usually a SIGALRM

   Function assumes that SIGALRM is not blocked.
   Function may return CW_FAILURE on failure, i.e. when call to
   sigemptyset(), sigprocmask() or sigsuspend() fails.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_signal_wait_internal(void)
{
	sigset_t empty_set, current_set;

	/* Prepare empty set of signals */
	int status = sigemptyset(&empty_set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigemptyset(): %s", strerror(errno));
		return CW_FAILURE;
	}

	/* Block an empty set of signals to obtain the current mask. */
	status = sigprocmask(SIG_BLOCK, &empty_set, &current_set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigprocmask(): %s", strerror(errno));
		return CW_FAILURE;
	}

	/* Wait on the current mask */
	status = sigsuspend(&current_set);
	if (status == -1 && errno != EINTR) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: suspend(): %s", strerror(errno));
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/* Array of callbacks registered for convenience signal handling.  They're
   initialized dynamically to SIG_DFL (if SIG_DFL is not NULL, which it
   seems that it is in most cases). */
static void (*cw_signal_callbacks[CW_SIG_MAX])(int);





/**
   \brief Generic function calling signal handlers

   Signal handler function registered when cw_register_signal_handler()
   is called.
   The function resets the library (with cw_complete_reset()), and then,
   depending on value of signal handler for given \p signal_number:
   \li calls exit(EXIT_FAILURE) if signal handler is SIG_DFL, or
   \li continues without further actions if signal handler is SIG_IGN, or
   \li calls the signal handler.

   The signal handler for given \p signal_number is either a pre-set, default
   value, or is a value registered earlier with cw_register_signal_handler().

   \param signal_number
*/
void cw_signal_main_handler_internal(int signal_number)
{
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
		      "libcw: caught signal %d", signal_number);

	/* Reset the library and retrieve the signal's handler. */
	cw_complete_reset();
	void (*callback_func)(int) = cw_signal_callbacks[signal_number];

	/* The default action is to stop the process; exit(1) seems to cover it. */
	if (callback_func == SIG_DFL) {
		exit(EXIT_FAILURE);
	} else if (callback_func == SIG_IGN) {
		/* continue */
	} else {
		/* invoke any additional handler callback function */
		(*callback_func)(signal_number);
	}

	return;
}





/**
   \brief Register a signal handler and optional callback function for given signal number

   On receipt of that signal, all library features will be reset to their
   default states.  Following the reset, if \p callback_func is a function
   pointer, the function is called; if it is SIG_DFL, the library calls
   exit(); and if it is SIG_IGN, the library returns from the signal handler.

   This is a convenience function for clients that need to clean up library
   on signals, with either exit, continue, or an additional function call;
   in effect, a wrapper round a restricted form of sigaction.

   The \p signal_number argument indicates which signal to catch.

   On problems errno is set to EINVAL if \p signal_number is invalid
   or if a handler is already installed for that signal, or to the
   sigaction error code.

   \return CW_SUCCESS - if the signal handler installs correctly
   \return CW_FAILURE - on errors or problems
*/
int cw_register_signal_handler(int signal_number, void (*callback_func)(int))
{
	static bool is_initialized = false;

	/* On first call, initialize all signal_callbacks to SIG_DFL. */
	if (!is_initialized) {
		for (int i = 0; i < CW_SIG_MAX; i++) {
			cw_signal_callbacks[i] = SIG_DFL;
		}
		is_initialized = true;
	}

	/* Reject invalid signal numbers, and SIGALRM, which we use internally. */
	if (signal_number < 0
	    || signal_number >= CW_SIG_MAX
	    || signal_number == SIGALRM) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Install our handler as the actual handler. */
	struct sigaction action, original_disposition;
	action.sa_handler = cw_signal_main_handler_internal;
	action.sa_flags = SA_RESTART;
	sigemptyset(&action.sa_mask);
	int status = sigaction(signal_number, &action, &original_disposition);
	if (status == -1) {
		perror("libcw: sigaction");
		return CW_FAILURE;
	}

	/* If we trampled another handler, replace it and return false. */
	if (!(original_disposition.sa_handler == cw_signal_main_handler_internal
	      || original_disposition.sa_handler == SIG_DFL
	      || original_disposition.sa_handler == SIG_IGN)) {

		status = sigaction(signal_number, &original_disposition, NULL);
		if (status == -1) {
			perror("libcw: sigaction");
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
   \brief Unregister a signal handler interception

   Function removes a signal handler interception previously registered
   with cw_register_signal_handler().

   \return true if the signal handler uninstalls correctly
   \return false otherwise (with errno set to EINVAL or to the sigaction error code)
*/
int cw_unregister_signal_handler(int signal_number)
{
	/* Reject unacceptable signal numbers. */
	if (signal_number < 0
	    || signal_number >= CW_SIG_MAX
	    || signal_number == SIGALRM) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	/* See if the current handler was put there by us. */
	struct sigaction original_disposition;
	int status = sigaction(signal_number, NULL, &original_disposition);
	if (status == -1) {
		perror("libcw: sigaction");
		return CW_FAILURE;
	}

	if (original_disposition.sa_handler != cw_signal_main_handler_internal) {
		/* No, it's not our signal handler. Don't touch it. */
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Remove the signal handler by resetting to SIG_DFL. */
	struct sigaction action;
	action.sa_handler = SIG_DFL;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	status = sigaction(signal_number, &action, NULL);
	if (status == -1) {
		perror("libcw: sigaction");
		return CW_FAILURE;
	}

	/* Reset the callback entry for tidiness. */
	cw_signal_callbacks[signal_number] = SIG_DFL;

	return CW_SUCCESS;
}





/* ******************************************************************** */
/*      Section:General control of console buzzer and of soundcard      */
/* ******************************************************************** */





/**
   \brief Set audio device name or path

   Set path to audio device, or name of audio device. The path/name
   will be associated with given generator \p gen, and used when opening
   audio device.

   Use this function only when setting up a generator.

   Function creates its own copy of input string.

   \param gen - current generator
   \param device - device to be associated with generator

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

   \return char string with current soundcard device name or device path
*/
const char *cw_get_soundcard_device(void)
{
	return generator->audio_device;
}





/**
   \brief Stop and delete generator

   Stop and delete current generator.
   This causes silencing current sound wave.

   \return CW_SUCCESS
*/
int cw_generator_release_internal(void)
{
	cw_generator_stop();
	cw_generator_delete();

	return CW_SUCCESS;
}





/**
   \brief Silence the generator

   Force the generator \p to go silent.
   Function stops the generator as well, but does not flush its queue.

   \param gen - current generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on errors
*/
int cw_generator_silence_internal(cw_gen_t *gen)
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





/* ******************************************************************** */
/*                 Section:Finalization and cleanup                     */
/* ******************************************************************** */





/* We prefer to close the soundcard after a period of library inactivity,
   so that other applications can use it.  Ten seconds seems about right.
   We do it in one-second timeouts so that any leaked pending timeouts from
   other facilities don't cause premature finalization. */
static const int CW_AUDIO_FINALIZATION_DELAY = 10000000;

 /* Counter counting down the number of clock calls before we finalize. */
static volatile bool cw_is_finalization_pending = false;
static volatile int cw_finalization_countdown = 0;

/* Use a mutex to suppress delayed finalizations on complete resets. */
static volatile bool cw_is_finalization_locked_out = false;





/**
   \brief Tick a finalization clock

   If finalization is pending, decrement the countdown, and if this reaches
   zero, we've waited long enough to release sound and timeouts.
*/
void cw_finalization_clock_internal(void)
{
	if (cw_is_finalization_pending) {
		/* Decrement the timeout countdown, and finalize if we reach zero. */
		cw_finalization_countdown--;
		if (cw_finalization_countdown <= 0) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
				      "libcw: finalization timeout, closing down");

			cw_sigalrm_restore_internal();
			// cw_generator_release_internal ();

			cw_is_finalization_pending = false;
			cw_finalization_countdown = 0;
		} else {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
				      "libcw: finalization countdown %d", cw_finalization_countdown);

			/* Request another timeout.  This results in a call to our
			   cw_finalization_cancel_internal below; to ensure that it doesn't
			   really cancel finalization, unset the pending flag, then set it
			   back again after reqesting the timeout. */
			cw_is_finalization_pending = false;
			cw_timer_run_with_handler_internal(USECS_PER_SEC, NULL);
			cw_is_finalization_pending = true;
		}
	}

	return;
}





/**
  Set the finalization pending flag, and request a timeout to call the
  finalization function after a delay of a few seconds.
*/
void cw_finalization_schedule_internal(void)
{
	if (!cw_is_finalization_locked_out && !cw_is_finalization_pending) {
		cw_timer_run_with_handler_internal(USECS_PER_SEC,
						   cw_finalization_clock_internal);

		/* Set the flag and countdown last; calling cw_timer_run_with_handler()
		 * above results in a call to our cw_finalization_cancel_internal(),
		 which clears the flag and countdown if we set them early. */
		cw_is_finalization_pending = true;
		cw_finalization_countdown = CW_AUDIO_FINALIZATION_DELAY / USECS_PER_SEC;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
			      "libcw: finalization scheduled");
	}

	return;
}





/**
   Cancel any pending finalization on noting other library activity,
   indicated by a call from the timeout request function telling us
   that it is setting a timeout.
*/
void cw_finalization_cancel_internal(void)
{
	if (cw_is_finalization_pending)  {
		/* Cancel pending finalization and return to doing nothing. */
		cw_is_finalization_pending = false;
		cw_finalization_countdown = 0;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
			      "libcw: finalization canceled");
	}

	return;
}





/**
   \brief Reset all library features to their default states

   Clears the tone queue, receive buffers and retained state information,
   any current keyer activity, and any straight key activity, returns to
   silence, and closes soundcard and console devices.  This function is
   suitable for calling from an application exit handler.
*/
void cw_complete_reset(void)
{
	/* If the finalizer thinks it's pending, stop it, then temporarily
	   lock out finalizations. */
	cw_finalization_cancel_internal();
	cw_is_finalization_locked_out = true;

	/* Silence sound, and shutdown use of the sound devices. */
	//cw_sound_soundcard_internal (0);
	cw_generator_release_internal ();
	cw_sigalrm_restore_internal ();

	/* Call the reset functions for each subsystem. */
	cw_reset_tone_queue ();
	cw_reset_receive ();
	cw_reset_keyer ();
	cw_reset_straight_key ();

	/* Now we can re-enable delayed finalizations. */
	cw_is_finalization_locked_out = false;

	return;
}





/* ******************************************************************** */
/*                       Section:Keying control                         */
/* ******************************************************************** */





/* External "on key state change" callback function and its argument.

   It may be useful for a client to have this library control an external
   keying device, for example, an oscillator, or a transmitter.
   Here is where we keep the address of a function that is passed to us
   for this purpose, and a void* argument for it. */
static void (*cw_kk_key_callback)(void*, int) = NULL;
static void *cw_kk_key_callback_arg = NULL;





/**
   \brief Register external callback function for keying

   Register a \p callback_func function that should be called when a state
   of a key changes from "key open" to "key closed", or vice-versa.

   The first argument passed to the registered callback function is the
   supplied \p callback_arg, if any.  The second argument passed to
   registered callback function is the key state: CW_KEY_STATE_CLOSED
   (one/true) for "key closed", and CW_KEY_STATE_OPEN (zero/false) for
   "key open".

   Calling this routine with a NULL function address disables keying
   callbacks.  Any callback supplied will be called in signal handler
   context (??).

   \param callback_func - callback function to be called on key state changes
   \param callback_arg - first argument to callback_func
*/
void cw_register_keying_callback(void (*callback_func)(void*, int),
				 void *callback_arg)
{
	cw_kk_key_callback = callback_func;
	cw_kk_key_callback_arg = callback_arg;

	return;
}





/**
   \brief Call external callback function for keying

   Control function that calls any requested keying callback only when there
   is a change of keying state.  This function filters successive key-down
   or key-up actions into a single action.

   \param requested_key_state - current key state to be stored
*/
void cw_key_set_state_internal(int requested_key_state)
{
	static int current_key_state = CW_KEY_STATE_OPEN;  /* Maintained key control state */

	if (current_key_state != requested_key_state) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw: keying state %d->%d", current_key_state, requested_key_state);

		/* Set the new keying state, and call any requested callback. */
		current_key_state = requested_key_state;
		if (cw_kk_key_callback) {
			(*cw_kk_key_callback)(cw_kk_key_callback_arg, current_key_state);
		}
	}

	return;
}





/**
   \brief Call external callback function for keying

   Control function that calls any requested keying callback only when there
   is a change of keying state.  This function filters successive key-down
   or key-up actions into a single action.

   \param requested_key_state - current key state to be stored
*/
void cw_key_straight_key_generate_internal(cw_gen_t *gen, int requested_key_state)
{
	static int current_key_state = CW_KEY_STATE_OPEN;  /* Maintained key control state */

	if (current_key_state != requested_key_state) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw: straight key: keying state %d->%d", current_key_state, requested_key_state);

		/* Set the new keying state, and call any requested callback. */
		current_key_state = requested_key_state;
		if (cw_kk_key_callback) {
			(*cw_kk_key_callback)(cw_kk_key_callback_arg, current_key_state);
		}

		if (current_key_state == CW_KEY_STATE_CLOSED) {
			cw_tone_t tone;
			tone.usecs = gen->tone_slope.length_usecs;
			tone.frequency = gen->frequency;
			tone.slope_mode = CW_SLOPE_MODE_RISING_SLOPE;
			cw_tone_queue_enqueue_internal(gen->tq, &tone);

			tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
			tone.usecs = CW_AUDIO_FOREVER_USECS;
			tone.frequency = gen->frequency;
			cw_tone_queue_enqueue_internal(gen->tq, &tone);

			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
				      "libcw: tone queue: len = %ud", cw_tone_queue_length_internal(gen->tq));
		} else {
			cw_tone_t tone;
			tone.usecs = gen->tone_slope.length_usecs;
			tone.frequency = gen->frequency;
			tone.slope_mode = CW_SLOPE_MODE_FALLING_SLOPE;
			cw_tone_queue_enqueue_internal(gen->tq, &tone);

			if (gen->audio_system == CW_AUDIO_CONSOLE) {
				/* Play just a bit of silence, just to switch
				   buzzer from playing a sound to being silent. */
				tone.usecs = CW_AUDIO_QUANTUM_USECS;
				tone.frequency = 0;
				tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
				cw_tone_queue_enqueue_internal(gen->tq, &tone);
			} else {
				/* On some occasions, on some platforms, some
				   sound systems may need to constantly play
				   "silent" tone. These four lines of code are
				   just for them.

				   It would be better to avoid queueing silent
				   "forever" tone because this increases CPU
				   usage. It would be better to simply not to
				   queue any new tones after "falling slope"
				   tone. Silence after the last falling slope
				   would simply last on itself until there is
				   new tone on queue to play. */
				tone.usecs = CW_AUDIO_FOREVER_USECS;
				tone.frequency = 0;
				tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
				cw_tone_queue_enqueue_internal(gen->tq, &tone);
			}
		}
	}

	return;
}





/**
   \brief Call external callback function for keying

   Control function that calls any requested keying callback only when there
   is a change of keying state.  This function filters successive key-down
   or key-up actions into a single action.

   \param requested_key_state - current key state to be stored
*/
void cw_key_iambic_keyer_generate_internal(cw_gen_t *gen, int requested_key_state, int usecs)
{
	static int current_key_state = CW_KEY_STATE_OPEN;  /* Maintained key control state */

	if (current_key_state != requested_key_state) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYING, CW_DEBUG_INFO,
			      "libcw: iambic keyer: keying state %d->%d", current_key_state, requested_key_state);

		/* Set the new keying state, and call any requested callback. */
		current_key_state = requested_key_state;
		if (cw_kk_key_callback) {
			(*cw_kk_key_callback)(cw_kk_key_callback_arg, current_key_state);
		}

		cw_tone_t tone;
		if (current_key_state == CW_KEY_STATE_CLOSED) {
			tone.slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES;
			tone.usecs = usecs;
			tone.frequency = gen->frequency;
			cw_tone_queue_enqueue_internal(gen->tq, &tone);
		} else {
			tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
			tone.usecs = usecs;
			tone.frequency = 0;
			cw_tone_queue_enqueue_internal(gen->tq, &tone);
		}
	}

	return;
}





/* ******************************************************************** */
/*                         Section:Tone queue                           */
/* ******************************************************************** */





enum cw_queue_state {
	QS_IDLE,
	QS_BUSY
};



enum {
	CW_TONE_QUEUE_CAPACITY = 3000,        /* ~= 5 minutes at 12 WPM */
	CW_TONE_QUEUE_HIGH_WATER_MARK = 2900  /* Refuse characters if <100 free */
};



struct cw_tone_queue_struct {
	volatile cw_tone_t queue[CW_TONE_QUEUE_CAPACITY];
	volatile int tail;  /* Tone queue tail index */
	volatile int head;  /* Tone queue head index */

	volatile enum cw_queue_state state;

	/* It's useful to have the tone queue dequeue function call
	   a client-supplied callback routine when the amount of data
	   in the queue drops below a defined low water mark.
	   This routine can then refill the buffer, as required. */
	volatile int low_water_mark;
	void       (*low_water_callback)(void*);
	void        *low_water_callback_arg;

	pthread_mutex_t mutex;
}; /* typedef cw_tone_queue_t */





#define CW_TONE_QUEUE_LENGTH(m_tq)				\
	( m_tq->tail >= m_tq->head				\
	  ? m_tq->tail - m_tq->head				\
	  : m_tq->tail - m_tq->head + CW_TONE_QUEUE_CAPACITY )	\





/**
   \brief Initialize a tone queue

   Initialize tone queue structure - \p tq

   \param tq - tone queue to initialize

   \return CW_SUCCESS on completion
*/
int cw_tone_queue_init_internal(cw_tone_queue_t *tq)
{
	int rv = pthread_mutex_init(&tq->mutex, NULL);
	assert (!rv);

	pthread_mutex_lock(&tq->mutex);

	tq->tail = 0;
	tq->head = 0;
	tq->state = QS_IDLE;

	tq->low_water_mark = 0;
	tq->low_water_callback = NULL;
	tq->low_water_callback_arg = NULL;

	pthread_mutex_unlock(&tq->mutex);

	return CW_SUCCESS;
}





/**
   \brief Return number of items on tone queue

   \param tq - tone queue

   \return the count of tones currently held in the circular tone buffer.
*/
uint32_t cw_tone_queue_length_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&tq->mutex);
	int len = CW_TONE_QUEUE_LENGTH(tq);
	pthread_mutex_unlock(&tq->mutex);

	return len;
}





/**
   \brief Get previous index to queue

   Calculate index of previous element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   \param ind - index in relation to which to calculate index of previous element in queue

   \return index of previous element in queue
*/
int cw_tone_queue_prev_index_internal(int ind)
{
	return ind - 1 >= 0 ? ind - 1 : CW_TONE_QUEUE_CAPACITY - 1;
}





/**
   \brief Get next index to queue

   Calculate index of next element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   \param ind - index in relation to which to calculate index of next element in queue

   \return index of next element in queue
*/
int cw_tone_queue_next_index_internal(int ind)
{
	return (ind + 1) % CW_TONE_QUEUE_CAPACITY;
}





/**
   \brief Dequeue a tone from tone queue

   Dequeue a tone from tone queue.

   The queue returns two distinct values when it is empty, and one value
   when it is not empty:
   \li CW_TQ_JUST_EMPTIED - when there were no new tones in the queue, but
       the queue still remembered its "BUSY" state; this return value
       is a way of telling client code "I've had tones, but no more, you
       should probably stop playing any sounds and become silent";
   \li CW_TQ_STILL_EMPTY - when there were no new tones in the queue, and
       the queue can't recall if it was "BUSY" before; this return value
       is a way of telling client code "I don't have any tones, you should
       probably stay silent";
   \li CW_TQ_NONEMPTY - when there was at least one tone in the queue;
       client code can call the function again, and the function will
       then return CW_TQ_NONEMPTY (if there is yet another tone), or
       CW_TQ_JUST_EMPTIED (if the tone from previous call was the last one);

   Information about successfully dequeued tone is returned through
   function's arguments: \p usecs and \p frequency.
   The function does not modify the arguments if there are no tones to
   dequeue.

   If the last tone in queue has duration "CW_AUDIO_FOREVER_USECS", the function
   won't permanently dequeue it (won't "destroy" it). Instead, it will keep
   returning (through \p usecs and \p frequency) the tone on every call,
   until a new tone is added to the queue after the "CW_AUDIO_FOREVER_USECS" tone.

   \param tq - tone queue
   \param usecs - output, space for duration of dequeued tone
   \param frequency - output, space for frequency of dequeued tone

   \return CW_TQ_JUST_EMPTIED (see information above)
   \return CW_TQ_STILL_EMPTY (see information above)
   \return CW_TQ_NONEMPTY (see information above)
*/
int cw_tone_queue_dequeue_internal(cw_tone_queue_t *tq, cw_tone_t *tone)
{
	pthread_mutex_lock(&tq->mutex);

#ifdef LIBCW_WITH_DEV
	static enum {
		REPORTED_STILL_EMPTY,
		REPORTED_JUST_EMPTIED,
		REPORTED_NONEMPTY
	} tq_report = REPORTED_STILL_EMPTY;
#endif


	/* Decide what to do based on the current state. */
	switch (tq->state) {

	case QS_IDLE:
#ifdef LIBCW_WITH_DEV
		if (tq_report != REPORTED_STILL_EMPTY) {
			/* tone queue is empty */
			cw_debug_ev ((&cw_debug_object_ev), 0, CW_DEBUG_EVENT_TQ_STILL_EMPTY);
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
				      "libcw: tone queue: still empty");
			tq_report = REPORTED_STILL_EMPTY;
		}
#endif
		pthread_mutex_unlock(&tq->mutex);
		/* Ignore calls if our state is idle. */
		return CW_TQ_STILL_EMPTY;

	case QS_BUSY:
		/* If there are some tones in queue, dequeue the next
		   tone. If there are no more tones, go to the idle state. */
		if (tq->head != tq->tail) {
			/* Get the current queue length.  Later on, we'll
			   compare with the length after we've scanned
			   over every tone we can omit, and use it to see
			   if we've crossed the low water mark, if any. */
			int queue_length = CW_TONE_QUEUE_LENGTH(tq);

			/* Advance over the tones list until we find the
			   first tone with a duration of more than zero
			   usecs, or until the end of the list.
			   TODO: don't add tones with duration = 0? */
			int tmp_tq_head = tq->head;
			do  {
				tmp_tq_head = cw_tone_queue_next_index_internal(tmp_tq_head);
			} while (tmp_tq_head != tq->tail
				 && tq->queue[tmp_tq_head].usecs == 0);

			/* Get parameters of tone to be played */
			tone->usecs = tq->queue[tmp_tq_head].usecs;
			tone->frequency = tq->queue[tmp_tq_head].frequency;
			tone->slope_mode = tq->queue[tmp_tq_head].slope_mode;

			if (tone->usecs == CW_AUDIO_FOREVER_USECS && queue_length == 1) {
				/* The last tone currently in queue is
				   CW_AUDIO_FOREVER_USECS, which means that we
				   should play certain tone until client
				   code adds next tone (possibly forever).

				   Don't dequeue the "forever" tone (hence "prev").	*/
				tq->head = cw_tone_queue_prev_index_internal(tmp_tq_head);
			} else {
				tq->head = tmp_tq_head;
			}

			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
				      "libcw: tone queue: dequeue tone %d usec, %d Hz", tone->usecs, tone->frequency);
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
				      "libcw: tone queue: head = %d, tail = %d, length = %d", tq->head, tq->tail, queue_length);

			/* Notify the key control function that there might
			   have been a change of keying state (and then
			   again, there might not have been -- it will sort
			   this out for us). */
			cw_key_set_state_internal(tone->frequency ? CW_KEY_STATE_CLOSED : CW_KEY_STATE_OPEN);

#if 0
			/* If microseconds is zero, leave it at that.  This
			   way, a queued tone of 0 usec implies leaving the
			   sound in this state, and 0 usec and 0 frequency
			   leaves silence.  */ /* TODO: ??? */
			if (tone->usecs == 0) {
				/* Autonomous dequeuing has finished for
				   the moment. */
				tq->state = QS_IDLE;
				cw_finalization_schedule_internal();
			}
#endif


#ifdef LIBCW_WITH_DEV
			if (tq_report != REPORTED_NONEMPTY) {
				cw_debug_ev ((&cw_debug_object_ev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_EVENT_TQ_NONEMPTY);
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
					      "libcw: tone queue: nonempty: usecs = %d, freq = %d, slope = %d", tone->usecs, tone->frequency, tone->slope_mode);
				tq_report = REPORTED_NONEMPTY;
			}
#endif
			pthread_mutex_unlock(&tq->mutex);

			/* Since client's callback can use functions
			   that call pthread_mutex_lock(), we should
			   put the callback *after* we release
			   pthread_mutex_unlock() in this funciton. */

			/* If there is a low water mark callback registered,
			   and if we passed under the water mark, call the
			   callback here.  We want to be sure to call this
			   late in the processing, especially after setting
			   the state to idle, since the most likely action
			   of this routine is to queue tones, and we don't
			   want to play with the state here after that. */
			if (tq->low_water_callback) {
				/* If the length we originally calculated
				   was above the low water mark, and the
				   one we have now is below or equal to it,
				   call the callback. */

				/* It may seem that the double condition in
				   'if ()' is redundant, but for some reason
				   it is necessary. Be very, very careful
				   when modifying this. */
				if (queue_length > tq->low_water_mark
				    && CW_TONE_QUEUE_LENGTH(tq) <= tq->low_water_mark

				    /* Avoid endlessly calling the callback
				       if the only queued tone is 'forever'
				       tone. Once someone decides to end the
				       'forever' tone, we will be ready to
				       call the callback again. */
				    && !(tone->usecs == CW_AUDIO_FOREVER_USECS && queue_length == 1)

				    ) {

					(*(tq->low_water_callback))(tq->low_water_callback_arg);
				}
			}

			return CW_TQ_NONEMPTY;
		} else { /* tq->head == tq->tail */
			/* State of tone queue (as indicated by tq->state)
			   is "busy", but it turns out that there are no
			   tones left on the queue to play (head == tail).

			   Time to bring tq->state in sync with
			   head/tail state. Set state to idle, indicating
			   that autonomous dequeuing has finished for the
			   moment. */
			tq->state = QS_IDLE;

			/* There is no tone to dequeue, so don't modify
			   function's arguments. Client code will learn
			   about "no tones" state through return value. */
			/* tone->usecs = 0; */
			/* tone->frequency = 0; */

			/* Notify the keying control function about the silence. */
			cw_key_set_state_internal(CW_KEY_STATE_OPEN);

			//cw_finalization_schedule_internal();

#ifdef LIBCW_WITH_DEV
			if (tq_report != REPORTED_JUST_EMPTIED) {
				cw_debug_ev ((&cw_debug_object_ev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_EVENT_TQ_JUST_EMPTIED);
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
					      "libcw: tone queue: just emptied");
				tq_report = REPORTED_JUST_EMPTIED;
			}
#endif

			pthread_mutex_unlock(&tq->mutex);
			return CW_TQ_JUST_EMPTIED;
		}
	}

	pthread_mutex_unlock(&tq->mutex);
	/* will never get here as "queue state" enum has only two values */
	assert(0);
	return CW_TQ_STILL_EMPTY;
}





/**
   \brief Add tone to tone queue

   Enqueue a tone for specified frequency and number of microseconds.
   This routine adds the new tone to the queue, and if necessary starts
   the itimer process to have the tone sent.  The routine returns CW_SUCCESS
   on success. If the tone queue is full, the routine returns CW_FAILURE,
   with errno set to EAGAIN.  If the iambic keyer or straight key are currently
   busy, the routine returns CW_FAILURE, with errno set to EBUSY.

   \param tq - tone queue
   \param usecs - length of added tone
   \param frequency - frequency of added tone

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tone_queue_enqueue_internal(cw_tone_queue_t *tq, cw_tone_t *tone)
{
	pthread_mutex_lock(&tq->mutex);
	/* If the keyer or straight key are busy, return an error.
	   This is because they use the sound card/console tones and key
	   control, and will interfere with us if we try to use them at
	   the same time. */
	// if (cw_is_keyer_busy() || cw_is_straight_key_busy()) {
	if (0) {
		errno = EBUSY;
		pthread_mutex_unlock(&tq->mutex);
		return CW_FAILURE;
	}

	/* Get the new value of the queue tail index. */
	int new_tq_tail = cw_tone_queue_next_index_internal(tq->tail);

	/* If the new value is bumping against the head index, then
	   the queue is currently full. */
	if (new_tq_tail == tq->head) {
		errno = EAGAIN;
		pthread_mutex_unlock(&tq->mutex);
		return CW_FAILURE;
	}

	cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      "libcw: tone queue: enqueue tone %d usec, %d Hz", tone->usecs, tone->frequency);

	/* Set the new tail index, and enqueue the new tone. */
	tq->tail = new_tq_tail;
	tq->queue[tq->tail].usecs = tone->usecs;
	tq->queue[tq->tail].frequency = tone->frequency;
	tq->queue[tq->tail].slope_mode = tone->slope_mode;

	/* If there is currently no autonomous dequeue happening, kick
	   off the itimer process. */
	if (tq->state == QS_IDLE) {
		tq->state = QS_BUSY;
		/* A loop in write() function may await for the queue
		   to be filled with new tones to dequeue and play.
		   It waits for a signal. This is a right place and time
		   to send such a signal. */
		pthread_kill(generator->thread.id, SIGALRM);
	}

	pthread_mutex_unlock(&tq->mutex);
	return CW_SUCCESS;
}





/**
   \brief Register callback for low queue state

   Register a function to be called automatically by the dequeue routine
   whenever the tone queue falls to a given \p level. To be more precise:
   the callback is called by queue manager if, after dequeueing a tone,
   the manager notices that tone queue length has become equal or less
   than \p level.

   \p callback_arg may be used to give a value passed back on callback
   calls.  A NULL function pointer suppresses callbacks.  On success,
   the routine returns CW_SUCCESS.

   If \p level is invalid, the routine returns CW_FAILURE with errno set to
   EINVAL.  Any callback supplied will be called in signal handler context.

   \param callback_func - callback function to be registered
   \param callback_arg - argument for callback_func to pass return value
   \param level - low level of queue triggering callback call

   \return CW_SUCCESS on successful registration
   \return CW_FAILURE on failure
*/
int cw_register_tone_queue_low_callback(void (*callback_func)(void*), void *callback_arg, int level)
{
	if (level < 0 || level >= CW_TONE_QUEUE_CAPACITY - 1) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Store the function and low water mark level. */
	cw_tone_queue.low_water_mark = level;
	cw_tone_queue.low_water_callback = callback_func;
	cw_tone_queue.low_water_callback_arg = callback_arg;

	return CW_SUCCESS;
}





/**
   \brief Check if tone sender is busy

   Indicate if the tone sender is busy.

   \return true if there are still entries in the tone queue
   \return false if the queue is empty
*/
bool cw_is_tone_busy(void)
{
	return cw_tone_queue.state != QS_IDLE;
}





/**
   \brief Wait for the current tone to complete

   The routine returns CW_SUCCESS on success.  If called with SIGALRM
   blocked, the routine returns CW_FAILURE, with errno set to EDEADLK,
   to avoid indefinite waits.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_tone(void)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait for the tail index to change or the dequeue to go idle. */
	int check_tq_head = cw_tone_queue.head;
	while (cw_tone_queue.head == check_tq_head && cw_tone_queue.state != QS_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the tone queue to drain

   The routine returns CW_SUCCESS on success. If called with SIGALRM
   blocked, the routine returns false, with errno set to EDEADLK,
   to avoid indefinite waits.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_tone_queue(void)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait until the dequeue indicates it's hit the end of the queue. */
	while (cw_tone_queue.state != QS_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the tone queue to drain until only as many tones as given in level remain queued

   This routine is for use by programs that want to optimize themselves
   to avoid the cleanup that happens when the tone queue drains completely;
   such programs have a short time in which to add more tones to the queue.

   The routine returns CW_SUCCESS on success.  If called with SIGALRM
   blocked, the routine returns false, with errno set to EDEADLK, to
   avoid indefinite waits.

   \param level - low level in queue, at which to return

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_tone_queue_critical(int level)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait until the queue length is at or below criticality. */
	while (cw_tone_queue_length_internal(generator->tq) > (uint32_t) level) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Indicate if the tone queue is full

   \return true if tone queue is full
   \return false if tone queue is not full
*/
bool cw_is_tone_queue_full(void)
{
	/* If advancing would meet the tail index, return true. */
	return cw_tone_queue_next_index_internal(cw_tone_queue.tail) == cw_tone_queue.head;
}





/**
   \brief Return the number of entries the tone queue can accommodate
*/
int cw_get_tone_queue_capacity(void)
{
	/* Since the head and tail indexes cannot be equal, the
	   perceived capacity for the client is always one less
	   than the actual declared queue size. */
	return CW_TONE_QUEUE_CAPACITY - 1;
}





/**
   \brief Return the number of entries currently pending in the tone queue
*/
int cw_get_tone_queue_length(void)
{
	/* TODO: change return type to uint32_t. */
	return (int) cw_tone_queue_length_internal(generator->tq);
}




/**
   \brief Cancel all pending queued tones, and return to silence.

   If there is a tone in progress, the function will wait until this
   last one has completed, then silence the tones.

   This function may be called with SIGALRM blocked, in which case it
   will empty the queue as best it can, then return without waiting for
   the final tone to complete.  In this case, it may not be possible to
   guarantee silence after the call.
*/
void cw_flush_tone_queue(void)
{
	pthread_mutex_lock(&generator->tq->mutex);
	/* Empty the queue, by setting the head to the tail. */
	generator->tq->head = generator->tq->tail;
	pthread_mutex_unlock(&generator->tq->mutex);

	/* If we can, wait until the dequeue goes idle. */
	if (!cw_sigalrm_is_blocked_internal()) {
		cw_wait_for_tone_queue();
	}

	/* Force silence on the speaker anyway, and stop any background
	   soundcard tone generation. */
	cw_generator_silence_internal(generator);
	//cw_finalization_schedule_internal();

	return;
}





/**
   \brief Primitive access to simple tone generation

   This routine queues a tone of given duration and frequency.
   The routine returns CW_SUCCESS on success.  If usec or frequency
   are invalid, it returns CW_FAILURE with errno set to EINVAL.
   If the sound card, console speaker, or keying function are busy,
   it returns CW_FAILURE  with errno set to EBUSY.  If the tone queue
   is full, it returns false with errno set to EAGAIN.

   \param usecs - duration of queued tone, in microseconds
   \param frequency - frequency of queued tone

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_queue_tone(int usecs, int frequency)
{
	/* Check the arguments given for realistic values.  Note that we
	   do nothing here to protect the caller from setting up
	   neverending (0 usecs) tones, if that's what they want to do. */
	if (usecs < 0
	    || frequency < 0
	    || frequency < CW_FREQUENCY_MIN
	    || frequency > CW_FREQUENCY_MAX) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES;
	tone.usecs = usecs;
	tone.frequency = frequency;
	return cw_tone_queue_enqueue_internal(generator->tq, &tone);
}





/**
   Cancel all pending queued tones, reset any queue low callback registered,
   and return to silence.  This function is suitable for calling from an
   application exit handler.
*/
void cw_reset_tone_queue(void)
{
	/* Empty the queue, and force state to idle. */
	cw_tone_queue.head = cw_tone_queue.tail;
	cw_tone_queue.state = QS_IDLE;

	/* Reset low water mark details to their initial values. */
	cw_tone_queue.low_water_mark = 0;
	cw_tone_queue.low_water_callback = NULL;
	cw_tone_queue.low_water_callback_arg = NULL;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_generator_silence_internal(generator);
	//cw_finalization_schedule_internal();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_TONE_QUEUE, CW_DEBUG_INFO,
		      "libcw: tone queue: reset");

	return;
}





/* ******************************************************************** */
/*                            Section:Sending                           */
/* ******************************************************************** */





/**
   \brief Send an element

   Low level primitive to send a tone element of the given type, followed
   by the standard inter-element silence.

   Function sets errno to EINVAL if an argument is invalid, and returns
   CW_FAILURE.
   Function also returns failure if adding the element to queue of elements
   failed.

   \param gen - current generator
   \param element - element to send - dot (CW_DOT_REPRESENTATION) or dash (CW_DASH_REPRESENTATION)

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
 */
int cw_send_element_internal(cw_gen_t *gen, char element)
{
	int status;

	/* Synchronize low-level timings if required. */
	cw_sync_parameters_internal(gen);

	/* Send either a dot or a dash element, depending on representation. */
	if (element == CW_DOT_REPRESENTATION) {
		cw_tone_t tone;
		tone.slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES;
		tone.usecs = cw_send_dot_length;
		tone.frequency = gen->frequency;
		status = cw_tone_queue_enqueue_internal(gen->tq, &tone);
	} else if (element == CW_DASH_REPRESENTATION) {
		cw_tone_t tone;
		tone.slope_mode = CW_SLOPE_MODE_STANDARD_SLOPES;
		tone.usecs = cw_send_dash_length;
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
	tone.usecs = cw_end_of_ele_delay;
	tone.frequency = 0;
	if (!cw_tone_queue_enqueue_internal(gen->tq, &tone)) {
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
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
	return cw_send_element_internal(generator, CW_DOT_REPRESENTATION);
}





/**
   See documentation of cw_send_dot() for more information
*/
int cw_send_dash(void)
{
	return cw_send_element_internal(generator, CW_DASH_REPRESENTATION);
}





/**
   See documentation of cw_send_dot() for more information
*/
int cw_send_character_space(void)
{
	/* Synchronize low-level timing parameters. */
	cw_sync_parameters_internal(generator);

	/* Delay for the standard end of character period, plus any
	   additional inter-character gap */
	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
	tone.usecs = cw_end_of_char_delay + cw_additional_delay;
	tone.frequency = 0;
	return cw_tone_queue_enqueue_internal(generator->tq, &tone);
}





/**
   See documentation of cw_send_dot() for more information
*/
int cw_send_word_space(void)
{
	/* Synchronize low-level timing parameters. */
	cw_sync_parameters_internal(generator);

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
	tone.usecs = cw_end_of_word_delay;
	tone.frequency = 0;
	int a = cw_tone_queue_enqueue_internal(generator->tq, &tone);

	int b = CW_FAILURE;

	if (a == CW_SUCCESS) {
		tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
		tone.usecs = cw_adjustment_delay;
		tone.frequency = 0;
		b = cw_tone_queue_enqueue_internal(generator->tq, &tone);
	}

	return a && b;
#else
	/* Queue space character as a single tone. */

	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;
	tone.usecs = cw_end_of_word_delay + cw_adjustment_delay;
	tone.frequency = 0;

	return cw_tone_queue_enqueue_internal(generator->tq, &tone);
#endif
}





/**
   Send the given string as dots and dashes, adding the post-character gap.

   Function sets EAGAIN if there is not enough space in tone queue to
   enqueue \p representation.

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
	if (cw_get_tone_queue_length() >= CW_TONE_QUEUE_HIGH_WATER_MARK) {
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
   On error, it returns CW_FAILURE, with errno set to EINVAL if any
   character of the representation is invalid, EBUSY if the sound card,
   console speaker, or keying system is busy, or EAGAIN if the tone
   queue is full, or if there is insufficient space to queue the tones
   or the representation.

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
		return cw_send_representation_internal(generator, representation, false);
	}
}





/**
   \brief Check, then send the given string as dots and dashes

   The \p representation passed in is assumed to be only part of a larger
   Morse representation; that is, no post-character delays will be added
   when the character is sent.

   On success, the routine returns CW_SUCCESS.
   On error, it returns CW_FAILURE, with errno set to EINVAL if any
   character of the representation is invalid, EBUSY if the sound card,
   console speaker, or keying system is busy, or EAGAIN if the tone queue
   is full, or if there is insufficient space to queue the tones for
   the representation.
*/
int cw_send_representation_partial(const char *representation)
{
	if (!cw_representation_is_valid(representation)) {
		errno = ENOENT;
		return CW_FAILURE;
	} else {
		return cw_send_representation_internal(generator, representation, true);
	}
}





/**
   \brief Lookup, and send a given ASCII character as Morse code

   If "partial" is set, the end of character delay is not appended to the
   Morse code sent.

   Function sets errno to ENOENT if \p character is not a recognized character.

   \param gen - current generator
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
   \brief Checks that the given character is validly sendable in Morse

   Function sets errno to ENOENT on failure.

   \param c - character to check

   \return CW_SUCCESS if character is valid
   \return CW_FAILURE if character is invalid
*/
bool cw_character_is_valid(char c)
{
	/* If the character is the space special-case, or it is in the
	   lookup table, return success. */
	if (c == ' ' || cw_character_to_representation_internal(c)) {
		return CW_SUCCESS;
	} else {
		errno = ENOENT;
		return CW_FAILURE;
	}
}



int cw_check_character(char c)
{
	return (int) cw_character_is_valid(c);
}




/**
   \brief Lookup, and send a given ASCII character as Morse

   The end of character delay is appended to the Morse sent.

   On success, the routine returns true.
   On error, it returns false, with errno set to ENOENT if the given
   character \p c is not a valid Morse character, EBUSY if the sound card,
   console speaker, or keying system is busy, or EAGAIN if the tone queue
   is full, or if there is insufficient space to queue the tones for the
   character.

   This routine returns as soon as the character has been successfully
   queued for sending; that is, almost immediately.  The actual sending
   happens in background processing.  See cw_wait_for_tone() and
   cw_wait_for_tone_queue() for ways to check the progress of sending.

   \param c - character to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_character(char c)
{
	if (!cw_check_character(c)) {
		errno = ENOENT;
		return CW_FAILURE;
	} else {
		return cw_send_character_internal(generator, c, false);
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
	if (!cw_check_character(c)) {
		errno = ENOENT;
		return CW_FAILURE;
	} else {
		return cw_send_character_internal(generator, c, true);
	}
}





/**
   \brief Check that each character in the given string is validly sendable in Morse

   Function sets errno to EINVAL on failure

   \param string - string to check

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
bool cw_string_is_valid(const char *string)
{
	/* Check that each character in the string has a Morse
	   representation, or - as a special case - is a space character. */
	for (int i = 0; string[i] != '\0'; i++) {
		if (!(string[i] == ' '
		      || cw_character_to_representation_internal(string[i]))) {

			errno = EINVAL;
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}





int cw_check_string(const char *string)
{
	return cw_string_is_valid(string);
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

   This routine queues its arguments for background processing.  See
   cw_send_character() for details of how to check the queue status.

   \param string - string to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_string(const char *string)
{
	/* Check the string is composed of sendable characters. */
	if (!cw_check_string(string)) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	/* Send every character in the string. */
	for (int i = 0; string[i] != '\0'; i++) {
		if (!cw_send_character_internal(generator, string[i], false))
			return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/* ******************************************************************** */
/*            Section:Receive tracking and statistics helpers           */
/* ******************************************************************** */





/* Receive adaptive speed tracking.  A moving averages structure, comprising
   a small array of element lengths, a circular index into the array, and
   a running sum of elements for efficient calculation of moving averages. */
enum { AVERAGE_ARRAY_LENGTH = 4 };
struct cw_tracking_struct {
	int buffer[AVERAGE_ARRAY_LENGTH];  /* Buffered element lengths */
	int cursor;                        /* Circular buffer cursor */
	int sum;                           /* Running sum */
}; /* typedef cw_tracking_t */

static cw_tracking_t cw_dot_tracking  = { {0}, 0, 0 },
	             cw_dash_tracking = { {0}, 0, 0 };





/**
   \brief Reset tracking data structure

   Moving average function for smoothed tracking of dot and dash lengths.

   \param tracking - tracking data structure
   \param initial - initial value to be put in table of tracking data structure
*/
void cw_reset_adaptive_average_internal(cw_tracking_t *tracking, int initial)
{
	for (int i  = 0; i < AVERAGE_ARRAY_LENGTH; i++) {
		tracking->buffer[i] = initial;
	}

	tracking->sum = initial * AVERAGE_ARRAY_LENGTH;
	tracking->cursor = 0;

	return;
}





/**
   \brief Add new element to tracking data structure

   Moving average function for smoothed tracking of dot and dash lengths.

   \param tracking - tracking data structure
   \param element_usec - new element to add to tracking data
*/
void cw_update_adaptive_average_internal(cw_tracking_t *tracking, int element_usec)
{
	tracking->sum += element_usec - tracking->buffer[tracking->cursor];
	tracking->buffer[tracking->cursor++] = element_usec;
	tracking->cursor %= AVERAGE_ARRAY_LENGTH;

	return;
}





/**
   \brief Get average sum from tracking data structure

   \param tracking - tracking data structure

   \return average sum
*/
int cw_get_adaptive_average_internal(cw_tracking_t *tracking)
{
	return tracking->sum / AVERAGE_ARRAY_LENGTH;
}





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


enum { STATISTICS_ARRAY_LENGTH = 256 };

static cw_statistics_t cw_receive_statistics[STATISTICS_ARRAY_LENGTH] = { {0, 0} };
static int cw_statistics_cursor = 0;

static void cw_add_receive_statistic_internal(stat_type_t type, int usecs);
static double cw_get_receive_statistic_internal(stat_type_t type);





/**
   \brief Add an element timing to statistics

   Add an element timing with a given statistic type to the circular
   statistics buffer.  The buffer stores only the delta from the ideal
   value; the ideal is inferred from the type passed in.

   \param type - element type
   \param usecs - timing of an element
*/
void cw_add_receive_statistic_internal(stat_type_t type, int usecs)
{
	/* Synchronize low-level timings if required. */
	cw_sync_parameters_internal(generator);

	/* Calculate delta as difference between usec and the ideal value. */
	int delta = usecs - ((type == STAT_DOT) ? cw_receive_dot_length
			     : (type == STAT_DASH) ? cw_receive_dash_length
			     : (type == STAT_END_ELEMENT) ? cw_eoe_range_ideal
			     : (type == STAT_END_CHARACTER) ? cw_eoc_range_ideal : usecs);

	/* Add this statistic to the buffer. */
	cw_receive_statistics[cw_statistics_cursor].type = type;
	cw_receive_statistics[cw_statistics_cursor++].delta = delta;
	cw_statistics_cursor %= STATISTICS_ARRAY_LENGTH;

	return;
}





/**
   \brief Calculate and return one given timing statistic type

   \return 0.0 if no record of given type were found
   \return timing statistics otherwise
*/
double cw_get_receive_statistic_internal(stat_type_t type)
{
	/* Sum and count elements matching the given type.  A cleared
	   buffer always begins refilling at element zero, so to optimize
	   we can stop on the first unoccupied slot in the circular buffer. */
	double sum_of_squares = 0.0;
	int count = 0;
	for (int cursor = 0; cursor < STATISTICS_ARRAY_LENGTH; cursor++) {
		if (cw_receive_statistics[cursor].type == type) {
			int delta = cw_receive_statistics[cursor].delta;
			sum_of_squares += (double) delta * (double) delta;
			count++;
		} else if (cw_receive_statistics[cursor].type == STAT_NONE) {
			break;
		}
	}

	/* Return the standard deviation, or zero if no matching elements. */
	return count > 0 ? sqrt (sum_of_squares / (double) count) : 0.0;
}





/**
   \brief Calculate and return receive timing statistics

   These statistics may be used to obtain a measure of the accuracy of
   received CW.  The values \p dot_sd and \p dash_sd contain the standard
   deviation of dot and dash lengths from the ideal values, and
   \p element_end_sd and \p character_end_sd the deviations for inter
   element and inter character spacing.  Statistics are held for all
   timings in a 256 element circular buffer.  If any statistic cannot
   be calculated, because no records for it exist, the returned value
   is 0.0.  Use NULL for the pointer argument to any statistic not required.

   \param dot_sd
   \param dash_sd
   \param element_end_sd
   \param character_end_sd
*/
void cw_get_receive_statistics(double *dot_sd, double *dash_sd,
			       double *element_end_sd, double *character_end_sd)
{
	if (dot_sd) {
		*dot_sd = cw_get_receive_statistic_internal(STAT_DOT);
	}
	if (dash_sd) {
		*dash_sd = cw_get_receive_statistic_internal(STAT_DASH);
	}
	if (element_end_sd) {
		*element_end_sd = cw_get_receive_statistic_internal(STAT_END_ELEMENT);
	}
	if (character_end_sd) {
		*character_end_sd = cw_get_receive_statistic_internal(STAT_END_CHARACTER);
	}
	return;
}





/**
   \brief Clear the receive statistics buffer

   Clear the receive statistics buffer by removing all records from it and
   returning it to its initial default state.
*/
void cw_reset_receive_statistics(void)
{
	for (int i  = 0; i < STATISTICS_ARRAY_LENGTH; i++) {
		cw_receive_statistics[i].type = STAT_NONE;
		cw_receive_statistics[i].delta = 0;
	}
	cw_statistics_cursor = 0;

	return;
}





/* ******************************************************************** */
/*                           Section:Receiving                          */
/* ******************************************************************** */





/* Receive buffering.
   This is a fixed-length representation, filled in as tone on/off
   timings are taken.  The buffer is vastly longer than any practical
   representation, and along with it we maintain a cursor indicating
   the current write position. */
enum { RECEIVE_CAPACITY = 256 };
static char cw_receive_representation_buffer[RECEIVE_CAPACITY];
static int cw_rr_current = 0;

/* Retained tone start and end timestamps. */
static struct timeval cw_rr_start_timestamp = {0, 0},
                      cw_rr_end_timestamp   = {0, 0};





/**
   \brief Set value of "adaptive receive enabled" flag

   Set the value of the flag that controls whether, on receive, the
   receive functions do fixed speed receive, or track the speed of the
   received Morse code by adapting to the input stream.

   \brief flag - intended flag value
*/
void cw_receive_set_adaptive_internal(bool flag)
{
	/* Look for change of adaptive receive state. */
	if ((cw_is_adaptive_receive_enabled && !flag)
	    || (!cw_is_adaptive_receive_enabled && flag)) {

		cw_is_adaptive_receive_enabled = flag;

		/* Changing the flag forces a change in low-level parameters. */
		cw_is_in_sync = false;
		cw_sync_parameters_internal(generator);

		/* If we have just switched to adaptive mode, (re-)initialize
		   the averages array to the current dot/dash lengths, so
		   that initial averages match the current speed. */
		if (cw_is_adaptive_receive_enabled) {
			cw_reset_adaptive_average_internal(&cw_dot_tracking, cw_receive_dot_length);
			cw_reset_adaptive_average_internal(&cw_dash_tracking, cw_receive_dash_length);
		}
	}

	return;
}





/**
   \brief Enable adaptive receive speeds

   If adaptive speed tracking is enabled, the receive functions will
   attempt to automatically adjust the receive speed setting to match
   the speed of the incoming Morse code. If it is disabled, the receive
   functions will use fixed speed settings, and reject incoming Morse
   which is not at the expected speed.

   Adaptive speed tracking uses a moving average of the past four elements
   as its baseline for tracking speeds.  The default state is adaptive
   tracking disabled.
*/
void cw_enable_adaptive_receive(void)
{
	cw_receive_set_adaptive_internal(true);
	return;
}





/**
   \brief Disable adaptive receive speeds

   See documentation of cw_enable_adaptive_receive() for more information
*/
void cw_disable_adaptive_receive(void)
{
	cw_receive_set_adaptive_internal(false);
	return;
}





/**
   \brief Get adaptive receive speeds flag

   The function returns state of "adaptive receive enabled" flag.
   See documentation of cw_enable_adaptive_receive() for more information

   \return true if adaptive speed tracking is enabled
   \return false otherwise
*/
bool cw_get_adaptive_receive_state(void)
{
	return cw_is_adaptive_receive_enabled;
}





/**
   \brief Validate timestamp

   If an input timestamp is given, validate it for correctness, and if
   valid, copy it into return_timestamp and return true.  If invalid,
   return false with errno set to EINVAL.  If an input timestamp is not
   given (NULL), return true with the current system time in
   return_timestamp.

   \param timestamp
   \param return_timestamp
*/
int cw_timestamp_validate_internal(const struct timeval *timestamp,
				   struct timeval *return_timestamp)
{
	if (timestamp) {
		if (timestamp->tv_sec < 0
		    || timestamp->tv_usec < 0
		    || timestamp->tv_usec >= USECS_PER_SEC) {

			errno = EINVAL;
			return CW_FAILURE;
		} else {
			*return_timestamp = *timestamp;
			return CW_SUCCESS;
		}
	} else {
		if (gettimeofday(return_timestamp, NULL)) {
			perror ("libcw: gettimeofday");
			return CW_FAILURE;
		} else {
			return CW_SUCCESS;
		}
	}
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
static enum {
	RS_IDLE,
	RS_IN_TONE,
	RS_AFTER_TONE,
	RS_END_CHAR,
	RS_END_WORD,
	RS_ERR_CHAR,
	RS_ERR_WORD
} cw_receive_state = RS_IDLE;


/**
   \brief Compare two timestamps

   Compare two timestamps, and return the difference between them in
   microseconds, taking care to clamp values which would overflow an int.

   This routine always returns a positive integer in the range 0 to INT_MAX.

   \param earlier - timestamp to compare
   \param later - timestamp to compare

   \return difference between timestamps (in microseconds)
*/
int cw_timestamp_compare_internal(const struct timeval *earlier,
				  const struct timeval *later)
{

	/* Compare the timestamps, taking care on overflows.

	   At 4 WPM, the dash length is 3*(1200000/4)=900,000 usecs, and
	   the word gap is 2,100,000 usecs.  With the maximum Farnsworth
	   additional delay, the word gap extends to 20,100,000 usecs.
	   This fits into an int with a lot of room to spare, in fact, an
	   int can represent 2,147,483,647 usecs, or around 33 minutes.
	   This is way, way longer than we'd ever want to differentiate,
	   so if by some chance we see timestamps farther apart than this,
	   and it ought to be very, very unlikely, then we'll clamp the
	   return value to INT_MAX with a clear conscience.

	   Note: passing nonsensical or bogus timevals in may result in
	   unpredictable results.  Nonsensical includes timevals with
	   -ve tv_usec, -ve tv_sec, tv_usec >= 1,000,000, etc.
	   To help in this, we check all incoming timestamps for
	   "well-formedness".  However, we assume the  gettimeofday()
	   call always returns good timevals.  All in all, timeval could
	   probably be a better thought-out structure. */

	/* Calculate an initial delta, possibly with overflow. */
	int delta_usec = (later->tv_sec - earlier->tv_sec) * USECS_PER_SEC
		+ later->tv_usec - earlier->tv_usec;

	/* Check specifically for overflow, and clamp if it did. */
	if ((later->tv_sec - earlier->tv_sec) > (INT_MAX / USECS_PER_SEC) + 1
	    || delta_usec < 0) {

		delta_usec = INT_MAX;
	}

	return delta_usec;
}





/**
   \brief Mark beginning of receive tone

   Called on the start of a receive tone.  If the \p timestamp is NULL, the
   current time is used.
   On error the function returns CW_FAILURE, with errno set to ERANGE if
   the call is directly after another cw_start_receive_tone() call or if
   an existing received character has not been cleared from the buffer,
   or EINVAL if the timestamp passed in is invalid.

   \param timestamp

   \return CW_SUCCESS on success
   \return CW_FAILURE otherwise (with errno set)
 */
int cw_start_receive_tone(const struct timeval *timestamp)
{
	/* If the receive state is not idle or after a tone, this is
	   a state error.  A receive tone start can only happen while
	   we are idle, or in the middle of a character. */
	if (cw_receive_state != RS_IDLE && cw_receive_state != RS_AFTER_TONE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Validate and save the timestamp, or get one and then save it. */
	if (!cw_timestamp_validate_internal(timestamp, &cw_rr_start_timestamp)) {
		return CW_FAILURE;
	}

	/* If we are in the after tone state, we can measure the
	   inter-element gap by comparing the start timestamp with the
	   last end one, guaranteed set by getting to the after tone
	   state via cw_end_receive tone(), or in extreme cases, by
	   cw_receive_add_element_internal().

	   Do that, then, and update the relevant statistics. */
	if (cw_receive_state == RS_AFTER_TONE) {
		int space_usec = cw_timestamp_compare_internal(&cw_rr_end_timestamp,
							       &cw_rr_start_timestamp);
		cw_add_receive_statistic_internal(STAT_END_ELEMENT, space_usec);
	}

	/* Set state to indicate we are inside a tone. */
	cw_receive_state = RS_IN_TONE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state ->%d", cw_receive_state);

	return CW_SUCCESS;
}





/**
   \brief Analyze and identify a tone

   Analyses a tone using the ranges provided by the low level timing
   parameters.  On success, it returns true and sends back either a dot or
   a dash in representation.  On error, it returns false with errno set to
   ENOENT if the tone is not recognizable as either a dot or a dash,
   and sets the receive state to one of the error states, depending on
   the tone length passed in.

   Note; for adaptive timing, the tone should _always_ be recognized as
   a dot or a dash, because the ranges will have been set to cover 0 to
   INT_MAX.

   \param element_usec
   \param representation

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_identify_tone_internal(int element_usec, char *representation)
{
	/* Synchronize low level timings if required */
	cw_sync_parameters_internal(generator);

	/* If the timing was, within tolerance, a dot, return dot to the caller.  */
	if (element_usec >= cw_dot_range_minimum
	    && element_usec <= cw_dot_range_maximum) {

		*representation = CW_DOT_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* Do the same for a dash. */
	if (element_usec >= cw_dash_range_minimum
	    && element_usec <= cw_dash_range_maximum) {

		*representation = CW_DASH_REPRESENTATION;
		return CW_SUCCESS;
	}

	/* This element is not a dot or a dash, so we have an error case.
	   Depending on the timestamp difference, we pick which of the
	   error states to move to, and move to it.  The comparison is
	   against the expected end-of-char delay.  If it's larger, then
	   fix at word error, otherwise settle on char error.

	   Note that we should never reach here for adaptive timing receive. */
	cw_receive_state = element_usec > cw_eoc_range_maximum
		? RS_ERR_WORD : RS_ERR_CHAR;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state ->%d", cw_receive_state);

	/* Return ENOENT to the caller. */
	errno = ENOENT;
	return CW_FAILURE;
}





/**
   \brief Update adaptive tracking data

   Function updates the averages of dot and dash lengths, and recalculates
   the adaptive threshold for the next receive tone.

   \param element_usec
   \param element
*/
void cw_receive_update_adaptive_tracking_internal(int element_usec, char element)
{
	/* We are not going to tolerate being called in fixed speed mode. */
	if (!cw_is_adaptive_receive_enabled) {
		return;
	}

	/* We will update the information held for either dots or dashes.
	   Which we pick depends only on what the representation of the
	   character was identified as earlier. */
	if (element == CW_DOT_REPRESENTATION) {
		cw_update_adaptive_average_internal(&cw_dot_tracking, element_usec);
	}
	else if (element == CW_DASH_REPRESENTATION) {
		cw_update_adaptive_average_internal(&cw_dash_tracking, element_usec);
	}

	/* Recalculate the adaptive threshold from the values currently
	   held in the moving averages.  The threshold is calculated as
	   (avg dash length - avg dot length) / 2 + avg dot_length. */
	int average_dot = cw_get_adaptive_average_internal(&cw_dot_tracking);
	int average_dash = cw_get_adaptive_average_internal(&cw_dash_tracking);
	cw_adaptive_receive_threshold = (average_dash - average_dot) / 2
		+ average_dot;

	/* Resynchronize the low level timing data following recalculation.
	   If the resultant recalculated speed is outside the limits,
	   clamp the speed to the limit value and recalculate again.

	   Resetting the speed directly really means unsetting adaptive mode,
	   resyncing to calculate the new threshold, which unfortunately
	   recalculates everything else according to fixed speed; so, we
	   then have to reset adaptive and resyncing one more time, to get
	   all other timing parameters back to where they should be. */
	cw_is_in_sync = false;
	cw_sync_parameters_internal (generator);
	if (cw_receive_speed < CW_SPEED_MIN || cw_receive_speed > CW_SPEED_MAX) {
		cw_receive_speed = cw_receive_speed < CW_SPEED_MIN
			? CW_SPEED_MIN : CW_SPEED_MAX;
		cw_is_adaptive_receive_enabled = false;
		cw_is_in_sync = false;
		cw_sync_parameters_internal (generator);
		cw_is_adaptive_receive_enabled = true;
		cw_is_in_sync = false;
		cw_sync_parameters_internal (generator);
	}

	return;
}





/**
   Called on the end of a receive tone.  If the timestamp is NULL, the
   current time is used.  On success, the routine adds a dot or dash to
   the receive representation buffer, and returns true.  On error, it
   returns false, with errno set to ERANGE if the call was not preceded by
   a cw_start_receive_tone call, EINVAL if the timestamp passed in is not
   valid, ENOENT if the tone length was out of bounds for the permissible
   dot and dash lengths and fixed speed receiving is selected, ENOMEM if
   the representation buffer is full, or EAGAIN if the tone was shorter
   than the threshold for noise and was therefore ignored.

   \param timestamp

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_end_receive_tone(const struct timeval *timestamp)
{
	/* The receive state is expected to be inside a tone. */
	if (cw_receive_state != RS_IN_TONE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* Take a safe copy of the current end timestamp, in case we need
	   to put it back if we decide this tone is really just noise. */
	struct timeval saved_end_timestamp = cw_rr_end_timestamp;

	/* Save the timestamp passed in, or get one. */
	if (!cw_timestamp_validate_internal(timestamp, &cw_rr_end_timestamp)) {
		return CW_FAILURE;
	}

	/* Compare the timestamps to determine the length of the tone. */
	int element_usec = cw_timestamp_compare_internal(&cw_rr_start_timestamp,
							 &cw_rr_end_timestamp);

	/* If the tone length is shorter than any noise canceling threshold
	   that has been set, then ignore this tone.  This means reverting
	   to the state before the call to cw_start_receive_tone.  Now, by
	   rights, we should use an extra state, RS_IN_FIRST_TONE, say, so
	   that we know whether to go back to the idle state, or to after
	   tone.  But to make things a touch simpler, here we can just look
	   at the current receive buffer pointer. If it's zero, we came from
	   idle, otherwise we came from after tone. */
	if (cw_noise_spike_threshold > 0
	    && element_usec <= cw_noise_spike_threshold) {
		cw_receive_state = cw_rr_current == 0 ? RS_IDLE : RS_AFTER_TONE;

		/* Put the end tone timestamp back to how it was when we
		   came in to the routine. */
		cw_rr_end_timestamp = saved_end_timestamp;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state ->%d", cw_receive_state);

		errno = EAGAIN;
		return CW_FAILURE;
	}

	char representation;
	/* At this point, we have to make a decision about the element
	   just received.  We'll use a routine that compares ranges to
	   tell us what it thinks this element is.  If it can't decide,
	   it will hand us back an error which we return to the caller.
	   Otherwise, it returns a character, dot or dash, for us to buffer. */
	int status = cw_receive_identify_tone_internal(element_usec, &representation);
	if (!status) {
		return CW_FAILURE;
	}

	/* Update the averaging buffers so that the adaptive tracking of
	   received Morse speed stays up to date.  But only do this if we
	   have set adaptive receiving; don't fiddle about trying to track
	   for fixed speed receive. */
	if (cw_is_adaptive_receive_enabled) {
		cw_receive_update_adaptive_tracking_internal(element_usec, representation);
	}

	/* Update dot and dash timing statistics.  It may seem odd to do
	   this after calling cw_receive_update_adaptive_tracking_internal(),
	   rather than before, as this function changes the ideal values we're
	   measuring against.  But if we're on a speed change slope, the
	   adaptive tracking smoothing will cause the ideals to lag the
	   observed speeds.  So by doing this here, we can at least
	   ameliorate this effect, if not eliminate it. */
	if (representation == CW_DOT_REPRESENTATION) {
		cw_add_receive_statistic_internal(STAT_DOT, element_usec);
	} else {
		cw_add_receive_statistic_internal(STAT_DASH, element_usec);
	}

	/* Add the representation character to the receive buffer. */
	cw_receive_representation_buffer[cw_rr_current++] = representation;

	/* We just added a representation to the receive buffer.  If it's
	   full, then we have to do something, even though it's unlikely.
	   What we'll do is make a unilateral declaration that if we get
	   this far, we go to end-of-char error state automatically. */
	if (cw_rr_current == RECEIVE_CAPACITY - 1) {
		cw_receive_state = RS_ERR_CHAR;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state ->%d", cw_receive_state);

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* All is well.  Move to the more normal after-tone state. */
	cw_receive_state = RS_AFTER_TONE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state ->%d", cw_receive_state);

	return CW_SUCCESS;
}





/**
   \brief Add dot or dash to receive representation buffer

   Function adds either a dot or a dash to the receive representation
   buffer.  If the \p timestamp is NULL, the current timestamp is used.
   The receive state is updated as if we had just received a call to
   cw_end_receive_tone().

   \param timestamp
   \param element

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
 */
int cw_receive_add_element_internal(const struct timeval *timestamp,
				    char element)
{
	/* The receive state is expected to be idle or after a tone in
	   order to use this routine. */
	if (cw_receive_state != RS_IDLE && cw_receive_state != RS_AFTER_TONE) {
		errno = ERANGE;
		return CW_FAILURE;
	}

	/* This routine functions as if we have just seen a tone end, yet
	   without really seeing a tone start.  To keep timing information
	   for routines that come later, we need to make sure that the end
	   of tone timestamp is set here.  This is because the receive
	   representation routine looks at the time since the last end of
	   tone to determine whether we are at the end of a word, or just
	   at the end of a character.  It doesn't matter that the start of
	   tone timestamp is never set - this is just for timing the tone
	   length, and we don't need to do that since we've already been
	   told whether this is a dot or a dash. */
	if (!cw_timestamp_validate_internal(timestamp, &cw_rr_end_timestamp)) {
		return CW_FAILURE;
	}

	/* Add the element to the receive representation buffer. */
	cw_receive_representation_buffer[cw_rr_current++] = element;

	/* We just added an element to the receive buffer.  As above, if
	   it's full, then we have to do something, even though it's
	   unlikely to actually be full. */
	if (cw_rr_current == RECEIVE_CAPACITY - 1) {
		cw_receive_state = RS_ERR_CHAR;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state ->%d", cw_receive_state);

		errno = ENOMEM;
		return CW_FAILURE;
	}

	/* Since we effectively just saw the end of a tone, move to
	   the after-tone state. */
	cw_receive_state = RS_AFTER_TONE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state ->%d", cw_receive_state);

	return CW_SUCCESS;
}





/**
   \brief Add a dot to the receive representation buffer.

   If the timestamp is NULL, the current timestamp is used.  These routines
   are for callers that have already determined whether a dot or dash was
   received by a method other than calling the routines cw_start_receive_tone
   and cw_end_receive_tone.  On success, the relevant element is added to
   the receive representation buffer.  On error, the routines return false,
   with errno set to ERANGE if preceded by a cw_start_receive_tone call
   with no matching cw_end_receive_tone or if an error condition currently
   exists within the receive buffer, or ENOMEM if the receive representation
   buffer is full.

   \param timestamp
*/
int cw_receive_buffer_dot(const struct timeval *timestamp)
{
	return cw_receive_add_element_internal(timestamp, CW_DOT_REPRESENTATION);
}





/**
   \brief Add a dash to the receive representation buffer.

   See documentation of cw_receive_buffer_dot() for more information

   \param timestamp
*/
int cw_receive_buffer_dash(const struct timeval *timestamp)
{
	return cw_receive_add_element_internal(timestamp, CW_DASH_REPRESENTATION);
}





/**
   \brief Get the current buffered representation from the receive buffer

   On success, the function returns true, and fills in representation with the
   contents of the current representation buffer.  On error, it returns false,
   with errno set to ERANGE if not preceded by a cw_end_receive_tone call,
   a prior successful cw_receive_representation call, or a prior
   cw_receive_buffer_dot or cw_receive_buffer_dash, EINVAL if the timestamp
   passed in is invalid, or EAGAIN if the call is made too early to determine
   whether a complete representation has yet been placed in the buffer
   (that is, less than the inter-character gap period elapsed since the last
   cw_end_receive_tone or cw_receive_buffer_dot/dash call).  is_end_of_word
   indicates that the delay after the last tone received is longer that the
   inter-word gap, and is_error indicates that the representation was
   terminated by an error condition.

   \param timestamp
   \param representation
   \param is_end_of_word
   \param is_error

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_representation(const struct timeval *timestamp,
			      char *representation, bool *is_end_of_word,
			      bool *is_error)
{


	/* If the the receive state indicates that we have in our possession
	   a completed representation at the end of word, just [re-]return it. */
	if (cw_receive_state == RS_END_WORD || cw_receive_state == RS_ERR_WORD) {
		if (is_end_of_word) {
			*is_end_of_word = true;
		}
		if (is_error) {
			*is_error = (cw_receive_state == RS_ERR_WORD);
		}
		*representation = '\0';
		strncat(representation, cw_receive_representation_buffer, cw_rr_current);
		return CW_SUCCESS;
	}


	/* If the receive state is also not end-of-char, and also not after
	   a tone, then we are idle or in a tone; in these cases, we return
	   ERANGE. */
	if (cw_receive_state != RS_AFTER_TONE
	    && cw_receive_state != RS_END_CHAR
	    && cw_receive_state != RS_ERR_CHAR) {

		errno = ERANGE;
		return CW_FAILURE;
	}

	/* We now know the state is after a tone, or end-of-char, perhaps
	   with error.  For all three of these cases, we're going to
	   [re-]compare the timestamp with the end of tone timestamp.
	   This could mean that in the case of end-of-char, we revise
	   our opinion on later calls to end-of-word. This is correct,
	   since it models reality. */

	/* If we weren't supplied with one, get the current timestamp
	   for comparison against the latest end timestamp. */
	struct timeval now_timestamp;
	if (!cw_timestamp_validate_internal(timestamp, &now_timestamp)) {
		return CW_FAILURE;
	}

	/* Now we need to compare the timestamps to determine the length
	   of the inter-tone gap. */
	int space_usec = cw_timestamp_compare_internal(&cw_rr_end_timestamp,
						       &now_timestamp);

	/* Synchronize low level timings if required */
	cw_sync_parameters_internal(generator);

	/* If the timing was, within tolerance, a character space, then
	   that is what we'll call it.  In this case, we complete the
	   representation and return it. */
	if (space_usec >= cw_eoc_range_minimum
	    && space_usec <= cw_eoc_range_maximum) {

		/* If state is after tone, we can validly move at this
		   point to end of char.  If it's not, then we're at end
		   char or at end char with error already, so leave it.
		   On moving, update timing statistics for an identified
		   end of character. */
		if (cw_receive_state == RS_AFTER_TONE) {
			cw_add_receive_statistic_internal(STAT_END_CHARACTER, space_usec);
			cw_receive_state = RS_END_CHAR;
		}

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state ->%d", cw_receive_state);

		/* Return the representation buffered. */
		if (is_end_of_word) {
			*is_end_of_word = false;
		}
		if (is_error) {
			*is_error = (cw_receive_state == RS_ERR_CHAR);
		}
		*representation = '\0';
		strncat(representation, cw_receive_representation_buffer, cw_rr_current);
		return CW_SUCCESS;
	}

	/* If the timing indicated a word space, again we complete the
	   representation and return it.  In this case, we also need to
	   inform the client that this looked like the end of a word, not
	   just a character.  And, we don't care about the maximum period,
	   only that it exceeds the low end of the range. */
	if (space_usec > cw_eoc_range_maximum) {
		/* In this case, we have a transition to an end of word
		   case.  If we were sat in an error case, we need to move
		   to the correct end of word state, otherwise, at after
		   tone, we go safely to the non-error end of word. */
		cw_receive_state = cw_receive_state == RS_ERR_CHAR
			? RS_ERR_WORD : RS_END_WORD;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
			      "libcw: receive state ->%d", cw_receive_state);

		/* Return the representation buffered. */
		if (is_end_of_word) {
			*is_end_of_word = true;
		}
		if (is_error) {
			*is_error = (cw_receive_state == RS_ERR_WORD);
		}
		*representation = '\0';
		strncat(representation, cw_receive_representation_buffer, cw_rr_current);
		return CW_SUCCESS;
	}

	/* If none of these conditions holds, then we cannot yet make a
	   judgement on what we have in the buffer, so return EAGAIN. */
	errno = EAGAIN;
	return CW_FAILURE;
}





/**
   \brief Get a current character

   Returns the current buffered character from the representation buffer.
   On success, the function returns true, and fills char *c with the contents
   of the current representation buffer, translated into a character.  On
   error, it returns false, with errno set to ERANGE if not preceded by a
   cw_end_receive_tone call, a prior successful cw_receive_character
   call, or a cw_receive_buffer_dot or cw_receive_buffer dash call, EINVAL
   if the timestamp passed in is invalid, or EAGAIN if the call is made too
   early to determine whether a complete character has yet been placed in the
   buffer (that is, less than the inter-character gap period elapsed since
   the last cw_end_receive_tone or cw_receive_buffer_dot/dash call).
   is_end_of_word indicates that the delay after the last tone received is
   longer that the inter-word gap, and is_error indicates that the character
   was terminated by an error condition.
*/
int cw_receive_character(const struct timeval *timestamp,
			 char *c, bool *is_end_of_word, bool *is_error)
{
	bool end_of_word, error;
	char representation[RECEIVE_CAPACITY + 1];

	/* See if we can obtain a representation from the receive routines. */
	int status = cw_receive_representation (timestamp, representation,
						&end_of_word, &error);
	if (!status) {
		return CW_FAILURE;
	}

	/* Look up the representation using the lookup functions. */
	char character = cw_representation_to_character_internal(representation);
	if (!character) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	/* If we got this far, all is well, so return what we uncovered. */
	if (c) {
		*c = character;
	}
	if (is_end_of_word) {
		*is_end_of_word = end_of_word;
	}
	if (is_error) {
		*is_error = error;
	}
	return CW_SUCCESS;
}





/**
   \brief Clear receive representation buffer

   Clears the receive representation buffer to receive tones again.
   This routine must be called after successful, or terminating,
   cw_receive_representation() or cw_receive_character() calls, to
   clear the states and prepare the buffer to receive more tones.
*/
void cw_clear_receive_buffer(void)
{
	cw_rr_current = 0;
	cw_receive_state = RS_IDLE;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state ->%d", cw_receive_state);

	return;
}





/**
   \brief Get the number of entries the receive buffer can accommodate

   The maximum number of character written out by cw_receive_representation()
   is the capacity + 1, the extra character being used for the terminating
   NUL.
*/
int cw_get_receive_buffer_capacity(void)
{
	return RECEIVE_CAPACITY;
}





/**
   \brief Get the number of elements currently pending in the receive buffer
*/
int cw_get_receive_buffer_length(void)
{
	return cw_rr_current;
}





/**
   \brief Clear receive data

   Clear the receive representation buffer, statistics, and any retained
   receive state.  This function is suitable for calling from an application
   exit handler.
*/
void cw_reset_receive(void)
{
	cw_rr_current = 0;
	cw_receive_state = RS_IDLE;

	cw_reset_receive_statistics ();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_RECEIVE_STATES, CW_DEBUG_INFO,
		      "libcw: receive state ->%d (reset)", cw_receive_state);

	return;
}





/* ******************************************************************** */
/*                        Section:Iambic keyer                          */
/* ******************************************************************** */





/* Iambic keyer status.  The keyer functions maintain the current known
   state of the paddles, and latch false-to-true transitions while busy,
   to form the iambic effect.  For Curtis mode B, the keyer also latches
   any point where both paddle states are true at the same time. */
static volatile bool cw_ik_dot_paddle = false,      /* Dot paddle state */
	cw_ik_dash_paddle = false,     /* Dash paddle state */
	cw_ik_dot_latch = false,       /* Dot false->true latch */
	cw_ik_dash_latch = false,      /* Dash false->true latch */
	cw_ik_curtis_b_latch = false;  /* Curtis Dot&&Dash latch */

/* Iambic keyer "Curtis" mode A/B selector.  Mode A and mode B timings
   differ slightly, and some people have a preference for one or the other.
   Mode A is a bit less timing-critical, so we'll make that the default. */
static volatile bool cw_ik_curtis_mode_b = false;





/**
   \brief Enable iambic Curtis mode B

   Normally, the iambic keying functions will emulate Curtis 8044 Keyer
   mode A.  In this mode, when both paddles are pressed together, the
   last dot or dash being sent on release is completed, and nothing else
   is sent. In mode B, when both paddles are pressed together, the last
   dot or dash being sent on release is completed, then an opposite
   element is also sent. Some operators prefer mode B, but timing is
   more critical in this mode. The default mode is Curtis mode A.
*/
void cw_enable_iambic_curtis_mode_b(void)
{
	cw_ik_curtis_mode_b = true;
	return;
}





/**
   See documentation of cw_enable_iambic_curtis_mode_b() for more information
*/
void cw_disable_iambic_curtis_mode_b(void)
{
	cw_ik_curtis_mode_b = false;
	return;
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
static volatile enum {
	KS_IDLE,
	KS_IN_DOT_A,
	KS_IN_DASH_A,
	KS_AFTER_DOT_A,
	KS_AFTER_DASH_A,
	KS_IN_DOT_B,
	KS_IN_DASH_B,
	KS_AFTER_DOT_B,
	KS_AFTER_DASH_B
} cw_keyer_state = KS_IDLE;


/**
   \brief Update state of generic key, update state of iambic keyer, queue tone
*/
int cw_keyer_update_internal(void)
{
	if (lock) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_INTERNAL, CW_DEBUG_ERROR,
			      "libcw: lock in thread %ld", (long) pthread_self());
		return CW_FAILURE;
	}
	lock = true;

	/* Synchronize low level timing parameters if required. */
	cw_sync_parameters_internal(generator);

	/* Decide what to do based on the current state. */
	switch (cw_keyer_state) {
		/* Ignore calls if our state is idle. */
	case KS_IDLE:
		lock = false;
		return CW_SUCCESS;

		/* If we were in a dot, turn off tones and begin the
		   after-dot delay.  Do much the same if we are in a dash.
		   No routine status checks are made since we are in a
		   signal handler, and can't readily return error codes
		   to the client. */
	case KS_IN_DOT_A:
	case KS_IN_DOT_B:
		cw_key_iambic_keyer_generate_internal(generator, CW_KEY_STATE_OPEN, cw_end_of_ele_delay);
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
			      "libcw: cw_keyer_state: KS_IN_DOT -> KS_AFTER_DOT");
		cw_keyer_state = cw_keyer_state == KS_IN_DOT_A
			? KS_AFTER_DOT_A : KS_AFTER_DOT_B;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
			      "libcw: keyer ->%d", cw_keyer_state);
		break;

	case KS_IN_DASH_A:
	case KS_IN_DASH_B:
		cw_key_iambic_keyer_generate_internal(generator, CW_KEY_STATE_OPEN, cw_end_of_ele_delay);
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
			      "libcw: cw_keyer_state: KS_IN_DASH -> KS_AFTER_DASH");
		cw_keyer_state = cw_keyer_state == KS_IN_DASH_A
			? KS_AFTER_DASH_A : KS_AFTER_DASH_B;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
			      "libcw: keyer ->%d", cw_keyer_state);
		break;

		/* If we have just finished a dot or a dash and its
		   post-element delay, then reset the latches as
		   appropriate.  Next, if in a _B state, go straight to
		   the opposite element state.  If in an _A state, check
		   the latch states; if the opposite latch is set true,
		   then do the iambic thing and alternate dots and dashes.
		   If the same latch is true, repeat.  And if nothing is
		   true, then revert to idling. */
	case KS_AFTER_DOT_A:
	case KS_AFTER_DOT_B:
		if (!cw_ik_dot_paddle) {
			cw_ik_dot_latch = false;
		}

		if (cw_keyer_state == KS_AFTER_DOT_B) {
			cw_key_iambic_keyer_generate_internal(generator, CW_KEY_STATE_CLOSED, cw_send_dash_length);
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
				      "libcw: cw_keyer_state: KS_AFTER_DOT -> KS_IN_DASH_A");
			cw_keyer_state = KS_IN_DASH_A;
		} else if (cw_ik_dash_latch) {
			cw_key_iambic_keyer_generate_internal(generator, CW_KEY_STATE_CLOSED, cw_send_dash_length);
			if (cw_ik_curtis_b_latch){
				cw_ik_curtis_b_latch = false;
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
					      "libcw: cw_keyer_state: KS_AFTER_DOT -> KS_IN_DASH_B");
				cw_keyer_state = KS_IN_DASH_B;
			} else {
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
					      "libcw: cw_keyer_state: KS_AFTER_DOT -> KS_IN_DASH_A");
				cw_keyer_state = KS_IN_DASH_A;
			}
		} else if (cw_ik_dot_latch) {
			cw_key_iambic_keyer_generate_internal(generator, CW_KEY_STATE_CLOSED, cw_send_dot_length);
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
				      "libcw: cw_keyer_state: KS_AFTER_DOT -> KS_IN_DOT_A");
			cw_keyer_state = KS_IN_DOT_A;
		} else {
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
				      "libcw: cw_keyer_state: KS_AFTER_DOT -> KS_IDLE");
			cw_keyer_state = KS_IDLE;
			//cw_finalization_schedule_internal();
		}

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
			      "libcw: keyer ->%d", cw_keyer_state);
		break;

	case KS_AFTER_DASH_A:
	case KS_AFTER_DASH_B:
		if (!cw_ik_dash_paddle) {
			cw_ik_dash_latch = false;
		}
		if (cw_keyer_state == KS_AFTER_DASH_B) {
			cw_key_iambic_keyer_generate_internal(generator, CW_KEY_STATE_CLOSED, cw_send_dot_length);
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
				      "libcw: cw_keyer_state: KS_AFTER_DASH_B -> IN_DOT_A");
			cw_keyer_state = KS_IN_DOT_A;
		} else if (cw_ik_dot_latch) {
			cw_key_iambic_keyer_generate_internal(generator, CW_KEY_STATE_CLOSED, cw_send_dot_length);
			if (cw_ik_curtis_b_latch) {
				cw_ik_curtis_b_latch = false;
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
					      "libcw: cw_keyer_state: KS_AFTER_DASH -> KS_IN_DOT_B");
				cw_keyer_state = KS_IN_DOT_B;
			} else {
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
					      "libcw: cw_keyer_state: KS_AFTER_DASH -> KS_IN_DOT_A");
				cw_keyer_state = KS_IN_DOT_A;
			}
		} else if (cw_ik_dash_latch) {
			cw_key_iambic_keyer_generate_internal(generator, CW_KEY_STATE_CLOSED, cw_send_dash_length);
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
				      "libcw: cw_keyer_state: KS_AFTER_DASH -> KS_IN_DASH_A");
			cw_keyer_state = KS_IN_DASH_A;
		} else {
			cw_keyer_state = KS_IDLE;
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_KEYER_STATES, CW_DEBUG_DEBUG,
				      "libcw: cw_keyer_state: KS_AFTER_DASH -> KS_STATE");
			//cw_finalization_schedule_internal();
		}

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
			      "libcw: keyer ->%d", cw_keyer_state);
		break;
	}
	lock = false;
	return CW_SUCCESS;
}





/**
   \brief Inform about changed state of keyer paddles

   Function informs the internal keyer states that the keyer paddles have
   changed state.  The new paddle states are recorded, and if either
   transition from false to true, paddle latches, for iambic functions,
   are also set. On success, the routine returns true.  On error, it returns
   false, with errno set to EBUSY if the tone queue or straight key are
   using the sound card, console speaker, or keying system.

   If appropriate, this routine starts the keyer functions sending the
   relevant element.  Element send and timing occurs in the background,
   so this routine returns almost immediately.  See cw_keyer_element_wait
   and cw_keyer_wait for details about how to check the current status of
   iambic keyer background processing.

   \param dot_paddle_state
   \param dash_paddle_state

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_notify_keyer_paddle_event(int dot_paddle_state,
				 int dash_paddle_state)
{
	/* If the tone queue or the straight key are busy, this is going to
	   conflict with our use of the sound card, console sounder, and
	   keying system.  So return an error status in this case. */
	// if (cw_is_straight_key_busy() || cw_is_tone_busy()) {
	if (0) {
		errno = EBUSY;
		return CW_FAILURE;
	}

	/* Clean up and save the paddle states passed in. */
	cw_ik_dot_paddle = (dot_paddle_state != 0);
	cw_ik_dash_paddle = (dash_paddle_state != 0);

	/* Update the paddle latches if either paddle goes true.
	   The latches are checked in the signal handler, so if the paddles
	   go back to false during this element, the item still gets
	   actioned.  The signal handler is also responsible for clearing
	   down the latches. */
	if (cw_ik_dot_paddle) {
		cw_ik_dot_latch = true;
	}
	if (cw_ik_dash_paddle) {
		cw_ik_dash_latch = true;
	}

	/* If in Curtis mode B, make a special check for both paddles true
	   at the same time.  This flag is checked by the signal handler,
	   to determine whether to add mode B trailing timing elements. */
	if (cw_ik_curtis_mode_b && cw_ik_dot_paddle && cw_ik_dash_paddle) {
		cw_ik_curtis_b_latch = true;
	}

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      "libcw: keyer paddles %d,%d, latches %d,%d, curtis_b %d",
		      cw_ik_dot_paddle, cw_ik_dash_paddle,
		      cw_ik_dot_latch, cw_ik_dash_latch, cw_ik_curtis_b_latch);

	/* If the current state is idle, give the state process a nudge. */
	if (cw_keyer_state == KS_IDLE) {
		if (cw_ik_dot_paddle) {
			/* Pretend we just finished a dash. */
			cw_keyer_state = cw_ik_curtis_b_latch
				? KS_AFTER_DASH_B : KS_AFTER_DASH_A;

			if (!cw_keyer_update_internal()) {
				/* just try again, once */
				usleep(1000);
				cw_keyer_update_internal();
			}
		} else if (cw_ik_dash_paddle) {
			/* Pretend we just finished a dot. */
			cw_keyer_state = cw_ik_curtis_b_latch
				? KS_AFTER_DOT_B : KS_AFTER_DOT_A;

			if (!cw_keyer_update_internal()) {
				/* just try again, once */
				usleep(1000);
				cw_keyer_update_internal();
			}
		} else {
			;
		}
	} else {
		;
	}

	return CW_SUCCESS;
}





/**
   \brief Change state of dot paddle

   Alter the state of just one of the two iambic keyer paddles.
   The other paddle state of the paddle pair remains unchanged.

   See cw_keyer_paddle_event() for details of iambic keyer background
   processing, and how to check its status.

   \param dot_paddle_state
*/
int cw_notify_keyer_dot_paddle_event(int dot_paddle_state)
{
	return cw_notify_keyer_paddle_event(dot_paddle_state, cw_ik_dash_paddle);
}





/**
   See documentation of cw_notify_keyer_dot_paddle_event() for more information
*/
int cw_notify_keyer_dash_paddle_event(int dash_paddle_state)
{
	return cw_notify_keyer_paddle_event(cw_ik_dot_paddle, dash_paddle_state);
}





/**
   \brief Get the current saved states of the two paddles

   \param dot_paddle_state
   \param dash_paddle_state
*/
void cw_get_keyer_paddles(int *dot_paddle_state, int *dash_paddle_state)
{
	if (dot_paddle_state) {
		*dot_paddle_state = cw_ik_dot_paddle;
	}
	if (dash_paddle_state) {
		*dash_paddle_state = cw_ik_dash_paddle;
	}
	return;
}





/**
   \brief Get the current states of paddle latches

   Function returns the current saved states of the two paddle latches.
   A paddle latches is set to true when the paddle state becomes true,
   and is cleared if the paddle state is false when the element finishes
   sending.

   \param dot_paddle_latch_state
   \param dash_paddle_latch_state
*/
void cw_get_keyer_paddle_latches(int *dot_paddle_latch_state,
				 int *dash_paddle_latch_state)
{
	if (dot_paddle_latch_state) {
		*dot_paddle_latch_state = cw_ik_dot_latch;
	}
	if (dash_paddle_latch_state) {
		*dash_paddle_latch_state = cw_ik_dash_latch;
	}
	return;
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
   \brief Wait for end of element from the keyer

   Waits until the end of the current element, dot or dash, from the keyer.

   On error the function returns CW_FAILURE, with errno set to
   EDEADLK if SIGALRM is blocked.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_keyer_element(void)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* First wait for the state to move to idle (or just do nothing
	   if it's not), or to one of the after- states. */
	while (cw_keyer_state != KS_IDLE
	       && cw_keyer_state != KS_AFTER_DOT_A
	       && cw_keyer_state != KS_AFTER_DOT_B
	       && cw_keyer_state != KS_AFTER_DASH_A
	       && cw_keyer_state != KS_AFTER_DASH_B) {

		cw_signal_wait_internal();
	}

	/* Now wait for the state to move to idle (unless it is, or was,
	   already), or one of the in- states, at which point we know
	   we're actually at the end of the element we were in when we
	   entered this routine. */
	while (cw_keyer_state != KS_IDLE
	       && cw_keyer_state != KS_IN_DOT_A
	       && cw_keyer_state != KS_IN_DOT_B
	       && cw_keyer_state != KS_IN_DASH_A
	       && cw_keyer_state != KS_IN_DASH_B) {

		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the current keyer cycle to complete

   The routine returns CW_SUCCESS on success.  On error, it returns
   CW_FAILURE, with errno set to EDEADLK if SIGALRM is blocked or if
   either paddle state is true.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_keyer(void)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Check that neither paddle is true; if either is, then the signal
	   cycle is going to continue forever, and we'll never return from
	   this routine. */
	if (cw_ik_dot_paddle || cw_ik_dash_paddle) {
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait for the keyer state to go idle. */
	while (cw_keyer_state != KS_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Reset keyer data

   Clear all keyer latches and paddle states, return to Curtis 8044 Keyer
   mode A, and return to silence.  This function is suitable for calling
   from an application exit handler.
*/
void cw_reset_keyer(void)
{
	cw_ik_dot_paddle = false;
	cw_ik_dash_paddle = false;
	cw_ik_dot_latch = false;
	cw_ik_dash_latch = false;
	cw_ik_curtis_b_latch = false;
	cw_ik_curtis_mode_b = false;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      "libcw: assigning to cw_keyer_state %d -> 0 (KS_IDLE)", cw_keyer_state);
	cw_keyer_state = KS_IDLE;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_generator_silence_internal(generator);
	cw_finalization_schedule_internal();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_KEYER_STATES, CW_DEBUG_INFO,
		      "libcw: keyer ->%d (reset)", cw_keyer_state);

	return;
}





/* ******************************************************************** */
/*                        Section:Straight key                          */
/* ******************************************************************** */





/* Period of constant tone generation after which we need another timeout,
   to ensure that the soundcard doesn't run out of data. */
static const int STRAIGHT_KEY_TIMEOUT = 500000;

/* Straight key status; just a key-up or key-down indication. */
static volatile bool cw_sk_key_state = CW_KEY_STATE_OPEN;





#if 0 /* unused */
/**
   \brief Generate a tone while straight key is down

   Soundcard tone data is only buffered to last about a second on each
   cw_generate_sound_internal() call, and holding down the straight key
   for longer than this could cause a soundcard data underrun.  To guard
   against this, a timeout is generated every half-second or so while the
   straight key is down.  The timeout generates a chunk of sound data for
   the soundcard.
*/
void cw_straight_key_clock_internal(void)
{
	if (cw_sk_key_state == CW_KEY_STATE_CLOSED) {
		/* Generate a quantum of tone data, and request another
		   timeout. */
		// cw_generate_sound_internal();
		cw_timer_run_with_handler_internal(STRAIGHT_KEY_TIMEOUT, NULL);
	}

	return;
}
#endif





/**
   \brief Inform the library that the straight key has changed state

   This routine returns CW_SUCCESS on success.  On error, it returns CW_FAILURE,
   with errno set to EBUSY if the tone queue or iambic keyer are using
   the sound card, console speaker, or keying control system.  If
   \p key_state indicates no change of state, the call is ignored.

   \param key_state - state of straight key
*/
int cw_notify_straight_key_event(int key_state)
{
	/* If the tone queue or the keyer are busy, we can't use the
	   sound card, console sounder, or the key control system. */
	// if (cw_is_tone_busy() || cw_is_keyer_busy()) {
	if (0) {
		errno = EBUSY;
		return CW_FAILURE;
	}

	/* If the key state did not change, ignore the call. */
	if (cw_sk_key_state != key_state) {

		/* Save the new key state. */
		cw_sk_key_state = key_state;

		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STRAIGHT_KEY_STATES, CW_DEBUG_INFO,
			      "libcw: straight key state ->%s", cw_sk_key_state == CW_KEY_STATE_CLOSED ? "DOWN" : "UP");

		/* Do tones and keying, and set up timeouts and soundcard
		   activities to match the new key state. */
		if (cw_sk_key_state == CW_KEY_STATE_CLOSED) {
			cw_key_straight_key_generate_internal(generator, CW_KEY_STATE_CLOSED);
		} else {
			cw_key_straight_key_generate_internal(generator, CW_KEY_STATE_OPEN);

			/* Indicate that we have finished with timeouts,
			   and also with the soundcard too.  There's no way
			   of knowing when straight keying is completed,
			   so the only thing we can do here is to schedule
			   release on each key up event.   */
			//cw_finalization_schedule_internal();
		}
	}

	return CW_SUCCESS;
}





/**
   \brief Get saved state of straight key

   Returns the current saved state of the straight key.

   \return true if the key is down
   \return false if the key up
*/
int cw_get_straight_key_state(void)
{
	return cw_sk_key_state;
}





/**
   \brief Check if the straight key is busy

   This routine is just a pseudonym for cw_get_straight_key_state(),
   and exists to fill a hole in the API naming conventions.

   \return true if the straight key is busy
   \return false if the straight key is not busy
*/
bool cw_is_straight_key_busy(void)
{
	return cw_sk_key_state;
}





/**
   \brief Clear the straight key state, and return to silence

   This function is suitable for calling from an application exit handler.
*/
void cw_reset_straight_key(void)
{
	cw_sk_key_state = CW_KEY_STATE_OPEN;

	/* Silence sound and stop any background soundcard tone generation. */
	cw_generator_silence_internal(generator);
	//cw_finalization_schedule_internal();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_STRAIGHT_KEY_STATES, CW_DEBUG_INFO,
		      "libcw: straight key state ->UP (reset)");

	return;
}





/* ******************************************************************** */
/*                     Section:Generator - generic                      */
/* ******************************************************************** */





/**
   \brief Get a readable label of current audio system

   The function returns one of following strings:
   None, Null, Console, OSS, ALSA, PulseAudio, Soundcard

   \return audio system's label
*/
const char *cw_generator_get_audio_system_label(void)
{
	return cw_get_audio_system_label(generator->audio_system);
}





/**
   \brief Create new generator

   Allocate memory for new generator data structure, set up default values
   of some of the generator's properties.
   The function does not start the generator (generator does not produce
   a sound), you have to use cw_generator_start() for this.

   \param audio_system - audio system to be used by the generator (console, OSS, ALSA, soundcard, see "enum cw_audio_systems")
   \param device - name of audio device to be used; if NULL then library will use default device.
*/
int cw_generator_new(int audio_system, const char *device)
{
	generator = (cw_gen_t *) malloc(sizeof (cw_gen_t));
	if (!generator) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: malloc()");
		return CW_FAILURE;
	}

	generator->tq = &cw_tone_queue;
	cw_tone_queue_init_internal(generator->tq);

	generator->audio_device = NULL;
	//generator->audio_system = audio_system;
	generator->audio_device_is_open = false;
	generator->dev_raw_sink = -1;
	generator->send_speed = CW_SPEED_INITIAL,
	generator->frequency = CW_FREQUENCY_INITIAL;
	generator->volume_percent = CW_VOLUME_INITIAL;
	generator->volume_abs = (generator->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
	generator->gap = CW_GAP_INITIAL;
	generator->buffer = NULL;
	generator->buffer_n_samples = -1;

	generator->oss_version.x = -1;
	generator->oss_version.y = -1;
	generator->oss_version.z = -1;

	generator->client.name = (char *) NULL;

	generator->tone_slope.length_usecs = CW_AUDIO_SLOPE_USECS;
	generator->tone_slope.shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
	generator->tone_slope.amplitudes = NULL;
	generator->tone_slope.n_amplitudes = 0;

#ifdef LIBCW_WITH_PULSEAUDIO
	generator->pa_data.s = NULL;

	generator->pa_data.ba.prebuf    = (uint32_t) -1;
	generator->pa_data.ba.tlength   = (uint32_t) -1;
	generator->pa_data.ba.minreq    = (uint32_t) -1;
	generator->pa_data.ba.maxlength = (uint32_t) -1;
	generator->pa_data.ba.fragsize  = (uint32_t) -1;
#endif

	generator->open_device = NULL;
	generator->close_device = NULL;
	generator->write = NULL;

	pthread_attr_init(&generator->thread.attr);
	pthread_attr_setdetachstate(&generator->thread.attr, PTHREAD_CREATE_DETACHED);

	int rv = cw_generator_new_open_internal(generator, audio_system, device);
	if (rv == CW_FAILURE) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: failed to open audio device for audio system '%s' and device '%s'", cw_get_audio_system_label(audio_system), device);
		cw_generator_delete();
		return CW_FAILURE;
	}

	if (audio_system == CW_AUDIO_NULL
	    || audio_system == CW_AUDIO_CONSOLE) {

		; /* the two types of audio output don't require audio buffer */
	} else {
		generator->buffer = (cw_sample_t *) malloc(generator->buffer_n_samples * sizeof (cw_sample_t));
		if (!generator->buffer) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: malloc()");
			cw_generator_delete();
			return CW_FAILURE;
		}
	}

	/* Set slope that late, because it uses value of sample rate.
	   The sample rate value is set in
	   cw_generator_new_open_internal(). */
	rv = cw_generator_set_tone_slope(generator, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, CW_AUDIO_SLOPE_USECS);
	if (rv == CW_FAILURE) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: failed to set slope");
		cw_generator_delete();
		return CW_FAILURE;
	}

	cw_sigalrm_install_top_level_handler_internal();

	return CW_SUCCESS;
}





/*
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
int cw_generator_new_open_internal(cw_gen_t *gen, int audio_system, const char *device)
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
   \brief Deallocate generator

   Deallocate/destroy generator data structure created with call
   to cw_generator_new(). You can't start nor use the generator
   after the call to this function.
*/
void cw_generator_delete(void)
{
	if (generator) {

		if (generator->generate) {
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
				      "libcw: you forgot to call cw_generator_stop()");
			cw_generator_stop();
		}

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

		if (generator->close_device) {
			generator->close_device(generator);
		} else {
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: WARNING: null function pointer, something went wrong");
		}

		pthread_attr_destroy(&generator->thread.attr);

		if (generator->client.name) {
			free(generator->client.name);
			generator->client.name = NULL;
		}

		if (generator->tone_slope.amplitudes) {
			free(generator->tone_slope.amplitudes);
			generator->tone_slope.amplitudes = NULL;
		}

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

	generator->generate = true;

	generator->client.thread_id = pthread_self();

	if (generator->audio_system == CW_AUDIO_NULL
	    || generator->audio_system == CW_AUDIO_CONSOLE
	    || generator->audio_system == CW_AUDIO_OSS
	    || generator->audio_system == CW_AUDIO_ALSA
	    || generator->audio_system == CW_AUDIO_PA) {

		int rv = pthread_create(&generator->thread.id, &generator->thread.attr,
					cw_generator_write_sine_wave_internal,
					(void *) generator);
		if (rv != 0) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: failed to create %s generator thread", cw_audio_system_labels[generator->audio_system]);
			return CW_FAILURE;
		} else {
			/* for some yet unknown reason you have to
			   put usleep() here, otherwise a generator
			   may work incorrectly */
			usleep(100000);
#ifdef LIBCW_WITH_DEV
			cw_dev_debug_print_generator_setup(generator);
#endif
			return CW_SUCCESS;
		}
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: unsupported audio system %d", generator->audio_system);
	}

	return CW_FAILURE;
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
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING,
			      "libcw: called the function for NULL generator");
		return;
	}

	cw_flush_tone_queue();

	cw_generator_silence_internal(generator);

	generator->generate = false;

	/* this is to wake up cw_signal_wait_internal() function
	   that may be waiting for signal in while() loop in thread
	   function; */
	pthread_kill(generator->thread.id, SIGALRM);

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
	int rv = pthread_kill(generator->thread.id, 0);
	if (rv == 0) {
		/* thread function didn't return yet; let's help it a bit */
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING, "libcw: EXIT: forcing exit of thread function");
		rv = pthread_kill(generator->thread.id, SIGKILL);
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING, "libcw: EXIT: pthread_kill() returns %d/%s", rv, strerror(rv));
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_INFO, "libcw: EXIT: seems that thread function exited voluntarily");
	}

	return;
}





/**
   \brief Write a constant sine wave to ALSA or OSS output

   \param arg - current generator (casted to (void *))

   \return NULL pointer
*/
void *cw_generator_write_sine_wave_internal(void *arg)
{
	cw_gen_t *gen = (cw_gen_t *) arg;

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

	while (gen->generate) {
		int q = cw_tone_queue_dequeue_internal(gen->tq, &tone);
		if (q == CW_TQ_STILL_EMPTY) {
			/* wait for signal from enqueue() function
			   informing that there appeared some tone
			   on tone queue */
			cw_signal_wait_internal();
			//usleep(CW_AUDIO_QUANTUM_USECS);
			continue;
		}

#ifdef LIBCW_WITH_DEV
		cw_debug_ev ((&cw_debug_object_ev), 0, tone.frequency ? CW_DEBUG_EVENT_TONE_HIGH : CW_DEBUG_EVENT_TONE_LOW);
#endif

		if (gen->audio_system == CW_AUDIO_NULL) {
			cw_null_write(gen, &tone);
		} else if (gen->audio_system == CW_AUDIO_CONSOLE) {
			cw_console_write(gen, &tone);
		} else {
			cw_soundcard_write_internal(gen, q, &tone);
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
		if (!cw_keyer_update_internal()) { /* TODO: follow this function, check if and when it needs to be called */
			/* just try again, once */
			usleep(1000);
			cw_keyer_update_internal();
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

   Calculate a fragment of sine wave, as many samples as can be
   fitted in generator's buffer. There will be gen->buffer_n_samples
   samples put into gen->buffer, starting from gen->buffer[0].

   The function takes into account all state variables from gen,
   so initial phase of new fragment of sine wave in the buffer matches
   ending phase of a sine wave generated in current call.

   \param gen - current generator

   \return position in buffer at which a last sample has been saved
*/
int cw_generator_calculate_sine_wave_internal(cw_gen_t *gen, cw_tone_t *tone)
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
		int amplitude = cw_generator_calculate_amplitude_internal(gen, tone);

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
   \brief Calculate value of a sample of sine wave

   \param gen - generator used to generate a sine wave

   \return value of a sample of sine wave, a non-negative number
*/
int cw_generator_calculate_amplitude_internal(cw_gen_t *gen, cw_tone_t *tone)
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
   of slope for every time. With the table at hand we can simply look
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

   There are three supported shapes of slopes:
   \li linear (the only one supported by libcw until version 4.1.1),
   \li raised cosine (supposedly the most desired shape),
   \li sine,
   \li rectangular.

   Use CW_TONE_SLOPE_SHAPE_* symbolic names as values of \p slope_shape.

   \param gen - current generator
   \param slope_shape - shape of slope: linear, raised cosine, sine
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

	 /* In theory we could reallocate the table every time the
	    function is called.  In practice the function may be most
	    often called when user changes volume of tone (and then
	    the function is called several times in a row, as volume
	    is changed in steps), and in such circumstances the size
	    of amplitudes table doesn't change.

	    So to save some time we do this check. */

	 if (gen->tone_slope.n_amplitudes != slope_n_samples) {
		 gen->tone_slope.amplitudes = realloc(gen->tone_slope.amplitudes, sizeof(float) * slope_n_samples);
		 if (!gen->tone_slope.amplitudes) {
			 cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
				       "libcw: realloc()");
			 return CW_FAILURE;
		 }
		 gen->tone_slope.n_amplitudes = slope_n_samples;
	 }

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
			 assert (0);
		 }
	 }

	 return CW_SUCCESS;
}





/* ******************************************************************** */
/*                         Section:Soundcard                            */
/* ******************************************************************** */





int cw_soundcard_write_internal(cw_gen_t *gen, int queue_state, cw_tone_t *tone)
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
			assert (0);
		}

		/* Length of a tone in samples:
		    - whole standard tone, from rising slope to falling
		      slope, or
		    - a part of longer, "forever" slope, either a fragment
		      being rising slope, or falling slope, or "no slopes"
		      fragment in between.
		   Either way - a length of dequeued tone, converted from
		   microseconds to samples. */
		tone->n_samples = gen->sample_rate / 100;
		tone->n_samples *= tone->usecs;
		tone->n_samples /= 10000;

		/* Length in samples of a single slope (rising or falling)
		   in standard tone of limited, known in advance length. */
		tone->slope_n_samples = ((gen->sample_rate / 100) * gen->tone_slope.length_usecs) / 10000;

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

		cw_generator_calculate_sine_wave_internal(gen, tone);
		if (tone->sub_stop + 1 == gen->buffer_n_samples) {

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





/* ******************************************************************** */
/*                         Section:Utilities                            */
/* ******************************************************************** */





/**
   \brief Convert microseconds to struct timespec

   Function fills fields of struct timespec \p t (seconds and nanoseconds)
   based on value of \p usecs.
   \p usecs should be non-negative.

   This function is just a simple wrapper for few lines of code.

   \param t - pointer to existing struct to be filled with data
   \param usecs - value to convert to timespec
*/
void cw_usecs_to_timespec_internal(struct timespec *t, int usecs)
{
	assert (usecs >= 0);
	assert (t);

	int sec = usecs / 1000000;
	int usec = usecs % 1000000;

	t->tv_sec = sec;
	t->tv_nsec = usec * 1000;

	return;
}





/**
   \brief Sleep for period of time specified by given timespec

   Function sleeps for given amount of seconds and nanoseconds, as
   specified by \p n.

   The function uses nanosleep(), and can handle incoming SIGALRM signals
   that cause regular nanosleep() to return. The function calls nanosleep()
   until all time specified by \p n has elapsed.

   The function may sleep a little longer than specified by \p n if it needs
   to spend some time handling SIGALRM signal. Other restrictions from
   nanosleep()'s man page also apply.

   \param n - period of time to sleep
*/
void cw_nanosleep_internal(struct timespec *n)
{
	struct timespec rem = { .tv_sec = n->tv_sec, .tv_nsec = n->tv_nsec };

	int rv = 0;
	do {
		struct timespec req = { .tv_sec = rem.tv_sec, .tv_nsec = rem.tv_nsec };
		//fprintf(stderr, " -- sleeping for %ld s, %ld ns\n", req.tv_sec, req.tv_nsec);
		rv = nanosleep(&req, &rem);
		if (rv) {
			//fprintf(stderr, " -- remains %ld s, %ld ns\n", rem.tv_sec, rem.tv_nsec);
		}
	} while (rv);

	return;
}




#if (defined(LIBCW_WITH_ALSA) || defined(LIBCW_WITH_PULSEAUDIO))
/**
   \brief Try to dynamically open shared library

   Function tries to open a shared library specified by \p name using
   dlopen() system function. On sucess, handle to open library is
   returned via \p handle.

   Name of the library should contain ".so" suffix, e.g.: "libasound.so.2",
   or "libpulse-simple.so".

   \param name - name of library to test
   \param handle - output argument, handle to open library

   \return true on success
   \return false otherwise
*/
bool cw_dlopen_internal(const char *name, void **handle)
{
	assert (name);

	dlerror();
	void *h = dlopen(name, RTLD_LAZY);
	char *e = dlerror();

	if (e) {
		cw_debug_msg (((&cw_debug_object_dev)), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: dlopen() fails for %s with error: %s", name, e);
		return false;
	} else {
		*handle = h;

		cw_debug_msg (((&cw_debug_object_dev)), CW_DEBUG_STDLIB, CW_DEBUG_DEBUG,
			      "libcw: dlopen() succeeds for %s", name);
		return true;
	}
}
#endif





#ifdef LIBCW_STANDALONE





/* ******************************************************************** */
/*             Section:main() function for testing purposes             */
/* ******************************************************************** */





typedef bool (*predicate_t)(const char *device);
static void main_helper(int audio_system, const char *name, const char *device, predicate_t predicate);





/* for stand-alone testing */
int main(void)
{
	main_helper(CW_AUDIO_OSS,     "OSS",         CW_DEFAULT_OSS_DEVICE,       cw_is_oss_possible);
	//main_helper(CW_AUDIO_ALSA,    "ALSA",        CW_DEFAULT_ALSA_DEVICE,      cw_is_alsa_possible);
	//main_helper(CW_AUDIO_PA,      "PulseAudio",  CW_DEFAULT_PA_DEVICE,        cw_is_pa_possible);
	//main_helper(CW_AUDIO_NULL,    "Null",        CW_DEFAULT_NULL_DEVICE,      cw_is_null_possible);
	//main_helper(CW_AUDIO_CONSOLE, "console",     CW_DEFAULT_CONSOLE_DEVICE,   cw_is_console_possible);
	sleep(4);

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
			cw_set_send_speed(12);
			cw_generator_start();

			//cw_send_string("abcdefghijklmnopqrstuvwyz0123456789");
			cw_send_string("eish ");
			cw_wait_for_tone_queue();

			cw_generator_set_tone_slope(generator, CW_TONE_SLOPE_SHAPE_LINEAR, -1);
			cw_send_string("eish ");
			cw_wait_for_tone_queue();

			cw_generator_set_tone_slope(generator, CW_TONE_SLOPE_SHAPE_SINE, -1);
			cw_send_string("eish ");
			cw_wait_for_tone_queue();

			cw_send_string("two");
			cw_wait_for_tone_queue();

			cw_send_string("three");
			cw_wait_for_tone_queue();

			cw_wait_for_tone_queue();
			cw_generator_stop();
			cw_generator_delete();
		} else {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
				      "libcw: can't create %s generator", name);
		}
	} else {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: %s output is not available", name);
	}
}





#endif // #if LIBCW_STANDALONE





#ifdef LIBCW_UNIT_TESTS





/* ******************************************************************** */
/*             Section:Unit tests for internal functions                */
/* ******************************************************************** */





/* Unit tests for internal functions (and also some public functions)
   defined in libcw.c.

   For unit tests of library's public interfaces see libcwtest.c. */

#include <stdio.h>
#include <assert.h>

static unsigned int test_cw_representation_to_hash_internal(void);

static unsigned int test_cw_tone_queue_init_internal(void);
static unsigned int test_cw_get_tone_queue_capacity(void);
static unsigned int test_cw_tone_queue_prev_index_internal(void);
static unsigned int test_cw_tone_queue_next_index_internal(void);
static unsigned int test_cw_tone_queue_length_internal(void);
static unsigned int test_cw_tone_queue_enqueue_internal(void);
static unsigned int test_cw_tone_queue_dequeue_internal(void);

static unsigned int test_cw_usecs_to_timespec_internal(void);


typedef unsigned int (*cw_test_function_t)(void);

static cw_test_function_t cw_unit_tests[] = {
	test_cw_representation_to_hash_internal,

	test_cw_tone_queue_init_internal,
	test_cw_get_tone_queue_capacity,
	test_cw_tone_queue_prev_index_internal,
	test_cw_tone_queue_next_index_internal,
	test_cw_tone_queue_length_internal,
	test_cw_tone_queue_enqueue_internal,
	test_cw_tone_queue_dequeue_internal,

	test_cw_usecs_to_timespec_internal,
	NULL
};




int main(void)
{
	fprintf(stderr, "libcw unit tests facility\n");

	int i = 0;
	while (cw_unit_tests[i]) {
		cw_unit_tests[i]();
		i++;
	}

	/* "make check" facility requires this message to be
	   printed on stdout; don't localize it */
	fprintf(stdout, "test result: success\n\n");

	return 0;
}





#define REPRESENTATION_LEN 7
/* for maximum length of 7, there should be 254 items:
   2^1 + 2^2 + 2^3 + ... * 2^7 */
#define REPRESENTATION_TABLE_SIZE ((2 << (REPRESENTATION_LEN + 1)) - 1)





unsigned int test_cw_representation_to_hash_internal(void)
{
	fprintf(stderr, "\ttesting cw_representation_to_hash_internal()... ");


	char input[REPRESENTATION_TABLE_SIZE][REPRESENTATION_LEN + 1];

	/* build table of all valid representations ("valid" as in "build
	   from dash and dot, no longer than REPRESENTATION_LEN"). */
	long int i = 0;
	for (unsigned int len = 0; len < REPRESENTATION_LEN; len++) {
		for (unsigned int binary_representation = 0; binary_representation < (2 << len); binary_representation++) {
			for (unsigned int bit_pos = 0; bit_pos <= len; bit_pos++) {
				unsigned int bit = binary_representation & (1 << bit_pos);
				input[i][bit_pos] = bit ? '-' : '.';
				// fprintf(stderr, "rep = %x, bit pos = %d, bit = %d\n", binary_representation, bit_pos, bit);
			}

			input[i][len + 1] = '\0';
			// fprintf(stderr, "input[%d] = \"%s\"\n", i, input[i]);
			// fprintf(stderr, "%s\n", input[i]);
			i++;

		}
	}

	/* compute hash for every valid representation */
	for (int j = 0; j < i; j++) {
		unsigned int hash = cw_representation_to_hash_internal(input[j]);
		assert(hash);
	}

	fprintf(stderr, "OK\n");

	return 0;
}






static cw_tone_queue_t test_tone_queue;




static unsigned int test_cw_tone_queue_init_internal(void)
{
	fprintf(stderr, "\ttesting cw_tone_queue_init_internal()...        ");
	int rv = cw_tone_queue_init_internal(&test_tone_queue);

	assert (rv == CW_SUCCESS);

	/* this is preparation for other tests that will be performed on the tq. */
	test_tone_queue.state = QS_BUSY;

	fprintf(stderr, "OK\n");

	return 0;
}





static unsigned int test_cw_get_tone_queue_capacity(void)
{
	fprintf(stderr, "\ttesting cw_get_tone_queue_capacity()...         ");

	int n = cw_get_tone_queue_capacity();
	assert (n == CW_TONE_QUEUE_CAPACITY - 1);

	fprintf(stderr, "OK\n");

	return 0;
}





static unsigned int test_cw_tone_queue_prev_index_internal(void)
{
	fprintf(stderr, "\ttesting cw_tone_queue_prev_index_internal()...  ");

	struct {
		int arg;
		int expected;
	} input[] = {
		{ CW_TONE_QUEUE_CAPACITY - 4, CW_TONE_QUEUE_CAPACITY - 5 },
		{ CW_TONE_QUEUE_CAPACITY - 3, CW_TONE_QUEUE_CAPACITY - 4 },
		{ CW_TONE_QUEUE_CAPACITY - 2, CW_TONE_QUEUE_CAPACITY - 3 },
		{ CW_TONE_QUEUE_CAPACITY - 1, CW_TONE_QUEUE_CAPACITY - 2 },
		{                          0, CW_TONE_QUEUE_CAPACITY - 1 },
		{                          1,                          0 },
		{                          2,                          1 },
		{                          3,                          2 },
		{                          4,                          3 },

		{ -1000, -1000 } /* guard */
	};

	int i = 0;
	while (input[i].arg != -1000) {
		int prev = cw_tone_queue_prev_index_internal(input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, prev, input[i].expected);
		assert (prev == input[i].expected);
		i++;
	}

	fprintf(stderr, "OK\n");

	return 0;
}





static unsigned int test_cw_tone_queue_next_index_internal(void)
{
	fprintf(stderr, "\ttesting cw_tone_queue_next_index_internal()...  ");

	struct {
		int arg;
		int expected;
	} input[] = {
		{ CW_TONE_QUEUE_CAPACITY - 5, CW_TONE_QUEUE_CAPACITY - 4 },
		{ CW_TONE_QUEUE_CAPACITY - 4, CW_TONE_QUEUE_CAPACITY - 3 },
		{ CW_TONE_QUEUE_CAPACITY - 3, CW_TONE_QUEUE_CAPACITY - 2 },
		{ CW_TONE_QUEUE_CAPACITY - 2, CW_TONE_QUEUE_CAPACITY - 1 },
		{ CW_TONE_QUEUE_CAPACITY - 1,                          0 },
		{                          0,                          1 },
		{                          1,                          2 },
		{                          2,                          3 },
		{                          3,                          4 },

		{ -1000, -1000 } /* guard */
	};

	int i = 0;
	while (input[i].arg != -1000) {
		int next = cw_tone_queue_next_index_internal(input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, next, input[i].expected);
		assert (next == input[i].expected);
		i++;
	}

	fprintf(stderr, "OK\n");

	return 0;
}





static unsigned int test_cw_tone_queue_length_internal(void)
{
	fprintf(stderr, "\ttesting cw_tone_queue_length_internal()...      ");

	/* This is just some code copied from implementation of
	   'enqueue' function. I don't use 'enqueue' function itself
	   because it's not tested yet. I get rid of all the other
	   code from the 'enqueue' function and use only the essential
	   part to manually add elements to list, and then to check
	   length of the list. */

	cw_tone_t tone;
	tone.usecs = 1;
	tone.frequency = 1;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;

	for (uint32_t i = 0; i < (uint32_t) cw_get_tone_queue_capacity(); i++) {

		/* Get the new value of the queue tail index. */
		int new_tq_tail = cw_tone_queue_next_index_internal(test_tone_queue.tail);

		/* If the new value is bumping against the head index, then
		   the queue is currently full. */
		if (new_tq_tail == test_tone_queue.head) {
			assert (0);
		}

		/* Set the new tail index, and enqueue the new tone. */
		test_tone_queue.tail = new_tq_tail;
		test_tone_queue.queue[test_tone_queue.tail].usecs = tone.usecs;
		test_tone_queue.queue[test_tone_queue.tail].frequency = tone.frequency;
		test_tone_queue.queue[test_tone_queue.tail].slope_mode = tone.slope_mode;

		/* OK, added a tone, ready to measure length of the queue. */
		uint32_t len = cw_tone_queue_length_internal(&test_tone_queue);
		assert (len == i + 1);
	}

	/* Empty the queue, by setting the head to the tail. */
	test_tone_queue.head = test_tone_queue.tail;

	fprintf(stderr, "OK\n");

	return 0;
}





static unsigned int test_cw_tone_queue_enqueue_internal(void)
{
	fprintf(stderr, "\ttesting cw_tone_queue_enqueue_internal()...     ");

	/* At this point cw_tone_queue_length_internal() should be
	   tested, so we can use it to verify correctness of 'enqueue'
	   function. */

	cw_tone_t tone;
	tone.usecs = 1;
	tone.frequency = 1;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;;

	for (uint32_t i = 0; i < (uint32_t) cw_get_tone_queue_capacity(); i++) {

		/* This tests for potential problems with function call. */
		int rv = cw_tone_queue_enqueue_internal(&test_tone_queue, &tone);
		assert (rv);

		/* This tests for correctness of working of the 'enqueue' function. */
		uint32_t len = cw_tone_queue_length_internal(&test_tone_queue);
		assert (len == i + 1);
	}


	/* Try adding a tone to full tq. */
	/* This tests for potential problems with function call.
	   Enqueueing should fail when the queue is full. */
	int rv = cw_tone_queue_enqueue_internal(&test_tone_queue, &tone);
	assert (rv == CW_FAILURE);

	/* This tests for correctness of working of the 'enqueue'
	   function.  Full tq should not grow beyond its capacity. */
	uint32_t len = cw_tone_queue_length_internal(&test_tone_queue);
	assert (len == (uint32_t) cw_get_tone_queue_capacity());


	fprintf(stderr, "OK\n");

	return 0;
}





static unsigned int test_cw_tone_queue_dequeue_internal(void)
{
	fprintf(stderr, "\ttesting cw_tone_queue_dequeue_internal()...     ");

	/* At this point cw_tone_queue_length_internal() should be
	   tested, so we can use it to verify correctness of 'deenqueue'
	   function.

	   test_tone_queue should be completely filled after tests of
	   'enqueue' function. */

	/* Just to be sure. */
	int capacity = cw_get_tone_queue_capacity();
	uint32_t len_full = cw_tone_queue_length_internal(&test_tone_queue);
	assert (len_full == (uint32_t) capacity);


	cw_tone_t tone;
	tone.slope_mode = CW_SLOPE_MODE_NO_SLOPES;;

	for (uint32_t i = capacity; i > 0; i--) {

		/* This tests for potential problems with function call. */
		int rv = cw_tone_queue_dequeue_internal(&test_tone_queue, &tone);
		assert (rv);

		/* This tests for correctness of working of the 'dequeue' function. */
		uint32_t len = cw_tone_queue_length_internal(&test_tone_queue);
		assert (len == i - 1);
	}


	/* Try removing a tone from empty queue. */
	/* This tests for potential problems with function call.
	   Dequeueing should fail when the queue is empty. */
	int rv = cw_tone_queue_dequeue_internal(&test_tone_queue, &tone);
	assert (rv == CW_FAILURE);

	/* This tests for correctness of working of the 'dequeue'
	   function.  Empty tq should stay empty. */
	uint32_t len = cw_tone_queue_length_internal(&test_tone_queue);
	assert (len == 0);


	fprintf(stderr, "OK\n");

	return 0;
}





unsigned int test_cw_usecs_to_timespec_internal(void)
{
	fprintf(stderr, "\ttesting cw_usecs_to_timespec_internal()...      ");

	struct {
		int input;
		struct timespec t;
	} input_data[] = {
		{           0,    {   0,             0 }},
		{     1000000,    {   1,             0 }},
		{     1000004,    {   1,          4000 }},
		{    15000350,    {  15,        350000 }},
		{          73,    {   0,         73000 }},
		{          -1,    {   0,             0 }},
	};

	int i = 0;
	while (input_data[i].input != -1) {
		struct timespec result = { .tv_sec = 0, .tv_nsec = 0 };
		cw_usecs_to_timespec_internal(&result, input_data[i].input);
#if 0
		fprintf(stderr, "input = %d usecs, output = %ld.%ld\n",
			input_data[i].input, (long) result.tv_sec, (long) result.tv_nsec);
#endif
		assert(result.tv_sec == input_data[i].t.tv_sec);
		assert(result.tv_nsec == input_data[i].t.tv_nsec);

		i++;
	}

	fprintf(stderr, "OK\n");

	return 0;
}





#endif /* #ifdef LIBCW_UNIT_TESTS */

