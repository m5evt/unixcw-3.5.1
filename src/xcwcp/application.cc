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

#include <sys/time.h> /* gettimeofday, struct timeval */

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


#include "icons/icon_start.xpm"
#include "icons/icon_stop.xpm"
#include "icons/icon_mini_xcwcp.xpm"

#include "application.h"
#include "sender.h"
#include "receiver.h"
#include "textarea.h"
#include "modeset.h"
#include "cw_common.h"

#include "libcw2.h"

#include "i18n.h"
#include "cw_copyright.h"





namespace cw {





/*
  Module variables, miscellaneous other stuff
*/


/* Strings displayed in 'about' dialog. */
const QString ABOUT_CAPTION = QString(_("Xcwcp version "))
                                  + PACKAGE_VERSION;

const QString ABOUT_TEXT = QString(_("Xcwcp version "))
                               + PACKAGE_VERSION + "\n" + CW_COPYRIGHT;

/* Strings for whats-this dialogs. */
const QString STARTSTOP_WHATSTHIS =
  _("When this button shows <img source=\"start\">, click it to begin "
  "sending or receiving.  Only one window may send at a time.<br><br>"
  "When the button shows <img source=\"stop\">, click it to finish "
  "sending or receiving.\n\n");

const QString MODE_WHATSTHIS =
  _("This allows you to change what Xcwcp does.  Most of the available "
  "selections will probably generate random CW characters of one form or "
  "another.<br><br>"
  "The exceptions are Send Keyboard CW, which sends the characters "
  "that you type at the keyboard, and Receive Keyed CW, which will "
  "decode CW that you key in using the mouse or keyboard.<br><br>"
  "To key CW into Xcwcp for receive mode, use either the mouse or the "
  "keyboard.  On the mouse, the left and right buttons form an Iambic "
  "keyer, and the middle mouse button works as a straight key.<br><br>"
  "On the keyboard, use the Left and Right cursor keys for Iambic keyer "
  "control, and the Up or Down cursor keys, or the Space, Enter, or "
  "Return keys, as a straight key.");

const QString SPEED_WHATSTHIS =
  _("This controls the CW sending speed.  If you deselect adaptive "
  "receive speed, it also controls the CW receiving speed.");

const QString FREQUENCY_WHATSTHIS =
  _("This sets the frequency of the CW tone on the system sound card "
  "or console.<br><br>"
  "It affects both sent CW and receive sidetone.");

const QString VOLUME_WHATSTHIS =
  _("This sets the volume of the CW tone on the system sound card.  "
  "It is not possible to control console sound volume, so in this "
  "case, all values other than zero produce tones.<br><br>"
  "The volume control affects both sent CW and receive sidetone.");

const QString GAP_WHATSTHIS =
  _("This sets the \"Farnsworth\" gap used in sending CW.  This gap is an "
  "extra number of dit-length silences between CW characters.");



/* A pointer to the class currently actively using the CW library.  As
   there is only one CW library, we need to make sure that only a
   single Xcwcp instance is using it at any one time.  When NULL, no
   instance is currently using the library. */
Application *Application::libcw_user_application_instance = NULL;





/**
   Create the application main window an GUI frame, and register
   everything we need to register to get the application up and
   running.
*/
Application::Application(cw_config_t *config) :
	QMainWindow (0)
{
	this->config = config;

	make_auxiliaries_begin();

	QMainWindow::setAttribute(Qt::WA_DeleteOnClose, true);
	QMainWindow::setWindowTitle(_("Xcwcp"));
	QMainWindow::setWindowIcon(xcwcp_icon);
	QMainWindow::resize(800, 400);

	make_toolbar();

	make_program_menu();
	make_settings_menu();
	make_help_menu();

	make_central_widget();

	make_auxiliaries_end();

	return;
}




#ifndef WITH_EXPERIMENTAL_RECEIVER
/**
   This is the class-level handler for the keying callback from the CW
   library indicating that the keying state changed.  This function
   uses the libcw_user_application_instance static variable to
   determine which class instance 'owns' the CW library at the moment
   (if any), then calls that instance's receiver handler function.

   This function is called in signal handler context. */
void Application::libcw_keying_event_static(void *arg, int key_state)
{
	const Application *app = libcw_user_application_instance;

	/* Notify the receiver about a libcw keying event only if
	   there is a user instance that is actively using the library
	   and the instance is in receive mode.  The receiver handler
	   function cannot determine this for itself. */
	if (app
	    && app->is_using_libcw
	    && app->modeset.get_current()->is_receive()) {

		//fprintf(stderr, "calling callback, stage 1 (key = %d)\n", key_state);

		struct timeval *t = (struct timeval *) arg;
		app->receiver->handle_libcw_keying_event(t, key_state);
	}

	return;
}
#endif




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

