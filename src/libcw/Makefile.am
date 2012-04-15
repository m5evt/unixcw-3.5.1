# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
# Copyright (C) 2011-2012  Kamil Ignacak (acerion@wp.pl)
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

-include $(top_builddir)/Makefile.inc

# targets to be built in this directory
lib_LTLIBRARIES=libcw.la
check_PROGRAMS=libcwunittests libcwtest
man_MANS=libcw.3
pkgconfig_DATA=libcw.pc


EXTRA_DIST=include.awk libdoc.awk libfuncs.awk libpc.awk libsigs.awk \
	libcw.3.m4 \
	libcw.pc.in




# target: shared library

# source code files used to build libcw shared library
libcw_la_SOURCES = libcw.c libcw.h
# target-specific linker flags (objects to link)
libcw_la_LIBADD=-lm -lpthread $(AC_ALSA_LIBS)
# target-specific linker flags (additional flags)
# http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
libcw_la_LDFLAGS = -version-number 3:1:1




# target: libcwtest, a test program, testing public API of libcw library;
# main() is defined in libcwtest.c

# source code files used to build libcwtest program
libcwtest_SOURCES = libcwtest.c
# target-specific linker flags (objects to link)
libcwtest_LDADD = -L./ -lcw




# target: libcwunittest, a test program, invoking unit tests for
# (internal) functions defined in libcw.c;
# main() is defined in libcw.c, enabled by a LIBCW_UNIT_TESTS flag

# source code files used to build libcwunittests program
libcwunittests_SOURCES = libcw.c
# target-specific preprocessor flags (#defs and include dirs)
libcwunittests_CPPFLAGS = -DLIBCW_UNIT_TESTS
# target-specific linker flags (objects to link)
libcwunittests_LDADD=-lm -lpthread $(AC_ALSA_LIBS)



# target: libcw man page
libcw.3: libcw.3.m4 libcw.c
	$(AC_AWK) -f $(top_srcdir)/src/libcw/libdoc.awk  < $(top_srcdir)/src/libcw/libcw.c | $(AC_AWK) -f $(top_srcdir)/src/libcw/libsigs.awk  > signatures
	$(AC_AWK) -f $(top_srcdir)/src/libcw/libdoc.awk  < $(top_srcdir)/src/libcw/libcw.c | $(AC_AWK) -f $(top_srcdir)/src/libcw/libfuncs.awk > functions
	$(AC_AWK) -f $(top_srcdir)/src/libcw/include.awk < $(top_srcdir)/src/libcw/libcw.3.m4 >libcw.3
	rm -f signatures functions




# target: libcw.pc
pkgconfigdir = $(libdir)/pkgconfig




# CLEANFILES extends list of files that need to be removed when
# calling "make clean"
CLEANFILES = greptest.sh libcw.3




TESTS = $(check_SCRIPTS)

# run test programs (only libcwunittests unit tests suite)
check_SCRIPTS = greptest.sh
greptest.sh:
	echo './libcwunittests | grep "test result: success"' > greptest.sh
	chmod +x greptest.sh


# sources, references
#
# source of snippet related to "check_SCRIPTS" and greptests.sh:
# http://www.freesoftwaremagazine.com/books/agaal/automatically_writing_makefiles_with_autotools

