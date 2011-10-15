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
QTDIR	= $(AC_QTDIR)
QTMOC	= $(AC_QTMOC)
CXX	= $(AC_CXX)
CXXFLAGS= $(AC_CXXFLAGS) $(AC_DEFS) -I../cwlib -I../cwutils -I$(QTDIR)/include -I/usr/include/qt4/Qt -I/usr/include/qt4/QtCore -I/usr/include/qt4/QtGui
GZIP	= $(AC_GZIP)

SHELL	= /bin/sh

# List of object files built in this directory.
OBJECTS = main.o modeset.o display.o sender.o receiver.o application.o \
          moc_application.o

# Link to the shared libcw.
LIBS	= -L$(QTDIR)/lib -L../cwlib -lcw -lpthread -lasound -lQtGui -lQtCore

default: all

# Build the xcwcp binary.
all: xcwcp

xcwcp: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o xcwcp $(OBJECTS) ../cwutils/i18n.o ../cwutils/cw_common.o \
	../cwutils/cmdline.o ../cwutils/dictionary.o ../cwutils/memory.o \
	$(LIBS) -lm

main.o: main.cc application.h

modeset.o: modeset.cc modeset.h

display.o: display.cc display.h application.h

sender.o: sender.cc sender.h display.h modeset.h

receiver.o: receiver.cc receiver.h display.h modeset.h

application.o: application.cc sender.h receiver.h application.h \
               modeset.h display.h

mod_application.o: moc_application.cc

moc_application.cc: application.h
	$(QTMOC) $< -o $@

# Install targets.
install: all
	$(INSTALL) -d $(bindir) $(mandir)/man1
	$(INSTALL_PROGRAM) -m 4755 xcwcp $(bindir)/xcwcp
	$(INSTALL_DATA) xcwcp.1 $(mandir)/man1/xcwcp.1
	$(GZIP) -f -9 $(mandir)/man1/xcwcp.1

install-strip:
	$(MAKE) INSTALL_PROGRAM='$(INSTALL_PROGRAM) -s' install

uninstall:
	rm -f $(bindir)/xcwcp
	rm -f $(mandir)/man1/xcwcp.1 $(mandir)/man1/xcwcp.1.gz

# Cleanup targets.
clean:
	rm -f xcwcp *.s *.o
	rm -f moc_*.cc
	rm -f core core.*

distclean: clean
	rm -f Makefile.port

mostlyclean: clean

maintainer-clean: distclean

# Test targets.
check: all
	-LD_LIBRARY_PATH=../cwlib:$$LD_LIBRARY_PATH ./xcwcp -sb ||	\
	 ( echo "Retrying the test for soundcard only";			\
	   LD_LIBRARY_PATH=../cwlib:$$LD_LIBRARY_PATH ./xcwcp -ss ) ||	\
	 ( echo "Retrying the test in silent mode";			\
	   CWLIB_DEBUG=1 \
	   LD_LIBRARY_PATH=../cwlib:$$LD_LIBRARY_PATH ./xcwcp -ss )

# Unimplemented targets.
TAGS:
info:
dvi:
dist:
