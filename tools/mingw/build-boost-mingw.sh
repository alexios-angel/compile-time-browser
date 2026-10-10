#!/usr/bin/env bash
# Build the COMPILED Boost libraries for the llvm-mingw target: Boost.URL, which
# the engine LINKS, and Boost.ProgramOptions, which the tools in ctbrowser/tools
# and the test programs that take arguments link.
#
# WHY THIS EXISTS AT ALL. Boost was header-only here until 2026-07-31, precisely
# so there would be nothing to cross-compile. Boost.URL ended that: it has been
# compiled-only since 1.87 - `boost/url/src.hpp` hard-errors, "src.hpp is
# discontinued" - so there is no header-only way to have it, and RFC 3986 is not
# something to keep hand-rolling. Boost.ProgramOptions followed on 2026-10-09,
# for the tools' and tests' command lines (ctbrowser/tools/support/options.hpp).
# See NOTICE and ctbrowser/docs/build.md.
#
# NOT b2, AND THAT IS THE POINT. Boost's own build system would want a toolset
# definition for llvm-mingw and a Boost source tree to run from. Both libraries
# are self-contained .cpp files whose only dependencies are header-only Boost (66
# for Boost.URL at 1.90, 19 at 1.92 once upstream folded most of it into headers;
# 11 for Boost.ProgramOptions), so compiling them directly with the cross
# compiler is both smaller and far easier to see. Nothing here knows what a
# Jamfile is.
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
# tag is read out of version.hpp rather than assumed, and every library below is
# cloned at that one tag.
version="$(sed -n 's/^#define BOOST_LIB_VERSION "\(.*\)"/\1/p' "$boost_inc/boost/version.hpp")"
tag="boost-${version//_/.}.0"
clean="${1:-}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

install_dir="$mingw/$target/lib"
mkdir -p "$install_dir"

# build_library <boostorg repository> <its -D flags>...: clone the repository at
# $tag into $HOME/projects/boost-<repository>-src unless it is already there
# (--clean fetches it again), compile every source under its src/ and archive
# them as libboost_<repository>.a in the sysroot.
build_library() {
    local lib="$1"
    shift
    local src="$HOME/projects/boost-$lib-src" objects="$work/$lib"
    if [ "$clean" = "--clean" ]; then rm -rf "$src"; fi
    if [ ! -d "$src" ]; then
        echo "build-boost-mingw: fetching boostorg/$lib $tag"
        git clone --depth 1 --branch "$tag" "https://github.com/boostorg/$lib.git" "$src"
    fi
    mkdir -p "$objects"

    echo "build-boost-mingw: compiling boostorg/$lib $tag for $target"
    # C++17, above both libraries' own floor (C++11). The ENGINE and its tests are
    # C++23 and link these happily - they meet at the ABI, and building third-party
    # code at our language level buys nothing and risks deprecation noise.
    #
    # BOOST_ALL_NO_LIB stops the auto-linker naming a library MSVC would look for.
    #
    # RECURSIVE, and that is not a detail. Boost.URL's `src/*.cpp` finds 27 files;
    # the library is 66, with the rest under src/detail, src/grammar and src/rfc.
    # Building only the top level produced an archive that LOOKED fine - 27
    # objects, ar was happy - and failed at link with undefined url_impl symbols.
    # `llvm-nm` showed them undefined inside the archive itself, which is the tell.
    #
    # OBJECT NAMES COME FROM THE PATH, not the basename: src/error.cpp and
    # src/rfc/detail/error.cpp would otherwise be the same .o, and the second would
    # silently replace the first.
    local sources source object pid failed=0 pids=()
    mapfile -t sources < <(find "$src/src" -name '*.cpp' | sort)
    echo "  ${#sources[@]} sources"
    for source in "${sources[@]}"; do
        object="$(realpath --relative-to="$src/src" "$source" | tr '/' '_' | sed 's/\.cpp$/.o/')"
        "$mingw/bin/$target-clang++" -std=gnu++17 -O2 -DNDEBUG \
            "$@" -DBOOST_ALL_NO_LIB \
            -isystem "$boost_inc" -isystem "$src/include" \
            -c "$source" -o "$objects/$object" &
        pids+=("$!")
    done
    # EVERY exit code checked. The first version of this loop was `... & done; wait`,
    # which discards them - a failed compile would have been archived around in
    # silence and shown up as a link error somewhere else entirely.
    for pid in "${pids[@]}"; do
        wait "$pid" || failed=1
    done
    if [ "$failed" -ne 0 ]; then
        echo "build-boost-mingw: a source failed to compile - see above" >&2
        exit 1
    fi

    local archive="$install_dir/libboost_$lib.a"
    "$mingw/bin/llvm-ar" rcs "$archive" "$objects"/*.o
    echo "build-boost-mingw: wrote $archive"
    "$mingw/bin/llvm-ar" t "$archive" | wc -l | xargs echo "  objects:"
    ls -la "$archive"
}

# BOOST_URL_SOURCE builds the definitions rather than declaring them imported,
# and BOOST_PROGRAM_OPTIONS_SOURCE likewise; the _STATIC_LINK beside each is what
# the library's own CMake defines for a static build.
build_library url -DBOOST_URL_SOURCE -DBOOST_URL_STATIC_LINK
build_library program_options -DBOOST_PROGRAM_OPTIONS_SOURCE -DBOOST_PROGRAM_OPTIONS_STATIC_LINK
