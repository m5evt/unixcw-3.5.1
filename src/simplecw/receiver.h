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

#ifndef H_SIMPLECW_RECEIVER
#define H_SIMPLECW_RECEIVER

#include <QMouseEvent>
#include <QKeyEvent>

#include "config.h"





#include "libcw2.h"





namespace cw {





	class Application;
	class TextArea;
	class Mode;





	/* Class Receiver encapsulates the main application receiver
	   data and functions.  Receiver abstracts states associated
	   with receiving, event handling, libcw keyer event handling,
	   and data passed between signal handler and foreground
	   contexts. */
	class Receiver {
	public:

		Receiver(Application *a, TextArea *t);
		~Receiver();

		/* Poll timeout handler. */
		void poll(int mode);

		/* Keyboard key event handler. */
		void handle_key_event(QKeyEvent *event, bool is_reverse_paddles);

		/* Mouse button press event handler. */
		void handle_mouse_event(QMouseEvent *event, bool is_reverse_paddles);

		/* Straight key and iambic keyer event handler
		   helpers. */
		void sk_event(bool is_down);
		void ik_left_event(bool is_down, bool is_reverse_paddles);
		void ik_right_event(bool is_down, bool is_reverse_paddles);

		/* CW library keying event handler. */
		void handle_libcw_keying_event(struct timeval *t, int key_state);

		/* Clear out queued data on stop, mode change, etc. */
		void clear();

		cw_rec_t *rec;
		cw_key_t *key;

	private:
		Application *app;
		TextArea *textarea;

		/* Flag indicating if receive polling has received a
		   character, and may need to augment it with a word
		   space on a later poll. */
		volatile bool is_pending_inter_word_space;

		/* Flag indicating possible receive errno detected in
		   signal handler context and needing to be passed to
		   the foreground. */
		volatile int libcw_receive_errno;

		/* Safety flag to ensure that we keep the library in
		   sync with keyer events.  Without, there's a chance
		   that of a on-off event, one half will go to one
		   application instance, and the other to another
		   instance. */
		volatile bool tracked_key_state;

		/* State of left and right paddle of iambic keyer. The
		   flags are common for keying with keyboard keys and
		   with mouse buttons.

		   A timestamp for libcw needs to be generated only in
		   situations when one of the paddles comes down and
		   the other is up. This is why we observe state of
		   both paddles separately. */
		bool is_left_down;
		bool is_right_down;

#if 0
		/* Timer for measuring length of dots and
		   dashes. Owned by libcw.

		   Initial value of the timestamp is created by
		   simplecw's receiver on first "paddle down" event in a
		   character. The timestamp is then updated by libcw
		   on specific time intervals. The intervals are a
		   function of keyboard key presses or mouse button
		   presses recorded by simplecw. */
		struct timeval *timer;
#endif

		/* Poll primitives to handle receive errors,
		   characters, and inter-word spaces. */
		void poll_report_error();
		void poll_character();
		void poll_space();

		/* Prevent unwanted operations. */
		Receiver(const Receiver &);
		Receiver &operator=(const Receiver &);
	};





}  /* namespace cw */





#endif  /* #endif H_SIMPLECW_RECEIVER */
