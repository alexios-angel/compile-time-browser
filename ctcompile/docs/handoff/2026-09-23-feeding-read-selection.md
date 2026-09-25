# Iterator-close feeding-read selection, 2026-09-23

Resumed clean **0ca10958** and retained source `005c36e5` from HANDOFF,
Current native work and iteration 114's journal. No dirty drafts or unmerged
`codex-wip-20260907` remained; unrelated branches were preserved. Source, raw
host-contract and escape agents worked independently. Two interruptions left
saved candidates, production edits and test artifacts; resumed work used those
without repeating the completed baseline or source preflight.

**8768efa0** selects the read feeding either initial protected write from the
write's actual operand. When a later argument read has already completed, it
joins the existing saved-value and complete-use census. Both snapshots remain
distinct and in source order. Missing reads, reuse and leaks still refuse.
All three helper-call paths share this proof. The clone order, source guard,
exact suppression, saved original exception, typed DOM/Style reproof before
publication and work budgets remain unchanged.

The original source is unchanged, SHA-256
`005c36e53d00452af3bd97932df6dea702e6a4cd0e2482b2335d320457d9ceb3`.
Getter `adcfb310` and order variant `b37ac443` also execute. Four raw admissions,
four typed refusals, nine structural refusals and two budget rows cover both
pending writes. All 28 historical raw MLIR literal bodies remain unchanged.
The source fixture preserves 233 historical bodies, 86 positive metadata rows,
93 saved cases, 202 retained refusals and 93 generated oracle constructions.
Only the retained original refusal becomes an execution case.

**0241f5fd** lets the existing bounded loop-index subdivision discover a varying
shift count when the left operand is invariant. Singleton subdivisions reuse
`boundedNumberBitwise` in the original operand order. For example,
`1 << (i % 2)` writes keys 1, 2, 1. Primitive conversion bounds, integral
intermediates, signed shifts, modulo-32 counts, complete reload/store checks,
independent reload gaps, work budgets and actual-write replay remain required.
Five new source child sites become confined; five controls remain stored.
All 324 historical source bodies and CHECKs remain unchanged.

Eight new CFG admissions and five refusals cover conversion, signed/unsigned
shifts, count wrapping, retained/saved children, reload gaps and invalid
intermediates. One SCF contents admission and one refusal explicitly retain the
incomplete legacy retention census. Two historical raw constant-left-shift
refusal inputs are preserved and now assert exact contents, reads and retention:
the left-shift case retains an unwritten child; the right-shift case clears both
original child slots. The first arrays gate caught these two stale expectations
(ten assertions); only their expectations changed. Production remained unchanged.

Focused validation:

- Source preflight: three admissions and ten refusals under both optimization
  policies, **26 checks PASS**.
- Native execution reached **48 executions, 24 Node/VM observations and 76
  refusals**. Its final local exit log was lost on interruption. The final
  manifest proves the fail-fast harness reached its last refusal after all
  executions, observations and 75 preceding refusals. Recovery reran all **48
  existing assertion binaries** and that final refusal successfully, without
  recompilation or a full source replay. The recovery log records this distinction.
- Exact `ctcompile_host_contract`: **1/1 PASS, 2.61 s test / 2.63 s total**.
- Final exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.68 s test /
  2.69 s total**. Lit did not run after the first failed arrays gate.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.19 s; 355 other cases excluded**.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Diff checks and scoped formatting pass. Local evidence includes 13 source
  syntax checks, 12 positive Node observations and ten escape key/read/own-key/
  original-identity witnesses.
- All six final code/test hashes match the devbox. The 24 generated C++ files
  retain distinct feeding/saved values, the later write and original exception,
  with no Script symbols. The source harness's standalone/linked checks completed.

Builds used locked `tools/remote-build.sh` with explicit targets: `ctjs-opt`,
`ctjs-translate`, `ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. CTests used exact anchored names and
`--output-on-failure --no-tests=error`; lit used the generated build configuration
and the exact anchored filter. Source selection used `throw_gate_mutable.py`.
Commands, snapshots, logs, generated C++ and checksums are retained in
`../test-results/2026-09-23-feeding-read-selection/` relative to the monorepo.

Initial process checks inspected 15 accessible Linux and 350 Windows processes;
final checks inspected 15 Linux and 345 Windows processes. No actual Claude
executable/CLI/loop matches appeared. Linux executable-link permission errors
(60 initially, 61 finally) kept status uncertain. Concurrent-area rules applied.
No browser/shared implementation changed; no runtime was changed to match native.
No push, history rewrite or local C++ build occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-Bootstrap coverage gain is claimed.

**Next:** retained `unsupported-saved-read-through-final-write-argument-read`,
SHA-256 `40cadb138078e7bb8c30498e697e70a5029d75b93fa3752908cd83edf87a3bb2`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Its final write uses an earlier saved read after another argument read:

```javascript
anchor.setAttribute('data-after-terminal', (
  anchor.hasAttribute('data-closed'), present
));
```

Preserve both observations, their order and the original body exception.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap, general powers and the legacy SCF
retention census remain unfinished.
