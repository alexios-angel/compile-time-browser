#!/usr/bin/env bash
# Fetch the OFFICIAL ECMAScript conformance suite, tc39/test262, at a PINNED
# commit, into a directory OUTSIDE the source tree. Usage:
#
#   tools/fetch-test262.sh                 # into $HOME/.cache/ctbrowser/test262
#   TEST262_DIR=/somewhere tools/fetch-test262.sh
#
# NEVER VENDORED. The corpus is ~50,000 files and ~200 MB checked out; it does
# not belong in this repository and tools/remote-build.sh does not sync it (the
# default directory is under $HOME, not under the tree). Fetch it on whichever
# machine runs it - the devbox, normally - and the runner
# (tools/check/test262.py) finds it at the same default.
#
# PINNED, not `main`: a conformance number is only comparable to another
# conformance number against the SAME corpus, and test262 lands several commits
# a week. Bump the pin deliberately, re-run the baseline, and record both in
# ctbrowser/docs/test262.md with the date.
#
# Shallow, by commit: one fetch of exactly the pinned object rather than a clone
# of 40,000 commits of history, and the hash is VERIFIED after checkout rather
# than trusted - a remote that serves something else for that name is a bug this
# script should refuse, not build a baseline on.
set -euo pipefail

TEST262_COMMIT="771005236e88a909635104e03ba12559688c0172" # tc39/test262 main, 2026-09-02
TEST262_URL="${TEST262_URL:-https://github.com/tc39/test262.git}"
dest="${TEST262_DIR:-$HOME/.cache/ctbrowser/test262}"

have() { git -C "$dest" rev-parse --verify --quiet HEAD 2>/dev/null || true; }

if [ "$(have)" = "$TEST262_COMMIT" ]; then
  echo "test262 already at $TEST262_COMMIT in $dest"
  exit 0
fi

mkdir -p "$dest"
if [ ! -d "$dest/.git" ]; then
  git -C "$dest" init --quiet
  git -C "$dest" remote add origin "$TEST262_URL"
fi
git -C "$dest" fetch --quiet --depth 1 origin "$TEST262_COMMIT"
git -C "$dest" checkout --quiet --detach FETCH_HEAD

got="$(have)"
if [ "$got" != "$TEST262_COMMIT" ]; then
  echo "test262: expected $TEST262_COMMIT, checked out $got" >&2
  exit 1
fi
echo "test262 at $TEST262_COMMIT in $dest"
