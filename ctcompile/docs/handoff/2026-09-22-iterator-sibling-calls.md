# Sibling iterator call trees and two-value shifts, 2026-09-22

Resumed clean `6ebf1280` and the nested sibling-helper source recorded in
HANDOFF, plan 00 and iteration 58's journal. No unfinished tree work remained.
`codex-wip-20260907` exists but is already an ancestor of `ctcompile-v1`; other
branches were preserved. Linux inspection found 18 readable executable identities,
57 permission errors and no Claude match; Windows CIM checked 346 identities
with no match. Status remained uncertain, so concurrent-agent rules applied.
No browser, shared implementation or runtime-oracle semantics changed.

## Landed

**d4e18328** extends the custom iterator's existing sibling proof to confined
call trees. Single-initialized root-local callable cells connect the complete
helper family, including wrappers with only callable captures. Every capture,
initializer, callable observer, ordinary/direct call, symbolic use and cycle is
checked before expansion. Callable stores, escaping values, unproved captures,
wrong direct targets and recursion refuse. The existing inliner receives each
call's evaluated scalar arguments, state-cell identities and callable identities;
the existing entry rewrite carries ordered state and ordinary results. Cloned
calls use the existing depth limit and shared work budget. No runtime closure,
cell representation or dispatch table was added.

The exact saved nested writer, SHA-256
`4460f66c11b76fe9430b3ade992752e6f14108835b7dc482b19593dd095605f4`, executes
unchanged: **2724 normally / 3598 on stop**, with `data-closed=false`.
A three-level shared-callee source returns **2729 / 3603**. An inner-argument
snapshot source returns **42651 / 51971** while its second argument updates the
first argument's captured cell. Ordinary/direct raw twins retain five call-site
observations, current two-cell state and prior results. Existing no-boxing,
root-next and sampled-budget assertions cover both. All 120 earlier complete
source bodies and 43 positive metadata tuples remain unchanged.
The nonprogressing zero-state break source remains compile-only and is never run.

**9d5506d4** reuses exact endpoint images for shifts whose input interval and
stride contain at most two values. This covers signed/unsigned conversion jumps,
left-shift output wraps and uneven right-shift gaps without a new range domain.
Larger ranges retain the prior conservative checks. Twenty-eight CFG rows,
twenty-eight SCF rows and twenty-two source witnesses cover signed counts,
conversion boundaries, overlapping and disjoint reloads, later writes and
retained/saved child ownership. All 106 historical source bodies remain unchanged;
the historical singleton high-bit stride expectations now admit. Complete
store/reload census and work limits remain in force.

## Focused validation

| Check | Result |
| --- | --- |
| Explicit affected devbox builds | PASS |
| Three new source admissions and all 82 refusal preflights | PASS |
| Seven-positive custom subset, including every source refusal | PASS: 112 native executions, 332 refusals, two nonexecuted admissions, 56 Node/VM observations |
| Exact `ctcompile_host_contract` | 1/1 PASS; 1.30 s test / 1.31 s total |
| Exact `ctcompile_escape_analysis_arrays` | 1/1 PASS; 1.80 s test / 1.81 s total |
| Left/right-shift index-overwrite lit | 2/2 PASS; 0.13 s |

The custom subset selects the three new cases and the previous
`entry-captured-sibling-loop-break-finite-writer`,
`entry-captured-sibling-loop-break-argument-snapshots`,
`entry-captured-sibling-loop-break-argument-control` and
`entry-captured-sibling-ordered-writer`. It covers borrowed/owned DOM, both
optimization policies, explicit/deduced C++ and GCC/Clang. The complete custom
case was not replayed. Shared completion and emission code did not change;
unchanged nested and dataset cases were skipped.

Local validation includes 129 complete-source syntax checks and 40 exact Node
traces, plus all 128 shift source functions and 22 exact new outputs. A local
source-validator invocation initially lacked `PYTHONPATH`; rerunning with the
repository's documented `PYTHONPATH=ctcompile/test` passed. All eight final
code/test hashes match local files and the devbox. Generated snapshot C++ was
inspected: evaluated arguments and ordered scalar state remain, public DOM APIs
handle browser operations, and no Script or VM iterator protocol references
appear. Existing native symbol checks pass. The raw agent reached a rate limit
only after freezing its completed fixtures and finishing a read-only review.
The devbox idle timer is active/enabled.

All devbox commands held `/tmp/ctbrowser-devbox-build.lock`. Explicit targets:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

Exact tests:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(left-shift|right-shift)-index-overwrite[.]test$' -o /tmp/ctcompile-iteration59-escape-lit.json
```

The exact source wrapper and flags are preserved in
`ctcompile-iteration59-native-source.sh` and its Python wrapper. Logs, hashes,
Node checks, source/IR witnesses and generated C++ are at
`../test-results/2026-09-22-iterator-sibling-calls/`.

Required `tools/format.sh --check` reports the same sixteen existing diagnostics
in four untouched files: `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Changed C++ passes the pinned
formatter; Python passes Black/AST; diff whitespace and temporary shell syntax
checks pass. Documentation needs no build or CTest.

No full CTest/compiler lit, complete custom case, unchanged nested/dataset case,
broad corpus/native matrix, WPT/test262, Windows, additional sanitizer, local
build or push ran. No full-Bootstrap admission or vendor coverage gain is claimed.

## Exact next boundary

`entry-captured-sibling-callable-argument-writer`, SHA-256
`bc38fced1d1c9a1e15b48ce58d375b07098d3c1251f3cb3b0b945652f9b35280`, retains
the complete saved nested writer but passes `advance` to
`const relay = (writer) => writer()`. Node returns **2724 normally / 3598 on
stop**, with `data-closed=false`. Both native policies return status 1:

```text
native DOM source: DOM iterator sibling call requires an immutable local helper
```

Both stderr captures are saved; this boundary was not executed natively. Next
carry confined callable argument identity through the complete call census,
retaining shared state, each evaluated argument and every ordinary result.
Nested custom opens, abrupt iterator close, literal range-for printing, unguarded
Bootstrap defaults and the application driver remain unfinished. Part 25 still
has larger discontinuous shift ranges and uneven sparse-gap unions to prove.
