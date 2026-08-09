#!/usr/bin/env bash
# Fetch the ANGLE build ctbrowser links against.
#
# ANGLE is NOT built here and deliberately so: it needs GN, depot_tools and - on
# Windows - clang-cl and the Windows SDK, which is a second toolchain this
# repository does not otherwise require. docs/plans/angle.md has the measurements;
# the fork at alexios-angel/angle has the build recipe and publishes the result.
#
# SO IT IS A PINNED ARTEFACT, the way the clang-std-embed toolchain already is.
# That matters more here than it looks: `tools/remote-build.sh windows` builds
# every other dependency on the Linux devbox, and ANGLE cannot join it. Two
# producers for one dist is only safe if one of them is a fixed version rather
# than whatever a developer happened to build.
#
#   tools/fetch-angle.sh [--clean]
#
# Leaves the libraries and headers in third_party/angle/<platform>/.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
release="ctbrowser-angle-25c80ccab4"
base="https://github.com/alexios-angel/angle/releases/download/${release}"
dest="$here/third_party/angle"

if [ "${1:-}" = "--clean" ]; then rm -rf "$dest"; fi

fetch() {
    # `local x=$1 y=$2` under `set -u` evaluates the whole declaration before
    # any name is bound, so a later initialiser referring to an earlier one is
    # an unbound variable. Split, rather than dropping -u.
    local platform="$1"
    local archive="$2"
    local into="$dest/$platform"
    # KEYED ON THE RELEASE, not on the directory existing. A guard that only
    # checks for a directory installs once and then never upgrades, so moving
    # the pin above would change nothing - the trap the clang toolchain sat in
    # for weeks.
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
