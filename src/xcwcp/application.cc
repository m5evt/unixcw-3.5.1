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
#include <ctime>
#include <cstdio>
#include <cctype>
#include <cerrno>

#include <qiconset.h>
#include <qtoolbar.h>
#include <qtoolbutton.h>
#include <qcombobox.h>
#include <qspinbox.h>
#include <qlabel.h>
#include <qpopupmenu.h>
#include <qcheckbox.h>
#include <qmenubar.h>
#include <qkeycode.h>
#include <qmessagebox.h>
#include <qapplication.h>
#include <qaccel.h>
#include <qtimer.h>
#include <qtooltip.h>
#include <qwhatsthis.h>
#include <qfontdialog.h>
#include <qcolordialog.h>
#include <qcolor.h>
#include <qpalette.h>

#include <string>

#include "start.xpm"
#include "stop.xpm"

#include "application.h"
#include "sender.h"
#include "receiver.h"
#include "display.h"
#include "modeset.h"

#include "cwlib.h"

#include "i18n.h"
#include "cmdline.h"
#include "copyright.h"
#include "memory.h"


namespace cw {

//-----------------------------------------------------------------------
//  Module variables, miscellaneous other stuff
//-----------------------------------------------------------------------

// Strings displayed in 'about' dialog.
const std::string ABOUT_CAPTION = std::string (_("Xcwcp version "))
                                  + PACKAGE_VERSION;

const std::string ABOUT_TEXT = std::string (_("Xcwcp version "))
                               + PACKAGE_VERSION + "  " + _(CW_COPYRIGHT);

// Strings for whats-this dialogs.
const std::string STARTSTOP_WHATSTHIS =
  _("When this button shows <img source=\"start\">, click it to begin "
  "sending or receiving.  Only one window may send at a time.<br><br>"
  "When the button shows <img source=\"stop\">, click it to finish "
  "sending or receiving.\n\n");

const std::string MODE_WHATSTHIS =
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

const std::string SPEED_WHATSTHIS =
  _("This controls the CW sending speed.  If you deselect adaptive "
  "receive speed, it also controls the CW receiving speed.");

const std::string FREQUENCY_WHATSTHIS =
  _("This sets the frequency of the CW tone on the system sound card "
  "or console.<br><br>"
  "It affects both sent CW and receive sidetone.");

const std::string VOLUME_WHATSTHIS =
  _("This sets the volume of the CW tone on the system sound card.  "
  "It is not possible to control console sound volume, so in this "
  "case, all values other than zero produce tones.<br><br>"
  "The volume control affects both sent CW and receive sidetone.");

const std::string GAP_WHATSTHIS =
  _("This sets the \"Farnsworth\" gap used in sending CW.  This gap is an "
  "extra number of dit-length silences between CW characters.");

const std::string DISPLAY_WHATSTHIS =
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
Application::Application ()
  : QMainWindow (0, _("Xcwcp"), WDestructiveClose),
    is_using_cwlib_ (false), saved_receive_speed_ (cw_get_receive_speed ())
{
  // Create a toolbar, and the start/stop button.
  QToolBar *toolbar = new QToolBar (this, _("Xcwcp Operations"));
  toolbar->setLabel (_("Xcwcp Operations"));

  QPixmap start_pixmap = QPixmap (start_xpm);
  QPixmap stop_pixmap = QPixmap (stop_xpm);

  QIconSet start_stop_icon_set;
  start_stop_icon_set.setPixmap (start_pixmap, QIconSet::Small,
                                 QIconSet::Normal, QIconSet::Off);
  start_stop_icon_set.setPixmap (stop_pixmap, QIconSet::Small,
                                 QIconSet::Normal, QIconSet::On);
  startstop_button_ = new QToolButton (start_stop_icon_set, _("Start/Stop"),
                                       QString::null, this, SLOT (startstop ()),
                                       toolbar, _("Start/Stop"));
  QToolTip::add (startstop_button_, _("Start/stop"));
  startstop_button_->setToggleButton (true);

  // Give the button two pixmaps, one for start, one for stop.
  startstop_button_->setIconSet (start_stop_icon_set);


  // Add the mode selector combo box to the toolbar.
  toolbar->addSeparator ();
  mode_combo_ = new QComboBox (false, toolbar, _("Mode"));
  QToolTip::add (mode_combo_, _("Mode"));
  connect (mode_combo_, SIGNAL (activated (int)), SLOT (mode_change ()));

  // Append each mode represented in the modes set to the combo box's
  // contents, then synchronize the current mode.
  for (int index = 0; index < modeset_.get_count (); index++)
    {
      const Mode *mode = modeset_.get (index);
      mode_combo_->insertItem (mode->get_description ());
    }
  modeset_.set_current (mode_combo_->currentItem ());

  // Add the speed, frequency, volume, and gap spin boxes.  Connect each to a
  // value change function, so that we can immediately pass on changes in
  // these values to the CW library.

  // QSpinBox ( int minValue, int maxValue, int step = 1, QWidget * parent = 0, const char * name = 0 )

  toolbar->addSeparator ();
  new QLabel (_("Speed:"), toolbar, _("Speed Label"));
  speed_spin_ = new QSpinBox (CW_SPEED_MIN, CW_SPEED_MAX, CW_SPEED_STEP, toolbar, _("Speed"));
  QToolTip::add (speed_spin_, _("Speed"));
  speed_spin_->setSuffix (_(" WPM"));
  speed_spin_->setValue (cw_get_send_speed ());
  connect (speed_spin_, SIGNAL (valueChanged (int)), SLOT (speed_change ()));

  toolbar->addSeparator ();
  new QLabel (_("Tone:"), toolbar, _("Frequency Label"));
  frequency_spin_ = new QSpinBox (CW_FREQUENCY_MIN, CW_FREQUENCY_MAX,
                                  CW_FREQUENCY_STEP, toolbar, _("Frequency"));
  QToolTip::add (frequency_spin_, _("Frequency"));
  frequency_spin_->setSuffix (_(" Hz"));
  frequency_spin_->setValue (cw_get_frequency ());
  connect (frequency_spin_,
           SIGNAL (valueChanged (int)), SLOT (frequency_change ()));

  toolbar->addSeparator ();
  new QLabel (_("Volume:"), toolbar, _("Volume Label"));
  volume_spin_ = new QSpinBox (CW_VOLUME_MIN, CW_VOLUME_MAX, CW_VOLUME_STEP, toolbar, _("Volume"));
  QToolTip::add (volume_spin_, _("Volume"));
  volume_spin_->setSuffix (_(" %"));
  volume_spin_->setValue (cw_get_volume ());
  connect (volume_spin_, SIGNAL (valueChanged (int)), SLOT (volume_change ()));

  toolbar->addSeparator ();
  new QLabel (_("Gap:"), toolbar, _("Gap Label"));
  gap_spin_ = new QSpinBox (CW_GAP_MIN, CW_GAP_MAX, CW_GAP_STEP, toolbar, _("Gap"));
  QToolTip::add (gap_spin_, _("Farnsworth gap"));
  gap_spin_->setSuffix (_(" dot(s)"));
  gap_spin_->setValue (cw_get_gap ());
  connect (gap_spin_, SIGNAL (valueChanged (int)), SLOT (gap_change ()));
  toolbar->addSeparator ();

  // Finally for the toolbar, add whatsthis.
  QWhatsThis::whatsThisButton (toolbar);
  QMimeSourceFactory::defaultFactory ()->setPixmap (_("start"), start_pixmap);
  QMimeSourceFactory::defaultFactory ()->setPixmap (_("stop"), stop_pixmap);
  QWhatsThis::add (startstop_button_, STARTSTOP_WHATSTHIS);
  QWhatsThis::add (mode_combo_, MODE_WHATSTHIS);
  QWhatsThis::add (speed_spin_, SPEED_WHATSTHIS);
  QWhatsThis::add (frequency_spin_, FREQUENCY_WHATSTHIS);
  QWhatsThis::add (volume_spin_, VOLUME_WHATSTHIS);
  QWhatsThis::add (gap_spin_, GAP_WHATSTHIS);

  // Create the file popup menu.
  int id;
  file_menu_ = new QPopupMenu (this, _("File"));
  menuBar ()->insertItem (_("&File"), file_menu_);

  file_menu_->insertItem (_("&New Window"),
                          this, SLOT (new_instance ()), CTRL + Key_N);
  file_menu_->insertSeparator ();
  file_menu_->insertItem (_("Clear &Display"),
                          this, SLOT (clear ()), CTRL + Key_C);
  id = file_menu_->insertItem (_("Synchronize S&peed"),
                               this, SLOT (sync_speed ()), CTRL + Key_P);
  file_synchronize_speed_id_ = id;
  file_menu_->insertSeparator ();
  id = file_menu_->insertItem (start_pixmap, _("&Start"),
                               this, SLOT (start ()), CTRL + Key_S);
  file_start_id_ = id;
  id = file_menu_->insertItem (stop_pixmap, _("S&top"),
                               this, SLOT (stop ()), CTRL + Key_T);
  file_stop_id_ = id;
  file_menu_->insertSeparator ();
  file_menu_->insertItem (_("&Close"), this, SLOT (close ()), CTRL + Key_W);
  file_menu_->insertSeparator ();
  file_menu_->insertItem (_("&Quit"),
                          qApp, SLOT (closeAllWindows ()), CTRL + Key_Q);

  // Set initial file menu item enabled states.
  file_menu_->setItemEnabled (file_synchronize_speed_id_,
                              modeset_.is_receive ());
  file_menu_->setItemEnabled (file_start_id_, true);
  file_menu_->setItemEnabled (file_stop_id_, false);

  // Create the settings popup menu.
  QPopupMenu *settings = new QPopupMenu (this, _("Settings"));
  menuBar ()->insertItem (_("&Settings"), settings);

  reverse_paddles_ = new QCheckBox (_("Reverse Paddles"),
                                    this, _("Reverse Paddles"));
  settings->insertItem (reverse_paddles_);
  curtis_mode_b_ = new QCheckBox (_("Curtis Mode B Timing"),
                                  this, _("Curtis Mode B Timing"));
  connect (curtis_mode_b_,
           SIGNAL (toggled (bool)), SLOT (curtis_mode_b_change ()));
  settings->insertItem (curtis_mode_b_);
  adaptive_receive_ = new QCheckBox (_("Adaptive CW Receive Speed"),
                                     this, _("Adaptive CW Receive Speed"));
  adaptive_receive_->setChecked (true);
  connect (adaptive_receive_,
           SIGNAL (toggled (bool)), SLOT (adaptive_receive_change ()));
  settings->insertItem (adaptive_receive_);
  settings->insertSeparator ();
  settings->insertItem (_("&Font Settings..."), this, SLOT (fonts ()));
  settings->insertItem (_("&Color Settings..."), this, SLOT (colors ()));

  // Create the help popup menu.
  QPopupMenu *help = new QPopupMenu (this, _("Help"));
  menuBar ()->insertSeparator ();
  menuBar ()->insertItem (_("&Help"), help);

  help->insertItem (_("&About"), this, SLOT (about ()), Key_F1);

  // Add the CW display widget, and complete the GUI initializations.
  display_ = new Display (this);
  QWidget *display_widget = display_->get_widget ();
  display_widget->setFocus ();
  QWhatsThis::add (display_widget, DISPLAY_WHATSTHIS);
  setCentralWidget (display_widget);
  display_->show_status (_("Ready"));
  resize (720, 320);

  // Register class handler as the CW library keying event callback. It's
  // important here that we register the static handler, since once we have
  // been into and out of 'C', all concept of 'this' is lost.  It's the job
  // of the static handler to work out which class instance is using the CW
  // library, and call the instance's cwlib_keying_event() function.
  cw_register_keying_callback (cwlib_keying_event_static, NULL);

  // Create a timer for polling send and receive.
  poll_timer_ = new QTimer (this, _("PollTimer"));
  connect (poll_timer_, SIGNAL (timeout ()), SLOT (poll_timer_event ()));

  // Create a sender and a receiver.
  sender_ = new Sender (display_);
  receiver_ = new Receiver (display_);
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
void
Application::about ()
{
  QMessageBox::about (this, ABOUT_CAPTION, ABOUT_TEXT);
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
void
Application::startstop ()
{
  startstop_button_->isOn () ? start () : stop ();
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
          startstop_button_->setOn (false);
          const bool is_stop = QMessageBox::warning (this, _("Xcwcp"),
                                  _("Another Xcwcp window is busy."),
                                  _("&Stop Other"), _("&Cancel"), 0, 0, 1) == 0;
          if (is_stop)
            {
              cwlib_user_application_instance->stop ();
              startstop_button_->setOn (true);
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

      startstop_button_->setOn (true);
      display_->clear_status ();

      file_menu_->setItemEnabled (file_start_id_, false);
      file_menu_->setItemEnabled (file_stop_id_, true);

      // Start the poll timer.  At 60WPM, a dot is 20ms, so polling for the
      // maximum library speed needs a 10ms timeout.
      poll_timer_->start (10, false);
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

      file_menu_->setItemEnabled (file_start_id_, true);
      file_menu_->setItemEnabled (file_stop_id_, false);

      startstop_button_->setOn (false);
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
  application->setCaption (_("Xcwcp"));
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
  const Mode *new_mode = modeset_.get (mode_combo_->currentItem ());

  // If this changes mode type, set the speed synchronization menu item state
  // to enabled for receive mode, disabled otherwise.  And for tidiness, clear
  // the display.
  if (!new_mode->is_same_type_as (modeset_.get_current ()))
    {
      file_menu_->setItemEnabled (file_synchronize_speed_id_,
                                  new_mode->is_receive ());
      display_->clear ();
    }

  // If the mode changed while we're busy, clear the sender and receiver.
  if (is_using_cwlib_)
    {
      sender_->clear ();
      receiver_->clear ();
    }

  // Keep the ModeSet synchronized to mode_combo changes.
  modeset_.set_current (mode_combo_->currentItem ());
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
void
Application::fonts ()
{
  bool status;

  QFont font = QFontDialog::getFont (&status, this);
  if (status)
    {
      QWidget *display_widget = display_->get_widget ();
      display_widget->setFont (font);
    }
}


// colors()
//
// Use a color dialog to allow selection of display color.
void
Application::colors ()
{
  QColor color = QColorDialog::getColor ();
  if (color.isValid ())
    {
      QWidget *display_widget = display_->get_widget ();
      display_widget->setPaletteForegroundColor (color);
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
  if (event->state () & AltButton && event->key () == Key_M)
    {
      mode_combo_->setFocus ();
      event->accept ();
      return;
    }

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

}  // cw namespace
