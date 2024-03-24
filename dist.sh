#!/bin/sh
# distribution tarball

cd "$(dirname "$(readlink -f "$0")")"

FILES='LICENSE README shake install.sh bake.c config.h bake.1'
VERSION="$(cat VERSION)"
TARGET="bake-$VERSION"

mkdir -p $TARGET
cp -f $FILES $TARGET
tar cf $TARGET.tar $TARGET
gzip -f $TARGET.tar
rm -rf $TARGET
