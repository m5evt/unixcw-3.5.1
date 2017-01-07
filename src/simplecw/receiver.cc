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

#include <cstdio>
#include <cstdlib>
#include <string>
#include <cerrno>
#include <sstream>


#include "application.h"
#include "receiver.h"
#include "textarea.h"

#include "libcw2.h"




namespace cw {




Receiver::Receiver(Application *a, TextArea *t)
{
	app = a;
	textarea = t;

	libcw_receive_errno = 0;

	this->rec = cw_rec_new();
	this->key = cw_key_new();
}





Receiver::~Receiver()
{
	cw_rec_delete(&this->rec);
	cw_key_delete(&this->key);
}





/**
   \brief Poll the CW library receive buffer and handle anything found
   in the buffer

   \param current_mode
*/
void Receiver::poll()
{
	if (libcw_receive_errno != 0) {
		poll_report_error();
	}

	if (cw_rec_poll_is_pending_inter_word_space(this->rec)) {

		/* Check if receiver received the pending inter-word
		   space. */
		poll_space();

		if (!cw_rec_poll_is_pending_inter_word_space(this->rec)) {
			/* We received the pending space. After it the
			   receiver may have received another
			   character.  Try to get it too. */
			poll_character();
		}
	} else {
		/* Not awaiting a possible space, so just poll the
		   next possible received character. */
		poll_character();
	}

	return;
}





/**
   \brief Handle keyboard keys pressed in main window in receiver mode

   Function handles both press and release events, but ignores
   autorepeat.

   Call the function only when receiver mode is active.

   \param event - key event in main window to handle
*/
void Receiver::handle_key_event(QKeyEvent *event)
{
	if (event->isAutoRepeat()) {
		/* Ignore repeated key events.  This prevents
		   autorepeat from getting in the way of identifying
		   the real keyboard events we are after. */
		return;
	}

	if (event->type() == QEvent::KeyPress
	    || event->type() == QEvent::KeyRelease) {

		const int is_down = event->type() == QEvent::KeyPress;

		if (event->key() == Qt::Key_Space
		    || event->key() == Qt::Key_Up
		    || event->key() == Qt::Key_Down
		    || event->key() == Qt::Key_Enter
		    || event->key() == Qt::Key_Return) {

			/* These keys are obvious candidates for
			   "straight key" key. */

			sk_event(is_down);
			event->accept();

		} else if (event->key() == Qt::Key_Left) {
			ik_left_event(is_down);
			event->accept();

		} else if (event->key() == Qt::Key_Right) {
			ik_right_event(is_down);
			event->accept();

		} else {
			event->accept();
			; /* Some other, uninteresting key. Ignore it. */
		}
	}

	return;
}





/**
   \brief Handle mouse events

   The function looks at mouse button events and interprets them as
   one of: left iambic key event, right iambic key event, straight key
   event.

   Call the function only when receiver mode is active.

   \param event - mouse event to handle
*/
void Receiver::handle_mouse_event(QMouseEvent *event)
{
	if (event->type() == QEvent::MouseButtonPress
	    || event->type() == QEvent::MouseButtonDblClick
	    || event->type() == QEvent::MouseButtonRelease) {

		const int is_down = event->type() == QEvent::MouseButtonPress
			|| event->type() == QEvent::MouseButtonDblClick;

		if (event->button() == Qt::MidButton) {
			sk_event(is_down);
			event->accept();

		} else if (event->button() == Qt::LeftButton) {
			ik_left_event(is_down);
			event->accept();

		} else if (event->button() == Qt::RightButton) {
			ik_right_event(is_down);
			event->accept();

		} else {
			; /* Some other mouse button, or mouse cursor
			     movement. Ignore it. */
		}
	}

	return;
}





/**
   \brief Handle straight key event

   \param is_down
*/
void Receiver::sk_event(bool is_down)
{
	cw_key_sk_notify_event(this->key, is_down);

	return;
}





/**
   \brief Handle event on left paddle of iambic keyer

   \param is_down
*/
void Receiver::ik_left_event(bool is_down)
{
	/* Inform libcw about state of left paddle regardless of state
	   of the other paddle. */
	cw_key_ik_notify_dot_paddle_event(this->key, is_down);

	return;
}





/**
   \brief Handle event on right paddle of iambic keyer

   \param is_down
*/
void Receiver::ik_right_event(bool is_down)
{
	/* Inform libcw about state of left paddle regardless of state
	   of the other paddle. */
	cw_key_ik_notify_dash_paddle_event(this->key, is_down);

	return;
}




/**
   \brief Clear the library receive buffer and our own flags
*/
void Receiver::clear()
{
	cw_rec_clear_buffer(this->rec);
	libcw_receive_errno = 0;

	return;
}





/**
   \brief Handle any error registered when handling a libcw keying event
*/
void Receiver::poll_report_error()
{
	/* Handle any receive errors detected on tone end but delayed until here. */
	app->show_status(libcw_receive_errno == ENOENT
			 ? "Badly formed CW element"
			 : "Receive buffer overrun");

	libcw_receive_errno = 0;

	return;
}





/**
   \brief Receive any new character from the CW library.
*/
void Receiver::poll_character()
{
	char c;

	struct timeval timer;
	gettimeofday(&timer, NULL);
	//fprintf(stderr, "poll_character:  %10ld : %10ld\n", timer.tv_sec, timer.tv_usec);

	if (cw_rec_poll_character(this->rec, &timer, &c, NULL, NULL)) {
		/* Receiver stores full, well formed
		   character. Display it. */
		textarea->append(c);

		/* A full character has been received. Directly after
		   it comes a space. Either a short inter-character
		   space followed by another character (in this case
		   we won't display the inter-character space), or
		   longer inter-word space - this space we would like
		   to catch and display. */

		/* Update the status bar to show the character
		   received.  Put the received char at the end of
		   string to avoid "jumping" of whole string when
		   width of glyph of received char changes at variable
		   font width. */
		QString status = "Received at %1 WPM: '%2'";
		app->show_status(status.arg(cw_rec_get_speed(this->rec)).arg(c));
		//fprintf(stderr, "Received character '%c'\n", c);

	} else {
		/* Handle receive error detected on trying to read a character. */
		switch (errno) {
		case EAGAIN:
			/* Call made too early, receiver hasn't
			   received a full character yet. Try next
			   time. */
			break;

		case ERANGE:
			/* Call made not in time, or not in proper
			   sequence. Receiver hasn't received any
			   character (yet). Try harder. */
			break;

		case ENOENT:
			/* Invalid character in receiver's buffer. */
			cw_rec_clear_buffer(this->rec);
			textarea->append('?');
			app->show_status(QString("Unknown character received at %1 WPM").arg(cw_rec_get_speed(this->rec)));
			break;

		default:
			perror("cw_rec_poll_character");
			abort();
		}
	}

	return;
}





/**
   If we received a character on an earlier poll, check again to see
   if we need to revise the decision about whether it is the end of a
   word too.
*/
void Receiver::poll_space()
{
	/* Recheck the receive buffer for end of word. */
	bool is_end_of_word;

	/* We expect the receiver to contain a character, but we don't
	   ask for it this time. The receiver should also store
	   information about an inter-character space. If it is longer
	   than a regular inter-character space, then the receiver
	   will treat it as inter-word space, and communicate it over
	   is_end_of_word. */

	struct timeval timer;
	gettimeofday(&timer, NULL);
	//fprintf(stderr, "poll_space(): %10ld : %10ld\n", timer.tv_sec, timer.tv_usec);
	cw_rec_poll_character(this->rec, &timer, NULL, &is_end_of_word, NULL);
	if (is_end_of_word) {
		//fprintf(stderr, "End of word\n\n");
		textarea->append(' ');
		cw_rec_clear_buffer(this->rec);
	} else {
		/* The space that currently lasts after last polled
		   non-space character isn't long enough to be
		   considered inter-word space. It may grow to become
		   the inter-word space. Or not.

		   This growing of inter-character space into
		   inter-word space may be terminated by incoming next
		   tone (key down event) - the tone will mark
		   beginning of new character within the same
		   word. */
	}

	return;
}




}  /* namespace cw */
