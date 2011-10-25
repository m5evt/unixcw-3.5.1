/* vi: set ts=2 shiftwidth=2 expandtab:
 *
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#define _BSD_SOURCE

#include "../config.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <errno.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "cw.h"
#include "cwlib.h"

#include "i18n.h"
#include "cmdline.h"
#include "copyright.h"


/*---------------------------------------------------------------------*/
/*  Module variables, miscellaneous other stuff                        */
/*---------------------------------------------------------------------*/

/* Assorted definitions and constants. */
enum { FALSE = 0, TRUE = !FALSE };

/* Forward declarations for printf-like functions with checkable arguments. */
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)
static void write_to_echo_stream (const char *format, ...)
    __attribute__ ((__format__ (__printf__, 1, 2)));
static void write_to_message_stream (const char *format, ...)
    __attribute__ ((__format__ (__printf__, 1, 2)));
static void write_to_cw_sender (const char *format, ...)
    __attribute__ ((__format__ (__printf__, 1, 2)));
#endif

/*
 * Program-specific state variables, settable from the command line, or from
 * embedded input stream commands.  These options may be set by the embedded
 * command parser to values other than strictly TRUE or FALSE; all non-zero
 * values are equivalent to TRUE.
 */
#if 0
static int do_echo = TRUE,          /* Echo characters */
           do_errors = TRUE,        /* Print error messages to stderr */
           do_commands = TRUE,      /* Execute embedded commands */
           do_combinations = TRUE,  /* Execute [...] combinations */
           do_comments = TRUE;      /* Allow {...} as comments */
#endif

static cw_config_t *config = NULL;
static const char *all_options = "s:|system,d:|device,"
	"w:|wpm,t:|tone,v:|volume,"
	"g:|gap,k:|weighting,"
	"f:|infile,"
	"e|noecho,m|nomessages,c|nocommands,o|nocombinations,p|nocomments,"
	"h|help,V|version";
static const char *argv0 = NULL;



/*---------------------------------------------------------------------*/
/*  Convenience functions                                              */
/*---------------------------------------------------------------------*/

/*
 * write_to_echo_stream()
 * write_to_message_stream()
 *
 * Local fprintf functions that suppress output to if the appropriate flag
 * is not set; writes are synchronously flushed.
 */
static void
write_to_echo_stream (const char *format, ...)
{
  if (config->do_echo)
    {
      va_list ap;

      va_start (ap, format);
      vfprintf (stdout, format, ap);
      fflush (stdout);
      va_end (ap);
    }
}

static void
write_to_message_stream (const char *format, ...)
{
  if (config->do_errors)
    {
      va_list ap;

      va_start (ap, format);
      vfprintf (stderr, format, ap);
      fflush (stderr);
      va_end (ap);
    }
}


/*
 * write_to_cw_sender()
 *
 * Fprintf-like function that allows us to conveniently print to the cw
 * output 'stream'.
 */
static void
write_to_cw_sender (const char *format, ...)
{
  va_list ap;
  char buffer[128];

  /*
   * Format the CW send buffer using vsnprintf.  Formatted strings longer than
   * the declared buffer will be silently truncated to the buffer length.
   */
  va_start (ap, format);
  vsnprintf (buffer, sizeof (buffer), format, ap);
  va_end (ap);

  /* Sound the buffer, and wait for the send to complete. */
  if (!cw_send_string (buffer))
    {
      perror ("cw_send_string");
      cw_flush_tone_queue ();
      abort ();
    }
  if (!cw_wait_for_tone_queue_critical (1))
    {
      perror ("cw_wait_for_tone_queue_critical");
      cw_flush_tone_queue ();
      abort ();
    }
}


/*---------------------------------------------------------------------*/
/*  Embedded commands handling                                         */
/*---------------------------------------------------------------------*/

/*
 * parse_stream_query()
 *
 * Handle a query received in the input stream.  The command escape character
 * and the query character have already been read and recognized.
 */
