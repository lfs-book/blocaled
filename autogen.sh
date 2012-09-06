#!/bin/sh

autoreconf --install --symlink

args="--prefix=/usr \
--sysconfdir=/etc \
--localstatedir=/var"

echo
echo "----------------------------------------------------------------"
echo "Initialized build system. For a common configuration please run:"
echo "----------------------------------------------------------------"
echo
echo "./configure CFLAGS='-ggdb -O0' $args"
echo
