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

# General initializations.
AC_PREREQ(2.57)
AC_INIT([UnixCW], 3.0.1, [acerion@wp.pl])

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
AC_CHECK_LIB(asound, snd_pcm_open)
AC_CHECK_LIB(pthread, pthread_create)
AC_CHECK_LIB(m, floor)

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

# Check for certain routines which are missing from liboss-salsa-dev
# (kfreebsd's libasound implementation):
AC_CHECK_FUNCS([ \
	snd_pcm_hw_params_test_rate \
	snd_pcm_hw_params_test_periods \
	snd_pcm_hw_params_test_buffer_size \
	])

# Decide on which subdirectories to build; substitute into SUBDIRS.  Build
# cwcp if curses is available, and xcwcp if Qt is available.
SUBDIRS="libcw cwutils cw cwgen"
# Simple test for curses based on prior library check.
if test $ac_cv_lib_curses_initscr = 'yes' ; then
  SUBDIRS="$SUBDIRS cwcp"
else
  AC_MSG_WARN([Cannot find libcurses - unable to build cwcp])
fi




# autodetection of QT4 dependencies

MOC_MAJOR_EARLIEST=4

# possible locations of Qt4 headers
QT4HEADER_DIRS="/usr/include/qt4 \ # Debian
               /usr/include"      # Arch (?)

# possible locations of Qt4 moc (a.k.a. moc-qt4)
QT4BIN_DIRS=$PATH # pretty standard

# QT4DIR="" # for testing purposes
QT4INC=""  # full path to directory with QT4 QtCore + QtGui subdirs and include files, e.g. /usr/include/qt4/
QT4MOC=""  # full path to QT4 moc executable

if [[[ $QT4DIR = "" ]]] ; then

    # look for QT4 include directories
    for dir in $QT4HEADER_DIRS; do
	if test -d "$dir/QtGui" && test -d "$dir/QtCore" ; then
            QT4INC=$dir;
	fi
    done

    # look for QT4 moc binary
    AC_PATH_PROG(QT4MOC, moc-qt4, , $PATH)
    if test -n "$QT4MOC" ; then
        MOC_MAJOR="`$QT4MOC -v 2>&1 | sed -e 's;.\+ (Qt \([[0-9]]\).\+;\1;g'`"
	if [[[ $MOC_MAJOR != $MOC_MAJOR_EARLIEST ]]] ; then
	   QT4MOC=""
	fi
    fi

    if [[[ "$QT4MOC" = "" ]]] ; then
        QT4MOC_2=""
        AC_PATH_PROG(QT4MOC_2, moc, , $PATH)
        if test -n "$QT4MOC_2" ; then
            MOC_MAJOR="`$QT4MOC_2 -v 2>&1 | sed -e 's;.\+ (Qt \([[0-9]]\).\+;\1;g'`"
	    if [[[ $MOC_MAJOR = $MOC_MAJOR_EARLIEST ]]] ; then
	        QT4MOC=$QT4MOC_2
	    fi
        fi
    fi


    if [[[ $QT4INC != "" && $QT4MOC != "" ]]] ; then

	# we have moc4 and path to QT4 include files,
	# so we can build xcwcp
	SUBDIRS="$SUBDIRS xcwcp"

	AC_SUBST(QT4INC)
	AC_SUBST(QT4MOC)
    else
	if [[[ $QT4INC = "" ]]] ; then
	    AC_MSG_WARN([Can't find directory with QT4 header files - unable to build xcwcp])
	fi

	if [[[ $QT4MOC = "" ]]] ; then
	    AC_MSG_WARN([Can't find QT moc version == $MOC_MAJOR_EARLIEST - unable to build xcwcp])
	fi

	AC_MSG_WARN([Hint: try setting QT4DIR env variable per instructions in unixcw/INSTALLATION])

	QT4INC=""
   	QT4MOC=""
    fi


else


# Look for $QT4DIR.  If found, then look for moc, either on $PATH, or
# in $QT4DIR/bin.

  AC_PATH_PROG(QT4MOC, moc, , $QT4DIR/bin:$PATH)
  if test -n "$QT4MOC" ; then
    MOC_VERSION="`$QT4MOC -v 2>&1 | sed -e 's;.*(Qt ;;' -e 's;).*;;'`"
    if test -n "$MOC_VERSION" ; then
      MOC_MAJOR="`echo $MOC_VERSION | sed -e 's;\..*;;'`"
      expr "$MOC_MAJOR" + 0 >/dev/null 2>/dev/null
      status=$?
      if test -n "$MOC_MAJOR" -a $status -eq 0 ; then
        if test "$MOC_MAJOR" -ge $MOC_MAJOR_EARLIEST ; then
          SUBDIRS="$SUBDIRS xcwcp"
	  QT4INC="$QT4DIR/include"
          AC_SUBST(QT4INC)
          AC_SUBST(QT4MOC)
        else
          AC_MSG_WARN([Found 'moc' $MOC_VERSION - unable to build xcwcp])
          AC_MSG_WARN([Hint: xcwcp requires 'moc' version >= $MOC_MAJOR_EARLIEST])
        fi
      else
        AC_MSG_WARN([Cannot find 'moc' version - unable to build xcwcp])
        AC_MSG_WARN([Hint: ensure 'moc' is in QT4DIR/bin on your PATH])
      fi
    else
      AC_MSG_WARN([Cannot find 'moc' version - unable to build xcwcp])
      AC_MSG_WARN([Hint: ensure 'moc' is in QT4DIR/bin on your PATH])
    fi
  else
    AC_MSG_WARN([Cannot find 'moc' - unable to build xcwcp])
    AC_MSG_WARN([Hint: ensure 'moc' is in QT4DIR/bin on your PATH])
  fi
fi
AC_SUBST(SUBDIRS)

if [[[ $QT4INC != "" && $QT4MOC != "" ]]] ; then
   echo "QT4 headers dir is $QT4INC"
   echo "QT4 moc is $QT4MOC"
fi



# Add -Wall, -W, -pedantic, and other paranoia to gcc command flags, and -Wall
# to with a few suppressions to icc command flags, then similarly, though less
# so, for g++ and icpc.
if test "`basename $CC`" = "gcc" ; then
  CFLAGS="$CFLAGS -Wall -W -pedantic -Wfloat-equal -Wpointer-arith -std=gnu99"
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
