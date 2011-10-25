/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011       Kamil Ignacak (acerion@wp.pl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "../config.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "cwlib.h"

#include "dictionary.h"
#include "cwwords.h"
#include "memory.h"
#include "i18n.h"


/*---------------------------------------------------------------------*/
/*  Dictionary data                                                    */
/*---------------------------------------------------------------------*/

enum { FALSE = 0, TRUE = !FALSE };
enum { MAX_LINE = 8192 };

/* Aggregate dictionary data into a structure. */
struct dictionary_s
{
  const char *description;      /* Dictionary description */
  const char *const *wordlist;  /* Dictionary word list */
  int wordlist_length;          /* Length of word list */
  int group_size;               /* Size of a group */

  void *mutable_description;    /* Freeable (aliased) description string */
  void *mutable_wordlist;       /* Freeable (aliased) word list */
  void *mutable_wordlist_data;  /* Freeable bulk word list data */

  dictionary *next;             /* List pointer */
};

/* Current loaded dictionaries list head. */
static dictionary *dictionaries_head = NULL;


/*---------------------------------------------------------------------*/
/*  Dictionary implementation                                          */
/*---------------------------------------------------------------------*/

/*
 * dictionary_new()
 * dictionary_new_const()
 * dictionary_new_mutable()
 *
 * Create a new dictionary, and add to any list tail passed in, returning
 * the entry created (the new list tail).  The main function adds the data,
 * plus any mutable pointers that need to be freed on destroying the
 * dictionary.  The const and mutable variants are convenience interfaces.
 */
static dictionary *
dictionary_new (dictionary *tail,
                const char *description, const char *const wordlist[],
                void *mutable_description, void *mutable_wordlist,
                void *mutable_wordlist_data)
{
  dictionary *dict;
  int words, is_multicharacter, word;

  /* Count words in the wordlist, and look for multicharacter entries. */
  words = 0;
  is_multicharacter = FALSE;
  for (word = 0; wordlist[word]; word++)
    {
      is_multicharacter |= strlen (wordlist[word]) > 1;
      words++;
    }

  /*
   * Create a new dictionary and fill in the main fields.  Group size is
   * set to one for multicharacter word lists, five otherwise.
   */
  dict = safe_malloc (sizeof (*dict));
  dict->description = description;
  dict->wordlist = wordlist;
  dict->wordlist_length = words;
  dict->group_size = is_multicharacter ? 1 : 5;
  dict->next = NULL;

  /* Add mutable pointers passed in. */
  dict->mutable_description = mutable_description;
  dict->mutable_wordlist = mutable_wordlist;
  dict->mutable_wordlist_data = mutable_wordlist_data;

  /* Add to the list tail passed in, if any. */
  if (tail)
    tail->next = dict;

  return dict;
}

static dictionary *
dictionary_new_const (dictionary *tail,
                      const char *description, const char *const wordlist[])
{
  return dictionary_new (tail, description, wordlist, NULL, NULL, NULL);
}

static dictionary *
dictionary_new_mutable (dictionary *tail,
                        char *description, const char *wordlist[],
                        void *wordlist_data)
{
  return dictionary_new (tail, description, wordlist,
                         description, wordlist, wordlist_data);
}


/*
 * dictionary_unload()
 *
 * Free any allocations from the current dictionary, and return to the
 * initial state.
 */
void
dictionary_unload (void)
{
  dictionary *entry, *next;

  /* Free each dictionary in the list. */
  for (entry = dictionaries_head; entry; entry = next)
    {
      next = entry->next;

      /* Free allocations held in the dictionary. */
      free (entry->mutable_wordlist);
      free (entry->mutable_description);
      free (entry->mutable_wordlist_data);

      /* Free the dictionary itself. */
      free (entry);
    }

  dictionaries_head = NULL;
}


/*
 * dictionary_getline()
 *
 * Helper function for dictionary_load().  Returns the next file line (or
 * FALSE if no more lines), stripped of any trailing nl/cr.
 */
static int
dictionary_getline (FILE *stream, char *buffer, int length, int *line_number)
{
  if (!feof (stream) && fgets (buffer, length, stream))
    {
      int bytes;

      bytes = strlen (buffer);
      while (bytes > 0 && strchr ("\r\n", buffer[bytes - 1]))
        buffer[--bytes] = '\0';

      *line_number += 1;
      return TRUE;
    }

  return FALSE;
}


/*
 * dictionary_is_parse_comment()
 * dictionary_is_parse_section()
 *
 * Parse helpers, categorize lines and return allocated fields if matched.
 */
static int
dictionary_is_parse_comment (const char *line)
{
  size_t index;

  index = strspn (line, " \t");
  return index == strlen (line) || strchr (";#", line[0]);
}

static int
dictionary_is_parse_section (const char *line, char **name_ptr)
{
  char *name, dummy;
  int count;

  name = safe_malloc (strlen (line) + 1);

  count = sscanf (line, " [ %[^]] ] %c", name, &dummy);
  if (count == 1)
    {
      *name_ptr = safe_realloc (name, strlen (name) + 1);
      return TRUE;
    }

  free (name);
  return FALSE;
}


/*
 * dictionary_build_wordlist()
 *
 * Build and return a wordlist from a string of space-separated words.  The
 * wordlist_data is changed by this function.
 */
static const char **
dictionary_build_wordlist (char *wordlist_data)
{
  const char **wordlist, *word;
  int size, allocation;

  /* Split contents into a wordlist, and store each word retrieved. */
  size = allocation = 0;
  wordlist = NULL;
  for (word = strtok (wordlist_data, " \t"); word; word = strtok (NULL, " \t"))
    {
      if (size == allocation)
        {
          allocation = allocation == 0 ? 1 : allocation << 1;
          wordlist = safe_realloc (wordlist, sizeof (*wordlist) * allocation);
        }

      wordlist[size++] = word;
    }

  /* Add a null sentinel. */
  if (size == allocation)
    {
      allocation++;
      wordlist = safe_realloc (wordlist, sizeof (*wordlist) * allocation);
    }
  wordlist[size++] = NULL;

  return wordlist;
}


/*
 * dictionary_trim()
 *
 * Helper functions for dictionary_load().  Trims a line of all leading and
 * trailing whitespace.
 */
static void
dictionary_trim (char *buffer)
{
  int bytes, index;

  bytes = strlen (buffer);
  while (bytes > 0 && isspace (buffer[bytes - 1]))
    buffer[--bytes] = '\0';

  index = strspn (buffer, " \t");
  if (index > 0)
    memmove (buffer, buffer + index, bytes - index + 1);
}


/*
 * dictionary_check_line()
 *
 * Check a line for unsendable characters.  Returns an allocated string with
 * '^' in error positions, and spaces otherwise, or NULL if no unsendable
 * characters.
 */
static char *
dictionary_check_line (const char *line)
{
  char *errors;
  int count, index;

  /* Allocate a string, and set a '^' marker for any unsendable characters. */
  errors = safe_malloc (strlen (line) + 1);
  count = 0;

  for (index = 0; line[index] != '\0'; index++)
    {
      errors[index] = cw_check_character (line[index]) ? ' ' : '^';
      if (errors[index] == '^')
        count++;
    }
  errors[index] = '\0';

  /* If not all sendable, return the string, otherwise return NULL. */
  if (count > 0)
    return errors;

  free (errors);
  return NULL;
}


/*
 * dictionary_create_from_stream()
 *
 * Create a dictionary list from a stream.  Returns the list head on success,
 * NULL if loading fails.  The file format is expected to be ini-style.
 */
static dictionary *
dictionary_create_from_stream (FILE *stream, const char *file)
{
  int line_number;
  char *line, *name, *content;
  const char **wordlist;
  dictionary *head, *tail;

  /* Clear the variables used to accumulate stream data. */
  line = safe_malloc (MAX_LINE);
  line_number = 0;
  name = content = NULL;
  head = tail = NULL;

  /* Parse input lines to create a new dictionary. */
  while (dictionary_getline (stream, line, MAX_LINE, &line_number))
    {
      char *new_name;

      if (dictionary_is_parse_comment (line))
        continue;

      else if (dictionary_is_parse_section (line, &new_name))
        {
          /*
           * New section, so handle data accumulated so far.  Or if no data
           * accumulated, forget it.
           */
          if (content)
            {
              wordlist = dictionary_build_wordlist (content);
              tail = dictionary_new_mutable (tail, name, wordlist, content);
              head = head ? head : tail;
            }
          else
            free (name);

          /* Start new accumulation of words. */
          dictionary_trim (new_name);
          name = new_name;
          content = NULL;
        }

      else if (name)
        {
          char *errors;

          /* Check the line for unsendable characters. */
          errors = dictionary_check_line (line);
          if (errors)
            {
              fprintf (stderr, "%s:%d: unsendable character found:\n",
                               file, line_number);
              fprintf (stderr, "%s\n%s\n", line, errors);
              free (errors);
            }

          /* Accumulate this line into the current content. */
          dictionary_trim (line);
          if (content)
            {
              content = safe_realloc (content,
                                      strlen (content) + strlen (line) + 2);
              strcat (content, " ");
              strcat (content, line);
            }
          else
            {
              content = safe_malloc (strlen (line) + 1);
              strcpy (content, line);
            }
        }

      else
        fprintf (stderr, "%s:%d: unrecognized line, expected [section]"
                         " or commentary\n", file, line_number);
    }

  /* Handle any final accumulated data. */
  if (content)
    {
      wordlist = dictionary_build_wordlist (content);
      tail = dictionary_new_mutable (tail, name, wordlist, content);
      head = head ? head : tail;
    }

  if (!head)
    fprintf (stderr, "%s:%d: no usable dictionary data found in"
                     " the file\n", file, line_number);

  free (line);
  return head;
}


/*
 * dictionary_create_default()
 *
 * Create a dictionary list from internal data.  Returns the list head.
 */
static dictionary *
dictionary_create_default (void)
{
  dictionary *head, *tail;

  head = dictionary_new_const (NULL, _("Letter Groups"), CW_ALPHABETIC);
  tail = dictionary_new_const (head, _("Number Groups"), CW_NUMERIC);
  tail = dictionary_new_const (tail, _("Alphanum Groups"), CW_ALPHANUMERIC);
  tail = dictionary_new_const (tail, _("All Char Groups"), CW_ALL_CHARACTERS);
  tail = dictionary_new_const (tail, _("English Words"), CW_SHORT_WORDS);
  tail = dictionary_new_const (tail, _("CW Words"), CW_CW_WORDS);
  tail = dictionary_new_const (tail, _("PARIS Calibrate"), CW_PARIS);
  tail = dictionary_new_const (tail, _("EISH5 Groups"), CW_EISH5);
  tail = dictionary_new_const (tail, _("TMO0 Groups"), CW_TMO0);
  tail = dictionary_new_const (tail, _("AUV4 Groups"), CW_AUV4);
  tail = dictionary_new_const (tail, _("NDB6 Groups"), CW_NDB6);
  tail = dictionary_new_const (tail, _("KX=-RP Groups"), CW_KXffRP);
  tail = dictionary_new_const (tail, _("FLYQC Groups"), CW_FLYQC);
  tail = dictionary_new_const (tail, _("WJ1GZ Groups"), CW_WJ1GZ);
  tail = dictionary_new_const (tail, _("23789 Groups"), CW_23789);
  tail = dictionary_new_const (tail, _(",?.;)/ Groups"), CW_FIGURES_1);
  tail = dictionary_new_const (tail, _("\"'$(+:_ Groups"), CW_FIGURES_2);

  return head;
}


/*
 * dictionary_load()
 *
 * Set the main dictionary list to data read from a file.  Returns TRUE on
 * success, FALSE if loading fails.
 */
int
dictionary_load (const char *file)
{
  FILE *stream;
  dictionary *head;

  /* Open the input stream, or fail if unopenable. */
  stream = fopen (file, "r");
  if (!stream)
    {
      fprintf (stderr, "%s: open error: %s\n", file, strerror (errno));
      return FALSE;
    }

  /*
   * If we can generate a dictionary list, free any currently allocated one
   * and store the details of what we loaded into module variables.
   */
  head = dictionary_create_from_stream (stream, file);
  if (head)
    {
      dictionary_unload ();
      dictionaries_head = head;
    }

  /* Close stream and return TRUE if we loaded a dictionary. */
  fclose (stream);
  return head != NULL;
}


/*
 * dictionary_iterate()
 *
 * Iterate known dictionaries.  Returns the first if dictionary is NULL,
 * otherwise the next, or NULL if no more.
 *
 * Because this is the only way dictionaries can be accessed by callers,
 * this function sets up a default dictionary list if none loaded.
 */
const dictionary *
dictionary_iterate (const dictionary *current)
{
  /* If no dictionary list has been loaded, supply a default one. */
  if (!dictionaries_head)
    dictionaries_head = dictionary_create_default ();

  return current ? current->next : dictionaries_head;
}


/*
 * dictionary_write()
 *
 * Write the currently loaded (or default) dictionary out to a given file.
 * Returns TRUE on success, FALSE if write fails.
 */
int
dictionary_write (const char *file)
{
  FILE *stream;
  const dictionary *dict;

  /* Open the output stream, or fail if unopenable. */
  stream = fopen (file, "w");
  if (!stream)
    return FALSE;

  /*
   * If no dictionary list has been loaded, supply a default one, then
   * print details of each.
   */
  if (!dictionaries_head)
    dictionaries_head = dictionary_create_default ();

  for (dict = dictionaries_head; dict; dict = dict->next)
    {
      int index, chars;

      fprintf (stream, "[ %s ]\n\n", dict->description);

      chars = 0;
      for (index = 0; index < dict->wordlist_length; index++)
        {
          fprintf (stream, " %s", dict->wordlist[index]);
          chars += strlen (dict->wordlist[index]) + 1;
          if (chars > 72)
            {
              fprintf (stream, "\n");
              chars = 0;
            }
        }
      fprintf (stream, chars > 0 ? "\n\n" : "\n");
    }

  fclose (stream);
  return TRUE;
}


/*
 * get_dictionary_description()
 * get_dictionary_group_size()
 *
 * Return the text description and group size for a given dictionary.
 */
const char *
get_dictionary_description (const dictionary *dict)
{
  return dict->description;
}

int get_dictionary_group_size (const dictionary *dict)
{
  return dict->group_size;
}


/*
 * get_dictionary_random_word()
 *
 * Return a random word from the given dictionary.
 */
const char *
get_dictionary_random_word (const dictionary *dict)
{
  static int is_initialized = FALSE;

  /* On the first call, seed the random number generator. */
  if (!is_initialized)
    {
      srand (time (NULL));

      is_initialized = TRUE;
    }

  return dict->wordlist[rand () % dict->wordlist_length];
}
