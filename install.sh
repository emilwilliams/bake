#!/bin/sh
cd $(dirname "$(readlink -f "$0")")
SUDO=${SUDO:-sudo}
cc baked.c -o baked -std=gnu99 -O2 -Wall -Wextra -Wpedantic -pipe $CFLAGS
$SUDO install -m 755 baked /usr/local/bin
