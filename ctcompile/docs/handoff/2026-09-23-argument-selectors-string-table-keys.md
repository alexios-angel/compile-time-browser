# Cleanup argument selectors and String table keys, 2026-09-23

Resumed clean **7d714efb** and retained `b3cd2859` from HANDOFF, Current native
work, iteration 117's journal and selector-final-argument-values detail. No dirty
drafts or unmerged `codex-wip-20260907` remained. Unrelated branches were preserved.
Three agents handled source checks, raw checks/review and escape work. The user
interruption paused their work; resumption reused saved candidates, baseline and
completed gates. The source agent's final rate limit left a frozen correction
and resume harness, which the parent gated and committed.

**4db46d60** removes the `finalMethod` refusal when a completed selector joins
the existing suffix-read census. An ignored argument selector keeps exact
suppression; a consumed selector enters existing literal validation. All uses,
source guard/order, charged scanning/cloning, typed Element/Style reproof and
original saved primitive exception remain. No runtime or ownership seam changed.
Independent review found no actionable findings; the parent reviewed the final
regressions and the correction below.

Original SHA-256
`b3cd28592431e3d5645c8d493a21abe1e1d87e9f91e1de5034eeb60777f9ed13`
executes unchanged. Getter `4a96134c` and order variant `21ca2178` execute too;
the latter distinguishes an earlier false saved result from the later true
ignored selector. Raw checks add three admissions, four typed refusals, ten
structural refusals and two expansion-budget rows. Two exact historical inputs
that feed writes directly from selectors move to admission checks. All 28 raw
MLIR literal bodies remain unchanged. Source preservation retains 233 historical
bodies, 86 metadata rows, 102 prior saved cases, 230 retained refusal bodies and
102 oracle constructions. Local checks cover 13 source syntax cases and 12 Node
observations. Only the retained original refusal moves into execution coverage.

**cea41194** gives `LoopProof::indexRange` an explicit property-key context.
For a String selected from a distinct invariant table, existing `ownArrayIndex`
proves a canonical own position. That temporary positional fact does not enter
array/value state. Unary/binary recursion keeps Number-only operands, so String
addition cannot masquerade as arithmetic. Nested table lookup keys use the same
property boundary. Complete mutation checks, every receiver reload's independent
write-gap proof, bounded subdivision and actual-write replay remain unchanged.

All ten baseline child sites were `escapes:stored` (program `a67000ed07604b53`).
The final focused lit source reports four confined and six stored: String keys,
mixed Number/String keys, an untouched reloaded table and nested String tables
are confined; retained/saved/noncanonical/concatenated/mutated/overlapping cases
stay stored. Seven raw admissions preserve exact contents and read identities;
22 refusals cover coercion, spelling, arithmetic, mutation and reload overlap.
Ten Node witnesses compare original/instrumented outcomes, ordered reads/writes,
own keys and original child identity. Historical raw tests and prior source
function bodies remain unchanged. The parent reviewed the complete key context,
mutation census, reload proof and replay before committing.

Focused validation and corrections:

- Source preflight: **26 PASS**, three admissions and ten refusals under both
  optimization policies, including the unchanged original and next boundary.
- Selected native execution: **48 executions, 76 refusals, 24 distinct Node/VM
  observations PASS** across original/getter/order cases. The first run completed
  the original/getter cases (32 executions, 24 refusal checks) and all three
  cases' Node/VM observations, then failed a new generated-order assertion before
  executing the third case. It used the last selector, which is now the ignored
  argument selector. The correction steps back to the saved selector only for
  the new case. The resume selects just that case plus ten refusals and passes
  **16 executions, 52 refusals, eight repeated Node/VM observations**. No completed
  native binary execution was replayed. Production was unchanged.
- Final exact `ctcompile_host_contract`: **1/1 PASS, 2.54 s / 2.55 s total**.
  The first run failed one historical final-read selector refusal, now supported
  by the same proof. Its exact input was preserved and promoted with method,
  single-use, result identity and order assertions. Another historical selector
  admission was anticipated before the first gate. No production correction.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 3.00 s / 3.01 s total**.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`:
  **1/1 PASS, 0.12 s, 356 excluded**. New child verdicts: four confined/six stored.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting, scratch shell syntax, source preservation and diff checks pass.
- Six final code/test hashes match the devbox. All **24 generated C++ files**
  contain no `ctbrowser::script`; selected fixture checks also compile standalone
  and linked native output. Native source executions used the unchanged native
  production before the independent escape change was linked into its test targets.

Builds used `tools/remote-build.sh` under `/tmp/ctbrowser-devbox-build.lock` with
explicit targets `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
`ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`. CTests selected
exact anchored names with `--output-on-failure --no-tests=error`; lit used the
generated build configuration and the named case. Selected source runs used
`/tmp/ctcompile-tests118/{preflight,native-gate,native-resume}.sh`; the resume AST
filter retains only the unfinished case and new refusals. Commands, logs,
sources, generated C++, hashes, reviews and witnesses are preserved in
`../test-results/2026-09-23-argument-selectors-string-table-keys/` relative to
the repo, with SHA-256 checksums.

Initial process checks inspected 20 accessible Linux and 347 Windows processes;
final checks inspected 16 Linux and 347 Windows processes. No actual Claude
executable/CLI/loop matched, but 61 Linux executable-link permission errors kept
status uncertain. Concurrent-area rules applied. No browser/shared implementation
write, push, history rewrite or local C++ build occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-suite or full-Bootstrap coverage gain is claimed.

**Next native boundary:** retained `unsupported-selector-inside-first-write-argument`,
SHA-256 `95420a6711a678b7b1102a97762be62cb6678e08472558e7f353e6895aa9dac4`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. It feeds the first protected `setAttribute` from `matches`, before the
currently required initial write/read sequence. Preserve selector validity,
source order, guard, saved body exception and complete proof obligations.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap, general powers and legacy SCF
retention remain unfinished.
