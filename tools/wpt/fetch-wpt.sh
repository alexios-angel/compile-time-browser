#!/usr/bin/env bash
# Fetch web-platform-tests, SPARSE and SHALLOW, at a PINNED commit.
#
#   tools/wpt/fetch-wpt.sh              into ~/.cache/wpt (or $WPT_DIR)
#   WPT_DIR=/tmp/wpt tools/wpt/fetch-wpt.sh
#   tools/wpt/fetch-wpt.sh --verify     check an existing checkout, fetch nothing
#
# OUTSIDE THE SOURCE TREE, and never committed. WPT is ~1.5 million files and
# hundreds of MB of history; vendoring even this subset would be larger than the
# repository and would go stale silently. It is a MEASURING INSTRUMENT, so what
# has to be reproducible is the COMMIT, not a copy of the bytes - which is what
# the pin below is. `~/.cache` rather than `build/`: the build tree is wiped by
# a reconfigure, and re-downloading a corpus because somebody deleted a
# CMakeCache is a bad trade.
#
# The suites are listed rather than globbed, and ctbrowser/docs/wpt.md says why
# each is here. Fetching all of WPT would be far larger and measure nothing
# extra: a suite for a feature the engine has never heard of reports NOTRUN for
# every subtest, which is noise in a table rather than a finding.
set -euo pipefail

# THE PIN. Moving it is a deliberate act with a re-baseline attached: the
# expectations file beside this script is keyed on test paths, and WPT adds and
# renames tests every day. Recorded here, echoed by --verify, and printed by
# every run of the harness, so a table can never be read against the wrong
# corpus.
WPT_COMMIT="${WPT_COMMIT:-3f6b09ae3ed55280074645ce38e9002f52fc60a8}"
WPT_REMOTE="${WPT_REMOTE:-https://github.com/web-platform-tests/wpt.git}"
WPT_DIR="${WPT_DIR:-$HOME/.cache/wpt}"

# What gets checked out. `resources/` is the harness itself and is not optional;
# `common/` is what the suites import for their fixtures. The rest are chosen by
# what this engine implements - see ctbrowser/docs/wpt.md for the per-suite
# reasoning and for what was deliberately left out.
SPARSE_PATHS=(
  /resources/          # testharness.js, testharnessreport.js, testharness.css
  /common/             # get-host-info, gc, reftest-wait, the shared fixtures
  /dom/nodes/          # the DOM tree: Node, Element, Attr, Document
  /dom/events/         # EventTarget, dispatch, propagation, listener options
  /html/dom/           # reflection, and the document's own interface
  /css/cssom/          # getComputedStyle, style declarations, stylesheets
  /css/css-values/     # value parsing and computation - calc, lengths, units
  # THE HELPERS THE css/ SUITES IMPORT, and leaving them out was not a saving.
  # `test_valid_value`, `test_computed_value`, `test_math_used`,
  # `test_interpolation` and `test_specified_serialization` all live in
  # css/support/*.js, and a test that cannot load one reports HARNESS_ERROR
  # before it runs a single subtest - a measurement that cannot move no matter
  # what the engine does. 94 of css/css-values' 128 harness errors were exactly
  # that on 2026-09-02; adding this directory converted 87 into real results,
  # 84 FAIL and 3 PASS. The score got worse and the instrument got honest.
  #
  # It is a SKIP_DIR_PARTS directory in run-wpt.py, so nothing here is ever
  # collected as a test: it is imported, never run.
  /css/support/        # parsing-, computed-, numeric-testcommon.js and friends
)

verify_only=0
[ "${1:-}" = --verify ] && verify_only=1

if [ "$verify_only" = 1 ]; then
  if [ ! -d "$WPT_DIR/.git" ]; then
    echo "no WPT checkout at $WPT_DIR - run tools/wpt/fetch-wpt.sh" >&2
    exit 1
  fi
  at=$(git -C "$WPT_DIR" rev-parse HEAD)
  if [ "$at" != "$WPT_COMMIT" ]; then
    echo "WPT at $WPT_DIR is $at, pinned at $WPT_COMMIT - re-run tools/wpt/fetch-wpt.sh" >&2
    exit 1
  fi
  # THE HARNESS ITSELF, BY NAME. A sparse checkout that silently matched no
  # paths leaves a directory that exists, has the right HEAD, and cannot run one
  # test - which is a green --verify and then a hundred HARNESS_ERRORs.
  # NAMED FILES THAT ACTUALLY EXIST AT THE PIN. This list said
  # `resources/testharness.css`, which WPT deleted - the harness carries its own
  # styles now - so --verify could never pass on any checkout at all. A guard
  # that always fires is worth exactly as much as one that never does, and this
  # one was inherited unrun. Each entry below was checked against the pin.
  # css/support/parsing-testcommon.js is named because a checkout made before
  # that path joined the list above is a checkout at the right SHA with the
  # right harness that still loses 94 css-values files to a missing helper -
  # which is precisely the failure --verify exists to catch and could not.
  for must in resources/testharness.js resources/testharnessreport.js common dom/nodes \
              css/support/parsing-testcommon.js; do
    [ -e "$WPT_DIR/$must" ] || {
      echo "$WPT_DIR/$must missing - the sparse checkout is incomplete" >&2
      exit 1
    }
  done
  echo "wpt $WPT_COMMIT verified at $WPT_DIR"
  exit 0
fi

mkdir -p "$WPT_DIR"
if [ ! -d "$WPT_DIR/.git" ]; then
  git -C "$WPT_DIR" init -q
  git -C "$WPT_DIR" remote add origin "$WPT_REMOTE"
fi
git -C "$WPT_DIR" remote set-url origin "$WPT_REMOTE"
# --no-cone, because the paths above are patterns and cone mode silently accepts
# a pattern it then does not apply.
git -C "$WPT_DIR" sparse-checkout init --no-cone
git -C "$WPT_DIR" sparse-checkout set "${SPARSE_PATHS[@]}"
# BY COMMIT, at depth 1, with no blobs but the ones checked out. Fetching the
# branch and resetting would download every commit since; this downloads one
# tree. GitHub serves an arbitrary reachable SHA to `git fetch`, which is what
# makes a pin cost the same as a tip.
git -C "$WPT_DIR" fetch --depth 1 --filter=blob:none origin "$WPT_COMMIT"
git -C "$WPT_DIR" checkout -q --detach FETCH_HEAD

at=$(git -C "$WPT_DIR" rev-parse HEAD)
[ "$at" = "$WPT_COMMIT" ] || {
  echo "checked out $at, wanted $WPT_COMMIT" >&2
  exit 1
}
# ON DISK, not in the index. `git ls-files` lists the whole TREE - 162,834
# entries here - whatever the sparse checkout materialised, so it reported a
# number two orders of magnitude larger than the thing it was describing and
# made the sparse checkout look like it had not worked.
files=$(find "$WPT_DIR" -path "$WPT_DIR/.git" -prune -o -type f -print | wc -l)
echo "wpt $WPT_COMMIT -> $WPT_DIR ($files files on disk, $(du -sh --exclude=.git "$WPT_DIR" | cut -f1))"
