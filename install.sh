#!/bin/sh
# source install

TARGET=${TARGET:-/usr/local}
INSTALL=${INSTALL:-bake}

cd "$(dirname "$(readlink -f $0)")"

./shake bake.l -s $@ && \
mkdir $TARGET/bin $TARGET/man/man1 -p && \
install -m 755 $INSTALL $TARGET/bin

gzip -c bake.1 > $TARGET/man/man1/bake.1.gz
