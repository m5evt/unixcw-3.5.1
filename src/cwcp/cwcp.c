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

#include "config.h"

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <curses.h>
#include <errno.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "libcw.h"
#if defined(LIBCW_WITH_DEV)
#include "libcw_debug.h"
#endif

#include "i18n.h"
#include "cmdline.h"
#include "copyright.h"
#include "dictionary.h"
#include "memory.h"


#if defined(LIBCW_WITH_DEV)
extern cw_debug_t *debug2;
#endif


/*---------------------------------------------------------------------*/
/*  Module variables, miscellaneous other stuff                        */
/*---------------------------------------------------------------------*/

/* Flag set if colors are requested on the user interface. */
static bool do_colors = true;

/* Curses windows used globally. */
static WINDOW *text_box, *text_display, *timer_display;
static cw_config_t *config = NULL; /* program-specific configuration */
static bool generator = false;     /* have we created a generator? */
static const char *all_options = "s:|system,d:|device,"
	"w:|wpm,t:|tone,v:|volume,"
	"g:|gap,k:|weighting,"
	"f:|infile,F:|outfile,"
	"T:|time,"
	/* "c:|colours,c:|colors,m|mono," */
	"h|help,V|version";

static void cwcp_atexit(void);

static int  timer_get_total_practice_time(void);
static bool timer_set_total_practice_time(int practice_time);
static void timer_start(void);
static bool is_timer_expired(void);
static void timer_display_update(int elapsed, int total);

/*---------------------------------------------------------------------*/
/*  Circular character queue                                           */
/*---------------------------------------------------------------------*/

/*
 * Characters awaiting send are stored in a circular buffer, implemented as
 * an array with tail and head indexes that wrap.
 */
enum { QUEUE_CAPACITY = 256 };
static volatile char queue_data[QUEUE_CAPACITY];
static volatile int queue_tail = 0,
                    queue_head = 0;

/*
 * There are times where we have no data to send.  For these cases, record
 * as idle, so that we know when to wake the sender.
 */
static volatile bool is_queue_idle = true;




/*
 * queue_get_length()
 * queue_next_index()
 * queue_prior_index()
 *
 * Return the count of characters currently held in the circular buffer, and
 * advance/regress a tone queue index, including circular wrapping.
 */
static int
queue_get_length (void)
{
  return queue_tail >= queue_head
         ? queue_tail - queue_head : queue_tail - queue_head + QUEUE_CAPACITY;
}

static int
queue_next_index (int index)
{
  return (index + 1) % QUEUE_CAPACITY;
}

static int
queue_prior_index (int index)
{
  return index == 0 ? QUEUE_CAPACITY - 1 : index - 1;
}


/*
 * queue_display_add_character()
 * queue_display_delete_character()
 * queue_display_highlight_character()
 *
 * Add and delete a character to the text display when queueing, and
 * highlight or un-highlight, a character in the text display when dequeueing.
 */
static void
queue_display_add_character (void)
{
  /* Append the last queued character to the text display. */
  if (queue_get_length () > 0)
    {
      waddch (text_display, toupper (queue_data[queue_tail]));
      wrefresh (text_display);
    }
}

static void
queue_display_delete_character (void)
{
  int y, x, max_x;
  __attribute__((unused)) int max_y;

  /* Get the text display dimensions and current coordinates. */
  getmaxyx (text_display, max_y, max_x);
  getyx (text_display, y, x);

  /* Back the cursor up one position. */
  x--;
  if (x < 0)
    {
      x += max_x;
      y--;
    }

  /* If these coordinates are on screen, write a space and back up. */
  if (y >= 0)
    {
      wmove (text_display, y, x);
      waddch (text_display, ' ');
      wmove (text_display, y, x);
      wrefresh (text_display);
    }
}

static void
queue_display_highlight_character (int is_highlight)
{
  int y, x, max_x;
  __attribute__((unused)) int max_y;

  /* Get the text display dimensions and current coordinates. */
  getmaxyx (text_display, max_y, max_x);
  getyx (text_display, y, x);

  /* Find the coordinates for the queue head character. */
  x -= queue_get_length () + 1;
  while (x < 0)
    {
      x += max_x;
      y--;
    }

  /*
   * If these coordinates are on screen, highlight or unhighlight, and then
   * restore the cursor position so that it remains unchanged.
   */
  if (y >= 0)
    {
      int saved_y, saved_x;

      getyx (text_display, saved_y, saved_x);
      wmove (text_display, y, x);
      waddch (text_display,
              is_highlight ? winch (text_display) | A_REVERSE
                           : winch (text_display) & ~A_REVERSE);
      wmove (text_display, saved_y, saved_x);
      wrefresh (text_display);
    }
}


/*
 * queue_discard_contents()
 *
 * Forcibly empty the queue, if not already idle.
 */
