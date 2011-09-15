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
# AWK script to produce function signatures from processed C source.
#

# Initialize the states, arrays, and indices.
BEGIN {
  IDLE = 0
  TYPE = 1
  SPECIFICATION = 2
  state = IDLE

  GLOBALTYPE_TAG = "GT"
  GLOBALSPECIFICATION_TAG = "GS"
  GLOBALEND_TAG = "GE"
  specifications = 0
}

# Find a global type tag.
$1 == GLOBALTYPE_TAG {
  printf ("%s\n", (specifications > 0 ? ".sp" : ".nf"))
  sub (GLOBALTYPE_TAG, "")
  sub (/^ */, "")
  gsub (/\t/, " ")
  printf (".BI \"%s ", $0)
  specifications++
  state = TYPE
  next
}

# Handle each global specification entry.
$1 == GLOBALSPECIFICATION_TAG {
  sub (GLOBALSPECIFICATION_TAG, "")
  sub (/^ */, "")
  gsub (/\t/, " ")
  printf ("%s%s\"\n", (state == TYPE ? "" : ".BI \""), $0)
  state = SPECIFICATION
  next
}

# On end of a function, reset state.
$1 == GLOBALEND_TAG {
  state = IDLE
  next
}

# Tidy up on end of file.
END {
  printf ("%s.fi\n", (specifications > 0 ? ".sp\n" : ""))
}
