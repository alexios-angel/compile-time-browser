#!/usr/bin/env bash
# Fetch the ANGLE build ctbrowser links against.
#
# ANGLE is deliberately not built here: it needs GN, depot_tools and (on
# Windows) clang-cl and the Windows SDK, a second toolchain this repository does
# not otherwise require. ctbrowser/docs/history/angle.md has the measurements;
# the fork at alexios-angel/angle has the build recipe and publishes the result.
#
# So it is a pinned artefact, like the clang-std-embed toolchain.
# `tools/remote-build.sh windows` builds every other dependency on the Linux
# devbox, and ANGLE cannot join it; two producers for one dist are only safe if
# one of them is a fixed version rather than whatever a developer happened to
# build.
#
#   tools/fetch-angle.sh [--clean]
#
# Leaves the libraries and headers in third-party/angle/<platform>/.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
release="ctbrowser-angle-25c80ccab4"
base="https://github.com/alexios-angel/angle/releases/download/${release}"
dest="$here/third-party/angle"

if [ "${1:-}" = "--clean" ]; then rm -rf "$dest"; fi

fetch() {
    # `local x=$1 y=$2` under `set -u` evaluates the whole declaration before
    # any name is bound, so a later initialiser referring to an earlier one is
    # an unbound variable. Split, rather than dropping -u.
    local platform="$1"
    local archive="$2"
    local into="$dest/$platform"
    # Keyed on the release tag, not on the directory existing, so changing the
    # pin above actually re-downloads.
    if [ "$(cat "$into/.release" 2>/dev/null)" = "$release" ]; then
        echo "fetch-angle: $platform already at $release"
        return
    fi
    echo "fetch-angle: $platform <- $archive"
    rm -rf "$into"
    mkdir -p "$into"
    local tmp
    tmp="$(mktemp -d)"
    curl -fsSL --retry 5 "$base/$archive" -o "$tmp/$archive"
    case "$archive" in
        *.tar.gz) tar -xzf "$tmp/$archive" -C "$into" ;;
        *.zip)    unzip -q "$tmp/$archive" -d "$into" ;;
    esac
    rm -rf "$tmp"
    printf '%s' "$release" > "$into/.release"
}

fetch linux-x86_64 angle-ctbrowser-linux-x86_64.tar.gz
fetch windows-x64  angle-ctbrowser-windows-x64.zip

echo "fetch-angle: into $dest"
for platform in linux-x86_64 windows-x64; do
    printf '  %-14s ' "$platform"
    ls "$dest/$platform" | grep -cE '\.so|\.dll' | tr -d '\n'
    echo " libraries"
done
