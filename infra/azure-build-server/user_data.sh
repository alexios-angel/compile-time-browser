#!/bin/bash
# cloud-init for the ctbrowser build server (Ubuntu 24.04).
# Installs the toolchain for the ENGINE build: PCH bake + headless test suite.
#
# Ground rules:
#   * ONLY the std::embed clang is supported, fetched from the embed repo's
#     GitHub release — see "the ONE compiler" below.
#   * Utilities come from LINUXBREW, not apt; apt installs just enough to
#     bootstrap (details at the apt step).
#   * SDL3 is deliberately absent: render test and examples skip without it,
#     exactly like CI. Goldens stay a local check.
#   * Ordering is deliberate: compiler + tailnet FIRST, comforts after — a
#     failure in the nice-to-haves must never strand the box without clang
#     or SSH.
#
# Terraform renders this file with templatefile(); the tailscale auth key +
# hostname are the ONLY template vars. Keep the bash free of dollar-brace
# expansions — $VAR and $(cmd) forms only — or terraform will eat the braces.
#
# Everything runs as root; user-owned steps go through as_ubuntu() with
# EXPLICIT /home/ubuntu paths. Never `as_ubuntu cmd >>~/file` — the redirect
# is opened by ROOT'S shell before sudo runs, so it lands in /root.
set -Eeuxo pipefail
trap 'echo "user_data.sh: FAILED at line $LINENO: $BASH_COMMAND" >&2' ERR
# -E so the trap also fires for failures inside the helpers. BASH_COMMAND is
# the unexpanded source text, so a failing `tailscale up` cannot leak the key.

as_ubuntu() { sudo -Hu ubuntu "$@"; }   # a user-owned step; -H sets HOME
fetch() { curl -fsSL --retry 5 "$@"; }  # fail on HTTP errors, retry transient

# ---- apt: bootstrap only ----------------------------------------------------
# build-essential procps curl file git — Homebrew's prerequisites, and
# build-essential also supplies make + the libstdc++ headers clang needs;
# xz-utils — decodes the toolchain tarball, which arrives before brew exists;
# zsh — the login shell for oh-my-zsh below.
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y build-essential procps curl file git xz-utils zsh

# ---- the ONE compiler: the std::embed clang release -------------------------
# Unpacked to the in-repo path a checkout at ~/ctbrowser expects
# (tools/clang-std-embed); other clones symlink this directory in.
CLANG_TAG=clang-std-embed-23dd34f8f924
install -d -o ubuntu -g ubuntu /home/ubuntu/ctbrowser/tools/clang-std-embed
fetch "https://github.com/alexios-angel/embed/releases/download/$CLANG_TAG/$CLANG_TAG-linux-x86_64.tar.xz" |
  as_ubuntu tar -xJ --strip-components=1 -C /home/ubuntu/ctbrowser/tools/clang-std-embed

# ---- Tailscale: bidirectional ssh over the tailnet (laptop <-> server) ------
# Survives public-IP changes. It's a system daemon needing systemd, so it uses
# the official installer, not brew; --ssh enables Tailscale SSH on this node.
#
# SECRET HANDLING: xtrace prints assignments, test expansions and argv — an
# earlier version leaked the key into cloud-init-output.log that way. Keep
# xtrace OFF on every line where the key is in play.
set +x
TS_AUTHKEY='${tailscale_auth_key}'
if [ -n "$TS_AUTHKEY" ]; then
  set -x                            # no secret in the next two lines
  fetch https://tailscale.com/install.sh -o /tmp/tailscale-install.sh
  sh /tmp/tailscale-install.sh
  set +x                            # the key is in the argv here
  tailscale up --authkey="$TS_AUTHKEY" --ssh --hostname='${tailscale_hostname}'
fi
set -x

# ---- Homebrew (linuxbrew), non-interactive, owned by the ubuntu user --------
fetch https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh -o /tmp/brew-install.sh
as_ubuntu env NONINTERACTIVE=1 /bin/bash /tmp/brew-install.sh
BREW=/home/linuxbrew/.linuxbrew/bin/brew

