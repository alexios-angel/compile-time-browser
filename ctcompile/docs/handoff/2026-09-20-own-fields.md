# Fixed own-field snapshots and bounded BitNot indices — 2026-09-20 UTC

Continued clean `9f971ecd` and the original Bootstrap receiver/own-property
boundary recorded in HANDOFF and plan 00. Both required histories and unmerged
branches were reviewed; no dirty or interrupted WIP remained at session start,
and September 7 WIP was absent from the unmerged list. Service interruptions at
03:44:55 and 03:55:31 resumed the same drafts and exact areas. Three agents
supplied source fixtures, an independent escape extension and native proof review;
root reconciled, gated and committed each area separately.

Linux executable/argv and ps checks found no Claude identities, but permission
denials prevented confirming availability. Windows Get-CimInstance succeeded
(366 processes) without Claude matches. Concurrent-agent restrictions remained.
No browser/runtime edits or push; no new WPT/test262 measurement.

## Changes

- `89f987bd`: bounded varying BitNot array indices reuse the existing exact Number
  complement proof. Both endpoints must lie within signed i32, excluding ToInt32
  discontinuities; complement reverses endpoint order and retains positive stride.
  Reload-overlap, fractional intermediate, alias, cycle and budget guards remain.
  Added six CFG/SCF positives, five unit refusals and a 20-function source oracle.
  Existing unary function20 now proves confined; its source is unchanged.
- `674b985c`: `Object.getOwnPropertyNames` snapshots
  on exact local class receivers fold only their length and constant in-bounds
  numeric-index observations. Constructors must use straight-line constant/parameter
  assignments to ordinary named own data fields; repeated writes retain first insertion order. Every
  method and external instance write must preserve that field set. Known snapshot
  keys can then clear fields with ordinary typed writes. No native key container
  or reflection runtime is emitted.

The optional closed-source `Object` declaration reuses the existing intrinsic
member/call identity check. All original aliases and writes are checked before
snapshot erasure; declaration alone never authorizes an Object operation.
Constructor replacement returns are refused even on classes without methods.
Fixed-cell cached producers follow replaced scalar snapshot reads. Original
method frames and other effects remain; all unused bodies still pass the source
census. Inheritance, conditional fields, numeric property names, escaping snapshots,
callbacks and for-of iteration remain refused by this slice.

Eighteen new source cases comprise five executable positives and thirteen refusals.
The boxed-key regression explicitly asserts that imported IR stores a scalar
snapshot read in a cell. Original source sections 01–09 and Bootstrap bodies
remain unchanged. Missing Object identity, complete work-budget and forged-input
controls reuse the existing harness.

## Focused validation

Every devbox command held `/tmp/ctbrowser-devbox-build.lock`. Scripts, logs and
hashes are in `/tmp/ctcompile-own-fields-0342/`.

- Built `ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle` with explicit
  `tools/remote-build.sh` target lists.
- Exact `ctcompile_escape_analysis_arrays`: 1/1, 1.35s (1.36s total).
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(bitnot|unary|composed)-index-overwrite[.]test$`:
  3/3, 0.14s. BitNot: 60 sites, nine sound, 9/14 confined precision. Unary:
  63 sites, ten sound, 10/14 precision. Composed: 54 sites, seven sound,
  7/10 precision. All have zero violations, partial, pending or unclaimed sites.
  Five tested source hashes match the devbox.
- Exact `ctcompile_host_contract`: 1/1, 0.49s (0.50s total).
- The narrow eighteen-source native probe passed: 40 main native executions,
  36 unprepared and 18 preparation refusals, first complete own-fields-order
  budget 755. Ancillary checks: 16 constructed-method executions/20 refusals
  and eight original-r executions/four refusals. The later final gate includes
  the strengthened boxed-local fixture and its imported-cell assertion.
- Final class initialization/DOM lit filter
  `^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$`:
  2/2, 399.41s. Class: 276 source observations, 764 main native executions,
  552 unprepared and 299 preparation refusals. DOM: 632 observations, eight
  combined native executions, 4,922 refusals. Seven tested native source hashes
  match; all twelve native/escape hashes match in total. Class ancillary controls:
  16 constructed-method executions/20 refusals, eight original-r executions/four
  refusals, four prototype-key executions/six refusals and 15 prepared refusals.
  Own-fields-order complete budget is 755; existing nested-chain budget is 446.
- Inspected key-order, field-clearing and boxed-local generated C++: stack-owned
  records, borrowed receiver pointers, direct field writes and constant strings.
  Null clearing uses the existing nullable scalar representation. No key container,
  prototype storage, closure environment, Script/VM symbol or collector is emitted.
  The boxed-key structured IR contains the exact snapshot read stored into a cell.
- Required `tools/format.sh --check`: final check retains the same 20 diagnostics
  in six HEAD-identical files (browser ctdrive; compiler PrefixAnalysis,
  ProviderCallbacks, ProviderPaths, PartialEvaluation Heap and Symbolic Facts).
  Changed C++/Python formatting, Python syntax, Node source checks, shell syntax
  and whitespace checks pass. The first formatting check also saw in-flight
  escape-test formatting; the final check was after all drafts froze.

The first native build failed because NumberAttr takes raw IEEE bits rather than
an implicitly converted double; explicit bit construction fixed it. Independent
review found the replacement-return and cached-cell issues before final gating;
both have source regressions. The initial boxed-local fixture did not force the
local into a cell, so the strengthened fixture and IR assertion now enforce that
precondition. No runtime oracle behavior was changed to fit native output.

Skipped: full CTest/compiler lit, broad native/corpus matrices, WPT/test262, whole
Bootstrap, Windows and sanitizers. These are focused passes only.

## Exact next boundary

The unchanged original W/B specimen, with declared standard Object identity,
now refuses `class own-key snapshot requires fixed constructor fields`.
This gate rejects its inherited class; source inspection also shows B's
conditional `_element`/`_config` writes and early return. The standalone preserved
`for (const t of Object.getOwnPropertyNames(this)) this[t] = null` control refuses
snapshot consumption. Neither is successful full Bootstrap execution.

Next: prove complete per-instance inherited and conditional own-field presence,
and the original own-key iterator snapshot/clearing loop, reusing existing
iterator identity and ordered receiver proofs. Do not derive field authority
from the null-input observation or discard dispose's e.remove/P.off effects.
Ordinary static `this.getInstance`/`new this`, inherited DOM/getter receiver proof,
H/config, selectors/events/Popper, broader ownership/own-data provenance and the
application driver remain unfinished.
