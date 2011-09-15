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
# AWK script to produce function descriptions from processed C source.
#

# Initialize the states, arrays, and indices.
BEGIN {
  IDLE = 0
  TYPE = 1
  SPECIFICATION = 2
  DESCRIPTION = 3
  state = IDLE

  GLOBALTYPE_TAG = "GT"
  GLOBALSPECIFICATION_TAG = "GS"
  GLOBALDOCUMENTATION_TAG = "GD"
  GLOBALEND_TAG = "GE"
}

# Find a global type tag; start of a function description.
$1 == GLOBALTYPE_TAG {
  sub (GLOBALTYPE_TAG, "")
  sub (/^ */, "")
  gsub (/\t/, " ")
  printf (".nf\n.sp\n.sp\n")
  printf (".BI \"%s ", $0)
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

# Handle all documentation entries.
$1 == GLOBALDOCUMENTATION_TAG {
  if (state == SPECIFICATION || state == TYPE)
    {
      printf (".sp\n")
      printf (".fi\n")
    }
  sub (GLOBALDOCUMENTATION_TAG, "")
  sub (/^ */, "")
  printf ("%s\n", $0)
  state = DESCRIPTION
  next
}

# On end of a function, reset state.
$1 == GLOBALEND_TAG {
  state = IDLE
  next
}
