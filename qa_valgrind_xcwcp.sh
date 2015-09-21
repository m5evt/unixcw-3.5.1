#!/bin/bash

executable_name=xcwcp
executable_fullpath=src/xcwcp/.libs/lt-xcwcp

timestamp=`date '+%Y.%m.%d_%H.%M.%S'`

valgrind --tool=memcheck \
	 --leak-check=yes \
	 --leak-check=full \
	 -v \
	 --show-reachable=yes \
	 --track-origins=yes \
	 --num-callers=20 \
	 --track-fds=yes \
	 $executable_fullpath $@ 2>log_valgrind_"$executable_name"_"$timestamp".log