static void
queue_discard_contents (void)
{
  if (!is_queue_idle)
    {
      queue_display_highlight_character (false);
      queue_head = queue_tail;
      is_queue_idle = true;
    }
}


/*
 * queue_dequeue_character()
 *
 * Called when the CW send buffer is empty.  If the queue is not idle, take
 * the next character from the queue and send it.  If there are no more queued
 * characters, set the queue to idle.
 */
static void
queue_dequeue_character (void)
{
  if (!is_queue_idle)
    {
      /* Unhighlight any previous highlighting, and see if we can dequeue. */
      queue_display_highlight_character (false);
      if (queue_get_length () > 0)
        {
          char c;

          /*
           * Take the next character off the queue, highlight, and send it.
           * We don't expect sending to fail because only sendable characters
           * are queued.
           */
          queue_head = queue_next_index (queue_head);
          c = queue_data[queue_head];
          queue_display_highlight_character (true);

          if (!cw_send_character (c))
            {
              perror ("cw_send_character");
              abort ();
            }
        }
      else
        is_queue_idle = true;
    }
}


/*
 * queue_enqueue_string()
 * queue_enqueue_character()
 *
 * Queues a string or character for sending by the CW sender.  Rejects any
 * unsendable character, and also any characters passed in where the character
 * queue is already full.  Rejection is silent.
 */
static void
queue_enqueue_string (const char *word)
{
  bool is_queue_notify = false;
  for (int i = 0; word[i] != '\0'; i++)
    {
      char c;

      c = toupper (word[i]);
      if (cw_check_character (c))
        {
          /*
           * Calculate the new character queue tail.  If the new value will
           * not hit the current queue head, add the character to the queue.
           */
          if (queue_next_index (queue_tail) != queue_head)
            {
              queue_tail = queue_next_index (queue_tail);
              queue_data[queue_tail] = c;
              queue_display_add_character ();

              if (is_queue_idle)
                is_queue_notify = true;
            }
        }
    }

  /* If we queued any character, mark the queue as not idle. */
  if (is_queue_notify)
    is_queue_idle = false;
}

static void
queue_enqueue_character (char c)
{
  char buffer[2];

  buffer[0] = c;
  buffer[1] = '\0';
  queue_enqueue_string (buffer);
}


/*
 * queue_delete_character()
 *
 * Remove the most recently added character from the queue, provided that
 * the dequeue hasn't yet reached it.  If there's nothing available to
 * delete, fail silently.
 */
static void
queue_delete_character (void)
{
  /* If data is queued, regress tail and delete one display character. */
  if (queue_get_length () > 0)
    {
      queue_tail = queue_prior_index (queue_tail);
      queue_display_delete_character ();
    }
}





/*---------------------------------------------------------------------*/
/*  Practice timer                                                     */
/*---------------------------------------------------------------------*/

static const int TIMER_MIN_TIME = 1, TIMER_MAX_TIME = 99; /* practice timer limits */
static int timer_total_practice_time = 15; /* total time of practice, from beginning to end */
static int timer_practice_start = 0;       /* time() value on practice start */





/**
   \brief Get current value of total time of practice
*/
int timer_get_total_practice_time(void)
{
	return timer_total_practice_time;
}





/**
   \brief Set total practice time

   Set total time (total duration) of practice.

   \param practice_time - new value of total practice time

   \return true on success
   \return false on failure
*/
bool timer_set_total_practice_time(int practice_time)
{
	if (practice_time >= TIMER_MIN_TIME && practice_time <= TIMER_MAX_TIME) {
		timer_total_practice_time = practice_time;
		return true;
	} else {
		return false;
	}
}





/**
   \brief Set the timer practice start time to the current time
*/
void timer_start(void)
{
	timer_practice_start = time(NULL);
	return;
}





/**
   \brief Update the practice timer, and return true if the timer expires

   \return true if timer has expired
   \return false if timer has not expired yet
*/
bool is_timer_expired(void)
{
	/* Update the display of minutes practiced. */
	int elapsed = (time(NULL) - timer_practice_start) / 60;
	timer_display_update(elapsed, timer_total_practice_time);

	/* Check the time, requesting stop if over practice time. */
	return elapsed >= timer_total_practice_time;
}





/**
   \brief Update value of time spent on practicing

   Function updates 'timer display' with one or two values of practice
   time: time elapsed and time total. Both times are in minutes.

   You can pass a negative value of \p elapsed - function will use
   previous valid value (which is zero at first time).

   \param elapsed - time elapsed from beginning of practice
   \param total - total time of practice
*/
void timer_display_update(int elapsed, int total)
{
	static int el = 0;
	if (elapsed >= 0) {
		el = elapsed;
	}

	char buffer[16];
	sprintf (buffer, total == 1 ? _("%2d/%2d min ") : _("%2d/%2d mins"), el, total);
	mvwaddstr(timer_display, 0, 2, buffer);
	wrefresh(timer_display);

	return;
}





