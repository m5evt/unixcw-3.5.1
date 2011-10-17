# vi: set ts=2 shiftwidth=2 expandtab:
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

# General initializations.
AC_PREREQ(2.57)
AC_INIT([UnixCW], 2.3, [simon_baldwin@yahoo.com])

# Specify a configuration header.
AC_CONFIG_HEADERS(src/config.h)

# General checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_LN_S

# Check specifically for gzip and ldconfig, and substitute the harmless ":"
# if absent.
AC_PATH_PROG(GZIP, gzip, ,)
if test -z "$GZIP" ; then
  GZIP=":"
fi
AC_PATH_PROG(LDCONFIG, ldconfig, ,)
if test -z "$LDCONFIG" ; then
  LDCONFIG=":"
fi
AC_SUBST(GZIP)
AC_SUBST(LDCONFIG)

# Checks for libraries.
AC_CHECK_LIB(curses, initscr)

# Checks for header files, and refuse to go on if no KIOCSOUND is available.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h stdlib.h string.h strings.h sys/ioctl.h \
                  sys/param.h sys/time.h unistd.h locale.h libintl.h])

AC_CHECK_HEADERS([sys/kd.h sys/vtkd.h sys/kbio.h])
if test "$ac_cv_header_sys_kd_h" = 'no' \
    && test "$ac_cv_header_sys_vtkd_h" = 'no' \
    && test "$ac_cv_header_sys_kbio_h" = 'no' ; then
  AC_MSG_ERROR([Cannot find either sys/kd.h, sys/vtkd.h, or sys/kbio.h])
fi
AC_CHECK_HEADERS([soundcard.h sys/soundcard.h])
if test "$ac_cv_header_soundcard_h" = 'no' \
    && test "$ac_cv_header_sys_soundcard_h" = 'no' ; then
  AC_MSG_ERROR([Cannot find either soundcard.h or sys/soundcard.h])
fi
AC_CHECK_HEADERS([getopt.h])
AC_CHECK_HEADERS([string.h strings.h])
if test "$ac_cv_header_string_h" = 'no' \
    && test "$ac_cv_header_strings_h" = 'no' ; then
  AC_MSG_WARN([Cannot find either string.h or strings.h])
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_C_VOLATILE

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRCOLL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([floor gettimeofday memset sqrt strchr strdup strrchr \
                strtoul getopt_long setlocale memmove select strerror strspn])
AC_FUNC_SELECT_ARGTYPES

# Decide on which subdirectories to build; substitute into SUBDIRS.  Build
# cwcp if curses is available, and xcwcp if Qt is available.
SUBDIRS="cwlib cwutils cw cwgen"
# Simple test for curses based on prior library check.
if test $ac_cv_lib_curses_initscr = 'yes' ; then
  SUBDIRS="$SUBDIRS cwcp"
else
  AC_MSG_WARN([Cannot find libcurses - unable to build cwcp])
fi


#
# test area
#

# possible locations of Qt4 headers
QT4HEADER_DIRS="/usr/include/qt4 \ # Debian
               /usr/include/"      # Arch (?)

QT4DIR=""
# QTDIR=""
if [[[ $QTDIR != "" ]]] ; then
    for dir in $QT4HEADER_DIRS; do
	if test -d "$dir/QtGui" && test -d "$dir/QtCore" ; then
            QT4DIR=$dir;
	fi
    done
fi

if [[[ $QT4DIR != "" ]]] ; then
    echo "QT4 headers dir is $QT4DIR"
fi



# Look for $QTDIR.  If found, then look for moc, either on $PATH, or
# in $QTDIR/bin.
MOC_EARLIEST=4
if test -n "$QTDIR" ; then
  AC_PATH_PROG(QTMOC, moc, , $QTDIR/bin:$PATH)
  if test -n "$QTMOC" ; then
    MOC_VERSION="`$QTMOC -v 2>&1 | sed -e 's;.*(Qt ;;' -e 's;).*;;'`"
    if test -n "$MOC_VERSION" ; then
      MOC_MAJOR="`echo $MOC_VERSION | sed -e 's;\..*;;'`"
      expr "$MOC_MAJOR" + 0 >/dev/null 2>/dev/null
      status=$?
      if test -n "$MOC_MAJOR" -a $status -eq 0 ; then
        if test "$MOC_MAJOR" -ge $MOC_EARLIEST ; then
          SUBDIRS="$SUBDIRS xcwcp"
          AC_SUBST(QTDIR)
          AC_SUBST(QTMOC)
        else
          AC_MSG_WARN([Found 'moc' $MOC_VERSION - unable to build xcwcp])
          AC_MSG_WARN([Hint: xcwcp requires 'moc' version >= $MOC_EARLIEST])
        fi
      else
        AC_MSG_WARN([Cannot find 'moc' version - unable to build xcwcp])
        AC_MSG_WARN([Hint: ensure 'moc' is in QTDIR/bin on your PATH])
      fi
    else
      AC_MSG_WARN([Cannot find 'moc' version - unable to build xcwcp])
      AC_MSG_WARN([Hint: ensure 'moc' is in QTDIR/bin on your PATH])
    fi
  else
    AC_MSG_WARN([Cannot find 'moc' - unable to build xcwcp])
    AC_MSG_WARN([Hint: ensure 'moc' is in QTDIR/bin on your PATH])
  fi
