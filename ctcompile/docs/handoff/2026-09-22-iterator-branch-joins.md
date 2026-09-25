# Iterator branch joins and fixed-bit rounding, 2026-09-22

Resumed clean `6f7ee572` and the exact pre-traversal branch boundary recorded in
HANDOFF, plan 00 and iteration 55's journal. No dirty predecessor work remained;
`codex-wip-20260907` was absent and other branches were preserved. Linux `/proc`
found no Claude identity among 18 readable executable records, with 57 permission
errors. Windows CIM inspected 345 identities without a match. Status remained
uncertain; concurrent-agent rules applied. No browser, shared implementation or
runtime-oracle semantics changed.

## Landed

**63b36a15** retains an ordinary branch's result and scalar state in one join,
then emits its common continuation once. The existing completion emitter handles
the branch arms and validates their yield types, dominance, defined values,
complete source census and work budget. Joining requires JavaScript result types
and excludes nested switches, poison and mapped outer poison. Completion tags
and inactive slots keep their existing path expansion and exact selector proof.
This also keeps ordinary post-close observations outside the close branches.

The saved `entry-captured-sibling-preloop-branch-writer` source executes unchanged.
Two further sources retain branch-dependent ordinary results and early helper
returns. The importer already normalizes the early returns; the completed join
now composes with that result. All 102 earlier complete sources and 34 positive
metadata tuples remain unchanged. Ordinary/direct raw twins observe both current
cells and saved return values at five call positions, including after close and
on repeated calls. Malformed joins, live outer poison and sampled budgets refuse.
Earlier raw fixtures remain unchanged; their assertions now inspect joined close
state and once-only effects instead of duplicated post-close arithmetic.

**45658831** extends monotone AND/OR rounding recognition using already-proved
fixed input bits. Fixed high bits translate the output; fixed low bits can fill
gaps in the rounded suffix. Existing same-signed-band endpoints, output period,
complete mutation/reload census and work budget remain authoritative. Eighteen
CFG rows, 18 SCF rows and 18 source witnesses extend all 251 earlier source bodies.
The escape subagent hit a rate limit after preparing its draft; root reviewed,
finished checking and committed that work.

## Focused validation

| Check | Result |
| --- | --- |
| Explicit affected devbox builds | PASS |
| Final exact `ctcompile_host_contract` | 1/1 PASS; 1.14 s test / 1.15 s total |
| Three new source admission preflights and all 68 refusal preflights | PASS |
| Seven-positive custom subset, including all 68 refusal programs | PASS; 112 native executions / 304 refusals / 56 Node/VM observations |
| Exact nested-iteration lit | 1/1 PASS; 132.99 s; 48 native executions / two previous-source checks / 94 refusals |
| Final exact `ctcompile_escape_analysis_arrays` | 1/1 PASS; 1.83 s total |
| AND and OR/XOR index-overwrite lit | 2/2 PASS; 0.14 s |

The custom subset selects the three new branch sources plus
`entry-captured-sibling-loop-writer`, `entry-captured-sibling-ordered-writer`,
`entry-captured-sibling-ordered-reader` and `entry-captured-ordered-state`.
Each uses borrowed/owned DOM, both optimization policies, explicit/deduced output
and the existing two C++ compilers. The nested case was selected because shared
completion handling changed. The complete custom case was not replayed.

All eight final code/test hashes match local files and the devbox. Native
production and source fixtures remained unchanged throughout the passing source
checks; the later raw assertion update received its own final host build/check.
Generated C++ was inspected: typed scalar loops and public DOM calls retain the
joined branch without Script or the VM iterator protocol. Existing symbol checks
also pass. Local checks cover 105 Node source syntax checks, 360 exact traces and
ten further observations when the early-return source was promoted; escape checks
execute 269 source functions and check 18 new exact outputs. These are separate
from the measured Node/VM differential observations above.

Every devbox operation held `/tmp/ctbrowser-devbox-build.lock`. The initial
combined affected target set was:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

Final exact tests used:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitand|bitor-xor)-index-overwrite[.]test$' -o /tmp/ctcompile-iteration56-escape-lit.json
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-nested-iteration[.]test$' -o /tmp/ctcompile-iteration56-nested-lit.json
```

The seven-label execution wrapper, exact compiler flags and source preflights
are in `/tmp/ctcompile-iteration56-native-source.sh` and its Python wrapper,
preserved in the artifacts. The first compiler build caught an ambiguous MLIR
type-range conversion; explicit conversion fixed it. A prematurely queued
preflight used the old binary and repeated the known refusal; it is not a final
gate. The first arrays check found an OR-only test string replacement applied
to the negative AND fixture. Both fixture constructors were corrected without
changing production. Initial host checks exposed assertions tied to duplicated
continuations; temporary IR dumps guided their update and were removed before
the final passing check. No prior source was weakened to pass.

`tools/format.sh --check` retains 16 pre-existing diagnostics in four untouched
files: `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Changed C++ passes the pinned
formatter; changed Python passes Black/AST; diff whitespace and temporary shell
syntax checks pass. Early formatter output also saw in-flight agent edits;
the final run has only the historical diagnostics.

No full CTest/compiler lit, full custom case, unchanged dataset case, broad
corpus/native matrix, WPT/test262, Windows, additional sanitizer, local build or
push ran. The devbox idle timer is active/enabled. Logs, scripts, hashes,
source/IR witnesses and generated C++ are retained at
`../test-results/2026-09-22-iterator-branch-joins/` beside the monorepo.

## Exact next boundary

Use `refusals()["entry-captured-sibling-argument-writer"]` in
`ctcompile/test/CTNative/Browser/native_dom_custom_iteration.py`.
The complete source SHA-256 is
`7cfdaf2a7379adc07ab8ed1cf7a85105f80a48868d0e6394c9232f6f00f93d84`.
It passes `closed` to the helper and observes `emitted + amount` before the
helper's ordered state writes. Both optimization policies return status 1:

```text
native DOM source: DOM iterator sibling helper requires an exact local leaf
```

Two stderr-only captures preserve that boundary; no native execution is claimed.
Next prove explicit arguments at each sibling call while retaining their source
evaluation, argument snapshot, ordinary return and latest cell state. Sibling
method breaks, nested custom opens, abrupt iterator close, literal range-for
printing, unguarded Bootstrap defaults and the application driver remain open.
Full Bootstrap is not admitted and no vendor coverage gain is claimed. Higher
nonmonotone mask gaps still need a richer range representation.
