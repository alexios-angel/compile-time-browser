# Selector results in iterator closes and correlated array bounds, 2026-09-23

Resumed clean **de58563d** and retained `unsupported-third-postselector-result`
source `8038c1f8` from HANDOFF, current00 and iteration 98's final journal.
No dirty drafts or unmerged `codex-wip-20260907` remained; unrelated branches
were preserved. Iteration 99 was interrupted after editing. Its first host gate
completed successfully; the resumed session kept the edits and frozen candidates
and continued the pending checks. Parallel agents prepared source regressions,
escape analysis and independent review; the parent reconciled and committed them.

## Landed

**a78a11ed** admits the original selector-result source unchanged. Only the
following attribute write's value operand may consume the selector Boolean.
A consumed selector remains an ordinary ordered read under the original guard.
Before lifting it, expansion charges the literal's bytes and validates it through
`ctbrowser::style::css::parse_selector_text`. Complete typed DOM/Style reproof
still validates method/receiver identity, primitive arguments and non-reentry.
The final write keeps its exact suppression and independent valid-name check.
Private callable/frame proofs, saved exception ownership, ordinary-close
validation, confinement and cloning budgets remain. No browser implementation
or runtime was changed.

Original source SHA-256:
`8038c1f85c56ca2ee4800089c7e3379f9a29643668ede6bb7405f58d0bd686de`.
Getter `cfc0fc8d` and ordering variant `2e375c56` also execute. The latter writes
true, then false, evaluates `[data-closed=true]`, and writes the resulting false.
Node/VM observations and native DOM state check source order and the original
saved false/true body exception, exhaustion, normal completion, reentry, two
documents and owned sessions. Generated C++ passes the actual `js_boolean_t`
selector result directly to the following write; it adds no result container.

Parallel **31649d7e** shares the existing bounded whole-key subdivision
between array-bound and reload-gap proofs. A coarse range may include an
unreachable endpoint: `(2*i)**(i%2)` at visits 0, 1, 2 writes keys 1, 2, 1.
Subdivision proves every aligned subrange before recording a conservative own-array
footprint. Complete key/receiver reload census, later-store checks, independently
proved reload gaps and actual element replay remain. No scalar power rule changed.
Ten source and fourteen raw CFG/SCF controls were added.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.38 s test / 2.39 s total**.
- Three selected saved-throw sources: **48 native executions, 84 refusals,
  24 Node/VM observations PASS**, both providers and optimization policies,
  explicit/deduced C++, GCC and Clang. The selector runs the committed fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.40 s test / 2.41 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.14 s**, with 355 other lit cases excluded.
- Final full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` passes.
- All **eight final code/test hashes match the devbox**. All **24 generated C++
  files** retain one selector evaluation whose Boolean directly feeds the next
  attribute write, with no Script/VM protocol or nullable-scalar fallback.
  Linked-symbol checks pass in the source fixture.
- Source preservation retains **233 historical iterator bodies, 86 metadata
  rows and all 39 previous saved cases**. Local source checks recorded twelve
  positive and 24 refusal Node observations. Escape checks retain all 157 earlier
  source bodies, with exactly CHECK130 promoted; 167 Node outputs and own-key sets
  pass. Four historical raw bodies are retained as admission controls.

The first arrays gate failed sixteen checks across four historical refusal rows;
the first power lit run failed at CHECK130. Targeted Node witnesses confirmed the
new admissions: mixed nonpositive power writes exactly keys 0 and 1, and the
varying OR low-bit case writes 2,3,2,3,6,7,6,7,10,11,10,11 without touching its mask
at index 0. Exact bodies were preserved and expectations now check actual array
contents, reads and escaped identities. Adjacent even-offset power length extension
and AND mask-reload overlap controls still refuse. Production stayed unchanged;
only those expectations needed correction. The final focused rerun passes.
Independent reviews found no blockers. No runtime was changed to settle a mismatch.

Initial WSL-root process checks inspected 82 Linux processes and Windows CIM
inspected 355. Prelanding checks inspected 86 and 350 respectively. Actual
executable, CLI and loop identity checks found no matches or errors: Claude was
confirmed stopped. There were no browser/shared implementation edits, local C++
builds, pushes or history rewrites.

Commands, selectors, generated artifacts and checksums are preserved in
`../test-results/2026-09-23-selector-result-close/` relative to the monorepo.
Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizer
runs were skipped under the focused policy.

## Next boundary

Retained `unsupported-third-postselector-read` source
`fcad547a0a87a8302831d843ef17a6cf52bcbfa20c0b5e7b3511d4079e09178f`
still refuses **DOM protected helper needs an independent inert-body proof**
under both policies. Its extra `hasAttribute` producer occurs after the selector
and feeds the final attribute write. Prove that complete ordering and exception
behavior without changing its saved body exception.

Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain unfinished.
No full-Bootstrap admission or coverage gain is claimed.