/*---------------------------------------------------------------------*/
/*  General program state and mode control                             */
/*---------------------------------------------------------------------*/

/*
 * Definition of an interface operating mode; its description, related
 * dictionary, and data on how to send for the mode.
 */
typedef enum { M_DICTIONARY, M_KEYBOARD, M_EXIT } mode_type_t;
struct mode_s
{
  const char *description;       /* Text mode description */
  mode_type_t type;              /* Mode type; dictionary, keyboard... */
  const cw_dictionary_t *dict;   /* Dictionary, if type is dictionary */
};
typedef struct mode_s *moderef_t;

/*
 * Modes table, current program mode, and count of modes in the table.
 * The program is always in one of these modes, indicated by current_mode.
 */
static moderef_t modes = NULL,
                 current_mode = NULL;
static int modes_count = 0;

/* Current sending state, active or idle. */
static bool is_sending_active = false;


/*
 * mode_initialize()
 *
 * Build up the modes from the known dictionaries, then add non-dictionary
 * modes.
 */
static void
mode_initialize (void)
{
  /* Dispose of any pre-existing modes -- unlikely. */
  free (modes);
  modes = NULL;

  /* Start the modes with the known dictionaries. */
  int count = 0;
  for (const cw_dictionary_t *dict = cw_dictionaries_iterate(NULL); dict; dict = cw_dictionaries_iterate(dict))
    {
      modes = safe_realloc (modes, sizeof (*modes) * (count + 1));
      modes[count].description = cw_dictionary_get_description (dict);
      modes[count].type = M_DICTIONARY;
      modes[count++].dict = dict;
    }

  /* Add keyboard, exit, and null sentinel. */
  modes = safe_realloc (modes, sizeof (*modes) * (count + 3));
  modes[count].description = _("Keyboard");
  modes[count].type = M_KEYBOARD;
  modes[count++].dict = NULL;

  modes[count].description = _("Exit (F12)");
  modes[count].type = M_EXIT;
  modes[count++].dict = NULL;

  memset (modes + count, 0, sizeof (*modes));

  /* Initialize the current mode to be the first listed, and set count. */
  current_mode = modes;
  modes_count = count;
}


/*
 * mode_get_count()
 * mode_get_current()
 * mode_get_description()
 * mode_current_is_type()
 *
 * Get the count of modes, the index of the current mode, a description of the
 * mode at a given index, and a type comparison for the current mode.
 */
static int
mode_get_count (void)
{
  return modes_count;
}

static int
mode_get_current (void)
{
  return current_mode - modes;
}

static const char *
mode_get_description (int index)
{
  return modes[index].description;
}

static bool
mode_current_is_type (mode_type_t type)
{
  return current_mode->type == type;
}


/*
 * mode_advance_current()
 * mode_regress_current()
 *
 * Advance and regress the current node, returning false if at the limits.
 */
static bool
mode_advance_current (void)
{
  current_mode++;
  if (!current_mode->description)
    {
      current_mode--;
      return false;
    }
  else
    return true;
}

static bool
mode_regress_current (void)
{
  if (current_mode > modes)
    {
      current_mode--;
      return true;
    }
  else
    return false;
}


/*
 * change_state_to_active()
 *
 * Change the state of the program from idle to actively sending.
 */
static void
change_state_to_active (void)
{
  static moderef_t last_mode = NULL;  /* Detect changes of mode */

  if (!is_sending_active)
    {
      cw_start_beep();

      /* Don't set sending_state until after the above warning has completed. */
      is_sending_active = true;

      mvwaddstr (text_box, 0, 1, _("Sending(F9 or Esc to exit)"));
      wnoutrefresh (text_box);
      doupdate ();

      if (current_mode != last_mode)
        {
          /* If the mode changed, clear the display window. */
          werase (text_display);
          wmove (text_display, 0, 0);
          wrefresh (text_display);

          /* And if we are starting something new, start the timer. */
          timer_start ();

          last_mode = current_mode;
        }
   }
}


/*
 * change_state_to_idle()
 *
 * Change the state of the program from actively sending to idle.
 */
static void
change_state_to_idle (void)
{
  if (is_sending_active)
    {
      is_sending_active = false;

      box (text_box, 0, 0);
      mvwaddstr (text_box, 0, 1, _("Start(F9)"));
      wnoutrefresh (text_box);
      touchwin (text_display);
      wnoutrefresh (text_display);
      doupdate ();

      /* Remove everything in the outgoing character queue. */
      queue_discard_contents ();

      cw_end_beep();
    }
}


/*
 * mode_buffer_random_text()
 *
 * Add a group of elements, based on the given mode, to the character queue.
 */
