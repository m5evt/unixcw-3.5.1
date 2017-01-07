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
	void poll();

	/* Keyboard key event handler. */
	void handle_key_event(QKeyEvent *event);

	/* Mouse button press event handler. */
	void handle_mouse_event(QMouseEvent *event);

	/* Clear out queued data on stop, mode change, etc. */
	void clear();

	cw_rec_t *rec;
	cw_key_t *key;

 private:
	/* Prevent unwanted operations. */
	Receiver(const Receiver &);
	Receiver &operator=(const Receiver &);

	/* Straight key and iambic keyer event handler
	   helpers. */
	void sk_event(bool is_down);
	void ik_left_event(bool is_down);
	void ik_right_event(bool is_down);


	/* Poll methods to extract from libcw's receiver characters and inter-word spaces. */
	void poll_character();
	void poll_space();


	Application *app;
	TextArea *textarea;

	/* Flag indicating possible receive errno detected in
	   signal handler context and needing to be passed to
	   the foreground. */
	volatile int libcw_receive_errno;
};




#endif  /* #endif H_SIMPLECW_RECEIVER */
