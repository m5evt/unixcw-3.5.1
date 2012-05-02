/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2012  Kamil Ignacak (acerion@wp.pl)
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
#define _XOPEN_SOURCE 600 /* signaction() + SA_RESTART */

#include "config.h"

#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "libcw.h"


/*---------------------------------------------------------------------*/
/*  Unit tests                                                         */
/*---------------------------------------------------------------------*/

/*
 * cw_self_test_admin()
 */
static int cw_self_test_admin(void)
{
	/* Test the cw_version and cw_license functions. */
	fprintf(stderr, "libcw: version %d.%d\n", cw_version() >> 16, cw_version() & 0xff);
	cw_license();

	/* Test getting and setting of debug flags. */
	unsigned int flags = cw_get_debug_flags();
	int failures = 0;
	for (unsigned int i = 0; i <= CW_DEBUG_MASK; i++) {
		cw_set_debug_flags(i);
		if (cw_get_debug_flags() != i) {
			fprintf(stderr, "libcw: ERROR: cw_get/set_debug flags\n");
			failures++;
			break;
		}
	}
	cw_set_debug_flags(flags);
	fprintf(stderr, "libcw: cw_get/set_debug flags tests complete\n");

	return failures;
}


/*
 * cw_self_test_limits()
 */
static int
cw_self_test_limits (void)
{
  int failures = 0;
  int cw_min_speed, cw_max_speed, cw_min_frequency, cw_max_frequency,
      cw_min_volume, cw_max_volume, cw_min_gap, cw_max_gap,
      cw_min_tolerance, cw_max_tolerance, cw_min_weighting, cw_max_weighting;

  /*
   * Ensure that we can obtain the main parameter limits.
   */
  cw_get_speed_limits (&cw_min_speed, &cw_max_speed);
  printf ("libcw: cw_get_speed_limits=%d,%d\n",
          cw_min_speed, cw_max_speed);

  cw_get_frequency_limits (&cw_min_frequency, &cw_max_frequency);
  printf ("libcw: cw_get_frequency_limits=%d,%d\n",
          cw_min_frequency, cw_max_frequency);

  cw_get_volume_limits (&cw_min_volume, &cw_max_volume);
  printf ("libcw: cw_get_volume_limits=%d,%d\n",
          cw_min_volume, cw_max_volume);

  cw_get_gap_limits (&cw_min_gap, &cw_max_gap);
  printf ("libcw: cw_get_gap_limits=%d,%d\n",
          cw_min_gap, cw_max_gap);

  cw_get_tolerance_limits (&cw_min_tolerance, &cw_max_tolerance);
  printf ("libcw: cw_get_tolerance_limits=%d,%d\n",
          cw_min_tolerance, cw_max_tolerance);

  cw_get_weighting_limits (&cw_min_weighting, &cw_max_weighting);
  printf ("libcw: cw_get_weighting_limits=%d,%d\n",
          cw_min_weighting, cw_max_weighting);

  printf ("libcw: cw_get_limits tests complete\n");
  return failures;
}


/*
 * cw_self_test_ranges()
 */
