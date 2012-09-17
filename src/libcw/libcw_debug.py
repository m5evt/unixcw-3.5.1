#!/usr/bin/python3

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
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.





# This file is a part of libcw package, which is a part of unixcw
# project.

# Read output of debugged libcw application, find lines starting with
# "libcwevent:", attempt to construct a table with data that can be
# plotted with some plotting program.
#
# Resulting plot should (in theory) demonstrate timings of different
# events in libcw and applications using libcw.


import re
import sys


# For mapping human-readable representations to plot-friendly numbers.
event_values = {
        'CW_DEBUG_EVENT_TONE_LOW'  : "0",
        'CW_DEBUG_EVENT_TONE_MID'  : "1",
        'CW_DEBUG_EVENT_TONE_HIGH' : "2",

        'CW_DEBUG_EVENT_TQ_JUST_EMPTIED' : "0",
        'CW_DEBUG_EVENT_TQ_NONEMPTY'     : "1",
        'CW_DEBUG_EVENT_TQ_STILL_EMPTY'  : "2"
}





# Main table for input data. Once read from file,
# it will be reused by the script as input source of data.
input_events = {}


tone_event_present = False
tone_event_previous = "CW_DEBUG_EVENT_TONE_LOW"
tq_event_present = False
tq_event_previous = "CW_DEBUG_EVENT_TQ_STILL_EMPTY"



f = open(sys.argv[1], "r")
for line in f:
    m = re.match("^libcwevent:\t([0-9]+)\t(.+)", line)
    if not m:
        continue

    if re.match("CW_DEBUG_EVENT_TONE", m.group(2)):
        tone_event_present = True

        time = int(m.group(1))
        event = m.group(2)

        # Collisions with other events.
        if time in input_events.keys():
            print("Tone collision 1", file=sys.stderr)
        if time - 1 in input_events.keys():
            print("Tone collision 2", file=sys.stderr)

        # We need 'time - 1' to make nice, rectangular-shaped
        # transitions of signals instead of long ramps.
        input_events[time - 1] = tone_event_previous
        input_events[time] = event
        tone_event_previous = event

    elif re.match("CW_DEBUG_EVENT_TQ", m.group(2)):
        tq_event_present = True

        time = int(m.group(1))
        event = m.group(2)

        # Collisions with other events.
        if time in input_events.keys():
            print("TQ collision 1", file=sys.stderr)
        if time - 1 in input_events.keys():
            print("TQ collision 2", file=sys.stderr)


        # We need 'time - 1' to make nice, rectangular-shaped
        # transitions of signals instead of long ramps.
        input_events[time - 1] = tq_event_previous
        input_events[time] = event
        tq_event_previous = event

    else:
        print("Unknown type of debug event: " + m.group(2), file=sys.stderr)
        exit




# Restore initial values of 'previous' values for purposes
# of printing output table.
tone_event_previous = "CW_DEBUG_EVENT_TONE_LOW"
tq_event_previous = "CW_DEBUG_EVENT_TQ_STILL_NEMPTY"




# Header of output table.
print("Time", end = "")
if tone_event_present:
    print("\tTone", end = "")
if tq_event_present:
    print("\tTQ", end = "")
print("")



# Walk through created table of events, put every event in
# appropriate column. Time goes to column 1.
for time in sorted(input_events.keys()):

    print(time, end = "")

    if tone_event_present:
        # There were some tone events in input data, so produce value
        # in 'tone' column in current row.

        if re.match("CW_DEBUG_EVENT_TONE", input_events[time]):
            print("\t" + event_values[input_events[time]], end = "")
            tone_event_previous = input_events[time]
        else:
            # This is not a tone event, but we need to use some
            # tone event to fill the cell in the row (output
            # table cannot be sparse). We use previous tone event
            # to fill the gap.
            print("\t" + event_values[tone_event_previous], end = "")


    if tq_event_present:
        # There were some tq events in input data, so produce value
        # in 'tq' column in current row.

        if re.match("CW_DEBUG_EVENT_TQ", input_events[time]):
            print("\t" + event_values[input_events[time]], end = "")
            tq_event_previous = input_events[time]
        else:
            # This is not a tq event, but we need to use some
            # tq event to fill the cell in the row (output
            # table cannot be sparse). We use previous tq event
            # to fill the gap.
            print("\t" + event_values[tq_event_previous], end = "")


    # Row's newline.
    print("")