	if (is_using_libcw) {
		is_closing = QMessageBox::warning(this, _("Xcwcp"),
						  _("Busy - are you sure?"),
						  _("&Exit"), _("&Cancel"), 0, 0, 1) == 0;
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
	play ? stop() : start();

	return;
}





/**
   \brief Start sending or receiving CW
*/
void Application::start()
{
	if (is_using_libcw) {
		/* Already in action, nothing to do. */
		return;
	}

	/* If the CW library is in use by another instance, let the
	   user stop that one and let this one continue. */
	if (libcw_user_application_instance) {
		const bool stop_other = QMessageBox::warning(this, _("Xcwcp"),
							     _("Another Xcwcp window is busy."),
							     _("&Stop Other"), _("&Cancel"), 0, 0, 1) == 0;
		if (stop_other) {
			libcw_user_application_instance->stop();
		} else {
			/* Restore button's proper visual appearance
			   after it has been pressed, but user hasn't
			   confirmed starting playing in this instance. */
			// this->startstop_button->setDown(false);
			return;
		}
	}

	is_using_libcw = true;

	/* This instance of xcwcp is the current libcw user. */
	libcw_user_application_instance = this;

	/* Synchronize the libcw's sender with our values of
	   speed/tone/gap, and Curtis mode B.  We need to do this here
	   since updates to the GUI widgets are ignored if we aren't
	   in fact active; this permits multiple instances of the
	   class to interoperate with the CW library.  Sort of.  We
	   can do it by just calling the slots for the GUI widgets
	   directly. */
	change_speed();
	change_frequency();
	change_volume();
	change_gap();
	change_curtis_mode_b();
	/* Call the adaptive receive change callback to synchronize
	   the CW library with this instance's idea of receive
	   tracking and speed. */
	change_adaptive_receive();


	sender->clear();
	receiver->clear();

	/* Accessing proper action through this->startstop should also
	   work. */
	QAction *action = startstop_button->defaultAction();
	action->setChecked(true);
	action->setIcon(stop_icon);
	action->setText(_("Stop"));
	action->setToolTip(_("Stop"));
	//startstop_button->setDown(true);
	play = true;

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
	if (!is_using_libcw) {
		/* Not in action at the moment, nothing to do. */
		return;
	}

	is_using_libcw = false;

	poll_timer->stop();
	sender->clear();
	receiver->clear();

	/* Saving speed for restore on next start. */
	saved_receive_speed = cw_rec_get_speed(receiver->rec);

	/* Done with the CW library sender for now. No xcwcp instance
	   is being a user of libcw. */
	libcw_user_application_instance = NULL;

	/* Accessing proper action through this->startstop_action
	   should also work. */
	QAction *action = startstop_button->defaultAction();
	action->setChecked(false);
	action->setIcon(start_icon);
	action->setText(_("Start"));
	action->setToolTip(_("Start"));
	//startstop_button->setDown(false);
	play = false;

	show_status(_("Ready"));

	return;
}





/**
   \brief Create a new instance of the Xcwcp application
*/
void Application::new_instance()
{
	Application *app = new Application(this->config);
	//app->setCaption(_("Xcwcp"));
	app->show();

	return;
}





/**
   \brief Clear the text area window of this xcwcp instance
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
	if (is_using_libcw) {
		if (adaptive_receive_action->isChecked()) {
			/* Force by unsetting adaptive receive,
			   setting the receive speed, then resetting
			   adaptive receive again. */
			cw_rec_set_adaptive_mode(receiver->rec, false);
			if (!cw_rec_set_speed(receiver->rec, speed_spin->value())) {
				perror("cw_rec_set_speed");
				abort();
			}
			cw_rec_set_adaptive_mode(receiver->rec, true);
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
	if (is_using_libcw) {
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
	}

