#!/usr/bin/env bash
# Sync the working tree (incl. submodules) to the shared devbox and run the
# build there. Usage: ./remote-build.sh [make target ...]    (default: the
# top-level `make`, which bakes the combined PCH if stale, then builds + runs
# the headless suite)
#
# The box is github.com/alexios-angel/infra (sibling checkout ../infra),
# reached via the `devbox` ssh alias that `../infra/azure-build-server/
# server.sh ssh-config` writes; DEVBOX_HOST=ubuntu@<ip> overrides. Project
# deps the box deliberately doesn't ship (the pinned clang-std-embed
# toolchain) are converged here, not in the box's cloud-init.
set -euo pipefail
cd "$(dirname "$0")"

host="${DEVBOX_HOST:-devbox}"
# Same knob + default as CI (.github/workflows/tests.yml): the embed repo's
# pinned toolchain release. sync-to-ctbrowser.sh may install a locally-built
# one instead — the rsync protect filter keeps whichever is on the server.
CLANG_STD_EMBED_RELEASE="${CLANG_STD_EMBED_RELEASE:-https://github.com/alexios-angel/embed/releases/download/clang-std-embed-23dd34f8f924/clang-std-embed-23dd34f8f924-linux-x86_64.tar.xz}"

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

# rsync the whole tree including submodule checkouts; leave remote build
# artifacts in place so the PCH bake is reused across syncs.
# tools/clang-std-embed stays local: the server-side copy is converged below.
rsync -az --delete \
  --exclude '.git/' \
  --exclude 'build/' \
  --exclude 'tools/clang-std-embed/' \
  --exclude '*.d' \
  --filter 'protect *.pch' --filter 'protect *.gch' --filter 'protect build/' \
  --filter 'protect tools/clang-std-embed/' --filter 'protect *.d' \
  "$repo_root"/ "$host:projects/compile-time-browser/"

# Converge project-owned deps on the box. glm is apt's libglm-dev and baked
# into the box image — the guard just heals a box that predates it.
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
if [ ! -x "$tool/bin/clang++" ]; then
  mkdir -p "$tool"
  curl -fsSL --retry 5 "$CLANG_STD_EMBED_RELEASE" | tar -xJ --strip-components=1 -C "$tool"
fi
REMOTE

ssh "$host" "cd projects/compile-time-browser && make ${*:-}"
