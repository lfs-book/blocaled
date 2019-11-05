#!/bin/bash

(
flock -x 17
if [ -s lock ]; then
    read -u 17 nb_links
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
flock -u 17
) 17>lock

export DBUS_SYSTEM_BUS_ADDRESS=$(cat dbus_address)
export DBUS_PID=$(cat dbus_pid)
