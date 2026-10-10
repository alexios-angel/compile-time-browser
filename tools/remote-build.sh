#!/usr/bin/env bash
# Sync the working tree (incl. submodules) to the shared devbox and run the
# build there. Usage:
#   ./remote-build.sh [ninja target ...]   default preset: configure + build
#                                          + ctest (Ninja); with targets, build
#                                          only those and run no tests
#   ./remote-build.sh windows              `windows` preset: cross-compile
#                                          the examples, collect exes+SDL3.dll
#                                          via windows-dist, rsync them back
#                                          into examples-windows/
#
# The configure root is ctbrowser/, not the repository root: the repository is
# a monorepo with ctbrowser/ and ctcompile/ as sibling projects (the LLVM
# model), so CMakePresets.json lives in ctbrowser/ and every cmake and ctest
# invocation below runs from there. The build trees still land at the
# repository root (build/, build-windows/, ...), where the other tools look.
#
# The `default` preset builds both projects: it sets
# CTBROWSER_ENABLE_PROJECTS=ctcompile. There is no CI, so this script is the
# only full build and test run; a sibling project it skipped would break
# unnoticed. `browser` and `browser-no-llvm` are the engine-only configurations.
#
# The box is github.com/alexios-angel/infra (sibling checkout ../infra),
# reached via the `devbox` ssh alias that `../infra/azure-build-server/
# server.sh ssh-config` writes; DEVBOX_HOST=ubuntu@<ip> overrides. Project
# deps the box deliberately doesn't ship (the pinned clang-std-embed
# toolchain) are converged here, not in the box's cloud-init.
set -euo pipefail
cd "$(dirname "$0")"

host="${DEVBOX_HOST:-devbox}"
# The directory on the box. Give each worktree its own: the rsync below
# carries --delete, so two checkouts syncing to one directory would erase each
# other's tree mid-compile.
#
#   DEVBOX_DIR=projects/ctbrowser-lexical ./tools/remote-build.sh
remote_dir="${DEVBOX_DIR:-projects/compile-time-browser}"

# The pinned clang-std-embed toolchain release (from the embed repo). There is
# no CI, so this is the only place the pin lives. sync-to-ctbrowser.sh may
# install a locally built one instead; the rsync protect filter keeps whichever
# is on the server.
CLANG_STD_EMBED_TAG="${CLANG_STD_EMBED_TAG:-clang-std-embed-e3986d225}"
CLANG_STD_EMBED_RELEASE="${CLANG_STD_EMBED_RELEASE:-https://github.com/alexios-angel/embed/releases/download/${CLANG_STD_EMBED_TAG}/${CLANG_STD_EMBED_TAG}-linux-x86_64.tar.xz}"

# ANGLE is on for a devbox build and off everywhere else. The option is a
# FATAL_ERROR when third-party/angle/ is missing, which is right for a developer
# who has not fetched it, so it is set here, where fetch-angle.sh has just run,
# rather than in the shared `default` preset. Without it gl_basics prints SKIP
# and the render-angle-* tests are never registered, so the suite passes without
# testing WebGL. CTBROWSER_ANGLE=OFF opts out.
CTBROWSER_ANGLE="${CTBROWSER_ANGLE:-ON}"

if ! ssh -o ConnectTimeout=5 "$host" true 2>/dev/null; then
  cat >&2 <<EOF
cannot reach '$host' — likely one of:
  alias missing:    ../infra/azure-build-server/server.sh ssh-config
  box deallocated:  ../infra/azure-build-server/server.sh start
  your IP changed:  ../infra/azure-build-server/server.sh allow-ip
or set DEVBOX_HOST=ubuntu@<ip>.
EOF
  exit 1
fi

# Use the devbox CPU count, not the local machine's. Explicit CTest jobs also
# work with CTest 3.28, where the preset's jobs=0 still runs tests serially.
# The shared devbox lock serializes build sessions; either pool can be capped.
devbox_jobs="$(ssh "$host" nproc)"
build_jobs="${CT_BUILD_JOBS:-$devbox_jobs}"
test_jobs="${CT_TEST_JOBS:-$devbox_jobs}"
printf 'devbox: %s CPUs; build jobs=%s; CTest jobs=%s\n' "$devbox_jobs" "$build_jobs" "$test_jobs"

repo_root=$(git rev-parse --show-toplevel)

# rsync the whole tree including submodule checkouts; leave remote build
# artifacts in place so incremental builds survive a sync.
#
# Exclude every build*/ tree (build-windows/, build-asan/, ...): a CMake cache
# holds absolute paths from the machine that wrote it, and cmake refuses a
# cache copied from elsewhere. tools/clang-std-embed stays local: the
# server-side copy is converged below.
#
# Directories created on the box itself are excluded and protected from
# --delete, so a sync from a checkout that lacks them does not wipe them:
# third-party/angle/ (~34 MB fetched by tools/fetch-angle.sh from a pinned
# release) and tools/.venv/ (~400 MB with Playwright's browsers, created by
# `tools/check/compare.py setup`; losing it makes css-parity.py report
# "playwright not installed"). Windows toolchains and packaged executables
# are cached build assets too.
#
# Switching worktrees can replace a file with an older version than the last
# build saw. --no-times gives changed files fresh destination mtimes so Ninja
# rebuilds them; --checksum leaves identical files (and their mtimes) alone.
rsync -az --checksum --no-times --delete \
  --exclude '.git/' \
  --exclude '.claude/' \
  --exclude 'build*/' \
  --exclude 'tools/clang-std-embed/' \
  --exclude 'tools/llvm-mingw/' \
  --exclude 'examples-windows/' \
  --exclude 'tools/.venv/' \
  --exclude 'third-party/angle/' \
  --exclude '*.d' \
  --filter 'protect *.pch' --filter 'protect *.gch' --filter 'protect build*/' \
  --filter 'protect tools/clang-std-embed/' --filter 'protect third-party/angle/' \
  --filter 'protect tools/.venv/' \
  --filter 'protect tools/llvm-mingw/' --filter 'protect examples-windows/' \
  --filter 'protect *.d' \
  "$repo_root"/ "$host:$remote_dir/"

