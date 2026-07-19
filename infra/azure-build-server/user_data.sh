#!/bin/bash
# cloud-init for the ctbrowser build server (Ubuntu 24.04).
# Installs the toolchain for the ENGINE build: PCH bake + headless test suite.
# ONLY the std::embed clang is supported: fetched from the embed repo's
# GitHub release (build-essential provides make + libstdc++ headers).
# SDL3 is deliberately absent (not packaged for 24.04) — render test and
# examples skip without it, exactly like CI. Goldens stay a local check.
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y \
  build-essential \
  cmake ninja-build make \
  pkg-config git python3 \
  rsync ccache unzip zstd htop xz-utils

# the ONE compiler: the std::embed clang release
install -d -o ubuntu -g ubuntu /home/ubuntu/ctbrowser
install -d -o ubuntu -g ubuntu /home/ubuntu/ctbrowser/tools/clang-std-embed
curl -fsSL "https://github.com/alexios-angel/embed/releases/download/clang-std-embed-23dd34f8f924/clang-std-embed-23dd34f8f924-linux-x86_64.tar.xz" \
  | sudo -u ubuntu tar -xJ --strip-components=1 -C /home/ubuntu/ctbrowser/tools/clang-std-embed

# ccache for the non-PCH translation units
sudo -u ubuntu ccache --set-config max_size=10G || true

touch /var/lib/cloud/instance/ctbrowser-ready
