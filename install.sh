#!/bin/sh
# source install

TARGET=${TARGET:-/usr/local}
INSTALL=${INSTALL:-bake shake}

cd $(dirname "$(readlink -f "$0")")/src
chmod +x shake

./shake bake.c -s $@ && \
mkdir $TARGET/bin $TARGET/man/man1 -p && \
install -m 755 $INSTALL $TARGET/bin

gzip -c bake.1 > $TARGET/man/man1/bake.1.gz && \
ln -f -s $TARGET/man/man1/bake.1.gz $TARGET/man/man1/shake.1.gz 
