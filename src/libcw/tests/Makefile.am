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

-include $(top_builddir)/Makefile.inc

# targets to be built in this directory
check_PROGRAMS=libcw_test_tq_short_space


EXTRA_DIST=libcw_test_tq_short_space.c





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
