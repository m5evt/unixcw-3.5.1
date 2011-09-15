#!/bin/awk -f
# vi: set ts=2 shiftwidth=2 expandtab:
#
# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
#
# Simple AWK script to produce documentation suitable for processing into
# man pages from a C source file.
#

# Initialize the states, arrays, and indices.
BEGIN {
  IDLE = 0
  DOCUMENTATION = 1
  FUNCTION_TYPE = 2
  SPECIFICATION = 3
  FUNCTION_BODY = 4
  state = IDLE

  GLOBAL_TAG = "G"
  STATIC_TAG = "S"
  DOCUMENTATION_TAG = "D"
  FUNCTION_TYPE_TAG = "T"
  SPECIFICATION_TAG = "S"
  static = 0

  # Delete output.
  output_line   = 0
}

# Ignore all blank lines in the file.
/^[[:space:]]*$/ {
  next
}

# Handle every other line in the file according to the state.
{
  # Ignore everything seen while idle.
  if (state == IDLE)
    {
      # Move to documentation state on seeing '/**'.  Read and discard the
      # two lines that follow, up to '^ *$'.
      if ($0 ~ /^\/\*\*/)
        {
          static = 0
          state = DOCUMENTATION
          output_line = 0
          while ($0 !~ /^ \* *$/)
            if (getline == 0)
              break;
          next
        }
      next
    }

  # Catch documentation lines, stopping on ' */'.
  if (state == DOCUMENTATION)
    {
      # Check for end of the documentation.
      if ($0 ~ /^ \*\//)
        {
          # Now expecting the function specification, starting with a one-
          # line return type.
          state = FUNCTION_TYPE
        }
      else
        {
          # Remove any " * ", and save the line as documentation.
          sub (/^ \* /," *")
          sub (/^ \*/,"")
          output[output_line++] = DOCUMENTATION_TAG" "$0
        }
      next
    }

  # Catch the function type, one line after the documentation.
  if (state == FUNCTION_TYPE)
    {
      output[output_line++] = FUNCTION_TYPE_TAG" "$0
      static = ($0 ~ /static/)
      state = SPECIFICATION
      next
    }

  # Catch all specification lines, stopping on '{'.
  if (state == SPECIFICATION)
    {
      # Check for end of the specification.
      if ($0 ~ /^\{/)
        {
          # Now expecting some form of function body.
          state = FUNCTION_BODY
        }
      else
        {
          # Save this line as specification, prepending the function type if
          # we have one.
          output[output_line++] = SPECIFICATION_TAG" "$0
        }
      next
    }

  # Ignore function lines, but at the end, check for 'run-on' functions.  If
  # any are found, go back to storing specifications, otherwise drop through,
  # since we have something to report.
  if (state == FUNCTION_BODY)
    {
      # Check for the closing '}' of the function.
      if ($0 ~ /^\}/)
        {
          # Assume we are going to idle for now.
          state = IDLE

          # Look at the next line, and if blank the line after, to see if it
          # is a blank line.  If not, there is another specification coming.
          nextline = getline
          if (nextline != 0 && $0 ~ /^[[:space:]]*$/)
            nextline = getline
          if (nextline != 0)
            {
              if ($0 !~ /^[[:space:]]*$/)
                {
                  # Set the new state, catch the read specification line, and
                  # continue.
                  output[output_line++] = FUNCTION_TYPE_TAG" "$0
                  static = ($0 ~ /static/)
                  state = SPECIFICATION
                  next
                }
            }
        }
      else
        {
          # Not a closing '}', so still in the function body.
          next
        }
    }

  # Print out the specification and documentation lines we have found, re-
  # ordering so that documentation lines come after the function signatures.
  for (i = 0; i < output_line; i++)
    {
      if (index (output[i], DOCUMENTATION_TAG) == 0)
        print (static?STATIC_TAG:GLOBAL_TAG)output[i]
    }
  for (i = 0; i < output_line; i++)
    {
      if (index (output[i], DOCUMENTATION_TAG) != 0)
        print (static?STATIC_TAG:GLOBAL_TAG)output[i]
    }
  print (static?STATIC_TAG:GLOBAL_TAG)END_TAG

  # Reset variables and state for the next section.
  state = IDLE

  # Delete output.
  output_line   = 0
}

# Simply dump anything we have so far on end of file.
END {
  for (i = 0; i < output_line; i++)
    {
      if (index (output[i], DOCUMENTATION_TAG) == 0)
        print (static?STATIC_TAG:GLOBAL_TAG)output[i]
    }
  for (i = 0; i < output_line; i++)
    {
      if (index (output[i], DOCUMENTATION_TAG) != 0)
        print (static?STATIC_TAG:GLOBAL_TAG)output[i]
    }
  if (i > 0)
    print (static?STATIC_TAG:GLOBAL_TAG)END_TAG
}
