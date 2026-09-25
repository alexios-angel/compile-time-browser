# Multiple loop exit values and constant OR masks, 2026-09-22

Continued clean `5f79f5fc` from HANDOFF, plan 00 and the final iteration 46
journal. The preceding repair and counted-exit threads were committed; there
was no uncommitted predecessor work or unmerged `codex-wip-20260907`.
Linux /proc inspected 75 processes and Windows CIM 349 with no Claude executable,
CLI or loop matches or read errors. No browser or runtime-oracle code changed.

Root extended the native completion proof. Three agents handled the source
fixture, independent raw proof controls/review, and the escape improvement.
Builds, tests and source probes were serialized on the devbox.

## Changes

**d0db67ad** extends pure exit selection to multiple independent results.
Every chosen loop slot must have the matching type, belong entirely to the
exit dispatch, and be inactive on continuing backedges. Distinct destinations
reuse existing loop slots. The entire selected tuple is read before any slot
is overwritten, preserving crossed and repeated selections. Exact predicates
and tags, complete use/effect proofs and work budgets remain required.
Iterator close stays after the loop. Generated C++ continues to use ordinary
typed scalar loops and public DOM calls, with no VM iterator or Script dependency.

The previous `two-projected-break-exits` source executes unchanged, with
normal/break/already-exhausted results `11/11/7`. A three-result source gives each
counter a different update and weights their final values using supported Add;
results `81/81/65` distinguish all six output permutations. Existing source bodies,
DOM effect order, exact close counts, borrowed/owned sessions, invalid/foreign
receivers and Node/VM source-double checks remain. Raw crossed and repeated
projections check each original selected SSA operand, seven further invalid
continuations, and incomplete budgets on fresh clones.

**11230889** proves an exact all-one OR mask produces -1 even when its input
crosses zero or a ToInt32 signed boundary. Operand proofs, complete stores,
reload alias checks and budgets remain required. CFG and SCF cases cover signed
and unsigned mask spellings and commuted operands, with near-mask/later-store
controls. Eight source witnesses extend the unchanged 50 original functions.

## Focused validation

| Check | Measurement |
| --- | --- |
| Exact escape arrays CTest | 1/1 PASS, **1.57 s total** |
| Exact host-contract CTest | 1/1 PASS, **0.70 s total** |
| OR/XOR and AND overwrite lit | 2/2 PASS, **0.12 s total** |
| Custom iteration lit | PASS, **203.18 s**; **112 native executions, 212 refusals, 42 Node/VM observations** |
| Nested iteration lit | PASS, **157.28 s**; **48 native executions, two previous-source checks, 94 refusals** |
| Dataset lit | PASS, **89.59 s**; **112 Node/VM observations, eight GCC/Clang binaries, lifetime sanitizer, 432 refusals** |

The three browser lit cases passed together in **203.19 s**. All seven final
code/test hashes match the devbox. These are two selected CTests and five
selected lit cases, not a full-suite pass. The devbox idle timer was restored
and verified active after testing.

Logs, lit JSON, source hashes, runners, explicit/deduced C++ examples and
next-boundary sources/IR/diagnostics are in
`../test-results/2026-09-22-multiple-break-exits/` beside the monorepo.

All builds use tools/remote-build.sh under /tmp/ctbrowser-devbox-build.lock.
Initial explicit targets:

```text
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference
ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays
ctcompile-test-escape-claims ctcompile-test-type-oracle
```

The second build selected the first five targets. Exact test commands:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand)-index-overwrite[.]test$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(custom-iteration|nested-iteration|dataset)[.]test$'
```

The first host run failed while parsing the new raw test's parenthesized
multi-result switch signature. Its unsupported BinaryStatic Sub spelling was
also corrected to Add. The exact crossed-operand assertions remain. Before
native lit, inspection found that the new source's Sub/Mul arithmetic would
require a separate DOM proof; its weighting now uses supported Add. The original
two-result source was unchanged. These were fixture fixes; production stayed
unchanged after its initial build.

Required tools/format.sh --check retains 16 pre-existing diagnostics in four
untouched files. Changed files pass scoped pinned clang-format/Black, AST and
whitespace checks. Temporary runners pass bash -n. No full CTest/compiler lit,
broad corpus/native matrix, WPT/test262, Windows, additional sanitizer run,
local build or push was performed. The selected dataset fixture includes its
existing lifetime sanitizer.

## Next boundary

Prove state stored on a confined custom iterator. The saved
`state-probes/mutable-receiver-counter.js` declares `emitted: 0`, reads it in
`next`, and increments it through `this`. Native compilation refuses with
`DOM custom iterator slots must be unique unconditional callables`.
The companion `mutable-captured-counter.js` uses a mutable captured scalar and
refuses with `DOM helper observes an implicit argument`. Both complete sources
refuse for both DOM providers and both optimization policies: **eight probes**.
They were replayed only to save diagnostics omitted by the first probe runner;
no native execution is claimed. Local Node checks verified two result/effect
observations per source. They have no new VM differential measurement.

The next proof must retain receiver/cell binding, mutation, exact next/done
behavior, confinement, effects and element ownership. No VM state object or
runtime iterator may enter generated code. Body return/throw close behavior,
nested custom opens, literal range-for printing, unguarded Bootstrap defaults
and the application driver remain open. Full Bootstrap is not admitted.
Higher OR-mask gaps still require a union of lattices; the historical conformance
crashes and WPT event regressions remain separate follow-up work.

The unused Sub/Mul version of the new three-output source is preserved separately
as `state-probes/three-projected-subtraction-unmeasured.js`. Its general DOM
arithmetic proof was inspected but not measured or implemented this session.
