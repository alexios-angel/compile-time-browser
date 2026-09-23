# Terminal iterator-close reads and singleton shift counts, 2026-09-23

Resumed clean **3df6cedd** and retained terminal-read source `51fc580e` from
HANDOFF, current00, the ordered-suffix detail and iteration 101's final journal.
The previous iteration had completed; no dirty drafts or unmerged
`codex-wip-20260907` remained. Unrelated branches were preserved. Parallel agents
prepared source regressions and escape analysis; another agent independently
reviewed both production changes. The parent reconciled, gated and committed them.

## Landed

**0714925c** permits a complete final `hasAttribute` lookup/call after the
post-selector write sequence. Its method and result join the existing complete
use census: only the method's callee use and validated frame roots survive.
The result cannot escape or feed an additional observer. Sequential cloning keeps
the read under its original guard, after every write; complete typed DOM reproof
still excludes receiver failure, coercion and reentry. Each write retains exact
suppression and valid-name checks. Selector validation, private callable/frame
checks, original saved Boolean ownership and proof/expansion budgets remain.

The original source is unchanged, SHA-256
`51fc580e0b33837d7c76afdc693e8a9bd56c3ca619d553528c7b9c07925b2bc7`.
Getter `b838b3d7`, missing terminal read `57efd396` and order-sensitive
`7d24c782` also execute. The latter reads a distinct `data-after-selector`
attribute created by the suffix. Tests retain saved false/true exceptions,
normal completion, exhaustion, repeated calls, two documents and owned sessions.
Generated code discards the terminal read with an ordinary `(void)` call.
No browser/runtime implementation changed.

**b57811e7** adds shift counts to the existing singleton RHS range proof used
for division/remainder. A syntactically varying count must have equal proved
numeric bounds before the existing shift transfer applies. Signed/unsigned
conversion, count masking, left-shift overflow checks, complete producer/receiver
reload and later-store census, independent reload-gap reproof and actual-write
replay remain. Fourteen raw CFG/SCF controls and eleven source cases cover
signed/unsigned/left and negative/masked counts, retained/saved children, safe
reload gaps, later/overlapping stores, varying counts and non-own writes.

## Focused validation

- Exact `ctcompile_host_contract`: initial **1/1 PASS, 2.46 s test / 2.47 s total**;
  final formatted fixture rebuild **1/1 PASS, 2.49 s test / 2.51 s total**.
- Four selected saved-throw sources: **64 native executions, 96 refusals and
  32 Node/VM observations PASS**, both providers and optimization policies,
  explicit/deduced C++, GCC and Clang. The selector runs the committed fixture.
- Admission preflight: four positives and twelve refusals under both policies.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.46 s test / 2.47 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.15 s**, with 355 other lit cases excluded.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` passes. Scratch gate scripts pass `bash -n`.
- All **seven final code/test hashes match the devbox**. All **32 generated C++
  files** retain one selector, ordered suffix writes consuming actual read
  Booleans, and the final unused attribute read after every write. No Script/VM
  or nullable-scalar fallback appears; linked-symbol checks pass in the fixture.
- Source preservation retains **233 historical iterator bodies, 86 metadata
  rows, all 49 previous saved cases and 80 retained saved refusal bodies**.
  Only the original terminal-read refusal is promoted. Local checks record
  sixteen positive and 24 refusal Node observations. Escape checks preserve all
  188 historical source bodies/expectations and raw cases; **199 Node outputs
  and own-key sets pass**.

Initial scoped formatting found wrapping in new raw test rows; formatting was
corrected, then the final raw fixture rebuilt and passed. No production or source
test correction was needed after the first gates. A separate artifact audit
initially counted only assigned read results; correcting it to count the intended
`(void)` terminal call made the audit pass without changing generated code.
Independent native and escape reviews found no blockers.

Initial WSL-root `/proc` and Windows CIM identity checks inspected **81/347**
processes; a prelanding check inspected **87/354**. Both found no actual Claude
executable, CLI or loop matches and no errors: Claude was confirmed stopped.
No browser/shared implementation edits, local C++ builds, pushes or history
rewrites were made.

Commands, selectors, source witnesses, generated artifacts, logs and checksums
are preserved in `../test-results/2026-09-23-terminal-read-close/` relative to the
monorepo. Full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and
sanitizer runs were skipped under the focused policy.

## Next boundary

Retained `unsupported-terminal-postselector-match`, SHA-256
`5f783ab2c7efefc210bbd1f8f4c3b21385e07cc1b73b01df6ea48f53d9f7b437`,
still refuses **DOM protected helper needs an independent inert-body proof**
under both policies. Its second `matches('[data-closed=false]')` follows all
writes and supplies the ignored close throw. Preserve this evaluation, its
ordering and the original saved body exception; the single-selector proof
does not yet admit it.

Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain unfinished.
No full-Bootstrap admission or coverage gain is claimed.
