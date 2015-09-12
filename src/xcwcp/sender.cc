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
#include <deque>
#include <sstream>


#include "application.h"
#include "sender.h"
#include "textarea.h"
#include "modeset.h"

#include "libcw2.h"

#include "i18n.h"





namespace cw {



Sender::Sender(Application *a, TextArea *t, cw_config_t *config) :
			app (a),
			textarea (t),
			is_queue_idle (true)
{
	gen = cw_gen_new_from_config(config);
	if (!gen) {
		fprintf(stderr, "%s: failed to create generator\n", config->program_name);
	}

	cw_gen_start(gen);
}





Sender::~Sender()
{
	if (gen) {
		cw_gen_stop(gen);
		cw_gen_delete(&gen);
	}
}





/**
   \brief Get more characters to send

   Check the CW library tone queue, and if it is getting low, arrange
   for more data to be passed in to the sender.
*/
void Sender::poll(const Mode *current_mode)
{
	if (current_mode->is_dictionary() || current_mode->is_keyboard()) {
		if (cw_gen_queue_length(this->gen) <= 1) {
			/* Arrange more data for the sender.  In
			   dictionary modes, add more random data if
			   the queue is empty.  In keyboard mode, just
			   dequeue anything currently on the character
			   queue. */
			if (current_mode->is_dictionary() && queue.empty()) {
				enqueue_string(std::string(1, ' ')
					       + current_mode->get_dmode()->get_random_word_group());
			}

			dequeue_and_play_character();
		}
	}

	return;
}





/**
   \brief Handle keys entered in main window in keyboard mode

   If key is playable, the function enqueues the key for playing and
   accepts the key event \p event.

   If key event is not playable (e.g. Tab characters), the event is
   not accepted.

   Function handles only key presses. Key releases are ignored.

   Call the function only when keyboard mode is active.

   \param event - key event in main window to handle
*/
void Sender::handle_key_event(QKeyEvent *event)
{
	if (event->type() == QEvent::KeyPress) {

		if (event->key() == Qt::Key_Backspace) {

			/* Remove the last queued character, or at
			   least try, and we are done. */
			delete_character();
			event->accept();
		} else {
			/* Enqueue and accept only valid characters. */
			const char *c = event->text().toLatin1().data();

			if (cw_character_is_valid(c[0])) {
				enqueue_string(c);
				event->accept();
			}
		}
	}

	return;
}





/**
   \brief Clear sender state

   Flush libcw tone queue, empty the character queue, and set state to idle.
*/
void Sender::clear()
{
	cw_gen_flush_queue(this->gen);
	queue.clear();
	is_queue_idle = true;

	return;
}





/**
   \brief Get next character from character queue and play it

   Called when the CW send buffer is empty.  If the queue is not idle,
   take the next character from the queue and play it.  If there are
   no more queued characters, set the queue to idle.
*/
void Sender::dequeue_and_play_character()
{
	if (is_queue_idle) {
		return;
	}

	if (queue.empty()) {
		is_queue_idle = true;
		app->clear_status();
		return;
	}

	/* Take the next character off the queue and play it.  We
	   don't expect playing to fail as only valid characters are
	   queued. */
	const char c = queue.front();
	queue.pop_front();
	if (!cw_gen_enqueue_character(this->gen, c)) {
		perror("cw_gen_enqueue_character");
		/* TODO: don't call abort(). */
		abort();
	}

	/* Update the status bar with the character being played.  Put
	   the played char at the end to avoid "jumping" of whole
	   string when width of glyph of played char changes at
	   variable font width. */
	QString status = _("Sending at %1 WPM: '%2'");
	app->show_status(status.arg(cw_gen_get_speed(this->gen)).arg(c));

	return;
}





/**
   \brief Enqueue a string in player's queue

   Only valid characters from the \p s are enqueued. Invalid
   characters are discarded and no error is reported. Function does
   not perform validation of \p s before trying to enqueue it.

   \param s - string to be enqueued
*/
void Sender::enqueue_string(const std::string &s)
{
	for (unsigned int i = 0; i < s.size(); i++) {
		const char c = s[i];

		if (cw_character_is_valid(c)) {
			queue.push_back(c);
			textarea->append(c);

			is_queue_idle = false;
		}
	}

	return;
}





/**
   \brief Delete last character from queue

   Remove the most recently added character from the queue, provided that
   the dequeue hasn't yet reached it.  If there's nothing available to
   delete, don't report errors.
*/
void Sender::delete_character()
{
	if (!queue.empty()) {
		queue.pop_back();
		textarea->backspace();
	}

	return;
}





}  /* namespace cw */
