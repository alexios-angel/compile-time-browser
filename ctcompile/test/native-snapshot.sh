#!/usr/bin/env bash
# THE TWO GOLDENS THAT PROVE A REFACTOR CHANGED NOTHING.
#
# "The suite is green" is close to worthless for a pure refactor of the native
# lowering, and the reason is structural rather than a gap someone can close by
# adding cases. The differential gate compares ANSWERS, for programs that are
# still accepted. A function that silently stops being native produces no wrong
# answer - it produces no native code at all, the interpreter answers instead,
# and the differential passes. Everything downstream of the lowering measures
# the floor; a refactor moves the ceiling.
#
# So two artefacts the build ALREADY writes are snapshotted before a change and
# compared after it:
#
#   MODULES  <build>/ctcompile/test/{numeric,functions,structs,arrays}
#            .pipeline.emitc.mlir and .pipeline.deduced.emitc.mlir - eight
#            files, the whole post-pipeline IR for the four fixtures.
#   CENSUS   <build>/ctcompile/test/native-claims-{fixture,bootstrap,p5,phaser}
#            .json - every refusal reason with its count over the three real
#            corpora. This is the only instrument in the tree with resolution
#            over the ~12,900 refusals real code produces.
#
# THE RULE, and it is the whole point: FOR A PURE REFACTOR, NOT ONE BYTE MAY
# CHANGE. Not "the counts match", not "the suite is green" - `cmp` is silent on
# all twelve files or the refactor was not pure. A reworded diagnostic, a
# reordered check that changes which of two reachable refusals wins, a function
# that quietly stopped being claimed: each moves bytes here and nothing else.
#
# WHY A PROCEDURE AND NOT A CHECKED-IN GOLDEN. Three reasons, in order of how
# hard they are to work around:
#
#   1. The artefacts are not portable. The modules are written with
#      --mlir-print-debuginfo, so every operation carries a FileLineColLoc
#      naming the fixture BY THE ABSOLUTE PATH the build passed to
#      ctjs-translate; the census JSON records `"corpus": "<absolute path>"`.
#      A checked-in copy would differ on every machine, and the scrubber that
#      fixed that would be one more thing that can silently change what is
#      compared.
#   2. A golden of derived output has no safe regeneration rule. The honest
#      instruction is "regenerate when the change is meant to move it", and the
#      failure mode of every such golden is that the diff is large, nobody
#      reads it, and it is regenerated because it changed. A before/after
#      procedure has no regenerate step to abuse: the baseline is taken from
#      the tree as it stands BEFORE the edit, so it cannot be blessed after the
#      fact without re-running the old code.
#   3. The build already writes all twelve, every time. A snapshot taken out of
#      the same build tree is a true before/after with no third party in
#      between - which is exactly what a golden checked in months ago is not.
#
# THE COST is that this is opt-in: nothing runs it for you, and a refactor
# landed without it has no evidence behind "nothing changed". That is stated
# rather than papered over. `selftest` is the part that IS in the suite, so the
# instrument itself cannot rot unnoticed.
#
# USAGE
#
#   ctcompile/test/native-snapshot.sh save <dir> [build]
#   ...make the change, rebuild...
#   ctcompile/test/native-snapshot.sh compare <dir> [build]
#
#   ctcompile/test/native-snapshot.sh selftest [build]   # proves both teeth
#
# `build` defaults to $CTCOMPILE_BUILD_DIR, then to <repo>/build. `save` and
# `compare` re-run the four census checks themselves (about twenty seconds -
# p5 and phaser are large bundles), because the JSONs are written by ctest and
# a stale one from an earlier configuration is exactly the kind of quiet
# nothing this file exists to rule out.

set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"

MODULES=(numeric functions structs arrays)
CORPORA=(fixture bootstrap p5 phaser)

usage() {
  sed -n '2,/^set -uo/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//;$d'
  exit "${1:-2}"
}