static int
cw_self_test_ranges (void)
{
  int failures = 0;
  int status, index, cw_min, cw_max;
  int txdot_usecs, txdash_usecs, end_of_element_usecs, end_of_character_usecs,
      end_of_word_usecs, additional_usecs, adjustment_usecs,
      rxdot_usecs, rxdash_usecs, dot_min_usecs, dot_max_usecs, dash_min_usecs,
      dash_max_usecs, end_of_element_min_usecs, end_of_element_max_usecs,
      end_of_element_ideal_usecs, end_of_character_min_usecs,
      end_of_character_max_usecs, end_of_character_ideal_usecs,
      adaptive_threshold;

  /* Print default low level timing values. */
  cw_reset_send_receive_parameters ();
  cw_get_send_parameters (&txdot_usecs, &txdash_usecs,
                          &end_of_element_usecs, &end_of_character_usecs,
                          &end_of_word_usecs, &additional_usecs,
                          &adjustment_usecs);
  printf ("libcw: cw_get_send_parameters\n"
          "libcw:     %d, %d, %d, %d, %d, %d, %d\n",
          txdot_usecs, txdash_usecs, end_of_element_usecs,
          end_of_character_usecs,end_of_word_usecs, additional_usecs,
          adjustment_usecs);

  cw_get_receive_parameters (&rxdot_usecs, &rxdash_usecs,
                             &dot_min_usecs, &dot_max_usecs,
                             &dash_min_usecs, &dash_max_usecs,
                             &end_of_element_min_usecs,
                             &end_of_element_max_usecs,
                             &end_of_element_ideal_usecs,
                             &end_of_character_min_usecs,
                             &end_of_character_max_usecs,
                             &end_of_character_ideal_usecs,
                             &adaptive_threshold);
  printf ("libcw: cw_get_receive_parameters\n"
          "libcw:     %d, %d, %d, %d, %d, %d, %d, %d\n"
          "libcw:     %d, %d, %d, %d, %d\n",
          rxdot_usecs, rxdash_usecs, dot_min_usecs, dot_max_usecs,
          dash_min_usecs, dash_max_usecs, end_of_element_min_usecs,
          end_of_element_max_usecs, end_of_element_ideal_usecs,
          end_of_character_min_usecs, end_of_character_max_usecs,
          end_of_character_ideal_usecs, adaptive_threshold);

  /*
   * Set the main parameters to out-of-range values, and through their
   * complete valid ranges.
   */
  cw_get_speed_limits (&cw_min, &cw_max);
  errno = 0;
  status = cw_set_send_speed (cw_min - 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_send_speed(cw_min_speed-1)\n");
      failures++;
    }
  errno = 0;
  status = cw_set_send_speed (cw_max + 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_send_speed(cw_max_speed+1)\n");
      failures++;
    }
  for (index = cw_min; index <= cw_max; index++)
    {
      cw_set_send_speed (index);
      if (cw_get_send_speed () != index)
        {
          printf ("libcw: ERROR: cw_get/set_send_speed\n");
          failures++;
          break;
        }
    }
  errno = 0;
  status = cw_set_receive_speed (cw_min - 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_receive_speed(cw_min_speed-1)\n");
      failures++;
    }
  errno = 0;
  status = cw_set_receive_speed (cw_max + 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_receive_speed(cw_max_speed+1)\n");
      failures++;
    }
  for (index = cw_min; index <= cw_max; index++)
    {
      cw_set_receive_speed (index);
      if (cw_get_receive_speed () != index)
        {
          printf ("libcw: ERROR: cw_get/set_receive_speed\n");
          failures++;
          break;
        }
    }
  printf ("libcw: cw_set/get_send/receive_speed tests complete\n");

  cw_get_frequency_limits (&cw_min, &cw_max);
  errno = 0;
  status = cw_set_frequency (cw_min - 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_frequency(cw_min_frequency-1)\n");
      failures++;
    }
  errno = 0;
  status = cw_set_frequency (cw_max + 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_frequency(cw_max_frequency+1)\n");
      failures++;
    }
  for (index = cw_min; index <= cw_max; index++)
    {
      cw_set_frequency (index);
      if (cw_get_frequency () != index)
        {
          printf ("libcw: ERROR: cw_get/set_frequency\n");
          failures++;
          break;
        }
    }
  printf ("libcw: cw_set/get_frequency tests complete\n");

  cw_get_volume_limits (&cw_min, &cw_max);
  errno = 0;
  status = cw_set_volume (cw_min - 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_volume(cw_min_volume-1)\n");
      failures++;
    }
  errno = 0;
  status = cw_set_volume (cw_max + 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_volume(cw_max_volume+1)\n");
      failures++;
    }
  for (index = cw_min; index <= cw_max; index++)
    {
      cw_set_volume (index);
      if (cw_get_volume () != index)
        {
          printf ("libcw: ERROR: cw_get/set_volume\n");
          failures++;
          break;
        }
    }
  printf ("libcw: cw_set/get_volume tests complete\n");

  cw_get_gap_limits (&cw_min, &cw_max);
  errno = 0;
  status = cw_set_gap (cw_min - 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_gap(cw_min_gap-1)\n");
      failures++;
    }
  errno = 0;
  status = cw_set_gap (cw_max + 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_gap(cw_max_gap+1)\n");
      failures++;
    }
  for (index = cw_min; index <= cw_max; index++)
    {
      cw_set_gap (index);
      if (cw_get_gap () != index)
        {
          printf ("libcw: ERROR: cw_get/set_gap\n");
          failures++;
          break;
        }
    }
  printf ("libcw: cw_set/get_gap tests complete\n");

  cw_get_tolerance_limits (&cw_min, &cw_max);
  errno = 0;
  status = cw_set_tolerance (cw_min - 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_tolerance(cw_min_tolerance-1)\n");
      failures++;
    }
  errno = 0;
  status = cw_set_tolerance (cw_max + 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_tolerance(cw_max_tolerance+1)\n");
      failures++;
    }
  for (index = cw_min; index <= cw_max; index++)
    {
      cw_set_tolerance (index);
      if (cw_get_tolerance () != index)
        {
          printf ("libcw: ERROR: cw_get/set_tolerance\n");
          failures++;
          break;
        }
    }
  printf ("libcw: cw_set/get_tolerance tests complete\n");

  cw_get_weighting_limits (&cw_min, &cw_max);
  errno = 0;
  status = cw_set_weighting (cw_min - 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_weighting(cw_min_weighting-1)\n");
      failures++;
    }
  errno = 0;
  status = cw_set_weighting (cw_max + 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_set_weighting(cw_max_weighting+1)\n");
      failures++;
    }
  for (index = cw_min; index <= cw_max; index++)
    {
      cw_set_weighting (index);
      if (cw_get_weighting () != index)
        {
          printf ("libcw: ERROR: cw_get/set_weighting\n");
          failures++;
          break;
        }
    }

  printf ("libcw: cw_set/get_weighting tests complete\n");
  return failures;
}


/*
 * cw_self_test_tone_parameters()
 */
static int
cw_self_test_tone_parameters (void)
{
  int failures = 0;
  int status, cw_min, cw_max;

  /*
   * Test the limits of the parameters to the tone queue routine.
   */
  cw_get_frequency_limits (&cw_min, &cw_max);
  errno = 0;
  status = cw_queue_tone (-1, cw_min);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_queue_tone(-1, cw_min_frequency)\n");
      failures++;
    }
  errno = 0;
  status = cw_queue_tone (0, cw_min - 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_queue_tone(0, cw_min_frequency-1)\n");
      failures++;
    }
  errno = 0;
  status = cw_queue_tone (0, cw_max + 1);
  if (status || errno != EINVAL)
    {
      printf ("libcw: ERROR: cw_queue_tone(0, cw_max_frequency+1)\n");
      failures++;
    }

  printf ("libcw: cw_queue_tone argument tests complete\n");
  return failures;
}


/*
 * cw_self_test_simple_tones()
 */
static int
cw_self_test_simple_tones (void)
{
  int failures = 0;
  int index;

  /*
   * Ensure we can generate a few simple tones, and wait for them to end.
   */
  cw_set_volume (70);
  if (!cw_queue_tone (100000, 4000)
      || !cw_queue_tone (100000, 2000)
      || !cw_queue_tone (100000, 1000))
    {
      printf ("libcw: ERROR: cw_queue_tone(10000, 4000|2000|1000)\n");
      failures++;
    }
  for (index = 0; index < 3; index++)
    {
      if (!cw_wait_for_tone ())
        {
          printf ("libcw: ERROR: cw_wait_for_tone()\n");
          failures++;
        }
    }
  if (cw_get_tone_queue_length () != 0)
    {
      printf ("libcw: ERROR: cw_get_tone_queue_length()\n");
      failures++;
    }

  if (!cw_queue_tone (100000, 1000)
      || !cw_queue_tone (100000, 500))
    {
      printf ("libcw: ERROR: cw_queue_tone(10000, 1000|500)\n");
      failures++;
    }
  if (!cw_wait_for_tone_queue ())
    {
      printf ("libcw: ERROR: cw_wait_for_tone_queue()\n");
      failures++;
    }

  printf ("libcw: cw_queue_tone single tone test complete\n");
  return failures;
}


