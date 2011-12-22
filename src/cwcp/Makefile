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
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

include ../../Makefile.inc
CC	= $(AC_CC)
CFLAGS	= $(AC_CFLAGS) $(AC_DEFS) -I../libcw -I../cwutils
GZIP	= $(AC_GZIP)
LIBS	= -lcurses -L../libcw -lcw

SHELL	= /bin/sh

default: all

# Build the cwcp binary.
all: cwcp

cwcp.o: cwcp.c ../libcw/libcw.h

cwcp: cwcp.o
	$(CC) $(LDFLAGS) -o $@ cwcp.o ../cwutils/i18n.o ../cwutils/cw_common.o ../cwutils/cmdline.o \
	../cwutils/dictionary.o ../cwutils/memory.o $(LIBS)

# Install targets.
install: all
	$(INSTALL) -d $(bindir) $(mandir)/man1
	$(INSTALL_PROGRAM) -m 4755 cwcp $(bindir)/cwcp
	$(INSTALL_DATA) cwcp.1 $(mandir)/man1/cwcp.1
	$(GZIP) -f -9 $(mandir)/man1/cwcp.1

install-strip:
	$(MAKE) INSTALL_PROGRAM='$(INSTALL_PROGRAM) -s' install

uninstall:
	rm -f $(bindir)/cwcp
	rm -f $(mandir)/man1/cwcp.1 $(mandir)/man1/cwcp.1.gz

# Cleanup targets.
clean:
	rm -f cwcp *.s *.o
	rm -f core core.*

distclean: clean
	rm -f Makefile.port

mostlyclean: clean

maintainer-clean: distclean

# Test targets.
# This test target will be invoked when creating deb package.
check:

# This test target can be invoked manually.
real_check: all
	( echo "Trying with OSS audio output";				\
	  LD_LIBRARY_PATH=../libcw:$$LD_LIBRARY_PATH ./cwcp -so ) ||	\
	( echo "Retrying with ALSA audio output";			\
	  LD_LIBRARY_PATH=../libcw:$$LD_LIBRARY_PATH ./cwcp -sa ) ||	\
	( echo "Retrying with console audio output";			\
	  LIBCW_DEBUG=1 						\
	  LD_LIBRARY_PATH=../libcw:$$LD_LIBRARY_PATH ./cwcp -sc )

# Unimplemented targets.
TAGS:
info:
dvi:
dist:
