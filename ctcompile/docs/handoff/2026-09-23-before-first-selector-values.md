# Reads before iterator-close selectors, 2026-09-23

Resumed clean **3111e52e** and retained source `b3f9a6ca` from HANDOFF,
current00, the before-selector-read-values detail and iteration 110's journal.
No dirty drafts or unmerged `codex-wip-20260907` remained; unrelated branches
were preserved. Three agents began independent source, raw-host and escape work.
The source agent froze its candidate before a rate limit; the escape agent claimed
paths and identified a possible boundary but left no edits. The parent completed
both tasks. The raw agent independently reviewed native production without findings.

**795982ce** changes three conditions in protected helper expansion: standalone
reads use the existing suffix slots and use census after the second protected
write, before the first selector. This preserves one saved `hasAttribute` Boolean
across both selectors and intervening writes. The initial feeding writes, first
selector validation, single-consumer census, exact suppression, source guard/order,
private helper/capture proof, original saved exception and budgets remain. Direct,
closure and custom-iterator calls share this path. Complete typed DOM/Style reproof
still precedes publication; generated code calls public browser APIs.

The original source is unchanged, SHA-256
`b3f9a6ca302aafc1aea9f6654a390a2c8f62988ce56e02184f287e02b6b7a9af`.
Getter `97348f35` and order variant `c25de188` also execute. The latter saves false
from `data-unvisited` before the first selector, while the later ignored
`data-closed` read is true. Its final write must retain the earlier false.
Four raw positives, four typed refusals, seven structural refusals and two budget
rows cover both selectors, direct calls, ignored reads, invalid receivers/names,
leaks, reuse and exact source order. Historical raw bodies are unchanged.

**27ae4a64** lets a failed exact scalar transfer request existing bounded
subdivision when its input is a mixed enclosure. The witness visits indices 0,
3 and 6; `i % 5` visits 0, 3 and 1 while the enclosing lattice includes 4.
Adding 4294967292, multiplying by 1431655765, or subtracting from -4294967292
keeps every actual intermediate within the existing signed magnitude bound,
but the enclosing endpoint would exceed it. The final remainder writes 0, 1, 1.
Every accepted subdivision still needs the existing scalar proof. Actual unbounded
or fractional intermediates, later producer stores and overlapping reloads refuse.
The complete store/reload census, independent reload-gap reproof, restored induction
bounds, work budget and actual-write replay are unchanged. Seven raw admissions,
four raw refusals and nine source controls were added; no historical expectation
was changed. Parent review completed; no independent escape review completed.

Focused validation on the devbox:

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.48 s test / 2.49 s total**.
- Three selected saved-throw sources: **48 native executions, 76 refusals and
  24 Node/VM observations PASS**, both browser providers and optimization policies,
  GCC/Clang and explicit/deduced C++. Preflight admitted three positives and refused
  ten controls under both policies. Execution selected the promoted live fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.89 s test / 2.90 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.19 s; 355 other cases excluded**.
- `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS** before
  each code commit. Scoped formatting, Node/Python syntax, scratch `bash -n` and
  diff checks pass. All focused gates passed first run.
- All six changed code/test hashes match the devbox. All 24 generated C++ files
  retain the saved read before the first selector, its Boolean through the ignored
  read to the final write, and the original saved exception. Script/VM and nullable
  fallback checks plus standalone/linked symbol checks pass.

Preserved 233 historical iterator bodies, 86 metadata rows, 81 saved cases,
166 retained saved refusal bodies and 81 historical oracle constructions.
Local source checks cover 12 positive Node observations and 13 syntax checks.
All 285 historical escape source bodies/CHECKs and all historical raw bytes remain
unchanged. Local checks cover 294-source syntax and nine exact Node write/read,
own-key and original-child identity witnesses. Before the escape change, all nine
scratch sources were conservative. The first scratch claims invocation only
printed usage because required flags were missing; its corrected invocation
completed. No validation result is claimed from that usage error.

Builds used `tools/remote-build.sh` with explicit targets under the shared lock.
Host targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`. Escape targets: `ctjs-opt`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
`ctcompile-test-type-oracle`. CTests used exact anchored names with
`--output-on-failure --no-tests=error`; lit used the generated build configuration
and exact anchored filter above. Source selection used `throw_gate_mutable.py` in
the evidence folder. Exact commands, selected harnesses, source snapshots,
Node witnesses, logs and checksums are retained in
`../test-results/2026-09-23-before-first-selector-values/` relative to the monorepo.

Initial Linux executable/CLI/loop checks inspected 15 accessible processes, with
61 executable-link errors; Windows CIM inspected 347 processes. Final checks
inspected 18 accessible Linux and 349 Windows processes, with 60 Linux errors.
No actual Claude matches appeared; status stayed uncertain and concurrent-agent
area rules remained in force. No browser/shared implementation edits, local C++
builds, pushes or history rewrites occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-Bootstrap coverage gain is claimed.

**Next:** retained `unsupported-earlier-read-write-before-second-write`, SHA-256
`40e597cdc700f6206d15db2bd95a1c294499a78157161d678328ab7827158cdd`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Preserve `hasAttribute('data-closed')` captured after the first protected
write but before the second, through both selectors and later writes for
`data-after-terminal`, while the original body exception wins. Single-write
selector cleanup, nonterminal exceptional state, multiple protected regions,
implicit cleanup, nested custom iterators, unguarded Bootstrap defaults, the
application driver, full native Bootstrap and general powers remain.
