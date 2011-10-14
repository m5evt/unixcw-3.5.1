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

#include <Qt/qwidget.h>
#include <Qt/qstatusbar.h>
#include <Qt/qstring.h>
#include <Qt/qtextedit.h>
#include <Qt/qevent.h>
#include <Qt/qmenu.h>
#include <Qt/qpoint.h>

#include <string>

#include "display.h"
#include "application.h"

#include "i18n.h"

namespace cw {


//-----------------------------------------------------------------------
//  Class DisplayImpl
//-----------------------------------------------------------------------

// DisplayImpl class, extends QTextEdit.  This class is used as the
// implementation of the simple text display.  It overrides QTextEdit in
// order to gain finer control over the way text is displayed, and is local
// to this module.

class DisplayImpl : public QTextEdit {
 public:
	DisplayImpl (Application *application, QWidget *parent);

 protected:
  // Functions overridden to catch events from the parent class.
  void keyPressEvent (QKeyEvent *event);
  void keyReleaseEvent (QKeyEvent *event);

  void contentsMousePressEvent (QMouseEvent *event);
  void contentsMouseDoubleClickEvent (QMouseEvent *event);
  void contentsMouseReleaseEvent (QMouseEvent *event);

  virtual QMenu *createPopupMenu (const QPoint &);
  virtual QMenu *createPopupMenu ();

 private:
  // Application to forward key and mouse events to.
  Application *application_;

  // Prevent unwanted operations.
  DisplayImpl (const DisplayImpl &);
  DisplayImpl &operator= (const DisplayImpl &);
};


// DisplayImpl()
//
// Call the superclass constructor, and save the application for sending on
// key and mouse events.
	DisplayImpl::DisplayImpl (Application *application, QWidget *parent)
  : QTextEdit (parent), application_ (application)
{
	setPlainText("");
	//setWordWrap (WidgetWidth);
	//setWrapPolicy (Anywhere);
	//setBold (true);
}


// keyPressEvent()
// keyReleaseEvent()
//
// Catch key events and pass them to our parent Application.  Both press
// and release events are merged into one *_event() call.
void
DisplayImpl::keyPressEvent (QKeyEvent *event)
{
  application_->key_event (event);
}

void
DisplayImpl::keyReleaseEvent (QKeyEvent *event)
{
  application_->key_event (event);
}


// contentsMousePressEvent()
// contentsMouseDoubleClickEvent()
// contentsMouseReleaseEvent()
//
// Do the same for mouse button events.  We need to catch both press and
// double-click, since for keying we don't use or care about double-clicks,
// just any form of button press, any time.
void
DisplayImpl::contentsMousePressEvent (QMouseEvent *event)
{
  application_->mouse_event (event);
}

void
DisplayImpl::contentsMouseDoubleClickEvent (QMouseEvent *event)
{
  application_->mouse_event (event);
}

void
DisplayImpl::contentsMouseReleaseEvent (QMouseEvent *event)
{
  application_->mouse_event (event);
}


// createPopupMenu()
//
// Override and suppress popup menus, so we can use the right mouse button
// as a keyer paddle.
QMenu *
DisplayImpl::createPopupMenu (const QPoint &)
{
  return NULL;
}

QMenu *
DisplayImpl::createPopupMenu ()
{
  return NULL;
}


//-----------------------------------------------------------------------
//  Class Display
//-----------------------------------------------------------------------

// Display()
//
// Create a display implementation, passing the application to be informed
// when the display widget receives key or mouse events.
	Display::Display (Application *application, QWidget *parent)
		: application_ (application), implementation_ (new DisplayImpl (application, parent))
{
}


// get_widget()
//
// Return the underlying QWidget used to implement the display.  Returning
// the widget only states that this is a QWidget, it doesn't tie us to using
// any particular type of widget.
QWidget *
Display::get_widget () const
{
  return implementation_;
}


// append()
//
// Append a character at the current notional cursor position.
void
Display::append (char c)
{
  implementation_->insertPlainText (QString (QChar (c)));
}


// backspace()
//
// Delete the character left of the notional cursor position (that is, the
// last one appended).
void
Display::backspace ()
{
	// implementation_->doKeyboardAction (QTextEdit::ActionBackspace);
}


// clear()
//
// Clear the display area.
void
Display::clear ()
{
  implementation_->clear ();
}


// show_status()
//
// Display the given string on the status line.
void
Display::show_status (const QString &status)
{
  application_->statusBar ()->showMessage(status);
}


// clear_status()
//
// Clear the status line.
void
Display::clear_status ()
{
  application_->statusBar ()->clearMessage();
}

}  // cw namespace
