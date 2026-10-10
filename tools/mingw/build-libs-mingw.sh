#!/usr/bin/env bash
# Build the CMake-built libraries the Windows cross build links, for the
# llvm-mingw target: zlib, libpng, libjpeg-turbo, mimalloc, simdutf and cpptrace.
# (Boost.URL and Boost.ProgramOptions are not built with CMake here; they have
# their own tools/mingw/build-boost-mingw.sh.)
#
# Adding a library takes one `fetch` and one `build` line below.
#
# Why each one is here:
#   zlib, libpng, libjpeg-turbo   PNG and JPEG decode in the SDL-free engine
#                                 (ctbrowser/docs/build.md), so the sysroot
#                                 needs them. zlib only because libpng
#                                 needs it.
#   mimalloc                      backs operator new/delete and is required by
#                                 the default build (-4.2% instructions on Linux,
#                                 -11.7% wall on Windows; ctbrowser/docs/performance.md).
#                                 It beat jemalloc on this file alone: jemalloc
#                                 does not cross-compile under llvm-mingw.
#   simdutf                       base64_decode's fast path, 42x the loop.
#   cpptrace                      tests only, and optional: llvm-mingw has no
#                                 <stacktrace>, and most of this project's
#                                 expensive bugs were Windows-only deaths with
#                                 no location. ctbrowser/cmake/modules/CTTest.cmake attaches it.
#
# Static, because that makes an application a single .exe rather than a
# folder with DLLs in it; the SDL3 in that sysroot is static for the same reason.
# The result lands in the mingw sysroot beside SDL3, SDL3_ttf, plutosvg,
# libboost_url and libboost_program_options. That sysroot is gitignored and
# populated out-of-band.
#
#   tools/mingw/build-libs-mingw.sh [--clean]
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
target="x86_64-w64-mingw32"

# --- The cross compiler, looked for where the toolchain file looks. ---------
for root in "${LLVM_MINGW:-}" "$HOME/projects/llvm-mingw/install/llvm-mingw-native" \
            "$here/tools/llvm-mingw"; do
    if [ -n "$root" ] && [ -x "$root/bin/$target-clang++" ]; then
        mingw="$root"
        break
    fi
done
if [ -z "${mingw:-}" ]; then
    echo "build-libs-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi
sysroot="$mingw/$target"

work="$HOME/projects/ctbrowser-mingw-libs"
if [ "${1:-}" = "--clean" ]; then rm -rf "$work"; fi
mkdir -p "$work"

# --- Pinned versions. ------------------------------------------------------
# Goldens are byte-compared between the Windows and Linux builds, so a cross
# build that followed upstream's default branch could make them disagree for
# no visible reason. Bump these deliberately. mimalloc, simdutf and cpptrace
# must match the versions in tools/Brewfile, where the Linux build gets them.
zlib_tag="v1.3.1"
libpng_tag="v1.6.50"
turbo_tag="3.1.2"
mimalloc_tag="v3.4.3"
simdutf_tag="v9.0.0"
cpptrace_tag="v1.0.4"

# fetch <name> <git url> <tag>: re-clone whenever the pinned tag changes, not
# only when the checkout is missing; otherwise bumping a pin would silently
# keep the old version. <name>.tag records the tag of the current checkout.
fetch() {
    local name="$1" url="$2" tag="$3"
    if [ "$(cat "$work/$name.tag" 2>/dev/null)" != "$tag" ]; then
        echo "build-libs-mingw: fetching $name $tag"
        rm -rf "$work/$name" "$work/$name-build"
        git clone --depth 1 --branch "$tag" "$url" "$work/$name"
        printf '%s' "$tag" > "$work/$name.tag"
    fi
}

