# Saved record fields and addition indices, 2026-09-21 UTC

Continued clean `5d790a74` from the latest handoff and plan 00/24/25. Both commit
histories and unmerged branches were read; `codex-wip-20260907` is already an
ancestor. There was no unfinished code at entry. Linux inspected 72 processes
with 57 unreadable executable links; Windows CIM inspected 350 processes. Neither
found a Claude executable, CLI or loop. Availability remained uncertain, so the
concurrent-agent rules applied. No browser/shared implementation changed.

## Changes

`fbba77fa` extends saved scalar globals to exact caller own-field reads
already checked by the completed captured-Map family. Both the source dependency
consumer and the global-owner consumer require the original fresh allocation and
exact read. Existing entry order, sole global store, read-time category and the
complete field/alias/observer census remain mandatory. Categories do not supply
constants or predicates; native code retains the original read/store/load.

The complete former global-snapshot refusal now executes unchanged. Additional
witnesses save through an alias before overwriting its field and publish arithmetic
on a field read. The existing document-owner client checks repeated invocation,
separate documents, aliases, detachment, invalid inputs before effects and
lifetime order. Reassigned globals, escaped/captured objects, getters,
uninitialized fields and fields outside the completed family remain refused.

`f5d36487` admits one addition of direct Number literals as a UTF-16
`charAt`/`slice` bound. Existing numeric Add admission and emission are reused;
the sum may retain other already-supported numeric uses. No bounds-only
restriction is added to ordinary addition. Tests cover zero, fractional indices,
subnormals, wide bounds, overflow/infinity, Unicode and captured descriptions.
The identical old addition refusal body, previously used in two operator groups,
now executes unchanged. Direct-literal first-unit casing and dataset-tail
authority remain separate; computed 0/1 does not grant either.

## Focused validation

| Check | Measured result |
| --- | --- |
| Exact CTest `ctcompile_host_contract` | 1/1; 0.56 s, 0.57 s total |
| `CTNative/Browser/native-class-dom-data.test` | PASS; 101.24 s, 101.25 s total |
| `CTNative/HostContract/dom-data-inputs.test` | PASS in the initial three-case selection |
| `CTNative/HostContract/Provider/objects.test` | PASS in the initial three-case selection |
| Addition String selector | 40 native executions; 868 proof refusals; 118 Node/VM agreements, 15 known differences |
| Strengthened computed-0/1 casing controls | Four native refusals |
| Final source hashes | All eight code/test files match the devbox |

The final DOM fixture measures 56 record executions (24 new), 24 existing
object-key executions, nine published plus four old Node/VM source observations,
54 published refusals, 30 old preparation refusals, eight old incomplete-session
refusals and four prepared entries. Native executions use GCC/Clang,
explicit/deduced output and both optimization policies. The String selector
executes three new cases and two existing sentinels and retains all proof checks.
The two stronger casing controls were added after its source sync and passed a
separate final replay; no final whole intrinsic-export replay is claimed.

The explicit devbox targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`, under the
shared build lock. The initial saved-field probe passed source proof and both
native policies. The first three-case lit selection passed
`CTNative/HostContract/dom-data-inputs.test` and
`CTNative/HostContract/Provider/objects.test`, then exposed a fixture expectation:
the VM also prints the new `saved` global. Both oracles now compare that global
explicitly. An initial shell heredoc let the build consume the following test
command; only the later explicit lit invocations count as validation.

Scoped formatting/syntax and source-preservation checks pass. All 107 earlier
complete intrinsic cases and 33 general refusals are unchanged. Existing class
source generators, preparation/object driver and native lifetime client are
preserved. Repository `tools/format.sh --check` retains 16 pre-existing diagnostics
in untouched `ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.

Agents supplied the fixture changes, the initial String implementation and a
read-only ownership review. Repeated service limits required root to finish the
String tests and fixture oracle correction. Review found no actionable safety
defect in the saved-field proof. A local preservation script first expected one
old addition refusal site rather than two, and a Node check initially counted
only `var` declarations; corrected checks passed before the final gate.

No full CTest/compiler lit, complete intrinsic-export replay, broad corpus/matrix,
full Bootstrap, WPT/test262, Windows, new sanitizers, browser/runtime changes,
local native build or push ran. Existing intrinsic executable mutation tests were
skipped by the focused selector.

Evidence: `/tmp/ctcompile-field-snapshot-build2.log`,
`/tmp/ctcompile-field-snapshot-probe.log`, `/tmp/ctcompile-field-snapshot-lit.log`,
`/tmp/ctcompile-snapshot-addition-final.log`,
`/tmp/ctcompile-snapshot-addition-check-final.log`,
`/tmp/ctcompile-snapshot-addition-format-final.log`,
`/tmp/ctcompile-snapshot-addition-final.sha256` and
`/tmp/ctcompile-string-addition-focused.py`. The final DOM lit report is
`/tmp/ctcompile-field-snapshot-final.json` on the devbox.

## Next boundary

The complete vendor `published_source()` holder and constructor witnesses still
refuse class preparation and native emission. Global own-field scalar snapshots
are now supported; that adjacent prerequisite does not establish class ownership.

Class preparation must combine residual DOM input uses with a completed public
family. Its complete source census separately covers wrapper/factory closures and
calls. The provider then needs the existing constructor/local-Map/record proof,
and `OwnedGlobalRoots::analyzeMethodTable` still requires the exact source function
chain and allocation census. Reuse these proofs together; allowing `ConstructOp`
or residual calls by opcode would bypass independent obligations. The actual
class-result witnesses remain in the executable fixture as refusal controls.

Multiple DOM-input alias partitions, original B/Data+B, broader String methods and
indices, conditional callees, mutable captured cells, String ordering, document
views and the application driver remain. No full-Bootstrap admission or corpus
coverage gain is claimed.
