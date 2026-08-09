#!/usr/bin/env bash
# Build libshaderc for the llvm-mingw target.
#
# WHY: to compile a page's GLSL to SPIR-V at RUN TIME, so a Vulkan device can
# execute it instead of the software interpreter. The interpreter is 1.03 M
# fragments per second (tests/glsl_basics --bench, on p5's own lightTextureFrag),
# which is about 120 ms for one full-screen pass at 420x300 - a ceiling no amount
# of tuning lifts. docs/gpu-shaders-plan.md has the measurements and the staging.
#
# FROM A PACKAGING FORK, github.com/alexios-angel/shaderc, and the distinction
# matters: that fork carries NO compiler changes. Stock shaderc compiles a
# WebGL 2 shader with -std=310es and the auto-map flags, so the ES-version rule
# that looks like a blocker is not one. What the fork adds is a cache that builds
# the LIBRARY ALONE - no glslc, no tests, no examples, no HLSL front end - which
# is the difference between linking a compiler and shipping one.
#
# STATIC, like the SDL3, zlib, libpng, libjpeg-turbo, mimalloc, simdutf and
# cpptrace already in that sysroot: an application should be one .exe.
#
# IT IS THE BIGGEST OF THEM BY A WIDE MARGIN - glslang and SPIRV-Tools together
# are a real compiler - so this takes minutes rather than seconds, and it is
# skipped when the pin has not moved.
#
#   tools/mingw/build-shaderc-mingw.sh [--clean]
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
    echo "build-shaderc-mingw: no llvm-mingw found. Set LLVM_MINGW to its root." >&2
    exit 1
fi
sysroot="$mingw/$target"

work="$HOME/projects/ctbrowser-shaderc"
if [ "${1:-}" = "--clean" ]; then rm -rf "$work"; fi
mkdir -p "$work"

# --- THE PIN IS A COMMIT, not a branch ------------------------------------
# A branch name would let the compiler under this engine change without anything
# in this repository changing, which is the one thing a pin exists to prevent.
# shaderc's own DEPS pins glslang, SPIRV-Tools and SPIRV-Headers to exact
# revisions in turn, so this single hash fixes the whole stack.
shaderc_repo="https://github.com/alexios-angel/shaderc.git"
shaderc_commit="935ed60ab600a6061dcf70ebe8e33ab30bc2168d"

# Keyed on the commit rather than on the checkout existing: cloning once and
# never updating is how a pin quietly stops meaning anything - the trap the
# clang toolchain sat in for weeks.
if [ "$(cat "$work/.commit" 2>/dev/null)" != "$shaderc_commit" ]; then
    echo "build-shaderc-mingw: fetching shaderc $shaderc_commit"
    rm -rf "$work/shaderc" "$work/build"
    git clone --filter=blob:none "$shaderc_repo" "$work/shaderc"
    git -C "$work/shaderc" checkout --quiet "$shaderc_commit"
    # THE DEPENDENCIES COME FROM DEPS, at the revisions it names. This is the
    # one network step and it is why the checkout is cached: a build that
    # fetches a compiler every time is not a build anyone runs twice.
    python3 "$work/shaderc/utils/git-sync-deps"
    printf '%s' "$shaderc_commit" > "$work/.commit"
fi

echo "build-shaderc-mingw: building libshaderc for $target (this takes a few minutes)"
cmake -S "$work/shaderc" -B "$work/build" -G Ninja \
    -C "$work/shaderc/ctbrowser/static-release.cmake" \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/toolchain-windows-x86_64.cmake" \
    -DCMAKE_INSTALL_PREFIX="$sysroot" >/dev/null
cmake --build "$work/build" --target shaderc >/dev/null
cmake --install "$work/build" >/dev/null 2>&1 || true

# INSTALLED BY HAND IF NEED BE. shaderc's install target pulls in the tools this
# build deliberately did not make, so it can fail with the library sitting there
# built - which is a confusing way to lose a good build.
find "$work/build" -name 'libshaderc*.a' -exec cp {} "$sysroot/lib/" \; 2>/dev/null || true
find "$work/build" -name 'libglslang*.a' -o -name 'libSPIRV*.a' -o -name 'libMachineIndependent*.a' \
    -o -name 'libGenericCodeGen*.a' -o -name 'libOSDependent*.a' -o -name 'libOGLCompiler*.a' \
    | while read -r lib; do cp "$lib" "$sysroot/lib/" 2>/dev/null || true; done
mkdir -p "$sysroot/include/shaderc"
cp "$work/shaderc/libshaderc/include/shaderc/"*.h* "$sysroot/include/shaderc/" 2>/dev/null || true

echo "build-shaderc-mingw: installed into $sysroot"
found="$(find "$sysroot" -name 'libshaderc.a' -print -quit)"
if [ -z "$found" ]; then
    echo "  MISSING libshaderc.a - the configure step will fail" >&2
    exit 1
fi
echo "  $found"