/*
 * cw_self_test_complex_tones()
 */
static int
cw_self_test_complex_tones (void)
{
  int failures = 0;
  int index, cw_min, cw_max;

  /*
   * Run the complete range of tone generation, at 100Hz intervals, first up
   * the octaves, and then down.  If the queue fills, though it shouldn't
   * with this amount of data, then pause until it isn't so full.
   */
  cw_set_volume (70);
  cw_get_frequency_limits (&cw_min, &cw_max);
  for (index = cw_min; index < cw_max; index += 100)
    {
      while (cw_is_tone_queue_full ())
        {
          if (!cw_wait_for_tone ())
            {
              printf ("libcw: ERROR: cw_wait_for_tone()\n");
              failures++;
              break;
            }
        }
      if (!cw_queue_tone (10000, index))
        {
          printf ("libcw: ERROR: cw_queue_tone()\n");
          failures++;
          break;
        }
    }
  for (index = cw_max; index > cw_min; index -= 100)
    {
      while (cw_is_tone_queue_full ())
        {
          if (!cw_wait_for_tone ())
            {
              printf ("libcw: ERROR: cw_wait_for_tone()\n");
              failures++;
              break;
            }
        }
      if (!cw_queue_tone (10000, index))
        {
          printf ("libcw: ERROR: cw_queue_tone()\n");
          failures++;
          break;
        }
    }
  if (!cw_wait_for_tone_queue ())
    {
      printf ("libcw: ERROR: cw_wait_for_tone_queue()\n");
      failures++;
    }
  cw_queue_tone (0, 0);
  cw_wait_for_tone_queue ();

  printf ("libcw: cw_queue_tone/cw_wait_for_tone_queue tests complete\n");
  return failures;
}


/*
 * cw_self_test_tone_queue()
 */
static int
cw_self_test_tone_queue (void)
{
  int failures = 0;
  int index, status;

  /*
   * Test the tone queue manipulations, ensuring that we can fill the queue,
   * that it looks full when it is, and that we can flush it all again
   * afterwards, and recover.
   */
  cw_set_volume (70);
  printf ("libcw: cw_get_tone_queue_capacity=%d\n",
          cw_get_tone_queue_capacity ());
  printf ("libcw: empty cw_get_tone_queue_length=%d\n",
          cw_get_tone_queue_length ());

  index = 0;
  while (!cw_is_tone_queue_full ())
    cw_queue_tone (1000000, 100 + (index++ & 1) * 100);
  printf ("libcw: full cw_get_tone_queue_length=%d\n",
          cw_get_tone_queue_length ());

  errno = 0;
  status = cw_queue_tone (1000000, 100);
  if (status || errno != EAGAIN)
    {
      printf ("libcw: ERROR: full cw_queue_tone()\n");
      failures++;
    }

  cw_flush_tone_queue ();
  if (cw_get_tone_queue_length () > 0)
    {
      printf ("libcw: ERROR: cw_get_tone_queue_length()\n");
      failures++;
    }

  printf ("libcw: cw_flush_tone_queue/length/capacity tests complete\n");
  return failures;
}


/*
 * cw_self_test_volumes()
 */
static int
cw_self_test_volumes (void)
{
  int failures = 0;
  int index, cw_min, cw_max;

  /*
   * Fill the queue with short tones, then check that we can move the volume
   * through its entire range.  Flush the queue when complete.
   */
  cw_get_volume_limits (&cw_min, &cw_max);
  index = 0;
  while (!cw_is_tone_queue_full ())
    cw_queue_tone (100000, 800 + (index++ & 1) * 800);
  for (index = cw_max; index >= cw_min; index -= 10)
    {
      cw_wait_for_tone ();
      if (!cw_set_volume (index))
        {
          printf ("libcw: ERROR: cw_set_volume()\n");
          failures++;
          break;
        }
      if (cw_get_volume () != index)
        {
          printf ("libcw: ERROR: cw_get_volume()\n");
          failures++;
          break;
        }
      cw_wait_for_tone ();
    }
  for (index = cw_min; index <= cw_max; index += 10)
    {
      cw_wait_for_tone ();
      if (!cw_set_volume (index))
        {
          printf ("libcw: ERROR: cw_set_volume()\n");
          failures++;
          break;
        }
      if (cw_get_volume () != index)
        {
          printf ("libcw: ERROR: cw_get_volume()\n");
          failures++;
          break;
        }
      cw_wait_for_tone ();
    }
  cw_wait_for_tone ();
  cw_flush_tone_queue ();

  printf ("libcw: cw_set/get_volume tests complete\n");
  return failures;
}


/*
 * cw_self_test_lookups()
 */
static int
cw_self_test_lookups (void)
{
  int failures = 0;
  int index;
  char charlist[UCHAR_MAX + 1];

  /*
   * Collect and print out a list of characters in the main CW table.
   */
  printf ("libcw: cw_get_character_count %d\n", cw_get_character_count ());
  cw_list_characters (charlist);
  printf ("libcw: cw_list_characters\n"
          "libcw:     %s\n", charlist);

  /*
   * For each character, look up its representation, the look up each
   * representation in the opposite direction.
   */
  printf ("libcw: cw_get_maximum_representation_length %d\n",
          cw_get_maximum_representation_length ());
  for (index = 0; charlist[index] != '\0'; index++)
    {
      char c, representation[256];

      if (!cw_lookup_character (charlist[index], representation))
        {
          printf ("libcw: ERROR: cw_lookup_character()\n");
          failures++;
          break;
        }
      if (!cw_lookup_representation (representation, &c))
        {
          printf ("libcw: ERROR: cw_lookup_representation()\n");
          failures++;
          break;
        }
      if (charlist[index] != c)
        {
          printf ("libcw: ERROR: cw_lookup_() mapping wrong\n");
          failures++;
          break;
        }
    }

  printf ("libcw: cw list and lookup tests complete\n");
  return failures;
}


