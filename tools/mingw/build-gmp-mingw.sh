#!/usr/bin/env bash
# Build GNU GMP for the llvm-mingw target, for the OPTIONAL BigInt backend.
#
# READ THIS BEFORE RUNNING IT. Nothing needs this script. `CTBROWSER_WITH_GMP`
# is OFF by default and the engine's BigInt runs on Boost.Multiprecision's
# header-only cpp_int, which needs no sysroot entry at all. This file exists so
# the option is REAL rather than theoretical, and because "will GMP even
# cross-compile?" deserved an answer that was tested instead of guessed.
#
# IT WILL, and with its assembly enabled - which is the surprise. The usual
# advice is that GMP needs --disable-assembly under a clang-based mingw; on this
# toolchain llvm's integrated assembler takes GMP's hand-written x86-64 .asm
# unmodified, and the resulting static .exe runs.
#
# --- THE LICENCE, WHICH IS THE REASON THIS IS OPT-IN ------------------------
# GMP is LGPLv3+ OR GPLv2+. ctbrowser is Apache-2.0 with LLVM exceptions and
# ships SELF-CONTAINED, STATICALLY LINKED .exe files (ctbrowser/docs/platform.md). Static
# LGPL linking is permitted but carries obligations the rest of this tree's
# dependencies do not - notably giving recipients what they need to relink
# against a modified GMP. That is a distribution decision, not a build flag,
# which is why no amount of "GMP is installed" turns this on by itself.
# NOTICE records it. The one other LGPL item in this repository is p5.js, and
# it is test data that nothing links.
#
# --- AND IT IS SLOWER FOR THIS WORKLOAD -------------------------------------
# Measured on a Core Ultra 9 185H, cpp_int against GMP, both compiled for the
# same modern arch (numbers and method in ctbrowser/docs/script.md):
#
#                    64 bits        1024 bits      65536 bits
#   Linux            2.9x slower    1.8x faster    20x faster (to-string)
#   Windows          5.5x slower    1.7x faster    39x faster (to-string)
#
# A JavaScript BigInt is an id, a nanosecond timestamp or a 64-bit hash, so the
# LEFT column is the one that describes this engine. GMP loses there because
# every mpz_t is a heap allocation while cpp_int keeps a small value inline -
# and the Windows gap is wider because its CRT allocator is the slower, the same
# effect that made mimalloc worth 11.7% there.
#
# Tuning does not rescue it: GMP 6.3.0's config.guess reads that CPU as
# `nehalem` (2008), and building it correctly for `alderlake` instead moved the
# 64-bit numbers not at all. The cost is allocation, not instruction selection.
#
# GMP is the right tool for cryptography and for numbers thousands of bits wide.
# It is the wrong default for JavaScript's BigInt, and this script says so
# rather than leaving the next person to find out.
#
#   tools/mingw/build-gmp-mingw.sh [--clean]
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
    echo "build-gmp-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi
sysroot="$mingw/$target"

work="$HOME/projects/ctbrowser-gmp"
if [ "${1:-}" = "--clean" ]; then rm -rf "$work"; fi
mkdir -p "$work"

# --- THE VERSION IS PINNED, AND THE TARBALL IS VERIFIED ---------------------
# Same argument as every other pin here: a Windows half that silently follows
# upstream is how a byte-compared golden starts disagreeing for a reason nobody
# can see. This one is a plain tarball over the network rather than a git tag,
# so the checksum is not optional - it is the only thing that makes the pin
# mean anything.
gmp_version="6.3.0"
gmp_sha256="a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"
tarball="gmp-${gmp_version}.tar.xz"