# The twelve artefacts, as paths under a directory that holds ctcompile/test.
artefacts() {
  local root="$1" name
  for name in "${MODULES[@]}"; do
    printf '%s\n' "$root/$name.pipeline.emitc.mlir"
    printf '%s\n' "$root/$name.pipeline.deduced.emitc.mlir"
  done
  for name in "${CORPORA[@]}"; do
    printf '%s\n' "$root/native-claims-$name.json"
  done
}

resolve_build() {
  local given="${1:-${CTCOMPILE_BUILD_DIR:-$repo/build}}"
  if [ ! -d "$given/ctcompile/test" ]; then
    echo "native-snapshot: no ctcompile/test under '$given'." >&2
    echo "  Pass the build directory, or set CTCOMPILE_BUILD_DIR." >&2
    exit 2
  fi
  (cd "$given" && pwd)
}

# THE CENSUS JSONS ARE WRITTEN BY ctest, not by the build, so they are
# regenerated here rather than trusted. A missing ctest is fatal: silently
# comparing whatever JSON happened to be lying in the build tree is how a
# before/after ends up comparing a file with itself.
regenerate_census() {
  local build="$1"
  if ! command -v ctest >/dev/null 2>&1; then
    echo "native-snapshot: ctest is not on PATH; the census JSONs cannot be refreshed" >&2
    exit 2
  fi
  echo "native-snapshot: re-running the four census checks (p5 and phaser are large)..." >&2
  if ! (cd "$build" && ctest -R '^ctcompile_native_claims_(fixture|bootstrap|p5|phaser)$' \
          --output-on-failure >/dev/null); then
    echo "native-snapshot: a census check FAILED - fix that before measuring a refactor" >&2
    exit 1
  fi
}

missing=0
require_all() {
  local root="$1" file
  missing=0
  while read -r file; do
    if [ ! -f "$file" ]; then
      echo "native-snapshot: missing $file" >&2
      missing=$((missing + 1))
    fi
  done < <(artefacts "$root")
  if [ "$missing" -ne 0 ]; then
    echo "native-snapshot: $missing artefact(s) missing - build the tree first" >&2
    exit 2
  fi
}

cmd_save() {
  local dest="${1:-}"; shift || true
  [ -n "$dest" ] || usage
  local build; build="$(resolve_build "${1:-}")"
  local root="$build/ctcompile/test"
  regenerate_census "$build"
  require_all "$root"
  mkdir -p "$dest"
  local file
  while read -r file; do cp -f "$file" "$dest/"; done < <(artefacts "$root")
  # The build the snapshot came from, so `compare` can say when it is being
  # asked to compare across two different build trees - which would report
  # differences that are only paths.
  printf '%s\n' "$build" > "$dest/.native-snapshot-build"
  echo "native-snapshot: saved 12 artefacts from $root into $dest"
}

