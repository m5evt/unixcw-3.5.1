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
# On success, the script sets two variables for build system by making
# these two calls:
#    AC_SUBST(QT4INC)
#    AC_SUBST(QT4MOC)
# so that these two Autoconfigure definitions will be available:
#    AC_QT4INC	= @QT4INC@
#    AC_QT4MOC	= @QT4MOC@
# How these are used / can be used is outside of scope of this text.

# On some occasions the script needs to guess location of Qt4 include dir.
# It uses list of well-known possible locations of Qt4 headers: QT4HEADER_DIRS.
# If the script has problems on your system, try to add the include path
# that is valid on your system.

# You can include this file to your configure.ac file like this:

# # detection of Qt4 moc and Qt4 include dir
# m4_include([configure.qt.inc])



# possible locations of Qt4 headers - you may have to extend this list
QT4HEADER_DIRS="/usr/include/qt4 \ # Debian
               /usr/include"       # Arch (?)


MOC_MAJOR_EARLIEST=4



# There are four major tasks performed by this script:
# Task 1: find Qt4 include directory
# Task 2: find Qt4 moc
# Task 3: make sure that major version of the moc is >= MOC_MAJOR_EARLIEST
# Task 4: on success set up some build system variables (AC_SUBST)





QT4INC=""  # full path to directory with QT4 QtCore + QtGui subdirs and include files, e.g. /usr/include/qt4/
QT4MOC=""  # full path to Qt4 moc
# QT4DIR="" # for testing purposes





# Task 1: find Qt4 include directory
if [[[ $QT4DIR = "" ]]] ; then

    # build environment doesn't define $QT4DIR, we have to guess where
    # Qt4 include files are located; there are only few possible locations,
    # stored by this script in $QTHEADERS_DIRS
    for dir in $QT4HEADER_DIRS; do
	if test -d "$dir/QtGui" && test -d "$dir/QtCore" ; then
            QT4INC=$dir;
	fi
    done

else
    QT4INC="$QT4DIR/include" # trivial
fi





# Task 2: find Qt4 moc
QT4MOC=""
QT4MOC_PATH=""

if [[[ $QT4DIR = "" ]]] ; then

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
if [[[ "$QT4MOC_PATH" != "" ]]] ; then
    MOC_VERSION="`$QT4MOC_PATH -v 2>&1 | sed -e 's;.*(Qt ;;' -e 's;).*;;'`"
    if [[[ "$MOC_VERSION" != "" ]]] ; then
	MOC_MAJOR="`echo $MOC_VERSION | sed -e 's;\..*;;'`"
	expr "$MOC_MAJOR" + 0 >/dev/null 2>/dev/null
	status=$?
	if [[[ "$MOC_MAJOR" != "" && $status = 0 ]]] ; then
	    if [[[ "$MOC_MAJOR" < "$MOC_MAJOR_EARLIEST" ]]] ; then
		AC_MSG_WARN("Found moc $MOC_VERSION - unable to build xcwcp")
		AC_MSG_WARN(["xcwcp requires moc version >= $MOC_MAJOR_EARLIEST"])
	    else
		QT4MOC=$QT4MOC_PATH
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
if [[[ $QT4INC != "" && $QT4MOC != "" ]]] ; then
    echo "Qt4 headers dir is $QT4INC"
    echo "Qt4 moc is $QT4MOC"

    AC_SUBST(QT4INC)
    AC_SUBST(QT4MOC)

else
    if [[[ $QT4INC = "" ]]] ; then
	AC_MSG_WARN(["Can't find directory with Qt4 header files - unable to build xcwcp"])
    fi

    if [[[ $QT4MOC = "" ]]] ; then
	AC_MSG_WARN(["Can't find Qt moc version >= $MOC_MAJOR_EARLIEST - unable to build xcwcp"])
	AC_MSG_WARN(["Hint: to build xcwcp make sure that moc is in QT4DIR/bin on your PATH"])
    fi

    AC_MSG_WARN(["Hint: try setting QT4DIR env variable per instructions in unixcw/INSTALLING"])

    QT4INC=""
    QT4MOC=""
fi


