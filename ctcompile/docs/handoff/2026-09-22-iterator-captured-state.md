# Confined iterator captures and identity masks, 2026-09-22

Continued clean `830181ec` from the captured-counter boundary in HANDOFF,
plan 00 and the iteration 48 journal. No predecessor edits were uncommitted;
`codex-wip-20260907` was absent. Recent compiler, browser and Shell commits were
read. Linux process identities (82 processes) and Windows CIM (347 processes)
showed no Claude executable, CLI or loop. No browser/shared implementation or
runtime-oracle semantics changed.

Root implemented native capture state. Three agents handled source regressions,
raw proof controls/review, and independent escape precision. The escape agent
froze its files before hitting a rate limit; root took over its claims and
corrected the historical OR expectation. All builds and tests used the devbox
under the shared lock.

## Changes

**76a7933e** admits mutable Number cells captured only by one confined iterator's
unique `next` and `return` methods. Cells are root-local and initialized by a
Number literal, directly or with one assignment before all capturing methods.
The complete use census excludes external readers/writers and other closures.
Method loads/stores use exact capture slots in the outer block; methods with
mutable captures must be leaves. Arity, enclosure, target identity, symbolic uses,
capture counts and initialization order are checked before rewriting.

Captured cells join the existing receiver-state scalar tuple. Ordered updates
preserve cell identity across methods, even when capture orders differ. Pruning
only those capture slots reindexes the remaining immutable captures. Existing
helper expansion removes the private result records and cells. Each invocation
resets state; close sees the latest values, while exhaustion skips close.
The shared body preflight validates store operands structurally before replacement;
generic immutable capture admission and final body checks still reject stores.
Complete DOM proof retains types, effects, lifetime and budget requirements.

The original saved captured-counter source executes byte-identically, alongside
a two-cell source checking ordered updates and close mutation. All nine earlier
positive tuples and all 37 earlier refusal source bodies are preserved (the
captured counter moves to the positive set). Raw tests cover four capture shapes,
twenty invalid shapes, category changes and sampled budget thresholds. Generated
C++ uses typed scalar loops and public DOM calls without boxed state or Script.

**d66f0bc0** preserves arbitrary positive index strides through exact identity
masks: OR/XOR zero and AND all-one bits. Within one signed conversion band,
ToInt32 is affine, including across zero. Endpoint conversion reuses the existing
bounded Number operation; discontinuous bands remain refused. Complete reload,
later-store and gap checks remain. Fifteen new sources extend the unchanged
original 69 OR/XOR sources; all 55 AND sources are unchanged. The original OR and
AND zero-crossing programs now have confined expectations.

## Focused validation

| Check | Measurement |
| --- | --- |
| Exact host-contract CTest | 1/1 PASS, **0.81 s test / 0.82 s total** |
| Custom iteration lit | PASS, **315.88 s**; **176 native executions, 356 refusals, 66 Node/VM observations** |
| Nested iteration lit | PASS, **166.34 s**; **48 native executions, two previous-source checks, 94 refusals** |
| Dataset lit | PASS, **87.59 s**; **112 Node/VM observations, eight GCC/Clang binaries, lifetime sanitizer, 432 refusals** |
| Exact escape arrays CTest | 1/1 PASS, **1.78 s test / 1.79 s total** |
| OR/XOR and AND overwrite lit | 2/2 PASS, **0.14 s total** |

The three native lit cases passed together in **315.89 s**. Four final native
source hashes match that gate; five final escape hashes match its later gate.
The initial explicit build passed. Its arrays test found one historical OR
zero-crossing refusal now admitted (six failed assertions for one row).
Root promoted that unchanged raw source and its unchanged source witness;
production did not change. The focused arrays and two escape lit rerun passed.

Explicit initial build targets, through `tools/remote-build.sh`:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

The escape rerun built only the last three targets. Exact tests, from the synced
devbox repository root under `/tmp/ctbrowser-devbox-build.lock`:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(custom-iteration|nested-iteration|dataset)[.]test$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand)-index-overwrite[.]test$'
```

Required formatting retains the same **16 pre-existing diagnostics** in four
untouched files. Changed C++ passes pinned clang-format; changed Python passes
Black/AST. Source preservation, whitespace and runner syntax checks pass.
Agent checks also passed 57 native-source Node syntax checks, 33 exact Node
traces, and execution/syntax of all 84 OR/XOR and 55 AND source functions.
No full CTest/compiler lit, broad corpus/native matrix, WPT/test262, Windows,
additional sanitizer, local build or push ran. The dataset fixture includes its
existing lifetime sanitizer. Documentation-only updates need no build or CTest.
The devbox idle timer was restored and verified active/enabled.

Artifacts at `../test-results/2026-09-22-iterator-captured-state/` beside the
monorepo include all logs, lit JSON, source hashes, runners, preserved sources,
and explicit/deduced native C++ examples.

## Next boundary

Start with `refusals()["conditional-captured-store"]` in
`ctcompile/test/CTNative/Browser/native_dom_custom_iteration.py`. It remains a
complete-source refusal under both optimization policies. Its `next` increments
the captured cell only when `anchor.hasAttribute('advance')` is true. Prove
branch-local state updates and their scalar joins without discarding effects or
changing close state. Execution witnesses can set `advance` for exhaustion or
`stop` for a finite break without advancement. Conditional receiver writes are
the sibling boundary.

External cell observations, nested closures/custom opens, body return/throw close
behavior, literal range-for printing, unguarded Bootstrap defaults and the
application driver remain open. Full Bootstrap is not admitted. Nonidentity
bitwise masks still use their conservative lattice/range proof; richer signed
range unions remain separate. Historical conformance crashes and WPT event
regressions remain separate follow-up work.
