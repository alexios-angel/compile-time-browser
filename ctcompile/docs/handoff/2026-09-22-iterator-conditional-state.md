# Conditional iterator state and XOR complements, 2026-09-22

Continued clean `f1eb9201` from the conditional-captured-store boundary in
HANDOFF, plan 00 and the iteration 49 journal. No predecessor edits were dirty;
`codex-wip-20260907` was absent. Recent compiler, browser, Shell and WPT history
was read. Complete Linux process identities (73 processes) and Windows CIM
(349 processes) showed no Claude executable, CLI or loop. No browser/shared
implementation or runtime-oracle semantics changed.

Root implemented conditional state transport. Three agents independently
handled source regressions, raw proof controls/review, and Part 25 escape
precision. All builds and compiler/native tests used the devbox under the
shared lock. The two implementations were committed separately after their
focused gates passed.

## Changes

**a4c2d5d2** extends confined iterator receiver/capture state to nested `if`
arms. A complete access census permits only conditional ancestors between a
state read/write and its method root; loop-local mutation remains refused.
Existing body preflight checks dominance, exact capture slots, complete Boolean
arms, yield/result correspondence and shadow frames before rewriting.

Each rewritten branch retains its original result prefix and effects, then
yields the current scalar state tuple. Both arms start from the same incoming
values. Reads after writes use the latest value; an unwritten arm preserves its
input. Nested joins and later operations use the selected results. Receiver and
captured state share this transport, including conditional close-hook updates.
Complete DOM proof still checks types, effects and owner lifetimes before
publication. Generic helper capture admission remains immutable.

The original conditional-captured-store and conditional-state-store source
bodies are promoted unchanged. Two additional sources check nested two-cell and
two-field updates, order and close values. Advancing execution reaches
exhaustion; a stop attribute gives a finite break without advancement. All
eleven earlier positive tuples and all 46 earlier refusal source bodies remain
(two move to positives). Six new refusal sources cover non-Number state,
unknown effects, escaping receivers, nested captures and method-local loops.
Raw checks retain result prefixes, arm-local effects, sequential dependencies,
unwritten arms, latest close values, source-preserving refusals and sampled
budget cutoffs. Generated C++ uses typed scalar loops and public DOM calls;
there is no boxed iterator state, VM protocol or Script dependency.

**5805be25** extends the existing bitwise identity proof to XOR with all-one
bits. Complement is affine within one signed conversion band: the existing
Number bitwise evaluator transforms both endpoints, then their order reverses.
The full stride, including odd strides, survives. Conversion discontinuities,
overlapping reloads, later stores and noncomplement masks retain their existing
conservative proofs. Twelve CFG rows, twelve SCF rows and twelve source
witnesses cover both operand orders, signed/unsigned mask spelling, all three
conversion bands, retained children and refusal controls. All 84 original
source bodies and calls remain unchanged.

## Focused validation

| Check | Measurement |
| --- | --- |
| Exact host-contract CTest | 1/1 PASS, **0.88 s test / 0.89 s total** |
| Custom iteration lit | 1/1 PASS, **404.35 s total**; **240 native executions, 460 refusals, 120 Node/VM observations** |
| Exact escape arrays CTest | 1/1 PASS, **1.63 s test / 1.64 s total** |
| OR/XOR and AND overwrite lit | 2/2 PASS, **0.12 s total** |

The initial explicit build passed. Its arrays test found one malformed new
Structured near-mask control: `neg %two` preceded `%two`'s definition. An exact
`-2` Number literal repaired that test without changing production or mask
semantics. The native tests ran against the initial hashed build; the corrected
escape build and tests ran afterward. All seven final code/test hashes match
their passing gates. No production fix was needed after the first build.

Initial explicit targets through `tools/remote-build.sh`:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

The correction rebuilt only the last three targets. Exact tests from the synced
devbox repository root, under `/tmp/ctbrowser-devbox-build.lock`:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-custom-iteration[.]test$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand)-index-overwrite[.]test$'
```

Required `tools/format.sh --check` retains 16 pre-existing diagnostics in untouched
`ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp` (the latter three under
`ctcompile/lib/CTNative`). Changed C++ passes pinned clang-format; changed Python
passes Black/AST. Whitespace and runner syntax checks pass. Agent checks also
passed 65 native-source Node syntax checks, 60 exact Node traces and all 96
OR/XOR source calls.

No full CTest/compiler lit, broad corpus/native matrix, WPT/test262, Windows,
sanitizer, local build or push ran. Unchanged nested/dataset lit was not replayed;
its earlier passing measurements remain historical. Documentation updates need
no build or CTest. The devbox idle timer was restored and verified active/enabled.

Artifacts at `../test-results/2026-09-22-iterator-conditional-state/` beside the
monorepo include logs, lit JSON, source hashes, exact runners, the four new
positive sources, the next refusal and explicit/deduced C++ examples.

## Next boundary

Start with `refusals()["loop-captured-store"]` in
`ctcompile/test/CTNative/Browser/native_dom_custom_iteration.py`. The complete
source remains refused under both optimization policies. Its `next` method uses
`while (emitted < 1) emitted++`; the loop is finite. Extend state transport across
the method's before/after recurrence only after proving the complete loop,
effects, capture identity and latest close values. The current conditional
ancestor check deliberately excludes this case.

External cell observations, nested closures/custom opens, body return/throw close
behavior, literal range-for printing, unguarded Bootstrap defaults and the
application driver remain open. Full Bootstrap is not admitted. Richer bitwise
range unions remain separate from exact complement handling. Historical
conformance crashes and WPT event regressions remain separate follow-up work.
