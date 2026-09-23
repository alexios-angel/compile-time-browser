# Standalone cleanup selectors and binary table conversions, 2026-09-23

Resumed clean **1038e634** and retained source `c1ea55cf` from HANDOFF,
Current native work, iteration 119's final journal and its detailed handoff.
No dirty drafts or unmerged `codex-wip-20260907` remained. Unrelated branches
were preserved. Three agents handled source checks/review, raw checks and escape
work. Raw and escape agents hit rate limits; the parent finished the raw draft
and measured the escape baseline, then the escape agent resumed from those
artifacts. Completed source checks and the escape baseline were not repeated.

**8232f054** extends existing feeding-read selection to standalone reads before
initial cleanup write lookups. Initial `matches` calls all join literal validation,
including ignored selectors. Read membership, complete use census, source guard
and order, charged budgets and private typed DOM/Style reproof remain. An
unfinished pending method cannot be replaced by an earlier read. Original body
exceptions still win. No runtime or ownership interface changed.

The original SHA-256
`c1ea55cfbdf17421a3fecb1c03661042fdfeb5c4cea638d067ba25967112cd6e`
executes unchanged; getter `5ff068f0` and order variant `d22732ae` execute too.
Raw checks add five admissions, four typed refusals, 13 structural controls and
four budget rows. All 28 historical raw MLIR literal bodies remain unchanged.
Source preservation retains 233 historical bodies, 86 metadata rows, 108 saved
cases, 248 retained refusal bodies and 108 oracle constructions. Local source
checks cover 13 syntax cases and 12 Node observations.

The first host run found two new-layout order assumptions and one genuine
structural regression: selecting an earlier read could replace a pending method
with no completed call. The second host run isolated that exact historical
refusal. The final shared guard restores its refusal, without changing or
promoting the input. The assertion fixes retain dedicated selector identity,
order/guard and typed refusal coverage. Initial independent review missed this
structural gap; its follow-up review records the miss and finds no remaining
issues after the guard.

**58cce74f** requests the existing bounded primitive conversion for
numeric binary operands selected from invariant, distinct tables. All four
recursive operand sites retain Number proof for Add. Earlier String addition
and raw property spelling cannot borrow conversion from an outer operation.
Original stored primitives and reads remain intact. Mutation checks, receiver
reload exclusions, exact arithmetic bounds, singleton subdivision, shared
budgets and ordinary actual-write replay remain unchanged.

All ten baseline child sites were stored (program `3176b51caf120996`). The final
selected source (program `060a51c1409d8039`) reports five confined and five stored among those same bodies.
Subtraction, multiplication, division, shift and bounded power identities become
confined. Saved children, earlier String concatenation, noncanonical spelling,
mutation and overlapping receiver reloads remain stored. The noncanonical `'00'`
multiplication witness clears its child in Node but remains conservatively
unproved. There are 23 raw contents admissions and 27 refusals. Ten Node witnesses
check ordered reads/writes, table values, own keys and original child identity.
The historical raw body, 32 prior source functions/CHECKs and ten baseline source
bodies are unchanged. The parent reviewed both CFG/SCF callers, conversion context,
complete mutation census and actual replay.

Focused validation:

- Source preflight: **26 PASS**, three admissions and ten refusals under both
  optimization policies, including the unchanged original and next boundary.
- Final selected native execution: **48 executions, 76 refusals, 24 Node/VM
  observations PASS** across original/getter/order cases. The initial run also
  passed; the final run repeated these checks after the production guard.
- Final exact `ctcompile_host_contract`: **1/1 PASS, 2.53 s / 2.54 s total**,
  after the two failed runs described above.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.83 s / 2.84 s total**.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`:
  **1/1 PASS, 0.11 s, 356 excluded**; five new confined/five stored child verdicts.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting, scratch shell syntax, source preservation and diff checks pass.
- Six final code/test hashes match the devbox. All **24 generated C++ files**
  contain no `ctbrowser::script`; the selected fixture also checks standalone and
  linked native output. No native replay followed the independent escape change.

Builds used `tools/remote-build.sh` with explicit targets `ctjs-opt`,
`ctjs-translate`, `ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All devbox operations held
`/tmp/ctbrowser-devbox-build.lock`. CTests used exact anchored names with
`--output-on-failure --no-tests=error`; lit used the generated configuration and
one named case. An initial escape baseline invocation printed usage because it
omitted `--script`/`--out`; the corrected invocation measured the baseline before
escape production edits. Native scripts are
`/tmp/ctcompile-tests120/{preflight,native-gate}.sh`; escape commands are in
`/tmp/ctcompile-escape120/gate.sh`. Commands, logs, sources, generated C++, hashes,
reviews and checksums are preserved in
`../test-results/2026-09-23-standalone-selectors-binary-table-conversions/` relative
to the repo.

Initial process checks inspected 15 accessible Linux and 342 Windows processes;
final checks inspected 14 Linux and 344 Windows processes. No actual Claude
executable/CLI/loop matched, but 60 initial/61 final Linux executable-link errors
kept status uncertain. Concurrent-area rules applied. No browser/shared
implementation write, push, history rewrite or local C++ build occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-suite or full-Bootstrap coverage gain is claimed.

**Next native boundary:** retained `unsupported-selector-reused-by-second-write`,
SHA-256 `8f5b1ef8950d3a15e5f4a607df05523728293f3859585fb0f8b969e09f0ce8da`, refuses **DOM protected helper needs an independent
inert-body proof** under both policies. Preserve the first standalone selector
snapshot when it feeds the second protected write, both write positions and the
original body exception. Broader single-write cleanup, nonterminal exceptional
state, multiple protected regions, implicit cleanup, nested custom iterators,
unguarded Bootstrap defaults, the application driver, full native Bootstrap,
general powers and legacy SCF retention remain unfinished.
