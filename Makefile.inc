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

# Autoconfigure definitions.
AC_CC		= x86_64-linux-gnu-gcc
AC_CFLAGS	= -Wall -g -std=gnu99 -O2
AC_CXX		= x86_64-linux-gnu-g++
AC_CXXFLAGS	= -g -O2
AC_DEFS		= -DHAVE_CONFIG_H
AC_GZIP		= /bin/gzip
AC_LDCONFIG	= /sbin/ldconfig
AC_LIBS		= -lcurses 
AC_AWK		= gawk
AC_RANLIB	= ranlib
# AC_QTDIR	= @QTDIR@
# AC_QTMOC	= @QTMOC@
AC_QT4INC	= /usr/share/qt4/include
AC_QT4MOC	= /usr/share/qt4/bin/moc
AC_CFLAG_PIC	= -fPIC
AC_SUBDIRS	= libcw cwutils cw cwgen cwcp xcwcp
AC_CC_LINKS_SO	= yes
AC_LD_LINKS_SO	= yes
AC_LD		= /usr/bin/ld
AC_LN_S		= ln -s

# Portability values.
INSTALL		= /usr/bin/install -c
INSTALL_PROGRAM	= ${INSTALL}
INSTALL_DATA	= ${INSTALL} -m 644
prefix		= /usr
exec_prefix	= ${prefix}
includedir	= ${prefix}/include
bindir		= ${exec_prefix}/bin
mandir		= ${prefix}/share/man
libdir		= ${exec_prefix}/lib

# http://www.gnu.org/s/hello/manual/autoconf/Changed-Directory-Variables.html
datarootdir = ${prefix}/share
