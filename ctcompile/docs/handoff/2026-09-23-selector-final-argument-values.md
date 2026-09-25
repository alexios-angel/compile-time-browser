# Saved selector arguments and independent table keys, 2026-09-23

Resumed clean **7754eba6** and retained `c9ccc672` from HANDOFF, Current native
work and iteration 116's final-argument-read-values journal/detail. No dirty
drafts or unmerged `codex-wip-20260907` remained. Unrelated branches were preserved.
Three agents handled source checks, raw checks/review and escape investigation.
After two interruptions, the parent reused the completed compiler build, source
candidate and raw checks; resumed agents completed review and escape work.

**91a8cb96** removes the final write's unnecessary `hasAttribute` restriction
after `selectFeedingRead` has selected a saved read. A consumed suffix selector
leaves suppression and enters the existing literal-selector validator. This
preserves its result through an ignored argument read. Complete use checks,
original guard/order, private typed DOM/Style reproof, original saved body
exception and work budgets remain. All three helper-call paths share the proof.

Original source SHA-256
`c9ccc67215059eb8d0f3800da50d033f3418e4058715ee9eb071c16bf9c349b3`
executes unchanged; getter `1549cef1` and order variant `8d3c1954` also execute.
Three new raw admissions, four typed refusals, ten structural refusals and two
budget rows cover value identity, source order, selector validity, leaks/reuse,
receivers and exact suppression. All 28 historical raw MLIR literals remain.
Source preservation checks retain 233 historical bodies, 86 metadata rows,
99 prior saved cases, 220 retained refusal bodies and 99 oracle constructions.
Only the retained original refusal moves into execution coverage.

**a0e1b45d** extends `LoopProof::indexRange` to read varying numeric keys
from an invariant, exact array allocation distinct from the array being cleared.
It reuses bounded subdivision until the lookup index is a singleton. The complete
loop census requires every write to target the guard array and rejects unknown
effects. Recursive invariant receiver reads still populate `guardReloads`; every
such position needs independent exclusion from all writes. Existing actual-write
replay preserves remaining children and saved identities. No mutation or alias
assumption was relaxed.

The ten baseline source child sites were all `escapes:stored` (program
`6d971daa9f765ab6`). Three now become confined: table keys `[0,2]`, reversed keys
`[2,0]`, and a table receiver reloaded from an untouched guard-array slot.
Seven retained/saved/mutated/aliased/resized/guard-table/overlapping controls stay
stored. Six exact raw admissions and 18 refusals check the proof and actual
contents. Ten Node witnesses compare original/instrumented results, selected
indices, ordered writes, own keys and original child identity. All ten baseline
function bodies and the entire prior raw-test suffix are unchanged.

Focused validation and corrections:

- Source preflight: **28 checks PASS**, three admissions and eleven refusals under
  both optimization policies. The first 24 passed before a new invalid-selector
  test unexpectedly admitted; the final four passed after correction. No completed
  admission was replayed. `consume_block` accepts EOF as a closing delimiter, so
  `[data-closed=false` was a poor invalid fixture. Changed only that new control
  to `[`, already rejected by the raw selector checks. Production was unchanged.
- Selected saved-throw execution: **48 native executions, 80 refusals and
  24 Node/VM observations PASS**, including standalone/linked checks.
- Exact `ctcompile_host_contract`: **1/1 PASS, 2.86 s / 2.87 s total**.
- Final exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.64 s / 2.65 s total**.
  The first run failed only a new opaque-initializer reason assertion: array
  initialization returns `UnknownValue` before reaching loop proof. Preserved the
  exact refused input and corrected its expected reason. Production was unchanged.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`:
  **1/1 PASS, 0.10 s, 356 excluded**. It ran separately after the first arrays
  failure and was not replayed after the raw-only reason correction.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting, changed scratch shell syntax and diff checks pass.
- All six final code/test hashes match the devbox. All **24 generated C++** files
  contain no `ctbrowser::script`; fixture checks verify standalone and linked
  native code. Local native checks cover 14 source syntax cases and 12 recovered
  Node observations; escape checks cover ten source syntax/identity witnesses.
- Independent native review found no actionable issues. The parent reviewed
  escape dependencies, the changed range proof, complete mutation
  census, independent reload gaps, actual replay and final refusal correction.

Builds used `tools/remote-build.sh` under `/tmp/ctbrowser-devbox-build.lock`, with
explicit targets `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
`ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`. CTests used exact
anchored names with `--output-on-failure --no-tests=error`; lit used the generated
build configuration and the exact case filter. Source execution used the saved
`throw_gate_mutable.py`. Commands, logs, sources, generated C++, review and claims
are checksum-preserved in
`../test-results/2026-09-23-selector-final-argument-values/` relative to the repo.

Initial availability inspected 14 accessible Linux and 347 Windows processes;
final checks inspected 22 Linux and 343 Windows processes. No actual Claude
executable/CLI/loop matched, but 60 Linux executable-link permission errors kept
status uncertain. Concurrent-area rules applied. No browser/shared implementation
write, push, history rewrite or local C++ build occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-suite or full-Bootstrap coverage gain is claimed.

**Next native boundary:** retained
`unsupported-selector-inside-final-write-argument-read`, SHA-256
`b3cd28592431e3d5645c8d493a21abe1e1d87e9f91e1de5034eeb60777f9ed13`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. It evaluates an ignored `matches` inside the final write's arguments
while consuming an earlier saved selector Boolean. Preserve source order,
selector behavior and the original body exception. Single-write selector cleanup,
nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver,
full native Bootstrap, general powers and legacy SCF retention remain unfinished.
String-valued table elements such as `['0','2']` are an inferred remaining escape
boundary: the new transfer requires bounded Number elements. This was not measured
on the devbox and is not claimed as a coverage result.