/*
 * cw_self_test_prosign_lookups()
 */
static int
cw_self_test_prosign_lookups (void)
{
  int failures = 0;
  int index;
  char charlist[UCHAR_MAX + 1];

  /*
   * Collect and print out a list of characters in the procedural signals
   * expansion table.
   */
  printf ("libcw: cw_get_procedural_character_count %d\n",
          cw_get_procedural_character_count ());
  cw_list_procedural_characters (charlist);
  printf ("libcw: cw_list_procedural_characters\n"
          "libcw:     %s\n", charlist);

  /*
   * For each character, look up its expansion and check for two or three
   * characters, and a true/false assignment to the display hint.
   */
  printf ("libcw: cw_get_maximum_procedural_expansion_length %d\n",
          cw_get_maximum_procedural_expansion_length ());
  for (index = 0; charlist[index] != '\0'; index++)
    {
      char expansion[256];
      int is_usually_expanded;

      is_usually_expanded = -1;
      if (!cw_lookup_procedural_character (charlist[index],
                                           expansion, &is_usually_expanded))
        {
          printf ("libcw: ERROR: cw_lookup_procedural_character()\n");
          failures++;
          break;
        }
      if ((strlen (expansion) != 2 && strlen (expansion) != 3)
          || is_usually_expanded == -1)
        {
          printf ("libcw: ERROR: cw_lookup_procedural_() mapping wrong\n");
          failures++;
          break;
        }
    }

  printf ("libcw: cw prosign list and lookup tests complete\n");
  return failures;
}


/*
 * cw_self_test_phonetic_lookups()
 */
static int
cw_self_test_phonetic_lookups (void)
{
  int failures = 0;
  int index;

  /*
   * For each ASCII character, look up its phonetic and check for a string
   * that start with this character, if alphabetic, and false otherwise.
   */
  printf ("libcw: cw_get_maximum_phonetic_length %d\n",
          cw_get_maximum_phonetic_length ());
  for (index = 0; index < UCHAR_MAX; index++)
    {
      int status;
      char phonetic[256];

      status = cw_lookup_phonetic ((char) index, phonetic);
      if (status != (isalpha (index) ? true : false))
        {
          printf ("libcw: ERROR: cw_lookup_phonetic()\n");
          failures++;
          break;
        }
      if (status)
        {
          if (phonetic[0] != toupper (index))
            {
              printf ("libcw: ERROR: cw_lookup_phonetic() mapping wrong\n");
              failures++;
              break;
            }
        }
    }

  printf ("libcw: cw phonetics lookup tests complete\n");
  return failures;
}


/*
 * cw_self_test_dot_dash()
 */
static int
cw_self_test_dot_dash (void)
{
  int failures = 0;

  /*
   * Send basic dot and dash using the library primitives.
   */
  if (!cw_send_dot ())
    {
      printf ("libcw: ERROR: cw_send_dot()\n");
      failures++;
    }
  if (!cw_send_dash ())
    {
      printf ("libcw: ERROR: cw_send_dash()\n");
      failures++;
    }
  if (!cw_send_character_space ())
    {
      printf ("libcw: ERROR: cw_send_character_space()\n");
      failures++;
    }
  if (!cw_send_word_space ())
    {
      printf ("libcw: ERROR: cw_send_word_space()\n");
      failures++;
    }
  cw_wait_for_tone_queue ();

  printf ("libcw: cw_send_dot/dash tests complete\n");
  return failures;
}


/*
 * cw_self_test_representations()
 */
static int
cw_self_test_representations (void)
{
  int failures = 0;

  /*
   * Check just a couple of basic representations, and send the valid ones
   * as tones.
   */
  if (!cw_check_representation (".-.-.-")
      || cw_check_representation ("INVALID"))
    {
      printf ("libcw: ERROR: cw_check_representation()\n");
      failures++;
    }
  if (!cw_send_representation_partial (".-.-.-"))
    {
      printf ("libcw: ERROR: cw_send_representation_partial()\n");
      failures++;
    }
  if (!cw_send_representation (".-.-.-"))
    {
      printf ("libcw: ERROR: valid cw_send_representation()\n");
      failures++;
    }
  if (cw_send_representation ("INVALID"))
    {
      printf ("libcw: ERROR: invalid cw_send_representation()\n");
      failures++;
    }
  cw_wait_for_tone_queue ();

  printf ("libcw: cw_send_representation tests complete\n");
  return failures;
}


/*
 * cw_self_test_characters()
 */
static int
cw_self_test_characters (void)
{
  int failures = 0;
  int index;
  char charlist[UCHAR_MAX + 1];

  /*
   * Check all the single characters we can, up to UCHAR_MAX.
   */
  cw_list_characters (charlist);
  for (index = 0; index < UCHAR_MAX; index++)
    {
      if (index == ' '
          || (index != 0 && strchr (charlist, toupper (index)) != NULL))
        {
          if (!cw_check_character (index))
            {
              printf ("libcw: ERROR: valid cw_check_character()\n");
              failures++;
              break;
            }
        }
      else
        {
          if (cw_check_character (index))
            {
              printf ("libcw: ERROR: invalid cw_check_character()\n");
              failures++;
              break;
            }
        }
    }


  /*
   * Check the whole charlist item as a single string, then check a known
   * invalid string.
   */
  cw_list_characters (charlist);
  if (!cw_check_string (charlist))
    {
      printf ("libcw: ERROR: cw_check_string()\n");
      failures++;
    }
  if (cw_check_string ("%INVALID%"))
    {
      printf ("libcw: ERROR: invalid cw_check_string()\n");
      failures++;
    }

  printf ("libcw: cw_check_character/string tests complete\n");
  return failures;
}


