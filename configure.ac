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
AC_INIT(unixcw, 3.0.1, [acerion@wp.pl])

AM_INIT_AUTOMAKE(unixcw, 3.0.1)

# Specify a configuration header.
AC_CONFIG_HEADERS(src/config.h)

AC_CONFIG_FILES([src/libcw/libcw.pc])

# General checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_LIBTOOL
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_LN_S
AM_PROG_CC_C_O

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




# Check for basic but essential libraries
AC_CHECK_LIB(pthread, pthread_create)
AC_CHECK_LIB(m, floor)

# show-stoppers
if test $ac_cv_lib_m_floor = 'no' ; then
   AC_MSG_ERROR([math library is missing, cannot continue])
fi
if test $ac_cv_lib_pthread_pthread_create = 'no' ; then
   AC_MSG_ERROR([pthread library is missing, cannot continue])
fi




# ########################################################################
# definitions of custom command line options for configure script
# ########################################################################

# disable_console - 'no' by default
AC_ARG_ENABLE(console,
    AS_HELP_STRING([--disable-console], [disable support for console buzzer audio output]),
    [disable_console=yes],
    [disable_console=no])
AC_MSG_CHECKING([whether to include console buzzer audio suport])
if test "$disable_console" = "no" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi

# disable_oss - 'no' by default
AC_ARG_ENABLE(oss,
    AS_HELP_STRING([--disable-oss], [disable support for OSS audio output]),
    [disable_oss=yes],
    [disable_oss=no])
AC_MSG_CHECKING([whether to include OSS audio support])
if test "$disable_oss" = "no" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi

# disable_alsa - 'no' by default
AC_ARG_ENABLE(alsa,
    AS_HELP_STRING([--disable-alsa], [disable support for ALSA audio output]),
    [disable_alsa=yes],
    [disable_alsa=no])
AC_MSG_CHECKING([whether to include ALSA audio support])
if test "$disable_alsa" = "no" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi

# disable_cwcp - 'no' by default
AC_ARG_ENABLE(cwcp,
    AS_HELP_STRING([--disable-cwcp], [do not build cwcp (application with curses user interface)]),
    [disable_cwcp=yes],
    [disable_cwcp=no])
AC_MSG_CHECKING([whether to build cwcp])
if test "$disable_cwcp" = "no" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi

# disable_xcwcp - 'no' by default
AC_ARG_ENABLE(xcwcp,
    AS_HELP_STRING([--disable-xcwcp], [do not build xcwcp (application with Qt4 user interface)]),
    [disable_xcwcp=yes],
    [disable_xcwcp=no])
AC_MSG_CHECKING([whether to build xcwcp])
if test "$disable_xcwcp" = "no" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi

# enable_dev - 'no' by default
AC_ARG_ENABLE(dev,
    AS_HELP_STRING([--enable-dev], [enable development support (messages/debug code/asserts)]),
    [enable_dev=yes],
    [enable_dev=no])
AC_MSG_CHECKING([whether to enable development support])
if test "$enable_dev" = "yes" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi

# #####
#  end
# #####





# ########################################################################
# Process command line options, and if enabled - checking if requirements
# for enabled features are satisfied.
# ########################################################################


if test "$disable_console" = "yes" ; then
    WITH_CONSOLE='no'
else
    AC_CHECK_HEADERS([sys/kd.h sys/vtkd.h sys/kbio.h])
    if test "$ac_cv_header_sys_kd_h" = 'no' \
	&& test "$ac_cv_header_sys_vtkd_h" = 'no' \
	&& test "$ac_cv_header_sys_kbio_h" = 'no' ; then

	WITH_CONSOLE='no'
	AC_MSG_WARN([Cannot find either sys/kd.h, sys/vtkd.h, or sys/kbio.h - support for console buzzer audio output will be disabled])
    else
	WITH_CONSOLE='yes'
    fi
fi

if test "$WITH_CONSOLE" = 'yes' ; then
    AC_DEFINE([LIBCW_WITH_CONSOLE], [1], [Define as 1 if your build machine can support console buzzer.])
fi



if test "$disable_oss" = "yes" ; then
    WITH_OSS='no'
else
    AC_CHECK_HEADERS([soundcard.h sys/soundcard.h])
    if test "$ac_cv_header_soundcard_h" = 'no' \
	&& test "$ac_cv_header_sys_soundcard_h" = 'no' ; then

	WITH_OSS='no'
        AC_MSG_WARN([Cannot find either soundcard.h or sys/soundcard.h - support for OSS audio output will be disabled])
    else
	WITH_OSS='yes'
    fi
fi

