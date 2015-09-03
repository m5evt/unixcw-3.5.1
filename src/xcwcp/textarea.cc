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

#include "application.h"
#include "textarea.h"
#include "i18n.h"





namespace cw {





const QString DISPLAY_WHATSTHIS =
	_("This is the main display for Xcwcp.  The random CW characters that "
	  "Xcwcp generates, any keyboard input you type, and the CW that you "
	  "key into Xcwcp all appear here.<br><br>"
	  "You can clear the display contents from the File menu.<br><br>"
	  "The status bar shows the current character being sent, any character "
	  "received, and other general error and Xcwcp status information.");





TextArea::TextArea(Application *a, QWidget *parent) :
	QTextEdit (parent),
	app (a)
{
	/* Block context menu in text area, this is to make right mouse
	   button work as correct sending key (paddle).
	   http://doc.qt.nokia.com/latest/qt.html#ContextMenuPolicy-enum
	   Qt::PreventContextMenu:
	   "the widget does not feature a context menu, [...] the handling
	   is not deferred to the widget's parent. This means that all right
	   mouse button events are guaranteed to be delivered to the widget
	   itself through mousePressEvent(), and mouseReleaseEvent()." */
	setContextMenuPolicy(Qt::PreventContextMenu);

	/* Clear widget. */
	setPlainText("");

#if 0
	/* These two lines just repeat the default settings.
	   TODO: maybe just remove them? */

	/* Words will be wrapped at the right edge of the text
	   edit. Wrapping occurs at whitespace, keeping whole words
	   intact. */
	setLineWrapMode(QTextEdit::WidgetWidth);

	/* Text is wrapped at word boundaries. */
	setWordWrapMode(QTextOption::WordWrap);
#endif

	setFontWeight(QFont::Bold);

	setFocus();
	setWhatsThis("DISPLAY_WHATSTHIS");

	app->setCentralWidget(this);
	app->show_status(_("Ready"));
}





/**
   \brief Catch key event and pass it to Application
*/
void TextArea::keyPressEvent(QKeyEvent *event)
{
	app->key_event(event);

	return;
}





/**
   \brief Catch key event and pass it to Application
*/
void TextArea::keyReleaseEvent(QKeyEvent *event)
{
	app->key_event(event);

	return;
}





/**
   \brief Catch mouse event and pass it to Application
*/
void TextArea::mousePressEvent(QMouseEvent *event)
{
	app->mouse_event(event);

	return;
}





/**
   \brief Catch mouse event and pass it to Application

   We need to catch both press and double-click, since for keying we
   don't use or care about double-clicks, just any form of button
   press, any time.
*/
void TextArea::mouseDoubleClickEvent(QMouseEvent *event)
{
	app->mouse_event(event);

	return;
}





/**
   \brief Catch mouse event and pass it to Application
*/
void TextArea::mouseReleaseEvent(QMouseEvent *event)
{
	app->mouse_event(event);

	return;
}





/**
   \brief Avoid creating popup menu

   Override and suppress popup menus, so we can use the right mouse
   button as a keyer paddle.
*/
QMenu *TextArea::createPopupMenu(const QPoint &)
{
	return NULL;
}





/**
   \brief Avoid creating popup menu

   Override and suppress popup menus, so we can use the right mouse
   button as a keyer paddle.
*/
QMenu *TextArea::createPopupMenu()
{
	return NULL;
}





/**
   \brief Append a character at the current notional cursor position.
*/
void TextArea::append(char c)
{
	this->insertPlainText(QString(QChar(c)));

	return;
}





/**
   \brief React to backspace key

   Delete the character left of the notional cursor position (that is,
   the last one appended). Use this function only in sender mode.
*/
void TextArea::backspace()
{
	// implementation_->doKeyboardAction (QTextEdit::ActionBackspace);

	return;
}





}  // namespace cw