# the build kit (the project compiler is the std::embed clang above, NEVER brew's)
as_ubuntu "$BREW" install cmake ninja make pkg-config python ccache rsync unzip zstd xz
# the one library dep: glm, the header-only math lib babylon.hpp needs
as_ubuntu "$BREW" install glm
# editor: neovim + LazyVim's supporting cast; llvm is EDITOR TOOLING ONLY (clangd/lldb)
as_ubuntu "$BREW" install neovim llvm ripgrep fd lua luarocks xsel nvm
# creature comforts
as_ubuntu "$BREW" install gh htop fastfetch bat

# ---- PATH: brew (+ everything above) visible from every shell ---------------
# Non-interactive ssh included — rsync from the laptop, scripted
# `ssh server cmake ...`. pam_env reads /etc/environment for every ssh
# session; the rc files below only cover interactive shells.
cat >/etc/environment <<'EOF'
PATH="/home/linuxbrew/.linuxbrew/bin:/home/linuxbrew/.linuxbrew/sbin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/snap/bin"
EOF

# Interactive shells: brew shellenv + nvm loader + ~/.local/bin (claude).
# ONE snippet, appended to the bash rc files here and to .zshrc AFTER
# oh-my-zsh writes its template (below). The -s guards keep the lines inert
# if brew ever moves.
cat >/tmp/rc-snippet <<'EOF'
eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"
export NVM_DIR="$HOME/.nvm"
[ -s "/home/linuxbrew/.linuxbrew/opt/nvm/nvm.sh" ] && . "/home/linuxbrew/.linuxbrew/opt/nvm/nvm.sh"
export PATH="$HOME/.local/bin:$PATH"
EOF
cat /tmp/rc-snippet >>/home/ubuntu/.profile
cat /tmp/rc-snippet >>/home/ubuntu/.bashrc
# nvm's bash completion is bash-only, so it's not part of the shared snippet
cat >>/home/ubuntu/.bashrc <<'EOF'
[ -s "/home/linuxbrew/.linuxbrew/opt/nvm/etc/bash_completion.d/nvm" ] && . "/home/linuxbrew/.linuxbrew/opt/nvm/etc/bash_completion.d/nvm"
EOF

# ---- oh-my-zsh --------------------------------------------------------------
# --unattended skips the chsh; do it ourselves.
fetch https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh -o /tmp/omz-install.sh
as_ubuntu sh /tmp/omz-install.sh --unattended
chsh -s /usr/bin/zsh ubuntu
cat /tmp/rc-snippet >>/home/ubuntu/.zshrc   # .zshrc exists only from here on

# ---- node, via nvm ----------------------------------------------------------
# nvm is a shell FUNCTION, not a binary: source it, then install. ~/.nvm must
# exist first (brew nvm caveat).
as_ubuntu bash -c '
  export NVM_DIR="$HOME/.nvm" &&
  mkdir -p "$NVM_DIR" &&
  . /home/linuxbrew/.linuxbrew/opt/nvm/nvm.sh &&
  nvm install node
'

# ---- LazyVim starter --------------------------------------------------------
# The standard pre-install backups. No-ops on a fresh box — the -e guards
# keep set -e alive there, and make a rerun safe.
for d in /home/ubuntu/.config/nvim /home/ubuntu/.local/share/nvim \
         /home/ubuntu/.local/state/nvim /home/ubuntu/.cache/nvim; do
  if [ -e "$d" ]; then mv "$d" "$d.bak"; fi
done
install -d -o ubuntu -g ubuntu /home/ubuntu/.config
as_ubuntu git clone https://github.com/LazyVim/starter /home/ubuntu/.config/nvim
as_ubuntu rm -rf /home/ubuntu/.config/nvim/.git   # per the starter README
# plugins bootstrap on the first interactive `nvim` — nothing to launch here

# ---- claude code (native installer → ~/.local/bin/claude) -------------------
# Download-then-run so the install runs as ubuntu — this whole script is
# root, and an earlier piped `curl | bash` ran the install as root.
fetch https://claude.ai/install.sh -o /tmp/claude-install.sh
as_ubuntu bash /tmp/claude-install.sh

# ccache for the non-PCH translation units; best-effort — a comfort must
# never fail the box
as_ubuntu /home/linuxbrew/.linuxbrew/bin/ccache --set-config max_size=10G || true

# ---- done -------------------------------------------------------------------
rm -f /tmp/tailscale-install.sh /tmp/brew-install.sh /tmp/omz-install.sh \
  /tmp/claude-install.sh /tmp/rc-snippet
touch /var/lib/cloud/instance/ctbrowser-ready   # "provisioning finished" breadcrumb