static void
mode_buffer_random_text (moderef_t mode)
{
  int group_size = cw_dictionary_get_group_size (mode->dict);

  /* Select and buffer groupsize random wordlist elements. */
  queue_enqueue_character (' ');
  for (int group = 0; group < group_size; group++)
    queue_enqueue_string (cw_dictionary_get_random_word (mode->dict));
}


/*
 * mode_libcw_poll_sender()
 *
 * Poll the CW library tone queue, and if it is getting low, arrange for
 * more data to be passed in to the sender.
 */
static void
mode_libcw_poll_sender (void)
{
  if (cw_get_tone_queue_length () <= 1)
    {
      /*
       * If sending is active, arrange more data for libcw.  The source for
       * this data is dependent on the mode.  If in dictionary modes, update
       * and check the timer, then add more random data if the queue is empty.
       * If in keyboard mode, just dequeue anything currently on the character
       * queue.
       */
      if (is_sending_active)
        {
          if (current_mode->type == M_DICTIONARY)
            {
              if (is_timer_expired ())
                {
                  change_state_to_idle ();
                  return;
                }

              if (queue_get_length () == 0)
                mode_buffer_random_text (current_mode);
            }

          if (current_mode->type == M_DICTIONARY
              || current_mode->type == M_KEYBOARD)
            {
              queue_dequeue_character ();
            }
        }
    }
}


/*
 * mode_is_sending_active()
 *
 * Return true if currently sending, false otherwise.
 */
static bool
mode_is_sending_active (void)
{
  return is_sending_active;
}


/*---------------------------------------------------------------------*/
/*  User interface initialization and event handling                   */
/*---------------------------------------------------------------------*/

/*
 * User interface introduction strings, split in two to avoid the 509
 * character limit imposed by ISO C89 on string literal lengths.
 */
static const char *const INTRODUCTION = N_(
  "UNIX/Linux Morse Tutor v3.0.1\n"
  "Copyright (C) 1997-2006 Simon Baldwin\n"
  "Copyright (C) 2011-2012 Kamil Ignacak\n"
  "---------------------------------------------------------\n"
  "Cwcp is an interactive Morse code tutor program, designed\n"
  "both for learning Morse code for the first time, and for\n"
  "experienced Morse users who want, or need, to improve\n"
  "their receiving speed.\n");
static const char *const INTRODUCTION_CONTINUED = N_(
  "---------------------------------------------------------\n"
  "Select mode:                   Up/Down arrow/F10/F11\n"
  "Start sending selected mode:   Enter/F9\n"
  "Pause:                         F9/Esc\n"
  "Resume:                        F9\n"
  "Exit program:                  menu->Exit/F12/^C\n"
  "Use keys specified below to adjust speed, tone, volume,\n"
  "and spacing of the Morse code at any time.\n");

/* Alternative F-keys for folks without (some, or all) F-keys. */
enum
{ CTRL_OFFSET = 0100,                   /* Ctrl keys are 'X' - 0100 */
  PSEUDO_KEYF1 = 'Q' - CTRL_OFFSET,     /* Alternative FKEY1 */
  PSEUDO_KEYF2 = 'W' - CTRL_OFFSET,     /* Alternative FKEY2 */
  PSEUDO_KEYF3 = 'E' - CTRL_OFFSET,     /* Alternative FKEY3 */
  PSEUDO_KEYF4 = 'R' - CTRL_OFFSET,     /* Alternative FKEY4 */
  PSEUDO_KEYF5 = 'T' - CTRL_OFFSET,     /* Alternative FKEY5 */
  PSEUDO_KEYF6 = 'Y' - CTRL_OFFSET,     /* Alternative FKEY6 */
  PSEUDO_KEYF7 = 'U' - CTRL_OFFSET,     /* Alternative FKEY7 */
  PSEUDO_KEYF8 = 'I' - CTRL_OFFSET,     /* Alternative FKEY8 */
  PSEUDO_KEYF9 = 'A' - CTRL_OFFSET,     /* Alternative FKEY9 */
  PSEUDO_KEYF10 = 'S' - CTRL_OFFSET,    /* Alternative FKEY10 */
  PSEUDO_KEYF11 = 'D' - CTRL_OFFSET,    /* Alternative FKEY11 */
  PSEUDO_KEYF12 = 'F' - CTRL_OFFSET,    /* Alternative FKEY12 */
  PSEUDO_KEYNPAGE = 'O' - CTRL_OFFSET,  /* Alternative PageDown */
  PSEUDO_KEYPPAGE = 'P' - CTRL_OFFSET   /* Alternative PageUp */
};

/* User interface event loop running flag. */
static bool is_running = true;

/* Color definitions. */
static const short color_array[] = {
  COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
  COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
};
enum { COLORS_COUNT = sizeof (color_array) / sizeof (color_array[0]) };

