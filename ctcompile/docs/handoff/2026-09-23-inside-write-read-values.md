# Saved reads inside iterator-close arguments, 2026-09-23

Resumed clean **254fed05** and retained source `9533d479` from HANDOFF,
Current native work, the before-first-write-read-values detail and iteration 113's
journal. No dirty drafts or unmerged `codex-wip-20260907` remained; unrelated
branches were preserved. Iteration 114 was interrupted after the compiler build
and complete source preflight. The parent resumed the saved source candidate and
raw edits without repeating those checks. Restarted escape work and an independent
native review ran alongside integration and focused gates. Each concern was
committed separately.

**8bd4c58b** moves a completed earlier argument read into the existing saved-read
and complete-use census when the next read starts. The last read must still feed
the pending first or second write. This preserves distinct Boolean snapshots and
source evaluation order inside `setAttribute` arguments. Incomplete reads, leaks,
reuse and missing feeding obligations remain refused. Direct, closure and custom
iterator calls share this expansion path; exact suppression, private helper/capture
proof, source guard/order, saved exception and budgets remain. Complete typed
DOM/Style reproof still precedes publication. No runtime implementation changed.

The original source is unchanged, SHA-256
`9533d4795f42ca4159beec6d3a4c202898473c57f3ed19ed0248d23048ae9e63`.
Getter `caedb625` and order variant `86c2d224` also execute. The saved Boolean is
false on the first call and reflects the earlier attribute state on repeated
calls; it remains separate from the first write's feeding read. The final write
uses the saved value while the original body exception wins. Four raw admissions,
four typed refusals, nine structural refusals and two budget rows cover reads
inside both pending writes, exact method/read order, distinct feeding identity,
unused reads, direct calls, leaks, reuse and incomplete methods. Historical raw
literal bodies, source cases and oracle constructions remain unchanged.

**9e5ee141** permits the existing index proof to discover a varying right operand
for division/remainder with an invariant numerator. A varying divisor requests
bounded whole-key subdivision; a singleton divisor uses the existing scalar
`boundedNumberDivision` with the original operand order. Both source operands
remain bounded, divisors nonzero and quotients integral. Remainder retains the
numerator's sign. No arithmetic bound or work budget changes. Complete reload/store
census, independent numerator/divisor reload gaps, restored index bounds and actual
write replay remain required.

Nine CFG admissions and five refusals cover negative numerator/divisor, zero
numerator, retained children, saved identity, both reload positions, later stores,
overlap, poles, fractional quotients and unbounded numerator arithmetic. One SCF
contents admission and one fractional refusal use the same proof. The SCF rows
explicitly retain the existing incomplete legacy retention census. Ten source
controls preserve exact write keys, read snapshots, own keys and original child
identity; all 314 historical source bodies/CHECKs and raw prefix remain unchanged.

Focused validation on the devbox:

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.54 s test / 2.55 s total**.
- Three selected saved-throw sources: **48 native executions, 76 refusals and
  24 Node/VM observations PASS**, both browser providers and optimization policies,
  GCC/Clang and explicit/deduced C++. Preflight admitted three positives and refused
  ten controls under both policies. Execution used the promoted live fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.65 s test / 2.66 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.19 s; 355 other cases excluded**. Before the escape change, all ten
  scratch child sites were stored; the selected lit proves four confined and six
  stored. Retention was not claimed for raw SCF regions.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting, source syntax, scratch `bash -n` and diff checks pass.
- All six final code/test hashes match the devbox. All 24 generated C++ files
  retain the saved argument Boolean, its later write and the original exception.
  Script/VM and nullable fallback checks plus standalone/linked symbol checks pass.
- Local source evidence covers 13 syntax checks and 12 positive Node observations;
  escape evidence covers 324-source syntax and ten exact Node key/read/own-key/
  original-identity witnesses, including uninstrumented outcomes.

The first escape CTest failed two assertions on the new SCF positive: contents,
reads and exit identity were correct, but the induction harness also demanded
complete legacy retention. `computeVerdicts` deliberately marks the direct-storage
census incomplete for any operation with regions, and `refineArrayRetention`
requires that census. The existing structured harness checks contents separately.
Only the new test was corrected to follow that distinction and explicitly assert
incomplete legacy retention. Production and exact contents expectations were
unchanged. Lit did not run after the first failed CTest. Native checks passed
first run. Initial local formatting was corrected before its gates.

Builds used `tools/remote-build.sh` with explicit targets under the shared lock.
Native targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`. Escape targets: `ctjs-opt`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
`ctcompile-test-type-oracle`. CTests used exact anchored names with
`--output-on-failure --no-tests=error`; lit used the generated build configuration
and the exact anchored filter above. Source selection used `throw_gate_mutable.py`.
Exact commands, selected harnesses, source snapshots, Node witnesses, logs and
checksums are retained in `../test-results/2026-09-23-inside-write-read-values/`
relative to the monorepo.

Initial Linux executable/CLI/loop checks inspected 15 accessible processes and
Windows CIM inspected 347; the final check inspected 15 Linux and 354 Windows
processes. No actual Claude matches appeared; 61 Linux executable-link errors kept
status uncertain. Concurrent-agent area rules remained in force. Recent explicit
iterator-throw routing, Shell collection bounds and historical WPT measurements
were read; no runtime was changed to match native output. No browser/shared
implementation edits, local C++ builds, pushes or history rewrites occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-Bootstrap coverage gain is claimed.

**Next:** retained `unsupported-feeding-read-before-saved-read-inside-first-write`,
SHA-256 `005c36e53d00452af3bd97932df6dea702e6a4cd0e2482b2335d320457d9ceb3`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Its first read feeds the first write, while a later argument read is
saved for the final write:

```javascript
let present, feeding;
anchor.setAttribute('data-closed', (
  feeding = anchor.hasAttribute('data-visited'),
  present = anchor.hasAttribute('data-closed'),
  feeding
));
```

Preserve both snapshots, source evaluation order and the original body exception.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain.
