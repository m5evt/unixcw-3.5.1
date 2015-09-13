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
#include "modeset.h"

#include "libcw2.h"

#include "i18n.h"





namespace cw {





Receiver::Receiver(Application *a, TextArea *t)
{
	app = a;
	textarea = t;

	is_pending_inter_word_space = false;
	libcw_receive_errno = 0;

	tracked_key_state = false;

	is_left_down = false;
	is_right_down = false;

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
void Receiver::poll(const Mode *current_mode)
{
	if (!current_mode->is_receive()) {
		return;
	}

	if (libcw_receive_errno != 0) {
		poll_report_error();
	}

	if (is_pending_inter_word_space) {

		/* Check if receiver received the pending inter-word
		   space. */
		poll_space();

		if (!is_pending_inter_word_space) {
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
   \param is_reverse_paddles
*/
void Receiver::handle_key_event(QKeyEvent *event, bool is_reverse_paddles)
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

			fprintf(stderr, "---------- handle key event: sk: %d\n", is_down);
			sk_event(is_down);
			event->accept();

		} else if (event->key() == Qt::Key_Left) {
			ik_left_event(is_down, is_reverse_paddles);
			event->accept();

		} else if (event->key() == Qt::Key_Right) {
			ik_right_event(is_down, is_reverse_paddles);
			event->accept();

		} else {
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
   \param is_reverse_paddles
*/
void Receiver::handle_mouse_event(QMouseEvent *event, bool is_reverse_paddles)
{
	if (event->type() == QEvent::MouseButtonPress
	    || event->type() == QEvent::MouseButtonDblClick
	    || event->type() == QEvent::MouseButtonRelease) {

		const int is_down = event->type() == QEvent::MouseButtonPress
			|| event->type() == QEvent::MouseButtonDblClick;

		if (event->button() == Qt::MidButton) {
			fprintf(stderr, "---------- handle mouse event: sk: %d\n", is_down);
			sk_event(is_down);
			event->accept();

		} else if (event->button() == Qt::LeftButton) {
			ik_left_event(is_down, is_reverse_paddles);
			event->accept();

		} else if (event->button() == Qt::RightButton) {
			ik_right_event(is_down, is_reverse_paddles);
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
#if 0
	/* Prepare timestamp for libcw on both "key up" and "key down"
	   events. */
	gettimeofday(this->timer, NULL);
	// fprintf(stderr, "time on Skey down:  %10ld : %10ld\n", this->timer->tv_sec, this->timer->tv_usec);
#endif

	cw_key_sk_notify_event(this->key, is_down);

	return;
}





/**
   \brief Handle event on left paddle of iambic keyer

   \param is_down
   \param is_reverse_paddles
*/
void Receiver::ik_left_event(bool is_down, bool is_reverse_paddles)
{
	is_left_down = is_down;
#if 0
	if (is_left_down && !is_right_down) {
		/* Prepare timestamp for libcw, but only for initial
		   "paddle down" event at the beginning of
		   character. Don't create the timestamp for any
		   successive "paddle down" events inside a character.

		   In case of iambic keyer the timestamps for every
		   next (non-initial) "paddle up" or "paddle down"
		   event in a character will be created by libcw. */
		gettimeofday(this->timer, NULL);
		//fprintf(stderr, "time on Lkey down:  %10ld : %10ld\n", this->timer->tv_sec, this->timer->tv_usec);
	}
#endif

	/* Inform libcw about state of left paddle regardless of state
	   of the other paddle. */
	is_reverse_paddles
		? cw_key_ik_notify_dash_paddle_event(this->key, is_down)
		: cw_key_ik_notify_dot_paddle_event(this->key, is_down);

	return;
}





/**
   \brief Handle event on right paddle of iambic keyer

   \param is_down
   \param is_reverse_paddles
*/
void Receiver::ik_right_event(bool is_down, bool is_reverse_paddles)
{
	is_right_down = is_down;
#if 0
	if (is_right_down && !is_left_down) {
		/* Prepare timestamp for libcw, but only for initial
		   "paddle down" event at the beginning of
		   character. Don't create the timestamp for any
		   successive "paddle down" events inside a character.

		   In case of iambic keyer the timestamps for every
		   next (non-initial) "paddle up" or "paddle down"
		   event in a character will be created by libcw. */
		gettimeofday(this->timer, NULL);
		//fprintf(stderr, "time on Rkey down:  %10ld : %10ld\n", this->timer->tv_sec, this->timer->tv_usec);
	}
#endif

	/* Inform libcw about state of left paddle regardless of state
	   of the other paddle. */
	is_reverse_paddles
		? cw_key_ik_notify_dot_paddle_event(this->key, is_down)
		: cw_key_ik_notify_dash_paddle_event(this->key, is_down);

	return;
}





/**
   \brief Handler for the keying callback from the CW library
   indicating that the state of a key has changed.

   The "key" is libcw's internal key structure. Its state is updated
   by libcw when e.g. one iambic keyer paddle is constantly
   pressed. It is also updated in other situations. In any case: the
   function is called whenever state of this key changes.

   Notice that the description above talks about a key, not about a
   receiver. Key's states need to be interpreted by receiver, which is
   a separate task. Key and receiver are separate concepts. This
   function connects them. The connection is made through \p t and \p
   key_state values which come from a key, and are passed to receiver.

   This function, called on key state changes, calls receiver
   functions to ensure that receiver does "receive" the key state
   changes.

   This function is called in signal handler context, and it takes
   care to call only functions that are safe within that context.  In
   particular, it goes out of its way to deliver results by setting
   flags that are later handled by receive polling.
*/
void Receiver::handle_libcw_keying_event(struct timeval *t, int key_state)
{
	/* Ignore calls where the key state matches our tracked key
	   state.  This avoids possible problems where this event
	   handler is redirected between application instances; we
	   might receive an end of tone without seeing the start of
	   tone. */
	if (key_state == tracked_key_state) {
		//fprintf(stderr, "tracked key state == %d\n", tracked_key_state);
		return;
	} else {
		//fprintf(stderr, "tracked key state := %d\n", key_state);
		tracked_key_state = key_state;
	}

	/* If this is a tone start and we're awaiting an inter-word
	   space, cancel that wait and clear the receive buffer. */
	if (key_state && is_pending_inter_word_space) {
		/* Tell receiver to prepare (to make space) for
		   receiving new character. */
		cw_rec_clear_buffer(this->rec);

		/* The tone start means that we're seeing the next
		   incoming character within the same word, so no
		   inter-word space is possible at this point in
		   time. The space that we were observing/waiting for,
		   was just inter-character space. */
		is_pending_inter_word_space = false;
	}

	//fprintf(stderr, "calling callback, stage 2\n");

	/* Pass tone state on to the library.  For tone end, check to
	   see if the library has registered any receive error. */
	if (key_state) {
		/* Key down. */
		//fprintf(stderr, "start receive tone: %10ld . %10ld\n", t->tv_sec, t->tv_usec);
		if (!cw_rec_mark_begin(this->rec, t)) {
			perror("cw_rec_mark_begin");
			abort();
		}
	} else {
		/* Key up. */
		//fprintf(stderr, "end receive tone:   %10ld . %10ld\n", t->tv_sec, t->tv_usec);
		if (!cw_rec_mark_end(this->rec, t)) {
			/* Handle receive error detected on tone end.
			   For ENOMEM and ENOENT we set the error in a
			   class flag, and display the appropriate
			   message on the next receive poll. */
			switch (errno) {
			case EAGAIN:
				/* libcw treated the tone as noise (it
				   was shorter than noise threshold).
				   No problem, not an error. */
				break;

			case ENOMEM:
			case ENOENT:
				libcw_receive_errno = errno;
				cw_rec_clear_buffer(this->rec);
				break;

			default:
				perror("cw_rec_mark_end");
				abort();
			}
		}
	}

	return;
}





/**
   \brief Clear the library receive buffer and our own flags
*/
void Receiver::clear()
{
	cw_rec_clear_buffer(this->rec);
	is_pending_inter_word_space = false;
	libcw_receive_errno = 0;
	tracked_key_state = false;

	return;
}





/**
   \brief Handle any error registered when handling a libcw keying event
*/
void Receiver::poll_report_error()
{
	/* Handle any receive errors detected on tone end but delayed until here. */
	app->show_status(libcw_receive_errno == ENOENT
			 ? _("Badly formed CW element")
			 : _("Receive buffer overrun"));

	libcw_receive_errno = 0;

	return;
}





/**
   \brief Receive any new character from the CW library.
*/
void Receiver::poll_character()
{
	char c;

	/* Don't use receiver->timer - it is used exclusively for
	   marking initial "key down" events. Use local timer2.

	   Additionally using receiver->timer here would mess up time
	   intervals measured by receiver->timer, and that would
	   interfere with recognizing dots and dashes. */
	struct timeval timer2;
	gettimeofday(&timer2, NULL);
	//fprintf(stderr, "poll_receive_char:  %10ld : %10ld\n", timer2.tv_sec, timer2.tv_usec);

	if (cw_rec_poll_character(this->rec, &timer2, &c, NULL, NULL)) {
		/* Receiver stores full, well formed
		   character. Display it. */
		textarea->append(c);

		/* A full character has been received. Directly after
		   it comes a space. Either a short inter-character
		   space followed by another character (in this case
		   we won't display the inter-character space), or
		   longer inter-word space - this space we would like
		   to catch and display.

		   Set a flag indicating that next poll may result in
		   inter-word space. */
		is_pending_inter_word_space = true;

		/* Update the status bar to show the character
		   received.  Put the received char at the end of
		   string to avoid "jumping" of whole string when
		   width of glyph of received char changes at variable
		   font width. */
		QString status = _("Received at %1 WPM: '%2'");
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
			{	/* New scope to avoid gcc 3.2.2
				   internal compiler error. */

				cw_rec_clear_buffer(this->rec);
				textarea->append('?');

				QString status = _("Unknown character received at %1 WPM");
				app->show_status(status.arg(cw_rec_get_speed(this->rec)));
			}
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
	   is_end_of_word.

	   Don't use receiver->timer - it is used eclusively for
	   marking initial "key down" events. Use local timer2. */
	struct timeval timer2;
	gettimeofday(&timer2, NULL);
	//fprintf(stderr, "poll_space(): %10ld : %10ld\n", timer2.tv_sec, timer2.tv_usec);
	cw_rec_poll_character(this->rec, &timer2, NULL, &is_end_of_word, NULL);
	if (is_end_of_word) {
		//fprintf(stderr, "End of word\n\n");
		textarea->append(' ');
		cw_rec_clear_buffer(this->rec);
		is_pending_inter_word_space = false;
	} else {
		/* We don't reset is_pending_inter_word_space. The
		   space that currently lasts, and isn't long enough
		   to be considered inter-word space, may grow to
		   become the inter-word space. Or not.

		   This growing of inter-character space into
		   inter-word space may be terminated by incoming next
		   tone (key down event) - the tone will mark
		   beginning of new character within the same
		   word. And since a new character begins, the flag
		   will be reset (elsewhere). */
	}

	return;
}

}  /* namespace cw */