enum
{ BOX_COLORS = 1,          /* Normal color pair */
  DISPLAY_COLORS = 2,      /* Blue color pair */
  DISPLAY_FOREGROUND = 7,  /* White foreground */
  DISPLAY_BACKGROUND = 4,  /* Blue background */
  BOX_FOREGROUND = 7,      /* White foreground */
  BOX_BACKGROUND = 0       /* Black background */
};

/* Color values as arrays into color_array. */
static int display_foreground = DISPLAY_FOREGROUND,  /* White foreground */
           display_background = DISPLAY_BACKGROUND,  /* Blue background */
           box_foreground = BOX_FOREGROUND,          /* White foreground */
           box_background = BOX_BACKGROUND;          /* Black background */

/* Curses windows used by interface functions only. */
static WINDOW *screen = NULL,
              *mode_display = NULL, *speed_display = NULL,
              *tone_display = NULL, *volume_display = NULL,
              *gap_display = NULL;

/*
 * interface_init_screen()
 * interface_init_box()
 * interface_init_display()
 * interface_init_panel()
 *
 * Helper functions for interface_init(), to build boxes and displays.
 */
static WINDOW*
interface_init_screen (void)
{
  WINDOW *window;

  /* Create the main window for the complete screen. */
  window = initscr ();
  wrefresh (window);

  /* If using colors, set up a base color for the screen. */
  if (do_colors && has_colors ())
    {
      int max_y, max_x;
      WINDOW *base;

      start_color ();
      init_pair (BOX_COLORS,
                 color_array[box_foreground],
                 color_array[box_background]);
      init_pair (DISPLAY_COLORS,
                 color_array[display_foreground],
                 color_array[display_background]);
      getmaxyx (screen, max_y, max_x);
      base = newwin (max_y + 1, max_x + 1, 0, 0);
      wbkgdset (base, COLOR_PAIR (BOX_COLORS) | ' ');
      werase (base);
      wrefresh (base);
    }

  return window;
}

static WINDOW*
interface_init_box (int lines, int columns, int begin_y, int begin_x,
                    const char *legend)
{
  WINDOW *window;

  /* Create the window, and set up colors if possible and requested. */
  window = newwin (lines, columns, begin_y, begin_x);

  if (do_colors && has_colors ())
    {
      wbkgdset (window, COLOR_PAIR (BOX_COLORS) | ' ');
      werase (window);
      wattron (window, COLOR_PAIR (BOX_COLORS));
    }
  else
    wattron (window, A_REVERSE);
  box (window, 0, 0);

  /* Add any initial legend to the box. */
  if (legend)
    mvwaddstr (window, 0, 1, legend);

  wrefresh (window);
  return window;
}

static WINDOW*
interface_init_display (int lines, int columns, int begin_y, int begin_x,
                        int indent, const char *text)
{
  WINDOW *window;

  /* Create the window, and set up colors if possible and requested. */
  window = newwin (lines, columns, begin_y, begin_x);

  if (do_colors && has_colors ())
    {
      wbkgdset (window, COLOR_PAIR (DISPLAY_COLORS) | ' ');
      wattron (window, COLOR_PAIR (DISPLAY_COLORS));
      werase (window);
    }

  /* Add any initial text to the box. */
  if (text)
    mvwaddstr (window, 0, indent, text);

  wrefresh (window);
  return window;
}

static void
interface_init_panel (int lines, int columns, int begin_y, int begin_x,
                      const char *box_legend,
                      int indent, const char *display_text,
                      WINDOW **box, WINDOW **display)
{
  WINDOW *window;

  /* Create and return, if required, a box for the control. */
  window = interface_init_box (lines, columns, begin_y, begin_x, box_legend);
  if (box)
    *box = window;

  /* Add a display within the frame of the box. */
  *display = interface_init_display (lines - 2, columns - 2,
                                     begin_y + 1, begin_x + 1,
                                     indent, display_text);
}


/*
 * interface_initialize()
 *
 * Initialize the user interface, boxes and windows.
 */
