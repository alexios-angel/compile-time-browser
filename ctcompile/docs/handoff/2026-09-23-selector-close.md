# Selector reads in iterator closes and zero/unit power exponents, 2026-09-23

Resumed clean **740279da** and retained selector-read source `f4005c21` from
HANDOFF, current00 and iteration 96's final journal. No dirty drafts or unmerged
`codex-wip-20260907` remained; unrelated branches were preserved. Parallel agents
prepared source regressions and escape analysis. The parent completed the source
candidate after its agent hit a rate limit; the escape agent also reviewed the
native diff after the separate reviewer hit a rate limit.

## Landed

**1f353fa9** admits the original two-write, final-selector source unchanged.
The selector lookup and `matches` call remain after both writes at the source
guard. Its result must be unused, and its own exact suppression region requires
a literal selector accepted by the public `ctbrowser::style::css::parse_selector_text`
API. The parser entry and options match the native wrapper and VM binding; bytes
are charged before parsing. CTNativeAnalysis now links the public Style library.
Complete typed DOM/Style reproof, primitive arguments, private callable/frame
checks, confined results, saved Boolean ownership and clone budgets remain.

Original source SHA-256:
`f4005c21d70cda8651259272a5c329178fb8199adaeb43f772173c66d26a63ac`.
Getter `0749c918` and missing-read/selector-value `ae9f1c20` variants also execute.
Checks retain saved false/true payloads, exhaustion, normal completion, reentry,
two documents, and owned sessions. The missing-read variant writes true then
false, making order observable. Node/VM shims additionally record selector
evaluation. Generated output must retain its selector call, and raw IR checks
verify its position after both writes. Invalid/computed selectors, mismatched
receivers, observed results, missing/extra arguments, later effects and ordinary
throwing closes still refuse. No browser implementation changed.

Parallel **8ba1a605** admits varying bounded nonunit bases raised to zero or one.
The enclosing range contains the base and one; existing whole-key refinement
retains correlated reload gaps. Complete reload/store census, scalar restrictions
and actual-write replay remain. Two historical raw refusal bodies now admit
unchanged. All 135 historical power source bodies/CHECKs remain; ten source cases
were added. General powers retain their conservative boundary.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.40 s test / 2.41 s total**.
- Three selected saved-throw sources: **48 native executions, 68 refusals,
  24 Node/VM observations PASS**, both providers and optimization policies,
  explicit/deduced C++, GCC and Clang. Preflight measured six admissions and
  sixteen refusals. The focused selector uses the actual committed fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS,
  2.34 s test / 2.35 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.13 s**, with 355 other lit cases excluded.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` passes.
- All **nine final code/test hashes match the devbox**. All **24 generated
  C++ files** retain one selector call and contain no Script/VM protocol or
  nullable-scalar fallback; the execution harness's linked-symbol checks pass.
- Final source preservation retains **233 historical iterator bodies,
  86 metadata rows and all 33 previous saved cases**. Local source checks pass
  12 Node observations. Escape checks preserve all 135 earlier sources/CHECKs
  and pass 145 local Node outputs and own-key sets.

The first native execution gate found a test-harness omission: the new selector
signature requires a Style engine and Style linking. The harness now uses the
existing selector-test setup; production code did not change after its first
successful host/admission gate. A formatter check overlapped that edit and failed;
the final formatter passes after formatting the changed Python file. Independent
native review and parent escape review found no blocker.

Linux/WSL root checks inspected 85 then 84 live processes; Windows CIM inspected
348 then 351. Actual executable, CLI and loop identity checks found no matches
or errors: Claude was confirmed stopped. No browser/shared implementation edits,
local C++ builds, pushes or history rewrites.

Build commands, focused selectors, generated artifacts and checksums are preserved
in `../test-results/2026-09-23-selector-close/` relative to the monorepo. Full
CTest/compiler lit, complete iterator fixtures, unaffected native replays, broad
corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizer runs
were skipped under the focused policy.

## Next boundary

Retained `unsupported-after-selector-effect` source
`6d1b50006a02ae3ffa9c40e247f565ade1b71b6f43605c55afb8e9d9dc9ac03c`
still refuses **DOM protected helper needs an independent inert-body proof**
under both policies. Its close evaluates the selector after both writes, then
performs a third attribute write. Preserve all effects, their order and the saved
Boolean while proving the complete exceptional sequence. Single-write selector
cleanup also remains outside the current narrow source shape.

Nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver,
full native Bootstrap and general powers remain unfinished. No full-Bootstrap
coverage gain is claimed.
