# Guarded cleanup sequences and Number bitwise operands, 2026-09-23

Resumed clean **95a6e152** and retained source `006cce9f` from HANDOFF,
Current native work and iteration 125's journal. No dirty draft remained.
`codex-wip-20260907` is already an ancestor; unrelated unmerged branches were
preserved. Source, raw proof/review and escape agents worked in parallel.
Interruptions resumed their saved candidates and completed checks.

## Native change

**f87a99a7** extends the existing guarded cleanup scanner with a pending method
for each write. The first guarded write retains its existing proof; later writes
enter the existing complete-use census and suppression set. Ordered cloning
preserves their guard, method/read/write order, saved Boolean identity and the
original body exception. Dangling methods, observers, invalid selectors and
unproved typed receivers still refuse. Every moved read and split suppression
requires complete DOM/Style reproof on the private candidate before publication.
The original budget bounds the sequence. No runtime or ownership API changed.

Original source SHA-256:
`006cce9f9daaa45079df6893a856ddbe8e49ae63f194b9ac55048900698b6df6`.
Getter `55f9d218`, order `d82765f3` and selector `c8f9f7e5` variants execute.
The raw gate covers third/fourth writes, reused snapshots, direct helpers,
typed failures, 15 new structural refusals and budget controls. It preserves
28 raw MLIR literals, 110 original constructions and all 251 prior invalid cases.
Source preservation retains 233 general sources, 86 metadata rows, 130 saved
cases, 302 other refusal bodies and 130 oracle constructions.

The focused source gate passes **64 native executions, 92 refusals and 32
Node/VM observations**. Its complete stdout is retained locally and on the
devbox; no execution recovery or replay was needed. All 32 generated C++ files
contain no Script namespace. Standalone and linked checks passed. Independent
native review is clean; the later raw-test const-handle correction is mechanical.

## Escape change

**51cde33e** extends shared `boundedConvertedBits` to finite original Number
literals with magnitude at most `2^32-1`, including one frontend `Neg` of a
literal. Public Core `number_to_uint32` supplies the conversion. Scalar
complement/binary operations, invariant table inputs, masks and shift counts
reuse it. Original values, arithmetic/property-key authority, mutation checks
and receiver reload gaps remain separate. Computed fractions stay unproved.

Baseline program `ffd0eb15fee793d8` classified all six child sites as stored.
The same bodies in final program `343763b6045627bc`, functions 73–78, measure
**three confined and three stored**. Each is observed once, with zero unresolved
or unchecked instances. Saved-child, mutated-table and magnitude controls remain
stored. Six Node witnesses check original Number values, reads, writes, own keys,
child identity and returned array identity. All 72 historical source bodies,
72 historical claim lines, six baseline bodies and 53 raw named constructions
are preserved. Parent production review is complete; the independent escape
reviewer hit a rate limit before completing, so no such review is claimed.

The first arrays run failed 144 assertions. Besides the initial raw promotions,
23 historical fractional Number cases needed expectation updates across length,
mask and stride tests. Their IR, literal lists and mutation sequences remain.
The last failure was an exact budget pin: 32 extra BitNot results now cost 96
steps rather than 64 because each newly proved Number fact costs one extra step.
The check still asserts that exact cost. Nineteen focused Node length witnesses
and preservation checks support the historical length updates. Production was
unchanged after its successful build; only the affected arrays gate repeated.

## Focused validation

- Source preflight: 30 policy checks; 15 syntax checks and 16 local Node observations.
- Focused native execution: 64 executions, 92 refusals, 32 Node/VM observations.
- `ctcompile_host_contract`: 1/1 PASS, 2.58 s / 2.60 s total.
- `ctcompile_escape_analysis_arrays`: final 1/1 PASS, 3.00 s / 3.01 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: 1/1 PASS,
  0.13 s, 356 excluded. Not replayed after unrelated raw expectation updates.
- Six escape and 19 historical-length Node witnesses PASS.
- Final `tools/format.sh --check`: 1126 C++, 157 Python, 114 web files PASS.
- `git diff --check` and all 13 final code/test hashes against the devbox PASS.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. The initial host build failed on a const CallOp in
the new raw test; its next attempt found const UnaryOp/ConstantOp handles in the
new escape code. Removing those qualifiers fixed compilation. The successful
host CTest and native/source checks were not repeated. Formatting ran again
because the historical raw expectations changed. All remote operations held the
shared devbox lock; Git writes used the repository lock and explicit paths.

Initial identity checks inspected 14 readable Linux executables and 343 Windows
processes; final checks inspected 17 and 346. Neither found actual Claude
executables, CLI entry points or loops. Both had 60 Linux executable permission
errors, so status remained uncertain and concurrent-area rules applied.
No browser/shared implementation edits, local C++ builds, pushes or history
rewrites occurred.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. These are focused results, not full-Bootstrap coverage measurements.
Evidence: `../test-results/2026-09-23-guarded-multiwrite-number-bitwise/`, relative
to the repo, with `SHA256SUMS`. Working checkpoints are `/tmp/ctcompile-native126`,
`/tmp/ctcompile-tests126`, `/tmp/ctcompile-raw126` and `/tmp/ctcompile-escape126`.

## Next boundary

`unsupported-selector-guards-nested-third-write`, SHA-256
`9d3a36399990ec7468ac1375e93c62efa288441e9ffd980783540257d30a20db`, refuses
**DOM protected helper needs an independent inert-body proof** under both policies.
It nests the third cleanup write under a second DOM-read guard. Preserve both
guards, source order, read identity and the original body exception.
Broader cleanup/control flow, nested custom iterators, unguarded Bootstrap
defaults, the application driver, full native Bootstrap, general powers and
legacy SCF retention remain. Computed fractional arithmetic, wider Number
magnitudes, unsafe String exponents and Strings over 32 bytes remain outside
the conversion proof. The previously recorded VM fractional-index truncation
is separate runtime work; no runtime behavior was changed here.