static void
interface_initialize (void)
{
  static bool is_initialized = false;

  int max_y, max_x;

  /* Create the over-arching screen window. */
  screen = interface_init_screen ();
  getmaxyx (screen, max_y, max_x);

  /* Create and box in the mode window. */
  interface_init_panel (max_y - 3, 20, 0, 0, _("Mode(F10v,F11^)"),
                        0, NULL, NULL, &mode_display);
  for (int i = 0; i < mode_get_count (); i++)
    {
      if (i == mode_get_current ())
        wattron (mode_display, A_REVERSE);
      else
        wattroff (mode_display, A_REVERSE);
      mvwaddstr (mode_display, i, 0, mode_get_description (i));
    }
  wrefresh (mode_display);

  /* Create the text display window; do the introduction only once. */
  interface_init_panel (max_y - 3, max_x - 20, 0, 20, _("Start(F9)"),
                        0, NULL, &text_box, &text_display);
  wmove (text_display, 0, 0);
  if (!is_initialized)
    {
      waddstr (text_display, _(INTRODUCTION));
      waddstr (text_display, _(INTRODUCTION_CONTINUED));
      is_initialized = true;
    }
  wrefresh (text_display);
  idlok (text_display, true);
  immedok (text_display, true);
  scrollok (text_display, true);

  char buffer[16];
  /* Create the control feedback boxes. */
  sprintf (buffer, _("%2d WPM"), cw_get_send_speed ());
  interface_init_panel (3, 16, max_y - 3, 0, _("Speed(F1-,F2+)"),
                        4, buffer, NULL, &speed_display);

  sprintf (buffer, _("%4d Hz"), cw_get_frequency ());
  interface_init_panel (3, 16, max_y - 3, 16, _("Tone(F3-,F4+)"),
                        3, buffer, NULL, &tone_display);

  sprintf (buffer, _("%3d %%"), cw_get_volume ());
  interface_init_panel (3, 16, max_y - 3, 32, _("Vol(F5-,F6+)"),
                        4, buffer, NULL, &volume_display);

  int value = cw_get_gap ();
  sprintf (buffer, value == 1 ? _("%2d dot ") : _("%2d dots"), value);
  interface_init_panel (3, 16, max_y - 3, 48, _("Gap(F7-,F8+)"),
                        3, buffer, NULL, &gap_display);

  interface_init_panel (3, 16, max_y - 3, 64, _("Time(Dn-,Up+)"),
                        2, "", NULL, &timer_display);
  timer_display_update(0, timer_get_total_practice_time());

  /* Set up curses input mode. */
  keypad (screen, true);
  noecho ();
  cbreak ();
  curs_set (0);
  raw ();
  nodelay (screen, false);

  wrefresh (curscr);
}


/*
 * interface_destroy()
 *
 * Dismantle the user interface, boxes and windows.
 */
static void
interface_destroy (void)
{
  /* Clear the screen for neatness. */
  werase (screen);
  wrefresh (screen);

  /* End curses processing. */
  endwin ();

  /* Reset user interface windows to initial values. */
  screen = NULL;
  mode_display = NULL;
  speed_display = NULL;
  tone_display = NULL;
  volume_display = NULL;
  gap_display = NULL;
}


/*
 * interface_interpret()
 *
 * Assess a user command, and action it if valid.  If the command turned out
 * to be a valid user interface command, return true, otherwise return false.
 */
