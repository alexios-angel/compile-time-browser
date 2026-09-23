# Terminal iterator-close read sequences and varying shift counts, 2026-09-23

Resumed clean **71e24614** and retained source `5cd57a59` from HANDOFF, current00,
the terminal-selector-read detail and iteration 104's final journal. No dirty
work or unmerged `codex-wip-20260907` remained; unrelated branches were preserved.
Source, raw-host and escape workflows ran in parallel. Raw/escape agents hit
model rate limits; the parent preserved and completed their changes. A separate
agent completed independent native and escape reviews with no blockers.

## Landed

**294f2e21** records every unused read following a terminal selector in the
existing complete use census. Reads retain source order and the original guard;
only selectors enter exact suppression. The terminal marker continues to reject
following writes. Complete typed DOM/Style reproof, private callable/capture/frame
checks, original saved Boolean ownership and proof/expansion budgets remain.
No platform behavior was copied and no runtime fallback was added.

The original source is unchanged, SHA-256
`5cd57a592ed7f10369ebcdd72c3d597b9f88a0f990e204e50d97566e4b194e39`.
Getter `982dc220`, missing first read `1aa7ee90`, reordered reads `8b458311`,
four reads `15500eba` and alternating read/selector/read `2227023e` execute too.
Tests preserve false/true saved exceptions, normal completion, exhaustion,
repeated calls, separate documents and owned sessions. Raw cases include a
direct-call path, exact operation counts/order, bad inputs, observed results,
reused method lookups, subsequent writes and exact/one-less expansion budgets.

**7e081e79** permits existing whole-key subdivision when a shift's two operand
ranges are bounded but the count varies. Every accepted leaf still uses the
singleton count transfer, with original operand order, ToInt32/ToUint32 conversion
and modulo-32 masking. Complete producer/receiver reload and later-store census,
independent reload-gap reproof, restored bounds and actual write replay remain.
The every-prefix contents/retention budget harness covers the new raw cases.

Eleven new source controls cover signed/unsigned/left shifts, saved/retained
children, reload gaps, later stores, overlap, fractional/negative indices and
wrapped counts. All 224 historical source bodies survive; only CHECK 198 changes.
That exact source and two historical raw refusals now admit: Node witnesses show
writes `0,1,2`, reads `0,0,0`, unchanged own keys and no retained child. Parent
review corrected one new raw extending-key witness before gating: `i << (2-i)`
is actually own, whereas `i << (2+i)` extends. Production did not change.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.45 s test / 2.46 s total**.
- Six selected saved-throw sources: **96 native executions, 120 refusals and
  48 Node/VM observations PASS**, both providers and optimization policies,
  explicit/deduced C++, GCC and Clang. The selector executes the committed fixture.
- Admission preflight: six positives and twelve refusals under both policies.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.52 s test / 2.54 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.16 s**, with 355 other lit cases excluded.
- `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  scoped formatting, Python/Node syntax, `git diff --check` and scratch gate
  `bash -n` checks pass. No code changed after the final formatting pass.
- All **seven final code/test hashes match the devbox**. All **48 generated C++
  files** retain the exact terminal read/selector sequence after every write
  and pass the preceding read's Boolean directly to its write. No Script/VM
  or nullable-scalar fallback appears; fixture standalone and linked checks pass.
- Source preservation retains **233 historical iterator bodies, 86 metadata
  rows, 60 saved cases, 111 retained saved refusal bodies and 60 historical
  oracle constructions**. Twenty-four positive and twenty-four refusal local
  Node observations pass; historical oracles were compared without reexecution.
  Eleven new escape source outputs/own-key sets and three historical witnesses
  pass locally. Fractional count source 233 remains a conservative refusal.

The native/escape builds and tests used `tools/remote-build.sh` with explicit
listed targets, behind `/tmp/ctbrowser-devbox-build.lock`. Commands, selectors,
source witnesses, generated artifacts, logs and checksums are preserved in
`../test-results/2026-09-23-terminal-read-sequences/` relative to the monorepo.
The host gate built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`
and `ctcompile-test-native-reference`; the escape gate built `ctjs-opt`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`.

Initial Linux/Windows checks inspected **14/349** accessible processes; the final
check inspected **18/347**. No matches were found, but Linux root-owned executable
links were inaccessible and sudo was unavailable, so Claude status was **uncertain**.
Concurrent-agent area rules remained in force. No browser/shared implementation
edits, local C++ builds, pushes or history rewrites were made.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizer
runs were skipped under the focused policy. No full-Bootstrap gain is claimed.

## Next boundary

Retained `unsupported-terminal-read-write`, SHA-256
`ce6d790dd97fe5468d695afed17594278f2cdcd2cd9ac5f0c0955c57dfdfa1e7`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. After the terminal selector and both `hasAttribute` reads it calls
`setAttribute('data-after-terminal', false)`, then throws false. Preserve that
write in order while the original body exception wins.

Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain unfinished.
Wider varying shift/mask proofs retain the existing work limit.
