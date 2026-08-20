#!/bin/sh
# Install the engine into a temporary prefix and build the package consumer
# against it.
#
# The only proof that `find_package(ctbrowser)` actually works. Everything
# else in the tree consumes the engine through add_subdirectory, which cannot fail the
# way an install can.
#
# `$root` is the REPOSITORY root and `$engine` is the ctbrowser PROJECT inside
# it - the configure root since the monorepo split. The distinction is the
# whole point of this script: an installed engine is consumed by path-free
# find_package, and a consumer that accidentally reaches back into the source
# tree is exactly the bug it exists to catch.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
engine="$root/ctbrowser"
prefix=${1:-$(mktemp -d)}
build=$(mktemp -d)
consumer=$(mktemp -d)
compiler=${CXX:-clang++}

echo "--- configuring ---"
cmake -S "$engine" -B "$build" -G Ninja \
  -DCMAKE_CXX_COMPILER="$compiler" \
  -DCTBROWSER_BUILD_TESTS=OFF \
  -DCTBROWSER_BUILD_EXAMPLES=OFF \
  -DCMAKE_INSTALL_PREFIX="$prefix" >/dev/null

echo "--- building and installing ---"
cmake --build "$build" >/dev/null
cmake --install "$build" >/dev/null

echo "--- building the consumer against $prefix ---"
cmake -S "$engine/test/package" -B "$consumer" -G Ninja \
  -DCMAKE_CXX_COMPILER="$compiler" \
  -DCMAKE_PREFIX_PATH="$prefix" >/dev/null
cmake --build "$consumer" >/dev/null

echo "--- running it ---"
SDL_VIDEODRIVER=offscreen "$consumer/package-check"
