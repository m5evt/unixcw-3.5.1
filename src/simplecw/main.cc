// Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
// Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)
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
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

#include "config.h"

#include <cstdlib>
#include <cstdio>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>

#include <sstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <new>

#include <QApplication>

#include "application.h"

#include "libcw2.h"
#include "libcw_debug.h"

#include "cmdline.h"
#include "cw_copyright.h"


//extern cw_debug_t cw_debug_object;

static void simplecw_atexit(void);
static void register_signal_handler(void);


cw_config_t *config = NULL; /* program-specific configuration */
std::string all_options =
	"s:|sound,"
	"d:|device,"
	"w:|wpm,"
	"t:|tone,"
	"v:|volume,"
	"g:|gap,"
	"k:|weighting,"
	"R:|receiver,"  /* For development purposes, in experimental branch only. 0 - register callback in key code; 1 - register receiver in key code. */
	// "i:|infile,F:|outfile,"
	// "T:|time,"
	"h|help,"
	"V|version";







/**
   \brief Signal handler, called by the CW library after its own cleanup

   \param signal_number
*/
void signal_handler(int signal_number)
{
	std::clog << "Caught signal " << signal_number
		  << ", exiting..." << std::endl;
	exit(EXIT_SUCCESS);
}




/**
   Parse the command line, initialize a few things, then instantiate the
   Application and wait.
*/
int main(int argc, char **argv)
{
	try {

		//cw_debug_set_flags(&cw_debug_object, CW_DEBUG_KEYING | CW_DEBUG_GENERATOR | CW_DEBUG_TONE_QUEUE | CW_DEBUG_RECEIVE_STATES | CW_DEBUG_KEYER_STATES | CW_DEBUG_INTERNAL| CW_DEBUG_PARAMETERS);
		//cw_debug_object.level = CW_DEBUG_DEBUG;

		atexit(simplecw_atexit);

		register_signal_handler();

		/* Set locale and message catalogs. */
		i18n_initialize();

		/* Parse combined environment and command line arguments. */
		int combined_argc;
		char **combined_argv;


		// Parse combined environment and command line arguments.  Arguments
		// are passed to QApplication() first to allow it to extract any Qt
		// or X11 options.
		combine_arguments("XCWCP_OPTIONS", argc, argv, &combined_argc, &combined_argv);

		QApplication q_application (combined_argc, combined_argv);

		config = cw_config_new(cw_program_basename(argv[0]));
		if (!config) {
			return EXIT_FAILURE;
		}
		config->has_practice_time = 0;
		config->has_infile = false;

		if (!cw_process_argv(argc, argv, all_options.c_str(), config)) {
			fprintf(stderr, "%s: failed to parse command line args\n", config->program_name);
			return EXIT_FAILURE;
		}
		if (!cw_config_is_valid(config)) {
			fprintf(stderr, "%s: inconsistent arguments\n", config->program_name);
			return EXIT_FAILURE;
		}

		// Display the application's windows.
		Application application(config);
		application.setWindowTitle("simplecw");
		application.check_audio_system(config);
		application.show();
		q_application.connect(&q_application, SIGNAL (lastWindowClosed ()),
				      &q_application, SLOT (quit ()));

		// Enter the application event loop.
		return q_application.exec();
	}

	// Handle any exceptions thrown by the above.
	catch (std::bad_alloc) {
		std::clog << "Internal error: heap memory exhausted" << std::endl;
		return EXIT_FAILURE;
	}
	catch (...) {
		std::clog << "Internal error: unknown problem" << std::endl;
		return EXIT_FAILURE;
	}
}





void simplecw_atexit(void)
{
	if (config) {
		cw_config_delete(&config);
	}

	return;
}





void register_signal_handler(void)
{
	/* Set up signal handler to exit on a range of signals. */
	const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };
	for (int i = 0; SIGNALS[i]; i++) {

		struct sigaction action;
		memset(&action, 0, sizeof(action));
		action.sa_handler = signal_handler;
		action.sa_flags = 0;
		int rv = sigaction(SIGNALS[i], &action, (struct sigaction *) NULL);
		if (rv == -1) {
			fprintf(stderr, "can't register signal: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	return;
}
