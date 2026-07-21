#!/bin/bash
# cloud-init for the ctbrowser build server (Ubuntu 24.04).
# Installs the toolchain for the ENGINE build: PCH bake + headless test suite.
# ONLY the std::embed clang is supported: fetched from the embed repo's
# GitHub release. Utilities come from LINUXBREW, not apt — apt provides only
# Homebrew's own prerequisites (build-essential also supplies make + the
# libstdc++ headers clang needs).
# SDL3 is deliberately absent — render test and examples skip without it,
# exactly like CI. Goldens stay a local check.
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y build-essential procps curl file git

# Homebrew (linuxbrew), non-interactive, owned by the ubuntu user
sudo -Hu ubuntu env NONINTERACTIVE=1 /bin/bash -c \
  "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
BREW=/home/linuxbrew/.linuxbrew/bin/brew
sudo -Hu ubuntu echo 'eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"' >>~/.profile
sudo -Hu ubuntu echo 'eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"' >>~/.bashrc
nvim
# utilities via brew, not apt (glm: header-only math lib babylon.hpp needs)
sudo -Hu ubuntu "$BREW" install \
  cmake ninja pkg-config make python rsync ccache unzip zstd htop xz glm neovim gh \
  ripgrep xsel lua luarocks fd nvm fastfetch llvm bat

sudo -Hu ubuntu cat <<'EOF' >>~/.bashrc
export NVM_DIR="$HOME/.nvm"
  [ -s "/home/linuxbrew/.linuxbrew/opt/nvm/nvm.sh" ] && \
  \. "/home/linuxbrew/.linuxbrew/opt/nvm/nvm.sh"  # This loads nvm
  [ -s "/home/linuxbrew/.linuxbrew/opt/nvm/etc/bash_completion.d/nvm" ] && \
  \. "/home/linuxbrew/.linuxbrew/opt/nvm/etc/bash_completion.d/nvm"  # This loads nvm bash_completion
EOF

sudo -Hu ubuntu nvm install node

# required
sudo -Hu ubuntu mv ~/.config/nvim{,.bak}

# optional but recommended
sudo -Hu ubuntu mv ~/.local/share/nvim{,.bak}
sudo -Hu ubuntu mv ~/.local/state/nvim{,.bak}
sudo -Hu ubuntu mv ~/.cache/nvim{,.bak}

sudo -Hu ubuntu git clone https://github.com/LazyVim/starter ~/.config/nvim

sudo -Hu ubuntu curl -fsSL https://claude.ai/install.sh | bash

# the ONE compiler: the std::embed clang release (brew's xz decodes the tarball)
install -d -o ubuntu -g ubuntu /home/ubuntu/ctbrowser/tools/clang-std-embed
curl -fsSL "https://github.com/alexios-angel/embed/releases/download/clang-std-embed-23dd34f8f924/clang-std-embed-23dd34f8f924-linux-x86_64.tar.xz" |
  sudo -u ubuntu env PATH=/home/linuxbrew/.linuxbrew/bin:/usr/bin:/bin \
    tar -xJ --strip-components=1 -C /home/ubuntu/ctbrowser/tools/clang-std-embed

# ccache for the non-PCH translation units
sudo -Hu ubuntu /home/linuxbrew/.linuxbrew/bin/ccache --set-config max_size=10G || true

# Tailscale — bidirectional ssh over the tailnet (laptop <-> server) that
# survives public-IP changes. A system daemon needing systemd, so it uses the
# official installer, not brew. --ssh enables Tailscale SSH on this node.
# Rendered by terraform templatefile(); bash expansions below are $-escaped.
TS_AUTHKEY='${tailscale_auth_key}'
if [ -n "$TS_AUTHKEY" ]; then
  curl -fsSL https://tailscale.com/install.sh | sh
  set +x # keep the auth key out of cloud-init-output.log
  tailscale up --authkey="$TS_AUTHKEY" --ssh --hostname='${tailscale_hostname}'
  set -x
fi

touch /var/lib/cloud/instance/ctbrowser-ready
