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

#include "cwlib.h"
#include "cmdline.h"
#include "i18n.h"
#include "memory.h"
#include "cwlib.h"
#include "copyright.h"


static int cw_process_option(int opt, const char *optarg, cw_config_t *config, const char *argv0);
static void cw_print_usage(const char *argv0);


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



void cw_print_help(const char *argv0, cw_config_t *config)
{
	/* TODO: make use of this */
	/* int format = has_longopts() */
	fprintf(stderr, _("Usage: %s [options...]\n\n"), argv0);

	fprintf(stderr, _("Audio system options:\n"));
	fprintf(stderr, _("  -s, --system=SYSTEM\n"));
	fprintf(stderr, _("        generate sound using SYSTEM audio system\n"));
	fprintf(stderr, _("        SYSTEM: {console|oss|alsa|soundcard}\n"));
	fprintf(stderr, _("        'console': use system console/buzzer\n"));
	fprintf(stderr, _("               this output may require root privileges\n"));
	fprintf(stderr, _("        'oss': use OSS output\n"));
	fprintf(stderr, _("        'alsa' use ALSA output\n"));
	fprintf(stderr, _("        'soundcard': use either OSS or ALSA\n"));
	fprintf(stderr, _("        default sound system: 'oss'\n\n"));
	fprintf(stderr, _("  -d, --device=DEVICE\n"));
	fprintf(stderr, _("        use DEVICE as output device instead of default one;\n"));
	fprintf(stderr, _("        optional for {console|alsa|oss};\n"));
	fprintf(stderr, _("        default devices are:\n"));
	fprintf(stderr, _("        'console': %s\n"), CW_DEFAULT_CONSOLE_DEVICE);
	fprintf(stderr, _("        'oss': %s\n"), CW_DEFAULT_OSS_DEVICE);
	fprintf(stderr, _("        'alsa': %s\n\n"), CW_DEFAULT_ALSA_DEVICE);

	fprintf(stderr, _("Sending options:\n"));

	fprintf(stderr, _("  -w, --wpm=WPM          set initial words per minute\n"));
	fprintf(stderr, _("                         valid values: %d - %d\n"), CW_SPEED_MIN, CW_SPEED_MAX);
	fprintf(stderr, _("                         default value: %d\n"), CW_SPEED_INITIAL);
	fprintf(stderr, _("  -t, --tone=HZ          set initial tone to HZ\n"));
	fprintf(stderr, _("                         valid values: %d - %d\n"), CW_FREQUENCY_MIN, CW_FREQUENCY_MAX);
	fprintf(stderr, _("                         default value: %d\n"), CW_FREQUENCY_INITIAL);
	fprintf(stderr, _("  -v, --volume=PERCENT   set initial volume to PERCENT\n"));
	fprintf(stderr, _("                         valid values: %d - %d\n"), CW_VOLUME_MIN, CW_VOLUME_MAX);
	fprintf(stderr, _("                         default value: %d\n"), CW_VOLUME_INITIAL);

	fprintf(stderr, _("Dot/dash options:\n"));
	fprintf(stderr, _("  -g, --gap=GAP          set extra gap between letters\n"));
	fprintf(stderr, _("                         valid values: %d - %d\n"), CW_GAP_MIN, CW_GAP_MAX);
	fprintf(stderr, _("                         default value: %d\n"), CW_GAP_INITIAL);
	fprintf(stderr, _("  -k, --weighting=WEIGHT set weighting to WEIGHT\n"));
	fprintf(stderr, _("                         valid values: %d - %d\n"), CW_WEIGHTING_MIN, CW_WEIGHTING_MAX);
	fprintf(stderr, _("                         default value: %d\n"), CW_WEIGHTING_INITIAL);

	fprintf(stderr, _("Other options:\n"));
	if (config->is_cw) {
		fprintf(stderr, _("  -e, --noecho           disable sending echo to stdout\n"));
		fprintf(stderr, _("  -m, --nomessages       disable writing messages to stderr\n"));
		fprintf(stderr, _("  -c, --nocommands       disable executing embedded commands\n"));
		fprintf(stderr, _("  -o, --nocombinations   disallow [...] combinations\n"));
		fprintf(stderr, _("  -p, --nocomments       disallow {...} comments\n"));
	}
	if (config->has_practice_time) {
		fprintf(stderr, _("  -T, --time=TIME    set initial practice time\n"));
		fprintf(stderr, _("                     default value: %d\n"), CW_PRACTICE_TIME_INITIAL);
	}
	fprintf(stderr, _("  -f, --infile=FILE      read practice words from FILE\n"));
	if (config->has_outfile) {
		fprintf(stderr, _("  -F, --outfile=FILE        write current practice words to FILE\n"));
	}
	if (config->is_cw) {
		fprintf(stderr, _("                         default file: stdin\n"));
	}
	fprintf(stderr, "\n");
	fprintf(stderr, _("  -h, --help             print this message\n"));
	fprintf(stderr, _("  -V, --version          print version information\n\n"));

	return;
}





