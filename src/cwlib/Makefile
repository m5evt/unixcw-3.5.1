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
CFLAGS	= $(AC_CFLAGS) $(AC_CFLAG_PIC) $(AC_DEFS)
AWK	= $(AC_AWK)
RANLIB	= $(AC_RANLIB)
LD	= $(AC_LD)
LN_S	= $(AC_LN_S)
GZIP	= $(AC_GZIP)
LDCONFIG= $(AC_LDCONFIG)
CC_LINKS_SO	= $(AC_CC_LINKS_SO)
LD_LINKS_SO	= $(AC_LD_LINKS_SO)

SHELL	= /bin/sh

default: all

all: libcw.a libcw.so.0.0.0 libcw.so.0 libcw.so libcw.pc cwlib.3 cwlibtest

# Build the static and shared libraries.
libcw.a: cwlib.o
	rm -f libcw.a; ar -cr libcw.a cwlib.o
	$(RANLIB) libcw.a

libcw.so.0.0.0: cwlib.o
	if [ "$(CC_LINKS_SO)" = "yes" ]; then				\
		$(CC) -shared -Wl,-soname,libcw.so.0			\
			-o libcw.so.0.0.0 cwlib.o;			\
	else								\
		if [ "$(LD_LINKS_SO)" = "yes" ]; then			\
			$(LD) -G -Wl,-soname,libcw.so.0			\
				-o libcw.so.0.0.0 cwlib.o;		\
		fi							\
	fi

libcw.so.0: libcw.so.0.0.0
	$(LN_S) -f libcw.so.0.0.0 libcw.so.0

libcw.so: libcw.so.0.0.0
	$(LN_S) -f libcw.so.0.0.0 libcw.so

# Build the library man page.
cwlib.3: cwlib.3.m4 cwlib.c
	$(AWK) -f libdoc.awk <cwlib.c | $(AWK) -f libsigs.awk >signatures
	$(AWK) -f libdoc.awk <cwlib.c | $(AWK) -f libfuncs.awk >functions
	$(AWK) -f include.awk <cwlib.3.m4 >cwlib.3
	rm -f signatures functions

# Build the unit test.
cwlibtest: cwlibtest.o
	$(CC) -o cwlibtest cwlibtest.o -L. -lcw -lm -lpthread -lasound

# Build the pkgconfig metadata file.
libcw.pc:
	$(AWK) -vprefix="$(prefix)" -v exec_prefix="$(exec_prefix)"	\
	       -vlibdir="$(libdir)" -v includedir="$(includedir)"	\
	       -vstrip="$$RPM_BUILD_ROOT"				\
	       -vconfig_h="../config.h" -f libpc.awk >libcw.pc

# Define dependencies related to header/include files.
cwlib.o: Makefile cwlib.h

cwlibtest.o: Makefile cwlib.h

# Install targets.
install: all
	$(INSTALL) -d $(includedir) $(libdir) $(mandir)/man3 $(mandir)/man7 \
		$(libdir)/pkgconfig
	$(INSTALL_DATA) cwlib.h $(includedir)/cwlib.h
	$(INSTALL_DATA) libcw.a $(libdir)/libcw.a
	if [ "$(CC_LINKS_SO)" = "yes" -o "$(LD_LINKS_SO)" = "yes" ]; then \
		$(INSTALL_PROGRAM) libcw.so.0.0.0 $(libdir)/libcw.so.0.0.0; \
		$(LN_S) -f libcw.so.0.0.0 $(libdir)/libcw.so.0; \
		$(LN_S) -f libcw.so.0.0.0 $(libdir)/libcw.so; \
		$(LDCONFIG) -n $(libdir); \
	fi
	$(INSTALL_DATA) cwlib.3 $(mandir)/man3/cwlib.3
	$(GZIP) -f -9 $(mandir)/man3/cwlib.3
	$(INSTALL_DATA) cw.7 $(mandir)/man7/cw.7
	$(GZIP) -f -9 $(mandir)/man7/cw.7
	$(INSTALL_DATA) cw.7 $(mandir)/man7/CW.7
	$(GZIP) -f -9 $(mandir)/man7/CW.7
	$(INSTALL_DATA) libcw.pc $(libdir)/pkgconfig/libcw.pc

install-strip:
	$(MAKE) INSTALL_PROGRAM='$(INSTALL_PROGRAM) -s' install

uninstall:
	rm -f $(includedir)/cwlib.h
	rm -f $(libdir)/libcw.a
	rm -f $(libdir)/libcw.so $(libdir)/libcw.so.0 $(libdir)/libcw.so.0.0.0
	$(LDCONFIG) -n $(libdir)
	rm -f $(mandir)/man3/cwlib.3 $(mandir)/man3/cwlib.3.gz
	rm -f $(mandir)/man7/cw.7 $(mandir)/man7/cw.7.gz
	rm -f $(mandir)/man7/CW.7 $(mandir)/man7/CW.7.gz
	rm -f $(libdir)/pkgconfig/libcw.pc

# Cleanup targets.
clean:
	rm -f libcw.a libcw.so libcw.so.0 libcw.so.0.0.0 *.s libcw.pc *.o
	rm -f cwlib.3 signatures functions
	rm -f cwlibtest
	rm -f core core.*

distclean: clean
	rm -f Makefile.port

mostlyclean: clean

maintainer-clean: distclean

# Test targets.
check: all cwlibtest
	-LD_LIBRARY_PATH=.:$$LD_LIBRARY_PATH ./cwlibtest

# Unimplemented targets.
TAGS:
info:
dvi:
dist:
