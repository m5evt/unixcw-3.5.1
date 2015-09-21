#!/bin/bash

executable_name=libcw_test_public
executable_fullpath=src/libcw/tests/libcw_test_public

timestamp=`date '+%Y.%m.%d_%H.%M.%S'`

valgrind --tool=memcheck \
	 --leak-check=yes \
	 --leak-check=full \
	 -v \
	 --show-reachable=yes \
	 --track-origins=yes \
	 --num-callers=20 \
	 --track-fds=yes  \
	 $executable_fullpath $@ 2>log_valgrind_"$executable_name"_"$timestamp".log
