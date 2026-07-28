#!/bin/sh
# Install the engine into a temporary prefix and build tests/package against it.
#
# The only proof that `find_package(ctbrowser)` actually works. Everything
# else in the tree consumes the engine through add_subdirectory, which cannot fail the
# way an install can.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
prefix=${1:-$(mktemp -d)}
build=$(mktemp -d)
consumer=$(mktemp -d)
compiler=${CXX:-clang++}

echo "--- configuring ---"
cmake -S "$root" -B "$build" -G Ninja \
  -DCMAKE_CXX_COMPILER="$compiler" \
  -DCTBROWSER_BUILD_TESTS=OFF \
  -DCTBROWSER_BUILD_EXAMPLES=OFF \
  -DCMAKE_INSTALL_PREFIX="$prefix" >/dev/null

echo "--- building and installing ---"
cmake --build "$build" >/dev/null
cmake --install "$build" >/dev/null

echo "--- building the consumer against $prefix ---"
cmake -S "$root/tests/package" -B "$consumer" -G Ninja \
  -DCMAKE_CXX_COMPILER="$compiler" \
  -DCMAKE_PREFIX_PATH="$prefix" >/dev/null
cmake --build "$consumer" >/dev/null

echo "--- running it ---"
SDL_VIDEODRIVER=offscreen "$consumer/package-check"
