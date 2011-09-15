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

#include "../config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#if defined(HAVE_GETOPT_H)
# include <getopt.h>
#endif

#include "cmdline.h"

#include "memory.h"


/*---------------------------------------------------------------------*/
/*  Command line helpers                                               */
/*---------------------------------------------------------------------*/

/*
 * program_basename()
 *
 * Return the program's base name from the given argv[0].
 */
const char *
program_basename (const char *argv0)
{
  const char *base;

  base = strrchr (argv0, '/');
  return base ? base + 1 : argv0;
}


/*
 * combine_arguments()
 *
 * Build a new argc and argv by combining command line and environment
 * options.
 *
 * The new values are held in the heap, and the malloc'ed addresses are not
 * retained, so do not call this function repeatedly, otherwise it will leak
 * memory.
 */
void
combine_arguments (const char *env_variable,
                   int argc, char *const argv[],
                   int *new_argc, char **new_argv[])
{
  int local_argc;
  char **local_argv, *env_options;
  int arg;

  /* Begin with argv[0], which stays in place. */
  local_argv = safe_malloc (sizeof (*local_argv));
  local_argc = 0;
  local_argv[local_argc++] = argv[0];

  /* If options are given in an environment variable, add these next. */
  env_options = getenv (env_variable);
  if (env_options)
    {
      char *options, *option;

      options = safe_strdup (env_options);
      for (option = strtok (options, " \t"); option;
           option = strtok (NULL, " \t"))
        {
          local_argv = safe_realloc (local_argv,
                                     sizeof (*local_argv) * (local_argc + 1));
          local_argv[local_argc++] = option;
        }
    }

  /* Append the options given on the command line itself. */
  for (arg = 1; arg < argc; arg++)
    {
      local_argv = safe_realloc (local_argv,
                                 sizeof (*local_argv) * (local_argc + 1));
      local_argv[local_argc++] = argv[arg];
    }

  /* Return the constructed argc/argv. */
  *new_argc = local_argc;
  *new_argv = local_argv;
}


/*---------------------------------------------------------------------*/
/*  Option handling helpers                                            */
/*---------------------------------------------------------------------*/

enum { FALSE = 0, TRUE = !FALSE };


/*
 * has_longopts()
 *
 * Returns TRUE if the system supports long options, FALSE otherwise.
 */
int
has_longopts (void)
{
#if defined(HAVE_GETOPT_LONG)
  return TRUE;
#else
  return FALSE;
#endif
}


/*
 * get_option()
 *
 * Adapter wrapper round getopt() and getopt_long().  Descriptor strings are
 * comma-separated groups of elements of the form "c[:]|longopt", giving the
 * short form option ('c'), ':' if it requires an argument, and the long form
 * option.
 */
int
get_option (int argc, char *const argv[],
            const char *descriptor,
            int *option, char **argument)
{
  static char *option_string = NULL;          /* Standard getopt() string */
#if defined(HAVE_GETOPT_LONG)
  static struct option *long_options = NULL;  /* getopt_long() structure */
  static char **long_names = NULL;            /* Allocated names array */
  static int long_count = 0;                  /* Entries in long_options */
#endif

  int opt;

  /*
   * If this is the first call, build a new option_string and a matching
   * set of long options.
   */
  if (!option_string)
    {
      char *options, *element;

      /* Begin with an empty short options string. */
      option_string = safe_strdup ("");

      /* Break the descriptor into comma-separated elements. */
      options = safe_strdup (descriptor);
      for (element = strtok (options, ","); element;
           element = strtok (NULL, ","))
        {
          int needs_arg;

          /* Determine if this option requires an argument. */
          needs_arg = element[1] == ':';

          /*
           * Append the short option character, and ':' if present, to the
           * short options string.  For simplicity in reallocating, assume
           * that the ':' is always there.
           */
          option_string = safe_realloc (option_string,
                                        strlen (option_string) + 3);
          strncat (option_string, element, needs_arg ? 2 : 1);

#if defined(HAVE_GETOPT_LONG)
          /*
           * Take a copy of the long name and add it to a retained array.
           * Because struct option makes name a const char*, we can't just
           * store it in there and then free later.
           */
          long_names = safe_realloc (long_names,
                                     sizeof (*long_names) * (long_count + 1));
          long_names[long_count] = safe_strdup (element + (needs_arg ? 3 : 2));

          /* Add a new entry to the long options array. */
          long_options = safe_realloc (long_options,
                                     sizeof (*long_options) * (long_count + 2));
          long_options[long_count].name = long_names[long_count];
          long_options[long_count].has_arg = needs_arg;
          long_options[long_count].flag = NULL;
          long_options[long_count].val = element[0];
          long_count++;

          /* Set the end sentry to all zeroes. */
          memset (long_options + long_count, 0, sizeof (*long_options));
#endif
        }

      free (options);
    }

    /* Call the appropriate getopt function to get the first/next option. */
#if defined(HAVE_GETOPT_LONG)
    opt = getopt_long (argc, argv, option_string, long_options, NULL);
#else
    opt = getopt (argc, argv, option_string);
#endif

    /* If no more options, clean up allocated memory before returning. */
    if (opt == -1)
      {
#if defined(HAVE_GETOPT_LONG)
        int index;

        /*
         * Free each long option string created above, using the long_names
         * growable array because the long_options[i].name aliased to it is
         * a const char*.  Then free long_names itself, and reset pointer.
         */
        for (index = 0; index < long_count; index++)
          free (long_names[index]);
        free (long_names);
        long_names = NULL;

        /* Free the long options structure, and reset pointer and counter. */
        free (long_options);
        long_options = NULL;
        long_count = 0;
#endif
        /* Free and reset the retained short options string. */
        free (option_string);
        option_string = NULL;
      }

    /* Return the option and argument, with FALSE if no more arguments. */
    *option = opt;
    *argument = optarg;
    return !(opt == -1);
}


/*
 * get_optind()
 *
 * Return the value of getopt()'s optind after get_options() calls complete.
 */
int
get_optind (void)
{
  return optind;
}
