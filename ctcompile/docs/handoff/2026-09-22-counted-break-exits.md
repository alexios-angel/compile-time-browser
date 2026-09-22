# Counted break exits and OR-mask proofs, 2026-09-22

Resumed clean `51425445` from HANDOFF, plan 00 and the final compiler-repair
journal. That repair thread was complete; its drivers were not restarted.
No uncommitted predecessor work or unmerged `codex-wip-20260907` remained.
Linux `ps` checked 71 processes and Windows CIM checked 342, with no Claude
executable, CLI or loop matches. No browser or runtime-oracle code changed.

Root implemented the native boundary. Three agents independently handled the
source fixture, raw completion proof checks/review, and the escape-analysis
change. Builds and tests were serialized on the devbox.

## Landed

**45029d8e** preserves one live scalar selected by a pure loop-exit dispatch.
On each proved false loop condition, its exact tag selects the defined break
or exhaustion value. Every consumed loop-result use must belong to that
dispatch, and its after-region uses must only forward into dropped arguments.
Inactive slots receive defined values only when the selected destination cannot
observe them. Unknown tags, observed poison and effectful selections still
refuse. Budgets are charged, and the complete helper/DOM proof remains required.

The custom-iterator copier now leaves done state unchanged across yield-only
switches. This preserves the original exit selection for completion analysis.
The close call stays after the loop: exhaustion skips it; break calls it once.
Output uses the existing typed C++ scalar loops and public DOM calls, without
a VM iterator, boxed record, collector or Script dependency.

The original `counted-break-exit` source now executes unchanged. Other positives
check an update after the break condition (normal/stop/exhausted results
`10/7/7`) and a second ordinarily carried counter (`11/8/7`). Existing normal
and effect-only break sources remain intact. Native clients check exact writes,
detached/foreign elements, invalid input rejection and owned sessions; source
doubles independently check Node/VM results and write order. Raw tests include
renamed exit tags, seven invalid continuations and late incomplete budgets for
both original and counted fixtures.

**cc0fc749** retains the low-bit residue forced by an OR mask in array overwrite
proofs. The input lattice and mask's trailing-one bits combine by taking the
larger power-of-two period. An all-one mask avoids a width-sized shift. Existing
signed-range and complete-store checks remain required. CFG/SCF tests cover
unit and odd strides, commuted operands, signed masks and overlapping writes;
ten source witnesses extend the unchanged original 40-source fixture.

## Focused validation

All builds used `tools/remote-build.sh` under
`/tmp/ctbrowser-devbox-build.lock`. Initial explicit targets were:

```text
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference
ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays
ctcompile-test-escape-claims ctcompile-test-type-oracle
```

Later native builds selected the first five targets. Exact tests used:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand)-index-overwrite[.]test$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(custom-iteration|nested-iteration|dataset)[.]test$'
```

After fixture-only fixes, only `native-dom-custom-iteration.test` was replayed.

| Check | Final relevant measurement |
| --- | --- |
| Host contract | 1/1 PASS, **0.69 s total**, including counted late-budget controls |
| Escape arrays | 1/1 PASS, **1.66 s total** |
| OR/XOR and AND overwrite lit | 2/2 PASS, **0.13 s** |
| Custom iteration lit | 1/1 PASS, **127.06 s**; **80 native executions, 166 refusals, 30 Node/VM observations** |
| Nested iteration lit | PASS, **140.52 s**; **48 native executions, two previous-source checks, 94 refusals** |
| Dataset lit | PASS, **69.94 s**; **112 Node/VM observations, eight GCC/Clang binaries, lifetime sanitizer, 432 refusals** |

These are two selected CTests and five distinct lit cases, not full-suite
results. All eight changed code/test hashes match the devbox. Required
`tools/format.sh --check` still reports **16 pre-existing diagnostics in four
untouched files**. Changed C++/Python files pass pinned formatting, Black, AST
and whitespace checks. The temporary runner passes `bash -n`.

The initial host gate failed four new raw positives because the copier appended
unchanged done state to the pure exit switch; the production fix above resolved
them. The first three-case lit run was **two PASS/one FAIL, 140.53 s**: the custom
fixture expected numeric observation-name order, while the VM prints globals
lexically. The corrected custom run completed 64 native executions, then exposed
an incorrect new refusal expectation in **99.75 s**. That unchanged two-counter
source became the positive described above. The final custom run passes.
The final local capture launcher executed an accidental trailing command after
lit, returning shell status 127; the captured lit output and independent JSON
both record PASS. No test failure is being reclassified.

Logs, JSON results, final source hashes, the exact next-boundary probe, runner,
and explicit/deduced emitted C++ examples are in
`../test-results/2026-09-22-counted-break-exits/` beside the monorepo. The devbox
idle timer was restored and verified active after testing.

Full CTest/compiler lit, broad corpus/native matrices, WPT/test262, Windows,
additional sanitizer runs, local builds and push were skipped. The existing
dataset case's own lifetime sanitizer did run. Earlier conformance measurements
remain historical; this session does not claim full Bootstrap admission.

## Exact next boundary

`two-projected-break-exits` in `native_dom_custom_iteration.py` increments both
`count` and `extra` before its conditional break, then returns their sum. The
importer emits a two-result exit switch, and native lowering still refuses with
`DOM helper completion observes an inactive value`. Both optimization policies
retain that refusal. Extending the projection proof requires distinct proved
output slots for each independently selected value. A second ordinary carried
counter already works and must not be confused with this boundary.

Mutable iterator state, factories, nested custom opens, body return/throw close
behavior, literal range-for printing, unguarded Bootstrap defaults and the
application driver remain unfinished. Higher OR-mask gaps still need a union
of lattices. The earlier conformance crashes and WPT event regressions remain
separate follow-up work.
