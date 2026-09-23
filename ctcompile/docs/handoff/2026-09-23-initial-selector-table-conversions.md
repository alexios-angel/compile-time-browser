# Initial cleanup selectors and explicit table conversions, 2026-09-23

Resumed clean **2889faa9** and retained source `95420a67` from HANDOFF,
Current native work, iteration 118's final journal and its detailed handoff.
No dirty drafts or unmerged `codex-wip-20260907` remained. Unrelated branches
were preserved. Three agents handled source checks, raw checks/review and escape
work. Repeated user interruptions resumed their saved candidates and completed
measurements. The compiler build and 26 source preflight checks were reused;
interrupted native/host launches had not completed tests. Final commands retained
their logs and exit statuses in separate process sessions under the build lock.

**68e4614f** accepts `matches` inside either initial protected cleanup write's
arguments through the existing ordered read slots. Every initial argument selector
joins the existing literal validation before moving outside helper suppression,
including saved or ignored selectors. The dedicated later selector, feeding-read
selection, complete use census, source guard/order, charged budgets and private
typed DOM/Style reproof remain. Original body exceptions still win. No runtime
or ownership interface changed. Independent native review found no issues.

Original SHA-256
`95420a6711a678b7b1102a97762be62cb6678e08472558e7f353e6895aa9dac4`
executes unchanged. Getter `42824993` and order variant `44aac488` execute too;
the latter preserves an earlier false result while the final ignored selector
observes true. Raw checks add four admissions, four typed refusals, 13 structural
controls and three expansion-budget rows. All 28 historical raw literal bodies
remain unchanged. Source preservation retains 233 historical bodies, 86 metadata
rows, 105 saved cases, 239 retained refusal bodies and 105 oracle constructions.
Local source checks cover 13 syntax cases and 12 Node observations.

**9fee9698** distinguishes Number, property-key and explicit-conversion uses in
the existing counted-loop range proof. Unary `+`, `-` and `~` can convert an exact
primitive selected from an invariant, distinct table using `boundedConvertedNumber`.
Conversion does not propagate into earlier String addition or change the original
property spelling. Complete mutation checks, independent receiver-reload gaps,
bounded subdivision and actual-write replay remain unchanged.

All 12 baseline child sites were stored (program `6601254dd30ebde5`). The final
selected lit source, program `f303a3f028935fc8`, reports **five confined and seven
stored** among those same bodies: unary String conversions, an untouched reloaded
table and mixed null/String inputs become confined. Retained/saved children,
noncanonical spellings, concatenation, mutation, overlapping reloads and unconverted
signed keys stay stored. `+'00'` remains unproved despite its Node witness clearing
the child; the existing bounded conversion grammar is unchanged. Ten raw admission
rows retain exact contents/identities; 22 refusals cover primitive, arithmetic,
mutation and reload limits. Twelve Node witnesses compare original/instrumented
results, ordered reads/writes, table primitives, own keys and original child identity.
The historical raw body, 20 prior source bodies/CHECKs and all 12 baseline bodies
remain unchanged. The parent reviewed the conversion context and existing replay.

Focused validation:

- Source preflight: **26 PASS**, three admissions and ten refusals under both
  optimization policies, including the unchanged original and next boundary.
- Selected native execution: **48 executions, 76 refusals, 24 Node/VM observations
  PASS** across original/getter/order cases. No native execution correction or replay.
- Final exact `ctcompile_host_contract`: **1/1 PASS, 2.56 s / 2.57 s total**.
  The first run failed two uses of an existing assertion that mistook the new
  initial selector for the later intervening selector. The assertion now excludes
  that exact initial selector; dedicated identity/order/guard checks still cover it.
  No fixture input or production change. Only the host test was rerun.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.79 s / 2.80 s total**.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`:
  **1/1 PASS, 0.11 s, 356 excluded**. Five new confined/seven stored child verdicts.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting, scratch shell syntax, source preservation and diff checks pass.
- Six final code/test hashes match the devbox. All **24 generated C++ files**
  contain no `ctbrowser::script`; the selected fixture also checks standalone and
  linked native output. Native execution preceded linking the independent escape
  change into the compiler; its production/source were unchanged afterward.

Builds used `tools/remote-build.sh` with explicit targets `ctjs-opt`,
`ctjs-translate`, `ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All devbox operations held
`/tmp/ctbrowser-devbox-build.lock`. CTests used exact anchored names with
`--output-on-failure --no-tests=error`; lit used the generated build configuration
and the single named case. The selected source scripts live in
`/tmp/ctcompile-tests119/{preflight,native-gate}.sh`. Commands, logs, source witnesses,
generated C++, hashes, reviews and checksums are preserved in
`../test-results/2026-09-23-initial-selector-table-conversions/` relative to the repo.

Initial process checks inspected 14 accessible Linux and 348 Windows processes;
final checks inspected 20 Linux and 354 Windows processes. No actual Claude
executable/CLI/loop matched, but 60 initial/61 final Linux executable-link permission
errors kept status uncertain. Concurrent-area rules applied. No browser/shared
implementation write, push, history rewrite or local C++ build occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-suite or full-Bootstrap coverage gain is claimed.

**Next native boundary:** retained `unsupported-selector-before-first-write`,
SHA-256 `c1ea55cfbdf17421a3fecb1c03661042fdfeb5c4cea638d067ba25967112cd6e`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Preserve the saved `matches` result evaluated before the first write's
method lookup, selector validity, source order/guard and original body exception.
Broader single-write selector cleanup, nonterminal exceptional state, multiple
protected regions, implicit cleanup, nested custom iterators, unguarded Bootstrap
defaults, the application driver, full native Bootstrap, general powers and legacy
SCF retention remain unfinished.
