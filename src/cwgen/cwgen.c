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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "i18n.h"
#include "cmdline.h"
#include "copyright.h"
#include "memory.h"


/*---------------------------------------------------------------------*/
/*  Module variables, miscellaneous other stuff                        */
/*---------------------------------------------------------------------*/

/* Assorted definitions and constants. */
enum { FALSE = 0, TRUE = !FALSE };

enum
{ MIN_GROUPS = 1,         /* Lowest number of groups allowed */
  INITIAL_GROUPS = 128,   /* Default groups */
  MIN_GROUP_SIZE = 1,     /* Lowest group size allowed */
  INITIAL_GROUP_SIZE = 5, /* Default group size */
  INITIAL_REPEAT = 0,     /* Default repeat count */
  MIN_REPEAT = 0,         /* Lowest repeat count allowed */
  MIN_LIMIT = 0,          /* Lowest character count limit allowed */
  INITIAL_LIMIT = 0       /* Default character count limit */
};
static const char *const DEFAULT_CHARSET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                           "0123456789";

/* Global variables. */
static int groups = INITIAL_GROUPS,
           group_min  = INITIAL_GROUP_SIZE,
           group_max  = INITIAL_GROUP_SIZE,
           repeat  = INITIAL_REPEAT,
           limit = INITIAL_LIMIT;
static const char *charset  = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";


/*---------------------------------------------------------------------*/
/*  Character generation                                               */
/*---------------------------------------------------------------------*/

/*
 * generate_characters()
 *
 * Generate random characters on stdout, in groups as requested, and up
 * to the requested number of groups.  Characters are selected from the
 * set given at random.
 */
static void
generate_characters (int groups, int repeat,
                     int group_min, int group_max, int limit,
                     const char *charset)
{
  static int is_initialized = FALSE;

  int charset_length, group, chars;
  char *buffer;

  /* On first (usually only) call, seed the random number generator. */
  if (!is_initialized)
    {
      srand (time (NULL));

      is_initialized = TRUE;
    }

  /* Allocate the buffer for repeating groups. */
  buffer = safe_malloc (group_max);

  /* Generate groups up to the number requested or to the character limit. */
  charset_length = strlen (charset);
  chars = 0;
  for (group = 0;
       group < groups && (limit == 0 || chars < limit); group++)
    {
      int size, index, count;

      /* Randomize the group size between min and max inclusive. */
      size = group_min + rand () % (group_max - group_min + 1);

      /* Pick and buffer random characters from the set. */
      for (index = 0; index < size; index++)
        buffer[index] = charset[rand () % charset_length];

      /*
       * Repeatedly print the group as requested.  It's always printed once,
       * then repeated for the desired repeat count.  Break altogether if we
       * hit any set limit on printed characters.
       */
      count = 0;
      do
        {
          for (index = 0;
               index < size && (limit == 0 || chars < limit);
               index++, chars++)
            {
              /* Print with immediate buffer flush. */
              putchar (buffer[index]);
              fflush (stdout);
            }

          putchar (' ');
          fflush (stdout);
        }
      while (count++ < repeat && (limit == 0 || chars < limit));
    }

  free (buffer);
}


/*---------------------------------------------------------------------*/
/*  Command line mechanics                                             */
/*---------------------------------------------------------------------*/

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

  format = has_longopts ()
    ? _("Usage: %s [options...]\n\n"
      "  -g, --groups=GROUPS    send GROUPS groups of chars [default %d]\n"
      "                         GROUPS values may not be lower than %d\n"
      "  -n, --groupsize=GS     make groups GS chars [default %d]\n"
      "                         GS values may not be lower than %d, or\n"
      "  -n, --groupsize=GL-GH  make groups between GL and GH chars\n"
      "                         valid GL, GH values are as for GS above\n")
    : _("Usage: %s [options...]\n\n"
      "  -g GROUPS   send GROUPS groups of chars [default %d]\n"
      "              GROUPS values may not be lower than %d\n"
      "  -n GS       make groups GS chars [default %d]\n"
      "              GS values may not be lower than %d, or\n"
      "  -n GL-GH    make groups between GL and GH chars\n"
      "              valid GL, GH values are as for GS above\n");

  printf (format, argv0,
          INITIAL_GROUPS, MIN_GROUPS,
          INITIAL_GROUP_SIZE, MIN_GROUP_SIZE);

  format = has_longopts ()
    ? _("  -r, --repeat=COUNT     repeat each group COUNT times [default %d]\n"
      "                         COUNT values may not be lower than %d\n"
      "  -c, --charset=CHARSET  select chars to send from this set\n"
      "                         [default %s]\n"
      "  -x, --limit=LIMIT      stop after LIMIT characters [default %d]\n"
      "                         a LIMIT of zero indicates no set limit\n"
      "  -h, --help             print this message\n"
      "  -v, --version          output version information and exit\n\n")
    : _("  -r COUNT    repeat each group COUNT times [default %d]\n"
      "              COUNT values may not be lower than %d\n"
      "  -c CHARSET  select chars to send from this set\n"
      "              [default %s]\n"
      "  -x LIMIT    stop after LIMIT characters [default %d]\n"
      "              a LIMIT of zero indicates no set limit\n"
      "  -h          print this message\n"
      "  -v          output version information and exit\n\n");

  printf (format,
          INITIAL_REPEAT, MIN_REPEAT,
          DEFAULT_CHARSET, INITIAL_LIMIT);
  exit (EXIT_SUCCESS);
}


