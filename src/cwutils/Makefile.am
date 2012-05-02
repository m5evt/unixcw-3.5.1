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

# no header from this dir should be installed
# noinst_HEADERS = cmdline.h copyright.h cw_common.h cwwords.h dictionary.h i18n.h memory.h

# convenience libraries
noinst_LIBRARIES = lib_cw.a lib_cwcp.a lib_cwgen.a lib_xcwcp.a

lib_cw_a_SOURCES    = copyright.h i18n.c i18n.h cw_common.c cw_common.h cmdline.c cmdline.h memory.c memory.h
lib_cwcp_a_SOURCES  = copyright.h i18n.c i18n.h cw_common.c cw_common.h cmdline.c cmdline.h memory.c memory.h dictionary.c dictionary.h cwwords.h
lib_cwgen_a_SOURCES = copyright.h i18n.c i18n.h                         cmdline.c cmdline.h memory.c memory.h
lib_xcwcp_a_SOURCES = copyright.h i18n.c i18n.h cw_common.c cw_common.h cmdline.c cmdline.h memory.c memory.h dictionary.c dictionary.h cwwords.h


# Test targets; no self-test, but make sure all is built.
check: all
