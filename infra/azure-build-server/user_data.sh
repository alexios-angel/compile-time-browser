#!/bin/bash
# cloud-init for the ctbrowser build server (Ubuntu 24.04).
# Installs the toolchain for the ENGINE build: PCH bake + headless test suite.
# SDL3 is deliberately absent (not packaged for 24.04) — render test and
# examples skip without it, exactly like CI. Goldens stay a local check.
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y \
  build-essential g++ gcc \
  cmake ninja-build make \
  pkg-config git python3 \
  rsync ccache unzip zstd htop

# ccache for the non-PCH translation units
sudo -u ubuntu ccache --set-config max_size=10G || true

# Work area the remote-build script rsyncs into
install -d -o ubuntu -g ubuntu /home/ubuntu/ctbrowser

touch /var/lib/cloud/instance/ctbrowser-ready
