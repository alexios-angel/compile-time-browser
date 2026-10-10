#!/usr/bin/env bash
# The baseline run: every area ctbrowser/docs/test262.md records a number for,
# one after another, into /tmp/t262/. Sequential on purpose: each run already
# uses four workers, the cap on the shared build machine.
#
#   tools/check/test262-baseline.sh            # ~10 minutes on an idle devbox
#   OUT=/tmp/t262 tools/check/test262-baseline.sh
#
# One script so every area is measured with the same flags. Run it detached
# (`nohup ... &`) on a shared machine so an ssh drop does not lose the run.
# Not `set -e`: an area whose runner dies (e.g. OOM-killed) must not stop the
# remaining areas; each area records its own exit status.
set -uo pipefail
cd "$(dirname "$0")/../.."

out="${OUT:-/tmp/t262}"
binary="${BINARY:-build/tools/ct262}"
mkdir -p "$out"

# All of ECMA-262: test/language, test/built-ins and test/annexB. intl402
# (ECMA-402) and staging (not yet specification) are not run. The per-area
# table in ctbrowser/docs/test262.md is read out of the built-ins JSON by
# directory.
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
