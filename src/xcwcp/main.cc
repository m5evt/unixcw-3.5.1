// vi: set ts=2 shiftwidth=2 expandtab:
//
// Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//

#include "../config.h"

#include <cstdlib>
#include <cstdio>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include <sstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <new>

#include <qapplication.h>

#include "application.h"

#include "cwlib.h"

#include "i18n.h"
#include "cmdline.h"
#include "copyright.h"
#include "dictionary.h"


//-----------------------------------------------------------------------
//  Command line mechanics
//-----------------------------------------------------------------------

namespace {

bool is_console = false, is_soundcard = true, is_alsa = false;
std::string console_device, soundcard_device;
std::string argv0;

// print_usage()
//
// Print out a brief message directing the user to the help function.
void
print_usage (const std::string &argv0)
{
  std::clog << _("Try '") << argv0 << " "
            << (has_longopts () ? _("--help") : _("-h"))
            << _("' for more information.") << std::endl;
  exit (EXIT_FAILURE);
}


// print_option_get_indent()
// print_option_lhs()
// print_option_rhs()
// print_option_limits()
//
// Helper functions for printing option help strings.
int
print_option_get_indent ()
{
  return has_longopts () ? 25 : 14;
}

void
print_option_lhs (std::ostream &outs,
                  const std::string &short_option,
                  const std::string &long_option,
                  const std::string &tag = "")
{
  const int indent = print_option_get_indent ();

  outs << std::setw (indent) << std::setiosflags (std::ios::left)
       << std::string ("  -") + short_option
           + (has_longopts () ? ", --" + long_option : "")
           + (tag.empty () ? "" : std::string ("=") + tag);
}

void
print_option_rhs (std::ostream &outs,
                  const std::string &description,
                  const std::string &default_value = "")
{
  outs << description
       << (default_value.empty () ? std::string ("")
           : std::string (_(" [default ")) + default_value + "]") << std::endl;
}

void
print_option_rhs (std::ostream &outs,
                  const std::string &description, int default_value)
{
  outs << description << _(" [default ") << default_value << "]" << std::endl;
}

void
print_option_limits (std::ostream &outs,
                     const std::string &tag, int min_value, int max_value)
{
  const int indent = print_option_get_indent ();

  outs << std::setw (indent) << ""
       << _("valid ") << tag << _(" values are between ")
       << min_value << _(" and ") << max_value << std::endl;
}


// print_help()
//
// Print out a brief page of help information.
void
print_help (const std::string &argv0)
{
  cw_reset_send_receive_parameters ();

  std::ostream &outs = std::cout;
  outs << _("Usage: ") << argv0 << _(" [options...]") << std::endl << std::endl;
  print_option_lhs (outs, _("s"), _("sound"), _("SYSTEM"));
  print_option_rhs (outs, _("generate sound using SYSTEM"), _("'soundcard'"));
  outs << std::setw (print_option_get_indent ()) << ""
            << _("one of 's[oundcard]', 'c[onsole]'") << std::endl;
  print_option_lhs (outs, _("x"), _("sdevice"), _("SDEVICE"));
  print_option_rhs (outs,
                    _("use SDEVICE for soundcard sound"), CW_DEFAULT_OSS_DEVICE);
  print_option_lhs (outs, _("d"), _("cdevice"), _("CDEVICE"));
  print_option_rhs (outs,
                    _("use CDEVICE for console sound"), CW_DEFAULT_CONSOLE_DEVICE);
  print_option_lhs (outs, _("i"), _("inifile"), _("INIFILE"));
  print_option_rhs (outs, _("load practice words from INIFILE"));

  int cwlib_min, cwlib_max;

  print_option_lhs (outs, _("w"), _("wpm"), _("WPM"));
  print_option_rhs (outs,
                    _("set initial words per minute"), cw_get_send_speed ());
  cw_get_speed_limits (&cwlib_min, &cwlib_max);
  print_option_limits (outs, _("WPM"), cwlib_min, cwlib_max);

  print_option_lhs (outs, _("t"), _("hz,--tone"), _("HZ"));
  print_option_rhs (outs, _("set initial tone to HZ"), cw_get_frequency ());
  cw_get_frequency_limits (&cwlib_min, &cwlib_max);
  print_option_limits (outs, _("HZ"), cwlib_min, cwlib_max);

  print_option_lhs (outs, _("v"), _("volume"), _("PERCENT"));
  print_option_rhs (outs, _("set initial volume to PERCENT"), cw_get_volume ());
  cw_get_volume_limits (&cwlib_min, &cwlib_max);
  print_option_limits (outs, _("PERCENT"), cwlib_min, cwlib_max);

  print_option_lhs (outs, _("g"), _("gap"), _("GAP"));
  print_option_rhs (outs, _("set extra gap between letters"), cw_get_gap ());
  cw_get_gap_limits (&cwlib_min, &cwlib_max);
  print_option_limits (outs, _("GAP"), cwlib_min, cwlib_max);

  print_option_lhs (outs, _("h"), _("help"));
  print_option_rhs (outs, _("print this message"));
  print_option_lhs (outs, _("V"), _("version"));
  print_option_rhs (outs, _("output version information and exit"));
  outs << std::endl;

  exit (EXIT_SUCCESS);
}


// parse_command_line()
//
// Parse the command line options for initial values for the various
// global and flag definitions.
void
parse_command_line (int argc, char **argv)
{
  int option;
  char *argument;

  argv0 = program_basename (argv[0]);
  while (get_option (argc, argv,
                     _("s:|sound,d:|cdevice,x:|sdevice,y:|mdevice,i:|inifile,"
                     "t:|tone,t:|hz,v:|volume,w:|wpm,g:|gap,h|help,V|version,"
                     "#:|#"),
                     &option, &argument))
    {
      int intarg = -1;
      const std::string value (argument ? argument : "");
      std::istringstream ins (value);

      switch (option)
        {
        case 's':
          if (value == _("console") || value == _("c"))
            {
              is_console = true;
              is_soundcard = false;
            }
          else if (value == _("soundcard") || value == _("s"))
            {
              is_console = false;
              is_soundcard = true;
            }
#if 0
          else if (value == _("both") || value == _("b"))
            {
              is_console = true;
              is_soundcard = true;
            }
#endif
          else
            {
              std::clog << argv0 << _(": invalid sound source") << std::endl;
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
        case 'i':
          if (!dictionary_load (argument))
            {
              std::clog << argv0
                        << _(": error loading words list") << std::endl;
              exit (EXIT_FAILURE);
            }
          break;

        case '#':
          if (!dictionary_write (argument))
            {
              std::clog << argv0
                        << _(": error writing words list") << std::endl;
              exit (EXIT_FAILURE);
            }
          break;

        case 't':
          ins >> intarg;
          if (!cw_set_frequency (intarg))
            {
              std::clog << argv0 << _(": invalid tone value") << std::endl;
              exit (EXIT_FAILURE);
            }
          break;

        case 'v':
          ins >> intarg;
          if (!cw_set_volume (intarg))
            {
              std::clog << argv0 << _(": invalid volume value") << std::endl;
              exit (EXIT_FAILURE);
            }
          break;

        case 'w':
          ins >> intarg;
          if (!cw_set_send_speed (intarg))
            {
              std::clog << argv0 << _(": invalid wpm value") << std::endl;
              exit (EXIT_FAILURE);
            }
          break;

        case 'g':
          ins >> intarg;
          if (!cw_set_gap (intarg))
            {
              std::clog << argv0 << _(": invalid gap value") << std::endl;
              exit (EXIT_FAILURE);
            }
          break;

        case 'h':
          print_help (argv0);

        case 'V':
          std::cout << argv0 << _(" version ") << PACKAGE_VERSION << ", "
                    << _(CW_COPYRIGHT) << std::endl;
          exit (EXIT_SUCCESS);

        case '?':
          print_usage (argv0);

        default:
          std::clog << argv0 << _(": getopts returned ") << option << std::endl;
          exit (EXIT_FAILURE);
        }

    }
  if (get_optind () != argc)
    print_usage (argv0);

  // Deal with odd argument combinations.
  if (!is_console && !console_device.empty ())
    {
      std::clog << argv0 << _(": no console sound: -d invalid") << std::endl;
      print_usage (argv0);
    }
  if (!is_soundcard && !soundcard_device.empty ())
    {
      std::clog << argv0 << _(": no soundcard sound: -x invalid") << std::endl;
      print_usage (argv0);
    }
#if 0
  if (!is_soundcard && !mixer_device.empty ())
    {
      std::clog << argv0 << _(": no soundcard sound: -y invalid") << std::endl;
      print_usage (argv0);
    }
#endif
  return;
}


// signal_handler()
//
// Signal handler, called by the CW library after its own cleanup.
void
signal_handler (int signal_number)
{
  std::clog << _("Caught signal ") << signal_number
            << _(", exiting...") << std::endl;
  exit (EXIT_SUCCESS);
}


}  // namespace


// main()
//
// Parse the command line, initialize a few things, then instantiate the
// Application and wait.
int
main (int argc, char **argv)
{
  static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };

