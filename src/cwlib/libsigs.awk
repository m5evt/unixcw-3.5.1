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
# AWK script to produce function signatures from processed C source.
#

# Initialize the states, arrays, and indices.
BEGIN {
  IDLE = 0
  SPECIFICATION = 1
  state = IDLE

  FUNCTION_TAG = "F"
  END_TAG = "E"
  specifications = 0
}


# Handle each function specification entry.
$1 == FUNCTION_TAG {
  sub (FUNCTION_TAG, "")
  sub (/^ */, "")
  gsub (/\t/, "        ")
  printf (".BI \"%s\"\n.br\n", $0)
  state = SPECIFICATION
  next
}

# On end of a function, reset state.
$1 == END_TAG {
  state = IDLE
  next
}

# Tidy up on end of file.
END {
  printf ("%s.fi\n", (specifications > 0 ? ".sp\n" : ""))
}
