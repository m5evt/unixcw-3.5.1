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
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
#

-include Makefile.inc

SHELL	= /bin/sh
SUBDIRS	= src

DIST	= unixcw-3.0.1

# Top level makefile - descends into subdirectories and executes the make in
# these one at a time.

# Macro to support descending into selected subdirectories.
DESCEND	= for subdir in $(SUBDIRS); do				\
		( cd $$subdir; $(MAKE) $@ );			\
	  done

# Targets that do nothing other than descend.
all install install-strip uninstall TAGS info dvi check:
	$(DESCEND)

clean:
	$(DESCEND)
	rm -f Makefile.inc

# Targets that do just a little more than this.
distclean mostlyclean:
	$(DESCEND)
	rm -f $(DIST).tar $(DIST).tar.gz $(DIST).tgz
	rm -f Makefile.inc src/config.h src/config.h.in~
	rm -f config.status config.cache config.log
	rm -rf autom4te.cache

maintainer-clean: distclean
	rm -f configure src/config.h.in aclocal.m4 configure.scan autoscan.log
	rm -f po/UnixCW.po
	-rmdir po

dist:	distclean
	rm -f $(DIST).tar $(DIST).tar.gz $(DIST).tgz
	rm -f $(DIST); ln -s . $(DIST)
	FILES="`ls -d $(DIST)/* | grep -v '^$(DIST)/$(DIST)$$'`";	\
		tar zcvf $(DIST).tgz $$FILES
	rm $(DIST)
