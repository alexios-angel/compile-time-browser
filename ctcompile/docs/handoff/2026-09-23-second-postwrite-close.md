# Ordered iterator close writes and tight odd-power bounds, 2026-09-23

Resumed clean **98a8f335** and retained `second-postwrite-effect` source
`442af76b` from HANDOFF, current00 and iteration 95's final journal. No dirty
drafts or unmerged `codex-wip-20260907` remained; unrelated branches were
preserved. Parallel agents prepared source regressions, escape analysis and
independent native review. The parent completed the frozen source and escape
work after agent interruptions. User resume continued the same changes.

## Landed

**3098b880** admits the unchanged two-write source. The second `setAttribute`
retains its own exact unused-result suppression at the original guard. Its
method lookup and feeding `hasAttribute` remain after the first write. Complete
typed DOM reproof must exclude source exceptions and reentry from both writes
before this split can publish; each wrapper independently requires a valid
literal attribute name. Primitive arguments, receiver provenance, private
callable/frame checks, confined results, saved exception ownership and budgets
remain. Four added wrapper operations are charged before allocation.

Original source SHA-256:
`442af76b4d34d947dcbd8f2f3950aa971fb231514766bdd8311f850d267af2ff`.
Getter `d1153b3a` and missing-second-read `a71e54ad` variants also execute.
The latter writes true then false, making source order observable. Checks retain
saved false/true payloads, exhaustion, normal completion, reentry and two
documents. Raw controls cover invalid first and second names independently,
receiver/arity mismatches, escaped results, third effects and exact budgets.
Ordinary throwing closes remain refused. No browser implementation changed.

Parallel **a7342199** tightens the existing signed-unit power enclosure.
Negative-or-zero bases with positive odd varying exponents cannot produce +1;
the proof no longer invents that out-of-bounds array index. Positive bases,
even/zero exponents and mixed parity retain their wider bounds. Complete
reload/store census, scalar restrictions and actual-write replay remain.
All 122 historical source bodies/CHECKs and raw witnesses are unchanged;
thirteen source controls were added. General powers remain conservative.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.39 s test / 2.41 s total**.
- Three selected saved-throw sources: **48 native executions, 68 refusals,
  24 Node/VM observations PASS**, both providers and optimization policies,
  explicit/deduced C++, GCC and Clang. Preflight measured six admissions and
  sixteen refusals. The focused selector uses the actual committed fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS,
  2.33 s test / 2.34 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.13 s**, with 355 other lit cases excluded.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` passes.
- All **seven final code/test hashes match the devbox**. All **24 generated
  C++ files** contain no Script/VM protocol or nullable-scalar fallback;
  the execution harness's linked-symbol checks pass.
- Source preservation retains **233 historical iterator bodies, 86 metadata
  rows and all 30 previous saved cases**, including the original `442af76b`.
  Escape checks preserve all 122 earlier sources/CHECKs and pass 135 local
  Node outputs and own-key sets.

The independent native review found no blocker; the parent completed escape
review. Two SSH attempts failed before any build. `server.sh start` and
`allow-ip` restored access after an IP change; the subsequent focused gates
completed without production corrections. Linux/WSL root checks inspected
81 then 86 live processes; Windows CIM inspected 357 then 354. Actual executable,
CLI and loop identity checks had no matches or errors: Claude was confirmed
stopped. No browser/shared implementation edits, local C++ builds or pushes.

Build targets, selected checks, source helpers and generated evidence are in
`../test-results/2026-09-23-second-postwrite-close/` relative to the monorepo,
with a checksum manifest. Full CTest/compiler lit, the complete iterator fixture,
unaffected native replays, broad corpus/matrices, full Bootstrap, browser/WPT/
test262, Windows and sanitizer runs were skipped under the focused policy.

## Next boundary

Retained `unsupported-second-postwrite-selector-read` source
`f4005c21d70cda8651259272a5c329178fb8199adaeb43f772173c66d26a63ac`
still refuses **DOM protected helper needs an independent inert-body proof**
under both policies. Its close performs both writes, then calls
`anchor.matches('[data-closed]')` as the ignored throw payload. Preserve the
selector evaluation and saved Boolean; prove its exceptional behavior before
moving it out of suppression. No execution admission is claimed for this source.

Nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver,
full native Bootstrap and general powers remain unfinished. No full-Bootstrap
coverage gain is claimed.
