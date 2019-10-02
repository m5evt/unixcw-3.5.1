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

# List of files that implement tests of specific bug fixes.
LIBCW_BUG_TEST_FILES = \
	libcw_test_tq_short_space.c

test_framework_files = \
	test_framework.c \
	test_framework.h

legacy_api_test_files = \
	libcw_legacy_api_tests.c \
	libcw_legacy_api_tests.h \
	libcw_legacy_api_sets.c \
	libcw_legacy_api_sets.inc

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
	$(LIBCW_BUG_TEST_FILES)

EXTRA_DIST = $(test_framework_files) $(legacy_api_test_files) $(modern_api_test_files) $(other_test_files)





# target: libcw_test_legacy_api: a test program testing only public
# API of libcw library
# This test tests library's public API, so the target should depend on
# a regular shared library file found in build directory. It shouldn't
# compile all *.c files that are used to create library, it should
# compile only test files and "link" them with this library file.
libcw_test_legacy_api_SOURCES = \
	libcw_legacy_api_tests.c \
	libcw_legacy_api_sets.c \
	$(LIBCW_BUG_TEST_FILES) \
	test_framework.c \
	test_main.c

# target-specific preprocessor flags (#defs and include dirs)
libcw_test_legacy_api_CPPFLAGS = $(AM_CPPFLAGS) -DLIBCW_UNIT_TESTS

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
	test_framework.c \
	libcw_utils_tests.c \
	libcw_data_tests.c \
	libcw_debug_tests.c \
	libcw_tq_tests.c \
	libcw_gen_tests.c \
	libcw_key_tests.c \
	libcw_rec_tests.c \
	libcw_legacy_api_tests.c \
	$(LIBCW_BUG_TEST_FILES)

libcw_test_all_CPPFLAGS = $(AM_CPPFLAGS) -DLIBCW_UNIT_TESTS

libcw_test_all_LDADD = -lm -lpthread $(DL_LIB) -L../.libs -lcw




# CLEANFILES extends list of files that need to be removed when
# calling "make clean"
CLEANFILES = libcw_test_tq_short_space.sh
