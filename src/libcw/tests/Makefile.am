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
check_PROGRAMS = libcw_test_legacy_api libcw_test_all
#	libcw_test_simple_gen
#	libcw_test_tq_short_space



test_infrastructure_files = \
	libcw_test_framework.c \
	libcw_test_framework.h \
	libcw_test_driver.c

legacy_api_test_files = \
	libcw_test_legacy_api_tests.c \
	libcw_test_legacy_api_tests.h \
	libcw_test_legacy_api_test_sets.c

modern_api_test_files = \
	libcw_data_tests.c \
	libcw_data_tests.h \
	libcw_gen_tests.c \
	libcw_gen_tests.h \
	libcw_rec_tests.c \
	libcw_rec_tests.h \
	libcw_utils_tests.c \
	libcw_utils_tests.h \
	libcw_key_tests.c \
	libcw_key_tests.h \
	libcw_debug_tests.c \
	libcw_debug_tests.h \
	libcw_tq_tests.c \
	libcw_tq_tests.h

other_test_files = \
	libcw_test_simple_gen.c \
	libcw_test_tq_short_space.c

EXTRA_DIST = $(test_infrastructure_files) $(legacy_api_test_files) $(modern_api_test_files) $(other_test_files)





# library built only for purposes of testing (checking)
#check_LIBRARIES = libcw_check.a
#libcw_check_a_SOURCES = libcw_test.c
#libcw_check_a_CPPFLAGS = $(AM_CPPFLAGS)



# target: libcw_test_legacy_api: a test program testing only public API of libcw library;
# main() is defined in libcw_test_legacy_api.c
# Source code files used to build libcw_test_legacy_api program.
# This test tests library's public API, so the target should depend on
# a regular shared library file found in build directory. It shouldn't
# compile all *.c files that are used to create library, it should
# compile only test files and "link" them with this library file.
libcw_test_legacy_api_SOURCES = \
	libcw_test_legacy_api_tests.c \
	libcw_test_legacy_api_test_sets.c \
	libcw_test_framework.c \
	test_main.c

# target-specific preprocessor flags (#defs and include dirs)
libcw_test_legacy_api_CPPFLAGS = $(AM_CPPFLAGS)

# Target-specific linker flags (objects to link).
#
# We are testing public and original (legacy) API of libcw, so we have
# to have access only to "regular" libcw library with only public
# symbols (functions) provided by the library. Therefore link the test
# executable with the library through "-lcw".
libcw_test_legacy_api_LDADD = -lm -lpthread $(DL_LIB) -L../.libs -lcw




libcw_test_all_SOURCES = \
	test_main.c \
	test_all_sets.c \
	libcw_test_framework.c \
	libcw_utils_tests.c \
	libcw_data_tests.c \
	libcw_debug_tests.c \
	libcw_tq_tests.c \
	libcw_gen_tests.c \
	libcw_key_tests.c \
	libcw_rec_tests.c



libcw_test_all_CPPFLAGS = $(AM_CPPFLAGS)

libcw_test_all_LDADD = -lm -lpthread $(DL_LIB) -L../.libs -lcw




# target: libcw_test_simple_gen, yet another test program;
# main() is defined in libcw_test_simple_gen.c
# The file defines a very simple set of tests that make only a basic
# use of libcw's generator facilities.

# source code files used to build libcw_test_simple_gen program
#libcw_test_simple_gen_SOURCES = libcw_test_simple_gen.c

# target-specific linker flags (objects to link)
#libcw_test_simple_gen_LDADD=-lm -lpthread $(DL_LIB) $(OSS_LIB) libcw_check.a -L../.libs -lcw

# target-specific compiler flags
#libcw_test_simple_gen_CFLAGS = -rdynamic



# target: libcw_test_internal, a test program invoking unit tests for
# (internal) functions defined in libcw_xyz.c files;
# main() is defined in libcw_test_internal.c.

# For now I can't move this test to tests/ because the target refers
# to $(LIBCW_BASE_C_FILES) files residing in this directory, and not
# in tests/ directory. I could use
# "AM_INIT_AUTOMAKE([subdir-objects])" in configure.ac, but I don't
# know how portable it is.

# source code files used to build libcw_test_internal program
# libcw_test_internal_SOURCES = $(LIBCW_BASE_C_FILES) \
	libcw_utils_tests.c \
	libcw_debug_tests.c \
	libcw_data_tests.c \
	libcw_tq_tests.c \
	libcw_gen_tests.c \
	libcw_rec_tests.c \
	libcw_key_tests.c

# target-specific preprocessor flags (#defs and include dirs)
# libcw_test_internal_CPPFLAGS = $(AM_CPPFLAGS) -DLIBCW_UNIT_TESTS

# target-specific linker flags (objects to link)
# libcw_test_internal_LDADD=-lm -lpthread $(DL_LIB) $(OSS_LIB)

# target-specific compiler flags
# libcw_test_internal_CFLAGS = -rdynamic




#libcw_test_legacy_SOURCES = libcw_legacy_tests.c \
#	libcw_legacy_tests_sets.c \
#	tests/libcw_test_framework.c \
#	tests/libcw_test_driver.c
#
#libcw_test_legacy_CPPFLAGS = $(AM_CPPFLAGS)
#
## target-specific linker flags (objects to link)
#libcw_test_legacy_LDADD=-lm -lpthread $(DL_LIB) $(OSS_LIB) -L./.libs -lcw
#
## target-specific compiler flags
#libcw_test_legacy_CFLAGS = -rdynamic







# target: libcw_test_tq_short_space, a test program for a specific bug;

# source code files used to build the program
#libcw_test_tq_short_space_SOURCES = libcw_test_tq_short_space.c

# target-specific preprocessor flags (#defs and include dirs)
#libcw_test_tq_short_space_CPPFLAGS = $(AM_CPPFLAGS)

# target-specific linker flags (objects to link)
#libcw_test_tq_short_space_LDADD=-lm -lpthread $(DL_LIB) $(OSS_LIB) -L../.libs -lcw

# target-specific compiler flags
#libcw_test_tq_short_space_CFLAGS = -rdynamic






# CLEANFILES extends list of files that need to be removed when
# calling "make clean"
CLEANFILES = libcw_test_tq_short_space.sh
