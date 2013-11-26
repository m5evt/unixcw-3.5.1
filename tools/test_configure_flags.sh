#!/bin/bash

# Test configuration and compilation of software for all
# combinations of './configure' flags.
#
# The tests are performed in order to find compilation errors caused
# by conditional #includes and #defs in source code.
#
# Test are numbered and executed from 2^N to 1 (where N is the number of
# tested './configure' flags). You can pass a starting test number (in
# range from 2^N to 1) to the script.


let N=7


cd "../"
make clean &> /dev/null


options[0]="--disable-console"
options[1]="--disable-oss"
options[2]="--disable-alsa"
options[3]="--disable-pulseaudio"
options[4]="--disable-cwcp"
options[5]="--disable-xcwcp"
options[6]="--enable-dev"

#options[0]="1"
#options[1]="2"
#options[2]="3"
#options[3]="4"
#options[4]="5"
#options[5]="6"
#options[6]="7"



# Set up a starting point for tests - sometimes it's useful not to run
# the tests from the beginning
if [ $1 ]; then
    let i=$1
else
    let i=$((2**N))
fi



for ((; i>0; i--))
do
    let tmp=$(($i))

    switches=""

    for ((j = $N - 1; j >= 0; j--))
    do
	let power=$((2**$j))

	if (( $(($tmp - $power)) > 0 )); then
	    let tmp=$(($tmp - $power))

	    switches="$switches ${options[$j]}"
	else

	    # test
	    #switches="$switches 0"

	    # real code
	    switches="$switches"
	fi

    done

    # test
    #echo $switches

    echo $(eval pwd)

    # real code
    command="./configure $switches &>/dev/null  && make &>/dev/null && make clean &>/dev/null"
    echo $i": "$command
    result=$(eval $command)

    # $? is the result code of last command.
    if [ $? != 0 ]; then
	echo "FAILED"
	break
    fi
done