# stale_cache <build dir>: succeeds when the dir must be wiped. A CMake build
# dir records the toolchain file by path in both CMakeCache.txt and
# CMakeFiles/<ver>/CMakeSystem.cmake, which project() re-includes on every
# reconfigure. If that file moves, reconfiguring fails at project(). A failed
# reconfigure rewrites the cache but not CMakeSystem.cmake, so checking the
# cache is not enough: report the dir as stale when any .cmake that
# CMakeSystem.cmake includes no longer exists.
stale_cache() {
    local dir="$1" named
    [ -d "$dir" ] || return 1
    for named in $(grep -rhoE 'include\("[^"]+\.cmake"\)' \
                       "$dir"/CMakeFiles/*/CMakeSystem.cmake 2>/dev/null \
                   | sed -E 's/include\("(.*)"\)/\1/'); do
        [ -f "$named" ] || return 0
    done
    return 1
}

# build <name> <cmake -D flags>...: configure, build and install one library
# into the sysroot. Each ships its own CMakeLists, and all of them reuse the
# engine's toolchain file so there is one description of the cross compiler.
build() {
    local name="$1"
    shift
    echo "build-libs-mingw: building $name for $target"
    if stale_cache "$work/$name-build"; then
        echo "build-libs-mingw: wiping a build dir pinned to a moved toolchain"
        rm -rf "$work/$name-build"
    fi
    cmake -S "$work/$name" -B "$work/$name-build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$here/cmake/toolchains/windows-x86_64.cmake" \
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
# zlib's CMake builds a DLL and its import library regardless of
# BUILD_SHARED_LIBS, and FindZLIB prefers the name `zlib`, so leaving them
# there would make every .exe need zlib1.dll beside it. Remove them and keep
# the static library.
rm -f "$sysroot/lib/libzlib.dll.a" "$sysroot"/bin/*zlib*.dll

fetch libpng https://github.com/pnggroup/libpng.git "$libpng_tag"
# PNG_TESTS off: they build executables for the host to run and this is a cross
# build, so they cannot run and would only fail late. Same for simdutf's and
# cpptrace's tests below.
build libpng -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF

fetch libjpeg-turbo https://github.com/libjpeg-turbo/libjpeg-turbo.git "$turbo_tag"
# ENABLE_SHARED off leaves the static libturbojpeg the engine links. WITH_TURBOJPEG
# is what builds the tj3_* API jpeg.cpp uses - libjpeg's own API is not enough.
build libjpeg-turbo -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=ON

fetch mimalloc https://github.com/microsoft/mimalloc.git "$mimalloc_tag"
# MI_OVERRIDE off: on Windows the override machinery patches the CRT's malloc at
# load time, which is a different and much more invasive thing than linking an
# allocator. The engine calls mimalloc through operator new/delete instead -
# see ctbrowser/lib/Core/allocator.cpp - which is explicit, portable, and does
# not depend on load order.
build mimalloc -DMI_BUILD_SHARED=OFF -DMI_BUILD_OBJECT=OFF -DMI_BUILD_TESTS=OFF \
    -DMI_OVERRIDE=OFF -DMI_WIN_REDIRECT=OFF
# Copy into the sysroot's own lib/ and include/: mimalloc installs into
# versioned lib/mimalloc-<ver>/ and include/mimalloc-<ver>/ subdirectories,
# which find_library does not search, and MI_INSTALL_TOPLEVEL was not honoured
# in every release (2.1.9 ignored it).
# Remove every previous copy first: v2 installs libmimalloc-static.a and v3
# libmimalloc.a, so a bump would leave both and find_library could pick the
# stale one. ctbrowser/unittests/unit/core/basics.cpp checks which allocator
# version is linked to catch exactly this.
rm -f "$sysroot"/lib/libmimalloc*.a
built="$(find "$work/mimalloc-build" -name 'libmimalloc*.a' -print -quit)"
header="$(find "$work/mimalloc/include" -name 'mimalloc.h' -print -quit)"
if [ -z "$built" ] || [ -z "$header" ]; then
    echo "build-libs-mingw: mimalloc produced no library or header" >&2
    exit 1
fi
install -Dm644 "$built" "$sysroot/lib/$(basename "$built")"
install -Dm644 "$header" "$sysroot/include/mimalloc.h"

fetch simdutf https://github.com/simdutf/simdutf.git "$simdutf_tag"
build simdutf -DSIMDUTF_TESTS=OFF -DSIMDUTF_TOOLS=OFF -DSIMDUTF_BENCHMARKS=OFF

fetch cpptrace https://github.com/jeremy-rifkin/cpptrace.git "$cpptrace_tag"
build cpptrace -DCPPTRACE_BUILD_TESTING=OFF

echo "build-libs-mingw: installed into $sysroot"
for lib in libzlibstatic.a libpng16.a libturbojpeg.a 'libmimalloc*.a' 'libsimdutf*.a' \
           'libcpptrace*.a' cpptrace/cpptrace.hpp mimalloc.h; do
    found="$(find "$sysroot" -path "*/$lib" -print -quit)"
    if [ -z "$found" ]; then
        echo "  MISSING $lib - the configure step will fail" >&2
        exit 1
    fi
    ls -la "$found"
done
