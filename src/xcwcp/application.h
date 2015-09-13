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

#ifndef H_XCWCP_APPLICATION
#define H_XCWCP_APPLICATION





#include <QMainWindow>
#include <QToolButton>
#include <QComboBox>
#include <QSpinBox>

#include <string>
#include <deque>

#include "modeset.h"
#include "cw_common.h"





namespace cw {





	class Sender;
	class Receiver;
	class TextArea;





	/* Class Application encapsulates the outermost Xcwcp Qt
	   application.  Defines slots and signals, as well as the
	   usual class information. */

	class Application :
		public QMainWindow
	{
		Q_OBJECT
	public:
		Application(cw_config_t *config);

		/* Handle key press and mouse button press events. */
		void key_event(QKeyEvent *event);
		void mouse_event(QMouseEvent *event);
		void check_audio_system(cw_config_t *config);

		void show_status(const QString &status);
		void clear_status();

	protected:
		void closeEvent(QCloseEvent *event);

	private slots:
		/* Qt widget library callback functions. */

		void about();
		void startstop();
		void start();
		void stop();
		void new_instance();
		void clear();
		void fonts();
		void colors();
		void toggle_toolbar();
		void poll_timer_event();

		/* These Qt widget callback functions interact with
		   libcw. */
		void sync_speed();
		void change_speed();
		void change_frequency();
		void change_volume();
		void change_gap();
		void change_mode();
		void change_curtis_mode_b();
		void change_adaptive_receive();


	private:
		/* Class variable to enable sharing of the libcw
		   across instances.  Set to the 'this' of the xcwcp
		   instance that currently uses libcw, or NULL if no
		   current user. */
		static Application *libcw_user_application_instance;

		QPixmap xcwcp_icon;

		bool play;
		QPixmap start_icon;
		QPixmap stop_icon;

		/* GUI elements used throughout the class. */
		QToolBar *toolbar; // main toolbar
		QToolButton *startstop_button;
		QAction *startstop_action; /* Shared between toolbar and Progam menu */
		QComboBox *mode_combo;
		QSpinBox *speed_spin;
		QSpinBox *frequency_spin;
		QSpinBox *volume_spin;
		QSpinBox *gap_spin;


		QMenu *program_menu;
		QAction *new_window_action;
		QAction *clear_display_action;
		QAction *sync_speed_action;
		QAction *close_action;
		QAction *quit_action;

		QMenu *settings;
		QAction *reverse_paddles_action;
		QAction *curtis_mode_b_action;
		QAction *adaptive_receive_action;
		QAction *font_settings_action;
		QAction *color_settings_action;
		QAction *toolbar_visibility_action;

		QMenu *help;
		QAction *about_action;

		//int file_synchronize_speed_id;
		//int file_start_id;
		//int file_stop_id;

		/* Set of modes used by the application; initialized
		   from dictionaries, with keyboard and receive modes
		   added. */
		ModeSet modeset;

		Sender *sender;
		Receiver *receiver;

		cw_config_t *config;

		TextArea *textarea;

		/* Poll timer, used to ensure that all of the
		   application processing can be handled in the
		   foreground, rather than in the signal handling
		   context of a libcw tone queue low callback. */
		QTimer *poll_timer;

		/* Flag indicating if this instance is currently using
		   the libcw. Of course xcwcp is an application that
		   links to libcw, but this flag is for *active* use
		   of libcw, i.e when "play"/"start" button in xcwcp's
		   UI has been pressed. */
		bool is_using_libcw;

		/* Saved receive speed, used to reinstate adaptive
		   tracked speed on start. */
		int saved_receive_speed;

// #ifndef WITH_EXPERIMENTAL_RECEIVER
		/* Keying callback function for libcw.  There is a
		   static version for the whole class, and an instance
		   version for each object.  The class version calls
		   the relevant instance version, based on which
		   instance is the current registered libcw user. */
		static void libcw_keying_event_static(struct timeval *timestamp, int key_state, void *arg);
		void libcw_keying_event(int key_state);
// #endif

		/* Wrappers for creating UI. */
		void make_central_widget(void);
		void make_toolbar(void);
		void make_mode_combo(void);
		void make_program_menu(void);
		void make_settings_menu(void);
		void make_help_menu(void);

		void make_auxiliaries_begin(void);
		void make_auxiliaries_end(void);


		/* Prevent unwanted operations. */
		Application(const Application &);
		Application &operator=(const Application &);
	};





}  /* namespace cw */





#endif  /* #ifndef H_XCWCP_APPLICATION */
