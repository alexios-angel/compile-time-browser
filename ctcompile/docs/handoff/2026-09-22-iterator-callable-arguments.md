# Callable iterator arguments and rounded shift gaps, 2026-09-22

Resumed clean `6e2082e9` and the exact callable-argument source recorded in
HANDOFF, plan 00 and iteration 59's journal. No unfinished tree edits remained;
`codex-wip-20260907` was already merged. Other branches were preserved.
Linux inspection found 19 readable executable identities, 56 permission errors
and no Claude match. Windows CIM checked 347 identities with no match. Status
remained uncertain, so concurrent-agent rules applied. No browser, shared
implementation or runtime-oracle semantics changed.

## Landed

**a9fe119c** propagates confined callable identities through explicit helper
arguments. A bounded fixed point connects forwarding parameters, followed by a
complete actual/formal census and checks of every callable observer. Each formal
must have one immutable target across all callers. Unknown actuals, conflicting
targets, escapes, mutation, wrong direct-call targets and recursion refuse.
Existing inlining binds the actual SSA arguments at each invocation, preserving
their evaluation snapshots, current scalar state and ordinary results. No runtime
callable representation, dispatch table or collector was added.

The exact saved source, SHA-256
`bc38fced1d1c9a1e15b48ce58d375b07098d3c1251f3cb3b0b945652f9b35280`, now executes
unchanged: **2724 normally / 3598 on stop**, with `data-closed=false`.
Interleaved callable/scalar arguments return **42651 / 51971**; shared forwarding
returns **2729 / 3603**. Ordinary/direct raw twins check five observation points,
current state, argument snapshots, closure retirement and sampled budgets.
All 129 historical complete sources and 46 positive expectation tuples remain.
The nonprogressing zero-state break source is still compile-only, never executed.

**ad228463** retains sparse right-shift output strides when all rounded steps
are equal. Within the existing conversion band, each increment is `q` or `q+1`.
An integral average over the complete input lattice therefore proves that every
increment is identical. Other uneven ranges remain dense. Eight CFG and eight
SCF rows cover signed/unsigned sparse reloads, overlapping counts, later writes
and mixed increments. Six new source witnesses cover confinement, retained
children and the same refusal boundaries. All 66 historical right-shift bodies
and their call tails remain unchanged. Existing work limits and the full
store/reload census remain in force.

Three agents worked on source tests, raw tests and the escape proposal. The
escape agent reached a rate limit before editing; the parent implemented and
checked its bounded proposal. The raw agent reached a rate limit after freezing
its completed tests and reviewing the production proof. No unfinished agent
edits remain.

## Focused validation

| Check | Result |
| --- | --- |
| Explicit affected devbox builds | PASS |
| Three new native source admissions and all 88 refusal preflights | PASS |
| Seven-positive custom subset, including every source refusal | 112 native executions, 344 refusals, two nonexecuted admissions, 56 Node/VM observations |
| Exact `ctcompile_host_contract` | 1/1 PASS; 1.34 s test / 1.35 s total |
| Exact `ctcompile_escape_analysis_arrays` | 1/1 PASS; 1.83 s test / 1.84 s total |
| Left-shift index-overwrite lit | PASS in the initial two-case selection |
| Corrected right-shift index-overwrite lit | 1/1 PASS; 0.12 s |

The custom subset selects `entry-captured-sibling-callable-argument-writer`,
`entry-captured-sibling-callable-argument-snapshots`,
`entry-captured-sibling-callable-argument-shared`, the preceding nested-call
writer and snapshots, shared-call writer and loop-break argument snapshots.
It covers borrowed/owned DOM, both optimization policies, explicit/deduced C++
and GCC/Clang. The complete custom case was not replayed.

Two new escape refusal sources initially extended their arrays indefinitely.
The initial right-shift lit process was stopped after the two-case run reached
97.89 seconds; that run was not a pass. Only this task's exact type-oracle process
was terminated while its gate held the build lock. The overlapping-count source
now bounds its write by the array length, and the later count store uses four.
Production and raw tests were unchanged. Only right-shift lit was rerun; the
earlier arrays and left-shift checks were not repeated.

Local checks pass: 138 native-source syntax checks and 40 exact Node traces;
the complete 72-function right-shift source executes in Node, with six exact new
results checked separately. All seven final code/test hashes match local/devbox
files. The final `ctjs-opt` was relinked after the independent escape change.
Generated callable-snapshot C++ was inspected: scalar state and public DOM calls
remain, without Script or VM iterator protocol references. The existing native
symbol checks pass. The devbox idle timer is active/enabled.

All devbox build/test commands held `/tmp/ctbrowser-devbox-build.lock`.
Explicit build targets were:

```text
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference
ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays
ctcompile-test-escape-claims ctcompile-test-type-oracle
```

CTest used exact anchored names with `--output-on-failure --no-tests=error`.
Lit used the generated `build/ctcompile/test` configuration and exact filters for
`Analysis/Escape/escape-claims/left-shift-index-overwrite.test` and
`right-shift-index-overwrite.test`. Source wrapper, command scripts, logs, hashes,
source witnesses, generated C++ and both boundary stderr captures are preserved
at `../test-results/2026-09-22-iterator-callable-arguments/`.

Required `tools/format.sh --check` reports the same sixteen existing diagnostics
in four untouched files: `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Changed C++ passes the pinned
formatter, Python passes Black/AST, and diff whitespace and temporary shell
syntax checks pass. Documentation needs no build or CTest.

No full CTest/compiler lit, complete custom case, unchanged nested/dataset case,
broad corpus/native matrix, WPT/test262, Windows, additional sanitizer, local
C++ build or push ran. No full-Bootstrap admission or vendor coverage gain is
claimed.

## Exact next boundary

`entry-captured-sibling-returned-callable-writer`, SHA-256
`7783136d98a1938ca3214bbeaf9c2dfbf93c36dd8c68ae7fabaccc56377b7e08`, returns the
passed `writer` from `identity(writer)` and invokes that result. Node returns
**2724 normally / 3598 on stop**, with `data-closed=false`. Both native policies
return status 1:

```text
native DOM source: DOM iterator sibling call requires an immutable local helper
```

Both stderr captures are saved; this boundary has not executed natively.
Next carry callable return provenance through the complete caller/observer proof
without changing state snapshots or ordinary results. Different targets at one
parameter, nested custom opens, abrupt iterator close, literal range-for printing,
unguarded Bootstrap defaults and the application driver remain unfinished.
Part 25 still has larger discontinuous shifts and mixed sparse-gap unions to prove.