# Converge project-owned deps on the box: the compiled ones ride in
# tools/Brewfile, and a box without linuxbrew gets none of them.
ssh "$host" CLANG_STD_EMBED_RELEASE="$CLANG_STD_EMBED_RELEASE" CT_REMOTE_DIR="$remote_dir" 'bash -s' <<'REMOTE'
set -euo pipefail
BREW=/home/linuxbrew/.linuxbrew/bin/brew
if [ -x "$BREW" ]; then
  export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_ENV_HINTS=1
  "$BREW" bundle check --file="$HOME/$CT_REMOTE_DIR/tools/Brewfile" >/dev/null 2>&1 \
    || "$BREW" bundle install --file="$HOME/$CT_REMOTE_DIR/tools/Brewfile"
fi
tool="$HOME/$CT_REMOTE_DIR/tools/clang-std-embed"
# Reinstall whenever the pinned release changes, not only when the binary is
# missing; the .release stamp records which release is installed.
if [ "$(cat "$tool/.release" 2>/dev/null)" != "$CLANG_STD_EMBED_RELEASE" ]; then
  rm -rf "$tool"
  mkdir -p "$tool"
  curl -fsSL --retry 5 "$CLANG_STD_EMBED_RELEASE" | tar -xJ --strip-components=1 -C "$tool"
  printf '%s' "$CLANG_STD_EMBED_RELEASE" > "$tool/.release"
fi
# ANGLE, from the pinned release. fetch-angle.sh checks its own release stamp,
# so this is a no-op once the box is at the pin. It runs here rather than beside
# the cmake line because the Windows path needs it too, and a failed fetch must
# stop the run before configure.
cd "$HOME/$CT_REMOTE_DIR" && tools/fetch-angle.sh
REMOTE

if [ "${1:-}" = windows ]; then
  # The cross build needs compiled libraries in the mingw sysroot that the box
  # does not ship (Boost.URL, Boost.ProgramOptions, zlib, libpng,
  # libjpeg-turbo, mimalloc, simdutf, cpptrace), so the two idempotent builder
  # scripts run first.
  #
  # ~/projects/boost-inc is the isolated Boost include dir the cross toolchain
  # wants: cmake/toolchains/windows-x86_64.cmake puts it on the cross compile's
  # -isystem path, so it must hold a boost/ symlink and nothing else. Pointing
  # it at brew's whole include/ would put the host's SDL3, libpng and zlib
  # headers in front of the Windows ones.
  ssh "$host" 'inc="$HOME/projects/boost-inc"; mkdir -p "$inc";
    [ -e "$inc/boost" ] || ln -s /home/linuxbrew/.linuxbrew/include/boost "$inc/boost";
    ls "$inc/boost/version.hpp" >/dev/null'
  ssh "$host" "cd $remote_dir && tools/mingw/build-libs-mingw.sh && tools/mingw/build-boost-mingw.sh"
  ssh "$host" "cd $remote_dir/ctbrowser && cmake --preset windows -DCTBROWSER_WITH_ANGLE=$CTBROWSER_ANGLE && cmake --build --preset windows && cmake --build --preset windows --target windows-dist"
  rsync -az "$host:$remote_dir/examples-windows/" "$repo_root/examples-windows/"
  echo "examples-windows/ refreshed from the devbox"
else
  # Pass the brew prefix as CMAKE_PREFIX_PATH. The -isystem include path is
  # enough for header-plus-library dependencies, but SDL3 and SDL3_ttf ship
  # CMake config packages that find_package cannot see without it; a missing
  # SDL3_ttf silently falls back to the font8x8 bitmap font and skews every
  # text measurement.
  prefix=""
  if ssh "$host" '[ -d /home/linuxbrew/.linuxbrew ]'; then
    prefix="-DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew"
  fi
  # CTCOMPILE_ENABLE_MLIR defaults to OFF so a runtime-only build needs no
  # LLVM, and CMakePresets.json is shared with machines that have none. The
  # devbox has LLVM, so turn it on here; otherwise ctcompile's dialect,
  # importer, EmitC backend and their lit/differential tests are silently
  # skipped. CTCOMPILE_MLIR=OFF opts out.
  configure="cmake --preset default -DCTBROWSER_WITH_ANGLE=$CTBROWSER_ANGLE \
    -DCTCOMPILE_ENABLE_MLIR=${CTCOMPILE_MLIR:-ON} $prefix"
  if [ $# -gt 0 ]; then
    ssh "$host" "cd $remote_dir/ctbrowser && $configure && cmake --build --preset default -j$build_jobs --target $*"
  else
    ssh "$host" "cd $remote_dir/ctbrowser && $configure && cmake --build --preset default -j$build_jobs && ctest --preset default -j$test_jobs"
  fi
fi
