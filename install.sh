#!/bin/sh
# source install

TARGET="${TARGET:-/usr/local}"
INSTALL="bake"

usage() {
    echo "compiles and installs Bake into /usr/local (or TARGET) in bin/"
    echo ""
    echo "--alternatives          Includes awake and shake into the build"
    echo "--target=DIRECTORY      Target directory like /usr or /usr/local"
    echo ""
    exit 1
}

while [ ! -z $1 ]; do
    case $(echo "$1" | cut -d= -f1) in
        "--target")
            TARGET=$(echo "$1" | cut -d= -f2)
            ;;
        "--alternatives")
            INSTALL="bake shake awake"
            ;;
        "--help")
            usage
            ;;
        *)
            echo "Unknown option: " $1
            usage
            ;;
    esac
    shift
done

cd "$(dirname "$(readlink -f $0)")"

./awake bake.l
mkdir -p "$TARGET/bin" "$TARGET/man/man1"
install -m 755 $INSTALL "$TARGET/bin"
gzip -c bake.1 > "$TARGET/man/man1/bake.1.gz"
