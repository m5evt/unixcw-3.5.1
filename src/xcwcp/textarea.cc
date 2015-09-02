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

#include <QWidget>
#include <QStatusBar>
#include <QTextEdit>
#include <QEvent>
#include <QMenu>
#include <QPoint>

#include <string>

#include "textarea.h"
#include "application.h"

#include "i18n.h"

namespace cw {


const QString DISPLAY_WHATSTHIS =
  _("This is the main display for Xcwcp.  The random CW characters that "
  "Xcwcp generates, any keyboard input you type, and the CW that you "
  "key into Xcwcp all appear here.<br><br>"
  "You can clear the display contents from the File menu.<br><br>"
  "The status bar shows the current character being sent, any character "
  "received, and other general error and Xcwcp status information.");




using namespace cw;



// TextArea()
//
// Call the superclass constructor, and save the application for sending on
// key and mouse events.
TextArea::TextArea(Application *a, QWidget *parent) :
	QTextEdit (parent),
	app (a)
{
	// Block context menu in text area, this is to make right mouse
	// button work as correct sending key (paddle).
	// http://doc.qt.nokia.com/latest/qt.html#ContextMenuPolicy-enum
	// Qt::PreventContextMenu:
	// "the widget does not feature a context menu, [...] the handling
	// is not deferred to the widget's parent. This means that all right
	// mouse button events are guaranteed to be delivered to the widget
	// itself through mousePressEvent(), and mouseReleaseEvent()."
	setContextMenuPolicy(Qt::PreventContextMenu);

	// Clear widget.
	setPlainText("");

	// These two lines just repeat the default settings.
	// I'm putting them here just for fun.
	setLineWrapMode(QTextEdit::WidgetWidth); // Words will be wrapped at the right edge of the text edit. Wrapping occurs at whitespace, keeping whole words intact.
	setWordWrapMode(QTextOption::WordWrap); // Text is wrapped at word boundaries.

	// This can be changed by user in menu Settings -> Text font
	setFontWeight(QFont::Bold);

	QWidget *display_widget = get_widget();
	display_widget->setFocus();
	display_widget->setWhatsThis("DISPLAY_WHATSTHIS");

	app->setCentralWidget(display_widget);
	app->show_status(_("Ready"));
}





// keyPressEvent()
// keyReleaseEvent()
//
// Catch key events and pass them to our parent Application.  Both press
// and release events are merged into one *_event() call.
void TextArea::keyPressEvent(QKeyEvent *event)
{
	app->key_event(event);

	return;
}




void TextArea::keyReleaseEvent(QKeyEvent *event)
{
	app->key_event(event);

	return;
}





// mousePressEvent()
// mouseDoubleClickEvent()
// mouseReleaseEvent()
//
// Do the same for mouse button events.  We need to catch both press and
// double-click, since for keying we don't use or care about double-clicks,
// just any form of button press, any time.
void TextArea::mousePressEvent(QMouseEvent *event)
{
	app->mouse_event(event);

	return;
}





void TextArea::mouseDoubleClickEvent(QMouseEvent *event)
{
	app->mouse_event(event);

	return;
}





void TextArea::mouseReleaseEvent(QMouseEvent *event)
{
	app->mouse_event(event);

	return;
}





// createPopupMenu()
//
// Override and suppress popup menus, so we can use the right mouse button
// as a keyer paddle.
QMenu *TextArea::createPopupMenu(const QPoint &)
{
	return NULL;
}





QMenu *TextArea::createPopupMenu()
{
	return NULL;
}




// get_widget()
//
// Return the underlying QWidget used to implement the display.  Returning
// the widget only states that this is a QWidget, it doesn't tie us to using
// any particular type of widget.
QWidget *TextArea::get_widget()
{
	return this;
}





// append()
//
// Append a character at the current notional cursor position.
void TextArea::append(char c)
{
	this->insertPlainText(QString(QChar(c)));

	return;
}





// backspace()
//
// Delete the character left of the notional cursor position (that is, the
// last one appended).
void TextArea::backspace()
{
	// implementation_->doKeyboardAction (QTextEdit::ActionBackspace);

	return;
}





}  // cw namespace