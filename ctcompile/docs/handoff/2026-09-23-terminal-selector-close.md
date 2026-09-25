# Terminal iterator-close selectors and singleton bitwise masks, 2026-09-23

Resumed clean **3fe78026** and retained terminal-selector source `5f783ab2`
from HANDOFF, current00, the terminal-read detail and iteration 102's final journal.
No dirty drafts or unmerged `codex-wip-20260907` remained. Unrelated branches were
preserved. Parallel agents prepared source regressions and escape analysis;
an independent agent reviewed both changes. The parent gated and committed them.

## Landed

**76d69d1e** reuses the final-read slot for a terminal `matches` call, retaining
that call in the existing exact unused-result suppression. Complete typed DOM
reproof validates its receiver and literal selector through the public Style
parser, charging selector bytes before parsing. The complete use census permits
only the method's callee use and validated roots; its result cannot feed another
write or observer. Sequential cloning retains both selectors and every write at
the original guard. Private callable/frame checks, the saved body Boolean and
proof/expansion budgets remain. No browser/runtime implementation changed.

The original source is unchanged, SHA-256
`5f783ab2c7efefc210bbd1f8f4c3b21385e07cc1b73b01df6ea48f53d9f7b437`.
Getter `4cf29d3e` and order-sensitive `263d6851` execute too. The latter changes
the final write to true: the first selector returns true and the terminal selector
returns false. Tests retain false/true saved exceptions, normal completion,
exhaustion, repeated calls, separate documents and owned sessions.

**82f62403** extends existing singleton range reproof to bitwise masks. For
AND/OR/XOR, either operand may supply the singleton; only these commutative
operations swap proof operands. Source evaluation order is unchanged. Existing
ToInt32 conversions, bitwise enclosures, complete producer/receiver reload and
later-store census, independent reload-gap reproof and actual-write replay remain.
Seventeen raw CFG/SCF and eleven source controls cover both operand orders,
retained/saved children, reload gaps, overlapping/later stores, varying masks
and non-own writes.

## Focused validation

- Exact `ctcompile_host_contract`: initial **1/1 PASS, 2.45 s test / 2.46 s total**;
  final corrected raw control **1/1 PASS, 2.43 s test / 2.44 s total**.
- Three selected saved-throw sources: **48 native executions, 76 refusals and
  24 Node/VM observations PASS**, both providers and optimization policies,
  explicit/deduced C++, GCC and Clang. The selector runs the committed fixture.
- Admission preflight: three positives and ten refusals under both policies.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.50 s test / 2.51 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.16 s**, with 355 other lit cases excluded.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` and scratch gate `bash -n` checks pass.
- All **seven final code/test hashes match the devbox**. All **24 generated C++
  files** retain two selector calls, the terminal selector after every write,
  and the preceding attribute-read Boolean feeding its write. No Script/VM
  or nullable-scalar fallback appears; linked-symbol checks pass in the fixture.
- Source preservation retains **233 historical iterator bodies, 86 metadata
  rows, 53 saved cases and 91 retained saved refusal bodies**. Only the original
  terminal-selector refusal is promoted. Twelve positive and twenty refusal
  local Node observations pass. Historical oracle scripts/expectations were
  compared without reexecuting them. Escape checks preserve all 199 historical
  source bodies/expectations and raw cases; the **eleven new Node outputs and
  own-key sets pass**.

Independent review found that a new raw refusal reused an earlier write method
and therefore missed the new selector-to-write guard. The corrected control
uses a fresh write lookup and passes the final host rebuild; production remained
unchanged after the first gate. Escape preparation corrected new-test formatting
and a local negative-mask witness expectation before freezing. All devbox checks
passed; final independent reviews found no remaining issues.

Initial WSL-root `/proc` and Windows CIM identity checks inspected **81/351**
processes; prelanding checks inspected **87/350**. Both found no actual Claude
executable, CLI or loop matches and no errors. Claude was confirmed stopped.
No browser/shared implementation edits, local C++ builds, pushes or history
rewrites were made.

Commands, selectors, source witnesses, generated artifacts, logs and checksums
are preserved in `../test-results/2026-09-23-terminal-selector-close/` relative to
the monorepo. Full CTest/compiler lit, complete iterator fixtures, unaffected
native replays, broad corpus/matrices, full Bootstrap, browser/WPT/test262,
Windows and sanitizer runs were skipped under the focused policy.

## Next boundary

Retained `unsupported-terminal-match-read`, SHA-256
`54c46eea3a0177a82cfd63247ab637b9d24bdc7f86e32979faa54d5bf814181f`,
refuses **DOM protected helper needs an independent inert-body proof** under
both policies. Its final `hasAttribute` follows the second selector and supplies
the ignored close throw. Preserve that read and the original saved body exception.

Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap, general powers and genuinely
varying bitwise masks remain unfinished. No full-Bootstrap coverage gain is claimed.