static void
parse_stream_query (FILE *stream)
{
  int c, value;

  c = toupper (fgetc (stream));
  switch (c)
    {
    case EOF:
      return;
    default:
      write_to_message_stream ("%c%c%c", CW_STATUS_ERR, CW_CMD_QUERY, c);
      return;
    case CW_CMDV_FREQUENCY:
      value = cw_get_frequency ();
      break;
    case CW_CMDV_VOLUME:
      value = cw_get_volume ();
      break;
    case CW_CMDV_SPEED:
      value = cw_get_send_speed ();
      break;
    case CW_CMDV_GAP:
      value = cw_get_gap ();
      break;
    case CW_CMDV_WEIGHTING:
      value = cw_get_weighting ();
      break;
    case CW_CMDV_ECHO:
      value = config->do_echo;
      break;
    case CW_CMDV_ERRORS:
      value = config->do_errors;
      break;
    case CW_CMDV_COMMANDS:
      value = config->do_commands;
      break;
    case CW_CMDV_COMBINATIONS:
      value = config->do_combinations;
      break;
    case CW_CMDV_COMMENTS:
      value = config->do_comments;
      break;
    }

  /* Write the value obtained above to the message stream. */
  write_to_message_stream ("%c%c%d", CW_STATUS_OK, c, value);
}


/*
 * parse_stream_cwquery()
 *
 * Handle a cwquery received in the input stream.  The command escape
 * character and the cwquery character have already been read and recognized.
 */
static void
parse_stream_cwquery (FILE *stream)
{
  int c, value;
  const char *format;

  c = toupper (fgetc (stream));
  switch (c)
    {
    case EOF:
      return;
    default:
      write_to_message_stream ("%c%c%c", CW_STATUS_ERR, CW_CMD_CWQUERY, c);
      return;
    case CW_CMDV_FREQUENCY:
      value = cw_get_frequency ();
      format = _("%d HZ ");
      break;
    case CW_CMDV_VOLUME:
      value = cw_get_volume ();
      format = _("%d PERCENT ");
      break;
    case CW_CMDV_SPEED:
      value = cw_get_send_speed ();
      format = _("%d WPM ");
      break;
    case CW_CMDV_GAP:
      value = cw_get_gap ();
      format = _("%d DOTS ");
      break;
    case CW_CMDV_WEIGHTING:
      value = cw_get_weighting ();
      format = _("%d PERCENT ");
      break;
    case CW_CMDV_ECHO:
      value = config->do_echo;
      format = _("ECHO %s ");
      break;
    case CW_CMDV_ERRORS:
      value = config->do_errors;
      format = _("ERRORS %s ");
      break;
    case CW_CMDV_COMMANDS:
      value = config->do_commands;
      format = _("COMMANDS %s ");
      break;
    case CW_CMDV_COMBINATIONS:
      value = config->do_combinations;
      format = _("COMBINATIONS %s ");
      break;
    case CW_CMDV_COMMENTS:
      value = config->do_comments;
      format = _("COMMENTS %s ");
      break;
    }

  switch (c)
    {
    case CW_CMDV_FREQUENCY:
    case CW_CMDV_VOLUME:
    case CW_CMDV_SPEED:
    case CW_CMDV_GAP:
    case CW_CMDV_WEIGHTING:
      write_to_cw_sender (format, value);
      break;
    case CW_CMDV_ECHO:
    case CW_CMDV_ERRORS:
    case CW_CMDV_COMMANDS:
    case CW_CMDV_COMBINATIONS:
    case CW_CMDV_COMMENTS:
      write_to_cw_sender (format, value ? _("ON") : _("OFF"));
      break;
    }
}


/*
 * parse_stream_parameter()
 *
 * Handle a parameter setting command received in the input stream.  The
 * command type character has already been read from the stream, and is passed
 * in as the first argument.
 */
static void
parse_stream_parameter (int c, FILE *stream)
{
  int value;
  int (*value_handler) (int);

  /* Parse and check the new parameter value. */
  if (fscanf (stream, "%d;", &value) != 1)
    {
      write_to_message_stream ("%c%c", CW_STATUS_ERR, c);
      return;
    }

  /* Either assign a handler, or update the local flag, as appropriate. */
  value_handler = NULL;
  switch (c)
    {
    case EOF:
    default:
      return;
    case CW_CMDV_FREQUENCY:
      value_handler = cw_set_frequency;
      break;
    case CW_CMDV_VOLUME:
      value_handler = cw_set_volume;
      break;
    case CW_CMDV_SPEED:
      value_handler = cw_set_send_speed;
      break;
    case CW_CMDV_GAP:
      value_handler = cw_set_gap;
      break;
    case CW_CMDV_WEIGHTING:
      value_handler = cw_set_weighting;
      break;
    case CW_CMDV_ECHO:
      config->do_echo = value;
      break;
    case CW_CMDV_ERRORS:
      config->do_errors = value;
      break;
    case CW_CMDV_COMMANDS:
      config->do_commands = value;
      break;
    case CW_CMDV_COMBINATIONS:
      config->do_combinations = value;
      break;
    case CW_CMDV_COMMENTS:
      config->do_comments = value;
      break;
    }

  /*
   * If not a local flag, apply the new value to a CW library control using
   * the handler assigned above.
   */
  if (value_handler)
    {
      if (!(*value_handler) (value))
        {
          write_to_message_stream ("%c%c", CW_STATUS_ERR, c);
          return;
        }
    }

  /* Confirm the new value with a stderr message. */
  write_to_message_stream ("%c%c%d", CW_STATUS_OK, c, value);
}