static int
interface_interpret (int c)
{
  char buffer[16];
  int previous_mode, value;

  /* Interpret the command passed in */
  switch (c)
    {
    default:
      return false;

    case ']':
      display_background = (display_background + 1) % COLORS_COUNT;
      goto color_update;

    case '[':
      display_foreground = (display_foreground + 1) % COLORS_COUNT;
      goto color_update;

    case '{':
      box_background = (box_background + 1) % COLORS_COUNT;
      goto color_update;

    case '}':
      box_foreground = (box_foreground + 1) % COLORS_COUNT;
      goto color_update;

    color_update:
      if (do_colors && has_colors ())
        {
          init_pair (BOX_COLORS,
                     color_array[box_foreground],
                     color_array[box_background]);
          init_pair (DISPLAY_COLORS,
                     color_array[display_foreground],
                     color_array[display_background]);
          wrefresh (curscr);
        }
      break;


    case 'L' - CTRL_OFFSET:
      wrefresh (curscr);
      break;


    case KEY_F (1):
    case PSEUDO_KEYF1:
    case KEY_LEFT:
      if (cw_set_send_speed (cw_get_send_speed () - CW_SPEED_STEP))
        goto speed_update;
      break;

    case KEY_F (2):
    case PSEUDO_KEYF2:
    case KEY_RIGHT:
      if (cw_set_send_speed (cw_get_send_speed () + CW_SPEED_STEP))
        goto speed_update;
      break;

    speed_update:
      sprintf (buffer, _("%2d WPM"), cw_get_send_speed ());
      mvwaddstr (speed_display, 0, 4, buffer);
      wrefresh (speed_display);
      break;


    case KEY_F (3):
    case PSEUDO_KEYF3:
    case KEY_END:
      if (cw_set_frequency (cw_get_frequency () - CW_FREQUENCY_STEP))
        goto frequency_update;
      break;

    case KEY_F (4):
    case PSEUDO_KEYF4:
    case KEY_HOME:
      if (cw_set_frequency (cw_get_frequency () + CW_FREQUENCY_STEP))
        goto frequency_update;
      break;

    frequency_update:
      sprintf (buffer, _("%4d Hz"), cw_get_frequency ());
      mvwaddstr (tone_display, 0, 3, buffer);
      wrefresh (tone_display);
      break;


    case KEY_F (5):
    case PSEUDO_KEYF5:
      if (cw_set_volume (cw_get_volume () - CW_VOLUME_STEP))
        goto volume_update;
      break;

    case KEY_F (6):
    case PSEUDO_KEYF6:
      if (cw_set_volume (cw_get_volume () + CW_VOLUME_STEP))
        goto volume_update;
      break;

    volume_update:
      sprintf (buffer, _("%3d %%"), cw_get_volume ());
      mvwaddstr (volume_display, 0, 4, buffer);
      wrefresh (volume_display);
      break;


    case KEY_F (7):
    case PSEUDO_KEYF7:
      if (cw_set_gap (cw_get_gap () - CW_GAP_STEP))
        goto gap_update;
      break;

    case KEY_F (8):
    case PSEUDO_KEYF8:
      if (cw_set_gap (cw_get_gap () + CW_GAP_STEP))
        goto gap_update;
      break;

    gap_update:
      value = cw_get_gap ();
      sprintf (buffer, value == 1 ? _("%2d dot ") : _("%2d dots"), value);
      mvwaddstr (gap_display, 0, 3, buffer);
      wrefresh (gap_display);
      break;


    case KEY_NPAGE:
    case PSEUDO_KEYNPAGE:
      if (timer_set_total_practice_time (timer_get_total_practice_time () - CW_PRACTICE_TIME_STEP))
	timer_display_update(-1, timer_get_total_practice_time());
      break;

    case KEY_PPAGE:
    case PSEUDO_KEYPPAGE:
      if (timer_set_total_practice_time (timer_get_total_practice_time () + CW_PRACTICE_TIME_STEP))
	timer_display_update(-1, timer_get_total_practice_time());
      break;

    case KEY_F (11):
    case PSEUDO_KEYF11:
    case KEY_UP:
      change_state_to_idle ();
      previous_mode = mode_get_current ();
      if (mode_regress_current ())
        goto mode_update;
      break;

    case KEY_F (10):
    case PSEUDO_KEYF10:
    case KEY_DOWN:
      change_state_to_idle ();
      previous_mode = mode_get_current ();
      if (mode_advance_current ())
        goto mode_update;
      break;

    mode_update:
      wattroff (mode_display, A_REVERSE);
      mvwaddstr (mode_display,
                 previous_mode, 0, mode_get_description (previous_mode));
      wattron (mode_display, A_REVERSE);
      mvwaddstr (mode_display,
                 mode_get_current (), 0,
                 mode_get_description (mode_get_current ()));
      wrefresh (mode_display);
      break;


    case KEY_F (9):
    case PSEUDO_KEYF9:
    case '\n':
      if (mode_current_is_type (M_EXIT))
        is_running = false;
      else
        {
          if (!mode_is_sending_active ())
            change_state_to_active ();
          else
            if (c != '\n')
              change_state_to_idle ();
        }
      break;

    case KEY_CLEAR:
    case 'V' - CTRL_OFFSET:
      if (!mode_is_sending_active ())
        {
          werase (text_display);
          wmove (text_display, 0, 0);
          wrefresh (text_display);
        }
      break;

    case '[' - CTRL_OFFSET:
    case 'Z' - CTRL_OFFSET:
      change_state_to_idle ();
      break;

    case KEY_F (12):
    case PSEUDO_KEYF12:
    case 'C' - CTRL_OFFSET:
      queue_discard_contents ();
      cw_flush_tone_queue ();
      is_running = false;
      break;

    case KEY_RESIZE:
      change_state_to_idle ();
      interface_destroy ();
      interface_initialize ();
      break;
    }

  /* The command was a recognized interface key. */
  return true;
}


/*
 * interface_handle_event()
 *
 * Handle an interface 'event', in this case simply a character from the
 * keyboard via curses.
 */
static void
interface_handle_event (int c)
{
  /* See if this character is a valid user interface command. */
  if (interface_interpret (c))
    return;

  /*
   * If the character is standard 8-bit ASCII or backspace, and the current
   * sending mode is from the keyboard, then make an effort to either queue
   * the character for sending, or delete the most recently queued.
   */
  if (mode_is_sending_active () && mode_current_is_type (M_KEYBOARD))
    {
      if (c == KEY_BACKSPACE || c == KEY_DC)
        {
          queue_delete_character ();
          return;
        }
      else if (c <= UCHAR_MAX)
        {
          queue_enqueue_character ((char) c);
          return;
        }
    }

  /* The 'event' is nothing at all of interest; drop it. */
}





/*
 * poll_until_keypress_ready()
 *
 * Calls our sender polling function at regular intervals, and returns only
 * when data is available to getch(), so that it will not block.
 */
