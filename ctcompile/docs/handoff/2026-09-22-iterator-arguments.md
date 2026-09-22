# Iterator argument snapshots and sparse bitwise bounds, 2026-09-22

Resumed clean `9ad10593` and its exact `entry-captured-sibling-argument-writer`
boundary from HANDOFF, plan 00 and iteration 56's journal. No unfinished tree
changes remained; `codex-wip-20260907` was absent and other branches were
preserved. Linux checked 15 readable executable identities with 56 permission
errors; Windows CIM checked 341 identities without Claude matches. The initial
Linux matcher saw its own checking command and was corrected to inspect script
paths. Overall status remained uncertain; concurrent-agent rules applied.
No browser, shared implementation or runtime-oracle semantics changed.

## Landed

**8b5a9ebc** permits explicit arguments to confined sibling iterator
helpers. Existing entry/body verification and an exact argument count protect
the boundary. The existing inliner binds each call's already-evaluated arguments,
then its capture cell identities. The scalar entry rewrite preserves argument
snapshots, ordered state writes and ordinary results through branches and loops.
Complete family, call, symbol, typed DOM and budget checks still precede native
publication; no new runtime representation was introduced.

The exact saved argument writer executes unchanged. Two additional sources
exercise `read(emitted, emitted += closed)` and argument-driven loop/return
branches. The first argument must retain the old value while the helper sees the
second argument's write. Ordinary/direct raw twins retain two explicit arguments
across intervening writes at five call sites, including repeated post-close
calls. Malformed signatures, wrong arities, escaping helpers, object arguments,
unknown symbols and sampled budgets retain fail-closed coverage. All 105 earlier
complete sources and 37 positive metadata tuples remain unchanged.

**2b3f6d24** evaluates both endpoint images when the existing index
interval and stride prove at most two possible values. Ordered endpoint bounds
and their exact output distance preserve sparse AND/OR/XOR results, including
signed-conversion jumps. Larger ranges retain the existing conservative proof.
The complete mutation/reload census and shared work budget remain. Thirty-one
CFG rows, 31 SCF rows and 31 source witnesses extend all 269 earlier bodies.
Thirteen historical raw refusals and four source claims now admit; their complete
sources remain unchanged and their exact read/owner expectations are checked.

## Focused validation

| Check | Result |
| --- | --- |
| Explicit affected devbox builds | PASS |
| Final exact `ctcompile_host_contract` | 1/1 PASS; 1.19 s test / 1.20 s total |
| Three new source admission preflights and all 74 refusal preflights | PASS |
| Seven-positive custom subset, including all 74 refusal programs | PASS; 112 native executions / 316 refusals / 56 Node/VM observations |
| Final exact `ctcompile_escape_analysis_arrays` | 1/1 PASS; 1.83 s test / 1.84 s total |
| AND and OR/XOR index-overwrite lit | 2/2 PASS; 0.15 s |

The custom subset selects the three new argument cases plus
`entry-captured-sibling-loop-writer`, `entry-captured-sibling-ordered-writer`,
`entry-captured-sibling-ordered-reader` and `entry-captured-ordered-state`.
It covers borrowed/owned DOM, both optimization policies, explicit/deduced output,
and GCC/Clang. It includes every source refusal, but does not run all positives.
Shared completion code did not change; nested iteration was not replayed.

All eight final code/test hashes match local files and the devbox. Native
production and source fixtures stayed unchanged through the passing execution
checks. Generated snapshot-case C++ was inspected: typed scalar loops and public
DOM calls preserve the argument values without Script or VM iterator symbols.
The existing native symbol checks also pass.

Local checks include 113 Node syntax checks and 30 exact new-source traces from
the source agent, then four finite-boundary Node observations; the added finite
source makes 114 complete programs. Escape checks validate 31 new exact Node
outputs and six historical outputs used to promote old expectations. These are
separate from the measured Node/VM differential observations above.

Every devbox operation held `/tmp/ctbrowser-devbox-build.lock`. Affected builds
used explicit targets:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

Exact test commands were:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitand|bitor-xor)-index-overwrite[.]test$' -o /tmp/ctcompile-iteration57-escape-lit.json
```

The exact source wrapper/flags are retained in
`ctcompile-iteration57-native-source.sh` and its Python wrapper in the artifacts.
The first host run failed two fixture assertions because wrong-arity direct
calls already fail parser verification. Those complete hostile sources now
explicitly assert parser refusal; ordinary wrong arities still exercise the
normalizer. All positive raw assertions passed that first run.
The first arrays run failed 46 assertions across the 13 older refusal cases now
proved by the extension; all new cases passed. Their ownership/read expectations
and four corresponding source claims were updated after independent Node checks.
Production stayed unchanged. An initial source run stopped before execution on
a formatting-only LoopProof hash mismatch; rebuilding synchronized final sources
resolved it. The source agent hit a rate limit after writing its final finite
boundary; root verified and retained the completed draft.

`tools/format.sh --check` retains 16 pre-existing diagnostics in four untouched
files: `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Changed C++ passes the pinned
formatter; Python passes Black/AST; diff whitespace and temporary shell syntax
checks pass. The earlier formatter run also saw in-flight escape edits.

No full CTest/compiler lit, complete custom case, unchanged nested/dataset case,
broad corpus/native matrix, WPT/test262, Windows, additional sanitizer, local
build or push ran. The devbox idle timer is active/enabled. Logs, scripts, hashes,
source/IR witnesses and generated C++ are at
`../test-results/2026-09-22-iterator-arguments/`.

## Exact next boundary

The preserved `entry-captured-sibling-loop-break-writer` source has SHA-256
`635649b4fae50fd6e78cb2421050ad26c5787f6b0eee376da1cbde4ac0d9b6f7`.
Its zero-initialized helper breaks before advancing either state cell, so the
later `while (emitted < 1) emitted += closed` cannot progress. Keep the complete
source as a refusal; do not promote it into an execution test without accounting
for this behavior.

Use the separately labeled `entry-captured-sibling-loop-break-finite-writer`
for execution work. It changes only `closed`'s initializer to one and has SHA-256
`a590dedea54817da6c0391f471c0b71e62785d6332908abd5134d76d34856967`.
Node returns **1258 normally / 1651 when stopping**, including repeated calls;
the stop path writes `data-closed=false`. Retain those exact observations.
Both variants include breaks in the sibling helper and iterator `next` loop.

Both sources return status 1 under both optimization policies:

```text
native DOM source: DOM iterator sibling helper must be a scalar leaf
```

Four stderr captures preserve these refusals. No native execution of either
break-boundary source is claimed.

Next compose the existing method-break completion proof with the sibling helper
before its scalar leaf/body checks, preserving every argument, state update,
ordinary result and shared continuation. Nested custom opens, abrupt iterator
close, literal range-for printing, unguarded Bootstrap defaults and the application
driver remain unfinished. Full Bootstrap is not admitted and no vendor coverage
gain is claimed. Larger nonmonotone mask gaps still need a richer range proof.
