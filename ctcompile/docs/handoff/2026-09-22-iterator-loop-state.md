# Iterator method loops and fixed-bit AND bounds, 2026-09-22

Continued clean `25a6edff` from the `loop-captured-store` boundary in HANDOFF,
plan 00 and the iteration 50 completion journal. No predecessor edits were dirty;
`codex-wip-20260907` was absent. Other branches were preserved. Recent compiler,
browser, Shell and WPT history was read. Complete Linux process identities
(71 processes) and Windows CIM (341 processes) showed no Claude executable,
CLI or loop. No browser/shared implementation or runtime-oracle semantics changed.

Root implemented method-loop state transport. Three agents independently handled
source regressions, raw proof controls/review, and Part 25 escape precision.
All builds and compiler/native tests used the devbox under the shared lock.
The two implementation concerns were committed separately after focused checks.

## Changes

**cf0e4580** extends the existing conditional state transport to method
`while` regions. The complete access census allows only `if`/`while` ancestors;
existing body preflight checks both regions, original arguments/results,
condition/yield correspondence, dominance and shadow-frame consistency before
rewriting. Cell identity, literal Number initialization, confinement, unique
ordinary methods, immutable capture reindexing and work/depth budgets remain.

The same rewrite appends state to loop initial values, both region argument
lists, condition/yield edges and results. Original result positions remain the
prefix. Reads after writes use the latest scalar. The condition forwards its
updated values both into the body and out of the loop, including zero body trips.
Nested branches retain their ordered scalar joins. Complete DOM reproof still
checks Number recurrences, all effects and borrowed element lifetimes before
publication. Output uses existing typed C++ loops and public DOM calls without
boxed iterator state, VM protocol or Script dependencies.

The saved `loop-captured-store` body is promoted byte-identically. Paired captured
and receiver sources check two state values, an ordinary local loop counter,
zero-trip exhaustion, ordered close-hook loops and fresh invocation state.
All fifteen previous positive tuples and all fifty previous refusal bodies remain
(one moves to positives). Seven new loop controls cover invalid categories,
unknown calls, receiver escape, nested captures, external cell reads and a
conditional method break. Raw tests promote both prior zero-trip loops unchanged
and verify exact init/condition/yield/exit edges, original result prefixes,
region-local effects, latest close state, malformed loops and budget cutoffs.

**06814db5** lets low-bit AND use the existing fixed-input-bit interval bound.
Bits above an interval's highest differing bit stay fixed instead of being
widened to the entire mask. Sign/conversion crossings retain the previous safe
full-width enclosure; lattice residues, complete reload/store census and budgets
remain. The original raw `i & 2` source is preserved as a retained-child positive;
an actual out-of-bounds neighbor retains a refusal. Twelve AND source witnesses
extend the unchanged original fifty-five, with seven positives and five refusals.
CFG/SCF cases check translated bounds, operand order, signed inputs, retained
children, disjoint and overlapping reloads, and later writes. OR/XOR behavior
is unchanged.

## Focused validation

| Check | Measurement |
| --- | --- |
| Exact host-contract CTest | 1/1 PASS, **0.83 s test / 0.84 s total** |
| Custom iteration lit | 1/1 PASS, **488.40 s total**; **288 native executions, 544 refusals, 144 Node/VM observations** |
| Exact escape arrays CTest | 1/1 PASS, **1.67 s test / 1.68 s total** |
| AND and OR/XOR overwrite lit | 2/2 PASS, **0.13 s total** |

The initial explicit build and both exact CTests passed. All seven code/test
hashes match the synced devbox inputs. The custom source gate passed without
corrections; no production or fixture changed after the initial build. Final
hashes were also verified locally and on the devbox after testing.

Explicit targets through `tools/remote-build.sh`:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

Exact tests from the synced devbox repository root, under
`/tmp/ctbrowser-devbox-build.lock`:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand)-index-overwrite[.]test$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-custom-iteration[.]test$'
```

Required `tools/format.sh --check` retains sixteen pre-existing diagnostics in
untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp` (the latter three under
`ctcompile/lib/CTNative`). Changed C++ passes pinned clang-format; changed Python
passes Black/AST. Whitespace and runner syntax checks pass. Agent checks also
passed 74 native-source Node syntax checks, 72 exact Node traces, all 67 AND
source calls and eleven array-retention observations.

No full CTest/compiler lit, broad corpus/native matrix, WPT/test262, Windows,
sanitizer, local build or push ran. Unchanged nested/dataset lit was not replayed;
its prior measurements remain historical. Documentation updates need no build
or CTest. The devbox idle timer was restored and verified active/enabled.

Artifacts at `../test-results/2026-09-22-iterator-loop-state/` beside the monorepo
include logs, lit JSON, source hashes, exact runner, the three new positive
sources, next refusal and explicit/deduced C++ examples.

## Next boundary

Start with `refusals()["loop-captured-break"]` in
`ctcompile/test/CTNative/Browser/native_dom_custom_iteration.py`. The complete
source increments captured state inside a finite method loop, then conditionally
breaks on `anchor.hasAttribute('stop')`. Its method-local `scf.index_switch`
remains outside body preflight: both optimization policies refuse with
`native DOM source: DOM helper requires complete structured branches`.
Normalize and prove that method completion before scalarizing its state,
preserving effects and the final values seen by iterator close.

The successful source driver does not retain error-stream diagnostics. The same
refusal was replayed once per optimization policy solely to save those diagnostics;
both returned status 1. No native execution is claimed for that source. The two
commands used the existing `loop-captured-break.mlir` and
`refused-loop-captured-break-{False,True}.json` manifests with
`ctjs-opt --ctnative-lower-to-emitc='host-manifest=... optimize={false,true}'`.
Their exact paths and diagnostics are retained with the artifacts.

External cell observations, nested closures/custom opens, body return/throw close
behavior, literal range-for printing, unguarded Bootstrap defaults and the
application driver remain open. Full Bootstrap is not admitted. Richer bitwise
range unions remain separate. Historical conformance crashes and WPT event
regressions remain separate follow-up work.
