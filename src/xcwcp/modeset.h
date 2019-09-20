// Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
// Copyright (C) 2011-2019 Kamil Ignacak (acerion@wp.pl)
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

		inline std::string get_description() const { return description; };

		bool is_same_type_as(const Mode *other) const;

		inline virtual bool is_dictionary() const { return false; };
		inline virtual bool is_keyboard()   const { return false; };
		inline virtual bool is_receive()    const { return false; };

		virtual const DictionaryMode *get_dmode() const { return NULL; };
		virtual const KeyboardMode *get_kmode()   const { return NULL; };
		virtual const ReceiveMode *get_rmode()    const { return NULL; };

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

		inline virtual bool is_dictionary() const { return true; };
		inline virtual const DictionaryMode *get_dmode() const { return this; };

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

		inline virtual bool is_keyboard() const { return true; };
		inline virtual const KeyboardMode *get_kmode() const { return this; };

	private:
		/* Prevent unwanted operations. */
		KeyboardMode(const KeyboardMode &);
		KeyboardMode &operator=(const KeyboardMode &);
	};





	class ReceiveMode : public Mode {
	public:
		ReceiveMode(const std::string &descr) :
			Mode (descr) { }

		inline virtual bool is_receive() const { return true; };
		inline virtual const ReceiveMode *get_rmode() const { return this; };

	private:
		/* Prevent unwanted operations. */
		ReceiveMode(const ReceiveMode &);
		ReceiveMode &operator=(const ReceiveMode &);
	};





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
