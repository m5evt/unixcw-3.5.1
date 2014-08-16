#!/bin/bash
files=`find ./src/ -type f -name \*.h`

dir=`pwd`
QtGui_include="" #`pkg-config --cflags QtGui`
QtCore_include="" #`pkg-config --cflags QtCore`

includes="-I/usr/include -I/usr/include/c++/`gcc -dumpversion`/  $QtGui_include $QtCore_include -I$dir/ -I$dir/src/ -I$dir/src/libcw -I$dir/src/xcwcp -I$dir/src/cwcp -I$dir/src/cwutils -I$dir/src/cwgen -I$dir/src/cw"

for file in $files;
do
	gcc -c -Wall -pedantic -std=c99 $includes $file
	rm $file.gch
done

