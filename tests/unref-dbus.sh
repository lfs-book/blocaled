#!/bin/bash

(
flock -x 17
read -u 17 nb_links
if ((nb_links == 1)); then
    rm dbus_address
    kill $(cat dbus_pid)
    rm dbus_pid
    trap "rm lock" 0
else
    echo $((nb_links - 1)) > lock
fi
flock -u 17
) 17<lock
