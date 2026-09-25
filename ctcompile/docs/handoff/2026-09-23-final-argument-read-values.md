# Saved reads through final iterator-close arguments, 2026-09-23

Resumed clean **d58cbb32** and retained source `40cadb13` from HANDOFF,
Current native work and iteration 115's journal. No dirty drafts or unmerged
`codex-wip-20260907` remained; unrelated branches were preserved. Three agents
prepared source/raw checks and investigated escape analysis. After interruption,
the parent reused the completed compiler build and frozen source candidate;
resumed raw work completed its checks and independent review before rate limiting.

**51d60934** reuses `selectFeedingRead` for the final protected write. When its
argument list evaluates another `hasAttribute`, the write still consumes the
earlier saved Boolean. The displaced read remains in the complete use census.
Leaks, reuse, incomplete calls and invalid receivers still refuse. Source-order
cloning, the original guard, exact suppression, the saved body exception,
complete typed DOM/Style reproof before publication and work budgets remain.
All helper-call paths share this proof; no runtime implementation changed.

The original source executes unchanged, SHA-256
`40cadb138078e7bb8c30498e697e70a5029d75b93fa3752908cd83edf87a3bb2`.
Getter `6153974a` and order variant `db6cf9e2` also execute. Three new raw
admissions, four typed refusals, nine structural refusals and two budget rows
check saved identity, argument-read order and the complete use census.
One historical raw refusal also becomes an admission: `terminalReadValue`
whose final write consumes `%secondTerminalPresent`. The first host gate found
this stale expectation. Its exact input was preserved and moved to existing
saved-value identity/order assertions. Production remained unchanged.

All 28 historical raw MLIR literals are unchanged. The source fixture preserves
233 historical bodies, 86 positive metadata rows, 96 saved cases, 211 retained
refusal bodies and 96 generated oracle constructions. Only the retained original
source refusal moves into execution coverage.

Focused validation:

- Preflight: three admissions and ten refusals under both optimization policies,
  **26 checks PASS**. The next selector source refuses under both policies.
- Selected saved-throw source execution: **48 native executions, 76 refusals,
  24 Node/VM observations PASS**, including standalone/linked checks.
- Final exact `ctcompile_host_contract`: **1/1 PASS, 2.83 s test / 2.84 s total**.
  The first run failed only the historical refusal described above.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting and diff checks pass. Formatting of the moved test row was
  corrected before the final hash comparison.
- All three final code/test hashes match the devbox. All 24 generated C++ files
  preserve the saved final-write value, ordered read and original exception,
  with no Script symbols. Local checks preserve 13 source syntax cases and
  12 positive Node observations.
- Independent read-only native review found no actionable issues. It traced
  feeding-read membership, complete uses, helper callers, source cloning,
  suppression, private DOM/Style reproof and budgets. The parent reviewed the
  subsequent historical-test promotion; production did not change afterward.

Builds used locked `tools/remote-build.sh` with explicit targets `ctjs-opt`,
`ctjs-translate`, `ctcompile-test-native-reference` and
`ctcompile-test-host-contract`. CTest used the exact anchored name with
`--output-on-failure --no-tests=error`. Source selection used the saved
`throw_gate_mutable.py`; completed native execution was not repeated after the
raw expectation correction. Commands, source snapshots, checks, logs and
generated C++ are retained with checksums in
`../test-results/2026-09-23-final-argument-read-values/` relative to the monorepo.
The first artifact collection obtained native data but reported missing escape
claims because the failed host gate had skipped that probe; final collection passed.

The parallel escape investigation found existing invariant/both-varying shift
and arithmetic paths already covered. It made no tracked change. A single-source
probe using the existing escape-claims executable measured this next boundary:

```javascript
function varyingTableKey() {
    var child = {}, a = [child, 0, child], keys = [0, 2];
    for (var i = 0; i < a.length; i++) a[keys[i % 2]] = 0;
    return a;
}
```

Node observes reads `[[0,0],[1,2],[0,0]]`, writes `[0,2,0]`, own keys
`["0","1","2"]` and final `[0,0,0]`, with the original child absent.
The compiler reports four sites, zero confined, and the child `escapes:stored`
(program `5b68fc851b4e5376`, function 1/site 4). This is measured missed precision,
not an escape gain. `LoopProof` currently resolves table reads through invariant
keys; varying table reads need their own dependency proof, including mutations
and aliases. The exact source, Node witness and claims are in the evidence.

Initial process checks inspected 21 accessible Linux and 348 Windows processes;
final checks inspected 21 Linux and 349 Windows processes. No actual Claude
executable/CLI/loop matched. Sixty Linux executable-link permission errors kept
status uncertain. Concurrent-area rules applied. No browser/shared implementation
write, push, history rewrite or local C++ build occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
escape arrays/lit replays, broad corpus/matrices, full Bootstrap,
browser/WPT/test262, Windows and sanitizers were skipped. Escape code did not change.
No full-Bootstrap coverage gain is claimed.

**Next native boundary:** retained
`unsupported-saved-selector-through-final-write-argument-read`, SHA-256
`c9ccc67215059eb8d0f3800da50d033f3418e4058715ee9eb071c16bf9c349b3`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Its saved `matches` Boolean feeds the final write after another argument
read. Preserve its selector validity, value, order and original body exception.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap, general powers, varying table
keys and legacy SCF retention remain unfinished.
