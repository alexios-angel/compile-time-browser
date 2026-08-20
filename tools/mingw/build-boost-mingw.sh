#!/usr/bin/env bash
# Build the Boost libraries the engine LINKS, for the llvm-mingw target.
#
# WHY THIS EXISTS AT ALL. Boost was header-only here until 2026-07-31, precisely
# so there would be nothing to cross-compile. Boost.URL ended that: it has been
# compiled-only since 1.87 - `boost/url/src.hpp` hard-errors, "src.hpp is
# discontinued" - so there is no header-only way to have it, and RFC 3986 is not
# something to keep hand-rolling. See NOTICE and ctbrowser/docs/build.md.
#
# NOT b2, AND THAT IS THE POINT. Boost's own build system would want a toolset
# definition for llvm-mingw and a Boost source tree to run from. Boost.URL is 27
# self-contained .cpp files whose only dependencies are header-only Boost, so
# compiling them directly with the cross compiler is both smaller and far easier
# to see. Nothing here knows what a Jamfile is.
#
# The result lands in the mingw sysroot beside the static SDL3, SDL3_ttf and
# plutosvg that are already there - that sysroot is gitignored and populated
# out-of-band, which is the established pattern this follows rather than invents.
#
#   tools/mingw/build-boost-mingw.sh [--clean]
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
    echo "build-boost-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi

# --- header-only Boost, which the sources need to compile against ----------
# BREW'S FIRST when it has one. This tree moved its compiled dependencies onto
# brew on 2026-08-01 because apt's versions are the distribution's - noble
# packages `boost_url` separately from libboost-dev, so a box could have Boost
# installed and still fail to configure. Taking the headers from the same place
# the host library comes from also stops the cross build compiling one release
# against another's headers, which the tag check below exists to prevent.
# NOT `command -v brew`: on the devbox this runs over a non-interactive ssh,
# where brew is not on PATH and that test silently answers no - which is exactly
# how this script reported "no Boost include tree" on a box that had one.
# tools/remote-build.sh hardcodes the same path for the same reason.
brew_inc=""
for brew in "$(command -v brew 2>/dev/null || true)" /home/linuxbrew/.linuxbrew/bin/brew \
            /opt/homebrew/bin/brew; do
    if [ -n "$brew" ] && [ -x "$brew" ]; then
        brew_inc="$("$brew" --prefix)/include"
        break
    fi
done
for root in "${BOOST_INC:-}" "$brew_inc" "$HOME/projects/boost-inc"; do
    if [ -n "$root" ] && [ -f "$root/boost/version.hpp" ]; then
        boost_inc="$root"
        break
    fi
done
if [ -z "${boost_inc:-}" ]; then
    echo "build-boost-mingw: no Boost include tree. Set BOOST_INC." >&2
    exit 1
fi

# --- the sources, PINNED TO THE VERSION THE HEADERS ARE ---------------------
# A library built from one release against another release's headers is a
# silent-mismatch bug of exactly the kind this repository keeps finding, so the
# tag is read out of version.hpp rather than assumed.
version="$(sed -n 's/^#define BOOST_LIB_VERSION "\(.*\)"/\1/p' "$boost_inc/boost/version.hpp")"
tag="boost-${version//_/.}.0"
src="$HOME/projects/boost-url-src"
if [ "${1:-}" = "--clean" ]; then rm -rf "$src"; fi
if [ ! -d "$src" ]; then
    echo "build-boost-mingw: fetching Boost.URL $tag"
    git clone --depth 1 --branch "$tag" https://github.com/boostorg/url.git "$src"
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "build-boost-mingw: compiling Boost.URL $tag for $target"
# C++17: Boost.URL's own floor. The ENGINE is C++23 and links this happily -
# they meet at the ABI, and building third-party code at our language level buys
# nothing and risks deprecation noise.
#
# BOOST_URL_SOURCE builds the definitions rather than declaring them imported;
# BOOST_ALL_NO_LIB stops the auto-linker naming a library MSVC would look for.
#
# RECURSIVE, and that is not a detail. `src/*.cpp` finds 27 files; the library is
# 66, with the rest under src/detail, src/grammar and src/rfc. Building only the
# top level produced an archive that LOOKED fine - 27 objects, ar was happy - and
# failed at link with undefined url_impl symbols. `llvm-nm` showed them undefined
# inside the archive itself, which is the tell.
#
# OBJECT NAMES COME FROM THE PATH, not the basename: src/error.cpp and
# src/rfc/detail/error.cpp would otherwise be the same .o, and the second would
# silently replace the first.
mapfile -t sources < <(find "$src/src" -name '*.cpp' | sort)
echo "  ${#sources[@]} sources"
pids=()
for source in "${sources[@]}"; do
    object="$work/$(realpath --relative-to="$src/src" "$source" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    "$mingw/bin/$target-clang++" -std=gnu++17 -O2 -DNDEBUG \
        -DBOOST_URL_SOURCE -DBOOST_URL_STATIC_LINK -DBOOST_ALL_NO_LIB \
        -isystem "$boost_inc" -isystem "$src/include" \
        -c "$source" -o "$object" &
    pids+=("$!")
done
# EVERY exit code checked. The first version of this loop was `... & done; wait`,
# which discards them - a failed compile would have been archived around in
# silence and shown up as a link error somewhere else entirely.
failed=0
for pid in "${pids[@]}"; do
    wait "$pid" || failed=1
done
if [ "$failed" -ne 0 ]; then
    echo "build-boost-mingw: a source failed to compile - see above" >&2
    exit 1
fi

install_dir="$mingw/$target/lib"
mkdir -p "$install_dir"
"$mingw/bin/llvm-ar" rcs "$install_dir/libboost_url.a" "$work"/*.o
echo "build-boost-mingw: wrote $install_dir/libboost_url.a"
"$mingw/bin/llvm-ar" t "$install_dir/libboost_url.a" | wc -l | xargs echo "  objects:"
ls -la "$install_dir/libboost_url.a"
