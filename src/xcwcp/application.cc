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

//#include <cstdlib>
//#include <ctime>
//#include <cstdio>
//#include <cctype>
#include <cerrno>

// #include <sstream>
// #include <iostream>
// #include <string>

#include <QIconSet>
#include <QToolBar>
#include <QToolButton>
#include <QComboBox>
#include <QSpinBox>
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


#include "start.xpm"
#include "stop.xpm"

#include "application.h"
#include "sender.h"
#include "receiver.h"
#include "display.h"
#include "modeset.h"

#include "cwlib.h"

#include "i18n.h"
// #include "cmdline.h"
#include "copyright.h"
// #include "memory.h"


namespace cw {

//-----------------------------------------------------------------------
//  Module variables, miscellaneous other stuff
//-----------------------------------------------------------------------

// Strings displayed in 'about' dialog.
const QString ABOUT_CAPTION = QString(_("Xcwcp version "))
                                  + PACKAGE_VERSION;

const QString ABOUT_TEXT = QString(_("Xcwcp version "))
                               + PACKAGE_VERSION + "\n" + CW_COPYRIGHT;

// Strings for whats-this dialogs.
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

const QString DISPLAY_WHATSTHIS =
  _("This is the main display for Xcwcp.  The random CW characters that "
  "Xcwcp generates, any keyboard input you type, and the CW that you "
  "key into Xcwcp all appear here.<br><br>"
  "You can clear the display contents from the File menu.<br><br>"
  "The status bar shows the current character being sent, any character "
  "received, and other general error and Xcwcp status information.");


//-----------------------------------------------------------------------
//  Static variables, constructor
//-----------------------------------------------------------------------

// A pointer to the class currently actively using the CW library.  As there
// is only one CW library, we need to make sure that only a single Xcwcp
// instance is using it at any one time.  When NULL, no instance is currently
// using the library.
Application *Application::cwlib_user_application_instance = NULL;


// Application()
//
// Class constructor.  Creates the application main window an GUI frame, and
// registers everything we need to register to get the application up and
// running.
Application::Application()
	: QMainWindow (0),
	  is_using_cwlib_(false), saved_receive_speed_(cw_get_receive_speed())
{
	this->setAttribute(Qt::WA_DeleteOnClose, true);
	this->setWindowTitle(_("Xcwcp"));

	// QMimeSourceFactory::defaultFactory ()->setPixmap (_("start"), start_pixmap);
	// QMimeSourceFactory::defaultFactory ()->setPixmap (_("stop"), stop_pixmap);

	make_toolbar();

	make_file_menu();
	make_settings_menu();
	make_help_menu();

	// the constructor calls setCentralWidget()
	display_ = new Display(this, this->parentWidget());

	QMainWindow::resize(800, 400);

	// Register class handler as the CW library keying event callback. It's
	// important here that we register the static handler, since once we have
	// been into and out of 'C', all concept of 'this' is lost.  It's the job
	// of the static handler to work out which class instance is using the CW
	// library, and call the instance's cwlib_keying_event() function.
	cw_register_keying_callback(cwlib_keying_event_static, NULL);

	// Create a timer for polling send and receive.
	poll_timer_ = new QTimer (this);
	connect(poll_timer_, SIGNAL (timeout()), SLOT (poll_timer_event()));

	// Create a sender and a receiver.
	sender_ = new Sender(display_);
	receiver_ = new Receiver(display_);

	QLabel *sound_system = new QLabel("Output: ");
	statusBar()->addPermanentWidget(sound_system);

}


//-----------------------------------------------------------------------
//  Cwlib keying event callback
//-----------------------------------------------------------------------

// cwlib_keying_event_static()
//
// This is the class-level handler for the keying callback from the CW
// library indicating that the keying state changed.  This function uses the
// cwlib_user_application_instance static variable to determine which class
// instance 'owns' the CW library at the moment (if any), then calls that
// instance's receiver handler function.
//
// This function is called in signal handler context.
void
Application::cwlib_keying_event_static (void *, int key_state)
{
  const Application *application = cwlib_user_application_instance;

  // Notify the receiver of a cwlib keying event only if there is a user
  // instance that is actively using the library and in receive mode.  The
  // receiver handler function cannot determine this for itself.
  if (application && application->is_using_cwlib_
      && application->modeset_.is_receive ())
    {
      application->receiver_->handle_cwlib_keying_event (key_state);
    }
}


//-----------------------------------------------------------------------
//  Qt event and slot handlers
//-----------------------------------------------------------------------

// about()
//
// Pop up a brief dialog about the application.
void Application::about()
{
	QMessageBox::about(0, ABOUT_CAPTION, ABOUT_TEXT);
}


// closeEvent()
//
// Event handler for window close.  Requests a confirmation if we happen to
// be busy sending.
void
Application::closeEvent (QCloseEvent *event)
{
  bool is_closing = true;

  if (is_using_cwlib_)
    {
      is_closing =
          QMessageBox::warning (this, _("Xcwcp"),
                                _("Busy - are you sure?"),
                                _("&Exit"), _("&Cancel"), 0, 0, 1) == 0;
      if (is_closing)
        stop ();
    }

  is_closing ? event->accept () : event->ignore ();
}


// startstop()
//
// Call start or stop depending on the current toggle state of the toolbar
// button that calls this slot.
void Application::startstop(bool checked)
{
	checked ? start() : stop();
}


// start()
//
// Start sending or receiving CW.
void
Application::start ()
{
  if (!is_using_cwlib_)
    {
      // If the CW library is in use by another instance, let the user stop
      // that one and let this one continue.
      if (cwlib_user_application_instance)
        {
          startstop_button_->setDown(false);
          const bool is_stop = QMessageBox::warning (this, _("Xcwcp"),
                                  _("Another Xcwcp window is busy."),
                                  _("&Stop Other"), _("&Cancel"), 0, 0, 1) == 0;
          if (is_stop)
            {
              cwlib_user_application_instance->stop ();
              startstop_button_->setDown(false);
            }
          else
            return;
        }

      is_using_cwlib_ = true;

      // Acquire the CW library sender.
      cwlib_user_application_instance = this;

      // Synchronize the CW sender to our values of speed/tone/gap, and Curtis
      // mode B.  We need to do this here since updates to the GUI widgets are
      // ignored if we aren't in fact active; this permits multiple instances
      // of the class to interoperate with the CW library.  Sort of.  We can
      // do it by just calling the slots for the GUI widgets directly.
      speed_change ();
      frequency_change ();
      volume_change ();
      gap_change ();
      curtis_mode_b_change ();

      cw_flush_tone_queue ();
      cw_queue_tone (20000, 500);
      cw_queue_tone (20000, 1000);
      cw_wait_for_tone_queue ();

      // Call the adaptive receive change callback to synchronize the CW
      // library with this instance's idea of receive tracking and speed.
      adaptive_receive_change ();

      // Clear the sender and receiver.
      sender_->clear ();
      receiver_->clear ();

      startstop_button_->setDown(true);
      display_->clear_status ();

      // Start the poll timer.  At 60WPM, a dot is 20ms, so polling for the
      // maximum library speed needs a 10ms timeout.
      poll_timer_->setSingleShot(false);
      poll_timer_->start(10);
    }
}


// stop()
//
// Empty the buffer of characters awaiting send, and halt the process of
// refilling the buffer.
void
Application::stop ()
{
  if (is_using_cwlib_)
    {
      is_using_cwlib_ = false;

      // Stop the poll timer, and clear the sender and receiver.
      poll_timer_->stop ();
      sender_->clear ();
      receiver_->clear ();

      // Save the receive speed, for restore on next start.
      saved_receive_speed_ = cw_get_receive_speed ();

      cw_flush_tone_queue ();
      cw_queue_tone (20000, 500);
      cw_queue_tone (20000, 1000);
      cw_queue_tone (20000, 500);
      cw_queue_tone (20000, 1000);
      cw_wait_for_tone_queue ();

      // Done with the CW library sender for now.
      cwlib_user_application_instance = NULL;
      //stop_->setEnabled(false);

      startstop_button_->setDown(false);
      display_->show_status (_("Ready"));
    }
}


// new_instance()
//
// Creates a new instance of the Xcwcp application.
void
Application::new_instance ()
{
  Application *application = new Application ();
  //application->setCaption (_("Xcwcp"));
  application->show ();
}


// clear()
//
// Clears the display window of this Xcwcp instance.
void
Application::clear ()
{
  display_->clear ();
}


// sync_speed()
//
// Forces the tracked receive speed into synchronization with the speed
// spin box if adaptive receive is activated.
void
Application::sync_speed ()
{
  if (is_using_cwlib_)
    {
      if (adaptive_receive_->isChecked ())
        {
          // Force by unsetting adaptive receive, setting the receive speed,
          // then resetting adaptive receive again.
          cw_disable_adaptive_receive ();
          if (!cw_set_receive_speed (speed_spin_->value ()))
            {
              perror ("cw_set_receive_speed");
              abort ();
            }
          cw_enable_adaptive_receive ();
        }
    }
}


// speed_change()
// frequency_change()
// volume_change()
// gap_change()
//
// Handle changes in the spin boxes for these CW parameters.  The only action
// necessary is to write the new values out to the CW library.  The one thing
// we do do is to only change parameters when we are active (i.e. have
// control of the CW library).
void
Application::speed_change ()
{
  if (is_using_cwlib_)
    {
      if (!cw_set_send_speed (speed_spin_->value ()))
        {
          perror ("cw_set_send_speed");
          abort ();
        }
      if (!cw_get_adaptive_receive_state ())
        {
          if (!cw_set_receive_speed (speed_spin_->value ()))
            {
              perror ("cw_set_receive_speed");
              abort ();
            }
        }
    }
}

void
Application::frequency_change ()
{
  if (is_using_cwlib_)
    {
      if (!cw_set_frequency (frequency_spin_->value ()))
        {
          perror ("cw_set_frequency");
          abort ();
        }
    }
}

void
Application::volume_change ()
{
  if (is_using_cwlib_)
    {
      if (!cw_set_volume (volume_spin_->value ()))
        {
          perror ("cw_set_volume");
          abort ();
        }
    }
}

void
Application::gap_change ()
{
  if (is_using_cwlib_)
    {
      if (!cw_set_gap (gap_spin_->value ()))
        {
          perror ("cw_set_gap");
          abort ();
        }
    }
}


// mode_change()
//
// Handle a change of mode.  Synchronize mode and receive speed if moving to
// a receive mode, then clear the sender and receiver and any pending tones.
void
Application::mode_change ()
{
  // Get the mode to which mode we're changing.
  const Mode *new_mode = modeset_.get (mode_combo_->currentIndex());

  // If this changes mode type, set the speed synchronization menu item state
  // to enabled for receive mode, disabled otherwise.  And for tidiness, clear
  // the display.
  if (!new_mode->is_same_type_as (modeset_.get_current ()))
    {
	    sync_speed_->setEnabled(new_mode->is_receive());
      display_->clear ();
    }

  // If the mode changed while we're busy, clear the sender and receiver.
  if (is_using_cwlib_)
    {
      sender_->clear ();
      receiver_->clear ();
    }

  // Keep the ModeSet synchronized to mode_combo changes.
  modeset_.set_current(mode_combo_->currentIndex());
}


// curtis_mode_b_change()
//
// Called whenever the user request a change of Curtis iambic mode.  The
// function simply passes the Curtis mode on to the CW library if active,
// and ignores the call if not.
void
Application::curtis_mode_b_change ()
{
  if (is_using_cwlib_)
    {
      curtis_mode_b_->isChecked () ? cw_enable_iambic_curtis_mode_b ()
                                   : cw_disable_iambic_curtis_mode_b ();
    }
}


// adaptive_receive_change()
//
// Called whenever the user request a change of adaptive receive status.  The
// function passes the new receive speed tracking mode on to the CW library if
// active, and if fixed speed receive is set, also sets the hard receive speed
// to equal the send speed, otherwise, it restores the previous tracked receive
// speed.
void
Application::adaptive_receive_change ()
{
  if (is_using_cwlib_)
    {
      if (adaptive_receive_->isChecked ())
        {
          // If going to adaptive receive, first set the speed to the saved
          // receive speed, then turn on adaptive receiving.
          cw_disable_adaptive_receive ();
          if (!cw_set_receive_speed (saved_receive_speed_))
            {
              perror ("cw_set_receive_speed");
              abort ();
            }
          cw_enable_adaptive_receive ();
        }
      else
        {
          // If going to fixed receive, save the current adaptive receive
          // speed so we can restore it later, then turn off adaptive receive,
          // and set the speed to equal the send speed as shown on the speed
          // spin box.
          saved_receive_speed_ = cw_get_receive_speed ();
          cw_disable_adaptive_receive ();
          if (!cw_set_receive_speed (speed_spin_->value ()))
            {
              perror ("cw_set_receive_speed");
              abort ();
            }
        }
    }
}


// fonts()
//
// Use a font dialog to allow selection of display font.
void Application::fonts()
{
	bool status;

	QFont font = QFontDialog::getFont(&status, this);
	if (status) {
		QWidget *display_widget = display_->get_widget();
		display_widget->setFont(font);
	}
}


// colors()
//
// Use a color dialog to allow selection of display color.
void Application::colors()
{
	QColor color = QColorDialog::getColor();
	if (color.isValid()) {
		QWidget *display_widget = display_->get_widget();

		QPalette palette;
		palette.setColor(QPalette::Text, color);
		// display_widget->setAutoFillForeground(true);

		display_widget->setPalette(palette);
	}
}


//-----------------------------------------------------------------------
//  Timer, keyboard and mouse events
//-----------------------------------------------------------------------

// poll_timer_event()
//
// Handle a timer event from the QTimer we set up on initialization.  This
// timer is used for regular polling for sender tone queue low and completed
// receive characters.
void
Application::poll_timer_event ()
{
  if (is_using_cwlib_)
    {
      sender_->poll (modeset_.get_current ());
      receiver_->poll (modeset_.get_current ());
    }
}


// key_event()
//
// Handle a key press event from the display widget.
void
Application::key_event (QKeyEvent *event)
{
  event->ignore ();

  // Special case Alt-M as a way to acquire focus in the mode combo widget.
  // This was a workround applied to earlier releases, no longer required
  // now that events are propagated correctly to the parent.
  //if (event->state () & AltButton && event->key () == Qt::Key_M)
  //  {
  //    mode_combo_->setFocus ();
  //    event->accept ();
  //    return;
  //  }

  // Pass the key event to the sender and the receiver.
  if (is_using_cwlib_)
    {
      sender_->handle_key_event (event, modeset_.get_current ());
      receiver_->handle_key_event (event, modeset_.get_current (),
                                   reverse_paddles_->isChecked ());

    }
}


// mouse_event()
//
// Handle a mouse event from the display widget.
void
Application::mouse_event (QMouseEvent *event)
{
  event->ignore ();

  // Pass the mouse event to the receiver.  The sender isn't interested.
  if (is_using_cwlib_)
    {
      receiver_->handle_mouse_event (event, modeset_.get_current (),
                                     reverse_paddles_->isChecked ());
    }
}





void Application::toggle_toolbar(void)
{

	if (toolbar->isVisible()) {
		toolbar->hide();
		toolbar_visibility_->setText("Show Toolbar");
	} else {
		toolbar->show();
		toolbar_visibility_->setText("Hide Toolbar");
	}
}





void Application::make_toolbar(void)
{
	QPixmap start_pixmap = QPixmap(start_xpm);
	QPixmap stop_pixmap = QPixmap(stop_xpm);

	QIcon start_stop_icon_set;
	start_stop_icon_set.addPixmap(start_pixmap, QIcon::Normal, QIcon::Off);
	start_stop_icon_set.addPixmap(stop_pixmap, QIcon::Normal, QIcon::On);


	toolbar = QMainWindow::addToolBar(_("Xcwcp Operations"));

	startstop_button_ = new QToolButton(toolbar);
	startstop_button_->setIcon(start_stop_icon_set);
	startstop_button_->setText(_("Start/Stop"));
	startstop_button_->setToolTip(_("Start/stop"));
	startstop_button_->setCheckable(true);
	startstop_button_->setWhatsThis(STARTSTOP_WHATSTHIS);
	// Give the button two pixmaps, one for start, one for stop.
	startstop_button_->setIcon(start_stop_icon_set);
	connect(startstop_button_, SIGNAL (toggled(bool)), this, SLOT (startstop(bool)));
	toolbar->addWidget(startstop_button_);


	toolbar->addSeparator();


	make_mode_combo();
	toolbar->addWidget(mode_combo_);


	toolbar->addSeparator ();


	QLabel *speed_label_ = new QLabel(_("Speed:"), 0, 0);
	toolbar->addWidget(speed_label_);
	speed_spin_ = new QSpinBox();
	speed_spin_->setMinimum(CW_SPEED_MIN);
	speed_spin_->setMaximum(CW_SPEED_MAX);
	speed_spin_->setSingleStep(CW_SPEED_STEP);
	speed_spin_->setToolTip(_("Speed"));
	speed_spin_->setWhatsThis(SPEED_WHATSTHIS);
	speed_spin_->setSuffix(_(" WPM"));
	speed_spin_->setValue(cw_get_send_speed());
	connect(speed_spin_, SIGNAL (valueChanged(int)), SLOT (speed_change()));
	toolbar->addWidget(speed_spin_);


	toolbar->addSeparator();


	QLabel *tone_label = new QLabel(_("Tone:"));
	toolbar->addWidget(tone_label);
	frequency_spin_ = new QSpinBox(0);
	frequency_spin_->setMinimum(CW_FREQUENCY_MIN);
	frequency_spin_->setMaximum(CW_FREQUENCY_MAX);
	frequency_spin_->setSingleStep(CW_FREQUENCY_STEP);
	frequency_spin_->setToolTip(_("Frequency"));
	frequency_spin_->setSuffix(_(" Hz"));
	frequency_spin_->setWhatsThis(FREQUENCY_WHATSTHIS);
	frequency_spin_->setValue(cw_get_frequency());
	connect(frequency_spin_, SIGNAL (valueChanged(int)), SLOT (frequency_change()));
	toolbar->addWidget(frequency_spin_);


	toolbar->addSeparator ();


	QLabel *volume_label = new QLabel(_("Volume:"), 0, 0);
	toolbar->addWidget(volume_label);
	volume_spin_ = new QSpinBox(0);
	volume_spin_->setMinimum(CW_VOLUME_MIN);
	volume_spin_->setMaximum(CW_VOLUME_MAX);
	volume_spin_->setSingleStep(CW_VOLUME_STEP);
	volume_spin_->setToolTip(_("Volume"));
	volume_spin_->setSuffix(_(" %"));
	volume_spin_->setWhatsThis(VOLUME_WHATSTHIS);
	volume_spin_->setValue(cw_get_volume());
	connect(volume_spin_, SIGNAL (valueChanged(int)), SLOT (volume_change()));
	toolbar->addWidget(volume_spin_);


	toolbar->addSeparator ();


	QLabel *gap_label = new QLabel(_("Gap:"), 0, 0);
	toolbar->addWidget(gap_label);
	gap_spin_ = new QSpinBox(0);
	toolbar->addWidget(gap_spin_);
	gap_spin_->setMinimum(CW_GAP_MIN);
	gap_spin_->setMaximum(CW_GAP_MAX);
	gap_spin_->setSingleStep(CW_GAP_STEP);
	gap_spin_->setToolTip(_("Gap"));
	gap_spin_->setSuffix(_(" dot(s)"));
	gap_spin_->setWhatsThis(GAP_WHATSTHIS);
	gap_spin_->setValue(cw_get_gap());
	connect(gap_spin_, SIGNAL (valueChanged(int)), SLOT (gap_change()));


	// Finally for the toolbar, add whatsthis.
	//QWhatsThis::whatsThisButton (toolbar);

	// This removes context menu for the toolbar. The menu made it
	// possible to close a toolbar, which complicates 'show/hide'
	// behavior a bit.
	// Disabling the menu makes Settings->Hide toolbar the only place
	// to toggle toolbar visibility. Nice and simple.
	QAction *a = toolbar->toggleViewAction();
	a->setVisible(false);

	return;
}





void Application::make_mode_combo()
{
	mode_combo_ = new QComboBox(0); //, _("Mode"));
	mode_combo_->setToolTip(_("Mode"));
	mode_combo_->setWhatsThis(MODE_WHATSTHIS);
	connect(mode_combo_, SIGNAL (activated(int)), SLOT (mode_change()));

	// Append each mode represented in the modes set to the combo box's
	// contents, then synchronize the current mode.
	for (int index = 0; index < modeset_.get_count(); index++) {
		const QVariant data(index);
		const Mode *mode = modeset_.get(index);
		const QString string = QString::fromUtf8(mode->get_description().c_str());
		mode_combo_->addItem(string, data);
	}
	modeset_.set_current(mode_combo_->currentIndex());

	return;
}





void Application::make_file_menu(void)
{
	file_menu_ = new QMenu (_("&File"), this);
	QMainWindow::menuBar()->addMenu(file_menu_);

	new_window_ = new QAction(_("&New Window"), this);
	new_window_->setShortcut(Qt::CTRL + Qt::Key_N);
	connect(new_window_, SIGNAL (triggered()), SLOT (new_instance()));
	file_menu_->addAction(new_window_);


	file_menu_->addSeparator ();


	clear_display_ = new QAction(_("&Clear Text"), this);
	clear_display_->setShortcut(Qt::CTRL + Qt::Key_C);
	connect(clear_display_, SIGNAL (triggered()), SLOT (clear()));
	file_menu_->addAction(clear_display_);


	sync_speed_ = new QAction(_("Synchronize S&peed"), this);
	sync_speed_->setShortcut(Qt::CTRL + Qt::Key_P);
	sync_speed_->setEnabled(modeset_.is_receive());
	connect(sync_speed_, SIGNAL (triggered()), SLOT (sync_speed()));
	file_menu_->addAction(sync_speed_);


	file_menu_->addSeparator();


	quit_ = new QAction(_("&Quit"), qApp);
	quit_->setShortcut(Qt::CTRL + Qt::Key_Q);
	connect(quit_, SIGNAL (triggered()), SLOT (close()));
	file_menu_->addAction(quit_);

	return;
}





void Application::make_settings_menu(void)
{
	QMenu *settings_ = new QMenu(_("&Settings"), this);
	QMainWindow::menuBar()->addMenu(settings_);


	reverse_paddles_ = new QAction(_("&Reverse Paddles"), this);
	reverse_paddles_->setCheckable(true);
	reverse_paddles_->setChecked(false);
	settings_->addAction(reverse_paddles_);


	curtis_mode_b_ = new QAction(_("&Curtis Mode B Timing"), this);
	curtis_mode_b_->setCheckable(true);
	curtis_mode_b_->setChecked(false);
	connect(curtis_mode_b_, SIGNAL (toggled(bool)), SLOT (curtis_mode_b_change()));
	settings_->addAction(curtis_mode_b_);


	adaptive_receive_ = new QAction(_("&Adaptive CW Receive Speed"), this);
	adaptive_receive_->setCheckable(true);
	adaptive_receive_->setChecked(true);
	connect(adaptive_receive_, SIGNAL (toggled(bool)), SLOT (adaptive_receive_change()));
	settings_->addAction(adaptive_receive_);


	settings_->addSeparator();


	font_settings_ = new QAction(_("&Text font..."), this);
	connect(font_settings_, SIGNAL (triggered(bool)), SLOT (fonts()));
	settings_->addAction(font_settings_);


	color_settings_ = new QAction(_("&Text color..."), this);
	connect(color_settings_, SIGNAL (triggered(bool)), SLOT (colors()));
	settings_->addAction(color_settings_);


	settings_->addSeparator();


	toolbar_visibility_ = new QAction(_("Hide toolbar"), this);
	connect(toolbar_visibility_, SIGNAL (triggered(bool)), SLOT (toggle_toolbar()));
	settings_->addAction(toolbar_visibility_);

	return;
}





void Application::make_help_menu(void)
{
	help_ = new QMenu(_("&Help"), this);
	QMainWindow::menuBar()->addSeparator();
	QMainWindow::menuBar()->addMenu(help_);


	about_ = new QAction(_("&About"), this);
	connect(about_, SIGNAL(triggered(bool)), SLOT(about()));
	help_->addAction(about_);

}


}  // cw namespace
