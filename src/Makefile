# vi: set ts=8 shiftwidth=8:
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
#

include ../Makefile.inc
SUBDIRS	= $(AC_SUBDIRS)

SHELL	= /bin/sh

# Top level makefile - descends into subdirectories and executes the make in
# these one at a time.

# Macro to support descending into selected subdirectories.
DESCEND	= for subdir in $(SUBDIRS); do				\
		( cd $$subdir; $(MAKE) $@ );			\
	  done

# All targets do nothing other than descend.
all install install-strip uninstall clean TAGS info dvi check:
	$(DESCEND)

distclean mostlyclean maintainer-clean:
	$(DESCEND)
