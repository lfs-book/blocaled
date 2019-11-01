#!/bin/bash

coproc dbus-daemon --config-file=scratch/test-session.xml \
	           --print-address=1 \
		   --print-pid=1
read -u $COPROC DBUS_SYSTEM_BUS_ADDRESS
export DBUS_SYSTEM_BUS_ADDRESS
read -u $COPROC DBUS_PID
export DBUS_PID
