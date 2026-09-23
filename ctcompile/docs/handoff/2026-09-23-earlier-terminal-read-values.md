# Earlier terminal iterator-close read values, 2026-09-23

Resumed clean **6b908f6b** and retained source `378326b6` from HANDOFF,
current00, the terminal-read-values detail and iteration 107's final journal.
No dirty drafts or unmerged `codex-wip-20260907` remained; unrelated branches
were preserved. Source, raw-host and escape workflows ran in parallel. All
three agents saved work before rate limits; the parent finished raw/escape
integration and final review. The source agent independently reviewed the native
production diff without finding a defect before its later review was interrupted.

**9ba39cb4** retains terminal `hasAttribute` results in the existing suffix use
census. One later `setAttribute` may consume a saved result; erasing that candidate
and checking every operand use rejects reuse, leaks, receiver/name uses and
selector-result substitution. Reads stay in source order under the original
guard; complete typed DOM/Style reproof still excludes source throws and reentry.
The original saved body exception wins. Reads inside a pending write use the
existing proof. No platform implementation or runtime fallback was added.

The original source is unchanged, SHA-256
`378326b6423bc5f439e4fd413d8adfa54606c7e1bb2a1cd3aea37ee955e8909e`.
Getter `134465a7` and reordered-read `07576d80` also execute. Reordering changes
the Boolean written to `data-after-terminal` from true to false, checking that
the earlier read supplies the value. Raw tests cover ignored reads and selectors,
direct calls, typed refusals, extra uses, reuse, call order and exact/one-less
budgets. Two unchanged historical raw refusals now admit with local Node
call-order/value witnesses. Before building, the parent made the raw assertion
recognize a saved read by its consumer so invalid literal-name controls still
exercise typed refusal correctly.

**ad45ffe5** adds subtraction to the existing bounded varying-operand
refinement. It subdivides the whole key until the unchanged scalar difference
transfer proves each accepted range. For example, `(i + 1) - i` writes only index
one. It preserves source operand order, intermediate arithmetic checks, complete
producer reload/store census, independent reload-gap proof and actual-write
replay. Negative own keys, fractional intermediates and overlapping producer
stores remain refused. Two historical raw `i-i` bodies now admit with exact
Node own-key/read/retained-child witnesses; all 255 historical source bodies and
CHECKs remain unchanged. Eight source controls were added.

Focused validation on the devbox:

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.43 s test / 2.44 s total**.
- Three selected saved-throw sources: **48 native executions, 76 refusals and
  24 Node/VM observations PASS**, both browser providers and optimization policies,
  GCC/Clang and explicit/deduced C++. Preflight admits three positives and refuses
  ten controls under both policies. Execution selects the committed fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.59 s test / 2.60 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.17 s**; 355 other lit cases excluded.
- `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting, Python/Node syntax, scratch `bash -n` and diff checks pass.
- Seven final code/test hashes match the devbox. All 24 generated C++ files retain
  both selectors, ordered terminal reads, the earlier Boolean feeding the final
  write, and the saved body exception. No Script/VM or nullable fallback appears;
  fixture standalone and linked checks pass.
- Preserved 233 historical iterator bodies, 86 metadata rows, 72 saved cases,
  140 retained saved refusal bodies and 72 historical oracle constructions.
  Local Node checks cover 12 positive and 20 refusal observations. Historical
  oracle construction was compared without replay. Eight focused escape Node
  outputs/writes/reads/own-key/identity checks and syntax for 263 source functions
  pass; the two historical raw subtraction witnesses preserve surviving children.

All focused devbox gates passed on their first run. No test expectation was
changed after a failed gate. The raw assertion adjustment and scratch formatting
were completed before the first build.

Builds used `tools/remote-build.sh` with explicit targets under the shared devbox
lock. The host gate built `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`; the escape
gate built `ctjs-opt`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`. Commands,
selectors, witnesses, artifacts and checksums are in
`../test-results/2026-09-23-earlier-terminal-read-values/` relative to the monorepo.

Initial/final availability checks inspected 14/19 accessible Linux processes
and 347/345 Windows processes. No executable/CLI/loop matches appeared, but 60
Linux executable-link permission errors left Claude status uncertain.
Concurrent-agent area rules were observed; no browser/shared implementation
edits, local C++ builds, pushes or history rewrites occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-Bootstrap coverage gain is claimed.

**Next:** retained `unsupported-terminal-selector-write-earlier-value`, SHA-256
`f3f1e6c62f7ab403f59b5f4a43015ab7fa5a17731bce8e868280d001233c6312`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Save the second `matches('[data-closed=false]')` Boolean across two
ignored `hasAttribute` calls, then feed `data-after-terminal`, while the original
body exception wins. Reads before terminal mode, single-write selector cleanup,
nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver,
full native Bootstrap and general powers remain.
