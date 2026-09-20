# Captured local holders and unsigned-shift bands — 2026-09-20 UTC

Continued clean `36a48847` after required history, protocol and claim review.
No interrupted dirty draft or September 7 WIP remained. The service interruption
resumed the same native holder and escape-analysis work; three independent agents
supplied escape implementation, source fixtures and read-only native review.
Root reconciled and gated both concerns.

Linux executable/argv checks found no Claude identity among 72 processes, but 56
unread executable links prevented confirming availability. Windows
Get-CimInstance succeeded (346 processes), with no Claude identity. Concurrent
restrictions remained; no browser/runtime change or new WPT/test262 measurement.

## Changes

- `74bb2f83`: bounded negative Number inputs to unsigned right shift now share
  the existing exact ToUint32, masked-count and endpoint proof. The strictly
  negative band preserves order; crossing zero, out-of-range intermediates,
  nondivisible strides, saved children, cycles and overlapping count reloads
  retain their refusals. Original right-shift source 18 is promoted unchanged;
  twelve source cases and focused CFG/SCF controls accompany it.
- `4d909704`: ordinary class methods and constructors capture fixed
  local callable holders through the existing cell/alias/slot census. Closed
  source holders retain the strict complete source-body proof, including unused
  slots. DOM bodies still require their separate typed proof. Super expansion
  preserves each original base/leaf holder identity, including shared bases,
  different holders at the same capture index and multilevel inheritance.
  Holder functions retire only after every recorded captured read is rewritten.

Eight new native sources add **64 executions**, covering methods, sibling
captures, argument order, shared targets, inherited constructors and unused
captured-slot cleanup. One preserved unused class-method source passes
preparation but refuses native admission without a proving invocation. Eight
controls retain mutable cells/slots, aliases, receiver identity/escape, surplus
arguments, unknown unused effects and shared Map refusal. All earlier source
sections remain unchanged. Authentic Data+B retains all original class, helper
and Data bodies; its null-element observation does not exercise registration.

## Focused validation

All devbox commands held `/tmp/ctbrowser-devbox-build.lock`. Commands, logs,
source hashes and generated samples: `/tmp/ctcompile-captured-holders-0558/`.

- Explicit targets: `ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`.
- Exact `ctcompile_escape_analysis_arrays`: 1/1, 1.41s (1.42s total).
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(right-shift|left-shift|composed)-index-overwrite[.]test$`:
  3/3, 0.14s. Right shift: 99 sites / 18 sound / 18 of 24 confined precision;
  zero violations, partial, pending or unclaimed sites. Four tested hashes match.
- Exact `ctcompile_host_contract`: 1/1, 0.48s (0.49s total); the repeated
  fixture-correction gate also passed 1/1, 0.50s (0.51s total).
- Selected constructor probe: **42 source observations / 128 main native
  executions (64 new) / 84 unprepared and 45 preparation refusals**. Two further
  native refusals retain the preparation-only uncalled method. Selection:
  `captured-holder-*`, `inherited-captured-holder-*`, `bootstrap-base*`,
  `captured-helper-*` and `inherited-post-super-holder*`.
  Complete budgets: sibling holder 466, unused captured slot 520, inherited
  shared holder 1,454, inherited argument order 1,502; existing post-super chain
  2,673, post-super order 1,578 and helper constructor 378. Forged annotations,
  duplicate closures, bounded partial-proof refusals and rooted-super controls
  pass. Ancillary checks pass 16 constructed-method executions / 20 refusals
  and eight original-r executions / four refusals. GCC/Clang, explicit/deduced
  output and both optimization settings are covered. This is not a full
  class-initialization lit pass.
- Lit filter `^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$`:
  1/1, 291.76s; unchanged 632 Node/interpreter observations / eight combined
  native executions / 4,922 refusals.
- All eight native source hashes and four escape hashes match the devbox inputs.
  Inspected distinct-holder, multilevel and unused-capture generated C++:
  stack-owned records, borrowed receiver pointers and direct helper functions;
  existing nullable primitive scalars remain. No holder table, closure environment,
  prototype storage, collector, Script/VM symbol or runtime dispatch appears.
- Required `tools/format.sh --check` retains 20 diagnostics in six HEAD-identical
  files (ctdrive, PrefixAnalysis, ProviderCallbacks, ProviderPaths,
  PartialEvaluation Heap, Symbolic Facts). Changed C++/Python formatting,
  Python AST, Node checks and diff whitespace pass.

The first native probe passed the initial method and sibling-holder cases, then
refused inherited holder captures during super normalization. The original
helper-capture transport now carries holder identities too; a fresh focused gate
covers the correction. The second probe passed the new inherited cases, then
found the existing uncalled-method native boundary.
That source is preserved as preparation-only and pins the invocation refusal;
the independent unused captured-slot case still exercises retirement. Its first
synthetic draft left a helper without any live invocation/type. Both live and
unused slots now capture that same helper, proving its type while testing
retirement of the unused captured slot. No production rule was widened for it.

Skipped: full CTest/compiler lit, whole class-initialization lit, broad native/
corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers. No push.

## Exact next boundary

Both unchanged `bootstrap-base` and authentic `bootstrap-base-data` now refuse
`class own-key snapshot requires fixed constructor fields`. The Data holder's
identity gets past the earlier capture gate; B's conditional early return leaves
variable own-field presence, before the complete holder/Map body proof runs.
The focused `captured-holder-map` control still refuses
`class method capture is not its constructor or an inert sibling helper`.
This increment does not prove Data registration or Map ownership.

Next: variable own-field presence and original snapshot iteration, alongside
shared nested Map ownership, stored receiver lifetime and complete
duplicate-registration effects. Retain all dispose effects (`e.remove`, `P.off`).
Receiver-selected inherited getters, static construction, DOM/config/selectors/
events/Popper, broader ownership and the application driver remain unfinished.
