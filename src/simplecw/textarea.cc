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

#include <QApplication>
#include <QWidget>
#include <QStatusBar>
#include <QTextEdit>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPoint>

#include <string>

#include "application.h"
#include "textarea.h"




namespace cw {




TextArea::TextArea(Application *a, QWidget *parent) : QTextEdit(parent), app(a)
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

	setFontWeight(QFont::Bold);

	setFocus();

	app->setCentralWidget(this);
	app->show_status("Ready");
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
	QKeyEvent *keyEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
	QTextEdit::keyPressEvent(keyEvent);

	return;
}




}  // namespace cw
