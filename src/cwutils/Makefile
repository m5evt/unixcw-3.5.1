# vi: set ts=8 shiftwidth=8:
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

include ../../Makefile.inc
CC	= $(AC_CC)
CFLAGS	= $(AC_CFLAGS) $(AC_DEFS) -I../cwlib

SHELL	= /bin/sh

default: all

# Build everything.
all: cmdline.o dictionary.o i18n.o memory.o

cmdline.o: cmdline.c cmdline.h

dictionary.o: dictionary.c dictionary.h cwwords.h

i18n.o: i18n.c i18n.h

memory.o: memory.c memory.h

# Cleanup targets.
clean:
	rm -f *.s *.o

distclean: clean
	rm -f Makefile.port

mostlyclean: clean

maintainer-clean: distclean

# Test targets; no self-test, but make sure all is built.
check: all

# Unimplemented targets.
install:
install-strip:
uninstall:
TAGS:
info:
dvi:
dist:
