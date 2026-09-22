# Sibling iterator helper breaks and two-value complements, 2026-09-22

Resumed clean `e26b8342` and the exact finite sibling-helper break boundary
recorded in HANDOFF, plan 00 and iteration 57's journal. No unfinished tree work
remained; `codex-wip-20260907` was absent and other branches were preserved.
Linux inspection found 19 readable executable identities, 56 permission errors
and no Claude match; Windows CIM checked 344 identities with no match. Status
remained uncertain, so concurrent-agent rules applied. No browser, shared
implementation or runtime-oracle semantics changed.

## Landed

**5b6c909a** applies the existing completion proof to confined sibling iterator
helpers before their scalar-body census. This resolves proved loop breaks while
retaining argument snapshots, ordered captured-state writes and ordinary results.
The existing inliner and scalar entry rewrite carry that state through each
call, loop exit and close. Exact closure/call/symbol checks, complete typed DOM
reproof and work budgets still gate publication. Shared completion and emission
code did not change; no runtime representation was added.

The saved finite source executes byte-identically, returning **1258 normally /
1651 on stop**, with `data-closed=false`. Two additional complete sources check
argument evaluation and argument-selected breaks. Ordinary/direct raw twins
check five call positions, ordered state on break/continue/exhaustion, repeated
post-close calls and absence of boxed state. Live poison, observed completion
tags and sampled budgets remain refused. All 114 previous complete source
bodies and 40 previous positive metadata tuples remain unchanged.

The original zero-state source, SHA-256
`635649b4fae50fd6e78cb2421050ad26c5787f6b0eee376da1cbde4ac0d9b6f7`, also
compiles with the new proof. It cannot progress at runtime, so it is preserved
in `COMPILE_ONLY`, lowered under both policies and never executed. Its two
admissions are reported separately from native executions.

**11c87d97** reuses exact bitwise endpoint bounds for unary complement when a
proved range has at most two values across a signed-conversion jump. Larger
ranges still require the existing same-band proof. The existing stride domain,
complete store/reload census and work budget remain. Twelve CFG rows, twelve
SCF rows and twelve source witnesses cover both jumps, repeated two-value
intermediates, saved/retained children and overlap/later-store refusals. All
twenty previous bitnot source functions remain unchanged.

## Focused validation

| Check | Result |
| --- | --- |
| Explicit affected devbox builds | PASS |
| Three new source admissions and all 76 refusal preflights | PASS |
| Seven-positive custom subset, including every source refusal | PASS: 112 native executions, 320 refusals, two nonexecuted admissions, 56 Node/VM observations |
| Final exact `ctcompile_host_contract` | 1/1 PASS; 1.39 s test / 1.41 s total |
| Exact `ctcompile_escape_analysis_arrays` | 1/1 PASS; 1.82 s test / 1.83 s total |
| Bitnot, AND and OR/XOR index-overwrite lit | 3/3 PASS; 0.19 s |

The custom subset selects the three new break cases and the previous
`entry-captured-sibling-argument-snapshots`,
`entry-captured-sibling-argument-control`, `entry-captured-sibling-loop-writer`
and `loop-captured-break-ordered-close`. It covers borrowed/owned DOM, both
optimization policies, explicit/deduced C++ and GCC/Clang. The complete custom
case was not replayed. Shared completion code did not change; unchanged nested
and dataset cases were skipped.

The first exact host run failed eight assertions in a new poison control: its
poisoned loop-exit value was dead and could validly disappear. The corrected
control poisons the live initial trip read by the guard. All positive raw
assertions passed the first run; final host validation passes with the corrected
control. Production code was unchanged. The raw agent resumed after a rate
limit; the source agent was interrupted after freezing its completed draft.

Local checks include 120 complete-source syntax checks and 40 exact Node traces,
plus all 32 bitnot source functions and twelve exact new results. All seven
final code/test hashes match local files and the devbox. Generated snapshot C++
was inspected: typed scalar loops retain the evaluated arguments and call public
DOM APIs without Script or VM iterator symbols. Existing native symbol checks
pass. The devbox idle timer is active/enabled.

All devbox commands held `/tmp/ctbrowser-devbox-build.lock`. Explicit targets:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

Exact test commands:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitnot|bitand|bitor-xor)-index-overwrite[.]test$' -o /tmp/ctcompile-iteration58-escape-lit.json
```

The exact source wrapper and flags are preserved in
`ctcompile-iteration58-native-source.sh` and its Python wrapper. Logs, hashes,
Node checks, source/IR witnesses and generated C++ are at
`../test-results/2026-09-22-iterator-sibling-breaks/`.

Required `tools/format.sh --check` reports the same sixteen existing diagnostics
in four untouched files: `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Changed C++ passes the pinned
formatter; Python passes Black/AST; diff whitespace and temporary shell syntax
checks pass. The initial format run also saw unfinished agent drafts.

No full CTest/compiler lit, complete custom case, unchanged nested/dataset case,
broad corpus/native matrix, WPT/test262, Windows, additional sanitizer, local
build or push ran. No full-Bootstrap admission or vendor coverage gain is claimed.

## Exact next boundary

`entry-captured-sibling-nested-call-writer`, SHA-256
`4460f66c11b76fe9430b3ade992752e6f14108835b7dc482b19593dd095605f4`, retains
the finite source and moves an ordered state update into a second helper called
by the first. Node returns **2724 normally / 3598 on stop**, with
`data-closed=false`. Both native policies return status 1:

```text
native DOM source: DOM iterator sibling helper escapes or has an unsupported call
```

Both stderr captures are saved; this source was not executed natively. Next
prove the confined helper call tree while retaining each call's current shared
state, argument snapshots and ordinary result. Nested custom opens, abrupt
iterator close, literal range-for printing, unguarded Bootstrap defaults and the
application driver remain unfinished. Part 25 still refuses conversion jumps
with more than two possible values; exact two-value shifts are another bounded
continuation to investigate.