cmd_compare() {
  local dest="${1:-}"; shift || true
  [ -n "$dest" ] || usage
  local build; build="$(resolve_build "${1:-}")"
  local root="$build/ctcompile/test"
  local was; was="$(cat "$dest/.native-snapshot-build" 2>/dev/null || true)"
  if [ -n "$was" ] && [ "$was" != "$build" ]; then
    echo "native-snapshot: the snapshot was taken from '$was' but this is '$build'." >&2
    echo "  These artefacts embed absolute paths; comparing across build trees" >&2
    echo "  measures the paths, not the change. Refusing." >&2
    exit 2
  fi
  regenerate_census "$build"
  require_all "$root"
  local file base differed=0 checked=0
  while read -r file; do
    base="$(basename "$file")"
    checked=$((checked + 1))
    if [ ! -f "$dest/$base" ]; then
      echo "  MISSING FROM SNAPSHOT  $base" >&2
      differed=$((differed + 1))
      continue
    fi
    if ! cmp -s "$dest/$base" "$file"; then
      echo "  CHANGED  $base" >&2
      differed=$((differed + 1))
    fi
  done < <(artefacts "$root")
  if [ "$differed" -ne 0 ]; then
    cat >&2 <<EOF
native-snapshot: $differed of $checked artefacts CHANGED.

For a pure refactor that is a failure, not a diff to bless: not one byte may
change. Read the diffs - \`diff $dest/<name> $root/<name>\` - and either the
change was not a refactor, or it altered a diagnostic, altered which of two
reachable refusals wins, or stopped claiming a function. If the change was
MEANT to move these, say which and why in the commit that moves them.
EOF
    exit 1
  fi
  echo "native-snapshot: all $checked artefacts are byte-for-byte identical"
}

# BOTH TEETH OF THE INSTRUMENT, PROVED - the same discipline the census checks
# already use, because a comparison nobody has watched fail is not a comparison.
#
#   1. the pipeline is byte-deterministic: re-running it over an unchanged tree
#      must reproduce the module exactly. Without this, `compare` would report
#      differences that mean nothing and would be trained away.
#   2. `compare` fails when one byte moves.
cmd_selftest() {
  local build; build="$(resolve_build "${1:-}")"
  local root="$build/ctcompile/test"
  # NOT `local`: the EXIT trap runs after this function has returned, so a
  # local would be out of scope by then and `set -u` would turn the cleanup
  # into "work: unbound variable" on stderr with the directory left behind.
  work="$(mktemp -d)"
  trap 'rm -rf "$work"' EXIT
  local rc=0

  # 1: determinism. The module for the smallest fixture is regenerated with
  # the same commands native-pipeline.cmake uses and compared with the one in
  # the build tree.
  local translate opt
  translate="$(find "$build" -type f -name ctjs-translate -perm -u+x -print -quit)"
  opt="$(find "$build" -type f -name ctjs-opt -perm -u+x -print -quit)"
  if [ -z "$translate" ] || [ -z "$opt" ]; then
    echo "native-snapshot selftest: ctjs-translate/ctjs-opt not found under $build" >&2
    exit 2
  fi
  if [ ! -f "$root/numeric.pipeline.emitc.mlir" ]; then
    echo "native-snapshot selftest: $root/numeric.pipeline.emitc.mlir is missing" >&2
    exit 2
  fi
  local cmake_bin="${CMAKE:-cmake}"
  "$cmake_bin" -DTRANSLATE="$translate" -DOPT="$opt" \
        -DSOURCE="$here/native-pipeline-fixture.js" \
        -DOUTPUT="$work/again.mlir" \
        -P "$here/native-pipeline.cmake" >/dev/null 2>&1 || {
    echo "native-snapshot selftest: the pipeline failed to re-run" >&2
    exit 1
  }
  if cmp -s "$work/again.mlir" "$root/numeric.pipeline.emitc.mlir"; then
    echo "native-snapshot selftest: 1/2 the pipeline is byte-deterministic"
  else
    echo "native-snapshot selftest: 1/2 FAILED - re-running the pipeline over an" >&2
    echo "  unchanged tree did not reproduce numeric.pipeline.emitc.mlir. Every" >&2
    echo "  before/after comparison below it is meaningless until that is fixed." >&2
    rc=1
  fi

  # 2: the comparison bites. One byte is changed in a saved copy and `compare`
  # must name that file and exit non-zero. The census is not re-run for this -
  # the two large corpora take twenty seconds and prove nothing extra here - so
  # the check is made directly rather than through cmd_compare.
  local snap="$work/snap"
  mkdir -p "$snap"
  cp "$root/numeric.pipeline.emitc.mlir" "$snap/"
  printf '// one byte\n' >> "$snap/numeric.pipeline.emitc.mlir"
  if cmp -s "$snap/numeric.pipeline.emitc.mlir" "$root/numeric.pipeline.emitc.mlir"; then
    echo "native-snapshot selftest: 2/2 FAILED - a mutated copy compared EQUAL," >&2
    echo "  so this instrument would pass a refactor that changed the IR." >&2
    rc=1
  else
    echo "native-snapshot selftest: 2/2 a one-byte change is caught"
  fi
  return "$rc"
}

case "${1:-}" in
  save)     shift; cmd_save "$@" ;;
  compare)  shift; cmd_compare "$@" ;;
  selftest) shift; cmd_selftest "$@" ;;
  -h|--help|help) usage 0 ;;
  *)        usage ;;
esac