else
  AC_MSG_WARN([Cannot find libqt - unable to build xcwcp])
  AC_MSG_WARN([Hint: try setting a value for the QTDIR variable])
fi
AC_SUBST(SUBDIRS)

# Add -Wall, -W, -pedantic, and other paranoia to gcc command flags, and -Wall
# to with a few suppressions to icc command flags, then similarly, though less
# so, for g++ and icpc.
if test "`basename $CC`" = "gcc" ; then
  CFLAGS="$CFLAGS -Wall -W -pedantic -Wfloat-equal -Wpointer-arith"
  CFLAGS="$CFLAGS -Wcast-qual -Wwrite-strings -Wmissing-prototypes"
  CFLAGS="$CFLAGS -Wstrict-prototypes -Wmissing-declarations -Wredundant-decls"
else
  if test "`basename $CC`" = "icc" ; then
    CFLAGS="$CFLAGS -Wall -wd181,188,279,383,593,810,981,1469"
  fi
fi
if test "`basename $CXX`" = "g++" ; then
  CXXFLAGS="$CXXFLAGS -Wall -W -pedantic -Wno-long-long"
else
  if test "`basename $CXX`" = "icpc" ; then
    CXXFLAGS="$CXXFLAGS -Wall -wd181,188,279,383,593,810,981,1469"
  fi
fi

# Determine if -fPIC or -KPIC is available for building .so libraries.
# Because gcc complains about invalid flags, but then continues, we have to
# check by searching the compile stdout and stderr for any output.
if test -z "$CFLAG_PIC" ; then
  AC_MSG_CHECKING([for -KPIC or -fPIC compiler options])
  cat >conftest.c <<-EOF
  int so_test() { return 0; }
EOF
  if $CC -fPIC -c conftest.c 2>&1 | egrep -q '.' ; then
    if $CC -KPIC -c conftest.c 2>&1 | egrep -q '.' ; then
      CFLAG_PIC=""
    else
      CFLAG_PIC="-KPIC"
    fi
  else
    CFLAG_PIC="-fPIC"
  fi
  rm -f conftest.c conftest.o
  if test -n "$CFLAG_PIC" ; then
    AC_MSG_RESULT($CFLAG_PIC)
  else
    AC_MSG_RESULT(no)
  fi
fi
AC_SUBST(CFLAG_PIC)

# Determine if the C compiler builds DSO files, by testing with -shared.
CC_LINKS_SO="no"
if test -n "$CC" ; then
  AC_MSG_CHECKING(whether cc builds .so files with -shared)
  cat >conftest.c <<-EOF
  int so_test() { return 0; }
EOF
  $CC -c conftest.c >/dev/null 2>/dev/null
  $CC -shared -o conftest.so conftest.o >/dev/null 2>/dev/null
  rm -f conftest.c conftest.o
  if test -f conftest.so ; then
    nm conftest.so | grep -q so_test
    if test $? -eq 0 ; then
      CC_LINKS_SO="yes"
    fi
  fi
  rm -f conftest.so
  if test $CC_LINKS_SO = "yes" ; then
    AC_MSG_RESULT(yes)
  else
    AC_MSG_RESULT(no)
  fi
fi
AC_SUBST(CC_LINKS_SO)

# A linker might also build DSO files with -G, but we'll prefer to use the
# compiler if it will cooperate.
LD_LINKS_SO="no"
AC_PATH_PROG(LD, ld, , )
if test -n "$LD" ; then
  AC_MSG_CHECKING(whether ld builds .so files with -G)
  cat >conftest.c <<-EOF
  int so_test() { return 0; }
EOF
  $CC -c conftest.c >/dev/null 2>/dev/null
  $LD -G -o conftest.so conftest.o >/dev/null 2>/dev/null
  rm -f conftest.c conftest.o
  if test -f conftest.so ; then
    nm conftest.so | grep -q so_test
    if test $? -eq 0 ; then
      LD_LINKS_SO="yes"
    fi
  fi
  rm -f conftest.so
  if test $LD_LINKS_SO = "yes" ; then
    AC_MSG_RESULT(yes)
  else
    AC_MSG_RESULT(no)
  fi
else
  LD=":"
fi
AC_SUBST(LD)
AC_SUBST(LD_LINKS_SO)

# Write out configuration information to output files.
AC_CONFIG_FILES([Makefile.inc])
AC_OUTPUT
