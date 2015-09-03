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

#ifndef H_XCWCP_MODESET
#define H_XCWCP_MODESET





#include <string>
#include <vector>

#include "dictionary.h"





namespace cw {





	class DictionaryMode;
	class KeyboardMode;
	class ReceiveMode;





	/* Class describing an operating mode.  All modes have a
	   description, and dictionary modes add a way to generate
	   random groups of words from the dictionary. */

	class Mode {
	public:
		Mode(const std::string &descr) :
			description (descr) { }
		virtual ~Mode() { }

		std::string get_description() const;

		bool is_same_type_as(const Mode *other) const;

		virtual const DictionaryMode *is_dictionary() const;
		virtual const KeyboardMode *is_keyboard() const;
		virtual const ReceiveMode *is_receive() const;

	private:
		const std::string description;  /* Mode description. */

		/* Prevent unwanted operations. */
		Mode(const Mode &);
		Mode &operator=(const Mode &);
	};





	class DictionaryMode : public Mode {
	public:
		DictionaryMode(const std::string &descr, const cw_dictionary_t *dict) :
			Mode (descr),
			dictionary (dict) { }

		std::string get_random_word_group() const;

		virtual const DictionaryMode *is_dictionary() const;

	private:
		const cw_dictionary_t *dictionary;  /* Dictionary of the mode. */

		/* Prevent unwanted operations. */
		DictionaryMode(const DictionaryMode &);
		DictionaryMode &operator=(const DictionaryMode &);
	};





	class KeyboardMode : public Mode {
	public:
		KeyboardMode(const std::string &descr) :
			Mode (descr) { }

		virtual const KeyboardMode *is_keyboard() const;

	private:
		/* Prevent unwanted operations. */
		KeyboardMode(const KeyboardMode &);
		KeyboardMode &operator=(const KeyboardMode &);
	};





	class ReceiveMode : public Mode {
	public:
		ReceiveMode(const std::string &descr) :
			Mode (descr) { }

		virtual const ReceiveMode *is_receive() const;

	private:
		/* Prevent unwanted operations. */
		ReceiveMode(const ReceiveMode &);
		ReceiveMode &operator=(const ReceiveMode &);
	};





	/*  Class inline functions. */





	inline std::string Mode::get_description() const
	{
		return description;
	}





	inline const DictionaryMode *Mode::is_dictionary() const
	{
		return 0;
	}





	inline const KeyboardMode *Mode::is_keyboard() const
	{
		return 0;
	}





	inline const ReceiveMode *Mode::is_receive() const
	{
		return 0;
	}





	inline const DictionaryMode *DictionaryMode::is_dictionary() const
	{
		return this;
	}





	inline const KeyboardMode *KeyboardMode::is_keyboard() const
	{
		return this;
	}





	inline const ReceiveMode *ReceiveMode::is_receive() const
	{
		return this;
	}





	/* Class that aggregates modes, created from dictionaries and
	   locally, and provides a concept of a current mode and
	   convenient access to modes based on the current mode
	   setting. */

	class ModeSet {
	public:
		ModeSet();

		void set_current(int index);
		const Mode *get_current() const;

		int get_count() const;
		const Mode *get(int index) const;

	private:
		const std::vector<Mode*> *const modes;
		const Mode *current;

		/* Prevent unwanted operations. */
		ModeSet(const ModeSet &);
		ModeSet &operator=(const ModeSet &);
	};





	// Class inline functions.





	inline void ModeSet::set_current(int index)
	{
		current = modes->at(index);

		return;
	}





	inline const Mode *ModeSet::get_current() const
	{
		return current;
	}





	inline int ModeSet::get_count() const
	{
		return modes->size();
	}





	inline const Mode *ModeSet::get(int index) const
	{
		return modes->at(index);
	}





}  /* namespace cw */





#endif  /* #ifndef H_XCWCP_MODESET */
