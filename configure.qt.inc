# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
# Copyright (C) 2011-2013  Kamil Ignacak (acerion@wp.pl)
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




# Script that detects some paths/files necessary to build
# Qt4 application using autotools.
# On success, the script sets three variables for build system by making
# these two calls:
#    AC_SUBST(QT4INC, $QT4_CFLAGS)
#    AC_SUBST(QT4LIBS)
#    AC_SUBST(QT4MOC)

# You can include this file to your configure.ac file like this:

# # detection of Qt4 moc and Qt4 include dir
# m4_include([configure.qt.inc])



MOC_MAJOR_EARLIEST=4



# There are four major tasks performed by this script:
# Task 1: find Qt4 include directory, and for QT4 library
# Task 2: find Qt4 moc
# Task 3: make sure that major version of the moc is >= MOC_MAJOR_EARLIEST
# Task 4: on success set up some build system variables (AC_SUBST)





QT4INC=""  # full path to directory with QT4 QtCore + QtGui subdirs and include files, e.g. /usr/include/qt4/
QT4MOC=""  # full path to Qt4 moc
# QT4DIR="" # for testing purposes

AC_ARG_WITH([qt-includes],
AC_HELP_STRING([--with-qt-includes=<path>],
    [path to qt4 include files]),
    [QT4INC=$with_qt_includes])

AC_ARG_WITH([qt-libraries],
AC_HELP_STRING([--with-qt-libraries=<path>],
    [path to qt4 libraries]),
    [QT4LIBS=$with_qt_libraries])


# Task 1: find Qt4 include directory, and for QT4 library

PKG_CHECK_MODULES([QT4], [ "QtCore QtGui"],
			 [ AC_SUBST(QT4INC, $QT4_CFLAGS) AC_SUBST(QT4LIBS) ],
			 [ AC_MSG_ERROR([Qt4 libs not found])
			 ])




# Task 2: find Qt4 moc
QT4MOC=""
QT4MOC_PATH=""
QT4MOC_PATH=${MOC}
if [[ $MOC ]] ; then
  QT4MOC_PATH=${MOC}
  ac_cv_path_QT4MOC_PATH=$MOC
  echo ac_cv_path_QT4MOC_PATH $ac_cv_path_QT4MOC_PATH
elif [[ -z $QT4DIR ]] ; then

    # we should guess where Qt4 moc binary is;
    # first we see if we are lucky and Qt4 moc installed on build machine
    # is called moc-qt4;
    # on my system /usr/bin/moc links to /etc/alternatives/moc, which links
    # to /usr/binary/moc-qt4 - the real binary
    AC_PATH_PROG(QT4MOC_PATH, moc-qt4, , $PATH)
    if [[[ "$QT4MOC_PATH" = "" ]]] ; then
	# there is no moc-qt4, so let's try to find "regular" moc;
	# we will check its version below
	AC_PATH_PROG(QT4MOC_PATH, moc, , $PATH)
    fi
else
    # There is a decent build environment on this build machine, it
    # defines/provides a $QT4DIR environment variable. Use it to find
    # Qt4 moc, and to locate Qt4 include dir (which should be trivial).

    # Look for moc, either on $PATH (?), or in $QT4DIR/bin.

    AC_PATH_PROG(QT4MOC_PATH, moc, , $QT4DIR/bin:$PATH)

fi



# Task 3: make sure that major version of the moc is >= MOC_MAJOR_EARLIEST
if [[ "$QT4MOC_PATH" ]] ; then
    MOC_VERSION="`$QT4MOC_PATH -v 2>&1 | sed -e 's;.*(Qt ;;' -e 's;).*;;'`"
    if [[ "$MOC_VERSION" ]] ; then
	MOC_MAJOR="`echo $MOC_VERSION | sed -e 's;\..*;;'`"
	expr "$MOC_MAJOR" + 0 >/dev/null 2>/dev/null
	status=$?
	if [[ "$MOC_MAJOR" -a $status = "0" ]] ; then
	    if [[ "$MOC_MAJOR" -lt "$MOC_MAJOR_EARLIEST" ]] ; then
		AC_MSG_WARN("Found moc $MOC_VERSION - unable to build xcwcp")
		AC_MSG_WARN(["xcwcp requires moc version >= $MOC_MAJOR_EARLIEST"])
	    else
		QT4MOC=$QT4MOC_PATH

		AC_SUBST(QT4MOC)
	    fi
	else
	    AC_MSG_WARN(["Can't get moc major version - unable to build xcwcp"])
	fi
    else
	AC_MSG_WARN(["Can't get moc version - unable to build xcwcp"])
    fi
else
    AC_MSG_WARN(["Can't find moc - unable to build xcwcp"])
fi





# Task 4: on success set up some build system variables (AC_SUBST)
if [[ "$QT4INC" -a "$QT4MOC" ]] ; then
    echo "Qt4 headers dir is $QT4INC"
    echo "Qt4 moc is $QT4MOC"

else
    if [[ -z "$QT4INC" ]] ; then
	AC_MSG_WARN(["Can't find directory with Qt4 header files - unable to build xcwcp"])
    fi

    if [[ -z "$QT4MOC" ]] ; then
	AC_MSG_WARN(["Can't find Qt moc version >= $MOC_MAJOR_EARLIEST - unable to build xcwcp"])
	AC_MSG_WARN(["Hint: to build xcwcp make sure that moc is in QT4DIR/bin on your PATH"])
    fi

    AC_MSG_WARN(["Hint: try setting QT4DIR env variable per instructions in unixcw/INSTALLING"])

    QT4INC=""
    QT4MOC=""
fi


