#!/bin/bash

(
flock -x 9
read -u 9 nb_links
if ((nb_links == 1)); then
    rm dbus_address
    kill $(cat dbus_pid)
    rm dbus_pid
    trap "rm lock" 0
else
    echo $((nb_links - 1)) > lock
    flock -u  9
fi
) 9<>lock
