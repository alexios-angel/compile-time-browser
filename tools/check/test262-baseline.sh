#!/usr/bin/env bash
# The BASELINE run: every area docs/test262.md records a number for, one after
# another, into /tmp/t262/. Sequential on purpose - four workers is the cap the
# whole box shares, and two of these at once is eight.
#
#   tools/check/test262-baseline.sh            # ~15 minutes on an idle devbox
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

areas=(
  test/language
  test/built-ins/Array
  test/built-ins/Object
  test/built-ins/Number
  test/built-ins/Math
  test/built-ins/String
  test/built-ins/Boolean
  test/built-ins/Function
  test/built-ins/Error
  test/built-ins/JSON
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
