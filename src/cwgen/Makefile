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
CFLAGS	= $(AC_CFLAGS) $(AC_DEFS) -I../cwutils
GZIP	= $(AC_GZIP)

SHELL	= /bin/sh

default: all

# Build the cwgen binary.
all: cwgen

cwgen.o: cwgen.c

cwgen: cwgen.o
	$(CC) -o $@ cwgen.o ../cwutils/i18n.o ../cwutils/cmdline.o \
	../cwutils/memory.o

# Install targets.
install: all
	$(INSTALL) -d $(bindir) $(mandir)/man1
	$(INSTALL_PROGRAM) cwgen $(bindir)/cwgen
	$(INSTALL_DATA) cwgen.1 $(mandir)/man1/cwgen.1
	$(GZIP) -f -9 $(mandir)/man1/cwgen.1

install-strip:
	$(MAKE) INSTALL_PROGRAM='$(INSTALL_PROGRAM) -s' install

uninstall:
	rm -f $(bindir)/cwgen
	rm -f $(mandir)/man1/cwgen.1 $(mandir)/man1/cwgen.1.gz

# Cleanup targets.
clean:
	rm -f cwgen *.s *.o
	rm -f core core.*

distclean: clean
	rm -f Makefile.port

mostlyclean: clean

maintainer-clean: distclean

# Test targets.
check: all
	-./cwgen

# Unimplemented targets.
TAGS:
info:
dvi:
dist:
