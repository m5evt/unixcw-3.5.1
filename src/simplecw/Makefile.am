# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
# Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)
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
# get empty $(AC_QT5_CFLAGS), $(AC_QT5_LIBS) and $(AC_QT5_MOC);
-include $(top_builddir)/Makefile.inc

# program(s) to be built in current dir
bin_PROGRAMS = xcwcp

# source code files used to build xcwcp program
xcwcp_SOURCES = \
	application.cc application.h \
	moc_application.cc \
	textarea.cc textarea.h \
	main.cc \
	modeset.cc modeset.h \
	receiver.cc receiver.h\
	sender.cc sender.h

# target-specific preprocessor flags (#defs and include dirs)
xcwcp_CPPFLAGS = $(AM_CPPFLAGS) $(AC_QT5_CFLAGS) $(AC_CFLAG_PIC)
# target-specific linker flags (objects to link)
xcwcp_LDADD = -L$(top_builddir)/src/libcw/.libs -lcw $(AC_QT5_LIBS) $(top_builddir)/src/cwutils/lib_xcwcp.a



# CLEANFILES extends list of files that need to be removed when
# calling "make clean"
CLEANFILES = moc_application.cc





# Qt5 magic
# Previously the invocation of moc was: "$(AC_QT5_MOC) $< -o $@", but
# because of failed build on FreeBSD I decided to replace the "$<" and
# "$@" symbols with file names.
moc_application.cc: application.h
	$(AC_QT5_MOC) application.h -o moc_application.cc





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
