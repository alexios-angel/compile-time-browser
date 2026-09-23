# Reads after iterator-close selectors and correlated division, 2026-09-23

Resumed clean **44416c8e** and retained `unsupported-third-postselector-read`
source `fcad547a` from HANDOFF, current00, the selector-result detail and iteration
99's final journal. The interrupted prior iteration had already been completed;
no dirty drafts or unmerged `codex-wip-20260907` remained. Unrelated branches were
preserved. Parallel agents prepared source regressions and escape analysis;
another reviewed both changes. The parent reconciled, gated and committed them.

## Landed

**2c05286e** preserves one final `hasAttribute` producer after `matches` and
before the third attribute write. Only that write's value operand may consume
its Boolean. The read is cloned in source order at the original guard; complete
typed DOM/Style reproof must establish exact receivers, primitive arguments and
absence of source exceptions/reentry. Each write retains its exact suppression.
Private callable/frame checks, confined results, original saved Boolean ownership
and charged expansion/proof budgets remain. No browser/runtime implementation
changed.

Original source SHA-256:
`fcad547a0a87a8302831d843ef17a6cf52bcbfa20c0b5e7b3511d4079e09178f`.
Getter `96ab524e` and false-read variant `7a6959ac` also execute. Both variants
keep the selector true; the latter's following `hasAttribute('data-unvisited')`
is false, distinguishing its result from the selector. Native DOM state and
Node/VM observation order retain the saved false/true body exception, exhaustion,
normal completion, repeated calls, two documents and owned sessions.
Generated C++ feeds the actual `js_boolean_t` attribute result directly into
`set_attribute` after the selector.

**325a1b09** extends existing bounded whole-key subdivision to exact divisions
whose correlated range enclosure contains fractional quotients absent from actual
visits. For `((i + 2) ** (i % 2) - 1) / 2` at visits 0, 1, 2, actual keys are
0, 1, 0. A failed mixed-enclosure division requests subdivision; it supplies no
integer fact. Every accepted subrange still passes existing exact arithmetic
and own-array checks. The flag resets per attempt, budgets remain shared, and
full bounds are restored. Complete key/receiver reload census, later-store checks,
independent reload-gap reproof and ordinary actual-write replay remain.
Fifteen raw CFG/SCF and ten source controls cover saved/retained children,
negative divisors, reload overlap, later stores, actual fractions and zero division.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.41 s test / 2.42 s total**.
- Three selected saved-throw sources: **48 native executions, 84 refusals,
  24 Node/VM observations PASS**, both providers and optimization policies,
  explicit/deduced C++, GCC and Clang. The selector runs the committed fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.47 s test / 2.49 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.15 s**, with 355 other lit cases excluded.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` passes.
- All **seven final code/test hashes match the devbox**. All **24 generated C++
  files** retain one selector evaluation, then an attribute read whose Boolean
  directly feeds the next write; no Script/VM protocol or nullable-scalar fallback.
  Linked-symbol checks pass in the source fixture.
- Source preservation retains **233 historical iterator bodies, 86 metadata
  rows, all 42 previous saved cases and 58 retained saved refusal bodies**.
  The original refused source alone is promoted unchanged. Local source checks
  record twelve positive and 24 refusal Node observations. Escape checks retain
  all 167 earlier source bodies and expectations; 177 Node outputs and own-key
  sets pass. All historical raw cases remain unchanged.

All focused gates passed on their first run. No production or test semantic
correction followed a gate. The source agent hit a rate limit after preparing
its candidate. The parent removed unrelated scratch-formatting changes with
AST-equality checks and the repository Black configuration before promotion;
source bodies and metadata remained unchanged. Independent reviews found no
blockers in either production change.

Initial actual-identity checks inspected 76 Linux and 351 Windows processes;
WSL-root executable-readlink checks then inspected 79 and 349. Prelanding checks
inspected 86 and 351. All completed checks found no matches or errors: Claude
was confirmed stopped. An initial sudo attempt required a password; WSL-root
completed the stronger check. No browser/shared implementation edits, local C++
builds, pushes or history rewrites were made.

Commands, selectors, generated artifacts and checksums are preserved in
`../test-results/2026-09-23-postselector-read-close/` relative to the monorepo.
Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizer
runs were skipped under the focused policy.

## Next boundary

Retained `unsupported-fourth-postselector-effect` source
`c6857b845f5106c4ebcf1297b6ad6a39ac43f39b3f81da979afe8b9bbd10cc25`
still refuses **DOM protected helper needs an independent inert-body proof**
under both policies. It adds a fourth `setAttribute('data-closed', false)` after
the post-selector read and third write, before the ignored close throw. Preserve
that complete sequence and the original saved body exception.

Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain unfinished.
No full-Bootstrap admission or coverage gain is claimed.
