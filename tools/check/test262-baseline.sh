#!/usr/bin/env bash
# The BASELINE run: every area docs/test262.md records a number for, one after
# another, into /tmp/t262/. Sequential on purpose - four workers is the cap the
# whole box shares, and two of these at once is eight.
#
#   tools/check/test262-baseline.sh            # ~10 minutes on an idle devbox
#   OUT=/tmp/t262 tools/check/test262-baseline.sh
#
# It exists because the alternative is ten hand-typed commands whose flags drift
# between them, and a baseline whose runs were not measured the same way is not
# a baseline. Run it detached (`nohup ... &`) on the devbox: the box is shared,
# and an ssh drop should not cost fifteen minutes of measurement.
# NOT `set -e`: one area that dies must not take the other nine with it. The
# first run of this lost fifteen minutes because the runner was OOM-killed in
# test/language on a loaded box and the script stopped there, so the nine areas
# after it measured nothing at all. Each area records its own exit status.
set -uo pipefail
cd "$(dirname "$0")/../.."

out="${OUT:-/tmp/t262}"
binary="${BINARY:-build/tools/ct262}"
mkdir -p "$out"

# THE WHOLE OF ECMAScript, since 2026-09-12: test/language, test/built-ins and
# test/annexB, which together are every file the corpus holds for the language
# and its standard library (intl402 is ECMA-402 and staging is not yet the
# specification; neither is run). Before this the list was test/language and
# nine hand-picked built-ins directories, and the number it gave was the score
# over 33,000 of 48,600 files - so RegExp at 24%, Promise at 34% and
# TypedArray at 0.1% were invisible. The docs' per-area table is read out of
# the built-ins JSON by directory (docs/test262.md says how), so the old rows
# remain comparable.
areas=(
  test/language
  test/built-ins
  test/annexB
)

failed=0
for area in "${areas[@]}"; do
  name="${area//\//-}"
  echo "=== $area"
  if ! tools/check/test262.py --dir "$area" --binary "$binary" \
       --json "$out/$name.json" > "$out/$name.txt" 2>&1; then
    failed=$((failed + 1))
    echo "!!! $area did not finish - see $out/$name.txt"
  fi
  tail -n 40 "$out/$name.txt"
done
echo "=== baseline complete: $out ($failed area(s) did not finish)"
