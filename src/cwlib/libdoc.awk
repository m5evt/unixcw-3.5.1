#!/bin/awk -f
#
# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
# Copyright (C) 2011       Kamil Ignacak (acerion@wp.pl)
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
	FUNCTION_SPECIFICATION = 2
	FUNCTION_BODY = 3
	state = IDLE

	DOCUMENTATION_TAG = "D"
	FUNCTION_TAG      = "F"
	END_TAG           = "E"

	skip_this_specification = 0

	# Delete output.
	output_line   = 0
}



# Ignore all blank lines in the file.
/^[[:space:]]*$/ {
	if (state == IDLE) {
		next
	}
}

# Handle every other line in the file according to the state.
{
	# Ignore everything seen while idle.
	if (state == IDLE) {
		if ($0 ~ /^static /) {
			# potentially a static function declaration

			match($0, /[a-zA-Z0-9_\* ]+ \**([a-zA-Z0-9_]+)\(/, matches);
			if (matches[1] != "") {
				# print matches[1] > "/dev/stderr"
				static_functions[matches[1]] = matches[1];
			}
		} else if ($0 ~ /^\/\*\*/) {
			# Move to documentation state on seeing '/**'.  Read and discard the
			# two lines that follow, up to '^ *$'.
			state = DOCUMENTATION
			output_line = 0

			next
		} else {

		}
		next
	}


	# Catch documentation lines, stopping on ' */'.
	if (state == DOCUMENTATION) {

		# Check for end of the documentation.
		if ($0 ~ /^ *\*\//) {
			# Now expecting the function specification
			state = FUNCTION_SPECIFICATION

			# attempt to read the specification; if it turns out
			# to be static function (static_functions[]), this
			# flag will be set to 1, and the specification
			# (possibly multiline) won't be processed
			skip_this_specification = 0;
		} else {
			# Some documentation texts still have " * " at the
			# beginning sub (/^ \* /," *")
			sub(/^ \* */,"")

			# Handle Doxygen tags
			sub(/^ *\\brief /, "Brief: ")
			sub(/^ *\\param /, "Parameter: ")

			if (match($0, /\\param ([0-9a-zA-Z_]+)/, matches)) {
				replacement = "\\fB"matches[1]"\\fP"
				gsub(/(\\param [0-9a-zA-Z_]+)/, replacement, $0)
			}

			sub(/^ *\\return /, "Returns: ")

			output[output_line++] = DOCUMENTATION_TAG" "$0
		}
		next
	}


	# Catch all specification lines, stopping on '{'.
	if (state == FUNCTION_SPECIFICATION) {
		# Check for end of the specification.
		if ($0 ~ /^\{/) {
			# Now expecting some form of function body.
			state = FUNCTION_BODY
		} else {
			if (!skip_this_specification) {
				match($0, /[a-zA-Z0-9_\* ]+ \**([a-zA-Z0-9_]+)\(/, matches);
				if (static_functions[matches[1]]) {
					# specification of static function
					# no point in processing it
					skip_this_specification = 1

					# static function, mark its documentation
					# for removal
					i = output_line - 1;
					while (output[i] ~ /^D/) {
						output[i] = "";
						i--;
					}
				} else {

					# Save this line as specification, prepending the function type if
					# we have one.
					output[output_line++] = FUNCTION_TAG" "$0
				}
			}
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
                  output[output_line++] = FUNCTION_TAG" "$0
                  state = FUNCTION_SPECIFICATION
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
      if (index(output[i], DOCUMENTATION_TAG) == 0)
        print output[i]
    }
  for (i = 0; i < output_line; i++)
    {
      if (index(output[i], DOCUMENTATION_TAG) != 0)
        print output[i]
    }
  print END_TAG

  # Reset variables and state for the next section.
  state = IDLE

  # Delete output.
  output_line   = 0
}

# Simply dump anything we have so far on end of file.
END {
  for (i = 0; i < output_line; i++)
    {
      if (index(output[i], DOCUMENTATION_TAG) == 0)
        print output[i]
    }
  for (i = 0; i < output_line; i++)
    {
      if (index(output[i], DOCUMENTATION_TAG) != 0)
        print output[i]
    }
  if (i > 0)
    print END_TAG
}
