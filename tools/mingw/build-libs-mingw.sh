#!/usr/bin/env bash
# Build the CMake-built libraries the Windows cross build links, for the
# llvm-mingw target: zlib, libpng, libjpeg-turbo, mimalloc, simdutf and cpptrace.
# (Boost.URL is not CMake and has its own tools/mingw/build-boost-mingw.sh.)
#
# Until 2026-09-15 these were four scripts - image-libs, mimalloc, simdutf and
# cpptrace - each a copy of the same probe/clone/configure/install skeleton with
# a different repo, tag and flag list. One table-driven script is the same
# skeleton once, and a new library is one `fetch` and one `build` line.
#
# WHY EACH ONE IS HERE:
#   zlib, libpng, libjpeg-turbo   PNG and JPEG decode in the SDL-FREE engine
#                                 (2026-08-01; ctbrowser/docs/build.md), so the
#                                 sysroot needs them. zlib only because libpng
#                                 needs it.
#   mimalloc                      backs operator new/delete and is REQUIRED by
#                                 the default build (-4.2% instructions on Linux,
#                                 -11.7% wall on Windows; ctbrowser/docs/performance.md).
#                                 It beat jemalloc on this file alone: jemalloc
#                                 does not cross-compile under llvm-mingw.
#   simdutf                       base64_decode's fast path, 42x the loop.
#   cpptrace                      TESTS ONLY and optional: llvm-mingw has no
#                                 <stacktrace>, and most of this project's
#                                 expensive bugs were Windows-only deaths with
#                                 no location. ctbrowser/cmake/modules/CTTest.cmake attaches it.
#
# STATIC, because that is what makes an application a single .exe rather than a
# folder with DLLs in it - the same reason the SDL3 in that sysroot is static.
# The result lands in the mingw sysroot beside SDL3, SDL3_ttf, plutosvg and
# libboost_url. That sysroot is gitignored and populated out-of-band.
#
#   tools/mingw/build-libs-mingw.sh [--clean]
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
    echo "build-libs-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi
sysroot="$mingw/$target"

work="$HOME/projects/ctbrowser-mingw-libs"
if [ "${1:-}" = "--clean" ]; then rm -rf "$work"; fi
mkdir -p "$work"

# --- VERSIONS ARE PINNED ---------------------------------------------------
# A cross build that silently follows upstream's default branch is how the
# Windows half of a byte-compared golden starts disagreeing with the Linux half
# for a reason nobody can see. Bump these deliberately - and mimalloc, simdutf
# and cpptrace MATCH BREW'S (tools/Brewfile), which is where the Linux side gets
# its copy; two platforms on two allocator versions is the same disagreement.
zlib_tag="v1.3.1"
libpng_tag="v1.6.50"
turbo_tag="3.1.2"
mimalloc_tag="v3.4.3"
simdutf_tag="v9.0.0"
cpptrace_tag="v1.0.4"

# KEYED ON THE TAG, not merely on the checkout existing. `[ ! -d ... ]` clones
# once and then never updates, so bumping a pin above would change nothing and
# the sysroot would quietly keep the old version - which is exactly how the
# clang toolchain sat three releases behind its own pin for weeks.
fetch() {
    local name="$1" url="$2" tag="$3"
    if [ "$(cat "$work/$name.tag" 2>/dev/null)" != "$tag" ]; then
        echo "build-libs-mingw: fetching $name $tag"
        rm -rf "$work/$name" "$work/$name-build"
        git clone --depth 1 --branch "$tag" "$url" "$work/$name"
        printf '%s' "$tag" > "$work/$name.tag"
    fi
}

# A CMake build directory records the TOOLCHAIN FILE BY PATH - in the cache,
# and again in CMakeFiles/<ver>/CMakeSystem.cmake, which project() re-includes
# on every reconfigure. When that file MOVES - as it did when the repository
# became a monorepo and the toolchain went to cmake/toolchains/ - the include
# fails and the build dies at project() naming a path that no longer exists.
#
# CHECKING THE CACHE IS NOT ENOUGH, which is how this was first written and why
# it did not work: a reconfigure that fails still rewrites CMakeCache.txt with
# the NEW toolchain while CMakeSystem.cmake keeps the old one, so the two
# disagree and only the second is consulted. Ask the question that actually
# fails instead: does every .cmake this build dir says it includes still exist?
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

# CMake for all of them: each ships its own CMakeLists, and the engine's own
# toolchain file is reused rather than a second description of the same
# compiler - two spellings of one toolchain is the drift this tree keeps paying
# for.
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
# zlib's CMake builds a DLL and its import library REGARDLESS of
# BUILD_SHARED_LIBS, and FindZLIB prefers the name `zlib` - so leaving them
# there means every .exe wants a zlib1.dll beside it, which is exactly the
# folder-of-DLLs this sysroot exists to avoid. The static one stays.
rm -f "$sysroot/lib/libzlib.dll.a" "$sysroot"/bin/*zlib*.dll

fetch libpng https://github.com/pnggroup/libpng.git "$libpng_tag"
# PNG_TESTS off: they build executables for the HOST to run and this is a cross
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
# FLATTENED INTO THE SYSROOT'S OWN lib/ AND include/. mimalloc installs into
# lib/mimalloc-<version>/ and include/mimalloc-<version>/, and `find_library`
# does not search versioned subdirectories - so the cross build reports
# "mimalloc not found" with the library sitting right there. MI_INSTALL_TOPLEVEL
# is supposed to prevent that and is not honoured by 2.1.9. Copying is
# version-proof.
# EVERY PREVIOUS COPY GOES FIRST: v2 installs libmimalloc-static.a, v3
# libmimalloc.a, so a bump leaves BOTH and find_library picked the stale v2 -
# the Windows build quietly linked the old allocator across a major version.
# ctbrowser/unittests/unit/core_basics asks ctbrowser::allocator_version() and
# caught it, which is the entire reason that check exists.
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
