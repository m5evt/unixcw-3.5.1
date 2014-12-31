#!/bin/bash
executable=libcw_test_internal
timestamp=`date '+%Y.%m.%d_%H.%M.%S'`
valgrind --tool=memcheck --leak-check=yes --leak-check=full -v --show-reachable=yes --track-origins=yes --num-callers=20 --track-fds=yes  src/libcw/$executable $@ 2>log_valgrind_"$executable"_"$timestamp".log
