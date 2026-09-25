# Constructor helpers and uneven shift footprints — 2026-09-20 UTC

Continued clean `6149f1eb` and its recorded constructor-field census boundary.
The September 7 WIP was absent; no interrupted dirty draft remained. Native
fixtures, proof investigation and escape work ran independently. The escape
agent hit a service rate limit; root preserved and completed its exact draft.

Linux executable/argv checks found no Claude identity among 19 readable
processes, with 56 unread executable links. Windows Get-CimInstance checked
348 processes without a Claude identity. Availability remained uncertain;
concurrent restrictions applied. No browser/runtime/shared-file edits or push.

## Landed

- `e88230b1`: the constructor field census retains helper loads, selections and
  calls when none of their operands resolves to the construction receiver.
  The existing complete capture, callable, receiver and source-body proofs still
  run before publication. Reads/calls involving `this` refuse because a method
  might observe partially initialized fields. All helpers remain ordinary calls;
  no new output runtime or ownership mechanism was introduced.
- `1f9b600f`: nondivisible right-shift induction strides use a dense integer
  enclosure between proved endpoints, within the existing conversion band.
  Divisible strides retain their tighter lattice. These ranges only prove own
  bounds and exclude overlapping reloads; exact replay performs actual writes.
  Reversed/scaled compositions, retained gap children, fractional intermediates
  and conservative gap-reload refusals have CFG/SCF and source checks. The
  original 46 source bodies remain; three expected refusals were promoted and
  eight new source cases appended.

Five native positives cover captured and nested helper values, local holders,
distinct inherited capture identities and argument order, adding **40 native
executions**. Eight new controls retain receiver-escape, early method call,
SCF-selected receiver, unused/dead ambient effect, holder replacement, holder
receiver dependence and variable-field refusals. Original class fixtures 01–15
and all original Bootstrap bodies are unchanged.

Inspected nested/inherited C++ contains stack records, borrowed receiver pointers,
direct helper calls and field assignments. It has no Script/VM symbol, collector,
closure environment, callable-holder table or runtime snapshot iterator.

## Focused validation

All devbox commands held `/tmp/ctbrowser-devbox-build.lock`. Commands, logs,
source hashes and generated samples: `/tmp/ctcompile-own-field-helpers-0647/`.

- Built explicit targets `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle`.
- Exact `ctcompile_host_contract`: **1/1**, 0.48s (0.49s total).
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.40s (1.41s total).
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(right-shift|composed|left-shift)-index-overwrite[.]test$`:
  **3/3**, 0.14s. Existing right-shift recording and claims report **162 observed
  sites / 34 sound / 34 of 40 confined precision**, zero violations, partial,
  pending or unclaimed sites. Reporting reused those artifacts, with no replay.
- Final class selection: all 18 `OWN_FIELDS` sources, 13 new
  `inherited-own-fields-iterate-helper-*` cases and original Bootstrap B/Data+B.
  **33 observations / 88 main native executions / 66 unprepared refusals / 40
  preparation refusals**. GCC/Clang, explicit/deduced output and both optimization
  settings are covered. Complete proof budgets: captured helper **1,634**, nested
  helper **1,861**, inherited distinct holders **4,123**; existing ordered fields
  **763**, clearing loop **1,333**. Rooted super, missing identities, forged inputs
  and budget refusals pass. Ancillary checks: **16 constructed-method executions /
  20 refusals**, **eight original-r executions / four refusals**.
- All four native and four escape tested source hashes match. Node checked all
  13 new native sources and eight new escape retention observations. Original
  source preservation, changed C++ formatting, Python AST/Black and
  `git diff --check` pass.
- Required `tools/format.sh --check` retains **20 baseline diagnostics in six
  HEAD-identical files**. This is a changed-file formatting pass, not a clean
  repository-wide formatting result.

The initial native probe exposed a custom-assembly-only malformed-input test
helper applied to generic MLIR; the new cases now use the existing generic-safe
budget check. The next selection passed helper/inherited own-field groups before
encountering Bootstrap's stale expected diagnostic. That assertion was corrected
without a compiler/source change; the final 33-source selection checked the new
family and remaining suffix. No whole class-lit or 73-source pass is claimed.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
The native extension is restricted to the closed-source own-field census.

## Next boundary

Both original Bootstrap B and Data+B now refuse `class own-key snapshot
constructor observes its receiver`. B's constructor calls `a(t)`, then stores
`_element`, reads/calls `this._getConfig(i)`, and later registers the instance
through `e.set`. The first blocked receiver read is `_getConfig`; proving
receiver-independent helpers does not prove that method's complete effects.

Next prove instance methods at their actual construction point: what fields
exist, which fields they read/write, and whether they snapshot or publish the
receiver. Never substitute the eventual complete field set for an early snapshot.
Variable presence, complete config effects, shared nested Map ownership,
stored-receiver lifetime and duplicate registration remain separate obligations.
Retain `e.remove`, `P.off` and the original clearing loop throughout. Inherited
DOM/getters, static construction, config/selectors/events/Popper, full Bootstrap
and the application driver remain unfinished. Escape analysis still conservatively
rejects cross-band shifts and reloads in unproved gaps of uneven footprints.
