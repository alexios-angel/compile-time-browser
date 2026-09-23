# Nested cleanup guards and finite Number bitwise operands, 2026-09-23

Resumed clean **c46c8be0** and retained `9d3a3639` from HANDOFF, Current native
work and iteration 126's journal. No dirty draft remained.
`codex-wip-20260907` is already an ancestor; unrelated branches were preserved.
Source, raw proof/review and escape agents worked in parallel. User interruptions
and rate limits resumed their checkpoints without repeating completed gates.

## Native change

**76bdc8d1** extends the existing guarded cleanup scanner recursively. Each
nested condition must be a same-block, single-use truth conversion of a proved
DOM read. Completed read snapshots enter the complete-use census before region
boundaries; unfinished methods refuse. The scan accepts empty else arms and stops
at 64 levels, with every scanned operation charged to the existing budget.
The existing ordered clone preserves regions, method/read/write order, saved
Boolean identity, exact per-write suppression and the original body exception.
Selector validation and complete private DOM/Style reproof still gate publication.

Original SHA-256:
`9d3a36399990ec7468ac1375e93c62efa288441e9ffd980783540257d30a20db`.
Getter `2e395d42`, order `74091197` and false-inner-guard `584e19b0` execute too.
The focused source run passes **64 native executions, 92 refusals and 32 Node/VM
observations**, with complete stdout retained locally and on the devbox.
All 32 generated C++ files contain no Script namespace; their fixture's standalone
and linked checks passed. Independent review checked all three `inlineCall`
callers, region boundaries, suppression and private publication and found no issue.

Raw coverage adds seven admissions, two typed refusals, twelve structural
refusals, four malformed-region mutations and three budget inputs. It preserves
all 28 original MLIR literals, 114 named constructions and 266 historical invalid
constructions. Source preservation retains 233 general sources, 86 metadata rows,
134 saved cases, 312 other refusal bodies and 134 oracle constructions. Parent
removed unrelated candidate formatter changes with AST-equivalence checks;
source behavior and completed preflight/Node results were unchanged.

## Escape change

**bbc73532** removes only the magnitude ceiling in shared `boundedConvertedBits`
for finite original Number literals and one source `Neg`. Public Core truncates
and reduces modulo 2^32 before its integer cast, including huge finite doubles.
Scalar, table, mask and shift-count consumers reuse the same proof. Arithmetic,
property keys, original primitive values, computed provenance, mutations and
receiver reload gaps retain their separate requirements. No runtime code changed.

Baseline program `64322f7c8c302542` classified all six new child sites as stored.
The unchanged bodies in final program `803ac2ace90ef48b`, functions 79–84, measure
**three confined and three stored**. Every site is observed once, with zero
unresolved or unchecked instances. Saved-child, table-mutation and nonfinite
controls remain stored. All 78 historical source bodies, 78 claim lines, six
baseline bodies and 57 original raw constructions are preserved.

The first arrays gate failed 176 assertions on 30 historical cases newly covered
by the conversion: 24 length-index cases, one empty-length case, one invariant
shift, two table cases and two BitNot strides. Only expectations changed; original
IR, literals and mutation sequences remain. Final arrays pass. Six source identity
witnesses, eleven represented-number boundary witnesses and thirty historical
Node witnesses pass. Raw controls include signed zero, ±1e300, the largest finite
double, 2^53 and adjacent representable doubles. The prior `51cde33e` independent
review, interrupted last session, is now complete and clean. Independent review
of the final escape change also found no issue.

## Focused validation

- Source preflight: 30 policy checks; 15 syntax checks and 16 local Node observations.
- Native execution: 64 executions, 92 refusals, 32 Node/VM observations.
- `ctcompile_host_contract`: 1/1 PASS, 2.77 s / 2.78 s total.
- `ctcompile_escape_analysis_arrays`: final 1/1 PASS, 3.06 s / 3.07 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: 1/1 PASS,
  0.13 s, 356 excluded. Not repeated after raw-only expectation updates.
- Six escape source, eleven numeric-boundary and thirty historical Node witnesses PASS.
- Final `tools/format.sh --check`: 1126 C++, 157 Python, 114 web files PASS.
- `git diff --check` and all ten final code/test hashes against the devbox PASS.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Builds passed without compile fixes. Only the failed
arrays gate and formatting repeated after the historical expectation edits.
Native, host and lit checks were not replayed after independent escape updates.
Remote work used the shared build lock; Git writes used the repository lock and
explicit paths. No local C++ builds occurred.

Initial process checks inspected 18 readable Linux executables and 345 Windows
records; final checks inspected 21 and 344. No actual Claude matches appeared,
but 61 initial and 60 final Linux executable permission errors left availability
uncertain. Concurrent-area rules applied throughout. No browser/shared
implementation writes, pushes or history rewrites occurred.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. These focused results are not full-Bootstrap coverage measurements.
Evidence: `../test-results/2026-09-23-nested-cleanup-wide-number-bitwise/`, relative
to the repo, with `SHA256SUMS`. Working checkpoints are `/tmp/ctcompile-native127`,
`/tmp/ctcompile-tests127`, `/tmp/ctcompile-raw127` and `/tmp/ctcompile-escape127`.

## Next boundary

`unsupported-selector-nested-third-write-else`, SHA-256
`fe671bbb792d663f38bc1d7df1421991b8d9555858c6b4a3c72ce3594f53bedb`, refuses
**DOM protected helper needs an independent inert-body proof** under both policies.
It adds a nonempty else arm to the nested third-write guard. Preserve both arms,
their original guards, read/write order, saved values and the body exception.
Broader cleanup/control flow, nested custom iterators, unguarded Bootstrap
defaults, the application driver, full native Bootstrap, general powers and
legacy SCF retention remain. Computed fractions, nonfinite Number bitwise origins,
unsafe String exponents and Strings over 32 bytes remain unproved. The known VM
fractional-index discrepancy remains separate runtime work.
