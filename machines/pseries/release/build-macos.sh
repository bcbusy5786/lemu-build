#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -e
[ $# -eq 1 ] || { echo "usage: $0 OUTDIR" >&2; exit 1; }
src=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$1/build-macos" "$1/macos-arm64"
out=$(cd "$1/macos-arm64" && pwd)
export MACOSX_DEPLOYMENT_TARGET=15.0
cd "$1/build-macos"
# SDL is the default display, as in the 40P and 7006 tarballs
"$src/configure" --target-list=ppc64-softmmu --without-default-features \
    --enable-system --enable-tcg --enable-hmp --enable-sdl --enable-cocoa \
    --enable-fdt=internal --enable-pixman \
    --enable-slirp --enable-vnc --enable-vvfat --enable-qcow1 --enable-vdi \
    --enable-vmdk --enable-vpc --enable-vhdx --enable-qed --disable-docs
ninja qemu-system-ppc64

rm -rf "$out"/*
cp qemu-system-ppc64 "$out/"
cd "$out"
dylibbundler -od -b -x qemu-system-ppc64 -d lib/ -p @executable_path/lib/ \
    </dev/null
# sdl2-compat loads SDL3 at run time from its own directory
install -m 644 "$(brew --prefix sdl3)/lib/libSDL3.0.dylib" lib/libSDL3.dylib
install_name_tool -id @executable_path/lib/libSDL3.dylib lib/libSDL3.dylib
for f in qemu-system-ppc64 lib/*; do
    while otool -l "$f" | grep -q 'path @executable_path/lib/ '; do
        install_name_tool -delete_rpath @executable_path/lib/ "$f"
    done
done
codesign -f -s - lib/*.dylib qemu-system-ppc64
