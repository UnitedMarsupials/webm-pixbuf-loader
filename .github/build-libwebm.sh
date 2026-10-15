#!/bin/sh
#
# build-libwebm.sh [PREFIX]
#
# libwebm's webm_parser has no distribution package and no pkg-config file, so
# meson's find_library('webm') and <webm/webm_parser.h> cannot resolve it
# until it is built and installed.  Build it from a pinned release tag and
# install it under PREFIX (default /usr/local, where the compiler and linker
# look by themselves on Linux and the BSDs alike).
#
# Static, with position-independent code: the loader is a shared module that
# links libwebm in, so the archive's objects must be PIC.  Set SUDO=  (empty)
# to install without sudo, e.g. into a prefix you own.
set -e

ver=libwebm-1.0.0.32
prefix=${1:-/usr/local}
src=$(mktemp -d)

git clone --depth 1 --branch "$ver" \
    https://github.com/webmproject/libwebm "$src"

cmake -S "$src" -B "$src/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_WEBM_PARSER=ON \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DENABLE_TESTS=OFF \
    -DENABLE_SAMPLE_PROGRAMS=OFF \
    -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$src/build" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
${SUDO-sudo} cmake --install "$src/build"