/*
 * parse_stream_command()
 *
 * Handle a command received in the input stream.  The command escape
 * character has already been read and recognized.
 */
static void
parse_stream_command (FILE *stream)
{
  int c;

  c = toupper (fgetc (stream));
  switch (c)
    {
    case EOF:
      return;
    default:
      write_to_message_stream ("%c%c%c", CW_STATUS_ERR, CW_CMD_ESCAPE, c);
      return;
    case CW_CMDV_FREQUENCY:
    case CW_CMDV_VOLUME:
    case CW_CMDV_SPEED:
    case CW_CMDV_GAP:
    case CW_CMDV_WEIGHTING:
    case CW_CMDV_ECHO:
    case CW_CMDV_ERRORS:
    case CW_CMDV_COMMANDS:
    case CW_CMDV_COMBINATIONS:
    case CW_CMDV_COMMENTS:
      parse_stream_parameter (c, stream);
      break;
    case CW_CMD_QUERY:
      parse_stream_query (stream);
      break;
    case CW_CMD_CWQUERY:
      parse_stream_cwquery (stream);
      break;
    case CW_CMDV_QUIT:
      cw_flush_tone_queue ();
      write_to_echo_stream ("%c", '\n');
      exit (EXIT_SUCCESS);
    }
}


/*---------------------------------------------------------------------*/
/*  Input stream handling                                              */
/*---------------------------------------------------------------------*/

/*
 * send_cw_character()
 *
 * Sends the given character to the CW sender, and waits for it to complete
 * sounding the tones.  The character to send may be a partial or a complete
 * character.
 */
static void
send_cw_character (int c, int is_partial)
{
  int character, status;

  /* Convert all whitespace into a single space. */
  character = isspace (c) ? ' ' : c;

  /* Send the character to the CW sender. */
  status = is_partial ? cw_send_character_partial (character)
                      : cw_send_character (character);
  if (!status)
    {
      if (errno != ENOENT)
        {
          perror ("cw_send_character[_partial]");
          cw_flush_tone_queue ();
          abort ();
        }
      else
        {
          write_to_message_stream ("%c%c", CW_STATUS_ERR, character);
          return;
        }
    }

  /* Echo the original character while sending it. */
  write_to_echo_stream ("%c", c);

  /* Wait for the character to complete. */
  if (!cw_wait_for_tone_queue_critical (1))
    {
      perror ("cw_wait_for_tone_queue_critical");
      cw_flush_tone_queue ();
      abort ();
    }
}


/*
 * parse_stream()
 *
 * Read characters from a file stream, and either sound them, or interpret
 * controls in them.  Returns on end of file.
 */