	return;
}





/**
   \brief Handle change of frequency in spin box in main window

   The only action necessary is to write the new value out to the CW
   library. We do this only when we are active, i.e. when we are using
   libcw.
*/
void Application::change_frequency()
{
	if (is_using_libcw) {
		if (!cw_gen_set_frequency(sender->gen, frequency_spin->value())) {
			perror("cw_gen_set_frequency");
			abort();
		}
	}

	return;
}





/**
   \brief Handle change of volume in spin box in main window

   The only action necessary is to write the new value out to the CW
   library. We do this only when we are active, i.e. when we are using
   libcw.
*/
void Application::change_volume()
{
	if (is_using_libcw) {
		if (!cw_gen_set_volume(sender->gen, volume_spin->value())) {
			perror("cw_gen_set_volume");
			abort();
		}
	}

	return;
}





/**
   \brief Handle change of gap in spin box in main window

   The only action necessary is to write the new value out to the CW
   library. We do this only when we are active, i.e. when we are using
   libcw.
*/
void Application::change_gap()
{
	if (is_using_libcw) {
		if (!cw_gen_set_gap(sender->gen, gap_spin->value())) {
			perror("cw_gen_set_gap");
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
	const Mode *new_mode = modeset.get(mode_combo->currentIndex());

	/* If this changes mode type, set the speed synchronization
	   menu item state to enabled for receive mode, disabled
	   otherwise.  And for tidiness, clear the display. */
	if (!new_mode->is_same_type_as(modeset.get_current())) {
		sync_speed_action->setEnabled(new_mode->is_receive());
		textarea->clear();
	}

	/* If the mode changed while we're busy, clear the sender and
	   receiver. */
	if (is_using_libcw) {
		sender->clear();
		receiver->clear();
	}

	/* Keep the ModeSet synchronized to mode_combo changes. */
	modeset.set_current(mode_combo->currentIndex());

	return;
}





/**
   Called whenever the user requests a change of Curtis iambic mode.
   The function simply passes the Curtis mode on to the CW library if
   active, and ignores the call if not.
*/
void Application::change_curtis_mode_b()
{
	if (is_using_libcw) {
		curtis_mode_b_action->isChecked()
			? cw_key_ik_enable_curtis_mode_b(receiver->key)
			: cw_key_ik_disable_curtis_mode_b(receiver->key);
	}

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
	if (is_using_libcw) {
		if (adaptive_receive_action->isChecked()) {
			/* Going to adaptive receive. */
			cw_rec_set_adaptive_mode(receiver->rec, false);
			if (!cw_rec_set_speed(receiver->rec, saved_receive_speed)) {
				perror("cw_rec_set_speed");
				abort();
			}
			cw_rec_set_adaptive_mode(receiver->rec, true);
		} else {
			/* Going to fixed receive. Save the current
			   adaptive receive speed so we can restore it
			   later */
			saved_receive_speed = cw_rec_get_speed(receiver->rec);
			cw_rec_set_adaptive_mode(receiver->rec, false);
			if (!cw_rec_set_speed(receiver->rec, speed_spin->value())) {
				perror("cw_rec_set_speed");
				abort();
			}
		}
	}

	return;
}





/**
   \brief Use a font dialog to allow selection of text font in text
   area
*/
void Application::fonts()
{
	bool status;

	QFont font = QFontDialog::getFont(&status, this);
	if (status) {
		textarea->setFont(font);
	}

	return;
}





/**
   \brief Use a color dialog to allow selection of text color in text
   area
*/
void Application::colors()
{
	QColor color = QColorDialog::getColor();
	if (color.isValid()) {
		QPalette palette;
		palette.setColor(QPalette::Text, color);

		textarea->setPalette(palette);
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
	if (is_using_libcw) {
		sender->poll(modeset.get_current());
		receiver->poll(modeset.get_current());
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
#if 0
	/* Special case Alt-M as a way to acquire focus in the mode
	   combo widget.  This was a workaround applied to earlier
	   releases, no longer required now that events are propagated
	   correctly to the parent.

	   This section has been disabled long before 2015-08-31. */

	if (event->state() & AltButton && event->key() == Qt::Key_M) {
		mode_combo->setFocus();
		event->accept();
		return;
	}
#endif

	if (is_using_libcw) {
		if (modeset.get_current()->is_keyboard()) {
			fprintf(stderr, "---------- key event: keyboard mode\n");
			sender->handle_key_event(event);
		} else if (modeset.get_current()->is_receive()) {
			fprintf(stderr, "---------- key event: receiver mode mode\n");
			receiver->handle_key_event(event, reverse_paddles_action->isChecked());
		} else {
			;
		}
		return;
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

	/* Pass the mouse event only to the receiver.  The sender
	   isn't interested. */
	if (is_using_libcw) {
		if (modeset.get_current()->is_receive()) {
			fprintf(stderr, "---------- mouse event: receiver mode\n");
			receiver->handle_mouse_event(event, reverse_paddles_action->isChecked());
		}
	}

	return;
}





void Application::toggle_toolbar(void)
{
	if (toolbar->isVisible()) {
		toolbar->hide();
		toolbar_visibility_action->setText("Show Toolbar");
	} else {
		toolbar->show();
		toolbar_visibility_action->setText("Hide Toolbar");
	}

	return;
}





void Application::make_toolbar(void)
{
	toolbar = QMainWindow::addToolBar(_("Xcwcp Operations"));

	startstop_action = new QAction(_("Start/Stop"), this);
	startstop_action->setIcon(start_icon);
	startstop_action->setText(_("Start"));
	startstop_action->setToolTip(_("Start"));
	startstop_action->setWhatsThis(STARTSTOP_WHATSTHIS);
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


	toolbar->addSeparator();


	QLabel *speed_label_ = new QLabel(_("Speed:"), 0, 0);
	toolbar->addWidget(speed_label_);

	speed_spin = new QSpinBox(toolbar);
	speed_spin->setMinimum(CW_SPEED_MIN);
	speed_spin->setMaximum(CW_SPEED_MAX);
	speed_spin->setSingleStep(CW_SPEED_STEP);
	speed_spin->setToolTip(_("Speed"));
	speed_spin->setWhatsThis(SPEED_WHATSTHIS);
	speed_spin->setSuffix(_(" WPM"));
	speed_spin->setValue(cw_gen_get_speed(sender->gen));
	connect(speed_spin, SIGNAL (valueChanged(int)), SLOT (change_speed()));
	toolbar->addWidget(speed_spin);


	toolbar->addSeparator();


	QLabel *tone_label = new QLabel(_("Tone:"));
	toolbar->addWidget(tone_label);

	frequency_spin = new QSpinBox(toolbar);
	frequency_spin->setMinimum(CW_FREQUENCY_MIN);
	frequency_spin->setMaximum(CW_FREQUENCY_MAX);
	frequency_spin->setSingleStep(CW_FREQUENCY_STEP);
	frequency_spin->setToolTip(_("Frequency"));
	frequency_spin->setSuffix(_(" Hz"));
	frequency_spin->setWhatsThis(FREQUENCY_WHATSTHIS);
	frequency_spin->setValue(cw_gen_get_frequency(sender->gen));
	connect(frequency_spin, SIGNAL (valueChanged(int)), SLOT (change_frequency()));
	toolbar->addWidget(frequency_spin);


	toolbar->addSeparator();


	QLabel *volume_label = new QLabel(_("Volume:"), 0, 0);
	toolbar->addWidget(volume_label);

	volume_spin = new QSpinBox(toolbar);
	volume_spin->setMinimum(CW_VOLUME_MIN);
	volume_spin->setMaximum(CW_VOLUME_MAX);
	volume_spin->setSingleStep(CW_VOLUME_STEP);
	volume_spin->setToolTip(_("Volume"));
	volume_spin->setSuffix(_(" %"));
	volume_spin->setWhatsThis(VOLUME_WHATSTHIS);
	volume_spin->setValue(cw_gen_get_volume(sender->gen));
	connect(volume_spin, SIGNAL (valueChanged(int)), SLOT (change_volume()));
	toolbar->addWidget(volume_spin);


	toolbar->addSeparator();


	QLabel *gap_label = new QLabel(_("Gap:"), 0, 0);
	toolbar->addWidget(gap_label);

	gap_spin = new QSpinBox(toolbar);
	gap_spin->setMinimum(CW_GAP_MIN);
	gap_spin->setMaximum(CW_GAP_MAX);
	gap_spin->setSingleStep(CW_GAP_STEP);
	gap_spin->setToolTip(_("Gap"));
	gap_spin->setSuffix(_(" dot(s)"));
	gap_spin->setWhatsThis(GAP_WHATSTHIS);
	gap_spin->setValue(cw_gen_get_gap(sender->gen));
	connect(gap_spin, SIGNAL (valueChanged(int)), SLOT (change_gap()));
	toolbar->addWidget(gap_spin);


	/* Finally for the toolbar, add whatsthis. */
	//QWhatsThis::whatsThisButton(toolbar);

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
	mode_combo = new QComboBox(0); //, _("Mode"));
	mode_combo->setToolTip(_("Mode"));
	mode_combo->setWhatsThis(MODE_WHATSTHIS);
	connect(mode_combo, SIGNAL (activated(int)), SLOT (change_mode()));

	/* Append each mode represented in the modes set to the combo
	   box's contents, then synchronize the current mode. */
	for (int index = 0; index < modeset.get_count(); index++) {
		const QVariant data(index);
		const Mode *mode = modeset.get(index);
		const QString string = QString::fromUtf8(mode->get_description().c_str());
		mode_combo->addItem(string, data);
	}
	modeset.set_current(mode_combo->currentIndex());

	return;
}





void Application::make_program_menu(void)
{
	program_menu = new QMenu(_("&Program"), this);
	QMainWindow::menuBar()->addMenu(program_menu);

	new_window_action = new QAction(_("&New Window"), this);
	new_window_action->setShortcut(Qt::CTRL + Qt::Key_N);
	connect(new_window_action, SIGNAL (triggered()), SLOT (new_instance()));
	program_menu->addAction(new_window_action);


	program_menu->addSeparator();


	/* This action is connected in make_toolbar(). */
	program_menu->addAction(startstop_action);


	clear_display_action = new QAction(_("&Clear Text"), this);
	clear_display_action->setShortcut(Qt::CTRL + Qt::Key_C);
	connect(clear_display_action, SIGNAL (triggered()), SLOT (clear()));
	program_menu->addAction(clear_display_action);


	sync_speed_action = new QAction(_("Synchronize S&peed"), this);
	sync_speed_action->setShortcut(Qt::CTRL + Qt::Key_P);
	sync_speed_action->setEnabled(modeset.get_current()->is_receive());
	connect(sync_speed_action, SIGNAL (triggered()), SLOT (sync_speed()));
	program_menu->addAction(sync_speed_action);


	program_menu->addSeparator();


	close_action = new QAction(_("&Close"), this);
	close_action->setShortcut(Qt::CTRL + Qt::Key_W);
	connect(close_action, SIGNAL (triggered()), SLOT (close()));
	program_menu->addAction(close_action);


	quit_action = new QAction(_("&Quit"), qApp);
	quit_action->setShortcut(Qt::CTRL + Qt::Key_Q);
	connect(quit_action, SIGNAL (triggered()), qApp, SLOT (closeAllWindows()));
	program_menu->addAction(quit_action);

	return;
}





void Application::make_settings_menu(void)
{
	QMenu *settings = new QMenu(_("&Settings"), this);
	QMainWindow::menuBar()->addMenu(settings);


	reverse_paddles_action = new QAction(_("&Reverse Paddles"), this);
	reverse_paddles_action->setCheckable(true);
	reverse_paddles_action->setChecked(false);
	settings->addAction(reverse_paddles_action);


	curtis_mode_b_action = new QAction(_("&Curtis Mode B Timing"), this);
	curtis_mode_b_action->setCheckable(true);
	curtis_mode_b_action->setChecked(false);
	connect(curtis_mode_b_action, SIGNAL (toggled(bool)), SLOT (change_curtis_mode_b()));
	settings->addAction(curtis_mode_b_action);


	adaptive_receive_action = new QAction(_("&Adaptive CW Receive Speed"), this);
	adaptive_receive_action->setCheckable(true);
	adaptive_receive_action->setChecked(true);
	connect(adaptive_receive_action, SIGNAL (toggled(bool)), SLOT (change_adaptive_receive()));
	settings->addAction(adaptive_receive_action);


	settings->addSeparator();


	font_settings_action = new QAction(_("&Text font..."), this);
	connect(font_settings_action, SIGNAL (triggered(bool)), SLOT (fonts()));
	settings->addAction(font_settings_action);


	color_settings_action = new QAction(_("&Text color..."), this);
	connect(color_settings_action, SIGNAL (triggered(bool)), SLOT (colors()));
	settings->addAction(color_settings_action);


	settings->addSeparator();


	toolbar_visibility_action = new QAction(_("Hide toolbar"), this);
	connect(toolbar_visibility_action, SIGNAL (triggered(bool)), SLOT (toggle_toolbar()));
	settings->addAction(toolbar_visibility_action);

	return;
}





void Application::make_help_menu(void)
{
	help = new QMenu(_("&Help"), this);
	QMainWindow::menuBar()->addSeparator();
	QMainWindow::menuBar()->addMenu(help);


	about_action = new QAction(_("&About"), this);
	connect(about_action, SIGNAL(triggered(bool)), SLOT(about()));
	help->addAction(about_action);

	return;
}





void Application::make_central_widget(void)
{
	/* This constructor calls setCentralWidget(). */
	textarea = new TextArea(this, this->parentWidget());

	return;
}





void Application::make_auxiliaries_begin(void)
{
	start_icon = QPixmap(icon_start_xpm);
	stop_icon = QPixmap(icon_stop_xpm);
	xcwcp_icon = QPixmap(icon_mini_xcwcp_xpm);

	is_using_libcw = false;
	play = false;

	sender = new Sender(this, textarea, config);
	receiver = new Receiver(this, textarea);
	cw_key_register_generator(receiver->key, sender->gen);
	saved_receive_speed = cw_rec_get_speed(receiver->rec);

	/* Create a timer for polling send and receive. */
	poll_timer = new QTimer(this);
	connect(poll_timer, SIGNAL (timeout()), SLOT (poll_timer_event()));

	return;
}





void Application::make_auxiliaries_end(void)
{

#ifndef WITH_EXPERIMENTAL_RECEIVER
	/* Register class handler as the CW library keying event
	   callback. It's important here that we register the static
	   handler, since once we have been into and out of 'C', all
	   concept of 'this' is lost.  It's the job of the static
	   handler to work out which class instance is using the CW
	   library, and call the instance's libcw_keying_event()
	   function.

	   The handler called back by libcw is important because it's
	   used to send to libcw information about timings of events
	   (key down and key up events).

	   Without the callback the library can play sounds as key or
	   paddles are pressed, but (since it doesn't receive timing
	   parameters) it won't be able to identify entered Morse
	   code. */

	cw_key_register_keying_callback(receiver->key, libcw_keying_event_static, &(receiver->timer));

	/* The call above registered receiver->timer as a generic
	   argument to a callback. However, libcw needs to know when
	   the argument happens to be of type 'struct timeval'. This
	   is why we have this second call, explicitly passing
	   receiver's timer to libcw. */
	cw_key_ik_register_timer(receiver->key, &(receiver->timer));

	gettimeofday(&(receiver->timer), NULL);
	//fprintf(stderr, "time on aux config: %10ld : %10ld\n", receiver->timer.tv_sec, receiver->timer.tv_usec);
#endif

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
		QString message1 = _("Selected audio system is ALSA, but audio on your system is handled by PulseAudio.");
		QString message2 = _("Expect various problems.\n");
		QString message3 = _("In this situation it is recommended to run %1 like this:\n" \
				     "%2 -s p\n\n");
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
