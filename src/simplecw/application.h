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

#include "cw_common.h"





namespace cw {



	enum {
		MODE_SEND = 0, /* Send/generate/play text entered from keyboard. */
		MODE_RECEIVE = 1 /* Receive events from a key. */
	};



	class Sender;
	class Receiver;
	class TextArea;





	/* Class Application encapsulates the outermost Xcwcp Qt
	   application.  Defines slots and signals, as well as the
	   usual class information. */

	class Application : public QMainWindow
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
		void clear();
		void poll_timer_event();

		/* These Qt widget callback functions interact with
		   libcw. */
		void sync_speed();
		void change_speed();
		void change_mode();
		void change_adaptive_receive();


	private:
		bool is_running;

		/* GUI elements used throughout the class. */
		QToolBar *toolbar; // main toolbar
		QToolButton *startstop_button;
		QAction *startstop_action; /* Shared between toolbar and Progam menu */
		QComboBox *mode_combo;
		QSpinBox *speed_spin;

		int current_mode; /* MODE_SEND / MODE_RECEIVE. */

		QMenu *program_menu;
		QAction *clear_display_action;
		QAction *sync_speed_action;
		QAction *close_action;
		QAction *quit_action;

		QMenu *settings;
		QAction *adaptive_receive_action;

		QMenu *help;
		QAction *about_action;

		Sender *sender;
		Receiver *receiver;

		cw_config_t *config;

		TextArea *textarea;

		/* Poll timer, used to ensure that all of the
		   application processing can be handled in the
		   foreground, rather than in the signal handling
		   context of a libcw tone queue low callback. */
		QTimer *poll_timer;

		/* Saved receive speed, used to reinstate adaptive
		   tracked speed on start. */
		int saved_receive_speed;

		/* Keying callback function for libcw. */
		static void libcw_keying_event_static(struct timeval *timestamp, int key_state, void *arg);
		void libcw_keying_event(int key_state);

		/* Wrappers for creating UI. */
		void make_central_widget(void);
		void make_toolbar(void);
		void make_mode_combo(void);
		void make_program_menu(void);
		void make_settings_menu(void);
		void make_help_menu(void);
		void make_status_bar(void);

		void make_sender_receiver(void);



		/* Prevent unwanted operations. */
		Application(const Application &);
		Application &operator=(const Application &);
	};





}  /* namespace cw */





#endif  /* #ifndef H_XCWCP_APPLICATION */
