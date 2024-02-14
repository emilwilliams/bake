#!/bin/sh

TARGET=${TARGET:-/usr/local}
INSTALL=${INSTALL:-bake shake}

cd $(dirname "$(readlink -f "$0")")
chmod +x shake

./shake bake.c -s $@ && \
mkdir $TARGET/bin $TARGET/man/man1 -p && \
install -m 755 $INSTALL $TARGET/bin && \
install -m 644 bake.1.gz $TARGET/man/man1 && \
ln -f -s $TARGET/man/man1/bake.1.gz $TARGET/man/man1/shake.1.gz 
