#!/usr/bin/env bash
# Build cpptrace for the llvm-mingw target.
#
# WHY: when a test dies, this is what says where. The llvm-mingw build has
# <format> but NOT <stacktrace> - checked, not assumed - so the Windows half of
# this project had no trace at all, and almost every expensive bug here has been
# the Windows-only kind: the `module` keyword, a missing <cstdlib>, the stale-exe
# mtime skew, the GPU adapter. Those are exactly the deaths with no location.
#
# cpptrace supports mingw explicitly (StackWalk64, libgcc _Unwind_Backtrace,
# dbghelp), which is why it is here rather than backward-cpp.
#
# TESTS ONLY. Nothing the engine ships links this; ctbrowser/cmake/modules/CTTest.cmake attaches
# it to the test executables and treats it as OPTIONAL - a missing trace makes a
# failure harder to read, not wrong.
#
# The result lands in the mingw sysroot beside SDL3, zlib, libpng,
# libjpeg-turbo, mimalloc and simdutf. That sysroot is gitignored and populated
# out-of-band, which is the established pattern this follows rather than
# invents - tools/mingw/build-simdutf-mingw.sh is its sibling and this file is
# deliberately shaped like it.
#
#   tools/mingw/build-cpptrace-mingw.sh [--clean]
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
    echo "build-cpptrace-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi
sysroot="$mingw/$target"

work="$HOME/projects/ctbrowser-cpptrace"
if [ "${1:-}" = "--clean" ]; then rm -rf "$work"; fi
mkdir -p "$work"

# --- THE VERSION IS PINNED -------------------------------------------------
# A cross build that silently follows upstream's default branch is how the
# Windows half of a byte-compared golden starts disagreeing with the Linux half
# for a reason nobody can see. Bump this deliberately.
#
# AND IT MATCHES BREW'S, which is where the Linux side gets its copy. A trace
# that disagrees between platforms is a diagnostic nobody trusts.
cpptrace_tag="v1.0.4"

# KEYED ON THE TAG, not merely on the checkout existing. `[ ! -d ... ]` clones
# once and then never updates, so bumping the pin above would change nothing and
# the sysroot would quietly keep the old major version - which is exactly how
# the clang toolchain sat three releases behind its own pin for weeks. A stamp
# costs one line and makes the pin mean what it says.
if [ "$(cat "$work/.tag" 2>/dev/null)" != "$cpptrace_tag" ]; then
    echo "build-cpptrace-mingw: fetching cpptrace $cpptrace_tag"
    rm -rf "$work/cpptrace" "$work/build"
    git clone --depth 1 --branch "$cpptrace_tag" https://github.com/jeremy-rifkin/cpptrace.git \
        "$work/cpptrace"
    printf '%s' "$cpptrace_tag" > "$work/.tag"
fi

echo "build-cpptrace-mingw: building cpptrace for $target"
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
cmake -S "$work/cpptrace" -B "$work/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/toolchains/windows-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$sysroot" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCPPTRACE_BUILD_TESTING=OFF >/dev/null
cmake --build "$work/build" >/dev/null
cmake --install "$work/build" >/dev/null

# cpptrace's own install puts the header at include/cpptrace/cpptrace.hpp and
# the library in lib/, which is where find_path and find_library look - no
# flattening needed, unlike mimalloc's versioned directories.
echo "build-cpptrace-mingw: installed into $sysroot"
found="$(find "$sysroot" -name 'libcpptrace*.a' -print -quit)"
header="$sysroot/include/cpptrace/cpptrace.hpp"
if [ -z "$found" ] || [ ! -f "$header" ]; then
    echo "  MISSING libcpptrace.a or cpptrace/cpptrace.hpp - tests will build without traces" >&2
    exit 1
fi
echo "  $found"
echo "  $header"
