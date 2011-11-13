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
# Feed output of this script to libsigs.awk and libfuncs.awk to get
# file with function signatures, and file with function
# signatures+documentation respectively.
#





# Initialize the states, tags, and indexes
BEGIN {
	IDLE = 0
	DOCUMENTATION = 1
	FUNCTION_SPECIFICATION = 2
	FUNCTION_BODY = 3
	state = IDLE

	DOCUMENTATION_TAG = "D"
	FUNCTION_TAG      = "F"
	END_TAG           = "E"

	# line counter, starting from zero for every
	# <function's documentation + function's specification> block
	output_line = 0
}





function handle_global_space()
{
	do {
		if ($0 ~ /^static /) {
			# potentially a static function declaration
			#                               function_name
			match($0, /[a-zA-Z0-9_\* ]+ \**([a-zA-Z0-9_]+)\(/, matches);
			if (matches[1] != "") {
				# print matches[1] > "/dev/stderr"
				static_functions[matches[1]] = matches[1];
			}
		}
	} while ($0 !~ /^\/\*\*/ && getline)

	# caught beginning of documentation block (or end of file)

	output_line = 0;
}





# Erase documentation lines from output[]
function delete_documentation(line)
{
	while (line >= 0) {
		output[line--] = "";
	}
}





function handle_function_specification()
{
	#                               function_name
	match($0, /[a-zA-Z0-9_\* ]+ \**([a-zA-Z0-9_]+)\(/, matches);
	if (static_functions[matches[1]]) {
		# specification of static function;
		# no point in processing it

		delete_documentation(output_line - 1)
		output_line = 0

		while ($0 !~ /\)$/ && getline) {
			# read and discard
		}
	} else {
		# read and save function's specification
		# (possibly multi-line)
		do {
			output[output_line++] = FUNCTION_TAG" "$0
		} while ($0 !~ /\)$/ && getline)
	}
}





function handle_function_documentation()
{
	while ($0 !~ /^ *\*\//) {
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

		sub(/^ *\\return /, " Returns: ")

		output[output_line++] = DOCUMENTATION_TAG" "$0
		getline
	}
}





function handle_function_body()
{
	# Ignore function body lines, but watch for a bracket that
	# closes a function
	while ($0 !~ /^\}/) {
		# read and discard lines of function body
		getline
	}
}





function print_documentation_and_specification()
{
	# Print out the specification and documentation lines we have found;
	# reorder documentation and specification so that documentation
	# lines come after the function signatures.

	for (i = 0; i < output_line; i++) {
		if (index(output[i], DOCUMENTATION_TAG) == 0) {
			print output[i]
		}
	}

	for (i = 0; i < output_line; i++) {
		if (index(output[i], DOCUMENTATION_TAG) != 0) {
			print output[i]
		}
	}

	return i
}





# Ignore all blank lines outside of comments and function bodies
/^[[:space:]]*$/ {
	if (state == IDLE) {
		next
	}
}





# Handle every other line in the file according to the state;
# This is the main 'loop' of the script.
{
	# Process static function declarations and change
	# state on '^/**'
	if (state == IDLE) {
		handle_global_space()
		state = DOCUMENTATION
		next
	}


	# Process function documentation blocks, stopping on ' */'.
	if (state == DOCUMENTATION) {
		handle_function_documentation()
		state = FUNCTION_SPECIFICATION
		next
	}


	# Process function specification line(s), stopping on ')$'.
	if (state == FUNCTION_SPECIFICATION) {
		handle_function_specification()
		state = FUNCTION_BODY
		next
	}


	# Process function body, stopping on '^}'
	if (state == FUNCTION_BODY) {
		handle_function_body()
		state = IDLE
	}


	# Print function's documentation and specification,
	# i.e. the data accumulated in above functions
	print_documentation_and_specification()

	print END_TAG

	# prepare for next 'documentation + specification' section
	state = IDLE
	output_line = 0
}





# Simply dump anything we have so far on end of file.
END {
	i = print_documentation_and_specification()
	if (i > 0) {
		print END_TAG
	}
}
