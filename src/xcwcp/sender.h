// Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
// Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)
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

#ifndef H_XCWCP_SENDER
#define H_XCWCP_SENDER





#include <QKeyEvent>

#include <string>
#include <deque>





/* Class Sender encapsulates the main application sender data and
   functions.  Sender abstracts the send character queue, polling, and
   event handling. */





namespace cw {





	class Application;
	class TextArea;
	class Mode;





	class Sender {
	public:
		Sender(Application *a, TextArea *t) :
			app (a),
			textarea (t),
			is_queue_idle (true) { }

		/* Poll timeout handler, and keypress event
		   handler. */
		void poll(const Mode *current_mode);
		void handle_key_event(QKeyEvent *event);

		/* Clear out queued data on stop, mode change, etc. */
		void clear();

	private:
		/* Deque and queue manipulation functions, used to
		   handle and maintain the buffer of characters
		   awaiting sending through libcw. */
		void dequeue_and_play_character();
		void enqueue_string(const std::string &word);
		void delete_character();


		Application *app;
		TextArea *textarea;

		bool is_queue_idle;
		std::deque<char> queue;


		/* Prevent unwanted operations. */
		Sender(const Sender &);
		Sender &operator=(const Sender &);
	};





}  /* namespace cw */





#endif  /* #ifndef H_XCWCP_SENDER */
