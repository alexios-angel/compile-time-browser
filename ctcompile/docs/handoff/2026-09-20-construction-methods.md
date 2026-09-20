# Construction-point methods and signed left shifts — 2026-09-20 UTC

Continued clean `a618c083` and its recorded construction-point receiver boundary.
The September 7 WIP was absent; no dirty interrupted draft remained. Native
fixtures, source review and escape analysis ran in parallel. Service interruptions
stopped the agents after their drafts; root preserved and completed that work.

Linux executable/argv checks found no Claude identity among 16 readable
processes, with 57 unread identities. Windows Get-CimInstance checked 342
processes without a Claude identity. Availability remained uncertain;
concurrent restrictions applied. No browser/runtime/shared-file edits or push.

## Landed

- `e6cfaede`: constructor calls inspect the selected method's receiver uses
  against fields already present at that exact call. Reads and updates of
  existing fields and nested same-receiver calls retain their original bodies
  and arguments. The nearest inherited definition is checked for each leaf.
  New fields, missing fields, receiver escape and snapshots refuse; recursion
  and work remain bounded. Complete capture/source/receiver checks still run.
  Snapshot-method identity survives folding and lexical `super` expansion, so
  early inherited calls cannot use the eventual shape. Equal-final-shape
  regressions cover both direct and super-expanded inherited snapshots.
- `306af499`: left-shift range endpoints may be negative or cross zero when
  multiplying by the masked power of two stays within signed-i32 bounds.
  The asymmetric bounds include `INT32_MIN`; wrapped inputs/outputs still
  refuse. Existing composition, stride, reload, alias and work guards remain.
  Five CFG and three SCF positives plus underflow/wrap/reload controls use the
  existing budget sweeps. Original 22 source bodies are unchanged; one previous
  refusal is promoted and eight source cases are added.

Six new native positives add **48 executions**: existing-field reads/updates,
primitive-only methods, nested calls, nearest overrides and argument order.
Thirteen new controls cover early/missing/new fields, receiver escape, recursive
calls, dead ambient effects and inherited snapshot provenance. Original class
fixtures 01–16 and Bootstrap source bodies are unchanged.

The same-named local helper/method alias control measures **Node 7711 / VM
call-stack exhaustion**. Its source and both observations are preserved, and a
distinct-helper companion measures **7711** in both oracles. Both refuse native
preparation. The runtime was not changed to match either result.

Inspected nested/inherited C++ uses stack records, borrowed receiver pointers,
direct method calls and field stores. It contains no Script/VM symbol, collector,
closure environment, callable-holder table or snapshot iterator container.

## Focused validation

All devbox commands held `/tmp/ctbrowser-devbox-build.lock`. Exact commands,
logs, source hashes and generated samples: `/tmp/ctcompile-construction-methods-0714/`.

- Built explicit targets `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle` through `tools/remote-build.sh`.
- Exact `ctcompile_host_contract`: **1/1**, 0.48s (0.49s total).
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.39s (1.40s total).
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(right-shift|composed|left-shift)-index-overwrite[.]test$`:
  **3/3**, 0.13s. Existing recording/claims report **90 observed sites / 15 sound /
  15 of 21 confined precision**, zero violations, partial, pending or unclaimed
  sites. Reporting reused those artifacts without another replay.
- Final class selection: 19 new method cases, 13 previous constructor-helper
  cases, all 18 `OWN_FIELDS` cases, six inherited/conditional/snapshot controls
  and original Bootstrap B/Data+B. **58 observations / 160 main executions /
  116 unprepared refusals / 62 preparation refusals**. GCC/Clang,
  explicit/deduced output and both optimization settings are covered.
  New complete proof budgets: field read **1,622**, nested calls **1,864**,
  nearest override **5,447**. Existing budgets: captured helper **1,634**,
  nested helper **1,861**, distinct holders **4,123**, inherited fields **2,702**,
  ordered fields **763**, clearing loop **1,333**. Rooted super, missing
  identities, forged inputs and partial-budget controls pass. Ancillary checks:
  **16 constructed-method executions / 20 refusals**, **eight original-r
  executions / four refusals**.
- All six native and four escape tested source hashes match. Node checked new
  source observations; the escape agent checked all 30 source functions for
  syntax, termination and independent retention observations. Original source
  preservation, changed C++ formatting, Python AST/Black and `git diff --check`
  pass.
- Required `tools/format.sh --check` retains **20 baseline diagnostics in six
  HEAD-identical files**. This is a changed-file formatting pass, not a clean
  repository-wide formatting result.

The first native selection passed the six positives before exposing the
same-name alias-control VM exception. The source was retained, its VM expectation
recorded and a distinct-helper companion added. The final 58-source selection
passed. No first-attempt or whole-class pass is claimed.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
This extension is restricted to the closed-source own-field census.

## Next boundary

Both original Bootstrap B and Data+B now refuse `class construction method
requires an existing own field`. `_element` exists before `_getConfig`, but
inherited `_mergeConfigObj` and `_typeCheckConfig` read `this.constructor` for
`Default`, `DefaultType` and `NAME`. That identity comes from the actual
most-derived prototype/constructor; treating it as an own field is wrong.
Extend the existing receiver/getter proof at the construction point, preserving
each original getter/body and its effects.

Variable own-field presence across B's guarded early exit, complete config
effects, shared nested Map ownership, stored-receiver lifetime and registration
remain unproved. Keep `e.set`, `e.remove`, `P.off` and the original clearing loop.
Inherited DOM, static construction, config/selectors/events/Popper, full Bootstrap
and the application driver remain unfinished. Escape analysis still refuses
left-shift wrapping bands and unproved reload overlap.
