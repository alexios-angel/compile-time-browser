#!/usr/bin/env bash
# Build the image codecs the engine LINKS, for the llvm-mingw target.
#
# WHY THIS EXISTS. PNG and JPEG used to arrive through SDL3_image, a hook only
# `ctbrowser.app` installs - so `tests/`, which is SDL-free by an invariant
# `tests/lint/api_surface` lints for, saw every PNG as a zero-sized image and nothing
# in the suite said so. Phaser found it: its texture manager loads three base64
# PNGs during boot and will not start until all three settle. Both decoders moved
# into the SDL-free engine on 2026-08-01, which means the Windows cross build
# needs them in its sysroot. See docs/build.md and docs/shell.md.
#
# THREE LIBRARIES, and the third is only there for the second: libpng needs
# zlib, and the sysroot had neither. libjpeg-turbo needs nothing.
#
#   zlib            https://github.com/madler/zlib
#   libpng          the format's reference implementation
#   libjpeg-turbo   libjpeg's API with SIMD, what browsers and Android ship
#
# STATIC, because that is what makes an application a single .exe rather than a
# folder with DLLs in it - the same reason the SDL3 in that sysroot is static.
#
# The result lands in the mingw sysroot beside the static SDL3, SDL3_ttf,
# plutosvg and libboost_url already there. That sysroot is gitignored and
# populated out-of-band, which is the established pattern this follows rather
# than invents - tools/mingw/build-boost-mingw.sh is its sibling and this file is
# deliberately shaped like it.
#
#   tools/mingw/build-image-libs-mingw.sh [--clean]
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
target="x86_64-w64-mingw32"

# --- the cross compiler, looked for where the toolchain file looks ----------
for root in "${LLVM_MINGW:-}" "$HOME/projects/llvm-mingw/install/llvm-mingw-native" \
            "$here/tools/llvm-mingw"; do
    if [ -n "$root" ] && [ -x "$root/bin/$target-clang++" ]; then
        mingw="$root"
        break
    fi
done
if [ -z "${mingw:-}" ]; then
    echo "build-image-libs-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi
sysroot="$mingw/$target"

work="$HOME/projects/ctbrowser-image-libs"
if [ "${1:-}" = "--clean" ]; then rm -rf "$work"; fi
mkdir -p "$work"

# --- VERSIONS ARE PINNED ---------------------------------------------------
# A cross build that silently follows upstream's default branch is how the
# Windows half of a byte-compared golden starts disagreeing with the Linux half
# for a reason nobody can see. Bump these deliberately.
zlib_tag="v1.3.1"
libpng_tag="v1.6.50"
turbo_tag="3.1.2"

fetch() {
    local name="$1" url="$2" tag="$3"
    if [ ! -d "$work/$name" ]; then
        echo "build-image-libs-mingw: fetching $name $tag"
        git clone --depth 1 --branch "$tag" "$url" "$work/$name"
    fi
}

# CMake for all three: each ships its own CMakeLists, and the engine's own
# toolchain file is reused rather than a second description of the same
# compiler - two spellings of one toolchain is the drift this tree keeps paying
# for.
build() {
    local name="$1"
    shift
    echo "build-image-libs-mingw: building $name for $target"
    cmake -S "$work/$name" -B "$work/$name-build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$here/cmake/toolchain-windows-x86_64.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$sysroot" \
        -DCMAKE_PREFIX_PATH="$sysroot" \
        -DBUILD_SHARED_LIBS=OFF \
        "$@" >/dev/null
    cmake --build "$work/$name-build" >/dev/null
    cmake --install "$work/$name-build" >/dev/null
}

fetch zlib https://github.com/madler/zlib.git "$zlib_tag"
build zlib -DZLIB_BUILD_EXAMPLES=OFF
# zlib's CMake builds a DLL and its import library REGARDLESS of
# BUILD_SHARED_LIBS, and FindZLIB prefers the name `zlib` - so leaving them
# there means every .exe wants a zlib1.dll beside it, which is exactly the
# folder-of-DLLs this sysroot exists to avoid. The static one stays.
rm -f "$sysroot/lib/libzlib.dll.a" "$sysroot"/bin/*zlib*.dll

fetch libpng https://github.com/pnggroup/libpng.git "$libpng_tag"
# PNG_TESTS off: they build executables for the HOST to run and this is a cross
# build, so they cannot run and would only fail late.
build libpng -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF

fetch libjpeg-turbo https://github.com/libjpeg-turbo/libjpeg-turbo.git "$turbo_tag"
# ENABLE_SHARED off leaves the static libturbojpeg the engine links. WITH_TURBOJPEG
# is what builds the tj3_* API jpeg.cpp uses - libjpeg's own API is not enough.
build libjpeg-turbo -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=ON

echo "build-image-libs-mingw: installed into $sysroot"
for lib in libzlibstatic.a libpng16.a libturbojpeg.a; do
    found="$(find "$sysroot" -name "$lib" -print -quit)"
    if [ -z "$found" ]; then
        echo "  MISSING $lib - the configure step will fail" >&2
        exit 1
    fi
    ls -la "$found"
done
