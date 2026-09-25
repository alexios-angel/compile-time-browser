# Post-write cleanup reads and fractional bitwise keys, 2026-09-23

Resumed clean **ea386588** and retained source `77866d32` from HANDOFF,
Current native work and iteration 124's final journal. No uncommitted drafts or
unmerged branches remained; `codex-wip-20260907` was already merged. Three agents
handled source regressions, raw proof/review and escape work in parallel.
Interruptions and rate limits were resumed from frozen candidates and logs.

## Native change

**3a7ec75f** permits branch-local `hasAttribute` and literal `matches` reads after
the guarded second cleanup write. The existing complete-use census now records
that write's original feeding operand before a later read replaces the scanner's
pending read. Ordered cloning, the saved guard and body exception, suppression,
selector validation, budgets and private complete DOM/Style reproof remain.
No runtime or ownership interface changed. Independent production review is clean.

Original source SHA-256:
`77866d32652bc2438cc59ec6c912041b95c319be573d0f1c54d002a676558c0f`.
Getter `0355697a`, order `ec6b9bf8` and post-write selector `13ca97c6` variants also
execute. Source checks preserve the original body exception and verify the read
stays after the write inside its guard; the write keeps its earlier Boolean.
The raw test promotes two unchanged historical refusals and covers multiple
post-reads, mixed pre/post-reads, direct helpers, typed refusals and budgets.
All 28 raw MLIR literals and 105 original constant constructions are preserved.
Source preservation checks retain 233 general bodies, 86 metadata rows,
126 saved-throw cases, 294 other refusal bodies and 126 oracle constructions.

Native execution reached the final refusal manifest in a fresh work directory,
after **64 native executions, 32 Node/VM observations and 83 prior refusals**.
An interruption lost final stdout. Recovery verified all 32 generated C++ files
and 64 ELF binaries, then reran only the last refusal: **84 refusals accounted**.
This is recovered execution evidence, not a second complete run. No native binary
or Node/VM observation was replayed. Generated C++ contains no Script types;
the fixture's standalone and linked checks passed before reaching the refusals.

## Escape change

**7056768a** shares the existing bounded String grammar between exact numeric
conversion and bitwise conversion. Public Core `number_to_uint32` truncates
fractional String operands for complement, binary bitwise operations and shifts.
Scalar replay, varying invariant-table keys and invariant masks/counts use that
same proof. Original Strings, property spelling, non-bitwise arithmetic,
mutation checks and receiver reload gaps retain their separate authority.
The 32-byte source bound, complete grammar, finite uint32 magnitude ceiling and
safe exponent bound remain. Independent production review is clean.

Baseline program `3759ae6cdfb3c4f2` classified all six child sites as stored.
The same six source bodies in final program `a02f9eed4dcac203`, functions 67–72,
measure **three confined and three stored**. All six are observed once, with
zero unresolved or unchecked instances. Six Node witnesses preserve table
Strings, own keys, child/array identity and read/write sequences. All 66 historical
source bodies, six baseline bodies and 51 original raw constructions remain.

The first selected lit run passed claim checks and reported zero soundness
violations, but its new recording assertion for `arithmeticFractionalTableKey`
failed. The VM reports that child confined; Node retains it. Inspection found
`ctbrowser/lib/Script/vm/objects/store.cpp::store_index` casts numeric keys to
`ptrdiff_t`, truncating fractional keys. The compiler still classifies this
unchanged arithmetic control as **stored**. Only the recording expectation and
its explanatory comment changed. The second selected lit run passes; no browser
or runtime code was changed to match the compiler.

## Focused validation

- Source preflight: 26 policy checks; 13 syntax checks and 16 Node observations.
- `ctcompile_host_contract`: 1/1 PASS, 2.61 s / 2.62 s total.
- `ctcompile_escape_analysis_arrays`: 1/1 PASS, 3.02 s / 3.03 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: final 1/1 PASS,
  0.14 s, 356 excluded; only this case repeated after the recording correction.
- `tools/format.sh --check`: 1126 C++, 157 Python and 114 web files PASS.
- `git diff --check` and all eight final code/test hashes against the devbox PASS.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All remote operations held the shared build lock.
The interrupted initial host command never started its build; the resumed build
passed. Its shell input was consumed before the CTest line, so the first host
CTest was run separately. The interrupted formatter was restarted before its
first completed pass. Completed compiler/native/arrays gates were not replayed.

Initial process checks found no actual Claude executable/CLI/loop matches among
81 Linux records and 339 Windows records, with 64 Linux executable permission
errors. Final checks found no matches among 15 readable Linux executables and
350 Windows records, with 60 Linux permission errors. Status remained uncertain;
concurrent-area rules applied. No browser/shared implementation edits, local
C++ builds, pushes or history rewrites occurred.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and
sanitizers. These are focused results, not full-Bootstrap coverage measurements.
Evidence: `../test-results/2026-09-23-postwrite-reads-fractional-bitwise/`, relative
to the repo, with `SHA256SUMS`. Working checkpoints use `/tmp/ctcompile-native125`,
`/tmp/ctcompile-tests125`, `/tmp/ctcompile-raw125` and `/tmp/ctcompile-escape125`.

## Next boundary

`unsupported-selector-guards-third-write-postread`, SHA-256
`006cce9f9daaa45079df6893a856ddbe8e49ae63f194b9ac55048900698b6df6`, refuses
**DOM protected helper needs an independent inert-body proof** under both policies.
It adds a third write after the guarded second write, fed by a new branch-local
read. Preserve the read/write order, saved guard and original body exception.
Broader cleanup/control flow, nested custom iterators, unguarded Bootstrap
defaults, the application driver, full native Bootstrap, general powers and
legacy SCF retention remain. Fractional Number literals and arithmetic,
unsafe exponents and Strings over 32 bytes remain outside this conversion proof.
The VM fractional-index discrepancy is recorded above as separate runtime work.
