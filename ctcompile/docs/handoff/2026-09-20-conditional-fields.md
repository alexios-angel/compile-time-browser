# Conditional fields and right-shift indices — 2026-09-20 UTC

Continued clean `830f3e0c` from HANDOFF/plan 00. The required commit histories
and unmerged branches were reviewed; no dirty interrupted work remained at initial
start, and September 7 WIP was absent from the unmerged list. Service interruption
at 04:52 resumed the same right-shift draft and native investigation. Agents
supplied source fixtures, the independent escape extension and proof review;
root reconciled, gated and committed the two concerns separately.

Linux executable/argv checks found no Claude identities among 72 processes,
but 56 denied executable reads prevented confirming availability. Windows
Get-CimInstance succeeded (347 processes), with no Claude identities. Concurrent
restrictions remained. No browser/runtime edits, push or new WPT/test262 measurement.

## Changes

- `4a5ef7b2`: bounded right-shift own-index overwrites reuse the existing exact
  Number conversion/bitwise and reload proofs. Nonnegative input endpoints stay
  within signed i32 for `>>` or u32 for `>>>`; an invariant numeric count uses
  ECMAScript modulo 32. The input stride must divide evenly by the power of two.
  Unaligned starts remain exact. Negative inputs, nondivisible strides, conversion
  discontinuities, overlapping count reloads and out-of-bounds writes remain refused.
- `e38407aa`: preserve structured runtime branches after explicit super
  initialization. Only the exact private Boolean guard permits arm elimination;
  source conditions retain both arms, including literal-true dead ambient controls.
  Equal completion flags/receivers bypass runtime selection without discarding
  branch effects. Cloned base subtrees retain their original captured helper
  identities, with work charged before deep cloning.

Own-field snapshots now join ordered field sets across structured constructor
branches. Each arm must produce the identical order, retaining insertion order
on overwrites. Nested branches and inherited constructors compose; method/external
writes still cannot add fields. Partial-construction observations, variable field
presence, different insertion orders, loops and early completion remain refused.
The constructor census admits only the needed structured and scalar transport;
final native lowering still proves representation. Seven native positives and
seven refusal sources were added; the original `constructor-branch` source was
also promoted from refusal to native execution. Original parts 01–11 and Bootstrap
generation remain unchanged. Runtime branches and helper effects survive; rooted
super bodies remain refused.

## Focused validation

All devbox commands held `/tmp/ctbrowser-devbox-build.lock`. Exact scripts/logs,
source hashes and generated files are in `/tmp/ctcompile-conditional-fields-0454/`.

- Built `ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference` and separately `ctjs-opt ctjs-translate
  ctcompile-test-escape-analysis-arrays ctcompile-test-escape-claims
  ctcompile-test-type-oracle` through explicit `tools/remote-build.sh` targets.
- Exact `ctcompile_escape_analysis_arrays`: 1/1, 1.37s.
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(right-shift|bitnot|composed)-index-overwrite[.]test$`:
  3/3, 0.13s. Right shift: 63 sites / ten sound / 10 of 15 confined precision;
  BitNot: 60 / eleven / 11 of 14; composed: 54 / seven / 7 of 10. Zero violations,
  partial, pending or unclaimed sites. Six tested hashes match.
- Exact `ctcompile_host_contract`: 1/1, 0.48s (0.49s total).
- Narrow conditional probe: 17 observations / 56 native executions / 34 unprepared
  and 14 preparation refusals, including two existing method-branch controls and
  original Bootstrap. Exact complete budgets: 4,049 for nested inherited argument
  effects and 1,885 for nested inherited captures; rooted-super refusals passed.
  Ancillary controls: 16 constructed-method executions / 20 refusals and eight
  original-r executions / four refusals.
- Final selected class-source probes: conditional group 17 observations / 56 native
  executions (above); OWN_FIELDS plus preserved `constructor-branch`:
  19 observations / 48 native executions / 38 unprepared and 18 preparation
  refusals, with exact own-field budget 761. Together the probes exercise 36 sources
  and 104 main native executions, including 64 new executions (seven additions and one
  promotion). These are focused probe results, not a full class-lit pass.
- The complete DOM lit case passed: 632 observations / eight combined native
  executions / 4,922 refusals. The initial class/DOM invocation lasted 295.20s
  and reported one pass and one failed diagnostic assertion.
- Inspected generated same-order, inherited-argument and nested-helper C++:
  stack records, borrowed receiver pointers, direct fields, ordinary branches and
  direct helper calls. Existing nullable primitive scalars remain; there is no
  snapshot container, closure environment, prototype store, collector or Script/VM
  symbol. Six native hashes match; all twelve native/escape hashes match.
- Required `tools/format.sh --check`: same 20 diagnostics in six HEAD-identical
  files (ctdrive, PrefixAnalysis, ProviderCallbacks, ProviderPaths, PartialEvaluation
  Heap, Symbolic Facts). Changed C++/Python formatting, Python AST, Node source
  checks, gate-script syntax and diff whitespace pass.

The initial native build caught an MLIR clone API argument mismatch; corrected.
The first narrow probe refused unused poison completion transport in the new
nested constructor; the narrow transport allowance was corrected. A subsequent
probe passed all seven new native sources but failed the stale Bootstrap diagnostic
assertion; its source was preserved and the measured diagnostic was pinned.
The initial combined class/DOM gate failed one preserved conditional-field diagnostic
assertion; its expectation was corrected without changing source. DOM passed in
that run (295.20s total). The next class run (283.22s) found the original
`constructor-branch` source now admitted; it was preserved and promoted. Only
class initialization was rerun after these expectation changes. That run
(422.71s) passed the promotion and earlier groups, then reached a stale
`own-fields-conditional` diagnostic in the final OWN_FIELDS group. Its source was
preserved and its expectation corrected. The final focused probe covered all
18 OWN_FIELDS sources and the promoted constructor case, completing the remaining
suffix without another whole-class replay. No full class-lit pass is claimed for
this session; all failures and corrected focused checks remain in the logs.

Skipped: full CTest/compiler lit, broad native/corpus matrices, full Bootstrap,
WPT/test262, Windows and sanitizers. These are focused passes only.

## Exact next boundary

The original W/B specimen now refuses `super initialization contains an unproved
call`, past its previous conditional Boolean boundary. Source inspection identifies
B's `e.set(this._element, this.constructor.DATA_KEY, this)` registration as the
next constructor effect boundary. Prove the authentic Data/config effects and
receiver-selected getters; the missing-element observation cannot authorize them.
Variable per-instance field presence remains refused, as does the preserved
original own-key for-of clearing loop. Retain dispose's `e.remove` and `P.off`.

Distinct inherited receiver field sets, implicit derived rest/apply, static
`this.getInstance`/`new this`, inherited DOM/getter proof, H/config/selectors/events/
Popper, broader ownership/own-data/control-flow proof and the application driver
remain unfinished. This is not full Bootstrap execution.
