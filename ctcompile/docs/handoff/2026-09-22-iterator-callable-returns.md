# Returned iterator callables and conversion-jump shifts, 2026-09-22

Resumed clean `5631b2d7` and the exact returned-callable source in HANDOFF,
plan 00 and iteration 60's journal. No unfinished tree edits remained;
`codex-wip-20260907` was already merged. Other branches were preserved.
Linux process inspection found 17 readable executable identities, 57 permission
errors and no Claude match; Windows CIM found no match among 345 processes.
Status remained uncertain, so concurrent-agent rules applied. No browser, shared
implementation or runtime-oracle semantics changed.

## Landed

**d3767892** carries exact callable identity through a helper's single root
return. The existing bounded fixed point follows forwarding arguments and return
results; every caller and result observer is checked before expansion. Unknown
returned callees, escapes, scalar observations, conflicting identities, wrong
direct targets and recursive calls refuse. A source-ordered pending list expands
returned-callable producers before consumers without depending on SSA hash order.
The existing inliner preserves each evaluated scalar argument and current shared
state at its source position. No runtime callable storage or dispatch was added.

The saved source, SHA-256
`7783136d98a1938ca3214bbeaf9c2dfbf93c36dd8c68ae7fabaccc56377b7e08`, executes
unchanged: **2724 normally / 3598 on stop**, with `data-closed=false`.
A state-writing return helper preserves earlier scalar arguments and returns
**109776 / 127479**; chained/shared callable returns produce **2729 / 3603**.
Ordinary/direct raw twins check current state, argument snapshots, ordered effects,
closure retirement and sampled work limits. All 138 historical complete sources,
49 positive expectation tuples and both native assertion strings remain intact.
The nonprogressing zero-state break source remains compile-only, never executed.

**48838685** bounds larger right shifts across ToInt32/ToUint32 conversion jumps
using the existing signed/unsigned dense interval before shifting. Exact two-point
bounds remain unchanged. Guard reloads use the conservative enclosure; actual
write replay retains unwritten children and earlier snapshots. Paired CFG/SCF
tests cover masked/negative shift counts, equal endpoint images hiding an interior
extremum, overlapping reloads, later stores, retained children and negative keys.
Three earlier right-shift rows per representation now admit with their original
bodies and checked ownership expectations; the adjacent left-shift refusals remain.
Twelve source witnesses extend all 72 prior right-shift bodies and call tails.

Three parallel agents handled raw tests, source tests and the escape increment.
An interruption left their drafts in place; resumed agents completed those same
tasks. The parent reviewed, gated and committed each concern separately.

## Focused validation

| Check | Result |
| --- | --- |
| Explicit affected devbox builds | PASS |
| Three new source admissions and all 94 refusal preflights | PASS |
| Seven-positive custom subset, including every source refusal | 112 native executions, 356 refusals, two nonexecuted admissions, 56 Node/VM observations |
| Exact `ctcompile_host_contract` | 1/1 PASS; 1.56 s test / 1.57 s total |
| Exact `ctcompile_escape_analysis_arrays` | 1/1 PASS; 1.91 s test / 1.93 s total |
| Left/right-shift index-overwrite lit | 2/2 PASS; 0.13 s total |

The custom subset selects the three new returned-callable sources, the preceding
callable-argument writer/snapshots/shared sources and the nested-call writer.
It covers borrowed/owned DOM, both optimization policies, explicit/deduced C++
and GCC/Clang. The complete custom case was not replayed.

The first host run failed four refusal assertions: an undefined returned callee
could survive normalization until the later complete DOM proof. The call census
now rejects that invocation before changing the source. The second run failed two
budget assertions because pending-call hash order changed dependency-scan cost.
Source-order scheduling fixed that variability. The final host run passes with
the raw tests unchanged. These two earlier runs were failures, not partial passes.

The right-shift oracle observes **252 sites, 54 sound confined sites and 54/63
confined precision**, with zero violations, unclaimed, partial or pending sites.
Left-shift observes **186 sites, 39 sound and 39/43 precision**, likewise with zero
violations, unclaimed, partial or pending sites. These are focused fixture results.
Local checks pass: 147 native-source syntax checks, 40 exact Node traces,
84-function right-shift source execution and 12 exact new results.

All seven final code/test hashes match local/devbox files. Generated snapshot C++
was inspected and the native symbol checks pass: scalar state and public DOM calls
remain without Script or VM iterator protocol references. The idle timer is
active/enabled. Every devbox build/test/collection command held
`/tmp/ctbrowser-devbox-build.lock`. Explicit build targets were:

```text
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference
ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays
ctcompile-test-escape-claims ctcompile-test-type-oracle
```

CTest used exact anchored names with `--output-on-failure --no-tests=error`.
Lit used `build/ctcompile/test` and the exact filter
`^ctcompile :: Analysis/Escape/escape-claims/(left-shift|right-shift)-index-overwrite[.]test$`.
Commands, logs, hashes, sources, generated C++ and both next-boundary stderr
captures are in `../test-results/2026-09-22-iterator-callable-returns/`.

Required `tools/format.sh --check` retains the same sixteen existing diagnostics
in `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Changed C++ passes the pinned formatter;
Python passes Black/AST, and diff whitespace and temporary shell syntax pass.
One scoped formatter command named a nonexistent unchanged file; the corrected
selection of the three changed escape C++ files passes. Documentation needs no
build or CTest.

No full CTest/compiler lit, complete custom case, unchanged nested/dataset case,
broad corpus/native matrix, WPT/test262, Windows, additional sanitizer, local C++
build or push ran. No full-Bootstrap admission or vendor coverage gain is claimed.

## Exact next boundary

`entry-captured-sibling-returned-callable-different-targets`, SHA-256
`21eba50b0711b7469fd00929f75b62175bb9763b18faf1d305f4bf9c7bbe22d0`, passes distinct
known writers through one returning identity helper. Node returns **2729 normally /
3603 on stop**, with `data-closed=false`. Both native policies return status 1:

```text
native DOM source: DOM iterator callable argument requires one immutable target
```

Both stderr captures are saved; this boundary has not executed natively.
Next prove callable identities per invocation while retaining complete callers,
observers, argument snapshots and current shared state. Callable branch/loop joins,
nested custom opens, abrupt iterator close, literal range-for printing, unguarded
Bootstrap defaults and the application driver remain unfinished. Part 25 still
has larger discontinuous left shifts and mixed sparse-gap unions to prove.