static void
poll_until_keypress_ready (int fd, int usecs)
{
  int fd_count;

  /* Poll until the select indicates data on the file descriptor. */
  do
    {
      fd_set read_set;
      struct timeval timeout;

      /* Set up a the file descriptor set and timeout information. */
      FD_ZERO (&read_set);
      FD_SET (fd, &read_set);
      timeout.tv_sec = usecs / 1000000;
      timeout.tv_usec = usecs % 1000000;

      /*
       * Wait until timeout, data, or a signal.  If a signal interrupts
       * select, we can just treat it as another timeout.
       */
      fd_count = select (fd + 1, &read_set, NULL, NULL, &timeout);
      if (fd_count == -1 && errno != EINTR)
        {
          perror ("select");
          abort ();
        }

      /* Poll the sender on timeouts and on reads; it's just easier. */
      mode_libcw_poll_sender ();
    }
  while (fd_count != 1);
}


/*
 * signal_handler()
 *
 * Signal handler for signals, to clear up on kill.
 */
static void
signal_handler (int signal_number)
{
  /* Attempt to wrestle the screen back from curses. */
  interface_destroy ();

  /* Show the signal caught, and exit. */
  fprintf (stderr, _("\nCaught signal %d, exiting...\n"), signal_number);
  exit (EXIT_SUCCESS);
}


/*
 * main()
 *
 * Parse the command line, initialize a few things, then enter the main
 * program event loop, from which there is no return.
 */
int main(int argc, char **argv)
{
	atexit(cwcp_atexit);

	/* Set locale and message catalogs. */
	i18n_initialize();

	/* Parse combined environment and command line arguments. */
	int combined_argc;
	char **combined_argv;

	/* Parse combined environment and command line arguments. */
	combine_arguments(_("CWCP_OPTIONS"), argc, argv, &combined_argc, &combined_argv);

	config = cw_config_new(cw_program_basename(argv[0]));
	if (!config) {
		return EXIT_FAILURE;
	}
	config->has_practice_time = true;
	config->has_outfile = true;

	if (!cw_process_argv(argc, argv, all_options, config)) {
		fprintf(stderr, _("%s: failed to parse command line args\n"), config->program_name);
		return EXIT_FAILURE;
	}
	if (!cw_config_is_valid(config)) {
		fprintf(stderr, _("%s: inconsistent arguments\n"), config->program_name);
		return EXIT_FAILURE;
	}

	if (config->input_file) {
		if (!cw_dictionaries_read(config->input_file)) {
			fprintf(stderr, _("%s: %s\n"), config->program_name, strerror(errno));
			fprintf(stderr, _("%s: can't load dictionary from input file %s\n"), config->program_name, config->input_file);
			return EXIT_FAILURE;
		}
	}

	if (config->output_file) {
		if (!cw_dictionaries_write(config->output_file)) {
			fprintf(stderr, _("%s: %s\n"), config->program_name, strerror(errno));
			fprintf(stderr, _("%s: can't save dictionary to output file  %s\n"), config->program_name, config->input_file);
			return EXIT_FAILURE;
		}
	}

	if (config->audio_system == CW_AUDIO_ALSA
	    && cw_is_pa_possible(NULL)) {

		fprintf(stdout, "Selected audio system is ALSA, but audio on your system is handled by PulseAudio. Expect problems with timing.\n");
		fprintf(stdout, "In this situation it is recommended to run %s like this:\n", config->program_name);
		fprintf(stdout, "%s -s p\n\n", config->program_name);
		fprintf(stdout, "Press Enter key to continue\n");
		getchar();
	}

	generator = cw_generator_new_from_config(config);
	if (!generator) {
		fprintf(stderr, "%s: failed to create generator\n", config->program_name);
		return EXIT_FAILURE;
	}
	timer_set_total_practice_time(config->practice_time);


	static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };
	/* Set up signal handlers to clear up and exit on a range of signals. */
	for (int i = 0; SIGNALS[i]; i++) {
		if (!cw_register_signal_handler(SIGNALS[i], signal_handler)) {
			fprintf(stderr, _("%s: can't register signal: %s\n"), config->program_name, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	/*
	 * Build our table of modes from dictionaries, augmented with keyboard
	 * and any other local modes.
	 */
	mode_initialize();

	debug2 = cw_debug2_new("stderr");

	/*
	 * Initialize the curses user interface, then catch and action every
	 * keypress we see.  Before calling getch, wait until data is available on
	 * stdin, polling the libcw sender.  At 60WPM, a dot is 20ms, so polling
	 * for the maximum library speed needs a 10ms (10,000usec) timeout.
	 */
	interface_initialize ();
	cw_generator_start();
	while (is_running) {
		poll_until_keypress_ready(fileno(stdin), 10000);
		interface_handle_event(getch());
	}

	cw_wait_for_tone_queue();

	return EXIT_SUCCESS;
}





void cwcp_atexit(void)
{
	interface_destroy();

	if (generator) {
		cw_complete_reset();
		cw_generator_stop();
		cw_generator_delete();
	}

	if (config) {
		cw_config_delete(&config);
	}

	cw_debug2_delete(&debug2);

	return;
}

