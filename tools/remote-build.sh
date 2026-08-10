#!/usr/bin/env bash
# Sync the working tree (incl. submodules) to the shared devbox and run the
# build there. Usage:
#   ./remote-build.sh [ninja target ...]   default preset: configure + build
#                                          + ctest (Ninja)
#   ./remote-build.sh windows              windows-fetch preset: cross-compile
#                                          the examples, collect exes+SDL3.dll
#                                          via windows-dist, rsync them back
#                                          into examples-windows/
#
# The box is github.com/alexios-angel/infra (sibling checkout ../infra),
# reached via the `devbox` ssh alias that `../infra/azure-build-server/
# server.sh ssh-config` writes; DEVBOX_HOST=ubuntu@<ip> overrides. Project
# deps the box deliberately doesn't ship (the pinned clang-std-embed
# toolchain) are converged here, not in the box's cloud-init.
set -euo pipefail
cd "$(dirname "$0")"

host="${DEVBOX_HOST:-devbox}"
# The embed repo's pinned toolchain release. sync-to-ctbrowser.sh may install a
# locally-built one instead — the rsync protect filter keeps whichever is on the
# server. This used to say "same knob and default as CI"; there is no CI any
# more (deleted 2026-08-08), so this script IS the pin.
CLANG_STD_EMBED_TAG="${CLANG_STD_EMBED_TAG:-clang-std-embed-e3986d225}"
CLANG_STD_EMBED_RELEASE="${CLANG_STD_EMBED_RELEASE:-https://github.com/alexios-angel/embed/releases/download/${CLANG_STD_EMBED_TAG}/${CLANG_STD_EMBED_TAG}-linux-x86_64.tar.xz}"

# ANGLE IS ON FOR A DEVBOX BUILD, and stays off everywhere else. The option
# FATAL_ERRORs when third_party/angle/ is missing, which is the right answer for
# a developer who has not fetched - so it is set HERE, where fetch-angle.sh has
# just run, rather than in the `default` preset everyone shares. Without it the
# box builds a binary in which gl_basics prints SKIP and the render-angle-*
# tests are never registered: a green suite that measured nothing.
# CTBROWSER_ANGLE=OFF opts out.
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

repo_root=$(git rev-parse --show-toplevel)

# `build*/`, NOT `build/`: the presets here write build-windows/, build-asan/
# and build-tsan/ beside it, and only the first was excluded - so a local
# build-windows/ was copied UP to the box, cache and all, and cmake refused it
# with "the current CMakeCache.txt directory is different than the directory
# where CMakeCache.txt was created". A cache is full of absolute paths from the
# machine that wrote it and never belongs on another one.
#
# rsync the whole tree including submodule checkouts; leave remote build
# artifacts in place so the PCH bake is reused across syncs.
# tools/clang-std-embed stays local: the server-side copy is converged below.
#
# third_party/angle/ is FETCHED ON THE BOX, never pushed. It is ~34 MB of
# libraries that tools/fetch-angle.sh downloads from a pinned release, so
# sending them up every sync is pure wire time - and without the protect
# filter `--delete` would remove the box's copy on the first run from a
# checkout that has not fetched, which is a rebuild rather than a re-sync.
# tools/.venv/ is the same story as angle/: created ON THE BOX by
# `tools/check/compare.py setup`, ~400 MB with Playwright's browsers behind it,
# and gitignored - so it exists on one side or the other but never both. Without
# the protect filter `--delete` removed it on the next sync, and the failure lands
# nowhere near the cause: the build succeeds and then css-parity.py says
# "playwright not installed" about a venv that was there a minute ago.
rsync -az --delete \
  --exclude '.git/' \
  --exclude 'build*/' \
  --exclude 'tools/clang-std-embed/' \
  --exclude 'tools/.venv/' \
  --exclude 'third_party/angle/' \
  --exclude '*.d' \
  --filter 'protect *.pch' --filter 'protect *.gch' --filter 'protect build*/' \
  --filter 'protect tools/clang-std-embed/' --filter 'protect third_party/angle/' \
  --filter 'protect tools/.venv/' \
  --filter 'protect *.d' \
  "$repo_root"/ "$host:projects/compile-time-browser/"

# Converge project-owned deps on the box: brew-only deps ride in
# tools/Brewfile (glm >= 1.0 for constexpr math); apt glm is the
# brew-less fallback (everything but the constexpr-math tests).
ssh "$host" CLANG_STD_EMBED_RELEASE="$CLANG_STD_EMBED_RELEASE" 'bash -s' <<'REMOTE'
set -euo pipefail
BREW=/home/linuxbrew/.linuxbrew/bin/brew
if [ -x "$BREW" ]; then
  export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_ENV_HINTS=1
  "$BREW" bundle check --file="$HOME/projects/compile-time-browser/tools/Brewfile" >/dev/null 2>&1 \
    || "$BREW" bundle install --file="$HOME/projects/compile-time-browser/tools/Brewfile"
else
  # no linuxbrew on this box: apt glm builds everything except the
  # constexpr-math tests (needs glm >= 1.0)
  dpkg -s libglm-dev >/dev/null 2>&1 || sudo DEBIAN_FRONTEND=noninteractive apt-get install -y libglm-dev
fi
tool="$HOME/projects/compile-time-browser/tools/clang-std-embed"
# KEYED ON THE RELEASE, not merely on the binary existing. The guard used to be
# `[ ! -x "$tool/bin/clang++" ]`, which installs once and then never upgrades:
# bumping the pin above changed nothing, and the box quietly kept building with
# a toolchain three releases behind the one this file names. A stamp costs one
# line and makes the pin mean what it says.
if [ "$(cat "$tool/.release" 2>/dev/null)" != "$CLANG_STD_EMBED_RELEASE" ]; then
  rm -rf "$tool"
  mkdir -p "$tool"
  curl -fsSL --retry 5 "$CLANG_STD_EMBED_RELEASE" | tar -xJ --strip-components=1 -C "$tool"
  printf '%s' "$CLANG_STD_EMBED_RELEASE" > "$tool/.release"
fi
# ANGLE, from the pinned release. Its own guard is keyed on the release stamp,
# so this is a no-op once the box is at the pin and a re-download when the pin
# moves. It runs HERE rather than beside the cmake line because the Windows path
# needs it too, and because a fetch that failed should stop the build before it
# configures with the option off and reports a green suite that never linked
# ANGLE at all.
cd "$HOME/projects/compile-time-browser" && tools/fetch-angle.sh
REMOTE

if [ "${1:-}" = windows ]; then
  # `windows`, not `windows-fetch`: that preset no longer exists and this line
  # had gone stale, so the windows path failed at the first command with
  # "No such preset". The cross build also needs FOUR compiled libraries in the
  # mingw sysroot that the box does not ship - Boost.URL, zlib, libpng and
  # libjpeg-turbo - so the two builder scripts run first. Both are idempotent
  # and skip what is already installed.
  # THE ISOLATED BOOST INCLUDE DIR the cross toolchain wants. It is one
  # symlink: cmake/toolchain-windows-x86_64.cmake puts this on the cross
  # compile's -isystem path, so it must hold boost/ AND NOTHING ELSE - pointing
  # it at brew's whole include/ would put the host's SDL3, libpng and zlib
  # headers in front of a Windows build's. Created here rather than documented
  # as a prerequisite, because a box that cannot build itself is not much of a
  # build box.
  ssh "$host" 'inc="$HOME/projects/boost-inc"; mkdir -p "$inc";
    [ -e "$inc/boost" ] || ln -s /home/linuxbrew/.linuxbrew/include/boost "$inc/boost";
    ls "$inc/boost/version.hpp" >/dev/null'
  ssh "$host" "cd projects/compile-time-browser && tools/mingw/build-image-libs-mingw.sh && tools/mingw/build-boost-mingw.sh && tools/mingw/build-mimalloc-mingw.sh && tools/mingw/build-simdutf-mingw.sh && tools/mingw/build-cpptrace-mingw.sh"
  ssh "$host" "cd projects/compile-time-browser && cmake --preset windows -DCTBROWSER_WITH_ANGLE=$CTBROWSER_ANGLE && cmake --build --preset windows && cmake --build --preset windows --target windows-dist"
  rsync -az "$host:projects/compile-time-browser/examples-windows/" "$repo_root/examples-windows/"
  echo "examples-windows/ refreshed from the devbox"
elif [ $# -gt 0 ]; then
  ssh "$host" "cd projects/compile-time-browser && cmake --preset default -DCTBROWSER_WITH_ANGLE=$CTBROWSER_ANGLE && cmake --build --preset default --target $*"
else
  ssh "$host" "cd projects/compile-time-browser && cmake --preset default -DCTBROWSER_WITH_ANGLE=$CTBROWSER_ANGLE && cmake --build --preset default && ctest --preset default"
fi
