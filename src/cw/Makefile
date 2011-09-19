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
CFLAGS	= $(AC_CFLAGS) $(AC_DEFS) -I../cwlib -I../cwutils
GZIP	= $(AC_GZIP)

SHELL	= /bin/sh

default: all

LIBS	= -L../cwlib -lcw -lpthread

# Build the cw binary.
all: cw

cw.o: cw.c ../cwlib/cwlib.h

cw: cw.o
	$(CC) -o $@ cw.o ../cwutils/i18n.o ../cwutils/cmdline.o	\
	../cwutils/memory.o $(LIBS) -lm

# Install targets.
install: all
	$(INSTALL) -d $(includedir) $(bindir) $(mandir)/man1
	$(INSTALL_DATA) cw.h $(includedir)/cw.h
	$(INSTALL_PROGRAM) -m 4755 cw $(bindir)/cw
	$(INSTALL_DATA) cw.1 $(mandir)/man1/cw.1
	$(GZIP) -f -9 $(mandir)/man1/cw.1

install-strip:
	$(MAKE) INSTALL_PROGRAM='$(INSTALL_PROGRAM) -s' install

uninstall:
	rm -f $(includedir)/cw.h
	rm -f $(bindir)/cw
	rm -f $(mandir)/man1/cw.1 $(mandir)/man1/cw.1.gz

# Cleanup targets.
clean:
	rm -f cw *.s *.o
	rm -f core core.*

distclean: clean
	rm -f Makefile.port

mostlyclean: clean

maintainer-clean: distclean

# Test targets.
check: all
	-LD_LIBRARY_PATH=../cwlib:$$LD_LIBRARY_PATH ./cw -sb -f test.cw ||     \
	 ( echo "Retrying the test for soundcard only";			       \
	   LD_LIBRARY_PATH=../cwlib:$$LD_LIBRARY_PATH ./cw -ss -f test.cw ) || \
	 ( echo "Retrying the test in silent mode";			       \
	   CWLIB_DEBUG=1 \
	   LD_LIBRARY_PATH=../cwlib:$$LD_LIBRARY_PATH ./cw -ss -f test.cw )

# Unimplemented targets.
TAGS:
info:
dvi:
dist:
