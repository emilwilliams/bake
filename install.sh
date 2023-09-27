#!/bin/sh
#EXEC:
  SUDO=${SUDO:-sudo}
  cc baked.c -o baked -pipe -O2 -Wall -Wextra -Wpedantic -Wshadow -Wundef
  $SUDO install -m 755 baked /usr/local/bin
#:STOP
