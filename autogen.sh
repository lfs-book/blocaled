#!/bin/sh

gtkdocize || exit 1
autoreconf --install --symlink

args="--prefix=/usr \
--sysconfdir=/etc"

echo
echo "----------------------------------------------------------------"
echo "Initialized build system. For a common configuration please run:"
echo "----------------------------------------------------------------"
echo
echo "./configure CFLAGS='-ggdb -O0' $args"
echo
