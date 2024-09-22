cd "$(dirname "$(readlink -f $0)")"
PREFIX=${PREFIX:-/usr/local}
echo "PREFIX=$PREFIX"
bake cbake.l
install -m755 -sv ./cbake $PREFIX/bin/
