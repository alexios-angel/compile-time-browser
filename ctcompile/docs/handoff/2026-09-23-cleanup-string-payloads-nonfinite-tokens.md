# String cleanup payloads and nonfinite String tokens, 2026-09-23

Resumed clean **e4396a38** and source `39355ceb` from HANDOFF, Current native
work and the previous detail/journal. The old `codex-wip-20260907` is already
an ancestor; unrelated branches remain untouched. Source regressions, raw
proofs/review and escape work ran in parallel. Interruptions and rate limits
resumed saved checkpoints rather than replaying completed checks.

## Landed

**0ada61d2** adds `getAttribute` to both existing cleanup read scans. The first
probe passed the structural scan but exposed an ignored optional-String/Boolean
completion join. Protected cloning now drops that unused result/yield transport
after the complete observer census. Every read producer, guard, branch arm and
write remains for private typed DOM reproof. Unprotected cloning keeps its
results. No typed join was broadened and no runtime fallback was introduced.

The original `39355ceb` source is byte-identical. Three additional cases actually
execute the read in the else/getter/then paths, checking read order, suffix
suppression and precedence of the original body exception. Existing source and
oracle constructions remain. Raw checks cover mixed payloads in both arms,
standalone suffix reads, invalid receivers, observers and incomplete budgets.
Independent native production review is complete and clean.

**b678ce72** admits only exact trimmed `Infinity`, `+Infinity`, `-Infinity` and
`NaN` String tokens through public Core conversion. Arithmetic rejects nonfinite
values before integer conversion. Other grammar, radix-overflow, magnitude,
source-size, provenance, property-key, mutation and receiver proofs remain.
The prior **274c03fd** review was completed independently; parent review covers
the current production change. No completed independent review of the current
escape change is claimed.

Baseline `5f2cf36dcf06a4e4` classified all six child sites as stored. The identical
function bodies in final `62d9c3a8aef26a62`, functions 97–102, measure **three
confined and three stored**. Each child is observed once with zero unresolved
or unchecked instances. All 96 historical functions and their CHECK/RECORD
lines remain. Six Node source witnesses and eight exact-token witnesses pass.
The six new invocations precede the existing intentional mixed-BigInt throw.

## Focused validation

- Native source execution: **64 executions, 80 refusals, 32 Node/VM observations**.
- Source policy preflight: **24 checks**; **12 syntax checks**, **16 local Node
  observations**. The retained source also passed the initial two-policy probe.
- `ctcompile_host_contract`: **1/1 PASS**, 2.57 s / 2.58 s total.
- `ctcompile_escape_analysis_arrays`: **PASS**, 3.14 s in the selected two-test
  run; the host test in that run failed and was corrected separately.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.14 s, **356 excluded**.
- `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  The subsequent host expectation-only correction passed scoped clang-format.
- `git diff --check`; all **six final code/test hashes** match the devbox.
- All **32 generated C++ files** contain no Script namespace. The source
  fixture's standalone and linked checks pass.

The first arrays run failed 28 assertions, all from four historical Infinity/NaN
expectations. Only their expectations changed; original constructions remain.
The first host run found one historical `getAttribute` structural refusal now
admitted. Moving that input to the typed proof loop initially expected too much:
the second run confirmed protected optional-String writes still refuse typed
suppression. Its expectation now records structural admission and typed refusal.
The final host-only run passed. Completed arrays/lit/native executions were not
repeated after this raw-test correction.

Explicit devbox targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Build and test commands held the shared build lock;
Git writes held the Git lock and staged explicit paths. An interrupted formatter
had only its startup line and was completed later. A shell heredoc lost trailing
commands to SSH stdin; the missing probe/fetch ran separately, without replaying
the completed build or baseline.

Initial process checks inspected 14 readable Linux executables and 342 Windows
records; final checks inspected 14 and 341. Linux permission errors numbered 61
and 60, respectively. No actual Claude identity matched, but availability stayed
uncertain. Concurrent-area rules applied; there were no browser/shared
implementation edits, local C++ builds, pushes or history rewrites.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. These are focused results, not a full-suite or full-Bootstrap pass.
Evidence and checksums are in
`../test-results/2026-09-23-cleanup-string-payloads-nonfinite-tokens/` relative to
the repo. Working checkpoints are `/tmp/ctcompile-native130`,
`/tmp/ctcompile-tests130`, `/tmp/ctcompile-raw130` and `/tmp/ctcompile-escape130`.

## Next boundary

`normal-selector-branch-throw-read-close`, SHA-256
`b216ee2066b536569e12637b5c9697c354307af5cb963f1908ec7a73ba70ccd5`, is the
retained cleanup body with a normal `break`. Both policies refuse **DOM iterator
primitive close requires saved-throw suppression**. Ordinary loop exit must
preserve the cleanup exception as an observable result; it cannot borrow the
saved-body-exception suppression proof. Keep branch/read/write and suffix order,
including the original `getAttribute` payload. The body-return variant is also
retained as a refusal in the source fixture.

Broader cleanup/control flow, nested custom iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain. Wider finite
Strings, unsafe exponents, Strings over 32 bytes, computed fractional/nonfinite
origins, general powers and legacy SCF retention remain outside the escape proof.
The known VM fractional-index discrepancy is separate runtime work.
