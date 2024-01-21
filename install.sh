#!/bin/sh
cd $(dirname "$(readlink -f "$0")")
SUDO=${SUDO:-sudo}
chmod +x shake
./shake bake.c -s $@ && \
$SUDO install -m 755 shake bake ${TARGET:-/usr/local/bin}
