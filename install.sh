#!/bin/sh
SUDO=${SUDO:-sudo}
cc baked.c -o baked -std=gnu99 -O2 -Wall -Wextra -Wpedantic -pipe $CFLAGS
$SUDO install -m 755 baked /usr/local/bin
