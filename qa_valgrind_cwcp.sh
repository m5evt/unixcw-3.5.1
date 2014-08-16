#!/bin/bash
valgrind --tool=memcheck --leak-check=yes --leak-check=full -v --show-reachable=yes --track-origins=yes --num-callers=20 --track-fds=yes --suppressions=/usr/lib/valgrind/ncurses.supp src/cwcp/.libs/cwcp $@ 2>log_valgrind_cwcp
