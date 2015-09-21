#!/bin/bash

executable_name=cwcp
executable_fullpath=src/cwcp/.libs/cwcp

timestamp=`date '+%Y.%m.%d_%H.%M.%S'`

valgrind --tool=memcheck \
	 --leak-check=yes \
	 --leak-check=full \
	 -v \
	 --show-reachable=yes \
	 --track-origins=yes \
	 --num-callers=20 \
	 --track-fds=yes \
	 --suppressions=/usr/lib/valgrind/ncurses.supp \
	 --suppressions=/usr/lib/valgrind/default.supp \
	 $executable_fullpath $@ 2>log_valgrind_"$executable_name"_"$timestamp".log