if test "$WITH_OSS" = 'yes' ; then
    AC_DEFINE([LIBCW_WITH_OSS], [1], [Define as 1 if your build machine can support OSS.])
fi



if test "$disable_alsa" = "yes" ; then
    WITH_ALSA='no'
else
    AC_CHECK_LIB(asound, snd_pcm_open)
    if test "$ac_cv_lib_asound_snd_pcm_open" = 'yes' ; then

	# Check for certain routines which are missing from liboss-salsa-dev
        # (kfreebsd's libasound implementation). If a function is found
	# build system defines HAVE_function_name in config.h.
	AC_CHECK_FUNCS([ \
	    snd_pcm_hw_params_test_periods \
	    snd_pcm_hw_params_test_buffer_size \
	    ])

	WITH_ALSA='yes'
    else
	WITH_ALSA='no'
	AC_MSG_WARN([Cannot find ALSA library files - support for ALSA audio output will be disabled])
    fi
fi

if test "$WITH_ALSA" = 'yes' ; then
    AC_DEFINE([LIBCW_WITH_ALSA], [1], [Define as 1 if your build machine can support ALSA.])
    # to be appended to LIBS in src/libcw/Makefile.am
    ALSA_LIBS=" -lasound "
else
    # to be appended to LIBS in src/libcw/Makefile.am
    ALSA_LIBS=""
fi

AC_SUBST(ALSA_LIBS)



if test "$disable_cwcp" = "yes" ; then
   WITH_CWCP='no'
else
    AC_CHECK_LIB(curses, initscr)
    if test $ac_cv_lib_curses_initscr = 'yes' ; then
	WITH_CWCP='yes'
    else
	WITH_CWCP='no'
	AC_MSG_WARN([Cannot find libcurses - unable to build cwcp])
    fi
fi



if test "$disable_xcwcp" = "yes" ; then
    WITH_XCWCP='no'
else
    # autodetection of Qt4 dependencies
    m4_include([configure.qt.inc])

    if [[[ $QT4INC != "" && $QT4MOC != "" ]]] ; then
        # we have moc4 and path to Qt4 include files,
        # so we can build xcwcp
	WITH_XCWCP='yes'
    else
	WITH_XCWCP='no'
	AC_MSG_WARN([Cannot find Qt4 files - unable to build xcwcp])
    fi
fi



if test "$enable_dev" = "yes" ; then
    AC_DEFINE([LIBCW_WITH_DEV], [1], [Define as 1 if you want to enable development support.])
fi


# #####
#  end
# #####



# unixcw uses per-target linker object lists (target_LDADD)
LIBS=



# Checks for header files, and refuse to go on if no KIOCSOUND is available.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h stdlib.h string.h strings.h sys/ioctl.h \
                  sys/param.h sys/time.h unistd.h locale.h libintl.h])



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




# Decide on which subdirectories to build; substitute into SRC_SUBDIRS.
# Build cwcp if curses is available, and xcwcp if Qt is available.
SRC_SUBDIRS="libcw cwutils cw cwgen"

if test "$WITH_CWCP" = 'yes' ; then
    SRC_SUBDIRS="$SRC_SUBDIRS cwcp"
fi

if test "$WITH_XCWCP" = 'yes' ; then
    SRC_SUBDIRS="$SRC_SUBDIRS xcwcp"
fi

AC_SUBST(SRC_SUBDIRS)



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




AC_CONFIG_FILES([Makefile.inc
	Makefile
	src/Makefile
	src/libcw/Makefile
	src/cwutils/Makefile
	src/cw/Makefile
	src/cwgen/Makefile])

if test "$WITH_CWCP" = 'yes' ; then
   AC_CONFIG_FILES([src/cwcp/Makefile])
fi
if test "$WITH_XCWCP" = 'yes' ; then
   AC_CONFIG_FILES([src/xcwcp/Makefile])
fi



AC_MSG_NOTICE([------------------------------------])
AC_MSG_NOTICE([---- unixcw build configuration ----])
AC_MSG_NOTICE([build libcw:  .................  yes])
AC_MSG_NOTICE([build cw & cwgen:  ............  yes])
AC_MSG_NOTICE([build cwcp:  ..................  $WITH_CWCP])
AC_MSG_NOTICE([build xcwcp:  .................  $WITH_XCWCP])
AC_MSG_NOTICE([include console buzzer support:  $WITH_CONSOLE])
AC_MSG_NOTICE([include OSS support:  .........  $WITH_OSS])
AC_MSG_NOTICE([include ALSA support:  ........  $WITH_ALSA])
AC_MSG_NOTICE([------------------------------------])



AC_OUTPUT