static void
parse_stream (FILE *stream)
{
  int c;
  enum { NONE, COMBINATION, COMMENT, NESTED_COMMENT } state = NONE;

  /*
   * Cycle round states depending on input characters.  Comments may be
   * nested inside combinations, but not the other way around; that is,
   * combination starts and ends are not special within comments.
   */
  for (c = fgetc (stream); !feof (stream); c = fgetc (stream))
    {
      switch (state)
        {
        case NONE:
          /*
           * Start a comment or combination, handle a command escape, or send
           * the character if none of these checks apply.
           */
          if (config->do_comments && c == CW_COMMENT_START)
            {
              state = COMMENT;
              write_to_echo_stream ("%c", c);
            }
          else if (config->do_combinations && c == CW_COMBINATION_START)
            {
              state = COMBINATION;
              write_to_echo_stream ("%c", c);
            }
          else if (config->do_commands && c == CW_CMD_ESCAPE)
            parse_stream_command (stream);
          else
            send_cw_character (c, FALSE);
          break;

        case COMBINATION:
          /*
           * Start a comment nested in a combination, end a combination,
           * handle a command escape, or send the character if none of these
           * checks apply.
           */
          if (config->do_comments && c == CW_COMMENT_START)
            {
              state = NESTED_COMMENT;
              write_to_echo_stream ("%c", c);
            }
          else if (c == CW_COMBINATION_END)
            {
              state = NONE;
              write_to_echo_stream ("%c", c);
            }
          else if (config->do_commands && c == CW_CMD_ESCAPE)
            parse_stream_command (stream);
          else
            {
              /*
               * If this is the final character in the combination, do not
               * suppress the end of character delay.  To do this, look ahead
               * the next character, and suppress unless combination end.
               */
              int lookahead;

              lookahead = fgetc (stream);
              ungetc (lookahead, stream);
              send_cw_character (c, lookahead != CW_COMBINATION_END);
            }
          break;

        case COMMENT:
        case NESTED_COMMENT:
          /*
           * If in a comment nested in a combination and comment end seen,
           * revert state to reflect in combination only.  If in an unnested
           * comment and comment end seen, reset state.
           */
          if (c == CW_COMMENT_END)
            state = (state == NESTED_COMMENT) ? COMBINATION : NONE;
          write_to_echo_stream ("%c", c);
          break;
        }
    }
}


/*---------------------------------------------------------------------*/
/*  Command line mechanics                                             */
/*---------------------------------------------------------------------*/
#if 0
/*
 * print_usage()
 *
 * Print out a brief message directing the user to the help function.
 */
static void
print_usage (const char *argv0)
{
  const char *format;

  format = has_longopts ()
    ? _("Try '%s --help' for more information.\n")
    : _("Try '%s -h' for more information.\n");

  fprintf (stderr, format, argv0);
  exit (EXIT_FAILURE);
}

/*
 * print_help()
 *
 * Print out a brief page of help information.
 */
static void
print_help (const char *argv0)
{
  const char *format;
  int min_speed, max_speed, min_frequency, max_frequency, min_volume,
      max_volume, min_gap, max_gap, min_weighting, max_weighting;

  cw_reset_send_receive_parameters ();
  cw_get_speed_limits (&min_speed, &max_speed);
  cw_get_frequency_limits (&min_frequency, &max_frequency);
  cw_get_volume_limits (&min_volume, &max_volume);
  cw_get_gap_limits (&min_gap, &max_gap);
  cw_get_weighting_limits (&min_weighting, &max_weighting);

  format = has_longopts ()
    ? _("Usage: %s [options...]\n\n"
      "  -s, --sound=SYSTEM     generate sound using SYSTEM sound system"
      " [default 'soundcard']\n"
      "                         one of 's[oundcard]', 'c[onsole]'\n"
      "  -x, --sdevice=SDEVICE  use SDEVICE for soundcard sound [default %s]\n"
      "  -d, --cdevice=CDEVICE  use CDEVICE for console sound [default %s]\n"
      "  -f, --file=FILE        read from FILE [default stdin]\n")
    : _("Usage: %s [options...]\n\n"
      "  -s SYSTEM   generate sound using SYSTEM sound system [default 'console']\n"
      "              one of 'c[onsole]', 's[oundcard]'\n"
      "  -x SDEVICE  use SDEVICE for soundcard sound [default %s]\n"
      "  -d CDEVICE  use CDEVICE for console sound [default %s]\n"
      "  -f FILE     read from FILE [default stdin]\n");

  printf (format, argv0,
	  CW_DEFAULT_OSS_DEVICE,
	  CW_DEFAULT_CONSOLE_DEVICE);

  format = has_longopts ()
    ? _("  -w, --wpm=WPM          set initial words per minute [default %d]\n"
      "                         valid WPM values are between %d and %d\n"
      "  -t, --hz,--tone=HZ     set initial tone to HZ [default %d]\n"
      "                         valid HZ values are between %d and %d\n"
      "  -v, --volume=PERCENT   set initial volume to PERCENT [default %d]\n"
      "                         valid PERCENT values are between %d and %d\n")
    : _("  -w WPM      set initial words per minute [default %d]\n"
      "              valid WPM values are between %d and %d\n"
      "  -t HZ       set initial tone to HZ [default %d]\n"
      "              valid HZ values are between %d and %d\n"
      "  -v PERCENT  set initial volume to PERCENT [default %d]\n"
      "              valid PERCENT values are between %d and %d\n");

  printf (format,
          cw_get_send_speed (), min_speed, max_speed,
          cw_get_frequency (), min_frequency, max_frequency,
          cw_get_volume (), min_volume, max_volume);

  format = has_longopts ()
    ? _("  -g, --gap=GAP          set extra gap between letters [default %d]\n"
      "                         valid GAP values are between %d and %d\n"
      "  -k, --weighting=WEIGHT set weighting to WEIGHT [default %d]\n"
      "                         valid WEIGHT values are between %d and %d\n"
      "  -e, --noecho           don't echo sending to stdout [default echo]\n"
      "  -m, --nomessages       don't write messages to stderr"
      " [default messages]\n")
    : _("  -g GAP      set extra gap between letters [default %d]\n"
      "              valid GAP values are between %d and %d\n"
      "  -k WEIGHT   set weighting to WEIGHT [default %d]\n"
      "              valid WEIGHT values are between %d and %d\n"
      "  -e          don't echo sending to stdout [default echo]\n"
      "  -m          don't write messages to stderr [default messages]\n");

  printf (format,
          cw_get_gap (), min_gap, max_gap,
          cw_get_weighting (), min_weighting, max_weighting);

  format = has_longopts ()
    ? _("  -c, --nocommands       don't execute embedded commands"
      " [default commands]\n"
      "  -o, --nocombinations   don't allow [...] combinations"
      " [default combos]\n"
      "  -p, --nocomments       don't allow {...} comments [default comments]\n"
      "  -h, --help             print this message\n"
      "  -V, --version          output version information and exit\n\n")
    : _("  -c          don't execute embedded commands [default commands]\n"
      "  -o          don't allow [...] combinations [default combinations]\n"
      "  -p          don't allow {...} comments [default comments]\n"
      "  -h          print this message\n"
      "  -V          output version information and exit\n\n");

  printf (format);
  exit (EXIT_SUCCESS);
}


