# Inherited own-field snapshots and complement bands — 2026-09-20 UTC

Continued clean `07af396c` at the next boundary in HANDOFF/plan 00. Required
commit histories and unmerged branches were reviewed; no dirty interrupted work
remained at initial start, and September 7 WIP was absent from the unmerged list.
Service interruptions at 04:16:32, 04:23:12 and 04:30:00 resumed the same drafts.
Agents supplied source fixtures, the independent escape extension and proof review;
root reconciled, gated and committed the two concerns separately.

Linux executable/argv checks found no Claude identities among 67 processes,
but 56 denied executable reads prevented confirming availability. Windows
Get-CimInstance succeeded (367 processes), with no Claude identities. Concurrent
restrictions remained. No browser/runtime edits, push or new WPT/test262 measurement.

## Changes

- `170eb7e6`: bounded varying BitNot indices may occupy one negative-wrapped,
  signed or positive-wrapped ToInt32 band. Exact endpoints in the same band
  preserve the affine range and positive stride through the existing Number
  complement proof. Crossing discontinuities, out-of-range intermediates,
  reload overlap, saved aliases, cycles and work/depth limits remain guarded.
  Four CFG and two SCF positives include exact u32 magnitudes; boundary refusals
  remain. Original source functions 12/13 are unchanged and now prove confined.
- `117783b6`: fixed inherited own-field snapshots reuse explicit-super
  normalization before inspecting the complete constructor. Shared base-method
  snapshots impose the same ordered field set on every descendant, including
  empty sets and intermediate classes without snapshots. Every own/ancestor
  method and external instance write must preserve that set, including hidden
  ancestor definitions. A leaf-only snapshot may include fields added after super
  when its base has no snapshot requirement. Reassignments retain insertion order.

The original source functions and effects survive. Strict normalized-constructor
checks still reject calls that could observe partially initialized fields,
conditional presence, replacement returns and dynamic/numeric field names.
Early super normalization retains the existing root/capture checks and runs only
once. Snapshot scalar replacements preserve cached fixed-cell producers. Any
later descendant mismatch rejects the entire private candidate; no partial
rewrite is published. Six native positives and ten refusal sources were added;
original sections 01–10 and authentic Bootstrap generation remain unchanged.

## Focused validation

All devbox commands held `/tmp/ctbrowser-devbox-build.lock`. Exact scripts/logs,
source hashes and generated files are in `/tmp/ctcompile-inherited-fields-0420/`.

- Built `ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference` and separately
  `ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle` through explicit
  `tools/remote-build.sh` targets.
- Exact `ctcompile_escape_analysis_arrays`: 1/1, 1.35s (1.36s total).
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(bitnot|unary|composed)-index-overwrite[.]test$`:
  3/3, 0.13s. BitNot: 60 sites / 11 sound / 11 of 14 confined precision;
  unary: 63 / ten / 10 of 14; composed: 54 / seven / 7 of 10. All have
  zero violations, partial, pending or unclaimed sites. Five tested hashes match.
- Exact `ctcompile_host_contract`: 1/1, 0.48s (0.49s total).
- Narrow inherited probe: 17 observations / 48 native executions / 34 unprepared
  and 13 preparation refusals, including the existing collision source. Exact
  complete shared-field budget 2,604 and rooted-super refusal passed. Ancillary
  controls: 16 constructed-method executions / 20 refusals and eight original-r
  executions / four refusals. Ten actual source refusals were measured and pinned
  before the final gate.
- Final class initialization/DOM lit filter
  `^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$`:
  2/2, 425.99s. Class: 292 observations / 812 main native executions /
  584 unprepared and 311 preparation refusals. DOM: 632 observations / eight
  combined native executions / 4,922 refusals. Ancillary class controls: 16
  constructed-method executions / 20 refusals, eight original-r executions /
  four refusals, four prototype-key executions / six refusals and 15 prepared
  refusals. Complete shared-field budget remains 2,604. Six native source hashes
  match; all eleven recorded native/escape hashes match.
- Inspected generated shared, leaf-only, clearing and empty-shape C++:
  stack-owned records, borrowed receiver pointers, direct fields and shared method
  calls; empty receivers disappear. Existing nullable scalars represent primitive
  values. No snapshot container, prototype storage, closure environment, Script/VM
  symbol, collector or handle table is emitted.
- Required `tools/format.sh --check`: same 20 diagnostics in six HEAD-identical
  files (ctdrive, PrefixAnalysis, ProviderCallbacks, ProviderPaths, PartialEvaluation
  Heap, Symbolic Facts). Changed C++/Python formatting, Python AST, Node source
  checks, gate-script syntax and diff whitespace pass. The tests agent initially
  tried unavailable `python3 -m black`; the installed Black command passed.

No native or escape focused gate failed. The Bootstrap diagnostic advanced;
its authentic source was preserved. Generic inherited IR uses the existing
root/budget controls rather than the custom-assembly-only fixture mutations.
The independent native review found no new soundness/lifetime blocker.

Skipped: full CTest/compiler lit, broad native/corpus matrices, full Bootstrap,
WPT/test262, Windows and sanitizers. These are focused passes only.

## Exact next boundary

The original W/B specimen now refuses `super condition is not a proved Boolean`
while normalizing B's `(t = a(t)) && (...)` constructor. Its `_element` and
`_config` fields exist only on that branch. Prove conditional constructor
control flow and per-instance field presence without granting authority from the
null-input observation. The preserved exact own-key for-of clearing source still
refuses snapshot consumption. Retain dispose's `e.remove` and `P.off` effects.

Distinct inherited receiver field sets require a per-receiver snapshot proof;
this slice conservatively requires equality. Implicit derived rest/apply also
remains refused. Static `this.getInstance`/`new this`, inherited DOM/getter proof,
H/config/selectors/events/Popper, broader ownership/own-data/control-flow proof,
and the application driver remain unfinished. This is not full Bootstrap execution.
