#!/usr/bin/env bash
# Build the compiled Boost libraries for the llvm-mingw target: Boost.URL, which
# the engine links, and Boost.ProgramOptions, which the tools in ctbrowser/tools
# and the test programs that take arguments link.
#
# Everything else from Boost is header-only, but Boost.URL has been
# compiled-only since 1.87 (`boost/url/src.hpp` hard-errors with "src.hpp is
# discontinued"), and Boost.ProgramOptions parses the tools' and tests' command
# lines (ctbrowser/tools/support/options.hpp). See NOTICE and
# ctbrowser/docs/build.md.
#
# This does not use b2, Boost's build system, which would need a toolset
# definition for llvm-mingw and a full Boost source tree. Both libraries are
# plain .cpp files (66 for Boost.URL at 1.90, 19 at 1.92 once upstream folded
# most of it into headers; 11 for Boost.ProgramOptions) whose only dependencies
# are header-only Boost, so compiling them directly with the cross compiler is
# smaller and easier to follow.
#
# The result lands in the mingw sysroot beside the static SDL3, SDL3_ttf and
# plutosvg already there; that sysroot is gitignored and populated out-of-band.
#
#   tools/mingw/build-boost-mingw.sh [--clean]
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
    echo "build-boost-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi

# --- Header-only Boost, which the sources compile against. ----------------
# Prefer Homebrew's headers: the native build takes its Boost from brew, and
# using the same headers keeps the cross build on one release. The fixed brew
# paths are probed as well as PATH because a non-interactive ssh session does
# not have brew on PATH (tools/remote-build.sh does the same).
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

# --- The sources, pinned to the version of the headers. --------------------
# A library built from one release against another release's headers fails in
# silent ways, so the tag is read out of version.hpp (BOOST_LIB_VERSION "1_90"
# becomes boost-1.90.0) and every library below is cloned at that one tag.
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
    # C++17, above both libraries' own floor (C++11). The engine and its tests are
    # C++23 and link these fine: they meet at the ABI, and building third-party
    # code at the engine's language level gains nothing and risks deprecation
    # warnings.
    #
    # BOOST_ALL_NO_LIB stops the auto-linker naming a library MSVC would look for.
    #
    # The source search is recursive: Boost.URL keeps most of its sources under
    # src/detail, src/grammar and src/rfc, and a top-level-only build archives
    # cleanly but fails to link.
    #
    # Object names come from the path, not the basename: src/error.cpp and
    # src/rfc/detail/error.cpp would otherwise both be error.o, and the second
    # would silently replace the first.
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
    # Wait on each pid so a failed compile stops the script instead of being
    # archived around.
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