/*
 * parse_command_line()
 *
 * Parse the command line options for initial values for the various
 * global and flag definitions.
 */
static void
parse_command_line (int argc, char **argv)
{
  int option;
  char *argument;

  const char *argv0 = cw_program_basename (argv[0]);
  while (get_option (argc, argv,
                     _("g:|groups,n:|groupsize,r:|repeat,x:|limit,c:|charset,"
                     "h|help,v|version"),
                     &option, &argument))
    {
      switch (option)
        {
        case 'g':
          if (sscanf (argument, "%d", &groups) != 1
              || groups < MIN_GROUPS)
            {
              fprintf (stderr, _("%s: invalid groups value\n"), argv0);
              exit (EXIT_FAILURE);
            }
          break;

        case 'n':
          if (sscanf (argument, "%d-%d", &group_min, &group_max) == 2)
            {
              if (group_min < MIN_GROUP_SIZE || group_max < MIN_GROUP_SIZE
                  || group_min > group_max)
                {
                  fprintf (stderr, _("%s: invalid groupsize range\n"), argv0);
                  exit (EXIT_FAILURE);
                }
            }
          else if (sscanf (argument, "%d", &group_min) == 1)
            {
              if (group_min < MIN_GROUP_SIZE)
                {
                  fprintf (stderr, _("%s: invalid groupsize value\n"), argv0);
                  exit (EXIT_FAILURE);
                }
              group_max = group_min;
            }
          break;

        case 'r':
          if (sscanf (argument, "%d", &repeat) != 1
              || repeat < MIN_REPEAT)
            {
              fprintf (stderr, _("%s: invalid repeat value\n"), argv0);
              exit (EXIT_FAILURE);
            }
          break;

        case 'x':
          if (sscanf (argument, "%d", &limit) != 1
              || limit < MIN_LIMIT)
            {
              fprintf (stderr, _("%s: invalid limit value\n"), argv0);
              exit (EXIT_FAILURE);
            }
          break;

        case 'c':
          if (strlen (argument) == 0)
            {
              fprintf (stderr, _("%s: charset cannot be empty\n"), argv0);
              exit (EXIT_FAILURE);
            }
          charset = argument;
          break;

        case 'h':
          print_help (argv0);

        case 'v':
          printf (_("%s version %s\n%s\n"),
                  argv0, PACKAGE_VERSION, _(CW_COPYRIGHT));
          exit (EXIT_SUCCESS);

        case '?':
          print_usage (argv0);

        default:
          fprintf (stderr, _("%s: getopts returned %c\n"), argv0, option);
          exit (EXIT_FAILURE);
        }
    }
  if (get_optind () != argc)
    print_usage (argv0);
}


/*
 * main()
 *
 * Parse the command line options, then generate the characters requested.
 */
int
main (int argc, char **argv)
{
  int combined_argc;
  char **combined_argv;

  /* Set locale and message catalogs. */
  i18n_initialize ();

  /* Parse combined environment and command line arguments. */
  combine_arguments (_("CWGEN_OPTIONS"),
                     argc, argv, &combined_argc, &combined_argv);
  parse_command_line (combined_argc, combined_argv);

  /* Generate the character groups as requested. */
  generate_characters (groups, repeat, group_min, group_max, limit, charset);
  putchar ('\n');

  return EXIT_SUCCESS;
}