if [ "$(cat "$work/.version" 2>/dev/null)" != "$gmp_version" ]; then
    echo "build-gmp-mingw: fetching GMP $gmp_version"
    rm -rf "$work/gmp-$gmp_version" "$work/build" "$work/$tarball"
    # gmplib.org is frequently unreachable; the GNU mirrors carry the identical
    # tarball and the checksum below is what decides whether it is identical.
    for url in "https://ftp.gnu.org/gnu/gmp/$tarball" \
               "https://mirrors.kernel.org/gnu/gmp/$tarball" \
               "https://gmplib.org/download/gmp/$tarball"; do
        if curl -fsSL --connect-timeout 20 -o "$work/$tarball" "$url"; then break; fi
        echo "build-gmp-mingw: $url did not answer, trying the next mirror" >&2
    done
    if [ ! -f "$work/$tarball" ]; then
        echo "build-gmp-mingw: could not download $tarball from any mirror" >&2
        exit 1
    fi
    got="$(sha256sum "$work/$tarball" | cut -d' ' -f1)"
    if [ "$got" != "$gmp_sha256" ]; then
        echo "build-gmp-mingw: CHECKSUM MISMATCH for $tarball" >&2
        echo "  expected $gmp_sha256" >&2
        echo "  got      $got" >&2
        rm -f "$work/$tarball"
        exit 1
    fi
    tar xf "$work/$tarball" -C "$work"
    printf '%s' "$gmp_version" > "$work/.version"
fi

rm -rf "$work/build"
mkdir -p "$work/build"
cd "$work/build"

echo "build-gmp-mingw: configuring GMP $gmp_version for $target"

# --- TWO AUTOTOOLS TRAPS, BOTH FOUND THE HARD WAY ---------------------------
#
# 1. --build IS PASSED EXPLICITLY, and must be. Under WSL, binfmt_misc runs
#    Windows .exe files, so configure's cross-compile probe COMPILES a .exe,
#    RUNS it successfully, and concludes it is not cross-compiling at all. It
#    then fails a few lines later with "cannot determine executable suffix".
#    Naming --build makes the answer a fact rather than an experiment, and
#    costs nothing on an ordinary Linux box that would have got it right.
#
# 2. CC_FOR_BUILD MUST BE A NATIVE COMPILER. GMP builds little generator
#    programs and RUNS them during the build. Left alone it uses $CC - the
#    mingw one - and the build dies trying to execute what it just produced.
#
# --enable-fat: runtime CPU dispatch. GMP's whole speed argument is its
# per-microarchitecture assembly, but a SHIPPED .exe has to start on whatever
# machine opens it, and a binary tuned for the builder's CPU is one that
# crashes with an illegal instruction on an older one. Fat keeps every variant
# and chooses on startup, which is what a distributed binary needs and what
# distributions themselves ship. Build for a fixed CPU only if the target is
# known and private.
../gmp-"$gmp_version"/configure \
    --build="$(uname -m)-pc-linux-gnu" \
    --host="$target" \
    --prefix="$work/install" \
    --disable-shared --enable-static --with-pic --enable-fat \
    CC="$mingw/bin/$target-clang" \
    CXX="$mingw/bin/$target-clang++" \
    AR="$mingw/bin/llvm-ar" \
    RANLIB="$mingw/bin/llvm-ranlib" \
    NM="$mingw/bin/llvm-nm" \
    CC_FOR_BUILD=cc \
    CPP_FOR_BUILD="cc -E" \
    CFLAGS="-O2 -fomit-frame-pointer"

make -j"$(nproc)"
make install

# libtool names the archive gmp.lib or libgmp.a depending on how it read the
# host triple; both are ordinary COFF archives and CMake's find_library is
# given both spellings. Install whichever appeared under the name the rest of
# the sysroot uses.
install -d "$sysroot/include" "$sysroot/lib"
install -m 644 "$work/install/include/gmp.h" "$sysroot/include/gmp.h"
if [ -f "$work/install/lib/libgmp.a" ]; then
    install -m 644 "$work/install/lib/libgmp.a" "$sysroot/lib/libgmp.a"
elif [ -f "$work/install/lib/gmp.lib" ]; then
    install -m 644 "$work/install/lib/gmp.lib" "$sysroot/lib/libgmp.a"
else
    echo "build-gmp-mingw: the build produced no static library" >&2
    exit 1
fi

echo "build-gmp-mingw: wrote $sysroot/lib/libgmp.a"
ls -la "$sysroot/lib/libgmp.a"
echo
echo "Configure with -DCTBROWSER_WITH_GMP=ON to use it. It is OFF by default:"
echo "GMP is LGPL and this engine links statically - see NOTICE - and cpp_int"
echo "is the FASTER backend for BigInt-sized numbers. See the top of this file."
