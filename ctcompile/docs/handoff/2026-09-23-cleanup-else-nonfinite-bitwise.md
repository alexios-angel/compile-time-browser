# Cleanup else arms and nonfinite Number bitwise operands, 2026-09-23

Resumed clean **925ae490** and retained `fe671bbb` from HANDOFF, Current native
work, the preceding detail and journal. No dirty draft remained. The old
`codex-wip-20260907` branch is already an ancestor; unrelated branches were left
alone. Source, raw proof and escape work ran in parallel. Repeated agent rate
limits and a user interruption resumed checkpoints without repeating passed gates.

## Native change

**c1dfd76d** extends the existing cleanup scanner to both arms of each bounded
nested guard. Each arm has one block and no block arguments; conditions keep
their same-block, single-use DOM Boolean proof. Completed reads stay in the use
census, while unfinished method/read pairs cannot cross into a sibling arm.
Later guarded writes may use primitive values without a feeding read. Every
scanned operation remains charged, and the existing 64-level bound remains.

The ordered clone retains both arms, method/read/write order, saved Boolean
identity and each write's exception suppression. Literal selector validation and
complete private DOM/Style reproof still precede publication. The original body
exception wins over cleanup exceptions. There is no browser or runtime change.

Original source SHA-256:
`fe671bbb792d663f38bc1d7df1421991b8d9555858c6b4a3c72ce3594f53bedb`.
Getter `3ad50012`, order `9ac47bd2`, false-inner-guard `1b6b4a6d` and
cleanup-throw `7fec52a1` execute too. The false guard exercises the else write;
the cleanup-throw variant changes its terminal thrown value while preserving
the original loop-body exception.

The raw tests add six admissions, two typed refusals, eight structural refusals
and three budget inputs. Their trace now compares source arms and literal values,
as well as producer identity and call order. All 28 existing MLIR literals,
120 original named constructions and 278 historical invalid constructions remain;
the existing trace assertion was intentionally strengthened. Source preservation
retains 233 general sources, 86 metadata rows, 138 saved cases and their oracle
constructions, and 322 remaining refusal bodies. Only the exact retained refusal
is promoted. The parent finished the raw draft after its agent hit rate limits.

## Escape change

**df80fd69** removes the finite-only condition in shared `boundedConvertedBits`
for an original Number literal or one original source negation. Public Core maps
nonfinite Numbers to zero before integer conversion; finite Numbers retain their
existing truncation and modulo behavior. Scalar, table, mask and count consumers
share that proof. Arithmetic, property keys, original table values, computed
provenance, mutations and receiver reload gaps retain separate requirements.

Baseline program `b79bb4c948526e70` classified all six child sites as stored.
The unchanged bodies in final program `a9b170d6a91826b8`, functions 85–90, measure
**three confined and three stored**. Each site is made once, with zero unresolved
or unchecked instances. Recording agrees with the claims. Saved-child, mutated
table and unconverted nonfinite-property controls remain stored. All 84 prior
source bodies and claim lines, six baseline bodies and 61 raw constructions remain.

Six existing raw table cases were promoted after Node checks. The first array
CTest then failed 208 assertions on 34 more historical cases: 24 bitwise indices,
two computed shrink targets, six live NaN mutations and two BitNot strides.
Only expectations changed; the original literals, IR and mutation sequences
remain. Final arrays pass. Six actual-source identity/read/write/Number witnesses,
five nonfinite bit-pattern witnesses and forty historical Node witnesses pass.

The parent reviewed both production changes and all shared conversion callers.
The escape author's review is saved separately and is not an independent review.
Independent native review recovered its interrupted checkpoint and is complete
and clean, covering the scanner, clone, all three callers, dominance, complete-use
census, typed suppression and private DOM reproof before publication.

## Focused validation

- Source preflight: 30 policy checks; 15 syntax checks and 20 local Node observations.
- Native execution: 80 executions, 100 refusals, 40 Node/VM observations.
- `ctcompile_host_contract`: 1/1 PASS, 2.96 s / 2.97 s total.
- `ctcompile_escape_analysis_arrays`: final 1/1 PASS, 3.06 s / 3.07 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: 1/1 PASS,
  0.14 s, 356 excluded; unchanged and not repeated after expectation-only edits.
- Final `tools/format.sh --check`: 1126 C++, 157 Python, 114 web files PASS.
- `git diff --check`; ten final code/test hashes match the devbox.
- All 40 generated C++ files contain no Script namespace; their fixture's
  standalone and linked gates pass.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. The final rebuild targeted only the arrays executable.
Builds required no compile fixes. An initial baseline invocation omitted required
`--out` and failed before classification; the corrected command measured the
baseline once. Only the failed arrays gate and formatting repeated after edits.
No completed native, host or lit check was replayed after the independent escape
changes. Remote commands used the shared build lock; Git writes used the Git lock
and explicit paths. No local C++ builds occurred.

Initial process checks inspected 15 readable Linux executables and 346 Windows
records; final checks inspected 17 and 346. Zero actual Claude matches appeared,
but 61 initial and 60 final Linux permission errors left availability uncertain.
Concurrent-area rules applied throughout; there were no browser/shared
implementation writes, pushes or history rewrites.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. These focused results are not full-Bootstrap coverage measurements.
Evidence: `../test-results/2026-09-23-cleanup-else-nonfinite-bitwise/`, relative
to the repo, with `SHA256SUMS`. Working checkpoints are `/tmp/ctcompile-native128`,
`/tmp/ctcompile-tests128`, `/tmp/ctcompile-raw128` and `/tmp/ctcompile-escape128`.

## Next boundary

`unsupported-selector-nested-third-else-throw`, SHA-256
`65c65b0b7f7f885591a9ba36795e2dfd0e4f7417af835ba3f4b8a041244b228d`, refuses
**DOM helper completion has no return or yield** under both policies. It adds a
branch-local cleanup throw after the else write. Preserve both arms, their
read/write order, saved values and the original body exception.

Broader cleanup/control flow, nested custom iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain. Escape proofs
still exclude computed fractional/nonfinite origins, nonfinite String conversions,
unsafe String exponents and Strings over 32 bytes. General powers and legacy SCF
retention remain. The known VM fractional-index discrepancy is separate runtime
work; the new nonfinite-property control is observed as retained by both oracles.
