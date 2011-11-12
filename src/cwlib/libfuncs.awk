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
# AWK script to produce function documentation strings from processed C source.
#

# Initialize the states, arrays, and indices.
BEGIN {
	IDLE = 0
	TYPE = 1
	SPECIFICATION = 2
	DOCUMENTATION = 3
	state = IDLE

	FUNCTION_TAG      = "F"
	DOCUMENTATION_TAG = "D"
	END_TAG           = "E"
}

# Handle each global specification entry.
$1 == FUNCTION_TAG {
	sub(FUNCTION_TAG, "")
	sub(/^ */, "")
	gsub(/\t/, "       ")

	if (state != SPECIFICATION) {
		# additional line before printing (possibly
		# multi-line) specification
		print("\n.sp\n");
	}

	printf(".br\n.B \"%s\"\n", $0)
	if ($0 ~ /\)$/) {
		# newline line after last line of (possibly multi-line)
		# specification
		printf(".sp\n", $0)
	}
	state = SPECIFICATION
	next
}

# Handle all documentation entries.
$1 == DOCUMENTATION_TAG {
	if (state == SPECIFICATION) {
		state = DOCUMENTATION
		# empty line between function prototype and
		# function documentation
		print("\n.br\n");
	}

	sub(DOCUMENTATION_TAG, "")

	print $0

	next
}

# On end of a function, reset state.
$1 == END_TAG {
	state = IDLE
	next
}