  try
    {
      struct sigaction action;
      int combined_argc;
      char **combined_argv;

      // Set locale and message catalogs.
      i18n_initialize ();

      // Parse combined environment and command line arguments.  Arguments
      // are passed to QApplication() first to allow it to extract any Qt
      // or X11 options.
      combine_arguments (_("XCWCP_OPTIONS"),
                         argc, argv, &combined_argc, &combined_argv);
      QApplication q_application (combined_argc, combined_argv);
      parse_command_line (combined_argc, combined_argv);

      if (is_soundcard) {
	      int rv = cw_generator_new(CW_AUDIO_OSS, soundcard_device.empty() ? NULL : soundcard_device.c_str());
	      if (rv != 1) {
		      std::clog << argv0
				<< _(": cannot set up soundcard sound") << std::endl;
		      exit(EXIT_FAILURE);
	      }
      } else if (is_console) {
	      int rv = cw_generator_new(CW_AUDIO_CONSOLE, console_device.empty() ? NULL : console_device.c_str());
	      if (rv != 1) {
		      std::clog << argv0
				<< _(": cannot set up console sound") << std::endl;
		      exit(EXIT_FAILURE);
	      }

      } else if (is_alsa) {
	      int rv = cw_generator_new(CW_AUDIO_ALSA, soundcard_device.empty() ? NULL : soundcard_device.c_str());
	      if (rv != 1) {
		      std::clog << argv0
				<< _(": failed to open ALSA output")
				<< std::endl;
		      exit(EXIT_FAILURE);
	      }
      } else {
	      std::clog << argv0
			<< "both console and soundcard outputs disabled"
			<< std::endl;
	      exit(EXIT_FAILURE);
      }

      cw_generator_start();

      /* Set up signal handlers to clean up and exit on a range of signals. */
      action.sa_handler = signal_handler;
      action.sa_flags = 0;
      sigemptyset (&action.sa_mask);
      for (int index = 0; SIGNALS[index] != 0; index++)
        {
          if (!cw_register_signal_handler (SIGNALS[index], signal_handler))
            {
              perror ("cw_register_signal_handler");
              abort ();
            }
        }

      // Display the application's windows.
      cw::Application *application = new cw::Application ();
      application->setCaption (_("Xcwcp"));
      application->show ();
      q_application.connect (&q_application, SIGNAL (lastWindowClosed ()),
                             &q_application, SLOT (quit ()));

      // Enter the application event loop.
      int rv = q_application.exec ();

      cw_generator_stop();
      cw_generator_delete();
      return rv;
    }

  // Handle any exceptions thrown by the above.
  catch (std::bad_alloc)
    {
      std::clog << "Internal error: heap memory exhausted" << std::endl;
      return EXIT_FAILURE;
    }
  catch (...)
    {
      std::clog << "Internal error: unknown problem" << std::endl;
      return EXIT_FAILURE;
    }
}
