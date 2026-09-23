# Branch-local cleanup reads and radix conversions, 2026-09-23

Resumed clean **ebf753aa** and retained source `164962d0` from HANDOFF,
Current native work and iteration 122's final journal. No dirty drafts or
unmerged `codex-wip-20260907` remained; that old branch exists but is already
merged. Unrelated branches were preserved. Source, raw and escape agents worked
in parallel. Repeated interruptions were resumed from saved candidates, hashes,
witnesses and logs without restarting completed checks.

## Native change

**a7b6a1de** extends the existing guarded second-write scanner with one branch-local
`hasAttribute` or `matches` lookup/call. The read may precede the write's method
lookup or occur inside its arguments. It stays inside the original branch.
Existing feeding-read selection retains ignored reads and saved values in the
complete use census. Every branch-local selector, including an ignored selector,
requires a valid literal before leaving suppression. Private complete DOM/Style
reproof still precedes publication. The original body exception and both write
suppressions remain; cloning, runtime and ownership interfaces are unchanged.

Original source SHA-256:
`164962d0a51209f754ca933fed97a66cc4eaa12a74bfe831e4e16824314a0af3`.
Getter `4522c9b3`, order `546f31f5` and selector `dd1cf25d` variants also execute.
The focused runner accounts for **64 native executions, 88 refusals and
32 Node/VM observations**. Its final stdout was lost on interruption. Recovery
found the final refusal manifest after all prior checks, verified 32 generated
C++ files and 64 binaries, and reran only that last refusal. No native execution
was replayed. All generated C++ files contain no Script namespace; the fixture
checks standalone and linked output.

Preflight passed 28 policy checks; local checks passed 14 source syntax checks
and 16 Node observations. The source fixture preserves 233 general historical
bodies, 86 metadata rows, 118 saved cases, 275 retained refusal bodies and
118 oracle constructions. Raw coverage preserves 28 MLIR literals and 91 original
constant constructions; one exact historical refusal is promoted. Fourteen new
negative controls retain invalid-selector, use, incomplete-method, ordering and
exception boundaries. An independent native review found no actionable issues.

## Escape change

**bd312a19** extends shared `boundedConvertedNumber` with unsigned `0x`, `0o`
and `0b` grammars, case insensitive. Recognition precedes sign removal; whole
original input remains limited to 32 bytes. Public Core `hex_value` validates
digits, then `string_to_number` supplies conversion. Existing finite/integral
uint32 magnitude limits remain. Signed radix spellings, incomplete prefixes,
invalid digits and trailing junk stay unproved. Original String values/property
keys, String addition, complete table-mutation checks and receiver gaps remain
separate from numeric conversion facts. No browser/runtime implementation changed.

Baseline `ecb18efe66165a7d` classified all six child sites as stored. The same six
bodies, functions 55–60 in final program `116641a2564efc63`, measure **three
confined and three stored**. Permanent recording checks require all six children
to be observed once, with zero unresolved or unchecked instances. New calls
precede the existing intentional BigInt throw. Six saved Node witnesses verify
array/child identity, original Strings, own keys and read/write sequences.
All 54 historical source bodies and six baseline bodies remain byte-identical.
An independent review found no actionable correctness issue.

The first exact arrays gate failed 39 assertions under five old refusal labels.
Seven original radix cases were promoted across three test files by changing
only five success conditions and labels; the original IR, construction lists and
exact successful read/retention assertions remain. One earlier `0x0` table case
was also promoted unchanged. Production stayed unchanged after its first build.
Final arrays result: 1/1 PASS, 3.30 s / 3.31 s total.

## Focused validation

- `ctcompile_host_contract`: 1/1 PASS, 2.57 s / 2.58 s total.
- `ctcompile_escape_analysis_arrays`: 1/1 PASS, 3.30 s / 3.31 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: 1/1 PASS,
  0.13 s, 356 excluded. Not repeated after unrelated raw expectation edits.
- `tools/format.sh --check`: final 1126 C++, 157 Python, 114 web files PASS.
  Earlier formatter attempts were interrupted before completion.
- `git diff --check` passes. All nine final code/test hashes match the devbox.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All devbox operations held the shared build lock.
A build wrapper consumed following heredoc commands; exact CTests were then run
separately without repeating that build. The final build redirects stdin from
`/dev/null` before its following CTest.

Initial process checks inspected 74 Linux command lines and 352 Windows process
records. No actual Claude executable, CLI or loop matched; 60 Linux executable-link
permission errors kept status uncertain. Concurrent-area rules applied.
No browser/shared implementation edits, push or history rewrite occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows, sanitizers
and local C++ builds were skipped. These are focused results, not full-Bootstrap
coverage measurements.

Evidence: `../test-results/2026-09-23-branch-local-cleanup-radix-conversions/`,
relative to the repo, with `SHA256SUMS`. Working checkpoints are
`/tmp/ctcompile-native123`, `/tmp/ctcompile-tests123`, `/tmp/ctcompile-raw123` and
`/tmp/ctcompile-escape123`.

## Next boundary

`unsupported-selector-guards-second-write-two-reads`, SHA-256
`04f5317cc53d459eac286e6b1d21336ce25187865623072739a9505ba8c68f1b`, refuses
**DOM protected helper needs an independent inert-body proof** under both policies.
Its guarded second `setAttribute` evaluates two `hasAttribute` calls in a comma
expression. Preserve both reads, their original guard/order, the saved selector
and the body exception. Broader cleanup/control flow, nested custom iterators,
unguarded Bootstrap defaults, the application driver, full native Bootstrap,
general powers and legacy SCF retention remain unfinished. Numeric conversion
still excludes fractional/exponent grammars and Strings over 32 bytes.
