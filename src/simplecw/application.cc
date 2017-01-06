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

#include <sys/time.h> /* struct timeval */

#include <cerrno>
#include <iostream>

#include <QToolBar>
#include <QLabel>
#include <QStatusBar>
#include <QMenu>
#include <QCheckBox>
#include <QMenuBar>
#include <QMessageBox>
#include <QApplication>
#include <QTimer>
#include <QToolTip>
#include <QWhatsThis>
#include <QFontDialog>
#include <QColorDialog>
#include <QColor>
#include <QPalette>
#include <QCloseEvent>
#include <QPixmap>
#include <QDebug>
#include <QHideEvent>


#include "application.h"
#include "sender.h"
#include "receiver.h"
#include "textarea.h"
#include "cw_common.h"

#include "libcw2.h"

#include "i18n.h"
#include "cw_copyright.h"





namespace cw {





/*
  Module variables, miscellaneous other stuff
*/


const QString ABOUT_CAPTION = QString("simplecw version ") + PACKAGE_VERSION;

const QString ABOUT_TEXT = QString("simplecw version ") + PACKAGE_VERSION + "\n" + CW_COPYRIGHT;


/**
   Create the application main window an GUI frame, and register
   everything we need to register to get the application up and
   running.
*/
Application::Application(cw_config_t *config) :
	QMainWindow (0)
{
	this->config = config;
	this->is_running = false;

	textarea = new TextArea(this, this->parentWidget());
	setCentralWidget(this->textarea);


	make_sender_receiver();


	QMainWindow::setAttribute(Qt::WA_DeleteOnClose, true);
	QMainWindow::setWindowTitle("simplecw");
	QMainWindow::resize(800, 400);

	make_toolbar();
	make_program_menu();
	make_help_menu();
	make_status_bar();

	this->show_status("Ready");

	return;
}





/**
   \brief Keying callback to handle key events reported by libcw

   This is the class-level keying callback that is called by libcw's
   key module every time a state of libcw's key changes.

   Third argument of the callback is used to determine which
   Application instance (class object) should pass the callback to its
   receiver.

   This function is called in signal handler context.

   This callback and \p arg have been registered as callback and
   callback's argument using libcw key's
   cw_key_register_keying_callback function.

   \p arg is casted to 'Application *' in the function.

   \param timestamp - timestamp of key event
   \param key_state - state of libcw's key after the event (current state of key)
   \param arg - instance of Application class that should handle this callback
*/
void Application::libcw_keying_event_static(struct timeval *timestamp, int key_state, void *arg)
{
	const Application *app = (Application *) arg;

	/* Notify the receiver about a libcw keying event only if
	   there is an instance that is actively using the library
	   and the instance is in receive mode.  The receiver handler
	   function cannot determine this for itself. */
	if (app
	    && app->is_running
	    && app->current_mode == MODE_RECEIVE) { /* Receive events from key. */

		//fprintf(stderr, "calling callback, stage 1 (key = %d)\n", key_state);
		app->receiver->handle_libcw_keying_event(timestamp, key_state);
	}

	return;
}





/**
   \brief Pop up a brief dialog about the application.
*/
void Application::about()
{
	QMessageBox::about(0, ABOUT_CAPTION, ABOUT_TEXT);

	return;
}





/**
   \brief Event handler for window close

   Request a confirmation if we happen to be busy sending.
*/
void Application::closeEvent(QCloseEvent *event)
{
	bool is_closing = true;

	if (this->is_running) {
		is_closing = QMessageBox::warning(this, "simplecw", "Busy - are you sure?", "&Exit", "&Cancel", 0, 0, 1) == 0;
		if (is_closing) {
			stop();
		}
	}

	is_closing ? event->accept() : event->ignore();

	return;
}





/**
   Call start or stop depending on the current toggle state of the
   toolbar button that calls this slot.
*/
void Application::startstop()
{
	this->is_running ? stop() : start();

	return;
}





/**
   \brief Start sending or receiving CW
*/
void Application::start()
{
	if (this->is_running) {
		/* Already in action, nothing to do. */
		return;
	}


	/* Synchronize the libcw's sender with our values of
	   speed/tone/gap, and Curtis mode B.  We need to do this here
	   since updates to the GUI widgets are ignored if we aren't
	   in fact active; this permits multiple instances of the
	   class to interoperate with the CW library.  Sort of.  We
	   can do it by just calling the slots for the GUI widgets
	   directly. */
	change_speed();
	/* Call the adaptive receive change callback to synchronize
	   the CW library with this instance's idea of receive
	   tracking and speed. */
	change_adaptive_receive();


	sender->clear();
	receiver->clear();


	startstop_action->setIcon(QIcon::fromTheme("media-playback-stop"));
	startstop_action->setText("Stop");

	this->is_running = true;


	clear_status();

	/* At 60WPM, a dot is 20ms, so polling for the maximum speed
	   library needs a 10ms timeout. */
	poll_timer->setSingleShot(false);
	poll_timer->start(10);

	return;
}






/**
   Empty the buffer of characters awaiting send, and halt the process
   of refilling the buffer.
*/
void Application::stop()
{
	if (!this->is_running) {
		/* Not in action at the moment, nothing to do. */
		return;
	}


	poll_timer->stop();
	sender->clear();
	receiver->clear();

	/* Saving speed for restore on next start. */
	saved_receive_speed = cw_rec_get_speed(receiver->rec);


	startstop_action->setIcon(QIcon::fromTheme("media-playback-start"));
	startstop_action->setText("Start");

	this->is_running = false;


	show_status("Ready");

	return;
}




/**
   \brief Clear the text area window of this application instance
*/
void Application::clear()
{
	textarea->clear();

	return;
}





/**
   Forces the tracked receive speed into synchronization with the
   speed spin box if adaptive receive is activated.
*/
void Application::sync_speed()
{
	if (this->is_running) {
		if (adaptive_receive_action->isChecked()) {
			/* Force by unsetting adaptive receive,
			   setting the receive speed, then resetting
			   adaptive receive again. */
			cw_rec_disable_adaptive_mode(receiver->rec);
			if (!cw_rec_set_speed(receiver->rec, speed_spin->value())) {
				perror("cw_rec_set_speed");
				abort();
			}
			cw_rec_enable_adaptive_mode(receiver->rec);
		}
	}

	return;
}





/**
   \brief Handle change of speed in spin box in main window

   The only action necessary is to write the new value out to the CW
   library. We do this only when we are active, i.e. when we are using
   libcw.
*/
void Application::change_speed()
{
	if (!cw_gen_set_speed(sender->gen, speed_spin->value())) {
		perror("cw_gen_set_speed");
		abort();
	}
	if (!cw_rec_get_adaptive_mode(receiver->rec)) {
		if (!cw_rec_set_speed(receiver->rec, speed_spin->value())) {
			perror("cw_rec_set_speed");
			abort();
		}
	}

	return;
}





/**
   Handle a change of mode.  Synchronize mode and receive speed if
   moving to a receive mode, then clear the sender and receiver and
   any pending tones.
*/
void Application::change_mode()
{
	/* Get the mode to which mode we're changing. */
	int new_mode = mode_combo->currentIndex();

	/* If this changes mode type, set the speed synchronization
	   menu item state to enabled for receive mode, disabled
	   otherwise.  And for tidiness, clear the display. */
	if (new_mode != this->current_mode) {
		sync_speed_action->setEnabled(new_mode == MODE_RECEIVE); /* Receive events from key. */
		textarea->clear();
	}

	//if (this->is_running) {
	if (true) {
		sender->clear();
		receiver->clear();
	}

	/* Keep the mode synchronized to mode_combo changes. */
	this->current_mode = mode_combo->currentIndex();

	return;
}




/**
   Called whenever the user requests a change of adaptive receive
   status.  The function passes the new receive speed tracking mode on
   to the CW library if active, and if fixed speed receive is set,
   also sets the hard receive speed to equal the send speed,
   otherwise, it restores the previous tracked receive speed.
*/
void Application::change_adaptive_receive()
{
	if (adaptive_receive_action->isChecked()) {
		/* Going to adaptive receive. */
		cw_rec_disable_adaptive_mode(receiver->rec);
		if (!cw_rec_set_speed(receiver->rec, saved_receive_speed)) {
			perror("cw_rec_set_speed");
			abort();
		}
		cw_rec_enable_adaptive_mode(receiver->rec);
	} else {
		/* Going to fixed receive. Save the current
		   adaptive receive speed so we can restore it
		   later */
		saved_receive_speed = cw_rec_get_speed(receiver->rec);
		cw_rec_disable_adaptive_mode(receiver->rec);
		if (!cw_rec_set_speed(receiver->rec, speed_spin->value())) {
			perror("cw_rec_set_speed");
			abort();
		}
	}

	return;
}




/**
   Handle a timer event from the QTimer we set up on initialization.
   This timer is used for regular polling for sender tone queue low
   and completed receive characters.
*/
void Application::poll_timer_event()
{
	if (this->is_running) {
		if (this->current_mode == MODE_SEND) {
			sender->poll();
		} else if (this->current_mode == MODE_RECEIVE) {
			receiver->poll();
		} else {
			;
		}
	}

	return;
}





/**
   \brief Handle key event from a keyboard

   Handle keys pressed in main area of application.  Depending on
   application mode (keyboard mode / receiver mode) the keys are
   dispatched to appropriate handler.

   \param event - keyboard event
*/
void Application::key_event(QKeyEvent *event)
{
	// event->ignore();

	if (this->is_running) {
		if (this->current_mode == MODE_SEND) { /* Send / play. */
			sender->handle_key_event(event);
		} else if (this->current_mode == MODE_RECEIVE) { /* Receive. */
			receiver->handle_key_event(event);
		} else {
			;
		}
	}

	return;
}





/**
   \brief Handle button event from a mouse

   Handle button presses made in main area of application.  If
   application mode is receiver mode, the events will be passed to
   handle by receiver.

   \param event - mouse button event
*/
void Application::mouse_event(QMouseEvent *event)
{
	event->ignore();

	if (this->is_running) {
		/* Pass the mouse event only to the receiver.  The sender
		   isn't interested. */
		if (this->current_mode == MODE_RECEIVE) { /* Receive events from key. */
			receiver->handle_mouse_event(event);
		}
	}

	return;
}





void Application::make_toolbar(void)
{
	toolbar = QMainWindow::addToolBar("simplecw operations");

	startstop_action = new QAction("Start/Stop", this);
	startstop_action->setIcon(QIcon::fromTheme("media-playback-start"));
	startstop_action->setText("Start");
	startstop_action->setCheckable(false);
	connect(startstop_action, SIGNAL (triggered(bool)), this, SLOT (startstop()));


	/* Put a button in the toolbar, not the action.  Button can
	   gain focus through Tab key, whereas action can't. The focus
	   for button is, for some reason, invisible, but it's
	   there. */
	startstop_button = new QToolButton(toolbar);
	startstop_button->setDefaultAction(startstop_action);
	startstop_button->setCheckable(false);
	toolbar->addWidget(startstop_button);


	toolbar->addSeparator();


	make_mode_combo();
	toolbar->addWidget(mode_combo);


	QLabel *speed_label_ = new QLabel("Speed:", 0, 0);
	toolbar->addWidget(speed_label_);

	toolbar->addSeparator();

	speed_spin = new QSpinBox(toolbar);
	speed_spin->setMinimum(CW_SPEED_MIN);
	speed_spin->setMaximum(CW_SPEED_MAX);
	speed_spin->setSingleStep(1);
	speed_spin->setSuffix(" WPM");
	speed_spin->setValue(cw_gen_get_speed(sender->gen));
	connect(speed_spin, SIGNAL (valueChanged(int)), SLOT (change_speed()));
	toolbar->addWidget(speed_spin);


	toolbar->addSeparator();


	/* This removes context menu for the toolbar. The menu made it
	   possible to close a toolbar, which complicates 'show/hide'
	   behavior a bit.

	   Disabling the menu makes Settings->Hide toolbar the only
	   place to toggle toolbar visibility. Nice and simple. */
	QAction *a = toolbar->toggleViewAction();
	a->setVisible(false);

	return;
}





void Application::make_mode_combo()
{
	this->mode_combo = new QComboBox(0);
	connect(this->mode_combo, SIGNAL (activated(int)), SLOT (change_mode()));

	QVariant data;

	data = QVariant(MODE_SEND);
	this->mode_combo->addItem("Enter text from keyboard", data);

	data = QVariant(MODE_RECEIVE);
	this->mode_combo->addItem("Use keyboard keys/mouse buttons as Morse key", data);

	this->mode_combo->setCurrentIndex(MODE_SEND);
	this->current_mode = MODE_SEND;

	return;
}





void Application::make_program_menu(void)
{
	QAction * qa = NULL;

	program_menu = new QMenu("&Program", this);
	QMainWindow::menuBar()->addMenu(program_menu);


	program_menu->addSeparator();


	/* This action is connected in make_toolbar(). */
	program_menu->addAction(startstop_action);


	qa = new QAction("&Clear Text", this);
	qa->setShortcut(Qt::CTRL + Qt::Key_C);
	connect(qa, SIGNAL (triggered()), SLOT (clear()));
	program_menu->addAction(qa);


	sync_speed_action = new QAction("Synchronize S&peed", this);
	sync_speed_action->setShortcut(Qt::CTRL + Qt::Key_P);
	sync_speed_action->setEnabled(this->current_mode == MODE_RECEIVE); /* Receive events from key. */
	connect(sync_speed_action, SIGNAL (triggered()), SLOT (sync_speed()));
	program_menu->addAction(sync_speed_action);


	program_menu->addSeparator();

	adaptive_receive_action = new QAction("&Adaptive CW Receive Speed", this);
	adaptive_receive_action->setCheckable(true);
	adaptive_receive_action->setChecked(true);
	connect(adaptive_receive_action, SIGNAL (toggled(bool)), SLOT (change_adaptive_receive()));
	program_menu->addAction(adaptive_receive_action);


	program_menu->addSeparator();


	qa = new QAction("&Quit", qApp);
	qa->setShortcut(Qt::CTRL + Qt::Key_Q);
	connect(qa, SIGNAL (triggered()), qApp, SLOT (closeAllWindows()));
	program_menu->addAction(qa);

	return;
}




void Application::make_help_menu(void)
{
	help = new QMenu("&Help", this);
	QMainWindow::menuBar()->addSeparator();
	QMainWindow::menuBar()->addMenu(help);


	about_action = new QAction("&About", this);
	connect(about_action, SIGNAL(triggered(bool)), SLOT(about()));
	help->addAction(about_action);

	return;
}





void Application::make_sender_receiver(void)
{
	sender = new Sender(this, textarea, config);
	receiver = new Receiver(this, textarea);

	cw_key_register_generator(receiver->key, sender->gen);

	if (this->config->register_receiver) {
		fprintf(stderr, "---------- cw_key: register receiver\n");
		cw_key_register_receiver(receiver->key, receiver->rec);
	} else {
		/* Register class's static function as key's keying
		   event callback. It's important here that we
		   register the static function, since once we have
		   been into and out of 'C', all concept of 'this' is
		   lost.  It's the job of the static handler to work
		   out which class instance is using the CW library,
		   and call the instance's libcw_keying_event()
		   function.

		   The handler called back by libcw is important
		   because it's used to send to libcw's receiver
		   information about timings of events (key down and
		   key up events).

		   Without the callback the library can play sounds as
		   key or paddles are pressed, but since receiver
		   doesn't receive timing parameters it won't be able
		   to identify entered Morse code. */
		fprintf(stderr, "---------- cw_key: register callback\n");
		cw_key_register_keying_callback(receiver->key, libcw_keying_event_static, (void *) this);
	}

	saved_receive_speed = cw_rec_get_speed(receiver->rec);

	/* Create a timer for polling sender and receiver. */
	poll_timer = new QTimer(this);
	connect(poll_timer, SIGNAL (timeout()), SLOT (poll_timer_event()));

	return;
}





void Application::make_status_bar(void)
{
	QString label("Output: ");
	label += cw_gen_get_audio_system_label(sender->gen);
	QLabel *sound_system = new QLabel(label);
	statusBar()->addPermanentWidget(sound_system);

	return;
}





void Application::check_audio_system(cw_config_t *config)
{
	if (config->audio_system == CW_AUDIO_ALSA
	    && cw_is_pa_possible(NULL)) {

		QMessageBox msgBox;
		QString message1 = "Selected audio system is ALSA, but audio on your system is handled by PulseAudio.";
		QString message2 = "Expect various problems.\n";
		QString message3 = "In this situation it is recommended to run %1 like this:\n" \
			"%2 -s p\n\n";
		msgBox.setText(message1 + " " + message2 + message3.arg(config->program_name).arg(config->program_name));
		msgBox.exec();
	}

	return;
}





/**
   \brief Display the given string on the status line
*/
void Application::show_status(const QString &status)
{
	this->statusBar()->showMessage(status);

	return;
}





/**
   \brief Clear the status line
*/
void Application::clear_status()
{
	this->statusBar()->clearMessage();

	return;
}





}  /* namespace cw */
