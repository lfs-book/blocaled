#!/bin/bash

#  Copyright 2019 Pierre Labastie
#
#  Permission is hereby granted, free of charge, to any person obtaining
#  a copy of this software and associated documentation files (the
#  "Software"), to deal in the Software without restriction, including
#  without limitation the rights to use, copy, modify, merge, publish,
#  distribute, sublicense, and/or sell copies of the Software, and to
#  permit persons to whom the Software is furnished to do so, subject to
#  the following conditions:
#
#  The above copyright notice and this permission notice shall be
#  included in all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
#  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
#  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
#  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
#  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
#  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
#  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

(
flock -x 9
if [ -s lock ]; then
    read -u 9 nb_links
    echo $((nb_links + 1)) > lock
else
    coproc dbus-daemon --config-file=scratch/test-session.xml \
                       --print-address=1 \
                       --print-pid=1
    read -u $COPROC DBUS_SYSTEM_BUS_ADDRESS
    echo $DBUS_SYSTEM_BUS_ADDRESS > dbus_address
    read -u $COPROC DBUS_PID
    echo $DBUS_PID > dbus_pid
    echo 1 > lock
fi
flock -u 9
) 9<>lock

export DBUS_SYSTEM_BUS_ADDRESS=$(cat dbus_address)
export DBUS_PID=$(cat dbus_pid)
