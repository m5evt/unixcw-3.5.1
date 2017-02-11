# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
# Copyright (C) 2011-2017  Kamil Ignacak (acerion@wp.pl)
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
check_PROGRAMS=libcw_test_public libcw_test_simple_gen libcw_test_tq_short_space


EXTRA_DIST=libcw_test_public.c libcw_test_simple_gen.c libcw_test_tq_short_space.c \
	libcw_test.c libcw_test.h





# library built only for purposes of testing (checking)
check_LIBRARIES = libcw_check.a
libcw_check_a_SOURCES = libcw_test.c
libcw_check_a_CPPFLAGS = $(AM_CPPFLAGS)





# target: libcw_test_public: a test program testing only public API of libcw library;
# main() is defined in libcw_test_public.c
# Source code files used to build libcw_test_public program.
# This test tests library's public API, so the target should depend on
# a regular shared library file found in build directory. It shouldn't
# compile all *.c files that are used to create library, it should
# compile only test files and "link" them with this library file.
libcw_test_public_SOURCES = libcw_test_public.c

# target-specific preprocessor flags (#defs and include dirs)
libcw_test_public_CPPFLAGS = $(AM_CPPFLAGS)

# target-specific linker flags (objects to link)
libcw_test_public_LDADD = -lm -lpthread $(DL_LIB) libcw_check.a -L../.libs -lcw





# target: libcw_test_simple_gen, yet another test program;
# main() is defined in libcw_test_simple_gen.c
# The file defines a very simple set of tests that make only a basic
# use of libcw's generator facilities.

# source code files used to build libcw_test_simple_gen program
libcw_test_simple_gen_SOURCES = libcw_test_simple_gen.c

# target-specific linker flags (objects to link)
libcw_test_simple_gen_LDADD=-lm -lpthread $(DL_LIB) $(OSS_LIB) libcw_check.a -L../.libs -lcw

# target-specific compiler flags
libcw_test_simple_gen_CFLAGS = -rdynamic





# target: libcw_test_tq_short_space, a test program for a specific bug;

# source code files used to build the program
libcw_test_tq_short_space_SOURCES = libcw_test_tq_short_space.c

# target-specific preprocessor flags (#defs and include dirs)
libcw_test_tq_short_space_CPPFLAGS = $(AM_CPPFLAGS)

# target-specific linker flags (objects to link)
libcw_test_tq_short_space_LDADD=-lm -lpthread $(DL_LIB) $(OSS_LIB) -L../.libs -lcw

# target-specific compiler flags
libcw_test_tq_short_space_CFLAGS = -rdynamic






# CLEANFILES extends list of files that need to be removed when
# calling "make clean"
CLEANFILES = libcw_test_tq_short_space.sh
