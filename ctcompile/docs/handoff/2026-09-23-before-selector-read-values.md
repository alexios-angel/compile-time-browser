# Saved reads across iterator-close selectors, 2026-09-23

Resumed clean **97222f8e** and retained source `abc08f6e` from HANDOFF,
current00, the terminal-selector-values detail and iteration 109's journal.
No dirty drafts or unmerged `codex-wip-20260907` remained; unrelated branches
were preserved. Three agents began independent source, raw-host and escape work.
All hit rate limits. The parent completed the raw tests, saved source candidate,
review and gates; the escape agent completed its change before its rate limit.
No independent agent review completed this iteration.

**4b063cb7** removes the unnecessary terminal-selector state in protected helper
expansion. Standalone `hasAttribute` reads after the first selector immediately
join the existing complete use census. One later write may consume their saved
Boolean across intervening reads/selectors. Pending write/read pairs retain their
existing proof; consumed selectors retain literal syntax validation. Direct,
closure and custom-iterator callers share this path. Source order and guard,
complete typed DOM/Style reproof, exact suppression, work budgets and the original
saved body exception remain. Generated code calls public browser APIs without
Script/VM or nullable fallback.

The original source is unchanged, SHA-256
`abc08f6e295ef84717d9d294826af5a3372f99b74eaafdb13c025795e2ef4f19`.
Getter `4141cd37` and order variant `01d1d76e` also execute. The order variant
captures false from `data-unvisited` before the second selector; a later ignored
`data-closed` read is true. Its final write must use the saved false. Raw tests
cover source order, direct calls, typed failures, extra uses/reuse and exact
budgets. All historical raw source bodies remain unchanged.

**388db8e0** adds numeric Add to existing bounded operand refinement. Every
accepted subdivision still needs the existing exact scalar sum proof; String
concatenation and fractional/unbounded intermediates cannot borrow it. Complete
reload/store census, independent reload gaps, restored induction bounds, work
limits and actual-write replay remain. Ten CFG, six SCF and eleven source controls
cover cancellation, retained and saved children, reload mutations and interior
array growth. Two historical raw `i/2+i` refusal bodies now admit unchanged:
Node writes keys 0 and 3, retaining the original children at keys 1 and 2.
All 274 historical source bodies and CHECKs remain unchanged.

Focused validation on the devbox:

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.55 s test / 2.56 s total**.
- Three selected saved-throw sources: **48 native executions, 76 refusals and
  24 Node/VM observations PASS**, both browser providers and optimization policies,
  GCC/Clang and explicit/deduced C++. Preflight admitted three positives and
  refused ten controls under both policies. Execution selected the live fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.73 s test / 2.74 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`: **1/1 PASS, 0.18 s; 355 other cases excluded**.
- `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Python/Node syntax, scoped formatting, scratch `bash -n` and diff checks pass.
- Seven final code/test hashes match the devbox. All 24 generated C++ files retain
  the read before the selector, its Boolean through the ignored read to the final
  write, and the original saved exception. Standalone and linked symbol checks pass.

The initial host run failed five new-row assertions because an existing assertion
still expected the saved read after both selectors. The corrected assertion counts
its exact preceding operations and separately requires it before the second
selector; historical expectations are unchanged. The initial array run found one
new raw fixture's `%sum` temporary colliding with the base fixture's SSA name.
Renaming that temporary fixes parsing; production and semantic expectations did
not change. The lit case had not run after that failure. The first formatter ran
while the escape agent was editing and found unformatted new rows; final checks
use the frozen sources. These failures and corrected runs are retained in evidence.

Preserved 233 historical iterator bodies, 86 metadata rows, 78 saved cases,
157 retained saved refusal bodies and 78 historical oracle constructions.
Local checks pass for 12 positive Node observations and 13 focused source syntax
checks. Escape local checks cover 285-source syntax and 11 new plus two historical
Node write/read/own-key/original-identity witnesses. String sums create keys `00`,
`1-1`, `2-2`; interior growth writes 1, 3, 1, 3 and finishes at length four.

Builds used `tools/remote-build.sh` with explicit targets under the shared devbox
lock. Host targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`. Escape targets: `ctjs-opt`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
`ctcompile-test-type-oracle`. Exact commands, source snapshots, selected harnesses,
logs, witnesses and checksums live in
`../test-results/2026-09-23-before-selector-read-values/` relative to the monorepo.

Linux ps and executable/CLI/loop checks inspected 20 initially accessible processes;
60 entries were inaccessible or disappeared. Windows CIM inspected 350 processes.
Final checks inspected 19 accessible Linux and 350 Windows processes, with 60
Linux permission errors. No actual Claude matches appeared; status stayed uncertain
and concurrent-agent area rules remained in force. No browser/shared implementation
edits, local C++ builds, pushes or history rewrites occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-Bootstrap coverage gain is claimed.

**Next:** retained `unsupported-earlier-read-write-before-first-selector`, SHA-256
`b3f9a6ca302aafc1aea9f6654a390a2c8f62988ce56e02184f287e02b6b7a9af`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Preserve `hasAttribute('data-closed')` captured before the first selector
through both selectors and intervening writes for `data-after-terminal`, while
the original body exception wins. Single-write selector cleanup, nonterminal
exceptional state, multiple protected regions, implicit cleanup, nested custom
iterators, unguarded Bootstrap defaults, the application driver, full native
Bootstrap and general powers remain.
