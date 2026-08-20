#!/usr/bin/env bash
# Build simdutf for the llvm-mingw target.
#
# WHY: it backs base64_decode's fast path at 42x the hand-written loop (48 GB/s
# against 1.1 GB/s, measured - ctbrowser/docs/performance.md). The loop stays as the
# fallback for anything simdutf's STRICT mode refuses, so the leniency contract
# does not change; what changes is the speed of every well-formed `data:` URL
# and every real `atob`.
#
# STATIC, because that is what makes an application a single .exe rather than a
# folder with DLLs in it - the same reason the SDL3, zlib, libpng,
# libjpeg-turbo and mimalloc in that sysroot are static.
#
# The result lands in the mingw sysroot beside them. That sysroot is gitignored
# and populated out-of-band, which is the established pattern this follows
# rather than invents - tools/mingw/build-mimalloc-mingw.sh is its sibling and this
# file is deliberately shaped like it.
#
#   tools/mingw/build-simdutf-mingw.sh [--clean]
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
target="x86_64-w64-mingw32"

for root in "${LLVM_MINGW:-}" "$HOME/projects/llvm-mingw/install/llvm-mingw-native" \
            "$here/tools/llvm-mingw"; do
    if [ -n "$root" ] && [ -x "$root/bin/$target-clang++" ]; then
        mingw="$root"
        break
    fi
done
if [ -z "${mingw:-}" ]; then
    echo "build-simdutf-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi
sysroot="$mingw/$target"

work="$HOME/projects/ctbrowser-simdutf"
if [ "${1:-}" = "--clean" ]; then rm -rf "$work"; fi
mkdir -p "$work"

# --- THE VERSION IS PINNED, and it MATCHES BREW'S -------------------------
# The Linux side installs simdutf from brew (tools/Brewfile). Two platforms on
# two versions of a decoder is the kind of difference that makes a
# byte-compared golden disagree for a reason nobody can see - the same argument
# the mimalloc and plutosvg pins already make.
simdutf_tag="v9.0.0"

# Keyed on the tag, not merely on the checkout existing: `[ ! -d ... ]` clones
# once and then never updates, so bumping the pin would change nothing. That is
# the trap the clang toolchain sat in for weeks.
if [ "$(cat "$work/.tag" 2>/dev/null)" != "$simdutf_tag" ]; then
    echo "build-simdutf-mingw: fetching simdutf $simdutf_tag"
    rm -rf "$work/simdutf" "$work/build"
    git clone --depth 1 --branch "$simdutf_tag" https://github.com/simdutf/simdutf.git \
        "$work/simdutf"
    printf '%s' "$simdutf_tag" > "$work/.tag"
fi

echo "build-simdutf-mingw: building simdutf for $target"
# Tests and tools off: they build executables for the HOST to run, and this is a
# cross build - they cannot run and would only fail late. Same reason
# build-image-libs-mingw.sh turns PNG_TESTS off.
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

if stale_cache "$work/build"; then
    echo "$(basename "$0"): wiping a build dir pinned to a moved toolchain"
    rm -rf "$work/build"
fi
cmake -S "$work/simdutf" -B "$work/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/toolchains/windows-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$sysroot" \
    -DBUILD_SHARED_LIBS=OFF \
    -DSIMDUTF_TESTS=OFF \
    -DSIMDUTF_TOOLS=OFF \
    -DSIMDUTF_BENCHMARKS=OFF >/dev/null
cmake --build "$work/build" >/dev/null
cmake --install "$work/build" >/dev/null

echo "build-simdutf-mingw: installed into $sysroot"
found="$(find "$sysroot" -name 'libsimdutf*.a' -print -quit)"
if [ -z "$found" ]; then
    echo "  MISSING libsimdutf.a - the configure step will fail" >&2
    exit 1
fi
echo "  $found"
