#!/usr/bin/env bash
# Build mimalloc for the llvm-mingw target.
#
# WHY AN ALLOCATOR AT ALL. Allocator traffic is ~4.8% of a Phaser frame
# (docs/performance.md): _int_malloc, _int_free, malloc, free,
# malloc_consolidate and operator new between them. It is the one hot item in
# this engine where a LIBRARY is the whole answer - a drop-in allocator is a
# link-line change that cannot alter results, only their cost.
#
# WHY mimalloc AND NOT jemalloc, which was the obvious rival and was measured
# rather than argued about. On the real workload, min of seven:
#
#   glibc      15.058 G instructions   1021 ms
#   jemalloc   14.618 G  (-2.9%)        974 ms  (-4.6%)
#   mimalloc   14.421 G  (-4.2%)        976 ms  (-4.4%)
#
# They are TIED on wall clock - 974 against 976 is noise - and mimalloc is about
# 1.4% ahead on instructions. So the tie-breaker was not performance but THIS
# FILE: mimalloc is a Microsoft project with first-class Windows support that
# cross-compiles under llvm-mingw, while jemalloc's mingw support is an
# afterthought that has been unmaintained for years. This tree ships Windows
# binaries and byte-compares thirteen goldens on them, so an allocator that does
# not cross-compile was never a candidate whatever it scored.
#
# STATIC, because that is what makes an application a single .exe rather than a
# folder with DLLs in it - the same reason the SDL3, zlib, libpng and
# libjpeg-turbo in that sysroot are static.
#
# The result lands in the mingw sysroot beside them. That sysroot is gitignored
# and populated out-of-band, which is the established pattern this follows
# rather than invents - tools/build-image-libs-mingw.sh is its sibling and this
# file is deliberately shaped like it.
#
#   tools/build-mimalloc-mingw.sh [--clean]
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
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
    echo "build-mimalloc-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi
sysroot="$mingw/$target"

work="$HOME/projects/ctbrowser-mimalloc"
if [ "${1:-}" = "--clean" ]; then rm -rf "$work"; fi
mkdir -p "$work"

# --- THE VERSION IS PINNED -------------------------------------------------
# A cross build that silently follows upstream's default branch is how the
# Windows half of a byte-compared golden starts disagreeing with the Linux half
# for a reason nobody can see. Bump this deliberately.
#
# AND IT MATCHES BREW'S, which is where the Linux side gets its copy
# (tools/Brewfile). Two platforms on two allocator versions is exactly the kind
# of difference that makes a golden disagree for a reason nobody can see -
# the same argument the SDL3 and plutosvg pins already make.
mimalloc_tag="v3.4.3"

# KEYED ON THE TAG, not merely on the checkout existing. `[ ! -d ... ]` clones
# once and then never updates, so bumping the pin above would change nothing and
# the sysroot would quietly keep the old major version - which is exactly how
# the clang toolchain sat three releases behind its own pin for weeks. A stamp
# costs one line and makes the pin mean what it says.
if [ "$(cat "$work/.tag" 2>/dev/null)" != "$mimalloc_tag" ]; then
    echo "build-mimalloc-mingw: fetching mimalloc $mimalloc_tag"
    rm -rf "$work/mimalloc" "$work/build"
    git clone --depth 1 --branch "$mimalloc_tag" https://github.com/microsoft/mimalloc.git \
        "$work/mimalloc"
    printf '%s' "$mimalloc_tag" > "$work/.tag"
fi

echo "build-mimalloc-mingw: building mimalloc for $target"
# MI_OVERRIDE off: on Windows the override machinery patches the CRT's malloc at
# load time, which is a different and much more invasive thing than linking an
# allocator. The engine calls mimalloc through operator new/delete instead -
# see src/core/allocator.cpp - which is explicit, portable, and does not depend
# on load order.
cmake -S "$work/mimalloc" -B "$work/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/toolchain-windows-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$sysroot" \
    -DMI_BUILD_SHARED=OFF \
    -DMI_BUILD_OBJECT=OFF \
    -DMI_BUILD_TESTS=OFF \
    -DMI_OVERRIDE=OFF \
    -DMI_WIN_REDIRECT=OFF >/dev/null
cmake --build "$work/build" >/dev/null
cmake --install "$work/build" >/dev/null

# FLATTENED INTO THE SYSROOT'S OWN lib/ AND include/.
#
# mimalloc installs into lib/mimalloc-<version>/ and include/mimalloc-<version>/,
# and `find_library` does not search versioned subdirectories - so the cross
# build reports "mimalloc not found" with the library sitting right there.
# MI_INSTALL_TOPLEVEL is supposed to prevent that and is not honoured by 2.1.9.
# Copying is version-proof and needs no guess about which release renamed which
# option.
# EVERY PREVIOUS COPY GOES FIRST. mimalloc names its static library differently
# across versions - v2 installs libmimalloc-static.a, v3 libmimalloc.a - so a
# bump leaves BOTH in the sysroot and find_library picks whichever it is asked
# for first. It picked the stale v2, and the Windows build quietly linked the
# old allocator across a major version while the Linux one used v3.
#
# tests/core_basics asks ctbrowser::allocator_version() and caught it, which is
# the entire reason that check exists.
rm -f "$sysroot"/lib/libmimalloc*.a

built="$(find "$work/build" -name 'libmimalloc*.a' -print -quit)"
header="$(find "$work/mimalloc/include" -name 'mimalloc.h' -print -quit)"
if [ -z "$built" ] || [ -z "$header" ]; then
    echo "build-mimalloc-mingw: build produced no library or header" >&2
    exit 1
fi
install -Dm644 "$built" "$sysroot/lib/$(basename "$built")"
install -Dm644 "$header" "$sysroot/include/mimalloc.h"

echo "build-mimalloc-mingw: installed into $sysroot"
echo "  $sysroot/lib/$(basename "$built")"
echo "  $sysroot/include/mimalloc.h"
