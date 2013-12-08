# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
# Copyright (C) 2011-2013  Kamil Ignacak (acerion@wp.pl)
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

# use $(top_builddir) instead of $(top_srcdir) because Makefile.inc is
# (re)created at build time; if you use $(top_srcdir), you may (will?)
# get empty $(AC_QT4CFLAGS) and $(AT_QT4MOC);
-include $(top_builddir)/Makefile.inc

# program(s) to be built in current dir
bin_PROGRAMS = xcwcp

# source code files used to build xcwcp program
xcwcp_SOURCES = \
	application.cc application.h \
	moc_application.cc \
	display.cc display.h \
	main.cc \
	modeset.cc modeset.h \
	receiver.cc receiver.h\
	sender.cc sender.h

# target-specific preprocessor flags (#defs and include dirs)
xcwcp_CPPFLAGS = $(AM_CPPFLAGS) $(AC_QT4CFLAGS) #-I$(AC_QT4INC)/QtCore -I$(AC_QT4INC)/QtGui
# target-specific linker flags (objects to link)
xcwcp_LDADD = -L$(top_builddir)/src/libcw/.libs -lcw -lQtGui -lQtCore $(top_builddir)/src/cwutils/lib_xcwcp.a



# copy man page to proper directory during installation
man_MANS = xcwcp.1
# and mark it as distributable, too
EXTRA_DIST = xcwcp.1 \
	icons/icon_mini_xcwcp.xpm \
	icons/icon_start.xpm \
	icons/icon_stop.xpm



# CLEANFILES extends list of files that need to be removed when
# calling "make clean"
CLEANFILES = moc_application.cc



# Qt4 magic
moc_application.cc: application.h
	$(AC_QT4MOC) $< -o $@

# Test targets.
# This test target will be invoked when creating deb package.
check:

# This test target can be invoked manually.
real_check: all
	( echo "Trying with OSS audio output";				\
	  LD_LIBRARY_PATH=../libcw:$$LD_LIBRARY_PATH ./xcwcp -so ) ||	\
	( echo "Retrying with ALSA audio output";			\
	  LD_LIBRARY_PATH=../libcw:$$LD_LIBRARY_PATH ./xcwcp -sa ) ||	\
	( echo "Retrying the test in silent mode";			\
	  LIBCW_DEBUG=1 						\
	  LD_LIBRARY_PATH=../libcw:$$LD_LIBRARY_PATH ./xcwcp -sc )