/*
 * cw_self_test_full_send()
 */
static int
cw_self_test_full_send (void)
{
  int failures = 0;
  int index;
  char charlist[UCHAR_MAX + 1];

  /*
   * Send all the characters from the charlist individually.
   */
  cw_list_characters (charlist);
  printf ("libcw: cw_send_character\n"
          "libcw:     ");
  for (index = 0; charlist[index] != '\0'; index++)
    {
      putchar (charlist[index]);
      fflush (stdout);
      if (!cw_send_character (charlist[index]))
        {
          printf ("libcw: ERROR: cw_send_character()\n");
          failures++;
        }
      cw_wait_for_tone_queue ();
    }
  putchar ('\n');
  if (cw_send_character (0))
    {
      printf ("libcw: ERROR: invalid cw_send_character()\n");
      failures++;
    }


  /* Now send the complete charlist as a single string. */
  printf ("libcw: cw_send_string\n"
          "libcw:     %s\n", charlist);
  if (!cw_send_string (charlist))
    {
      printf ("libcw: ERROR: cw_send_string()\n");
      failures++;
    }
  while (cw_get_tone_queue_length () > 0)
    {
      printf ("libcw: tone queue length %-6d\r", cw_get_tone_queue_length ());
      fflush (stdout);
      cw_wait_for_tone ();
    }
  printf ("libcw: tone queue length %-6d\n", cw_get_tone_queue_length ());
  cw_wait_for_tone_queue ();
  if (cw_send_string ("%INVALID%"))
    {
      printf ("libcw: ERROR: invalid cw_send_string()\n");
      failures++;
    }

  printf ("libcw: cw_send_character/string tests complete\n");
  return failures;
}


/*
 * cw_self_test_fixed_receive()
 */
static int
cw_self_test_fixed_receive (void)
{
  static const struct {
    const char character;
    const char *const representation;
    const int usecs[9];
  } TEST_DATA[] = {  /* 60 WPM characters with jitter */
    {'Q', "--.-", {63456, 20111, 63456, 20111, 23456, 20111, 63456, 60111, 0} },
    {'R', ".-.", {17654, 20222, 57654, 20222, 17654, 60222, 0} },
    {'P', ".--.", {23456, 20333, 63456, 20333, 63456, 20333, 23456, 60333, 0} },
    {' ', NULL, {0} }
  };
  int failures = 0;
  int index;
  struct timeval tv;
  double dot_sd, dash_sd, element_end_sd, character_end_sd;

  /*
   * Test receive functions by spoofing them with a timestamp.  Getting the
   * test suite to generate reliable timing events is a little too much work.
   * Add just a little jitter to the timestamps.  This is a _very_ minimal
   * test, omitting all error states.
   */
  printf ("libcw: cw_get_receive_buffer_capacity=%d\n",
          cw_get_receive_buffer_capacity ());

  cw_set_receive_speed (60);
  cw_set_tolerance (35);
  cw_disable_adaptive_receive ();
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  for (index = 0; TEST_DATA[index].representation; index++)
    {
      int entry;
      bool is_word, is_error;
      char c, representation[256];

      tv.tv_sec++;
      tv.tv_usec = 0;
      for (entry = 0; TEST_DATA[index].usecs[entry] > 0; entry++)
        {
          entry & 1 ? cw_end_receive_tone (&tv) : cw_start_receive_tone (&tv);
          tv.tv_usec += TEST_DATA[index].usecs[entry];
        }
      if (cw_get_receive_buffer_length ()
          != (int) strlen (TEST_DATA[index].representation))
        {
          printf ("libcw: ERROR: incorrect receive_buffer_length()\n");
          failures++;
          break;
        }
      if (!cw_receive_representation (&tv, representation, &is_word, &is_error))
        {
          printf ("libcw: ERROR: cw_receive_representation()\n");
          failures++;
          break;
        }
      if (strcmp (representation, TEST_DATA[index].representation) != 0)
        {
          printf ("libcw: ERROR: incorrect cw_receive_representation\n");
          failures++;
          break;
        }
      if (is_word)
        {
          printf ("libcw: ERROR: cw_receive_representation not char\n");
          failures++;
          break;
        }
      if (is_error)
        {
          printf ("libcw: ERROR: cw_receive_representation error\n");
          failures++;
          break;
        }
      if (!cw_receive_character (&tv, &c, &is_word, &is_error))
        {
          printf ("libcw: ERROR: cw_receive_character()\n");
          failures++;
          break;
        }
      if (c != TEST_DATA[index].character)
        {
          printf ("libcw: ERROR: incorrect cw_receive_character\n");
          failures++;
          break;
        }
      printf ("libcw: cw_receive_representation/character <%s>,<%c>\n",
              representation, c);
      cw_clear_receive_buffer ();
      if (cw_get_receive_buffer_length () != 0)
        {
          printf ("libcw: ERROR: incorrect receive_buffer_length()\n");
          failures++;
          break;
        }
    }

  cw_get_receive_statistics (&dot_sd, &dash_sd,
                             &element_end_sd, &character_end_sd);
  printf ("libcw: cw_receive_statistics %.2f, %.2f, %.2f, %.2f\n",
          dot_sd, dash_sd, element_end_sd, character_end_sd);
  cw_reset_receive_statistics ();

  printf ("libcw: cw_receive_representation/character tests complete\n"
          "libcw: cw fixed speed receive tests complete\n");
  return failures;
}


/*
 * cw_self_test_adaptive_receive()
 */
static int
cw_self_test_adaptive_receive (void)
{
  static const struct {
    const char character;
    const char *const representation;
    const int usecs[9];
  } TEST_DATA[] = {  /* 60, 40, and 30 WPM (mixed speed) characters */
    {'Q', "--.-", {60000, 20000, 60000, 20000, 20000, 20000, 60000, 60000, 0} },
    {'R', ".-.", {30000, 30000, 90000, 30000, 30000, 90000, 0} },
    {'P', ".--.", {40000, 40000, 120000, 40000, 120000, 40000, 40000,
                   280000, -1} },  /* Includes word end delay, -1 indicator */
    {' ', NULL, {0} }
  };
  int failures = 0;
  int index;
  struct timeval tv;
  double dot_sd, dash_sd, element_end_sd, character_end_sd;

  /*
   * Test adaptive receive functions in much the same sort of way.  Again,
   * this is a _very_ minimal test, omitting all error states.
   */
  cw_set_receive_speed (45);
  cw_set_tolerance (35);
  cw_enable_adaptive_receive ();
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  for (index = 0; TEST_DATA[index].representation; index++)
    {
      int entry;
      bool is_word, is_error;
      char c, representation[256];

      tv.tv_sec++;
      tv.tv_usec = 0;
      for (entry = 0; TEST_DATA[index].usecs[entry] > 0; entry++)
        {
          entry & 1 ? cw_end_receive_tone (&tv) : cw_start_receive_tone (&tv);
          tv.tv_usec += TEST_DATA[index].usecs[entry];
        }
      if (cw_get_receive_buffer_length ()
          != (int) strlen (TEST_DATA[index].representation))
        {
          printf ("libcw: ERROR: incorrect receive_buffer_length()\n");
          failures++;
          break;
        }
      if (!cw_receive_representation (&tv, representation, &is_word, &is_error))
        {
          printf ("libcw: ERROR: cw_receive_representation()\n");
          failures++;
          break;
        }
      if (strcmp (representation, TEST_DATA[index].representation) != 0)
        {
          printf ("libcw: ERROR: incorrect cw_receive_representation\n");
          failures++;
          break;
        }
      if ((TEST_DATA[index].usecs[entry] == 0 && is_word)
          || (TEST_DATA[index].usecs[entry] < 0 && !is_word))
        {
          printf ("libcw: ERROR: cw_receive_representation not %s\n",
                  is_word ? "char" : "word");
          failures++;
          break;
        }
      if (is_error)
        {
          printf ("libcw: ERROR: cw_receive_representation error\n");
          failures++;
          break;
        }
      if (!cw_receive_character (&tv, &c, &is_word, &is_error))
        {
          printf ("libcw: ERROR: cw_receive_character()\n");
          failures++;
          break;
        }
      if (c != TEST_DATA[index].character)
        {
          printf ("libcw: ERROR: incorrect cw_receive_character\n");
          failures++;
          break;
        }
      printf ("libcw: adaptive speed tracking reports %d wpm\n",
              cw_get_receive_speed ());
      printf ("libcw: cw_receive_representation/character <%s>,<%c>\n",
              representation, c);
      cw_clear_receive_buffer ();
      if (cw_get_receive_buffer_length () != 0)
        {
          printf ("libcw: ERROR: incorrect receive_buffer_length()\n");
          failures++;
          break;
        }
    }

  cw_get_receive_statistics (&dot_sd, &dash_sd,
                             &element_end_sd, &character_end_sd);
  printf ("libcw: cw_receive_statistics %.2f, %.2f, %.2f, %.2f\n",
          dot_sd, dash_sd, element_end_sd, character_end_sd);
  cw_reset_receive_statistics ();

  printf ("libcw: cw_receive_representation/character tests complete\n"
          "libcw: cw adaptive speed receive tests complete\n");
  return failures;
}


/*
 * cw_self_test_keyer()
 */
static int
cw_self_test_keyer (void)
{
  int failures = 0;
  int index, dot_paddle, dash_paddle;

  /*
   * Perform some tests on the iambic keyer.  The latch finer timing points
   * are not tested here, just the basics - dots, dashes, and alternating
   * dots and dashes.
   */
  if (!cw_notify_keyer_paddle_event (true, false))
    {
      printf ("libcw: ERROR: cw_notify_keyer_paddle_event\n");
      failures++;
    }
  printf ("libcw: testing iambic keyer dots   ");
  fflush (stdout);
  for (index = 0; index < 30; index++)
    {
      cw_wait_for_keyer_element ();
      putchar ('#');
      fflush (stdout);
    }
  putchar ('\n');
  cw_get_keyer_paddles (&dot_paddle, &dash_paddle);
  if (!dot_paddle || dash_paddle)
    {
      printf ("libcw: ERROR: cw_keyer_get_paddles mismatch\n");
      failures++;
    }

  if (!cw_notify_keyer_paddle_event (false, true))
    {
      printf ("libcw: ERROR: cw_notify_keyer_paddle_event\n");
      failures++;
    }
  printf ("libcw: testing iambic keyer dashes ");
  fflush (stdout);
  for (index = 0; index < 30; index++)
    {
      cw_wait_for_keyer_element ();
      putchar ('#');
      fflush (stdout);
    }
  putchar ('\n');
  cw_get_keyer_paddles (&dot_paddle, &dash_paddle);
  if (dot_paddle || !dash_paddle)
    {
      printf ("libcw: ERROR: cw_keyer_get_paddles mismatch\n");
      failures++;
    }

  if (!cw_notify_keyer_paddle_event (true, true))
    {
      printf ("libcw: ERROR: cw_notify_keyer_paddle_event\n");
      failures++;
    }
  printf ("libcw: testing iambic alternating  ");
  fflush (stdout);
  for (index = 0; index < 30; index++)
    {
      cw_wait_for_keyer_element ();
      putchar ('#');
      fflush (stdout);
    }
  putchar ('\n');
  cw_get_keyer_paddles (&dot_paddle, &dash_paddle);
  if (!dot_paddle || !dash_paddle)
    {
      printf ("libcw: ERROR: cw_keyer_get_paddles mismatch\n");
      failures++;
    }

  cw_notify_keyer_paddle_event (false, false);
  cw_wait_for_keyer ();

  printf ("libcw: cw_notify_keyer_paddle_event tests complete\n");
  return failures;
}


/*
 * cw_self_test_straight_key()
 */
static int
cw_self_test_straight_key (void)
{
  int failures = 0;
  int index;

  /*
   * Unusually, a nice simple set of tests.
   */
  for (index = 0; index < 10; index++)
    {
      if (!cw_notify_straight_key_event (false))
        {
          printf ("libcw: ERROR: cw_notify_straight_key_event false\n");
          failures++;
        }
      if (cw_get_straight_key_state ())
        {
          printf ("libcw: ERROR: cw_get_straight_key_state\n");
          failures++;
        }
      if (cw_is_straight_key_busy ())
        {
          printf ("libcw: ERROR: cw_straight_key_busy\n");
          failures++;
        }
    }
  for (index = 0; index < 10; index++)
    {
      if (!cw_notify_straight_key_event (true))
        {
          printf ("libcw: ERROR: cw_notify_straight_key_event true\n");
          failures++;
        }
      if (!cw_get_straight_key_state ())
        {
          printf ("libcw: ERROR: cw_get_straight_key_state\n");
          failures++;
        }
      if (!cw_is_straight_key_busy ())
        {
          printf ("libcw: ERROR: cw_straight_key_busy\n");
          failures++;
        }
    }
  sleep (1);
  for (index = 0; index < 10; index++)
    {
      if (!cw_notify_straight_key_event (false))
        {
          printf ("libcw: ERROR: cw_notify_straight_key_event false\n");
          failures++;
        }
    }
  if (cw_get_straight_key_state ())
    {
      printf ("libcw: ERROR: cw_get_straight_key_state\n");
      failures++;
    }

  printf ("libcw: cw_notify_straight_key_event/busy tests complete\n");
  return failures;
}


/*
 * cw_self_test_delayed_release()
 */
static int
cw_self_test_delayed_release (void)
{
  int failures = 0;
  struct timeval start, finish;
  int is_released, delay;

  /*
   * This is slightly tricky to detect, but circumstantial evidence is provided
   * by SIGALRM disposition returning to SIG_DFL.
   */
  if (!cw_send_character_space ())
    {
      printf ("libcw: ERROR: cw_send_character_space()\n");
      failures++;
    }

  if (gettimeofday (&start, NULL) != 0)
    {
      printf ("libcw: WARNING: gettimeofday failed, test incomplete\n");
      return failures;
    }
  printf ("libcw: waiting for cw_finalization delayed release");
  fflush (stdout);
  do
    {
      struct sigaction disposition;

      sleep (1);
      if (sigaction (SIGALRM, NULL, &disposition) != 0)
        {
          printf ("libcw: WARNING: sigaction failed, test incomplete\n");
          return failures;
        }
      is_released = disposition.sa_handler == SIG_DFL;

      if (gettimeofday (&finish, NULL) != 0)
        {
          printf ("libcw: WARNING: gettimeofday failed, test incomplete\n");
          return failures;
        }

      delay = (finish.tv_sec - start.tv_sec) * 1000000 + finish.tv_usec
                                                       - start.tv_usec;
      putchar ('.');
      fflush (stdout);
    }
  while (!is_released && delay < 20000000);
  putchar ('\n');

  /*
   * The release should be around 10 seconds after the end of the sent space.
   * A timeout or two might leak in, reducing it by a bit; we'll be ecstatic
   * with more than five seconds.
   */
  if (is_released)
    {
      printf ("libcw: cw_finalization delayed release after %d usecs\n", delay);
      if (delay < 5000000)
        {
          printf ("libcw: ERROR: cw_finalization release too quick\n");
          failures++;
        }
    }
  else
    {
      printf ("libcw: ERROR: cw_finalization release wait timed out\n");
      failures++;
    }

  printf ("libcw: cw_finalization release tests complete\n");
  return failures;
}

# if 0
/*
 * cw_self_test_signal_handling_callback()
 * cw_self_test_signal_handling()
 */
static int cw_self_test_signal_handling_callback_called = false;
static void
cw_self_test_signal_handling_callback (int signal_number)
{
  signal_number = 0;
  cw_self_test_signal_handling_callback_called = true;
}

static int
cw_self_test_signal_handling (void)
{
  int failures = 0;
  struct sigaction action, disposition;

  /*
   * Test registering, unregistering, and raising SIGUSR1.  SIG_IGN and
   * handlers are tested, but not SIG_DFL, because that stops the process.
   */
  if (cw_unregister_signal_handler (SIGUSR1))
    {
      printf ("libcw: ERROR: cw_unregister_signal_handler invalid\n");
      failures++;
    }

  if (!cw_register_signal_handler (SIGUSR1,
                                   cw_self_test_signal_handling_callback))
    {
      printf ("libcw: ERROR: cw_register_signal_handler failed\n");
      failures++;
    }

  cw_self_test_signal_handling_callback_called = false;
  raise (SIGUSR1);
  sleep (1);
  if (!cw_self_test_signal_handling_callback_called)
    {
      printf ("libcw: ERROR: cw_self_test_signal_handling_callback missed\n");
      failures++;
    }

  if (!cw_register_signal_handler (SIGUSR1, SIG_IGN))
    {
      printf ("libcw: ERROR: cw_register_signal_handler (overwrite) failed\n");
      failures++;
    }

  cw_self_test_signal_handling_callback_called = false;
  raise (SIGUSR1);
  sleep (1);
  if (cw_self_test_signal_handling_callback_called)
    {
      printf ("libcw: ERROR: cw_self_test_signal_handling_callback called\n");
      failures++;
    }

  if (!cw_unregister_signal_handler (SIGUSR1))
    {
      printf ("libcw: ERROR: cw_unregister_signal_handler failed\n");
      failures++;
    }

  if (cw_unregister_signal_handler (SIGUSR1))
    {
      printf ("libcw: ERROR: cw_unregister_signal_handler invalid\n");
      failures++;
    }

  action.sa_handler = cw_self_test_signal_handling_callback;
  action.sa_flags = SA_RESTART;
  sigemptyset (&action.sa_mask);
  if (sigaction (SIGUSR1, &action, &disposition) != 0)
    {
      printf ("libcw: WARNING: sigaction failed, test incomplete\n");
      return failures;
    }
  if (cw_register_signal_handler (SIGUSR1, SIG_IGN))
    {
      printf ("libcw: ERROR: cw_register_signal_handler clobbered\n");
      failures++;
    }
  if (sigaction (SIGUSR1, &disposition, NULL) != 0)
    {
      printf ("libcw: WARNING: sigaction failed, test incomplete\n");
      return failures;
    }

  printf ("libcw: cw_[un]register_signal_handler tests complete\n");
  return failures;
}
#endif

/*---------------------------------------------------------------------*/
/*  Unit tests drivers                                                 */
/*---------------------------------------------------------------------*/

/*
 * cw_self_test_setup()
 *
 * Run before each individual test, to handle setup of common test conditions.
 */
static void
cw_self_test_setup (void)
{
  cw_reset_send_receive_parameters ();
  cw_set_send_speed (30);
  cw_set_receive_speed (30);
  cw_disable_adaptive_receive ();
  cw_reset_receive_statistics ();
  cw_unregister_signal_handler (SIGUSR1);
  errno = 0;
}


/*
 * cw_self_test()
 *
 * Perform a series of self-tests on library public interfaces.
 */
static int cw_self_test (unsigned int testset)
{
	static int (*const TEST_FUNCTIONS[])(void) = {
		cw_self_test_admin, /* Version, license, debug flags */
		cw_self_test_limits,
		cw_self_test_ranges,
		cw_self_test_tone_parameters,
		cw_self_test_simple_tones,
		cw_self_test_complex_tones,
		cw_self_test_tone_queue,
		cw_self_test_volumes,
		cw_self_test_lookups,
		cw_self_test_prosign_lookups,
		cw_self_test_phonetic_lookups,
		cw_self_test_dot_dash,
		cw_self_test_representations,
		cw_self_test_characters,
		cw_self_test_full_send,
		cw_self_test_fixed_receive,
		cw_self_test_adaptive_receive,
		cw_self_test_keyer,
		cw_self_test_straight_key,
		cw_self_test_delayed_release,
		//cw_self_test_signal_handling, /* FIXME - not sure why this test fails :( */
		NULL };

	int output = CW_AUDIO_NONE;
	if (cw_is_oss_possible(NULL)) {
		output = CW_AUDIO_OSS;
	} else {
		fprintf(stderr, "libcw: OSS: soundcard device unavailable: %s\n", strerror(errno));
	}

	if (output == CW_AUDIO_NONE) {
		if (cw_is_alsa_possible(NULL)) {
			output = CW_AUDIO_ALSA;
		} else {
			fprintf(stderr, "libcw: ALSA: soundcard device unavailable: %s\n", strerror(errno));
		}
	}

	if (output == CW_AUDIO_NONE) {
		if (cw_is_console_possible(NULL)) {
			output = CW_AUDIO_OSS;
		} else {
			fprintf(stderr, "libcw: console device cannot do sound: %s\n", strerror(errno));
		}
	}
	if (output == CW_AUDIO_NONE) {
		fprintf(stderr, "libcw: no audio output available, stopping the test\n");
		return -1;
	}

	int rv = cw_generator_new(output, NULL);
	if (rv != 1) {
		fprintf(stderr, "libcw: can't create generator, stopping the test\n");
		return -1;
	}
	rv = cw_generator_start();
	if (rv != 1) {
		fprintf(stderr, "libcw: can't start generator, stopping the test\n");
		cw_generator_delete();
		return -1;
	}


	int tests = 0, failures = 0;
	/* Run each test specified in the testset bit mask,
	   and add up the errors that the tests report. */
	for (int test = 0; TEST_FUNCTIONS[test]; test++) {
		if (testset & (1 << test)) {
			cw_self_test_setup();
			tests++;
			failures += (*TEST_FUNCTIONS[test])();
		}
	}

	sleep(1);
	cw_generator_stop();
	sleep(1);
	cw_generator_delete();

	/* All tests done; return success if no failures,
	   otherwise return an error status code. */
	if (failures == 0) {
		fprintf(stderr, "libcw: %d test%c completed SUCCESSFULLY\n",
			tests, tests == 1 ? ' ' : 's');
		return 0;
	} else {
		fprintf(stderr, "libcw: %d test%c completed with %d ERROR%c\n",
			tests, tests == 1 ? ' ' : 's',
			failures, failures == 1 ? ' ' : 'S');
		return -1;
	}
}


/*
 * main()
 *
 * Calls the main test function, and exits with EXIT_SUCCESS if all
 * tests complete successfully, otherwise exits with EXIT_FAILURE.
 */
int main(int argc, const char *argv[])
{
	static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };

	unsigned int testset;

	/* Obtain a bitmask of the tests to run from the command line
	   arguments. If none, then default to ~0, which effectively
	   requests all tests. */
	if (argc > 1) {
		testset = 0;
		for (int arg = 1; arg < argc; arg++) {
			unsigned int test = strtoul(argv[arg], NULL, 0);
			testset |= 1 << test;
		}
	} else {
		testset = ~0;
	}

	/* Arrange for the test to exit on a range of signals. */
	for (int i = 0; SIGNALS[i] != 0; i++) {
		if (!cw_register_signal_handler(SIGNALS[i], SIG_DFL)) {
			fprintf(stderr, "libcw: ERROR: cw_register_signal_handler\n");
			exit(EXIT_FAILURE);
		}
	}
	/* Run each requested test. */
	int rv = cw_self_test(testset);

	return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