/*
 * parse_command_line()
 *
 * Parse the command line options for initial values for the various
 * global and flag definitions.
 */
static void
parse_command_line (int argc, char *const argv[])
{

  int option;
  char *argument;

  argv0 = program_basename (argv[0]);
  while (get_option (argc, argv,
                     _("s:|sound,d:|cdevice,x:|sdevice,y:|mdevice,f:|file,"
                     "t:|tone,t:|hz,v:|volume,w:|wpm,g:|gap,k:|weighting,"
                     "e|noecho,m|nomessages,c|nocommands,o|nocombinations,"
                     "p|nocomments,h|help,V|version"),
                     &option, &argument))
    {
      int intarg;

      switch (option)
        {
        case 's':
          if (strcoll (argument, _("console")) == 0
              || strcoll (argument, _("c")) == 0)
            {
              is_console = TRUE;
              is_soundcard = FALSE;
            }
          else if (strcoll (argument, _("soundcard")) == 0
                   || strcoll (argument, _("s")) == 0)
            {
              is_console = FALSE;
              is_soundcard = TRUE;
            }
#if 0
          else if (strcoll (argument, _("both")) == 0
                   || strcoll (argument, _("b")) == 0)
            {
              is_console = TRUE;
              is_soundcard = TRUE;
            }
#endif
          else
            {
              fprintf (stderr, _("%s: invalid sound source\n"), argv0);
              exit (EXIT_FAILURE);
            }
          break;

        case 'd':
          console_device = argument;
          break;

        case 'x':
          soundcard_device = argument;
          break;
#if 0
        case 'y':
          mixer_device = argument;
          break;
#endif
        case 'f':
          if (!freopen (argument, "r", stdin))
            {
              fprintf (stderr, _("%s: error opening input file\n"), argv0);
              perror (argument);
              exit (EXIT_FAILURE);
            }
          break;

        case 't':
          if (sscanf (argument, "%d", &intarg) != 1
              || !cw_set_frequency (intarg))
            {
              fprintf (stderr, _("%s: invalid tone value\n"), argv0);
              exit (EXIT_FAILURE);
            }
          break;

        case 'v':
          if (sscanf (argument, "%d", &intarg) != 1
              || !cw_set_volume (intarg))
            {
              fprintf (stderr, _("%s: invalid volume value\n"), argv0);
              exit (EXIT_FAILURE);
            }
          break;

        case 'w':
          if (sscanf (argument, "%d", &intarg) != 1
              || !cw_set_send_speed (intarg))
            {
              fprintf (stderr, _("%s: invalid wpm value\n"), argv0);
              exit (EXIT_FAILURE);
            }
          break;

        case 'g':
          if (sscanf (argument, "%d", &intarg) != 1
              || !cw_set_gap (intarg))
            {
              fprintf (stderr, _("%s: invalid gap value\n"), argv0);
              exit (EXIT_FAILURE);
            }
          break;

        case 'i':
          if (sscanf (argument, "%d", &intarg) != 1
              || !cw_set_weighting (intarg))
            {
              fprintf (stderr, _("%s: invalid weighting value\n"), argv0);
              exit (EXIT_FAILURE);
            }
          break;

        case 'e':
          do_echo = FALSE;
          break;

        case 'm':
          do_errors = FALSE;
          break;

        case 'c':
          do_commands = FALSE;
          break;

        case 'o':
          do_combinations = FALSE;
          break;

        case 'p':
          do_comments = FALSE;
          break;

        case 'h':
          print_help (argv0);

        case 'V':
          printf (_("%s version %s, %s\n"),
                  argv0, PACKAGE_VERSION, _(CW_COPYRIGHT));
          exit (EXIT_SUCCESS);

        case '?':
          print_usage (argv0);

        default:
          fprintf (stderr, _("%s: getopts returned '%c'\n"), argv0, option);
          exit (EXIT_FAILURE);
        }
    }
  if (get_optind () != argc)
    print_usage (argv0);

  /* Deal with odd argument combinations. */
  if (!is_console && console_device)
    {
      fprintf (stderr, _("%s: no console sound: -d invalid\n"), argv0);
      print_usage (argv0);
    }
  else if (!is_soundcard)
    {
      if (soundcard_device)
        {
          fprintf (stderr, _("%s: no soundcard sound: -x invalid\n"), argv0);
          print_usage (argv0);
        }
#if 0
      else if (mixer_device)
        {
          fprintf (stderr, _("%s: no soundcard sound: -y invalid\n"), argv0);
          print_usage (argv0);
        }
#endif
    }

}
#endif

