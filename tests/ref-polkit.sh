#!/bin/bash

(
flock -x 9
if [ -s pklock ]; then
    read -u 9 nb_links
    echo $((nb_links + 1)) > pklock
else
    ./gdbus-mock-polkit &
    echo $! > pk_pid
    echo 1 > pklock
fi
flock -u 9
) 9<>pklock