int cw_process_argv(int argc, char *const argv[], const char *options, cw_config_t *config)
{
	const char *argv0 = program_basename(argv[0]);

	int option;
	char *argument;

	while (get_option(argc, argv, options, &option, &argument)) {
		if (!cw_process_option(option, argument, config, argv0)) {
			return CW_FAILURE;
		}
	}

	if (get_optind() != argc) {
		fprintf(stderr, "cwlib: expected argument after options\n");
		cw_print_usage(argv0);
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





int cw_process_option(int opt, const char *optarg, cw_config_t *config, const char *argv0)
{
	switch (opt) {
	case 's':
		if (!strcmp(optarg, "alsa")
		    || !strcmp(optarg, "a")) {

			config->audio_system = CW_AUDIO_ALSA;
		} else if (!strcmp(optarg, "oss")
			   || !strcmp(optarg, "o")) {

			config->audio_system = CW_AUDIO_OSS;
		} else if (!strcmp(optarg, "console")
			   || !strcmp(optarg, "c")) {

			config->audio_system = CW_AUDIO_CONSOLE;

		} else if (!strcmp(optarg, "soundcard")
			   || !strcmp(optarg, "s")) {

			config->audio_system = CW_AUDIO_SOUNDCARD;
		} else {
			fprintf(stderr, "cwlib: invalid audio system (option 's'): %s\n", optarg);
			return CW_FAILURE;
		}
		break;

	case 'd':
		fprintf(stderr, "cwlib: d:%s\n", optarg);
		if (optarg && strlen(optarg)) {
			config->audio_device = strdup(optarg);
		} else {
			fprintf(stderr, "cwlib: no device specified for option -d\n");
			return CW_FAILURE;
		}
		break;

	case 'w':
		{
			fprintf(stderr, "cwlib: w:%s\n", optarg);
			int speed = atoi(optarg);
			if (speed < CW_SPEED_MIN || speed > CW_SPEED_MAX) {
				fprintf(stderr, "cwlib: speed out of range: %d\n", speed);
				return CW_FAILURE;
			} else {
				config->send_speed = speed;
			}
			break;
		}

	case 't':
		{
			fprintf(stderr, "cwlib: t:%s\n", optarg);
			int frequency = atoi(optarg);
			if (frequency < CW_FREQUENCY_MIN || frequency > CW_FREQUENCY_MAX) {
				fprintf(stderr, "cwlib: frequency out of range: %d\n", frequency);
				return CW_FAILURE;
			} else {
				config->frequency = frequency;
			}
			break;
		}

	case 'v':
		{
			fprintf(stderr, "cwlib: v:%s\n", optarg);
			int volume = atoi(optarg);
			if (volume < CW_FREQUENCY_MIN || volume > CW_FREQUENCY_MAX) {
				fprintf(stderr, "cwlib: volume level out of range: %d\n", volume);
				return CW_FAILURE;
			} else {
				config->volume = volume;
			}
			break;
		}

	case 'g':
		{
			fprintf(stderr, "cwlib: g:%s\n", optarg);
			int gap = atoi(optarg);
			if (gap < CW_GAP_MIN || gap > CW_GAP_MAX) {
				fprintf(stderr, "cwlib: gap out of range: %d\n", gap);
				return CW_FAILURE;
			} else {
				config->gap = gap;
			}
			break;
		}

	case 'k':
		{
			fprintf(stderr, "cwlib: k:%s\n", optarg);
			int weighting = atoi(optarg);
			if (weighting < CW_WEIGHTING_MIN || weighting > CW_WEIGHTING_MAX) {
				fprintf(stderr, "cwlib: weighting out of range: %d\n", weighting);
				return CW_FAILURE;
			} else {
				config->weighting = weighting;
			}
			break;
		}

	case 'T':
		{
			fprintf(stderr, "cwlib: T:%s\n", optarg);
			int time = atoi(optarg);
			if (time < 0) {
				fprintf(stderr, "cwlib: practice time is negative\n");
				return CW_FAILURE;
			} else {
				config->practice_time = time;
			}
			break;
		}

	case 'f':
		if (optarg && strlen(optarg)) {
			config->input_file = strdup(optarg);
		} else {
			fprintf(stderr, "cwlib: no input file specified for option -f\n");
			return CW_FAILURE;
		}
		/* TODO: access() */
		break;

	case 'F':
		if (optarg && strlen(optarg)) {
			config->output_file = strdup(optarg);
		} else {
			fprintf(stderr, "cwlib: no output file specified for option -F\n");
			return CW_FAILURE;
		}
		/* TODO: access() */
		break;

        case 'e':
		config->do_echo = FALSE;
		break;

        case 'm':
		config->do_errors = FALSE;
		break;

        case 'c':
		config->do_commands = FALSE;
		break;

        case 'o':
		config->do_combinations = FALSE;
		break;

        case 'p':
		config->do_comments = FALSE;
		break;

	case 'h':
		cw_print_help(argv0, config);
		exit(EXIT_SUCCESS);

	case 'V':
		fprintf(stderr, _("%s version %s\n"), argv0, PACKAGE_VERSION);
		fprintf(stderr, "%s\n", CW_COPYRIGHT);
		exit(EXIT_SUCCESS);
	case '?':
	default: /* '?' */
		cw_print_usage(argv0);
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}




void cw_print_usage(const char *argv0)
{
	const char *format = has_longopts()
		? _("Try '%s --help' for more information.\n")
		: _("Try '%s -h' for more information.\n");

	fprintf(stderr, format, argv0);
	return;
}