/*
 * main()
 *
 * Parse command line args, then produce CW output until end of file.
 */
int main (int argc, char *const argv[])
{
	argv0 = program_basename(argv[0]);

	/* Set locale and message catalogs. */
	i18n_initialize();

	/* Parse combined environment and command line arguments. */
	int combined_argc;
	char **combined_argv;
	combine_arguments("CW_OPTIONS", argc, argv, &combined_argc, &combined_argv);

	config = cw_config_new();
	if (!config) {
		return EXIT_FAILURE;
	}
	config->is_cw = 1;

	if (!cw_process_argv(argc, argv, all_options, config)) {
		fprintf(stderr, _("%s: failed to parse command line args\n"), argv0);
		return EXIT_FAILURE;
	}
	if (!cw_config_is_valid(config)) {
		fprintf(stderr, _("%s: inconsistent arguments\n"), argv0);
		return EXIT_FAILURE;
	}

	if (config->input_file) {
		if (!freopen(config->input_file, "r", stdin)) {
			fprintf(stderr, _("%s: %s\n"), argv0, strerror(errno));
			fprintf(stderr, _("%s: error opening input file %s\n"), argv0, config->input_file);
			return EXIT_FAILURE;
		}
	}

	if (!cw_generator_new_from_config(config, argv0)) {
		fprintf(stderr, "%s: failed to create generator\n", argv0);
		return EXIT_FAILURE;
	}

	/* Set up signal handlers to exit on a range of signals. */
	int index;
	static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };
	for (index = 0; SIGNALS[index] != 0; index++) {
		if (!cw_register_signal_handler(SIGNALS[index], SIG_DFL)) {
			fprintf(stderr, _("%s: can't register signal: %s\n"), argv0, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	/* Start producing sine wave (amplitude of the wave will be
	   zero as long as there are no characters to process). */
	cw_generator_start();

	/* Send stdin stream to CW parsing. */
	parse_stream(stdin);

	/* Await final tone completion before exiting. */
	cw_wait_for_tone_queue();

	cw_generator_stop();
	cw_generator_delete();

	cw_config_delete(&config);

	return EXIT_SUCCESS;
}
