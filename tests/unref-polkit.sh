#!/bin/bash

(
flock -x 9
read -u 9 nb_links
if ((nb_links == 1)); then
    kill $(cat pk_pid)
    rm pk_pid
    trap "rm pklock" 0
else
    echo $((nb_links - 1)) > pklock
    flock -u  9
fi
) 9<>pklock
