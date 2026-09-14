# Handoff: continuing ctcompile

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

## Owned DOM sessions and direct arrays, 2026-09-14 UTC

**Owned synchronous DOM sessions and direct-array contents, 2026-09-14 UTC.**
Clean start at **5070d4aa**; the 04:27 UTC journal confirmed the interrupted
outer-key/loop recovery was already landed. **f4089b65** adds
`ctbrowser-dom-session-v1`, using the existing explicit element-parameter proof
and generating a nonmovable owner of atoms, document and optional live Style
engine. Every input domain is checked before any validation or source effect.
The owner gate passes **3 sources / 5 retention refusals**, both policies/layouts,
GCC13.3/Clang24 and generated-client ASan/UBSan; focused **1/1 in 49.61s**. Existing
DOM entries pass in **178.07s**. Browser libraries are the ordinary devbox build.

**956b33bd** recognizes an already-executed direct array in the bounded counted
contents certificate after SCF removes an invariant array parameter. Nine new
matrix rows pass with the complete escape oracle (**0.67s / 1.19s**); the preserved
called overwrite-loop source passes its refusal lit (**0.04s**). Its measured
admission remains **0/2 before and after**, both policies: the importer places
an `scf.if` inside the loop header, with poison/arith flag transport. That shape
still needs proof; no native emission or vendor coverage gain is claimed.

Final standard devbox gate: **295-step build / 563/563 CTests PASS in 1217.73s**,
including **169/169 lit in 862.71s**. All **1418 frozen inputs** match local/devbox.
The structured matrix now covers **43 rows / 2228 budget cutoffs**. Under full
load, owned DOM takes **84.03s**, ordinary DOM **278.06s**, Data **55.17s**, and
exhaustive shared-Map ownership **354.99s**. Stable format **823 C++ / 90 Python /
33 web PASS**; the required pinned check matches the existing nine-file /
26-diagnostic baseline exactly. Evidence: `/tmp/ctcompile-dom-input/`, including
`measured.json`, full/focused logs, generated owner/client and before/after loop IR.
No browser source/runtime changes, WPT/test262 score remeasurement or push.

Fresh full Bootstrap stays **19/574 native / 0 of 43 globals**, both policies,
no skipped/pruned functions. Existing DOM remains **18 entries / 42 refusals**.
The pinned Data source remains **3218 bytes / 7/7 functions / 23 direct calls /
19 observations / 3 outer-key objects**. The unchanged complete escape source and
snapshot remain **222 functions / 861 claims / 895 observed sites / 35 unclaimed /
zero violations / 40 of 172 precision**. The separate called loop source is pinned
at SHA256 `3d926af84da7a76e3080257dade46e5d59ad603ba0231dff7d24f97de2f81cbf`.
Three agents split implementation and surveys; two reached service limits before
edits, and root completed both implementations. Independent owner and escape
reviews found no actionable defects.

**Next native:** connect the owned DOM session to source-proved Data with explicit
DOM origins across its complete method family, conservative aliasing of distinct
input parameters, and Data/table storage that cannot outlive the document. The
current shared table/global carriers and source-only object census are insufficient.
Outer-key-only source allocations still cannot authorize external DOM handles.
Then component construction/disposal, retained callbacks and the application driver.
**Next escape:** normalize or certify the preserved imported header branch and
its poison/arith transport before consuming the whole-function contents proof.
Use the preserved source in `CTNative/Fixtures/Objects/array-overwrite-loop.js`;
`LiftToSCF.cpp` documents why arbitrary CTJS canonicalization is unsafe.
Keep all paths, mutation refusals and budgets; broader alias ownership and a vendor
emission gain remain separate. DOM-backed Data and full Bootstrap startup are absent.

## Outer-key provenance and structured-loop recovery, 2026-09-14 UTC

Resumed six dirty compiler files at **ef764cf8**, identified in the 03:39:01
AGENT-SYNC journal and explicitly abandoned by the 03:42:08 loop exit.
The September 7 WIP was already an ancestor. Three agents split loop recovery,
key tests and ownership review; two hit service limits, and root completed the
key implementation, tests, integration and gates. Recovery was committed first.

**722ddd82** shares the existing read-only zero/+1 own-length certificate between
CFG and single-block `scf.while`. Initial values, condition arguments, backedges
and results transport exact aliases and scalar snapshots simultaneously. Zero-trip,
sequential loops, enclosing structured branches and result-to-CFG transport are
covered. Mutation, repeated allocation, nested loop control, uncertain induction,
unsafe reads and incomplete budgets refuse. The structured matrix has **34 rows**,
including **22 new rows**. This is analysis evidence; native loop/alias admission
and a preserved vendor emission gain still need their own consumer proof.
Focused **85-step build / escape arrays and complete oracle 2/2 PASS in 1.88s**.

**cb76be27** derives `HostCapturedMap.outerKeyObjects` from the completed family
and every use of each caller allocation and named alias. Only direct outer Map
key uses qualify; a sibling payload/child-key use, field access, parameter
transport or outer-key snapshot excludes the allocation. Fluent outer `set`
aliases are followed within the shared budget. Child snapshots remain distinct.
The ordinary owning-object contract is preserved when the narrower role fails.
Every owning family revalidates the same source-ordered allocation list.

Host diagnostics and final native owner reports expose a count, never authority.
The unchanged Data probe proves exactly **three** key allocations (`element`,
`other`, `absent`), excluding its payload. Forged counts are rederived. The Data
measurement runs after the existing private preparation and final live ownership
check; raw unprepared host analysis retains its refusal. No new DOM input type,
external handle permission or lifetime owner is introduced.

Focused ownership **237.18s**, then expanded **6-step rebuild / 239.08s PASS**;
corrected native Data **3-step rebuild / 1/1 PASS in 30.93s** (test 30.91s).
Data retains the pinned **3218-byte source / 7/7 functions / 23 direct calls /
19 Node/VM observations**, both policies/layouts/GCC/Clang, compile-clean,
no Script and ASan/UBSan saved-child/payload lifetimes. Stable formatter
**823 C++ / 89 Python / 33 web PASS**; the required pinned output matches the
prior nine-file / 26-diagnostic baseline exactly.

Final standard devbox gate: **282-step build / 562/562 CTests PASS in 1234.62s**,
including **168/168 lit in 873.73s**. Native Data **53.28s**, native DOM **268.91s**,
and exhaustive shared-Map ownership **342.85s** under full load. All **1415 frozen
inputs** match local/devbox. The 34 structured rows exercise **1769 budget cutoffs**.
The generated Data probe is byte-identical to the previous output: **48,583 bytes /
981 lines**, excluding appended test assertions. Evidence:
`/tmp/ctcompile-key-recovery/` (`measured.json`, `size.json`, full/focused logs and
generated C++). No browser source/runtime changes, WPT/test262 score remeasurement
or push.

Fresh full Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
no skips/prunes. Native DOM remains **18 entries / 42 refusals**. The unchanged
complete escape fixture/snapshot remains **222 functions / 861 claims / 895 observed
sites / 35 unclaimed / zero violations / 40 of 172 precision**.

**Exact next native boundary:** explicit DOM input provenance and a nonmovable
owner declaring atoms, document, then Data. The new allocation role is not DOM
provenance and cannot substitute for it. Revalidate every external key's complete
family and document domain; preserve document plus full node identity across
detachment, and reject foreign owners before dereferencing them. Then original
component construction/disposal, retained callbacks and the native application driver.
**Exact next escape boundary:** first recognize a dominating direct array in the
counted contents certificate after SCF preparation removes invariant array phis.
A called overwrite plus read-only loop can then exercise the existing native
contents consumer without new alias carriers. Measure that preserved fixture
before broader structured/CFG alias ownership or a vendor gain. Read-only review
found that `denseIndexPaths` also hits branch allocation, nested elements and
returned-array ownership; relaxing its direct-use check alone is insufficient.
This next slice was surveyed, not implemented or measured. DOM-backed Data and
full Bootstrap startup remain absent.

## Structured contents recovery and direct Data members, 2026-09-14 UTC

Resumed three dirty escape files at **1a0d533d**, identified in the 02:54 UTC
`codex-dom-session` journal and explicit 02:56:23 abandonment. The September 7
WIP is already an ancestor; the two old lens branches are superseded by
**6520fcf2**. Recovery was gated and committed first. Three agents split recovery,
DOM ownership review and a Bootstrap source survey; two hit service limits and
root completed integration. No browser files changed.

**722ba713** transports exact contents through single-block `scf.if` arms and
simultaneous `scf.yield` results. Each arm keeps separate aliases, array/object
contents and read-time scalar facts; nested branches and an implicit empty else
are covered. Both arms are checked even for constant conditions. Unsupported
paths and incomplete budgets discard the complete result. Structured loops and
native alias emission remain outside this proof. Twelve rows exercise **633
budget cutoffs** and the new transport/refusal boundaries. Focused **17-step
build / 2/2 escape array and complete-oracle CTests PASS in 1.88s**.

**1b6ec81b** removes three stored capture tuples and their initialization from
source-proved by-value outer Data Map sessions. Member bodies pass their own
`&captured_map` directly to the source functions. The table occupies exactly its
Map storage; it stays noncopyable/nonmovable. Other session environments and the
ordinary owning-callable provider retain their contracts. The unchanged
**3218-byte probe / 7/7 functions / 23 calls / 19 Node/VM observations** passes
both policies/layouts/GCC/Clang, compile-clean, no Script and ASan/UBSan saved-child,
payload and reentry checks. Focused **19-step build / 31.48s**, then final guarded
closure check **3-step rebuild / session 1/1 in 31.09s**.
On both GCC13.3 and Clang18.1.3, the table shrank **48 → 24 bytes**, exactly
its Map's size. The same generated probe, excluding appended test assertions,
shrank **50,376 → 48,583 bytes / 1015 → 981 lines**.

Final standard devbox gate: **259-step build / 562/562 CTests PASS in 1210.74s**,
including **168/168 lit in 871.12s**. Native Data session **63.80s**, native DOM
**268.88s**, exhaustive Map ownership **339.60s** under full load. All **1415 frozen
source/test/config inputs** match local/devbox. Stable format passes **823 C++ /
89 Python / 33 web**; required pinned formatter output is byte-identical to the
known nine-file / 26-diagnostic baseline. Evidence:
`/tmp/ctcompile-structured-resume/` (`measured.json`, `size.json`, complete logs
and generated C++). No browser source/runtime changes, WPT/test262 remeasurement
or push.

Fresh complete Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
no skips/prunes. Native DOM remains **18 entries / 42 refusals**. The complete
escape fixture/snapshot remains **222 functions / 861 claims / 895 observed sites /
35 unclaimed / zero violations / 40 of 172 precision**; neither source nor
expected snapshot changed.

**Exact next native boundary:** actual DOM inputs and the nonmovable
atoms/document/Data owner, with separate DOM-key provenance. Current entry proof
accepts only the script's three implicit arguments; actual object arguments must
come from source `CreateObjectOp` allocations. `objectKeys` currently also permits
payload uses, so it must not be reused for DOM inputs. Initially restrict DOM
values to outer Data keys, excluding payloads, child keys, snapshots and returns.
Revalidate the complete family, preserve document identity and the full node ID,
and reject foreign document domains before dereferencing their owners. Then
prove detach/reinsert and teardown with preserved original Data methods. No DOM
key admission or complete Bootstrap startup is claimed by this session.

**Independent escape next:** structured-loop/CFG alias transport in the native
consumer, then a preserved vendor refused-to-emitted case. Local vector/branch
proofs alone do not resolve Bootstrap's callback/DOM/capture-heavy arrays.

## Data Map ownership and native array overwrites, 2026-09-14 UTC

Resumed five dirty EmitC files on **0d5ca5b4**, identified in the 01:59:41 UTC
`codex-session-map` journal and explicit 02:01:28 abandonment. September 7 WIP was
already an ancestor. The recovery was gated and committed before the next item.
Three agents split lifetime tests, ownership review and the vector consumer;
the first two reached service limits before edits. The vector agent delivered
its implementation; root integrated, reviewed and gated it after its service limit.

**4868c75d** makes the `closed-source-session-v1` table own the captured outer
Data Map by value. Its three method capture tuples borrow that member. The table
remains noncopyable/nonmovable; the complete current source ownership proof still
guards the change. Saved child Maps and ordinary payloads retain their independent
ownership across removal, reinsertion and session destruction. The table and
ordinary object carriers keep their existing ownership. No DOM keys are admitted.

The unchanged **3218-byte probe / 7/7 functions / 23 direct calls / 19 typed
Node/VM observations** passes both policies, both layouts, GCC13.3/Clang18.1.3,
strict compile-clean, no-Script symbols and ASan/UBSan saved-value/reentry controls.
Focused recovery: **4-step rebuild / session 1/1 PASS in 29.45s**. The existing
`closed-source-v1` UMD owning-callable and refusal matrix also passes separately.

**d878d5ee** reuses bounded static Number Add evidence for original dynamic Add
only when both original operands are proved exact nonnegative integers and their
sum fits 2^32-1. It adds no coercion, rounding or control-flow exemption.
**d01294da** consumes complete current `computeArrayContents` write witnesses in
`TypeInference::isDenseVectorSite`, joins stored values into element inference,
and records accesses before retyping invalidates evidence. Direct local own-element
Number overwrites emit vector assignments. Aliases, sparse writes, uncertain or
nullable indices/values, length mutation and escaping stores continue to refuse.

The new called `CTNative/Fixtures/Objects/array-overwrite.js` is byte-identical
before/after (SHA256 `012f7c90595d25de02e4c27a83bcc1d59996f88709acaf7b2a7536df51d2b437`).
Measured with **4868c75d** then the new compiler: **0/6 → 6/6 native**, both policies,
no skips/prunes, five direct calls. This is a local fixture, not a vendor gain.
The original uncalled `indexed.js` body in `array.mlir` is preserved; its assertion
now records the remaining unvisited-element-type refusal. The existing length+1
escape controls retain their source and now refuse as MissingElement.

Focused vector validation: corrected **13-step rebuild**, all twelve native
pipeline checks and the complete escape oracle pass. Two unit-fixture corrections
pass **4-step rebuild / 2/2 in 0.73s**; the two array lit cases pass in **0.14s**.
The array length matrix covers **175 rows / 33 live states / 4970 budget cutoffs**.

Final standard devbox gate: **253-step build / 562/562 CTests PASS in 1210.58s**,
including **168/168 lit cases in 869.32s**. The Data session took **53.09s**, native
DOM **266.42s**, and exhaustive shared-Map ownership **341.22s** under full load.
All **1414 frozen source/test/config inputs** match local and devbox.
Stable whole-tree format passes **822 C++ / 89 Python / 33 web**; required pinned
`format.sh --check` is byte-identical to the known nine-file / 26-diagnostic baseline.
Evidence: `/tmp/ctcompile-map-recovery/`. No browser source or runtime semantics
changed; full WPT/test262 scores were not remeasured. No push.

Fresh complete Bootstrap remains **19/574 native / 0 of 43 globals resolved**,
both policies, no skipped/pruned functions. Native DOM remains **18 sources /
42 refusals**. The complete escape oracle remains **222 functions / 861 claims /
895 observed sites / 35 unclaimed / zero violations / 40 of 172 precision**;
its source and expected snapshot are unchanged.

**Exact next browser boundary:** a nonmovable owner of atoms, document, then Data
state. Give retained DOM keys separate provenance and document+node identity;
detaching a node must preserve that identity. Foreign document domains need session
ownership or refusal. Keep ordinary-object owning-callable behavior separate.
Then original Button/BaseComponent/Config construction/disposal, retained browser
callbacks, event delivery and the native application driver. DOM-backed Data and
complete vendor initialization remain unimplemented.

**Independent escape next:** structured-loop/CFG alias transport and a preserved
vendor refused-to-emitted source case. The direct local contents consumer now
exists; general retained-graph ownership and refined verdict emission do not.

## Direct Data session methods, 2026-09-14 UTC

Resumed the eleven uncommitted compiler/proof files left after the 01:07 UTC
interruption on **263f2051**, identified in the working diff and synchronization
journal. September 7 WIP was already an ancestor. Three parallel agents reached
service rate limits before writing files; root completed recovery, tests and review.

**61448f32** adds explicit `closed-source-session-v1`, reusing the complete current
closed-source host and captured-Map owner proof. It requires the whole captured
method family to pass native admission. Data methods become members of a
noncopyable, nonmovable table, with private capture tuples and direct calls.
Method reads cannot escape independently as owning callables. The existing
`closed-source-v1` owning-callable provider retains its contract. DOM parameters,
keys and document ownership are not admitted by this prerequisite.

The registered session gate preserves the original **3218-byte Data/UMD probe**,
**7/7 functions**, **23 Data calls** and **19 typed observations**, checked against
Node and the VM. Both optimization policies and explicit/deduced C++ pass GCC 13.3 and
Clang 18.1.3, the no-Script symbol gate and ASan/UBSan lifetime/reentry checks. Budget
exhaustion, stale manifests and source callable escape refuse; freshly fingerprinted
forged report attributes do not change emission. Unit coverage also checks provider
separation, uncaptured/scalar owners and incomplete proof publication.

Focused validation: corrected **99-step build**, native session **20.98s**, existing
exhaustive shared-Map proof **247.37s**. A new parser fixture omitted required empty
binding arrays; correcting the fixture gives **2-step rebuild / 2/2 focused PASS
in 20.99s**.

The full **576-step build** passed; CTest initially passed **530/550 in 1154.03s**,
including all browser tests and **167/167 lit in 826.29s**. Twenty compile-clean
checks exposed the new helper's missing provenance prefix. **c67cb463** adds helper
and session-member provenance and runs the existing strict compile-clean gate on
each session policy/layout. A **242-step rebuild / 20/20 rerun in 7.75s** fixed the
initial failures. The expanded session check then exposed older owned-global
helper comments; after their correction, **241-step rebuild / session 1/1 in
30.05s** passed. All **550 tests have passing results across the full run and
focused reruns**; this was not a single clean full run. Follow-up emission changes
are comments only.

All **1411 frozen source/test/config inputs** match the devbox. Stable whole-tree
format passes **822 C++ / 89 Python / 33 web**; required pinned `format.sh --check`
retains exactly the same **nine baseline files / 26 diagnostics**. No browser
source or semantics changed; full WPT/test262 scores were not remeasured. Evidence:
`/tmp/ctcompile-session-resume/`, including final C++ and `measured.json`.

Fresh full Bootstrap remains **19/574 native / 0 of 43 globals resolved**, both
policies, no skips/prunes. The independent native DOM gate remains **18 sources /
42 refusals** (**246.20s** under full load). Escape remains **222 functions / 861
claims / 895 observed sites / 35 unclaimed / zero violations / 40 of 172 precision**.
No native bundle startup or DOM-backed Data retention is established. No push.

**Exact next browser boundary:** own atoms, document, then Data state in a single
nonmovable session; replace this prerequisite's existing shared Map/object carriers
only where the new ownership proof permits ordinary by-value/unique ownership.
Give DOM keys separate provenance and document+node identity, preserving detachment
and refusing foreign document domains unless owned by the session. Keep ordinary
Data owning-callable behavior separate. Then original Button/BaseComponent/Config
construction and disposal, retained callbacks and the native application driver.

**Independent escape work surveyed, not implemented:** the current contents query
already records exact array-write witnesses. A first native consumer can prove
bounded overwrites, join stored values into element inference, and capture emission
plans before retyping invalidates those records. The existing `indexed.js` body in
`CTNative/Lowering/Objects/array.mlir` is a useful control; it is currently uncalled,
so preserve that test and add a separately measured called subject. Structured-loop
transport still needs its own live proof. No escape precision gain is claimed here.

## Native selector queries and original array induction, 2026-09-14 UTC

Continued clean **fc03829b**. Both commit histories and the synchronization journal
showed the preceding review/native work complete; September 7 WIP was already an
ancestor. Resumed the retained DOM Data boundary from HANDOFF/plan00, then followed
the user's browser-API steering. Three agents split shared closest extraction,
native query proof/emission and the independent escape loop; root integrated and
gated `codex-native-session-20260914`. No push.

**eb80c53f** moves the existing inclusive ancestor walk into public
`style::engine::closest`. The VM binding is a thin adapter after its unchanged
argument conversion and selector validation. Both callers use the existing matcher;
`:scope`, nearest-match order, detachment and shadow boundaries are preserved.
The direct Style/DOM/Core test and VM selector regressions cover those behaviors.

**c83d90cc** emits proved `contains`, `matches` and `closest` calls through
public DOM/Style APIs. Query entries append borrowed Style engine references for
queried element parameters, in source order. The embedding supplies each document's
live engine; element validation and atom-table checks precede source effects.
This preserves interactive selector state. No new engine, selector implementation,
VM context or GC handle is generated. Closest misses use canonical empty element
identities; local results permit only strict identity comparisons, including
cross-document misses. Nullable dereference, retention, borrowed returns and
explicit source-null comparison still refuse. The existing Data provider is unchanged.

**8fb90ece** certifies a read-only header/body loop with literal zero/+1 induction
under the same stable array's strict own-length guard. Existing exact state
transport and operation checks run on every bounded iteration. Allocation,
mutation, other control, unsafe elements and incomplete budgets refuse. Thirty
matrix rows exercise 1527 retention-budget cutoffs, plus a 32-iteration budget case.
The unchanged original `confinedArray` becomes confined: the complete snapshot
changes only its fn2/pc3 array claim. All recorded observations and source pins
remain intact. Escape precision is **40/172**, with **222 functions / 861 claims /
895 observed sites / 35 unclaimed / zero soundness violations**.

Validation: initial **400-step devbox build**; corrected focused gate **6/6 PASS
in 168.15s**, including native **18 sources / 42 refusal controls** in **168.14s**,
both optimization policies, both layouts, GCC13.3 and Clang24. Final standard
gate: **690-step build / 549/549 CTests in 1167.79s**,
including **167/167 lit cases in 823.37s**. The native matrix took **247.81s**
under full load.
All **1410 frozen inputs** match isolated/devbox. Stable formatting passes
**822 C++ / 88 Python / 33 web**. The pinned formatter retains the same nine
baseline files / 26 diagnostics. No full WPT/test262 score remeasurement.
Evidence: `/tmp/ctcompile-native-session/`, including emitted closest C++.

Fresh complete Bootstrap remains **19/574 native / 0 of 43 globals resolved**,
both policies, no skipped/pruned functions. This session adds synchronous queries;
it does not establish native vendor initialization or a complete application.

**Exact next browser boundary:** a separate source-derived Data+DOM session whose
nonmovable owner declares atoms, document, then Data state, with direct/member
calls that cannot independently escape. Preserve the existing owning-callable
Data contract. DOM keys need separate provenance and document+node identity;
foreign keys must either belong to the session's owned domains or refuse. Then
original Button/BaseComponent/Config construction and disposal, retained callbacks
and the native application driver.
**Exact next escape boundary:** a current-IR consumer connecting contents/identity
and index evidence to `TypeInference::isDenseVectorSite`, admission and emission.
The existing native query recognizes only direct array sites; structured-loop
transport needs live proof, not copied analysis-report attributes. Require a
preserved refused-to-emitted source case before counting native coverage progress.

## Monorepo review fixes and shared browser cores, 2026-09-13 UTC

Continued clean **ec5b3060**, resuming review **c0259f2a** at the user's request to
implement its findings. Sixteen focused implementation commits on
`codex-review-fixes-20260913` finish the eight correctness/tooling fixes, three
architecture follow-ups and five small simplifications. The [review resolution](reviews/2026-09-13-monorepo.md) maps every finding to its commit and validation.
No push. No native admission increase is claimed.

Core shutdown/capacity **e74f3d5a**, Canvas/Raster arithmetic **8ffe12b4**, HTTP
cap failures **b5e52c2d**, opened-stream asset reads **3198903b**, packaging close
failures **2d891f7b**, remote baseline identity **3935aac0** and comparison deadlines
**6d9fe47b** have regressions. **87a73e75** registers the Python checks in CTest.
The five equivalent cleanups remove 46 source lines; capture removal still uses
absolute target nesting, and Brew's GLM dependency remains.

**da27a512** moves shadow identity, attachment and composed root queries into
`document` / `read_txn`; bindings are adapters. **275642d0** moves easing and
interpolation into public Style; Promise/VM state stays in the adapter.
**d499b068** uses the existing CSS integer tokenizer and clamps large positive
step counts before narrowing, after Chromium caught an incorrect proposed rejection.
**06bff4cf** shares positional/flex shorthand assignment and names in Style core;
CSSOM/cascade ownership, validation timing, aliases and other differing policies
remain intact. These APIs are available to native callers without Script.

Measured gate: **1088-step build / 24/24 focused CTests in 1.56s**; full standard
**548/548 CTests in 1164.19s**, including **167/167 lit in 827.68s**. After the
Chromium-driven easing correction: **201-step rebuild / 176/176 browser tests in
66.00s**, packaging **1/1 in 0.31s**; ASan/UBSan **3/3 in 0.67s** for Core/shadow/
easing and TSan **1/1 in 2.17s** for Core. All 1408 final inputs match isolated
and devbox; final Core/DOM/Style client binaries contain no Script symbols.
Stable format **820 C++ / 88 Python / 33 web PASS**; bundled retains the same
9 baseline files / 26 diagnostics. Live Chromium startup/evaluation/timeout cleanup
and the real remote baseline collector also pass. Full WPT/test262 scores were not
remeasured; the review records the extreme Canvas rectangle's Chrome difference.
Evidence: `/tmp/ctcompile-review-fixes/`.

Fresh full Bootstrap remains **19/574, zero of 43 globals resolved**, both policies,
no skipped/pruned functions. Native DOM remains **10 sources / 28 refusals**.
Escape remains **895 observed sites / zero violations / 39 of 172 precision**.

**Exact next native boundary:** nonmovable generated atoms/document/Data ownership,
with member/direct session calls that cannot escape independently. Retained DOM
keys must preserve document plus node identity, detachment and equal-bit foreign
nodes. Then original Button/BaseComponent/Config construction and disposal.
**Next escape boundary:** original zero/+1 induction under a strict same-stable-array
length guard before bounded repeated-block execution; generic facts still need a
current-IR native consumer. This review maintenance does not complete those steps.

## Native attribute operations and bounded Number indices, 2026-09-13 UTC

Continued clean **03fa27f6**, resuming the native browser boundary in its latest
HANDOFF and the plan journal. Both histories showed the frame/parser fix complete;
September 7 WIP is already an ancestor. The unmerged lens branches concern a
closure-order bug already superseded by **6520fcf2**, not interrupted native work.
Three agents split public DOM extraction, native execution and the independent
escape prerequisite; root integrated in `codex-native-dom-ops-20260913`. No push.

**9e5888c3** lifts Element.toggleAttribute into public
`toggle_element_attribute`, using the existing document and attribute-name APIs.
The VM binding is a thin adapter preserving validation/conversion order,
requested-state returns, qualified namespace matching, no-op bytes, failed-write
handling and mutation notifications. ProcessingInstruction retains its distinct
force conversion. Native DOM/Core and VM regressions cover those behaviors.

**9a67fd2b** proves and emits `toggleAttribute`, `hasAttribute`, `removeAttribute`
and optional Boolean force for class toggles. Results can feed later forces.
Explicit undefined keeps the current adapters' behavior: omitted for classList,
false for Element.toggleAttribute. The DOM provider fixes the initial undefined
binding; complete source discovery rejects replacement/reentry, then private
preparation replaces proved reads with constants and reproves before publication.
This is not a claim that VM globals are immutable. Generated C++ uses public DOM
calls, ordinary element identities and Boolean/optional-Boolean values. The gate
rejects Script/AOT symbols and scalar value-model helpers in Boolean entries.

**f6d61d2a** carries exact bounded Number static-Add facts through the existing
immutable contents record, storage and simultaneous successor transport. Both
operands and sum must lie in [0, 2^32-1]; only proper array indices select elements.
Twenty-nine boundary cases use the existing transactional budget harness. Two
existing expectations change without changing their bodies: zero+zero Add gains
an exact index, while length+zero still refuses as a missing element. No branch
pruning, backedge admission, generic native consumer or source-fixture substitution.

**53a8f49a** resolves the requested strict review's one finding in the new
implementation: optional-force normalization now happens once in private DOM
preparation, using original proof plus clone mapping. Fingerprinting and reproof
still precede publication. No emitter side table, erased-force reconstruction
or force-specific branch remains in generic function lowering.

Validation: initial **398-step build**, then **59-step / 6 of 6 focused CTest
PASS in 63.66s**. First full standard gate **682-step build / 543 of 543 CTests
PASS in 1173.27s**, including **167/167 lit cases in 844.16s**. After the review
cleanup, unchanged focused **6/6 PASS in 64.21s**; final standard **238-step build /
543/543 CTests PASS in 1173.88s**, including **167/167 lit cases in 846.11s**.
The native matrix is **10 sources / 28 refusal controls**, both policies/layouts/
GCC13.3/configured Clang, **64.20s** focused and **98.85s** in the final full gate.
All **1398 frozen source/test/config inputs** match isolated and devbox trees.
Stable whole formatting passes **812 C++ / 86 Python / 33 web**; the required
bundled formatter has exactly the same **9 baseline files / 26 diagnostics**.
Full WPT/test262 scores were not remeasured. Evidence: `/tmp/ctcompile-native-dom-ops/`.

The user broadened the requested skills review to the entire monorepo.
[The review](reviews/2026-09-13-monorepo.md) records eight open correctness/tooling
findings, three architectural opportunities and five measured simplifications.
Prioritize the pre-existing scheduler teardown and slab capacity defects; Canvas
numeric bounds and output/resource failure handling also remain open. These
paths were not changed here. The scope inventory spans 1486 first-party files;
it is not exhaustive line-by-line coverage. Two Python-only tooling checks ran.
The c-review/code-improver workflows require an unavailable Workflow tool, and an
automated security filter stopped preparation of C++ reproductions; no sanitizer
confirmation or successful automated review loop is claimed.

Fresh complete Bootstrap remains **19/574 native / 0 of 43 globals resolved**,
both policies, no skipped/pruned functions. The complete escape oracle remains
**222 functions / 861 claims / 895 observed sites / 35 unclaimed / zero violations /
39 of 172 precision**. This adds synchronous actions, not retained DOM Data,
original Button construction, full Bootstrap initialization or an application driver.

**Exact next browser boundary:** a nonmovable generated owner holds atoms, document,
then Data/component state in that declaration order, so teardown destroys state
before document before atoms. Existing extracted Data callables can outlive their
owner; borrowing DOM keys into that carrier would dangle. Prove direct/member
session calls that cannot escape independently and distinct DOM-key provenance
preserving document plus node identity, including detachment and equal-bit foreign
nodes. Then original Button/BaseComponent/Config construction and disposal.
**Exact next escape boundary:** prove zero/+1 induction and a strict same-stable-array
length guard for original confinedArray before bounded repeated-block execution.
Its source and refusal remain intact; generic escape facts still lack a native consumer.

## Frame stylesheet failure fixed, 2026-09-13 UTC

After **c478d66f**, the user requested fixing the remaining `frames` failure before
ending this conversation. **53ec43be**, landed atomically as **fc902a6e**,
fixes the shared fragment parser path: `innerHTML` parsed a full document and
copied only its body, dropping a leading stylesheet routed into the scratch head.
The same defect affected `outerHTML` and `insertAdjacentHTML`.

Public `parse_html_body_fragment` now initializes the existing DOM tree builder
in body mode, preserving leading whitespace, comments and metadata in order and
ignoring document shell tags. All three bindings use it. Full-document parsing
and `document.write` keep their existing behavior. This helper covers the body's
context; table/select/raw-text fragment contexts remain separate work.

The original frame expectation is unchanged. Baseline failure was reproduced;
a **196-step focused build and 4/4 CTests in 0.11s** pass after the fix. Chromium
**151.0.7922.34** confirms all **18 new insertion observations** and the exact frame
result, `200px,50px,rgb(1, 2, 3),50,1,0px`. The final standard
devbox gate passed its **884-step build and 543/543 CTests in 1185.32s**, including
**167/167 lit cases in 837.34s**. All **1398 frozen inputs** match shared, isolated
and devbox trees.
Whole stable formatting passes **812 C++ / 86 Python / 33 web files**; the bundled
formatter retains the same **nine baseline files / 26 diagnostics**. Full
WPT/test262 scores were not remeasured. Evidence: `/tmp/ctbrowser-frames-fix/`.

Native Bootstrap and DOM entry scope are unchanged. Fresh full Bootstrap remains
**19/574 native / zero of 43 globals resolved**, both policies, with no skipped
or pruned functions.
The next native boundary remains retained DOM-backed Data keys with document
ownership outliving every key and future invocation, followed by original Button
construction and disposal. No push. Shared tree and synchronization claims are
clean at handoff; the next session can resume that native boundary.

## Typed native DOM entries and shared attributes, 2026-09-13 UTC

Continued clean **f7966251**, resuming the typed document/node entry promised by
its latest HANDOFF and the plan's current journal. Both commit histories showed
the token extraction complete; the September 7 WIP was already an ancestor.
Three agents split the public DOM extraction, strict contract proof and native
execution regression; root integrated and gated the isolated branch. No push in
this session; the current user instruction supersedes the historical push note below.

**e9d6a462** adds public `dom/element.hpp`: a borrowed document/node identity,
handle validation, and the attribute name/folding/setter behavior lifted from
Shell bindings. The binding is now a thin adapter over that core, retaining
conversion order, JavaScript errors and mutation notifications. DOM/Core-only
unit coverage checks document domains, detached nodes, stale handles, naming,
namespace behavior and same-value writes; the VM regression checks invalid-name
validation before value conversion. No Script types enter the public core.

**6b45ca17** proves a strict fingerprint-bound `ctbrowser-dom-v1` contract for
one synchronous entry and all its explicit element parameters. It accepts only
proved source receivers and a checked inert declaration wrapper; missing/stale
contracts, forged reports, skipped source, captures, prototype/method writes,
source calls and retention refuse. **ed184301** carries that live proof through
type inference and final admission to ordinary `ctbrowser::element_ref` C++.
The selected function exports without a launcher, validates incoming handles
before mutation, compares document-aware identities and calls the shared token
and attribute APIs. Boolean actions contain no boxed scalar helper or Script/AOT
symbol. [native-dom-entry.md](native-dom-entry.md) gives the contract and scope.

Validation: standard devbox **697-step build**, **542/543 CTests in 1183.20s**,
including **167/167 lit cases in 850.72s**. The sole failure is byte-identical
existing browser `frames.cpp:70`; no expectation moved. The new five-source DOM
matrix passes with GCC13.3 and configured Clang24, both optimization policies
and printing layouts, in **49.82s** under the full gate (**34.33s** focused).
It checks 16 document lifetime rounds, alias/cross-document identity, detachment,
ordered mutations, String/Boolean conversion, errors and ten refusal controls.
All **1398 final source/test/config inputs** match isolated and devbox trees.
Whole stable formatting passes **812 C++ / 86 Python / 33 web files**; the
required bundled formatter retains the same **nine baseline files / 26
diagnostics**, with changed C++ passing. Full WPT/test262 scores were not
remeasured. Evidence: `/tmp/ctcompile-native-dom-entry/`, including the emitted
`toggle.generated.cpp`, full gate, input hashes and fresh bundle census.

Fresh full Bootstrap **5.3.8 / 133701 bytes / SHA5b29f169** still admits **19/574**,
both policies, zero skipped/pruned and **0/43 globals resolved**. This standalone
action probe does not compile original Button construction or change the prior
ordinary-object Data probe into DOM-backed Data. Generic escape precision is
unchanged; no plan25 refinement gained a native consumer here.

**Exact next boundary:** retain actual DOM keys in the existing Data owner while
proving that each document outlives every key and future invocation. The current
DOM entry borrows only for one call and cannot combine with a closed-source
Data owner contract. Preserve both document and node identity with ordinary C++
ownership. Then admit original Button's `this._element`, BaseComponent/Config
construction, prototype/static getters and disposal. Shell observers/custom
reactions, retained events, DOMContentLoaded, timers, layout and the native
application driver remain outside this synchronous action milestone.

## Native Bootstrap survey and shared DOM token API, 2026-09-13 UTC

Continued clean **a4458ae1**. The lookup-mode thread was already committed and merged;
its final sync claims were the interrupted remainder, now released. The old September 7
WIP is already an ancestor. The user requested an honest Bootstrap survey, then directed
continued API adaptation and authorized pushing `ctcompile-v1` after the gates.

[bootstrap-native-next.md](bootstrap-native-next.md) records the browser critical path.
Full Bootstrap **5.3.8 / 133,701 bytes / SHA5b29f169** remains **19/574 native**, both
policies, no skipped/pruned functions and **0/43 globals resolved**. The 7/7 browser
UMD result is the source-derived Data probe with ordinary object keys, manifest/prefix
and explicit1m budget; it does not establish real Window/DOM/component execution.
Fresh full-source IR exposes **272 this-receiver / 137 own-closure / 70 boxed-parameter /
29 passed-closure / 15 lexical-this** leading refusals. Generic plan25 contents/verdict
refinements have no native-emitter consumer; current vector admission uses the separate
TypeInference proof. CommonJS publication is useful independent work, not the next
browser capability.

**1e71c6ad**, landed atomically as **29a76492**, lifts ordered token parsing, validation,
attribute updates and toggle from the VM binding into public `dom/token_list.hpp` and
`lib/DOM/token_list.cpp`. The associated attribute is generic: classList, blocking and
class-name collections reuse the core. The binding retains conversion, exception and
mutation notification behavior, including current omitted-token conversion, validation
priority, force/no-op handling and stale-write results. No Script type or context enters
the core. Two regressions exercise real document/node handles and the VM adapter. The
new native unit's normal CTest target links only DOM/Core plus configured test support.
This is a shared native API milestone, **not compiler DOM admission or a compiled Button**.

Validation: baseline standard build and **539/540 CTests in 1125.70s**, lit **167/167 in
808.86s**. Final isolated **1067-step build** and **540/541 CTests in 1133.92s**, lit
**167/167 in 807.09s**. The only failure in both runs is byte-identical browser
`frames.cpp:70`. The subsequent test-link-only CMake change passed a two-step build and
**1/1 CTest in 0.01s**; no production code changed after the full gate. Both standalone
Clang and GCC13.3 core/client checks pass, with no Script/AOT symbols; the ordinary
CTest binary passes the same symbol check. All **1358 final inputs** match local,
isolated and devbox trees. Whole stable formatting passes **807 C++ / 85 Python /
33 web files**; required bundled formatting retains the same **nine baseline files /
26 diagnostics**, with all changed C++ passing. Full WPT/test262 scores were not
remeasured. Evidence and reproducible link commands: `/tmp/ctcompile-bootstrap-survey/`.

**Exact next compiler boundary:** typed native document/node entry capability and real
DOM-backed Data keys, preserving document identity/domain, node identity and detached-node
lifetime. Then compile original Button.toggle against the shared token API, explicitly
labelling an action-only milestone until original BaseComponent/Config construction,
prototype/static getters and disposal are admitted. Retained events, DOMContentLoaded,
lexical-this callbacks, transitions/layout and the native application driver remain.
Use public ctbrowser cores; the existing boxed AOT ABI and Script-linked Shell are not a
native entry. The user-authorized branch push follows the final handoff commit.

## Source lookup provenance and native browser UMD, 2026-09-13 UTC

Resumed the interrupted 13:04–13:09 UTC thread from dirty compiler files, the existing
`codex-lookup-20260913` worktree and the sync journal's explicit 13:09:29 abandonment.
Shared HEAD was **79cc7999**; the old September 7 WIP was already merged. Three agents
recovered source-lookup tests, the isolated runtime change and immutable escape facts.
Root integrated and gated the combined tree.

**5f27789b**, landed by atomic merge **f4e1bfd4**, preserves source `typeof
IdentifierReference` through dedicated `get_global_typeof` bytecode. Comma expressions,
saved locals and conditional expressions perform ordinary reads, which throw for absent
names. Parenthesized identifiers yield "undefined". TDZ and throwing getters still
throw. The old adjacent-opcode inference is removed. The existing soft AOT helper now
advertises throwing effects; its caller checks exception status before using the raw
value. This intentionally corrects the VM toward Node semantics, as coordinated in
AGENT-SYNC. Inventories are **94 opcodes / 69 result writers**.

**ff57f36b** carries default-hard `typeof_lookup` provenance through CTJS import,
skipped-body effect census and boxed lowering. **beb37605** updates the shared
independent escape checker inventory; source and snapshot rows stay intact. **a30d8669**
requires that provenance for absent-binding proof and prefix specialization. Only a
fully validated private owner clone materializes declared absent soft reads as
undefined, charging work before mutation and reproving before publication. Hard absent
reads stop prefix progress and remain compile-time refusals. **b4146261** exempts only
an exactly proved source-created ordinary `globalThis`/`window` object from the coarse
realm-write rule; incomplete, stale or unproved sibling accesses still box scalars.
**5deee2a3** marks unused calls after dead-store cleanup, retaining their effects
without unused C++ results.

The original browser UMD is unchanged: **3218 bytes**, SHA256
**80a6fd87cbfdaafa6b2a3c6aab05bfc66ca36bf23f3827c29b4fe79b93ab3722**. With the existing
declared manifest and prefix specialization it now executes **7/7 native functions at
explicit 1,000,000 steps**, both optimization policies, both printing layouts and
GCC/Clang. All **19 typed Node/VM observations**, **23 Data calls** and the payload
field read remain. No-Script symbol checks, ASan/UBSan/leak checks and **1024 future
lifetime rounds**, reentry and final-owner release pass. Raw input/default 100k, missing
declarations, stale evidence, observed fallback/arguments, ordinary absent reads and
source writes still refuse. Fresh forged reports cannot change the emitted program. The
prior safe wrapper remains 4/4. The default budget is unchanged.

**63770328** recovers immutable held escape facts: original producer, primitive category
and optional saved String/array length travel together, including simultaneous successor
assignment. Six regressions cover swaps, opaque replacement, stored primitive
categories, saved lengths and continued backedge refusal. The complete oracle snapshot
remains **222 functions / 861 claims / 895 sites / 35 unclaimed / zero violations /
precision 39/172**. The final fixture gate passed in **2.01s**. No loop admission or
escape precision increase is claimed.

Validation: focused source-mode, type-inference, cleanup and original UMD gates pass.
Standard devbox configure/build passed. The full run was **538/540 CTests in 1129.83s**,
including **166/167 lit cases in 806.98s**. Failures were the unchanged browser
`frames.cpp:70` baseline and one old absent-read diagnostic assertion. Updating only
that assertion passed the filtered gate **1/1 in 16.04s**. All compiler CTests and **167
lit cases** have passing coverage across those runs; only the unchanged browser failure
remains. Fresh full Bootstrap stays **19/574 native / zero of 43 globals resolved**,
both policies. Original 2522-byte Data stays **7/7 at explicit 1m with declared
undefined / 0/7 at default 100k**; its empty manifest still gives 0/7. Fresh CommonJS
and realm fallback remain **0/7**, raw and prefix-specialized, at 1m with optimizations
disabled. All **1273 frozen compiler/test/tool inputs** match the isolated tree and
devbox; compiler/tool copies also match the shared tree before integration. The required
bundled formatter retains **nine pre-existing files / 26 diagnostics**; changed C++
passes, and whole Homebrew 23.1.1 formatting passes **804 C++ / 85 Python / 33 web
files**. Evidence is in `/tmp/ctcompile-lookup/`. No push.

Next native: the unchanged **3117-byte CommonJS / SHAcc6c3960** provider. Preserve both
writes to `module.exports` and `exports` retaining the first object; extend existing
per-read publication history, not just the single-write predicate in
`OwnedGlobalRoots.cpp`. Actual realm fallback (**3568 / SHAa8dd4151**) remains a
separate typed embedding-ownership/entry-ABI boundary; prefix realm tokens do not
provide native storage. Browser APIs must still call public ctbrowser RAII seams.

Next escape: original `confinedArray`, **118 bytes / SHA7bc1160c**, still fn2/pc3
`escapes:passed`. Held facts are now available; prove literal-zero/+1 Number induction
against the same stable dense array's strict `< length` guard, then preserve exact
element alternatives under the unchanged budget/effect checks. Do not remove the
visited-block refusal without that finite-bound proof. The original CFG and eight
controls remain in `/tmp/ctcompile-umd/escape-loop.md` and `escape-loop-controls.js`.
Full native Bootstrap and the application driver remain unfinished.

## Browser UMD ownership and safe preparation, 2026-09-13 UTC

Continued clean **4a5ff450**, resuming its promised original browser UMD boundary
from HANDOFF and `/tmp/ctcompile-data-number/next-bootstrap.md`. Both commit logs
and the synchronization journal showed the preceding Number thread completed;
`codex-wip-20260907` is already an ancestor. Three agents covered proof tests,
exact-source execution and the independent escape-loop/provenance review. Root
integrated the results and serialized every devbox and Git operation.

**01238f3a** follows the factory through its exact explicit wrapper formal instead
of requiring parameter 3 in a four-argument wrapper. Existing unique invocation,
closed callable and actual/formal arity proofs remain mandatory. Five additional
source/prepared/imported shapes exercise later and trailing formals, with exhaustive
incomplete/exact budgets, stale/forged evidence, swapped actuals, escaping callbacks
and repeated calls. **278d4322** admits only nonescaping `typeof` inspection of
proved ordinary root aliases; source/prepared mutations to coercing unary Plus
still refuse. Original fixture bodies are unchanged.

**2205942e** prepares only a privately validated owner clone: `typeof` of an exact
ordinary owner or its checked global load becomes String `"object"`, and unused
explicit wrapper slots are erased together with their exact actual operands.
Every evaluated producer remains. Fields are not assumed to be objects; observed
fallbacks and raw argument windows are not erased. Collection is budgeted, and
complete ownership is rechecked before publication. Fixed-present-undefined
normalization remains; **absent loads are not folded or erased**.

The original browser UMD remains **3218 bytes**, SHA256
**80a6fd87cbfdaafa6b2a3c6aab05bfc66ca36bf23f3827c29b4fe79b93ab3722**. With its
existing declared manifest and prefix specialization, complete native ownership
now succeeds at explicit **1,000,000 steps**, both optimization policies; it still
emits **0/7 native functions**. Raw prepared input and the unchanged default100k
remain owner-refused and 0/7. Before primitive preparation, the complete specialized
owner used **875,901 steps**. Prefix reports alone confer no native authority.

The registered `global-maps.py --group umd` gate preserves all **19 typed Node/VM
observations** and **23 Data calls in order**. It covers missing declarations,
0/32/default100k budgets, stale/fresh forged evidence, observed fallback returns
and publication, ordinary absent reads and source writes. A separate four-function
wrapper is **4/4 native**, both policies/layouts, GCC/Clang and no-Script checks:
its unused `(tracePre = 1, this)` actual still performs the visible assignment,
and its ordinary-root type observation stays correct. Returning the fallback,
reading `arguments.length`, and incrementing a twice-written scalar global remain
refused. Original UMD bytes and all earlier recorder sources remain intact.

Focused gates passed: **257-step build / 3 CTests in 41.74s**, then **256-step
build / 2 CTests in 1.93s**; final consumers rebuilt in **241 steps**, with
**2/2 CTests in 1.95s** and the complete new UMD group passing. The new harness
was corrected to accept rollback's original diagnostic and to compare canonically
printed SSA operands after inserting a result. No production diagnostic edits remain.
The devbox connection recovered through the authorized start/allow-ip workflow.

The standard devbox configure/build passed. The first full run was **538/540
CTests in 1098.92s**, with **165/166 lit cases in 785.36s**. Failures were the
unchanged browser `frames` baseline and `global-methods.test`'s hard-coded
480–544 preparation-budget window. **3b614722** replaces that window with the
bounded binary search already used by the neighboring global test. Source fixtures,
partial-component rejection and unchanged-operation rollback checks remain; the
measured adjacent boundary is **547 rollback / 548 complete**. The corrected
filtered CTest passed **1/1 in 33.57s**, with lit **33.50s**. Across those runs,
every compiler CTest and all **166 lit cases** have passing coverage. The sole
remaining failure is `frames.cpp:70`, with the same embedded-style/viewport mismatch.

The escape fixture passed in **1.74s**; no escape code or snapshots changed.
Fresh full Bootstrap remains **19/574 native / zero of 43 globals resolved**, both
policies. Original 2522-byte Data remains **7/7 at explicit 1m with declared
undefined**, **0/7 at default100k**, both policies; its empty manifest still refuses.
Other recorder/template admissions and original source hashes are unchanged.

All **1286 final inputs** match locally and on the devbox. Production stayed fixed
after **2205942e**; the only later test change is **3b614722**. Required
`tools/format.sh --check` retains exactly **nine baseline files / 26 diagnostics**
with bundled 23.0.0git; changed C++ passes. The whole check with Homebrew **23.1.1**
passes **804 C++ / 85 Python / 33 web files**. Logs, both frozen manifests and
`final-measured.json` are under `/tmp/ctcompile-umd/`.

The next native boundary requires **source reference-mode provenance**, not a
broader TypeOf-use heuristic. Direct `typeof missing` returns `"undefined"`;
`typeof (0, missing)` and `var x = missing; typeof x` must throw ReferenceError.
Fresh devbox imports reduce all three to a LoadGlobal-to-TypeOf use graph. Measured
`traceCaught` is **0/0/1 in the VM versus 0/1/1 in Node**: moves lose the alias's
hard-read behavior, and the VM's adjacent-op heuristic also misclassifies comma.
An experimental absent-TypeOf fold was removed before committing. The existing
host queries and boxed soft-global lowering have the same missing provenance.

Coordinate the real lookup mode from Script `compile/expressions.cpp`, through
`bytecode_opcodes.def` / VM `run_loop.cpp`, CTJS `Import/Bytecode/Instructions.cpp`
and `Ops/Bindings.td`, then make both boxed and native consumers use it. The sync
journal records this for Claude; `/tmp/ctcompile-umd/absent-typeof.md` and
`typeof-measured/` preserve exact sources, hashes, raw IR and observations.
CommonJS replacement/old-exports aliases and realm embedding remain separate work.

Escape production and snapshots are unchanged. The untouched 118-byte confinedArray
still needs immutable held scalar facts, simultaneous successor transport and a
verified Number induction/guard relation before bounded read-only-loop admission.
Eight source controls pass; no escape precision gain is claimed. The exact CFG,
backedge hazards and first implementation step are in `/tmp/ctcompile-umd/escape-loop.md`.
Full native Bootstrap and the application driver remain unfinished. Browser APIs
must use public ctbrowser RAII subsystem headers. No browser/runtime/parser edits
or push; all session evidence is under `/tmp/ctcompile-umd/`.

## Exact Data Number observations, 2026-09-13 UTC

Resumed the interrupted **10:38:21 / 10:39:13 UTC** Number-result thread from
**01073560** and the synchronization journal's explicit **10:40:15** abandonment.
The checkout was clean; the old `codex-wip-20260907` is already an ancestor.
Three agents split producer/query, source/prepared tests and exact-source execution;
root integrated them and ran all devbox/Git operations. Follow-up review agents
hit rate limits, so root completed the review and next-boundary audit locally.

The predecessor's **01073560** already materializes contract-declared fixed
undefined reads on a privately validated clone, with complete ownership before
and after rewriting. Its focused build/three owner CTests and `globals.test`
passed. Missing/absent bindings, source writes and stale/exhausted proofs remain
refused. This session resumed the remaining scalar boundary instead of repeating
that implementation.

**62838bcf** retains each complete entry-order invocation's primitive alternatives
beside its exact returned-leaf edge. Only complete scans publish; a direct scalar
store can demand the scan without an unrelated field read. Whole-owner validation
checks unique actual calls and rejects conflicting scalar/leaf edges, then indexes
queries by exact result. Generalized parameter/return evidence stays separate.
The captured result moves into its callable edge instead of copying the new vector.
Fourteen source/prepared controls cover Number versus object results, six falsy
values, missing/alias/deletion/reinsertion, stale/forged evidence, incomplete/exact
budgets and a later unsupported invocation that discards all optional facts.
The original direct mixed-return row now gains one independently proved leaf;
its fixture and all earlier source literals remain unchanged.

**6c56354d** consumes only exact Number evidence at the actual StoreGlobal use.
Object-family admission, the complete global store census and emission agree on
that use; every other store still requires its own proof. The reusable getter,
its parameters and its mixed result ABI remain broad. A checked Number extraction
borrows the existing object/scalar carrier; no generic object coercion, runtime
context, Script dependency or browser implementation was added. All source calls,
recorder callbacks, ownership and field reads remain live.

Original full Data is still **2522 bytes / SHA256
8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3**. With
`undefined_bindings: ["undefined"]`, it now emits **7/7 native functions under both
optimization policies at explicit 1,000,000 steps**. It remains **0/7 at the unchanged
100,000-step default**, and the original empty-manifest case remains **0/7** at
both budgets. All five original recorder source definitions and their pins match.
The generated entry retains **23 method calls in order / one field read**.

Focused validation: **256-step rebuild / 3 of 3 CTests in 237.08s**. The recorder
gate passes **10 native programs / 32 refusals / 394 typed observations / 13
mutations**. Both C++ layouts, GCC/Clang, no-Script and ASan/UBSan/leak checks pass,
including **1024 mixed future rounds**, zero-to-null, saved leaves across scalar
overwrite/deletion, detached callables, entry reruns and final owner destruction.
Three new same-global Object/missing/zero stores refuse even after restoring the
final Number observation. The longer zero control uses explicit **2m** to reach
its semantic refusal; default and original exact Data budgets are unchanged.
The new default100k check asserts unchanged calls and failed ownership: a failed
private preparation can report its original property-receiver refusal, not the
private clone's exhausted budget.

Final standard devbox gate: configure/build passed (no remaining Ninja work),
then **539/540 CTests in 1092.70s**, including every compiler CTest and **166/166
lit cases in 768.42s** (CTest wrapper 768.49s). The sole failure is the unchanged
browser `frames` baseline at `unittests/unit/frames.cpp:70`: embedded style is
missing and viewport measurements disagree. Escape fixture CTest passed in
**1.71s**. Fresh full Bootstrap remains **19/574 native / zero of 43 globals
resolved**, under both optimization policies.

All **1285 frozen inputs** match locally and on the devbox. Required
`tools/format.sh --check` with bundled **23.0.0git** retains **nine baseline files /
26 diagnostics**; every changed C++ file passes. The same whole check using
Homebrew **23.1.1** passes **804 C++ / 84 Python / 33 web files**. No production
or test input changed after **6c56354d**.

Fresh native-manifest UMD probes remain **0/7** with `optimize=false` and explicit
1m, before and after existing prefix specialization: CommonJS **3117 bytes /
cc6c3960**, ordinary browser **3218 / 80a6fd87**, realm fallback **3568 / a8dd4151**.
`tools/check/bootstrap-host-prefix.py` deliberately omits a native manifest in
its standard census; passing a freshly fingerprinted manifest alone does not
close this boundary. These are its original `_provider_objects` cases.

Next native subject: the unchanged **3218-byte ordinary browser UMD** source.
Its wrapper has five CTJS arguments, factory at `%arg4`, and an unused fallback
actual still reading script `this`. `HostContract/Values.cpp` recognizes indirect
factories only at `%arg3` of a four-argument wrapper;
`Analysis/OwnedGlobalMethods.cpp` permits only three/four wrapper arguments.
Selected wrapper code also retains absent-binding `typeof` expressions.
Reuse exact actual/formal and complete live ownership proofs, preserve evaluation
effects, and never turn a general absent load into present undefined. CommonJS
additionally replaces `module.exports` while retaining the old `exports` alias;
realm fallback needs actual typed embedding ownership. Do not use prefix reports
as authority or rewrite fixture bytes. `/tmp/ctcompile-data-number/next-bootstrap.md`
contains measured pins, exact code seams and the separate proof obligations.

Escape production/fixtures are unchanged. The original confinedArray loop/index
invariant and Claude-coordinated literal own-definition provenance remain the next
independent work; no primitive/index exemption or precision gain is claimed here.
Full native Bootstrap and its application driver remain unfinished. Browser APIs
must continue through public ctbrowser RAII subsystem headers. No browser/runtime/
parser edits and no push. Logs, frozen inputs and generated C++ are under
`/tmp/ctcompile-data-number/`.

## Distinct Data entry keys and scalar snapshot payloads, 2026-09-13 UTC

Continued clean **cef8c834**, resuming its exact full-Data next step from the
latest HANDOFF, both commit logs and the synchronization journal. No interrupted
compiler edits remained; `codex-wip-20260907` was already an ancestor. Three agents
worked independently on invocation tests, execution and source escape soundness;
root integrated their changes and serialized all Git/devbox operations.

**27de9b13** extends the existing optional invocation analysis with canonical
entry object-key identities, exact/read-time Map-size equality and truth retained
through a selected yield. Only independently checked entry allocations dominating
the call prove distinctness; aliases, method-local sites and schemas do not.
Eleven source/prepared controls cover aliases, unknown keys, child recreation,
wrong-key deletion, saved size, reversed/negated branches and inactive effects.
Live mutations and incomplete/exact budgets pass. Focused **258-step rebuild /
7 of 7 CTests in 244.62s** passed.

**0f664eb5** passes complete live captured-snapshot read edges into the existing
scalar-field environment. This lets ordinary `{value: 64}` payloads coexist with
`Array.from(s.keys())[0]`; numeric keys elsewhere and prototype effects still
refuse. Getter ABI, emitted source calls and runtime storage stay broad. No new
runtime helper, Script dependency or browser implementation was added.

The separate exact-prefix entry-object probe is **1999 bytes**, SHA256
**5dc98179c1a06bcac5b7136ce8a86d11690e7a7e93de8229e82488c09c52e030**. It retains
**16 method calls / five field reads**, distinct keys and aliases, conflict and
wrong-key effects, mixed Object/Number payloads, deletion and reinsertion.
Measured **7/7 native at explicit1m**, **0/7 at unchanged default100k**, both
policies. Final recorder execution passes
**9 native programs / 30 refusals / 317 typed observations / 13 mutations**,
both layouts, GCC/Clang, no-Script and ASan/UBSan/leaks. The new future harness
runs **1024 rounds**, retains saved leaves across scalar overwrite/deletion,
and checks detached callables, entry reruns, independent owners and destruction.

**4b002f13**, committed first, conservatively repairs the optional escape contents
query: both non-BigInt BinaryStatic operands need independent primitive origins.
Fresh objects/arrays can invoke inherited conversion hooks that retain a child;
Node counterexamples and compiler mutation controls cover both operands. Generic
VM-relative BinaryStatic effect metadata is unchanged and remains a separate
source-semantics coordination issue. The complete fixture snapshot is byte-identical
to the predecessor: **1123 rows / 112,001 JavaScript bytes / 222 functions /
861 claims / 895 sites / 35 unclaimed / zero violations / precision 39/172**.
Fixture rerun **1.14s**, 13 evidence mutations, eight malformed controls and the
three vendor escape gates passed. No precision increase is claimed.

The original full Data source remains **2522 bytes / SHA8359592c / 0 of 7 native**.
A separately declared-manifest control now uses the existing
`undefined_bindings: ["undefined"]` without changing those source bytes. At1m
both policies complete ownership, including distinct keys and the returned leaf;
the original empty manifest still refuses. The next actual native refusal is
`standard Map identity is unproved with other host/global value reads`:
`Analysis/NativeMap.cpp` does not consume fixed-undefined host evidence. The older
property-receiver diagnostic was fallback after speculative preparation failed.
All temporary diagnostic edits were restored to committed bytes before the gate.

Final **259-step build** passed. Standard CTest first ran **538/540 in 386.10s**:
**165/166 lit cases** passed; failures were the existing browser `frames` test and
the new declared-manifest harness wrongly requiring unprepared call operands after
successful ownership. **b74cd063** fixes only that new control: all source calls
and the **23 entry method calls/order** survive, and fresh forged reports leave
the prepared operands unchanged. The complete corrected `global-maps.test` then
passed filtered CTest: **1/1 in 711.46s**, lit **711.38s**. All compiler CTests and
all **166 lit cases** have passing results across these two runs; the sole
remaining failure is the unchanged browser `frames` baseline.

All **1285 final frozen inputs** match locally and on the devbox. The required
bundled **23.0.0git** formatter retains **nine baseline files / 26 diagnostics**;
changed files pass. Whole formatting with Homebrew **23.1.1** passes
**804 C++ / 84 Python / 33 web files**. No production input changed after
**0f664eb5**; only the new regression changed after the first full run.

Fresh Bootstrap remains **19/574 native**, both policies. Fresh exact-source
measurements under both policies (default stays 100,000 steps):

| Source (SHA256 prefix) | Bytes | Default 100k | Explicit 1m |
|---|---:|---:|---:|
| Single recorder (`4ebf1cd4`) | 1327 | 7/7 | — |
| Repeated recorder (`45d621b3`) | 1733 | 0/7 | 7/7 |
| Entry objects (`5dc98179`) | 1999 | 0/7 | 7/7 |
| Full Data, either manifest (`8359592c`) | 2522 | 0/7 | 0/7 |
| Captured child template (`e39ea887`) | 1303 | 6/6 | — |
| Captured outer template (`db9730ef`) | 1108 | 0/6 | 6/6 |
| Original returned field (`9d4c8b9c`) | 595 | 5/5 | — |
| Recreated object (`3385321d`) | 660 | 6/6 | — |

Full Data ownership is **false at default**, with either manifest; it is **true at
1m only with declared undefined**, while native emission remains refused.
The browser `frames` baseline is still `0px,184px,rgb(0, 0, 0),184,0,8px` versus
`200px,50px,rgb(1, 2, 3),50,1,0px`, with zero frame style elements.

Next native work: in `LowerToEmitC.cpp`'s private validated clone, after the first
complete owner proof, materialize only contract-declared present-undefined loads
as ordinary UndefinedAttr constants. Charge the collection, recompute the derived
fingerprint and reprove before preparation/publication; preserve missing, absent,
reassigned, stale and exhausted controls. Existing constant paths then serve Map
recognition, typing and emission. This is a proposed next step, not measured code.

Next after that: `HostContract/Values.cpp` currently discards each invocation's
PrimitiveAlternatives while retaining its exact returned-leaf edge. Carry complete
Number evidence to the precise `StoreGlobal` observation (first `traceGet`), keeping
the getter/call ABI broad and the whole global store census consistent. Preserve
source pins, recorder effects, falsy/missing controls and future calls. Do not
replace source `undefined` with `void 0` or narrow a signature to one observation.
Read `/tmp/ctcompile-data-distinct/next-data-final.md` for exact producer/consumer
paths and trust constraints; subsequent native admission is not yet measured.
Full native Bootstrap and the application driver remain unfinished; browser
calls must continue through public ctbrowser RAII subsystem APIs.

Next escape prerequisite is still literal own-definition semantics/provenance for
`objectFrameDeletedChild`, **fn15/pc2 / 138 bytes / SHA88644673**. Coordinate with
Claude before importing that fact; fresh allocation does not bypass inherited
assignment setters. Opaque conversions need primitive proof, and original
`confinedArray` needs an actual loop/index invariant. Read-only audits, generated
C++, frozen inputs and all logs are in `/tmp/ctcompile-data-distinct/`.
This entry supersedes the prior distinct-key/size next step. No browser/runtime/
parser files changed; no push.

## Per-invocation Data returns and private arrays, 2026-09-13 UTC

Resumed the interrupted **07:40:50 UTC** thread, explicitly abandoned at
**07:43:17 UTC**, from **a8f7f722**, one dirty `EscapeAnalysis.cpp`, and the
synchronization journal. Saved predecessor bytes before editing. The old
`codex-wip-20260907` is already an ancestor. Three agents split array completion,
return execution and proof review; root recovered the rate-limited execution
work, integrated the results and ran every devbox gate. The interrupted array
change was committed first. This entry supersedes the earlier pending private
array and 595-byte returned-field instructions.

**97e8620c** extends the completed array retention consumer to private fresh
GetProperty/SetProperty receivers. It retains generic sink roles, complete
contents/effect/exit-reachability proofs, all-write cycle rejection, transactional
budgets and the Stored-only counter. No Stored child is needed to start the query.
The **257-step build**, arrays **122 rows / 33 live states / 3801 retention
cutoffs**, and all three vendor escape gates pass. The fixture rerun passes in
**1.13s**, including **13 evidence mutations / eight malformed controls**.

All **112,001 fixture JavaScript bytes** and old pins remain unchanged. Exactly
**12 of 1123 complete snapshot rows** change from Passed to Confined, all private
arrays: fn/pc **8/8, 187/8, 191/8, 194/8, 197/5, 199/11, 201/5, 203/11, 207/11,
208/6, 213/10, 217/14**. All other coordinates, claims, counts and runtime routes
match. Oracle: **222 functions / 861 claims / 895 observed sites / 35 unclaimed /
zero violations / precision 39/172**. Returned containers and children retain
their escape verdicts.

**556494ad** proves exact caller leaves returned by individual entry calls.
It reuses `capturedMapBody` transfers after the independent complete family proof,
in entry execution order rather than the category worklist's method order.
Child identity includes the constructor and invocation; stored keys resolve to
literal or canonical entry actuals, and `has` observations expire between calls.
Persistent-state copies are charged before copying. Unknown branches and local
leaf constructors discard optional invocation evidence. Complete family checks
still cover every arm and future supported arguments. Nothing is published from
an incomplete prefix or exhausted budget.

`HostReturnedLeaf` carries live source edges through complete ownership validation
into field admission. The getter's broad mixed return ABI, emitted source calls,
mutations and field reads remain intact; no runtime helper or VM dependency was
added. **573a254e** preserves all **79 old source bodies and SHA pins**, adds four
scalar-overwrite/child-recreation/inactive-effect controls, and executes the new
boundary. Three older safe field probes now admit because their actual receivers
are objects; missing-field and scalar-receiver probes still refuse. Source/prepared
ownership tests retain every original fixture literal and unsafe scope mutation.

Final focused gate: **255-step rebuild / 3 of 3 CTests in 245.37s**. The original
`nested_map_mixed_child_returned_field` remains **595 bytes / five functions /
16 calls / SHA256
9d4c8b9c5e52297f6dd7f37f2a3a8c1a955f0d8348912cf35365de1ec00f57a0** and is now
**5/5 native under both optimization policies at the unchanged 100k default**.
Its first complete budget is **34,903 steps**, with **31 cutoffs**, exact completion,
stale/forged evidence, both C++ layouts, GCC/Clang, no-Script and sanitizers checked.
The complete nested group passes **46 native programs / 37 refusals / 311 typed
observations / 208 distinguishing mutations**; its existing **128 future-call**
witnesses cover mixed results, copied/detached leaves, deletes, replacement,
entry reruns, independent owners and final destruction.

Fresh full Bootstrap remains **19/574 native**, both optimization policies.
Exact-source census, also measured under both policies:

| Source (SHA256 prefix) | Bytes | Default 100k steps | Explicit 1m steps |
|---|---:|---:|---:|
| Original single recorder (`4ebf1cd4`) | 1327 | 7/7 | — |
| Original repeated recorder (`45d621b3`) | 1733 | 0/7 | 7/7 |
| Exact full Data (`8359592c`) | 2522 | 0/7 | 0/7 |
| Captured child template (`e39ea887`) | 1303 | 6/6 | — |
| Captured outer template (`db9730ef`) | 1108 | 0/6 | 6/6 |
| Original returned field (`9d4c8b9c`) | 595 | 5/5 | — |
| Recreated object control (`3385321d`) | 660 | 6/6 | — |

The compiler default stays at 100,000 steps. Full Data retains
`property receiver lacks a fresh own-data object proof`; its SHA256 remains
`8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3`.

Final standard full gate at **573a254e**: **539/540 CTests in 1031.06s**;
all compiler tests pass, including **166/166 lit cases in 720.11s** (CTest
**720.42s**) and the complete `global-maps.test`. The sole failure is the existing
browser `frames` test, with unchanged actual/expected output. All **1285 frozen
inputs** match locally and on the devbox. The required bundled formatter
**23.0.0git** retains the same **nine baseline files / 26 diagnostics**; changed
files pass. The whole check with Homebrew **23.1.1** passes **804 C++ / 84 Python /
33 web files**.

The browser failure still observes `0px,184px,rgb(0, 0, 0),184,0,8px` instead of
`200px,50px,rgb(1, 2, 3),50,1,0px`, with zero frame style elements.

Next native boundary: preserve the exact full Data source and extend optional
actual-call facts to prove distinct caller object keys and size/equality branches. Then supply proved Number extraction at scalar uses of
the broad mixed getter carrier; do not narrow just a CallDirect result or the
reusable getter signature. Full Data still requires its recorder, wrong-key,
conflicting-set, remove/reinsert and independent-owner behavior. Browser APIs
continue through public ctbrowser RAII subsystem APIs; this source-owned Data
increment required no browser extraction.

Next source-sound escape prerequisite is original `objectFrameDeletedChild`,
**fn 15 / pc 2**, **138 bytes / SHA256
886446734779abe3b1a5ca6a0ef74012f24f8ca9aed5cb272b642a400d0c0616**. Its child is
observed confined but Stored; its returned container remains escaping. Literal
own-property definition semantics/provenance are currently erased into generic
SetPropertyOp, so the existing refusal is correct. Coordinate that semantic seam
with Claude before reusing object contents/deletion/reachability. Do not exempt
opaque unsigned shifts using the current VM's static coercion: JavaScript object
operands can execute user conversion, which requires independent operand proof.

Evidence: `/tmp/ctcompile-data-return-finish/` contains saved predecessor bytes,
frozen manifests, source pins, complete array delta, focused/full logs, generated
C++, fresh census and the `next-data.md`, `next-escape.md` and
`return-proof-review.md` audits.
No browser/runtime/parser files changed; no push. Full native Bootstrap and the
application driver remain unfinished.

## Original recorder callbacks and String offsets, 2026-09-13 UTC

Resumed the interrupted **05:44:18 UTC** recorder thread, explicitly abandoned
at **05:47:51 UTC**, from **07863005** and its 15 dirty tracked paths plus
`ScalarCallbacks.cpp`. Predecessor bytes were saved before editing. The old
`codex-wip-20260907` is already an ancestor. Three agents split ownership tests,
callback review and the independent escape proposal; root integrated and gated
all changes. The recorder work was committed before the escape increment.
This entry supersedes the pending-recorder/String-offset instructions below.

**0246604d** proves the source-owned `console.error` callable separately from
exported methods and pure snapshots. Evidence records the unique initialized
own slot, exact zero-capture target, all String arguments and callback arms,
and both Number globals' initializers, reads and writes. Replacement, unknown
calls, reentry, unsafe inactive arms and captured Map/caller-object access remain
refused. Existing nullable Number storage preserves initial Undefined; ordinary
sole-store scalar and type-inference rules are unchanged.

**a1137fc2** reuses existing local-cell and unused lexical-receiver normalization
on a fingerprint-validated clone, then requires fresh complete owner proofs before
publishing it. Existing lifting privatizes the source callback target, and existing
closure carriers emit its owner field. Pure entry selections are admitted; entry
calls/stores/allocations still require unconditional positions. Old conservative
selection assertions were updated; invalid scope and conditional-effect controls
remain. **32dcf4d5** supplies the source-preserving execution/lifetime checks.

Focused final gate: **257-step rebuild**, **3/3 ownership/host CTests in 238.33s**.
Callback tests cover **23 source / 25 prepared rows**, two live mutations each,
stale/forged evidence and incomplete/exact budgets. Recorder group: **eight native
programs / 27 refusals / 225 typed Node/VM observations / 11 distinguishing
mutations**. Both policies/layouts, GCC/Clang, no-Script symbols and ASan/UBSan/leaks
pass. **1024 future rounds / 2048 callback effects** cover copied String storage,
entry reruns, old/new callable families using current globals, detached callbacks
outliving destroyed recorder owners, and final Map/object destruction. All ten
historical source assignments and their hashes remain unchanged.

Fresh full Bootstrap is **19/574 native**, both policies. Exact-source census:

| Source (SHA256 prefix) | Bytes | Default 100k steps | Explicit 1m steps |
|---|---:|---:|---:|
| Original single recorder (`4ebf1cd4`) | 1327 | 7/7 | — |
| Original repeated recorder (`45d621b3`) | 1733 | 0/7 | 7/7 |
| Exact full Data (`8359592c`) | 2522 | 0/7 | 0/7 |
| Captured child template (`e39ea887`) | 1303 | 6/6 | — |
| Captured outer template (`db9730ef`) | 1108 | 0/6 | 6/6 |

Every listed count is measured under both optimization policies. The compiler's
100,000-step default is unchanged. Exact Data retains the diagnostic
`property receiver lacks a fresh own-data object proof`; its full SHA256 is
`8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3`.
The other full source pins remain in `native_owned_global_maps/driver_recorder.py`.

**12e1074d** extends existing length subtraction to a canonical original String
literal RHS using `ownArrayIndex`; it adds no parser or state map. Exact bounds,
original provenance, whole-function effect closure and charged proof limits remain.
Noncanonical, computed, oversized and BigInt offsets stay conservative. The
**256-step build**, arrays **117 rows / 33 live states / 3698 retention cutoffs**,
and all three vendor escape gates pass. Fixture rerun: **1.16s**, including
**13 evidence mutations / eight malformed controls**.

All **112,001 fixture JavaScript bytes and old pins** remain unchanged. Exactly
one of **1123 complete snapshot rows** changes: `denseIndexStringOffset`,
**fn 219 / pc 3**, Stored to Confined; every other allocation, count, route,
observation and claim matches. Oracle: **222 functions / 861 claims / 895 sites /
35 unclaimed / zero violations / precision 27/172**. Original witness: **144 bytes /
SHA256 373e33a520eb82e157c0e5137c0f14928a155fe768b0cc111a1447bf2913803f**.

Standard full gate at **12e1074d**: **538/540 CTests in 964.95s**. The failures
were the existing browser `frames` test and one old constant-global ownership
expectation in lit (**165/166 cases passed in 654.11s**). **7a1a3490** corrects
exactly four optional/mixed Boolean/String entry selections: they now prove
ownership but remain **0/5 native**, with their independent Map-identity refusal.
All **71 constant source bodies**, repairs and source pins are unchanged. Their
focused observation/repair/stale/fresh/rerun checks pass: **8 unowned / 11 complete
owners**, including the previously native Undefined case.

The corrected **entire `global-maps.test` passes** through filtered CTest:
**1/1 lit case in 716.29s**, CTest **716.36s** (total **716.37s**). The other
165 lit cases passed in the full run. Thus all 166 lit cases and all compiler
CTests are covered by passing results across these runs. Only the two expectation
test files differ; production, browser and parser inputs are identical. All
**1285 final frozen inputs** match locally and on the devbox; the final source
census is identical to the full-run census. The sole remaining failure is browser
`frames`, still observing `0px,184px,rgb(0, 0, 0),184,0,8px` instead of
`200px,50px,rgb(1, 2, 3),50,1,0px`, with zero frame style elements.

The required bundled formatter **23.0.0git** retains the same **nine baseline
files / 26 diagnostics**. Changed files pass formatting; the whole check with
Homebrew **23.1.1** passes **804 C++ / 84 Python / 33 web files**. Independent
read-only review found no concrete soundness regression in the source normalization.

Next native increment: advance the unchanged exact Data source's mixed
Number/owning-object contents and **per-invocation return/identity evidence**, in
particular its unguarded `.value` and scalar observations. Preserve the recorder
proof and all exact source pins; a reusable method's alternatives alone cannot
prove one invocation returns an object. Keep guards, future calls, independent
owners and stale/forged report controls. Future browser API calls go through public
ctbrowser RAII subsystem APIs; this source-defined recorder needed no extraction.

Start with the unchanged `nested_map_mixed_child_returned_field` probe in
`driver_nested_maps.py`: **595 bytes / five functions / 16 source calls / SHA256
9d4c8b9c5e52297f6dd7f37f2a3a8c1a955f0d8348912cf35365de1ec00f57a0**. Its adjacent
strict-identity probe already compiles; adding a source guard would evade the task.
Static audit: `HostContract/Values.cpp::capturedMap()` already has an invocation
worklist, but `completedResults` keeps only categories and each body starts with
unknown Map contents. Reuse the existing checked body transfers with bounded
actual-call state and result provenance, distinct factory/child invocation identity,
keys, mutations and invalidations; preserve the independent whole-family proof.
Recheck exact returned leaf/initializer evidence through `guardedLeafRead` and
`OwnedGlobalMethods`. Full Data also needs proved Number extraction from the mixed
return carrier: keep `joinedReturnType()`'s general getter ABI, then extract at the
proved use. Narrowing only a CallDirect result type would mismatch the emitted
callee signature. These are inferred next seams, not newly measured diagnostics.

Next independent escape increment: private fresh array receiver confinement.
Start with unchanged `denseLengthEmpty`, **fn 208 / pc 6**, **157 bytes / SHA256
181331ed8a69ad6ca61f6b476bbd34c7adf8f731cfffe3eadb74135cc93e1992**. Its empty
array is read only through own `length`, observed confined but claimed Passed.
`refineArrayRetention` currently only clears Stored candidates despite complete
contents evidence. Reuse the complete effect, graph, all-path and budget proof;
keep generic property sink roles, returned arrays/children, missing slots, unknown
effects and cycles conservative. A guard must work even without a Stored child.
`canonicalStringSaved`, **fn 197 / pc 5**, is a second existing private-array
witness. Measure the whole dump; related private arrays may also improve.

Evidence: `/tmp/ctcompile-recorder-finalize/` contains frozen manifests, focused/full
logs, the whole snapshot comparison, exact-source census, generated recorder C++,
and the read-only `next-escape.md` / `next-data.md` audits. No browser/runtime/parser files changed.
No push; native Bootstrap and the application driver remain unfinished.

## Captured String-key templates and literal BigInt indices, 2026-09-13 UTC

Resumed **9132ada5**, its **04:10:47 UTC** synchronization journal and the
promised String-key/template boundary. Saved snapshot recovery and dense shrink
were already landed; `codex-wip-20260907` is an ancestor. Three agents split
source-preserving execution, source/prepared ownership tests and the independent
escape proposal. The resumed native work was gated and committed before the
escape increment. This entry supersedes older pending-template and recovery
instructions below.

**5f0d5dfd / 9e9b7d57** prove and execute captured String-key template snapshots.
Separate outer/child facts close every insertion across the complete sibling
family with no invocation-result authority. Only checked Concat may convert a
String-key snapshot element; equality-only snapshots remain independent and direct
key return/storage stays refused. Existing snapshot-operation evidence carries the
completed conversions through the environment check. Existing ordered Map, owning
vector and nullable-String lowering supply the C++; no runtime helper was added.

The exact child probe **1303 bytes / SHA256
e39ea88765761d962f82aefa4b100ef5f6b508ef7f45cfe7a32fc31c0a36bc86** is
**6/6 native at the default 100,000-step budget**, both optimization policies.
The exact outer probe **1108 bytes / SHA256
db9730efbaeb237a8a123801cb40ed79288162c99a39369b21a3fe4ecd14a81e** is
**6/6 with `host-max-steps=1000000`**, both policies; it remains **0/6 at the
default budget**. Its 13 entry calls repeatedly reprove the family. The compiler
default is unchanged. New unsafe-key controls use the explicit budget and must
finish a semantic refusal, never pass merely by exhausting it.

Measured focused gates: **324-step build**, a **255-step correction rebuild**,
and **3/3 ownership/host CTests in 271.53s**. New source/prepared checks cover
**27 rows / four scope mutations each**, stale/forged reports and incomplete/exact
budgets. Recorder group: **six native programs / 16 refusals / 142 typed Node/VM
observations / 11 distinguishing mutations**. Both policies/layouts, GCC/Clang,
no-Script symbols, ASan/UBSan/leaks and **1024 future String calls** pass. These
exercise empty/Undefined snapshots, insertion order, deletion/reinsertion, copied
String keys, old owners across entry reruns, detached methods and final destruction.
All previous source assignments, fixture bodies and source pins are preserved.

**1a587848** proves exact indices from one subtraction of two bounded original
BigInt literals. Both operands use the existing literal index parser; nonnegative
subtraction is exact. Loaded operands, chains, other arithmetic, invalid spellings,
large values and negative results remain conservative. Existing path-local origins
transport the computed result without new state or category/effect authority.
The **256-step rebuild**, arrays gate (**133 rows / 36 live states / 2851 retention
cutoffs**) and three vendor escape gates pass. The measured fixture rerun passes
**1.17s**, including **13 evidence / eight malformed-record controls**.
All **112,001 JavaScript bytes and old pins** are unchanged. Exactly one existing
claim improves: `radixBigIntComputed`, **fn 215 / pc 3**, Stored to Confined.
Oracle: **222 functions / 861 claims / 895 sites / 35 unclaimed / zero violations /
precision 26/172**; all other allocation coordinates, observations and claims match.

Final standard gate at **1a587848**: **539/540 CTests in 1015.87s**; all
ctcompile tests pass, including **166/166 lit cases in 708.68s** (CTest **708.75s**).
The sole failure is the existing browser `frames` test. All **1284 frozen inputs**
match locally and on the devbox. The required bundled clang-format **23.0.0git**
retains the same **nine baseline files / 26 diagnostics**; changed C++ passes.
The whole check with Homebrew **23.1.1** passes **803 C++ / 84 Python / 33 web**.
The frame failure again observes `0px,184px,rgb(0, 0, 0),184,0,8px` instead of
`200px,50px,rgb(1, 2, 3),50,1,0px`; the frame document has zero style elements.
It remains Claude's browser boundary. Generated template C++ uses existing owning
String vectors and checked nullable reads; no Script symbol or VM call appears.

Fresh full Bootstrap remains **19/574 native**, both policies. Exact Data and
the original single/repeated numeric recorder probes remain **0/7**, both policies,
with `property receiver lacks a fresh own-data object proof`. The final measurement
also reconfirms the child/outer template counts and explicit-budget limit above.
The exact Data source remains **2522 bytes / SHA256
8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3**;
original single/repeated sources retain **4ebf1cd4 / 45d621b3**. Source pins are in
the committed driver, independent of temporary evidence files.

Next native increment: advance the **unchanged numeric single/repeated recorder
probes** in `native_owned_global_maps/driver_recorder.py`. Their source creates its
own `console.error` closure; this needs no browser API extraction. The first body
blocker is `load_global "console"`; the later property-receiver diagnostic follows
from the failed family proof. Prove the unique initialized own callable slot and
its exact target, every source alias/use, all callback arms and String arguments,
the initializers/reads/writes of `traceErrorCount` and `traceErrorMessage`, and
preserved Number categories across repeated future calls. Reject replacement,
pre-initialization reads, unsafe inactive-arm writes, unknown calls and reentry.
The callback must have no access to captured Maps or caller leaves.

Carry callback/global evidence separately through `HostContract`,
`CapturedMapBody` and `OwnedGlobalMethods`; keep it distinct from exported method
calls and pure snapshot operations. Feed the exact source target to existing
closure/direct-call lowering, preserving receiver/argument order and real callback
effects, then recheck transformed source with a fresh fingerprint. Startup
`ProviderCallbacks` reports do not prove reusable callback effects. Existing
nullable Number storage/arithmetic suffices; do not weaken `scalarGlobalRead`'s
sole-store rule or type inference's sole-store removal of initial Undefined.
Preserve both original probes, their mutations and pins, and gate future calls,
owner/callable destruction, both policies/layouts, no-Script and sanitizers.

Exact Data's unguarded `.value` and mixed scalar observations still separately
need per-invocation content/identity proof. Future browser API calls use public
ctbrowser RAII subsystem interfaces; the VM-context AOT ABI is not a native
boundary. The prior weak-global absence pins are already retired in **73d4e034**;
the MathML comparator still waits for the browser's public `node_ns::mathml` enum.

Independent escape next: canonical original String offsets in the unchanged
`denseIndexStringOffset` (**144 bytes / SHA256
373e33a520eb82e157c0e5137c0f14928a155fe768b0cc111a1447bf2913803f**,
**fn 219 / pc 3**). Its child is observed confined but still claimed Stored.
The existing length-minus-Number proof can reuse `ownArrayIndex` under an explicit
StringAttr guard and its existing exact bounds checks. No new state or parser is
needed. Preserve the source, distinguish String conversion from BigInt arithmetic,
keep noncanonical/coercing categories refused, and measure the complete oracle
before changing its snapshot. No further original computed-BigInt witness exists.

Evidence: `/tmp/ctcompile-string-snapshots/`, including focused/full logs, frozen
inputs, exact-source measurements, generated C++, and the read-only
`next-recorder.md` / `next-escape.md` audits. The original BigInt proposal and
preservation checks are in `/tmp/ctcompile-next-bigint-audit/`.
No browser/runtime/parser files changed. No push; full native Bootstrap remains
unfinished.

## Captured snapshots recovered and dense array shrink proved, 2026-09-13 UTC

Resumed **7adddab2**, its saved nine-path recovery patch and **03:11:11 UTC**
synchronization journal before starting anything new. The old WIP branch was
already resolved. Three agents reviewed snapshot safety, traced the recorder
boundary, and prepared the independent escape increment. This entry supersedes
the older pending-recovery instructions below.

**cecbd16c / c2036a70** land the captured Map key snapshot proof and its execution
regressions separately. The saved test-only fingerprint correction is now tested:
source/prepared restoration rederives with the forged report, removes it, then
checks the original fingerprint. The recovery patch is retired; **7adddab2**
retains its history. Existing ordered Map storage, owning vectors and nullable
read helpers implement the output; no runtime or browser implementation was added.
Immediate single iterator consumption, exact Array identity, confined read-only
uses, source scope and bounded traversal remain required. Snapshot elements still
gain no template, callback, field, return or storage authority from equality alone.

The **331-step build** and focused **3/3 CTests pass in 237.28s**. The recorder
group passes **four native snapshot programs / eight refusals / 72 typed Node/VM
observations / eight distinguishing mutations**. Both policies/layouts, GCC/Clang,
no-Script symbols, sanitizers/leaks and **1024 future iterations** pass. Empty and
out-of-range reads remain Undefined; saved copies preserve String/numeric insertion
order across replacement, deletion, reinsertion and clear.

**92feee25** adds bounded original Number-literal writes to a fresh dense array's
own `length`, reusing `ownArrayIndex`. Shrink and same-length writes preserve saved
Number/child origins and historical cycle edges; removed entries spend the existing
proof budget. Growth, coercing categories and computed targets remain conservative.
The gate passes **95 rows / 29 live states / 3121 retention cutoffs** and all three
vendor escape corpora. The snapshot rerun passes **1.18s**. All **112,001 JavaScript
bytes / 69 old pins** remain unchanged; one pin for the existing source was added.
Exactly one claim changes: `denseLengthChanged`, **function 210 / pc 3**, is now
confined. Its allocation and runtime observations are unchanged. Oracle:
**222 functions / 861 claims / 895 sites / 35 unclaimed / zero violations /
precision 25/172**. Checker: **13 evidence mutations / eight malformed controls**.

Final standard gate at **92feee25**: **539/540 CTests in 1006.90s**; all
ctcompile tests pass, including **166/166 lit cases in 690.12s** (CTest **690.19s**).
The sole failure is the pre-existing browser `frames` test. It observes
`0px,184px,rgb(0, 0, 0),184,0,8px` instead of the expected frame styles; the
frame document reports zero style elements. This remains Claude's browser boundary.

The required bundled clang-format **23.0.0git** retains the same **nine baseline
files / 26 diagnostics**. Changed C++ passes; the whole check with Homebrew
**23.1.1** passes **803 C++ / 84 Python / 33 web files**. No browser/runtime/parser
files changed. Claude's **cf4fb0e1** test/baseline integration was reviewed before
these gates. All **1284 frozen compiler/runtime/test inputs** match the devbox after
the full gate and final measurements.

Late browser handoff **42b7960d** changes documentation only. In response,
**73d4e034** retires exactly three historical `WeakRef`/`FinalizationRegistry`
absence assertions and corrects ND-2 documentation. All other cycle source and
GC/RAII/WeakMap checks are preserved. The subsequent **two-step build** and
`ctcompile_escape_cycle` pass (**0.03s total**); final **1284 local/remote inputs**
match, with only `Cycle.cpp` differing from the full-gate manifest. Both formatter
checks retain the results above. Runtime weak globals may now be enabled without
this obsolete pin; native weak-edge admission remains unimplemented.

The other browser request needs coordinated integration: `node_ns::mathml` is
absent from the current public enum. Add `case node_ns::mathml: return "mathml";`
to `HTML/DocumentComparator.cpp::ns_name` together with W's future enum/parser
change, then gate both projects. No browser branch was merged or edited here.

Fresh full Bootstrap remains **19/574 native**, both policies. Exact Data and
the original single/repeated numeric recorder probes remain **0/7**, both policies,
with `property receiver lacks a fresh own-data object proof`.
The three original Data/recorder sources and their pins remain unchanged in
`native_owned_global_maps/driver_recorder.py`; the full Data source is **2522 bytes /
SHA256 8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3**.

Next native increment: independently close every outer/child Map insertion over
String keys, including all sibling methods and future input categories, then admit
only checked template Concat uses. Existing `Lowering/StringValues` already converts
nullable String (including Undefined) correctly; no new string runtime or nonempty
snapshot assumption is needed. Use the independent no-results family census, retain
separate key roles, and recheck ownership. Direct snapshot return/storage and unsafe
keys must remain refused.

Two separate template probes (**db9730ef / e39ea887**, outer/child keys) pass **23 typed Node/VM
observations including three distinguishing mutations**; both are **0/6 native**
in both policies. Their default proof budget exhausts; at **1,000,000 diagnostic
steps**, both finish with `property call lacks a current source getter proof`.
These are measured next-boundary probes, not implemented template support. Exact
sources and the static caller/effect audit are under `/tmp/ctcompile-next-recorder-audit/`.

Then prove the original recorder's complete callable/global effects: its exact
`console.error` closure, initialized mutable scalar globals, every callback arm,
replacement/reentry exclusions and future calls. The current body proof stops at
`load_global "console"`; a startup prefix report cannot authorize that callback.
Exact Data's unguarded `.value` and mixed scalar observations still separately need
per-invocation return identity/content evidence. Future browser APIs call public
ctbrowser RAII subsystem APIs; its VM-context AOT ABI is not a native boundary.

Evidence: `/tmp/ctcompile-snapshot-resume/` (builds, focused/full logs, generated C++,
source preservation, frozen inputs and measurements),
`/tmp/ctcompile-next-escape-audit/`, `/tmp/ctcompile-next-recorder-audit/`.
No push. The complete native Bootstrap plan remains unfinished.

## Session close: dense indices landed; captured snapshots saved for recovery, 2026-09-13 UTC

Stopped at the user's bedtime request. Resumed **aa7c6bdb**, its **01:30:07 UTC**
journal and the original numeric-only Data recorder probes in
`/tmp/ctcompile-recorder-next-probe/`. The old WIP branch was already resolved.
Three agents split dense-index proof, preserved recorder witnesses, and snapshot
ownership tests/review. This entry supersedes older next-step and gate claims below.

**Landed: 8349d203**, bounded original array-length snapshots and subtraction of
nonnegative integral Number literals. Saved lengths/indices retain their original
values across appends and aliases; invalid bounds/categories remain conservative.
The array gate passes **64 rows / 19 live states / 2053 retention budget cutoffs**;
Bootstrap, p5 and Phaser escape corpus CTests pass. The old **109,689 fixture bytes /
63 source pins** are preserved. Measured oracle: **222 functions / 861 claims /
895 observed sites / 35 unclaimed / zero violations / precision 24/172**, exactly
three new confinement claims, including the untouched `denseLengthIndexed` source.
All old function evidence is preserved except that newly proved child; added global
closures account for the expected PC shifts. The checker passes **13 evidence
mutations / eight malformed or duplicate controls**. After Claude's **834bda1e**
merge, a **640-step rebuild** and final fixture CTest pass (**1.16s**).

**Not landed as implementation: captured Map key snapshots.** All nine changed
paths, including the new recorder driver, are preserved in
the recovery patch retained in **7adddab2** (now landed and retired), based on
**8349d203**. SHA256:
`9f368e807d4f5586dc2f492dff95906f359a7df0bd9e94ab770d7455a1fec6d8`.
Its header records every target hash. Apply/reverse checks and byte-for-byte
reconstruction of all nine targets pass. Only our nine working source paths were
restored after saving the patch; no browser work was discarded. The patch is the
durable continuation, not an assertion that the feature is fully gated.

The draft reuses existing ordered Map storage, `map_keys`, owning vectors and
nullable element helpers. It proves exact Array identity, one immediate keys
iterator consumption, confined read-only length or constant-index strict equality,
source scope and bounded traversal. It adds no runtime/browser implementation.
Element reads confer no scalar-return, field-read or callback authority. Iterator
reuse, intervening effects, foreign Root operands and unsafe indices stay refused.

Measured draft: **325-step build PASS**, **four native snapshot programs / eight
refusals / 72 typed Node/VM observations / eight distinguishing mutations**. Both
policies, both layouts, GCC/Clang, no-Script symbols, ASan/UBSan/leaks and **1024 future
iterations** through retained owners/tables/detached callables pass. Cases cover
String/numeric insertion order, replacement/delete/reinsert/clear, saved copies,
empty arrays and out-of-range Undefined. The focused CTest group passes **2/3 in
244.79s**: `ctcompile_host_contract` and `ctcompile_host_contract_seeded_maps` pass;
`ctcompile_owned_global_shared_map` fails exactly four new restoration assertions
(source/prepared, cross-arm/cross-function). Its other checks pass.

The test deliberately retained a forged `ctnative.map_snapshot_copy` attribute
while reusing the old module fingerprint. Non-host attributes correctly affect that
fingerprint. The recovery patch includes the prepared **test-only correction**:
rederive with `requested(*module)` while the forged report remains, then remove the
attribute before checking the original fingerprint/contract. **This correction has
not been rebuilt or executed.** Production and driver bytes match the measured
frozen draft; the corrected test is explicitly distinguished by its hashes.

Resume this recovery before new feature work:

1. Read the latest synchronization journal and claims. Claim the nine patch paths;
   under `/tmp/ctbrowser-repo-git.lock`, run `git apply --check` then `git apply` on
   the recovery patch from the repository root. The new browser merge **f52e2430**
   changes none of these paths. Do not blindly run the old `/tmp` finishing scripts:
   their frozen manifests predate the saved correction and later merge.
2. Under `/tmp/ctbrowser-devbox-build.lock`, run `tools/remote-build.sh all`, then
   on the devbox `ctest --test-dir build -R
   '^ctcompile_(owned_global_shared_map|host_contract|host_contract_seeded_maps)$'
   --output-on-failure -V`. Re-run the recorder group through
   `ctcompile/test/CTNative/Ownership/global-maps.py --group recorder`, with
   `PYTHONPATH=ctcompile/test`, the built `--translate`, `--opt`, `--reference`,
   explicit `--node` from `CTCOMPILE_BOOTSTRAP_NODE` in `build/CMakeCache.txt`, and
   a fresh `--work` directory. The package directory has no `__main__` entry.
3. Run the formatter; commit the six proof files plus `SharedMap.cpp`, then the
   driver hook/new recorder driver as a separate concern. Run the standard full
   `tools/remote-build.sh` gate and fresh Bootstrap/Data measurements. Remove this
   recovery patch once its contents are safely landed and record the final evidence.

The exact Data source remains **2522 bytes / SHA256
8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3**.
The original recorder/factory prefix remains **975 bytes / SHA256
c87ab961b1186537b86b5c96e35a5bec0905c97dbaa90c217f99c4175192efc9**.
The single/repeated recorder probes remain **1327 / 1733 bytes**, SHA256
`4ebf1cd4417eb0f54a3c154a1ba0a286eadfbba21526705808e731e3a85c4479` /
`45d621b3cba163ca44d4e0d9e9fc793f953a505b3664a38b2890e9775caf4e36`.
All three still measure **0/7 native, both policies**; their exact sources and pins
are embedded in the recovery driver. Full Bootstrap's last measurement is still
**19/574**, inherited from the previous session, not rerun tonight. After the recovery
gate, next prove key categories for the original template diagnostic and the complete
recorder callable/global-effect boundary across future calls. Exact Data's unguarded
`.value` and mixed scalar observations separately need per-invocation return evidence.
Prefix evaluation alone does not authorize these. Later DOM/component work must use
ctbrowser's public RAII APIs; VM-only behavior needs one lifted core and a thin adapter.

No new full standard gate completed for this snapshot draft. The earlier **540/540**
result below belongs to **415c4d5c**. Claude's subsequent **834bda1e** browser gate
reports **536/540**, with `bootstrap_layout`, `cssom_wpt`, `frames`, `layout_blocks`
failures. At session close Claude merged **f52e2430** (browser tests/docs only),
reports `layout_blocks` fixed and the other three still open; gate 5 was still running.
See `ctbrowser/docs/plans/wpt-next.md` and the **03:03:54 UTC** sync journal for that
handoff and four unmerged browser agent branches. Their pending JS changes are not
this session's differential oracle. No Codex browser/runtime/parser edits.

The required bundled clang-format **23.0.0git** check retains the known **nine files /
26 diagnostics**; changed C++ passes. Homebrew **23.1.1** whole check passes
**803 C++ / 84 Python / 33 web** with the draft installed. Final checkpoint checks
and job/claim release are recorded in the sync journal. Our queued finish workflow
was canceled before starting; no Codex devbox jobs remain. Claude's queues were left
alone. No push. The full native Bootstrap plan remains unfinished.

Detailed local evidence: `/tmp/ctcompile-recorder-{dense-build.log,dense-ctest.log,
dense-rerun-build.log,dense-rerun.log,native-build.log,native.log,owner.log,
native-frozen.json}`, `/tmp/ctcompile-dense-index/`,
`/tmp/ctcompile-recorder-witnesses/`, `/tmp/ctcompile-recorder-audit.md`, and
`/tmp/ctcompile-recorder-stop/`. Recovery does not depend on these temporary files.

## Guarded returned fields and original radix BigInt indices, 2026-09-13 UTC

This entry supersedes the integration failures and next steps in older entries below.
Resumed **3e803401**, its promised guarded returned-field step, and the
**23:15:01 UTC** synchronization journal after Claude's **7d76748d** merge.
Gated that integration before applying new proof code. **8c980759** repairs four
remaining callers of the retired key helper using the existing `constantKey` and
`ordinaryKey`; **98500c0b** formats the dump checker without changing its AST.
The full integration run passed **538/540 CTests in 1068.40s**. **7b493560**
corrects the lattice census **146→141**, exactly the five retired interface checks,
and **db6233d0** completes the shared harness migration in invocation-state and
map-mixed. Their focused reruns pass; all source bodies/assertions remain intact.

**b7bc36d3** proves a field read from a returned owner only under a dominating
strict-identity guard on that exact result, against a checked caller object with
an initialized scalar own field. Reuses existing leaf evidence, carriers and native
field helpers; adds no runtime or browser implementation. Entry observation branches
must remain pure. Unchecked fields, truthiness-only guards, another lookup's guard,
missing fields and scalar comparators stay refused. The source-scope check covers
If/Yield operands before active-arm selection. Source/prepared ownership checks pass
**29 rows each**, including stale/forged reports, wrong guards, invalid inactive-arm
yields and incomplete budgets; the original five raw MLIR fragments are unchanged.

**58350704** adds eight separate witnesses while preserving all **71 historical
JavaScript bodies**. Three guarded sources execute **5/5 native** in both policies,
with **16 calls** each; five hostile controls refuse. The full nested-Map cohort passes
**41 native programs / 38 refusals**, **275 typed Node/VM observations / 192
mutations**, both C++ layouts, GCC/Clang, no-Script and ASan/UBSan/leak checks.
Saved owners survive overwrite/delete/clear, field mutation, **128 future calls**,
detached closures, entry reexecution and final release. The new same-guard complete
budget is **32813 / 29 cutoffs**; the unchanged returned-identity source now needs
**31170 / 32 cutoffs** after the stronger scope checks.

**415c4d5c** extends the existing bounded index parser to original
hexadecimal/octal/binary BigInt constants. It preserves bounds, origin and category
checks; computed BigInts and String lookalikes remain conservative. The matrix passes
**126 rows / 18 live states / 2237 retention cutoffs**. Five appended witnesses
preserve the old **107,754-byte** source prefix and every old source pin. Fresh oracle:
**217 functions / 840 claims / 874 observed sites / 35 unclaimed / zero violations /
precision 21/168**, exactly two additional confinement claims. Every old function
1–210 row/claim is unchanged; only five new closures, their consequent PC offsets,
and the new functions change the complete snapshot. The checker still passes its
**13 evidence mutations / eight malformed or duplicate controls**.

The **260-step build** and feature gates pass; the initial sole failure was the old
escape snapshot, updated only after measuring and reviewing its full dump. Its final
CTest rerun passes **1.16s**. Final standard gate at **415c4d5c**:
**540/540 CTests in 1003.73s**, including **166/166 lit cases in
686.97s** (CTest **687.05s**). All **1597 frozen inputs** match the
devbox. The required bundled clang-format **23.0.0git** check retains exactly the
same **nine pre-existing files / 26 diagnostics**; changed C++ files pass. The same
whole check with Homebrew clang-format **23.1.1** passes **802 C++ / 83 Python /
33 web files**. No browser/runtime/parser files changed this increment.

Fresh full Bootstrap is **19/574 native** in both policies. Exact Data stays
**0/7** in both policies, preserving **2522 bytes / SHA256
8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3**.
Its reported host-contract diagnostic remains `property receiver lacks a fresh
own-data object proof`; the separate numeric recorder probes reach the same earlier
boundary even without returned-object field reads.

Next: keep the original recorder and Data factory bytes, use a separate numeric-only
entry to isolate the conflict diagnostic (`Array.from(s.keys())`, template expression,
then source recorder), and prove its complete call/effect boundary. Reuse existing
snapshot/string lowering and provider callback analysis. The current owning Map
representation permits lookup only: preserve insertion order, or prove the snapshot
has at most one live key. Prefix summaries alone do not authorize native future calls. Exact Data's unguarded `.value` and direct scalar
observations also need per-invocation return evidence: matching key/object, last write,
and no intervening delete/clear/reentry. Its later identity check is on another call.
The old unguarded returned-field probe remains refused. Independent escape next:
bounded integer provenance for `denseLengthIndexed`'s `before - 1`.

Two separate next-step probes preserve the original **975-byte recorder/factory
prefix**, SHA256 **c87ab961b1186537b86b5c96e35a5bec0905c97dbaa90c217f99c4175192efc9**.
Single conflict **4ebf1cd4** (1327 bytes) and repeat after delete/reinsert **45d621b3**
(1733 bytes) pass **15 typed Node/VM observations / two distinguishing mutations**.
Both remain **0/7 native**, both policies, using the existing Map/Array host contract
and explicit observed globals. Counts/messages are **1/1**, then **2/0** after the
first live key changes; removing the recorder and freezing the first key are caught.
Use `/tmp/ctcompile-recorder-next-probe/{README.md,prepare.py,measured.json}` to
resume this exact boundary; these are measured probes, not new native successes.

Actual component/DOM work must call ctbrowser's public RAII subsystem APIs. Data itself
needs no browser extraction. Check current helper signatures: the AOT VM-context ABI
is not an allowed native boundary, and the audited tree removed the old public
ctnative generator directory. Lift any needed VM-only platform behavior into its
owning subsystem, with a thin binding adapter and the required atomic browser gate.

Evidence: `/tmp/ctcompile-return-fields-{first-native.log,first-ctest.log,full.log,
full-detail.log,measured.json,final-frozen.json,final-remote-hashes.json}`,
`/tmp/ctcompile-radix-measured-review/`, and
`/tmp/ctcompile-return-fields-next-boundary.md`. No push. This is a completed
increment; the complete native Bootstrap plan remains unfinished.

## Owning child returns, Map keys and escape evidence, 2026-09-12

Resumed clean **b0439107** and the **20:46:23 UTC** recovery journal. The old
WIP branch was already merged. Three agents split ownership tests, escape-checker
simplification and key/oracle review. Audit-held integration paths remained untouched
until their **19:54:55 UTC** claims expired; the narrow takeover is journaled at
**22:18:17 UTC**. This update also records the predecessor's deferred handoff below.

**c398e22a** proves owning mixed child-Map returns through the existing
`object_value`/identity carriers. A returned leaf gains no primitive-return authority.
The existing source-scope check now covers Compare, Unary and Truthy operands before
active-arm selection. Direct formal/local-object returns, result-as-formal dependencies,
cycles and unchecked fields remain refused. Source/prepared checks pass with hostile
late-getter and cross-function operands, stale/fresh/forged reports and bounded proofs.
The stronger historical ownership expectations preserve all 19 raw MLIR fragments.

**64fa4a5f** executes the unchanged returned-identity source **3e51df8e** as
**5/5 native**, both policies, with **16 calls** and a complete budget of **31065 /
30 incomplete cutoffs**. All **71 original JavaScript bodies** remain byte-identical.
The cohort passes **38 native programs / 33 refusals**, **231 typed Node/VM
observations / 174 distinguishing mutations**, both C++ layouts, GCC/Clang, no-Script
and ASan/UBSan/leak checks. Saved owners come through compiled `get()` and survive
overwrite/delete/clear, field mutation, **128 future calls**, detached closures,
entry reexecution and final release. Null/false/zero/negative-zero/NaN payloads produce
the original Null result. Exactly six historical controls gain ownership evidence
while retaining their independent carrier/read refusals.

**786e606f** canonicalizes negative-zero Map keys in shared `map_set`, including
variant keys, without changing input or payload signs. It fixes all five native
Map/deforestation integration failures. The Map representation gate passes **14
observations**, Node/VM comparisons, both layouts/compilers and sanitizers; helper
checks cover insert/update/delete-reinsert and preserved negative-zero payloads.

**4df2ef5d / 88e0bf9f** replace duplicate CMake escape parsing with the checker's
deterministic `--dump` and a complete snapshot, removing **394 lines net**. All
**107,754 JavaScript bytes**, SHA256 **088377fc18a2cbc7f3aacd3c26dc8dbfcbd868d53b073c1a076c3abf277aa338**,
and the **9,112-byte** source hash block are unchanged. The snapshot round-trips through
all **39 old family checks**; **13 evidence mutations / eight malformed or duplicate
controls** pass. All four corpus CTests and the checker pass. The fresh fixture
measures **856 sites / 35 unclaimed / zero violations / precision 19/164**.

**2d5323aa / 28a1827a / 85e6cee9** apply the corresponding exact **0166780d**
test repairs: noniterable spread throws TypeError, the current parser has **1023
capture operands / 219 indexed operands**, and Map has the standard toString tag.
Differential/capture CTests and all **28 provider cases** pass. These commits change
no browser/runtime or parser files; the measured parser is **b2b5155**.

The standard full gate at **85e6cee9** passes **543/544 CTests in 1124.77s**.
All ctcompile tests pass, including **168/168 lit cases in 738.62s** (CTest
**738.82s**). The sole failure is the known browser `navigation` expectation for
Null in `Array.prototype.join`; its correction is in **0166780d**. All **1,619
frozen inputs** still matched the devbox at final measurement.

During the gate Claude merged **7c3a24e3** as **7d76748d**, including that navigation
correction. These measurements cover **85e6cee9**, not the merged revision. Claude's
**23:07:14 UTC** journal reports the incoming audit build green but its CTest run
incomplete. First gate the integrated tree before extending the next proof boundary.
The audit now requires Python drivers to use the shared harness, explicit
`--node`/`--reference`, and `PYTHONPATH=ctcompile/test`; merge resolutions changed
those driver imports. CTC-01 is complete in **4df2ef5d / 88e0bf9f**, despite the
incoming audit journal still listing it as open.

At **85e6cee9**, the corrected focused group passes **9/9 CTests in 420.95s**.
The required `tools/format.sh --check` reports the same **nine pre-existing files /
26 diagnostics** with bundled clang-format **23.0.0git**. Every changed C++ file
passes that formatter; the same repository-wide check using Homebrew clang-format
**23.1.1** passes all **819 files**. The required check after merge retains those
same nine files / 26 diagnostics and exits before the new Python/web checks.

Fresh measurements at **85e6cee9** keep full Bootstrap at **19/574 native** in
both policies. Exact Data remains **0/7** in both policies with
`property receiver lacks a fresh own-data object proof`. Preserve its **2,522 bytes /
SHA256 8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3**.

Next: independently prove guarded returned-object field receivers and per-invocation
contents/identity. Keep the original unguarded returned-field probe refused until its
receiver is proved; a truthy mixed value can still be Number. Exact Data also needs
its original recorder callback, `Array.from(s.keys())` and the template diagnostic.
Component/DOM work must use ctbrowser's public RAII subsystem APIs without Script or
GC dependencies. Independent escape candidates are bounded integer provenance for
`denseLengthIndexed`'s `before - 1`, or original nondecimal BigInt literal indices.

Evidence: `/tmp/ctcompile-owning-return-{full.log,full-detail.log,measured.json}`,
`/tmp/ctcompile-owning-return-{repair-ctest.log,native-final.log,final-frozen.json}`,
`/tmp/ctcompile-integration-checks.log`, `/tmp/ctcompile-zero-key-evidence.log`,
`/tmp/ctcompile-data-return-next/{results.json,summary.json}`,
`/tmp/ctcompile-escape-claims-before/` and `/tmp/ctcompile-owning-return-next.md`.
Stopped at this gated increment at the user's request; no push.

## Recovered mixed-child execution and dense-array length, 2026-09-12

Historical recovery state recorded at **20:46:23 UTC**; the newer entry above
supersedes its remaining failures and next steps. Resumed interrupted **471ac673**,
its **16:57:21 UTC** journal and the nine uncommitted ctcompile paths, after runtime
merge **422aa3f7**. Three agents split escape recovery, native lifetime checks and
a read-only runtime audit.
**4564097f** completes original dense-array length proofs; the predecessor's
production logic was retained and its alias/six source-PC expectations corrected.
The exact **30,235-byte** old escape fixture prefix is preserved. Fresh checks:
**29 rows / eight live states / 888 budget cutoffs**, oracle **856 sites /
35 unclaimed / zero violations / precision 19/164**.

**3d31a120** completes mixed-child native execution: **37 native programs /
34 refusals**, **227 typed observations / 171 distinguishing mutations**,
both layouts, GCC/Clang, no-Script and ASan/UBSan/leak/lifetime checks through
**128 future calls**. All **60 historical and 11 interrupted JavaScript bodies**
remain unchanged. Nine added programs compile fully; the two owning-return
probes remain refused. Complete budgets: identity **32700/31 cutoffs**,
replacement **83098/30 cutoffs**. The **1,000-step rebuild** and all **14 focused
CTests** pass (**230.42s**). Bundled formatting has **nine pre-existing files /
26 diagnostics**; changed C++ files pass, and the alternative formatter passes **819 files**.

The full gate passes **534/544 CTests in 996.29s**; lit passes **167/168 cases
in 686.92s**. All ten failures reproduce the already-journaled integration issues.
**b0439107** then repairs the stale WeakMap class expectation inside the audit's
escape-test exclusion; its two-step rebuild and cycle CTest pass (**0.02s**).
At that checkpoint **nine CTest failures remained**: five native Map/deforestation signed-zero
checks, navigation, capture census, differential and lit's Map-toString expectation.
Their paths were held by **claude-audit**. Test repairs were prepared on **0166780d**;
native key normalization and integration of those repairs were the next obligation.
Only Cycle.cpp changed after the full run; all **1,620 frozen inputs** matched on the
devbox at full-gate completion. No compiler, browser or parser bytes changed between
that run and the cycle rerun. Bundled formatting retains its baseline failures.

At that checkpoint full Bootstrap stayed **19/574 native** under both policies. Exact Data
**8359592c** was **0/7** under both policies, preserving **2,522 bytes** and its
SHA256. The next proof boundary was owning mixed-child returns/identity, followed by
guarded field reads and the original recorder callback. Component/DOM work uses
public ctbrowser APIs.
Evidence: `/tmp/ctcompile-recovery-summary.json`,
`/tmp/ctcompile-recovery-{focused-detail.log,native.log,full.log,full-detail.log,measured.json}`.
The recovered changes were committed without a push. This handoff was originally
deferred under the active docs claim; its reviewed patch is now incorporated here.

## Caller scalar-field payloads and decimal BigInt indices, 2026-09-12

Continued clean **ee4b2a57**, the **14:25:57 UTC** journal and
`/tmp/ctcompile-output-next.md`. The promised caller-payload proof was the next
unfinished step; `codex-wip-20260907` was already an ancestor. Three agents split
escape, ownership and native lifetime checks; root integrated and gated them.

**5ea064b0** admits caller leaves with definitely initialized scalar own fields,
including stable global aliases and strict identity uses. It checks every alias/use,
source order and SSA dominance. A formal may receive objects and supported scalars
across its complete call census; it gains no primitive-only authority. Existing
`object_value` and identity-field carriers suffice. Cycles, missing/prototype fields,
unknown consumers, method-formal field access and object returns remain refused.
Thirteen source and thirteen prepared ownership rows pass, with forged self-cycles,
late key/comparison operands and bounded incomplete proofs. Fourteen historical
expectations were corrected without changing their 120 source fragments.

**63206007** adds four straight-line caller witnesses, each **6/6 native** in
both optimization modes: field **c3b36e07**, alias **96bc9f68**, mixed Number first
**90b799ff** and last **b3f10a06**. The original 46 nested source bodies remain
byte-identical; the first four conditional-entry probes remain explicit **0/6**
refusals. The cohort passes **28 native programs / 32 refusals**, **139 typed Node/VM
observations / 149 distinguishing mutations** in **57.37s**. Both C++ layouts pass
GCC/Clang, no-Script and ASan/UBSan/leak checks through **128 future calls**, saved
field-bearing owners after delete/clear, entry reexecution and final release.
Measured complete budgets/cutoffs: **15329/31** and **21452/30**.

**e7760ba4** updates the historical caller cohort without altering any of its
55 JavaScript bodies. Field **698def06**, global field write **194d9834** and payload
field **5442abee** now execute **4/4 native**; six mixed-key/category controls prove
ownership but still refuse native emission. Preparation preserves each actual's
original allocation/global-load/literal origin. The checker rejects 20 distinguishing
mutations. The corrected cohort passes **22 native programs and their controls** in **46.35s**,
both C++ layouts, GCC/Clang and no-Script; payload-field budget/cutoffs **1869/29**.
The initial harness assertion expected an inline assignment; its one-line correction
counts the actual existing field setter. Generated behavior did not change.

**a78ab38d** proves original canonical decimal BigInt array indices with existing
origin/bounds/budget checks. The old escape fixture remains an exact **28,451-byte
prefix**. Five appended witnesses cover twelve literal sites: five confined and seven
retained. The matrix passes **54 rows / six live states / 914 retention cutoffs**;
all 11 escape CTests pass. The full oracle measures **828 sites / 33 unclaimed /
zero violations / precision 16/157**. Computed, noncanonical and object-loaded keys,
sparse/missing elements and ordinary-object/prototype effects remain conservative.
The importer spelling comment now correctly says that bytecode already stripped `n`.

The full standard run at **e7760ba4** passed **532/534 CTests**, including all
**162 browser tests**, in **858.88s**. It exposed four obsolete host-contract
expectations and one lit control whose mixed key now proves ownership but still
has no native carrier. **ded4eb06** fixes the host expectations with all **116 input
fragments / 41 rows** unchanged; its CTest passes **40.07s**. **f6caaadd** fixes
only the native control's evidence and summary. All **230 tail source bodies** are
unchanged; a two-policy census has zero errors, preserves 28 existing ownership-only
routes and identifies exactly one new ownership-only row. Its checker passes eight
measured/forged pairs, rejects 20 mutations, and its complete focused pipeline passes
**0.53s**. The corrected **ctcompile_lit** CTest passes **168/168 cases in
648.84s** (CTest **648.93s**). Together these runs cover all **534 CTests**.
Only three test files differ from the full run. All **1,368 frozen production,
browser, parser and test inputs** match the final local/remote snapshot; production,
browser and parser bytes are identical between the full run and corrected reruns.
Fresh Bootstrap stays **19/574 native** in both policies; Data stays **0/7 CommonJS,
0/7 browser and 0/8 AMD**.
Stable clang-format22 passes all **800 files**; bundled23 retains exactly the same
**nine baseline files / 28 diagnostics**. No browser/runtime or parser changes were
included. The parser remains **8eb3375**.

Next: independently prove mixed scalar/object child-Map contents, owning object
returns and identity, then Data's original recorder callback (`Array.from(s.keys())`
and its template diagnostic). See `/tmp/ctcompile-caller-next-session.md`.
The next independent escape candidate is dense-array `.length`; its proposed witness
is Node-measured only, with VM/PCs still unmeasured. See
`/tmp/ctcompile-bigint-indices-next.md`.
Preserve exact Data's **2,522 bytes / SHA256
8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3**.
The current probe remains **0/7** in both modes; new caller witnesses are bounded
proofs, not native Bootstrap. Component/DOM ownership must use ctbrowser's public API.

Evidence: `/tmp/ctcompile-caller-{native.log,repair-gate.log,object-keys-final.log}`,
`/tmp/ctcompile-caller-complete-summary.json`,
`/tmp/ctcompile-caller-full-{detail.log,frozen.json,revision.json,summary.json}`,
`/tmp/ctcompile-caller-{host-unit.log,lit-final.log,lit-final-detail.log,final-frozen.json}`,
`/tmp/ctcompile-caller-tail-{audit.json,measured/results.json}`,
`/tmp/ctcompile-caller-probe-measured/results.json`,
`/tmp/ctcompile-caller-key-{preparation-measured,checker-mutations}.json` and
`/tmp/ctcompile-caller-focused-detail.log`.
Claude's pending isolated runtime work includes the **15:08:14 UTC** ToNumeric ABI
proposal, accepted in the **15:10:59 UTC** journal. The **16:10:57 UTC**
journal also records pending RegExp/String protocol and Function.toString changes.
Re-read the latest journal and re-gate the differential oracle after integration.
Nothing was pushed.

## Native nullable output and canonical String indices, 2026-09-12

Continued clean **3e60248f** and the **11:04:32 UTC** journal. The preceding
nullable-child work was fully gated; `codex-wip-20260907` was already an ancestor.
Resumed its promised unchanged **99954bab** native result/output boundary. Three
agents prepared escape, output and lifetime checks; root integrated and gated them.

**850cada4** admits global observations only when the complete source-store lattice
fits an existing scalar or String carrier. Optional observations print actual
Null/Undefined/Number/Boolean/String tags; definite observations keep their existing
tag checks. No return type, Map membership or Host annotation supplies new authority.
Unsupported mixed String/scalar and object outputs still refuse. Both layouts,
GCC/Clang, both optimization policies and no-Script checks pass for **21 typed globals /
nine functions**, including negative zero, NaN, embedded-NUL Strings, missing/present
fields, saved early reads, wrong guards and forged reports. The original split-JS
statements remain unchanged. Historical **size_one_present_field** and
**constant_undefined_candidate** now execute **5/5 native** in both modes; stale/fresh
controls and their unchanged refusal siblings pass.

**382dff40** executes the unchanged **99954bab** as **4/4 native**, both modes,
**nine calls**, complete proof budget **2479 / 29 incomplete cutoffs**. Seven other
historical nullable sources also become native; six appended sources observe actual
Undefined after sibling replacement/delete/clear. All **40 historical JavaScript
bodies** remain byte-identical. The cohort passes **24 native programs / 22 refusals**,
**111 typed Node/VM observations / 125 distinguishing mutations** in **41.87s**.
Both C++ layouts pass GCC/Clang/no-Script and ASan/UBSan/leaks through **128 future
calls**, saved children, detached methods, outer recreation, entry reexecution and
final release. Original uncalled-removal **0205c78a** remains **0/6**.

**1b5f45e6** executes the original String-global case (**2/2 native**) and
Null/Undefined getters (**3/3 native**, both modes); **71bfc11d** executes original missing Map result
**d3b7dcee** (**4/4**). Their exact Node/VM tags, both C++ layouts/compilers, no-Script
and stale/fresh proof checks pass without changing the historical source bodies.
**f57cab15** executes original complete-field controls **b5804dcd / 01292145**
as **5/5 native** in both modes. Exact Node/VM/native output, both layouts/GCC/Clang,
no-Script and stale/fresh/rerun controls pass in **10.96s**. A source census checks
**67 historical controls under both policies**: only these two gain native output;
the other **65 still refuse**. Rerunning native lowering rejects the old source
authority and retains every function; its existing duplicate forward declarations
do not imply byte-identical C++ on rerun. Fresh forged-source output is identical.

**14920c37** proves original canonical String array indices through the existing
origin, bounds and budget checks. Sparse/missing elements, lookalike spellings,
computed keys and ordinary-object/prototype effects remain refused. The old escape
fixture is an exact **26,888-byte prefix**. Four appended witnesses observe ten
literal sites, four confined and six retained; the provisional final-array PC **29**
was corrected to measured **27**, with all verdict rows unchanged. The fixture
measures **811 sites / 33 unclaimed / zero violations / precision 13/152**.

The **262-step rebuild** passes. The corrected gate passes **13/13 focused CTests
in 0.85s**, all four affected lit tests pass **2.98s**, and the historical ownership
controls pass both layouts/compilers. Stable clang-format22 passes **800 files**;
bundled23 retains exactly the same **nine baseline files / 28 diagnostics**. No
browser/runtime, inference, Map ownership or carrier definition changed.

Validation is complete across two runs. The full run at **71bfc11d** passed all
**533 CTests outside lit**, including **162 browser tests**, in **874.79s**. After
the three-file test correction **f57cab15**, the **ctcompile_lit** CTest passes
**168/168 cases in 633.32s** (CTest **633.41s**). Together these cover all **534
CTests** on identical production, browser and parser inputs; only the three test
files differ between runs. All **1,368 frozen inputs** match locally and remotely.
Fresh full Bootstrap is **19/574 native** in both modes; Data remains **0/7 CommonJS,
0/7 browser, 0/8 AMD**. The exact ordinary Data **8359592c** probe is still **0/7**
in both modes, with `property receiver lacks a fresh own-data object proof`.

Next: exact Data's mixed Number/field-bearing caller payloads, owning object
returns/identity, and its original recorder callback (`Array.from(s.keys())` and the
template diagnostic). First extend `HostContract/Analysis.cpp::capturedMapParameters`: its
complete-use census currently excludes field-bearing caller objects and requires one
primitive/object category per formal. Child writes and object returns need independent
ownership proofs; reuse the existing `object_value` and identity-field carriers. Preserve
the exact **2,522-byte SHA256 8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3**
source. Component/DOM ownership follows through ctbrowser public APIs.
The next independent escape increment is original decimal BigInt array indices.
Details: `/tmp/ctcompile-output-next.md`, `/tmp/ctcompile-string-indices-next.md`.

Evidence: `/tmp/ctcompile-output-complete-summary.json`,
`/tmp/ctcompile-output-final-measured/summary.json`,
`/tmp/ctcompile-output-final.log`,
`/tmp/ctcompile-output-final-{detail.log,frozen.json,revision.json}`,
`/tmp/ctcompile-field-output-{gate.log,detail.log,frozen.json}` and
`/tmp/ctcompile-field-output-recovery.patch`.
Parser remains **8eb3375**; reread Claude's pending runtime/ABI journals through
**14:12:58 UTC** after integration, including live Map iterators, overridden `next`,
microtasks, array holes, for-of, private-name checks and the newer parser commits.
The pending differential row now expects `6/cba/TypeError` for number iteration.
Re-gate these source witnesses after that integration. Nothing was pushed.

## Nullable child contents and dynamic mixed Add, 2026-09-12

Continued clean **14dabe42** and the **09:21:23 UTC** journal. The prior guard
recovery was fully committed/gated; `codex-wip-20260907` was already an ancestor.
Resumed the promised unchanged **99954bab** nullable-child boundary. Agents split
independent ownership checks, source/lifetime tests and the next escape increment;
root completed the gates and corrections after two agents hit rate limits.

**23a27ec1** proves dynamic mixed BigInt Add retention using the existing original
primitive provenance checks. Its result never gains BigInt authority; separate String
proof, every structural continuation, handler/effect exclusions and budgets remain.
The existing matrix passes **57 rows / 43 live states / 3,567 budget cutoffs**.
The old escape fixture remains an exact **24,720-byte prefix**. Three appended
witnesses distinguish Number9, saved-child retention and independent TypeErrors;
measured Error PCs **24/29/11** and all literal claims pass. The oracle measures
**797 sites / 33 unclaimed / zero violations / precision 11/148**.

**8f776874** closes a homogeneous scalar category across every child write,
independently of required-key `childEntries`. Empty publication, delete and clear
preserve only this category; every read may still be Undefined. Ownership separately
rechecks the full write/publication census. Strict equality against Null/Undefined
filters the existing scalar alternatives without skipping structural effects.
Twenty-one source/prepared rows and **24,145 incomplete budgets**, forged reports,
wrong keys, sibling orders, cyclic/opaque writes and Null distinction checks pass.

The first build caught a new test initializer-list type mismatch; its explicit
`mlir::Value` conversion fixes it. The corrected **250-step rebuild** and **19/20**
focused tests pass. The last test had fourteen obsolete ownership-refusal assertions;
corrected assertions check missing membership/scalar-read authority instead. The
subsequent **two-step rebuild and ownership CTest pass in 229.58s**, completing all
20 focused checks. Stable clang-format22 passes **800 files**; bundled23 reports the
same **nine baseline files / 28 diagnostics**. No browser/runtime/carrier/emitter
source changed.

**1320795f** adds separately observed source controls **9017c484** (prior value,
**4/4 native / nine calls**) and **2f7dad4b** (dynamic String keys, get-or-null and
last-child removal, **6/6 native / sixteen calls**), both modes. Their entry observes
`get(...) === 41`; the dynamic control also calls `remove('missing')` to establish
its current formal category. Both retain the original method bodies and reuse the
existing `nullable_scalar` carrier. All **31 historical source bodies** and exact
Data **8359592c** remain unchanged. The native cohort passes **10 native programs /
30 refusals**, **80 typed Node/interpreter observations / 66 distinguishing mutations**
in **26.42s**. Both C++ layouts pass GCC/Clang/no-Script and lifetime sanitizers
through **128 future calls**, previous values, saved-child clear, dynamic key ownership,
child deletion/outer recreation, detached methods, entry reexecution and final release.
Complete proof budgets/cutoffs: **2499/32** and **13397/32**.

The unchanged prior-read source **99954bab** now proves ownership but remains **0/4
native**: native inference joins its return to Opt Num, while direct global output
requires a definite scalar. Eight historical cases now own their Maps while retaining
this native refusal. Their preparation preserves actual receiver/callee/capture edges,
call counts and source effects; stale/forged fingerprints still reject. Original dynamic
source **0205c78a** remains **0/6** without ownership: its parameterized removal has no
current call. These sources were preserved separately from the execution controls.

The standard devbox gate passes **534/534 CTests in 1022.85s**, including
**162 browser tests** and **168/168 lit cases in 677.51s**. All **1,367 frozen
code/test/parser inputs** match locally and remotely, including the committed changes.
Fresh full Bootstrap measures **19/574 native** by default and
**19/574** with optimization disabled. Fresh Data remains
**0/7 CommonJS,
0/7 browser,
0/8 AMD**. Full native Bootstrap is unfinished.

Next: carry the original nullable result through an independently proved native
result/output boundary, then exact Data's mixed/field-bearing payloads, object
returns/identity and recorder callback. Component/DOM ownership follows through
ctbrowser public APIs. The next escape increment is canonical literal String array
indices: runtime **aca7091c** already fixed them, but `ownArrayIndex` still rejects
"0" with a stale disagreement comment. Details: `/tmp/ctcompile-nullable-next.md`
and `/tmp/ctcompile-nullable-escape-next.md`.

Evidence: `/tmp/ctcompile-nullable-full-summary.json`,
`/tmp/ctcompile-nullable-full-measured/summary.json`,
`/tmp/ctcompile-nullable-boolean-focused.log` and
`/tmp/ctcompile-nullable-boolean-probe.log`; earlier focused logs preserve the test
corrections. Parser remains **8eb3375**. Claude's pending **08:00:13 / 08:28:55 /
09:42:23 / 10:48:07 UTC** runtime/ABI journals must be reread after integration,
including the shared `CTJS/Lowering/EmitC/Status.cpp` block-order fix, declaration
hoisting and wrapper/proxy/array changes. Nothing was pushed.

## Recovered inverted Map guards and mixed static BigInt retention, 2026-09-12

Resumed **32957956** after the **08:21:14 UTC failed loop**. Its only dirty compiler file
was `CapturedMapBody.cpp`: Not unwrapping had been started but its owner/source gates were
unfinished. The synchronization journal named three interrupted parallel tasks; agents
completed ownership controls, repeated-lookup execution, and mixed static BigInt
retention. The old `codex-wip-20260907` branch was already an ancestor.

**2830aee0** completes captured-Map guard polarity and scalar filtering. The actual Data
removal IR already puts its continuation in the opposite arm of `if (!has) return`; no CFG
or frame-exit rule changed. Thirteen additional source/prepared rows cover odd/even Not,
wrong keys, stale observations, mutations, scalar key filtering and the original common
frame exit. All **81,987 incomplete budgets** across six new source/prepared forms and
hostile live edits withhold ownership evidence. The first gate exposed six test-cleanup
failures: a forged Map annotation had to be removed along with restoring the operand to
recover the original source fingerprint. Production fingerprinting and proof rules were
unchanged.

**394b5b91** adds repeated child lookup **3463e38c** and inverted guard **a3c06f9c**:
**4/4 and 5/5 native functions**, respectively, in both modes, with **nine calls /
trace=41** each. All **28 historical JS sources** remain identical. The expanded cohort
passes **8 native programs / 23 refusals**, **55 typed Node/interpreter observations / 36
distinguishing mutations** in **18.39s**. Complete proof budgets are **2118/31 cutoffs**
and **4610/30**. Both C++ layouts pass GCC/Clang, no-Script checks and ownership
sanitizers through **128 future calls**, saved children, clear/replacement, entry
reexecution and final destruction.

**66965ef7** finishes mixed static BigInt error retention across all seven supported
static operators. Each operand needs independent original primitive provenance. An error
has its own origin and never acquires a BigInt category; object/opaque operands,
unsupported effects and handlers still refuse. The old escape fixture remains an exact
**22,451-byte prefix**; three appended functions check Number4, saved-child retention and
distinct TypeErrors. The runtime confirms six literal sites/twelve instances/three
retained and Error PCs **24/29/11**, with no source claims for the Errors. Focused escape
checks pass **11/11**, including arrays **1.04s** and the oracle fixture **1.61s**. The
fixture measures **785 observed sites / 30 unclaimed**, zero violations, partial or
pending cases, precision **10/145**.

The **259-step rebuild** passes. Nineteen focused CTests pass initially; the corrected
**two-step rebuild** and ownership CTest pass in **222.13s**, completing all twenty
focused checks.

**The standard gate passes 534/534 CTests in 1006.93s**, including **162 browser tests**
and **168/168 lit cases in 667.41s** (CTest wrapper **667.49s**). All **1367 selected
code/test/parser input hashes** match locally and remotely. The snapshot recorded
**66965ef7** before the guard/source commits, which contain identical bytes. Fresh full
Bootstrap remains **19/574 native functions** in both modes; exact Data remains **0/7
CommonJS, 0/7 browser and 0/8 AMD**. The final source cohort independently confirms **8
native / 23 refusals**, and the escape oracle confirms **785 sites / 30 unclaimed / zero
violations / precision 10/145**. Stable clang-format **22.1.8** passes **800 files**;
`tools/format.sh --check` with bundled23 retains exactly the same **nine baseline files /
28 diagnostics**. No browser, runtime, native emitter or carrier source changed. Full
Bootstrap and exact Data remain unfinished.

**Next boundary:** preserve `nested_map_conditional_unknown_contents` **99954bab** (still
**0/4 native** in both modes, nine calls) and prove nullable prior child contents across
every sibling method. Independently close a homogeneous scalar write category, separate
from the existing required-key `childEntries` theorem. Empty publication, deletion and
clear do not establish membership; unknown or incompatible writes must withhold the
category. Try the existing optional native Map reads and nullable scalar helpers before
adding lowerings. Dynamic String keys already fit parameter plumbing. Exact Data
**8359592c** additionally needs mixed/field-bearing caller payloads, object
returns/identity and its original recorder callback, including `Array.from(s.keys())` and
the template diagnostic. Component/DOM ownership then uses ctbrowser public APIs. Detailed
source/IR pointers: `/tmp/ctcompile-guard-next.md`.

This gate uses parser **8eb3375**. Claude's **08:00:13 / 08:28:55 UTC** journals describe
pending unresolved-global/soft-typeof ABI, inferred names, iterator destructuring, eager
generator prologues, private names, generator finally and strict-write semantics. Re-read
the latest journal and synchronize the recorded submodule after integration before judging
an oracle disagreement.

Evidence: `/tmp/ctcompile-guard-{focused.log,focused-detail.log,
corrected-focused.log,corrected-focused-detail.log,full.log,full-detail.log,
full-summary.json,full-frozen.json,full-revision.json}`,
`/tmp/ctcompile-guard-full-measured/summary.json` and
`/tmp/ctcompile-guard-mixed-static-observed.json`. Repository changes are committed
locally; external plan journals are updated. Nothing was pushed.

## Retained child Maps across calls, 2026-09-12

Continued clean **d5b9d767** and the **07:03:38 UTC** journal, whose next
boundary was unchanged cross-invocation **369d7cea/bdf9931e**. Three agents split
body/frame validation, independent presence, and ownership/execution controls;
root integrated their patches and serialized every devbox gate and commit.
Two agents hit rate limits after completing their patches.

**836ed8ed** proves one constant String entry and scalar category before every
fresh child's publication, independently of family invocation results. Every
child mutation must preserve it. Ownership separately rechecks all seeds/writes;
returned identity, cardinality and other keys stay independent. Original frame
exits in scalar return branches receive full lifecycle/token/use validation,
including malformed, foreign, duplicate and missing exits and forged reports.

**0362b690** independently reconstructs NativeMap membership from the closed
owner's complete call census. A first pass proves initialization before each
publication and checks all preserving writes; a second supplies only the proved
entry on definite outer reads. It never uses Host entry summaries or native
annotations as membership authority. Same-key repeated reads keep the actual
returned alias until a possible replacement. Twelve new presence/alias rows run
on reused/fresh modules with forged reports and absent/present owner evidence.

**837f0c5f** promotes the original cross-invocation source to **5/5 native
functions in both modes**, retaining **nine calls / trace=41**. All **20 historical
JS bodies** stay byte-identical. The expanded cohort passes **6 native programs /
22 refusals**, **50 typed Node/interpreter observations / 27 distinguishing
mutations**, in **13.39s**. Both C++ layouts pass GCC/Clang, no-Script checks and
ASan/UBSan/leaks through **128 future calls**, saved children after replacement
and clear, host/table release, entry reexecution and final destruction. Complete
proof budget: cross-invocation **4443 / 31 cutoffs**; other positives **1794/30**,
**3084/32**, **2016/32**. Source/prepared owner tests preserve budget and hostile
mutation checks.

The corrected **321-step rebuild** passes; eight focused CTests pass, including
owner **198.18s** and seeded Host **40.31s**. A test-only Map-report fingerprint
correction then passes the rebuilt type CTest in **0.21s**; production fingerprinting
and proof rules are unchanged. Stable clang-format22 passes **800 files**;
bundled23 retains the same **nine baseline files/28 diagnostics**. No browser,
runtime, carrier or emitter source changed.

**The standard gate passes 534/534 CTests in 972.71s**, including **162 browser
tests** and **168/168 lit cases in 665.42s** (CTest wrapper **665.53s**). All
**1367 selected code/test/parser input hashes** match locally and remotely; the
snapshot was taken before the final code commits, which contain identical bytes.
The fresh full Bootstrap result remains **19/574 native functions** in both
modes. Exact Data remains **0/7 CommonJS, 0/7 browser, 0/8 AMD**. The escape
fixture still measures **773 observed sites / 27 unclaimed**, zero violations,
partial or pending cases, precision **9/142**. The final native cohort independently
confirms **6 native / 22 refusals**, with cross-invocation **5/5** in both modes.
Generated C++ keeps the original setter, guarded getter and scalar return arms,
using the existing owning Maps and typed callables without Script symbols.

**Next boundary:** preserve exact ordinary Data **8359592c** (seven functions,
40 calls, 19 numeric observations). It publishes empty children, uses dynamic
String keys, permits deletion, and returns nullable/mixed scalar or field-bearing
object payloads. Its `if (!t.has(e)) return` also needs Host inversion/reaching
continuation proof; NativeMap already handles the inverted predicate. Object
returns/identity and its exact recorder callback (including `Array.from(s.keys())`
and the template diagnostic) remain separate obligations. Component/DOM ownership
follows through ctbrowser's public subsystem APIs. Do not generalize this single
required-entry invariant to Data's empty child publication and deletion. The
same-key repeated lookup now has independent presence/alias rows; a corresponding
source-level native promotion still needs its own unchanged-source measurement.
See `/tmp/ctcompile-inner-next.md`. Full native Bootstrap remains unfinished.

This gate uses recorded parser **8eb3375**. Claude's **08:00:13 UTC** journal
records unintegrated unresolved-global/soft-typeof ABI changes, inferred names
and image version 5, iterator destructuring, eager generator prologues and yield
delegation. Re-read that journal and synchronize the recorded submodule after
integration before interpreting a new oracle disagreement.

Evidence: `/tmp/ctcompile-inner-{corrected-focused.log,test-fix-focused.log,
full.log,full-detail.log,full-summary.json,full-frozen.json,full-revision.json}`,
`/tmp/ctcompile-inner-full-measured/summary.json` and
`/tmp/ctcompile-inner-driver-node.json`. Repository code and this handoff are
committed locally; the external plan journals are updated. Nothing was pushed.

## Conditional child Maps and recovered UShr, 2026-09-12

Resumed the four dirty escape files left by the **05:36:18 UTC failed loop**
at **d7b131ec**, identified in the synchronization journal and working diff.
The parallel conditional Map thread had only claims; its source **74539aeb**
remained intact. Three agents prepared recovery, owner and source/lifetime work,
then hit rate limits; root completed the measured oracle pins and integration.

**b150141e** finishes static two-original-BigInt unsigned-shift retention.
The TypeError result has an independent origin and no BigInt category; whole-frame
exclusions and every structural continuation remain. Seven new bounded rows,
live mutations and all incomplete budgets run in the existing array matrix.
The preserved source witness distinguishes Number4, saved-child retention and
three independent TypeErrors at measured PCs **24/29/11**. Six literal sites
have twelve instances, three retained; Error sites acquire no source claims.
The corrected focused gate passes **11/11 CTests in 0.79s**. The current oracle
measures **747 claims / 773 observed sites / 27 unclaimed**, zero violations,
partial or pending cases, and precision **9/142**. Historical fixture bodies
are unchanged; ordinary-object own-data/prototype authority remains absent.

**9c201b2d** proves the complete captured family's outer child-Map payload kind
before invocation results. A returned child has its own runtime identity and
unknown contents. Its following set can establish a read fact; possible child
aliases invalidate mutable facts, while saved owners survive outer mutations.
Independent ownership rechecks the complete constructor/write and returned-use
censuses. Existing native schemas and C++ carriers suffice.

**8b153339** promotes the unchanged conditional source **74539aeb** (compiler
input **660da1c0**) to **4/4 native functions**, both optimization modes, retaining
**8 calls / trace=41**. All **16 historical nested sources** remain byte-identical;
four new refusal controls cover mixed sibling payloads in both declaration
orders, unknown prior contents and possible child aliases. The cohort passes
**5 native programs / 15 refusals**, **25 typed Node/interpreter observations /
15 distinguishing mutations** in **9.72s**. Both C++ layouts pass GCC/Clang,
no-Script checks and ASan/UBSan/leaks through **128 future calls**, child reuse,
outer clear/replacement, released owners, entry reexecution and final destruction.
Complete proof budgets are **1777/30 cutoffs**, **3049/31** and conditional
**2007/32**. The ownership CTest passes in **200.79s**; other focused Host/type
checks pass. The stronger cross-invocation source **369d7cea/bdf9931e** stays refused.

The integrated runtime exposed two separate compiler gate issues.
**05840f4e** fixes operator-table early continues in the importer, which skipped
the common catch-edge emission for binary/compare/unary instructions. The
emitter's handler-snapshot safety guard remains. The original async source now
compiles; the expanded existing async lit case passes in **0.10s**.
**211e53ea** updates only the historical array-string-index differential
expectation after runtime **aca7091c**: unchanged Number0/String0 sources both
produce **909/909**, confirmed with Node and interpreter/AOT agreement.
The **342-step rebuild** and corrected **5/5 integration CTests in 1.18s** pass;
the differential covers **47 arms with all 53 entries installed**, zero tier
mismatches. Stable clang-format **22.1.8 passes 800 files**; bundled23 retains
the same nine baseline files/28 diagnostics, with only browser line shifts.
No browser, runtime, native emitter or carrier implementation changed.

**fcc11c60** updates the preserved exception/generator controls for the
integrated runtime. The obsolete null-property divergence is removed: runtime
**8257ebf7** correctly throws, and the original source catches **42**. The async
generator retains its real `await_value` suspension refusal. The importer repair
also promotes unchanged `computed_throw` to **2/2 native functions**, both
optimization modes, retaining runtime string addition and typed throw/catch.
The existing workflow passes **42 Node/interpreter programs**, **21 native /
21 refusals**, both C++ layouts with GCC/Clang/no-VM checks and owning-string
ASan/UBSan. All historical exception fixtures retain hash **777b9173**. The
corrected focused run passes **2/2 lit cases in 36.67s**.

**The corrected standard gate passes 534/534 CTests in 817.28s**, including
**162 browser tests** and **168/168 lit cases in 552.76s** (CTest wrapper
**552.83s**). It ran on **fcc11c60** with no rebuild needed; all **1367 selected
code/test/parser input hashes** match locally and remotely after the gate.
Fresh full Bootstrap remains **19/574 native functions** in both modes; exact
Data remains **0/7 CommonJS, 0/7 browser and 0/8 AMD**. The escape fixture still
measures **773 observed sites / 27 unclaimed**, zero violations, partial or
pending cases, and precision **9/142**. The final nested cohort independently
confirms **5 native programs / 15 refusals**, including the unchanged conditional
source at **4/4** and cross-invocation source at **0/5**, both modes.

The initial browser failures were stale submodule bookkeeping: the runtime merge
recorded parser **8eb3375**, while the clean checkout remained pre-merge
**4c1d5f3**. Standard submodule update under the Git lock synchronized the recorded
revision; no parser source or gitlink was edited. Both browser cases then passed.
Read Claude's latest journal before interpreting later runtime changes.

**Next boundary:** preserve cross-invocation **369d7cea/bdf9931e** (five functions,
nine calls) and establish a complete-family inner-key/type invariant separately
from child kind and identity. Every retained child must have its key initialized
before publication, and every reachable mutation must preserve the invariant.
NativeMap presence needs independent evidence too. Validate original entry-frame
exits in both scalar return branches; they are currently refused below top level.
The measured source needs no Map-valued branch-result support. The same-key repeated
lookup regression establishes Host/owner support only; independently measure
its native presence before claiming that sibling. Exact ordinary Data **8359592c** additionally needs
mixed/field-bearing payloads, object returns and its recorder callback; component
and DOM ownership follow. Full native Bootstrap is unfinished.

Evidence: `/tmp/ctcompile-conditional-{recovery-final.log,main-focused.log,
corrected-focused.log,lit-fetch.log,measured/summary.json}`,
`/tmp/ctcompile-conditional-recovery-observed/ushr-joined.json`,
`/tmp/ctcompile-conditional-source-tests-static.json`,
`/tmp/ctcompile-conditional-exceptions-{focused.log,measured/native.json}`,
`/tmp/ctcompile-conditional-verified-{full.log,full-detail.log,full-summary.json,
frozen.json,revision.json}` and
`/tmp/ctcompile-conditional-verified-measured/summary.json`.

## Fresh child Maps, 2026-09-12

Continued clean **bc639f5d** and the **04:11:42 UTC** synchronization journal.
The interrupted payload work was already gated; `codex-wip-20260907` was
already an ancestor. Independent agents handled owner validation, source/lifetime
regressions and the subsequent presence fix; root integrated and gated them.

**26b68622** proves fresh method-local child Maps retained by a captured outer
Map. Each runtime origin has independent contents, presence, cardinality and
saved aliases; structural branches join those states separately. The owner
independently checks standard empty constructors, all uses and root-to-child
retention. Child-to-Map ownership and Map returns remain refused.

**d2c9453a** fixes the independent native presence gap exposed by those original
sources: a definite outer `get` now retains its exact stored child's alias.
Replacement/delete/clear preserve an already saved child; uncertain writes,
summary effects and unequal branch origins remove unproved entry facts.
Distinct fresh allocations can share a C++ schema without sharing mutation
state; formal/unknown aliases remain conservative. **13 source rows** run on
reused/fresh modules with forged annotations and executable-source preservation.
No browser, runtime, carrier or emitter source changed.

**0f3ea6f9** adds `--group nested-maps` to the existing
`ctcompile/test/CTNative/Ownership/global-maps.py` workflow and its full lit run.
All four preserved witnesses (**81e86260**, **1279bc27**, **1507cc30**,
**6d5cf26f**) admit **4/4 native functions** in both optimization modes, retain
**8/9/15/15 calls**, respectively, and produce typed **trace=41**.
The cohort passes **4 native programs / 12 refusals**, **20 typed Node/interpreter
observations / 11 distinguishing mutations**. Both C++ layouts pass GCC/Clang,
no-Script symbol checks and ASan/UBSan/leaks, including saved children after outer
replacement/delete/clear, released host/table owners, **128 future calls**, entry
reexecution and final destruction. Complete proof budgets are **1759 / 33
cutoffs** for retained and **3007 / 31** for distinct saved children.
All **55 historical object-key sources** and exact ordinary Data **8359592c**
remain unchanged; fixture and compiled-input hashes differ by the import
helper's appended newline.

The corrected **313-step core rebuild**, **5/5 focused CTests in 188.58s**,
**256-step presence rebuild**, native cohort in **7.67s** and focused type CTest
in **0.19s** pass. Stable clang-format **22.1.8** passes **795 files**; bundled-23
retains the same **nine byte-identical baseline differences**.
The corrected full standard gate passes **530/530 CTests in 803.21s**,
including **158 browser tests** and **168/168 lit cases in 547.96s**. All **1241
frozen code inputs** match locally/remotely before and after the gate.
Fresh full Bootstrap remains **19/574 native functions** in both modes; exact
Data remains **0/7 CommonJS, 0/7 browser and 0/8 AMD**.

**5a4887d1** promotes the historical fresh-empty-Map result refusal identified
by the first full gate. The unchanged source **85aa6fa6** / compiled input
**cadaaa14** admits **6/6 native functions**, preserves **15 calls / trace=2**,
and returns Undefined from a distinct empty Map while preserving captured-state
writes. Both policies, exact repairs and fresh/stale forgeries pass; both C++
layouts pass GCC/Clang, no-Script checks and ASan/UBSan/leaks through **128 future
calls**, host/table release, entry reexecution and final destruction. The
corrected focused historical-result group passes in **11.82s**, including the
remaining refusals; all source catalogs are unchanged.
Evidence: `/tmp/ctcompile-nested-{host-gate,alias-focused,full}.log`,
`/tmp/ctcompile-nested-full-detail.log`,
`/tmp/ctcompile-nested-final-{focused.log,full.log,full-detail.log,full-summary.json,frozen.json}`,
`/tmp/ctcompile-nested-final-measured/{summary.json,foreign-summary.json}`,
`/tmp/ctcompile-nested-map-tests-static.json` and
`/tmp/ctcompile-nested-foreign-tests-static.json`.

**Next boundary:** preserve `nested_map_conditional_initialize` (**74539aeb**
fixture / **660da1c0** compiled input, four functions/eight calls) and prove its
`t.has(1) || t.set(1, new Map)` followed by child readback. Establish a positive
complete-family outer payload-kind proof before invocation results. Membership
alone cannot prove a child Map, and a constructor site cannot identify a child
retained by an earlier invocation. A proved returned child starts with unknown
contents; its following `saved.set` can establish the local read fact. Preserve
separate identity and may-alias invalidation. Its measured prepared IR has a
resultless `scf.if` with an empty present arm and child creation in the absent
arm; no Map-valued branch-result support is needed for this source.
The stronger cross-invocation source (**369d7cea** / **bdf9931e**, five functions/
nine calls) also needs an independently preserved inner-key/type invariant and
validation of original frame exits in both return branches. Host currently
rejects frame exits below the top level. Both remain measured refusals. Exact
Data additionally needs mixed/field-bearing payloads, object returns and its
recorder callback; component/DOM ownership follows. See
`/tmp/ctcompile-nested-next.md`. Full native Bootstrap is unfinished.

**Former parallel follow-up, completed by b150141e above:** the static
original-two-BigInt `UShr` retention proof and source/error controls are now
landed. The older audit is `/tmp/ctcompile-nested-escape-next.md`.
Ordinary-object assignment still needs separate own-data/prototype authority.

## Caller-owned Map payloads, 2026-09-11

Resumed the seven dirty files left by the **03:04:07 UTC failed loop**,
found in the synchronization journal and diff at **3255556d**. Two agents then
hit rate limits; root recovered their partial tests, completed the wiring and
reviewed all independent consumers. **added9fd** commits the recovered work.

Checked empty caller objects can now be retained at the exact `Map.set`
payload position. The complete family accounts for those writes before any
invocation return facts, preventing an unseeded sibling getter from inheriting
primitive-only contents. Source allocations, global stores/loads and actual
arguments remain intact. Existing identity ownership, `object_value` storage
and C++ emission suffice; no runtime helper or Script dependency was added.
Caller fields, outgoing edges, object returns, mixed actuals and unknown uses
remain refused. Tests cover both family orderings and source/prepared forms;
scalar reseeds and object-key-only primitive families retain their proofs.

Unchanged compiled inputs **ca0f13c2** and **a056b669** now admit **4/4 native
functions**, preserve **five calls** and produce **trace=1** in both modes.
Their fixture hashes remain **7f6601d7** and **9e803ae5**; the import helper
appends one newline. A scalar-key payload-only source admits **6/6**, ten calls,
trace=1, so key ownership cannot conceal a missing payload owner.
All **47 historical source bodies/hashes/call counts/traces** are unchanged.
The expanded cohort passes **19 native programs / 36 refusals**, **55 source
rows / 65 typed Node/interpreter observations / 36 distinguishing mutations**.
Both C++ layouts pass GCC/Clang and ASan/UBSan/leak checks, including caller and
global release, replacement/delete/clear, saved callables, **128 future rounds**,
entry reexecution and final owner destruction. Complete budgets are
**1763 / 32 cutoffs**, **1622 / 31** and **14072 / 31** for the three payload cases.

The **320-step devbox rebuild**, **8/8 focused CTests in 1.35 seconds** and
all-CPU object-key workflow in **39.85 seconds** pass. All **1240 frozen inputs**
match the devbox. Stable clang-format **22.1.8** passes **795 files**; the actual
bundled-23 check retains the same **nine byte-identical baseline differences**.
The corrected full standard gate passes **530/530 CTests in 783.53 seconds**,
including **158 browser tests** and **168/168 lit cases in 534.83 seconds**.
All **1240 frozen inputs** still match locally/remotely after the gate. Fresh
full Bootstrap remains **19/574 native functions** in both modes; exact Data
remains **0/7 CommonJS, 0/7 browser and 0/8 AMD**. No bundle increase is claimed.
Evidence: `/tmp/ctcompile-payload-resume-{focused.log,frozen.json}`,
`/tmp/ctcompile-payload-resume-final-full{.log,-detail.log,-summary.json}`,
`/tmp/ctcompile-payload-resume-static/summary.json` and
`/tmp/ctcompile-payload-resume-measured/summary.json`.

**Next Bootstrap boundary:** preserve the existing exact Data ordinary
publication source **8359592c** (2,522 bytes, seven functions, 40 calls,
19 observations). Prove a fresh child Map retained by the captured outer Map
and its guarded readback, keeping each Map identity and mutation state separate.
Start in `HostContract/CapturedMapBody.cpp`: its current `maps` set means
aliases of one captured allocation, so adding a child to that set is unsound.
Extend the captured-family and independent `OwnedGlobalMethods` proofs with
separate child identity, contents and presence facts. Reuse `NativeMap` nested
schemas/presence and existing EmitC storage; local `Maps/object-key.js` and
`Maps/nested-map.js` already provide smaller ownership fixtures. Exact Data also
needs field-bearing/mixed payloads, object returns and its recorder callback;
BaseComponent then needs component and DOM ownership. Earlier measured Data
publication refusals are not a measured nested-Map diagnostic. See
`/tmp/ctcompile-payload-resume-next.md` and
`/tmp/ctcompile-object-resume-exact-data/exact_data_ordinary_publication.js`.
Full native Bootstrap and direct browser integration remain unfinished.

**f9bbbb09** separately extends dense-array retention across primitive mixed
BigInt Pow errors, using the same original-operand proof as Sub/Mul/Div/Mod.
It grants no normal-completion, native type or ordinary-object prototype facts.
The existing matrix now executes Pow: **57 rows, 43 live states and 3,450 budget
cutoffs** pass. The focused gate passes **8/8 in 1.37 seconds** after correcting
historical Pow expectations while retaining every source body. The live BigInt
array-index mutation still refuses. The inherited-accessor oracle remains
**760 observed sites, 24 unclaimed, zero violations, precision 8/139**.
Evidence: `/tmp/ctcompile-payload-resume-pow-fixed.log` and
`/tmp/ctcompile-dense-array-pow-{static,expectations-static}.json`.
**bf5605c0** corrects two historical key-only rejection expectations after the
first full run exposed them. Their unchanged source/prepared programs only
store the checked caller leaf and return Boolean/Undefined; the existing
positive identity/family/owner assertions now execute. Object-valued readback
and return refusals remain. The corrected ownership test passes in **248.62
seconds**, and the final full gate above is green. All code changes are committed.

Claude's **04:03:51 UTC** journal reports further DOM/JS/VM changes on
`ctbrowser-wpt`, including new DOM node kinds with a compatibility hunk in
`ctcompile/lib/HTML/DocumentComparator.cpp`, nullish property-access errors,
named array properties, indirect eval and native-call rooting. These changes
are outside this gate's frozen inputs. Re-read that journal when they integrate
before using the changed interpreter as the native differential oracle.

## Global key alias chains, 2026-09-11

Continued exact **511cca31** from **0e5cfbef** and the **2026-09-12
02:05:52 UTC** journal. The checkout was clean and preceding work fully gated;
old codex-wip work was already merged. Separate agents handled HostContract,
source/lifetime regressions and the next Bootstrap payload audit; root checked
all consumers and serialized the devbox gates. No browser/runtime source changed.

**2359a6dd** proves complete acyclic global alias chains for empty object Map
keys. A budgeted iterative predecessor walk checks every initialization;
a complete successor census finds intermediate and branched bindings before
checking all reads and uses. Independent owner validation rechecks every edge
in strict entry order before publishing anything. Original allocations, stores,
loads and call actuals remain intact. Existing native identity storage and C++
emission handle the chains without a new carrier or runtime helper.
Unread descendants, early/nonentry reads or stores, duplicate/later stores,
cycles, foreign consumers and object-valued scalar observations remain refusals.

Unchanged **511cca31** now admits **4/4 functions**, preserves **four calls**
and produces typed **trace=0**. New branched **629982b5** admits **7/7 functions**,
preserves **15 calls** and produces **trace=11**, covering three alias hops,
an intermediate used only by another initializer, a branch and two identities.
All **42 prior source bodies/hashes/function/call/trace values** are unchanged.
The focused cohort passes **16 native programs / 31 refusals**, **47 source rows /
54 typed Node/interpreter observations** and **24 distinguishing mutations**.
Both C++ layouts pass GCC/Clang execution; alias-only retention, saved callables,
**128 future rounds**, entry reexecution and final owner release pass lifetime
ASan/UBSan/leak checks. Proof budgets complete at **1931 / 32 cutoffs** for the
exact chain and **50830 / 30 cutoffs** for the branched family.

The **320-step devbox build**, final no-op rebuild and **4/4 focused CTests in
1.35 seconds** pass. The existing all-CPU `--group object-keys` workflow passes
in **32.43 seconds**. Stable clang-format **22.1.8** passes **795 files**;
the actual bundled-23 check retains the same **nine byte-identical baseline
differences**. All **1240 frozen input hashes** match the devbox.
The full standard gate passes **530/530 CTests in 769.26 seconds**, including
all **158 browser tests** and **168/168 lit cases in 527.53 seconds**. All
**1240 frozen inputs** still match locally/remotely after the gate. Fresh full
Bootstrap remains **19/574 native functions** in both modes; exact Data stays
**0/7 CommonJS, 0/7 browser and 0/8 AMD**. No bundle-count increase is claimed.
Evidence: `/tmp/ctcompile-chain-{build,focused,full}.log`,
`/tmp/ctcompile-chain-full-{detail.log,summary.json}`,
`/tmp/ctcompile-chain-frozen.json`, `/tmp/ctcompile-key-chain-tests-static.json`,
and `/tmp/ctcompile-chain-measured/summary.json`. The latter separates fixture
and compiled-input hashes: the existing import helper appends one newline.

**Next: caller-owned Map payloads**, unchanged **ca0f13c2**
(`object_argument_global_object_payload`, four functions/five calls/trace=1)
and anonymous companion **a056b669**, still refused in this focused run.
Permit a proved empty caller leaf at the exact `Map.set` payload position while
keeping property uses and object returns refused. The complete family must also
account for those payload writes **before** computing invocation return facts:
`HostContract/Values.cpp` currently detects nonprimitive contents only through
method-local object creation. Otherwise a sibling unseeded getter can retain an
incorrect primitive proof. Reuse the existing object-value carrier and add a
payload-only lifetime case: storing the same object as key and payload can hide
a missing payload owner. The actionable audit is
`/tmp/ctcompile-chain-next-payload.md`.

Actual Bootstrap Data additionally needs nested Maps and component/element
ownership, plus full host/publication proofs; these small isolates do not prove
full Data admission. Generic ordinary-object escape refinement still needs
independent own-data/prototype authority. Full native Bootstrap and direct
browser API integration remain unfinished.

## Global key aliases, 2026-09-11

Resumed exact **abbf4b9c**, then **600b8fb6**, from **4d0db5a6** and the
**2026-09-12 00:35:28 UTC** journal. The checkout was clean; the interrupted
named-key work was fully gated and committed. Three agents handled the host
proof, source/lifetime regressions and test parallelism; root integrated the
independent consumers and serialized all devbox gates. No browser/runtime
source changed.

**7e829db9** proves one immutable global alias hop for empty object Map keys.
Every original allocation, store, predecessor load and actual argument remains
in the source graph. The complete binding/use census publishes checked edges
only after the whole family succeeds; OwnedGlobalMethods independently
revalidates each initialization. Existing native identity storage owns both
globals and retains the same key in the Map, with no new carrier, runtime helper
or Script symbol. Chains, cycles, unread aliases, early/nonentry reads or
stores, later stores and foreign consumers remain refusals. Scalar observation
of any key/alias/other object binding also refuses in both modes.

The unchanged **abbf4b9c** admits **4/4 functions**, preserves **four calls**
and produces typed **trace=0**. Unchanged **600b8fb6** admits **7/7 functions**,
preserves **15 calls** and produces typed **trace=11** with aliases and two
distinct identities. Both optimization modes and explicit/deduced C++ layouts
pass GCC/Clang execution. Global overwrite, alias-only retention, saved
set/get/delete/clear callables, **128 future rounds**, entry reexecution and
final key/Map destruction pass ASan/UBSan/leak checks. Budgets complete at
**1776 / 31 cutoffs** and **46995 / 30 cutoffs**, respectively.
All **33 historical source bodies/hashes/call counts** remain unchanged. The
expanded cohort passes **14 native programs / 28 refusals**, **42 source rows /
47 typed Node/interpreter observation programs** and **19 distinguishing
mutations**. Two refusal controls leave undefined globals; the observer now
checks those names and their exact type counts in both engines. There is no
oracle discrepancy or skipped check.

**01aad175** initially adds `--jobs` to the existing ownership driver, defaulting
to one, and runs its existing full lit workflow with **two workers**. Only independent
positive-program checks run concurrently; source observations and refusal
controls keep their order. The same focused group passes in **69.11 seconds
serially / 41.82 seconds with two workers** (**39.5% lower wall time**), with
all **1251 generated text artifact hashes identical** after fresh runs in the
same work directory. The full matrix now has **427 positives / 1708 baseline
C++ compilations**; the focused group has **56 baseline plus 14 sanitizer
builds**. Run the existing `--group object-keys` command without `--jobs`
to use all available CPUs.

The **320-step rebuild**, final no-op rebuild and **4/4 focused CTests in
3.01 seconds** pass. Stable clang-format **22.1.8** passes **795 files**;
the actual bundled-23 check retains the same nine pre-existing differences,
byte-identical to the prior gate. The completed alias gate passes **530/530
CTests in 1719.28 seconds**, including **158 browser tests** and **168/168 lit
cases in 886.75 seconds**. All **1238 frozen committed input hashes** match the
devbox before/after. Fresh full Bootstrap remains **19/574 native functions**
in both modes; exact Data remains **0/7 CommonJS, 0/7 browser and 0/8 AMD**.

The user then requested full devbox CPU utilization. **ce7ea791** defaults the
Map program pool to available CPUs and reserves that CPU count for lit using
CTest's `PROCESSORS` property. **4d022514** changes `tools/remote-build.sh` to
query remote `nproc` and pass explicit build/CTest job counts; `CT_BUILD_JOBS`
and `CT_TEST_JOBS` retain overrides. This devbox exposes **8 logical CPUs and
31 GiB RAM**. Its CTest **3.28.3** ran the preset's `jobs: 0` serially; the gate
now explicitly uses **8 jobs**, while lit keeps its own eight workers.
The same focused cohort passes with eight workers in **26.30 seconds**, and
all **1251 artifacts** still match serial/two-worker results. The rebuild,
**4/4 concurrent focused CTests in 1.33 seconds**, and lit's **8-slot reservation**
check pass. The output-path audit found no collisions or missing dependency.
The final parallel gate passes **530/530 CTests in 767.76 seconds**, including
all **158 browser tests** and **168/168 lit cases in 519.55 seconds**. The same
530-test matrix dropped from **28m 39s to 12m 48s**: **55.3% less wall time /
2.24× faster**, saving **15m 52s**. All **1240 frozen input hashes** match locally
and remotely before/after; fresh full Bootstrap and exact Data counts remain
unchanged. The working code is committed and no browser/runtime source changed.
Evidence: `/tmp/ctcompile-all-cores-{focused,full}.log`,
`/tmp/ctcompile-all-cores-full-{detail.log,summary.json}`,
`/tmp/ctcompile-all-cores-{comparison,frozen,speedup}.json` and
`/tmp/ctcompile-devbox-parallel-probe.log`.
Evidence: `/tmp/ctcompile-key-alias-focused-fixed.log`,
`/tmp/ctcompile-key-alias-{full.log,frozen.json,matrix.json}`,
`/tmp/ctcompile-key-alias-parallel-comparison.json`,
`/tmp/ctcompile-key-alias-measured/`.

**Next: the second alias hop**, exact **511cca31**
(`object_argument_global_alias_chain`: four functions/four calls/trace=0),
measured refused in both optimization modes. Prove the complete bounded,
acyclic predecessor sequence and census every intermediate binding/use before
publication; preserve every load/store. Both HostContract's direct-predecessor
restriction and the independent owner restriction must advance together.
Reuse the current leaf identity and global lifetime checks for three owning
bindings. Unread one-hop alias **ddfc4ee9** remains a separate refusal.
The bounded review is `/tmp/ctcompile-key-alias-next-chain.md`.

The next existing Bootstrap-facing payload isolate is **ca0f13c2**
(`object_argument_global_object_payload`: four functions/five calls/trace=1).
It retains the caller's key with `t.set(e,e)` and still measures **0/4 native**
in both modes. Prove caller-owned payload retention independently of key use;
current nonprimitive payload admission covers only proved method-local leaves.
Actual Bootstrap Data at `bootstrap.bundle.js:13` also stores inner Maps, and
BaseComponent at line 300 passes an instance with `_element`/`_config` fields.
Those ownership requirements and full host/publication proofs remain open.
The second alias hop does not appear in Data; neither small isolate establishes
full Data admission or a bundle-count increase. Exact source hashes, measured
refusals and static requirements are separated in
`/tmp/ctcompile-key-alias-bootstrap-next.md`. Full native Bootstrap, direct
browser API integration and the independent ordinary-object own-data/prototype
escape proof remain unfinished.

## Named global object keys, 2026-09-11

Continued the exact **7573e89b** boundary left by **2df63e19** and the
**2026-09-11 23:03:27 UTC** synchronization journal. The checkout was clean;
the interrupted accessor repair and old codex-wip work were already landed.
Three agents handled HostContract provenance, source/lifetime regressions and
the focused workflow; root integrated native consumers and serialized gates.
No browser/runtime source changed.

**d7b2e3f0** proves an empty object allocation, its sole unconditional global
initialization and all dominated reads across the complete captured-Map call
family. The actual argument remains the source LoadGlobal; an independent
checked edge carries its allocation. OwnedGlobalMethods revalidates that edge
before existing identity, type, Map and owned-global emission consume it.
Generated C++ owns the global leaf identity and shares that same identity with
Map keys, using the existing acyclic ownership carrier. No Script/VM dependency
or new runtime helper is introduced. Early reads, second/late stores, global
aliases, fields, unknown consumers, object payloads/returns and incompatible
later actuals remain refused. Requesting the key as a scalar observation also
refuses in both modes; review caught and closed an emitter crash on that path.

The exact source now admits **4/4 functions**, preserves **four source calls**
and emits typed **trace=0** in both optimization modes. All **21 historical
source bodies/hashes/call counts** remain intact. The expanded cohort passes
**12 native programs / 21 refusals**, **33 source rows / 36 typed Node/interpreter
observations** and **16 distinguishing mutations**. Repeated global reads and
Map retention pass with GCC/Clang in explicit/deduced layouts. The lifetime
checks cover global overwrite, caller/table release, **128 future key rounds**,
entry reexecution and final key/Map destruction with ASan/UBSan/leak detection.
The exact global proof completes at budget **1626**, with **31 cutoffs** checked.

**e55cf329** adds `--group object-keys` to the existing ownership driver. It
reuses the complete object-key observer, native execution and refusal phases;
the default full workflow is unchanged. Current full matrix: **425 positives /
1700 baseline C++ compilations**. The focused group uses **48 baseline
compilations plus ten sanitizer builds**. Run on the devbox under the build lock:

```sh
PYTHONPATH=ctcompile/test python3 ctcompile/test/CTNative/Ownership/global-maps.py \
  --translate build/ctcompile/tools/ctjs-translate/ctjs-translate \
  --opt build/ctcompile/tools/ctjs-opt/ctjs-opt \
  --node "$(command -v node)" --reference build/ctcompile/test/ctcompile-test-native-reference \
  --work /tmp/ctcompile-object-keys --group object-keys
```

The final **255-step rebuild** and **4/4 focused CTests in 1.71 seconds** pass.
Stable clang-format **22.1.8** passes **795 files**; bundled 23 has the same nine
pre-existing differences, byte-identical to the previous gate. All **1238
frozen input hashes** match locally and remotely before and after the gate.
The full standard gate passes **530/530 CTests in 2379.99 seconds**, including
all **158 browser CTests** and **168/168 lit cases** (lit **1544.74 seconds**).
Full Bootstrap still measures **19/574 native functions** in both optimization
modes; broader ownership work remains.
Evidence: `/tmp/ctcompile-global-key-final.log`,
`/tmp/ctcompile-global-key-full-{detail.log,summary.json}`,
`/tmp/ctcompile-global-key-{frozen,matrix}.json`,
`/tmp/ctcompile-global-key-measured/`,
`/tmp/ctcompile-global-key-test-static.json`.

**Next: one immutable global alias**, tracked **abbf4b9c**
(`object_argument_global_alias`: four functions/four calls/trace=0), then
unchanged **600b8fb6** (`object_argument_siblings_global`: seven functions/
fifteen calls/trace=11, aliases and two identities). The missing proof is the
LoadGlobal-to-StoreGlobal initialization edge, including predecessor loads
used only to initialize an alias. Keep each original store/load and resolve
its allocation through checked predecessor edges; revalidate all names and
uses before publication. Reuse current identity storage and retained-key
lifetime checks, accounting for every owning global at overwrite/reentry.
Both sources remain measured refusals. The bounded audit is
`/tmp/ctcompile-global-key-next-boundary.md`. Full native Bootstrap, field and
nested-Map ownership, direct browser API integration, and the independent
ordinary-object own-data/prototype escape proof remain unfinished.

## Inherited accessor proof repair, 2026-09-11

Resumed the interrupted ND-3 repair recorded by **bff05404** and the
**2026-09-11 02:07:03 UTC** synchronization journal. The checkout was clean at
**b5d66b38**; the old codex-wip branch was already an ancestor. Claude's runtime
**552a4ba0** had since integrated through **ec83e488**. Three agents handled
array expectations, fresh-context oracle regressions, and native admission
review while root repaired the shared escape proof and serialized all gates.
No browser/runtime source changed.

**8f56f1d4** marks property receivers as Passed and iterable sources as both
Passed and Carry. Candidate load provenance retains both effects. Ordinary
object assignments now refuse the independent contents proof: an inherited
setter can retain its argument without creating a field, so subsequent deletion
cannot justify discharging that child. Dense in-bounds Number-index array
proofs remain. The original 631 test body expressions and 95,603-byte fixture
(SHA-256 **c03c5607**) are preserved. All 530 existing observation rows retain
their nonclaim fields; 405 compiler claims change from fresh measurements.
Separate getter/setter/iteration programs pass exact receiver/child retention
checks and report zero oracle violations.
The strict fixture reports **737 claims, 760 observed sites, 24 unclaimed,
zero violations/partial/pending, and precision 8/139**. Ordinary-object
precision needs a new own-data/prototype proof; these conservative refusals
do not supply a native ownership proof.

**8bc3563a** rejects `__proto__` accesses in the shared native closed-shape proof.
Before the fix, the exact scalar, computed-key and lifted-method sources
emitted `trace="number"` while the interpreter returned `trace="object"` in
both optimization modes. All six now refuse; ordinary fields and `toString`
shadowing still compile and execute without Script symbols. Custom inherited
accessor setup already refuses through current module/call admission. This
one-key repair does not prove arbitrary external caller prototype environments.

The frozen **282-step rebuild** passes **10/10 focused CTests in 9.57 seconds**
and the expanded field-name lit test passes **1/1 in 0.15 seconds**. Stable
clang-format **22.1.8** passes **795 files**; bundled 23 retains its same nine
pre-existing differences. All **1,238 frozen inputs** match locally and remotely
before and after the full run.
An intermediate gate used stale array test objects: local changes predated
compilation of the first remote snapshot, and the next rsync preserved those
older source mtimes. Refreshing only authored input mtimes after freeze and
rebuilding resolves all 846 stale assertions; three genuine switch-budget
expectations now check the earlier refusal. The full standard monorepo gate
passes **529/530 CTests in 2158.31 seconds**, including all **158 browser
CTests**. Its one failure is the stale block-const expectation described below;
**167/168 lit cases** pass in **1341.67 seconds**.
Evidence: `/tmp/ctcompile-nd3-{frozen-focused,full}.log`,
`/tmp/ctcompile-nd3-{native-before,native-after}/summary.json`,
`/tmp/ctcompile-nd3-measured-pins/verification.json`.

**6e01ec5b** promotes the exact block-const Bootstrap key source **b6d341ad**,
which now admits **7/7 functions**, preserving all **15 calls** and typed **trace=11**, after the
integrated **d99ddf7b** scope fix. The source-preserving test promotion passes
its focused cohort: **nine native programs**, **21 source rows / 23 typed
Node/interpreter observations**, **14 distinguishing mutations**, and **twelve
refusals**. Explicit/deduced GCC/Clang output retains the two actual key
allocations. Saved set/get/delete/clear callables preserve aliases and distinct
keys, retain keys after caller release, and free them after erase/clear or final
Map destruction through **128 future rounds** and reentry under ASan/UBSan/leak
checks. The shared lifetime helper's original six-key output is byte-identical.
The new exact budget boundary is **42113**, with **30 cutoffs** checked.
The affected ownership CTest passes **1/1 in 1461.21 seconds** (lit **1461.14
seconds**). All **530 CTests and 168 lit cases** are covered across the full
run and affected rerun; this is not a single green full run. All **1,238 final
source hashes** still agree locally and remotely.
Evidence: `/tmp/ctcompile-named-key-{gate,rerun}.log`,
`/tmp/ctcompile-named-key-measured/summary.json`,
`/tmp/ctcompile-named-key-promotion-static.json`.

The ownership matrix contains **422 positive programs**, each built in two
layouts with two compilers: **1,688 baseline C++ compilations**, plus sanitizer
and refusal controls. For quick object-key feedback, the scratch cohort reuses
`object_argument_sources`, `check_object_argument_observations`, the existing
native execution loop and `check_object_argument_controls`: **36 baseline
compilations plus eight sanitizer builds**. A future optional cohort selector
must include historical `parameter_object` and avoid unrelated controls that
index other saved programs. At that checkpoint no selector was implemented; the standard full gate
still covers the complete matrix.

**Then-next: named empty-object keys for Bootstrap Data**, exact **7573e89b**
(`object_argument_global` in `native_owned_global_maps/sources_map_keys.py`).
Prove one allocation, its sole unconditional store and each dominated global
load across the complete captured-Map call family. Preserve the actual
LoadGlobal in HostMethodArgument; carry allocation identity on an independent
checked edge, following `scalarGlobalRead`. Revalidate it in OwnedGlobalMethods
before type, identity, Map and typed-global emission consume it. Early loads,
second stores including late ones, fields, unknown consumers, payload/return
escapes and incompatible later arguments remain refusals. **600b8fb6** adds
aliases and two identities and remains a later extension. Both original
true-global sources had measured refusals in both modes, including stale
and fresh forgeries and reruns; they are distinct from the admitted block consts.
Full native Bootstrap, field/nested-Map ownership, and direct browser API
integration remain unfinished.
Independent boundary audit: `/tmp/ctcompile-nd3-native-review/report.md`.

## Retained object keys, mixed key carriers and test splits, 2026-09-10

Continued the exact **7381e2fb / 5eba229d** boundaries left by **637f4e8c**
and the **17:06:31 UTC** synchronization journal. The checkout was clean at
**98b7fde4**. The older codex-wip branch, CallDirectOp repair, interrupted
mutation gate and source-split repairs were already merged and gated. Three
agents handled independent lowering, escape and execution work; root alone
serialized devbox operations and Git writes. No browser/runtime source changed.

**25c4c882** proves empty object keys across the complete captured-Map call
family. Actual uses must belong to that exact invocation census; every sibling
body independently permits object formals only as Map keys. Map storage retains
the key, which has no outgoing ownership edges. Same-family aliases, set/get/has/
delete and scalar payloads reuse existing ownership and native identity proofs.
Field mutation, object payloads, arbitrary calls and cycles remain refused.
Source/prepared owner checks cover live edits, stale fingerprints and incomplete
budgets. A separate review found no additional correctness issue.

**54d50a6b** supplies the separately proved Number/Object Map key carrier using
`std::variant<double, std::shared_ptr<ctnative::identity_object>>`. Key spelling,
admission and wrapping use the existing comparator/storage implementations;
object-valued payload storage is unchanged. Identity aliases, distinct keys,
NaN, signed zero, deletion, clear and owner release pass the native mixed-key
and object-key suites with GCC/Clang and sanitizers (**2/2**, **46.49 seconds**).
This storage carrier does not authorize mixing Number/Object actuals at one
formal across calls; **5eba229d** seeds a numeric key inside an object-key method.

**e59e6ce1** executes **seven native object-key programs**, with **21 typed
Node/interpreter observations across 19 sources** and **13 distinguishing
mutations**. Historical source bytes remain intact. The exact retained-key
**7381e2fb** and mixed-key **5eba229d** now admit **4/4 functions in both modes**;
the fresh-key sibling source **3fbdfc23** admits **7/7** with fifteen calls.
Explicit/deduced GCC/Clang output preserves actual allocations and Map calls,
with no Script symbols. Saved sibling callables retain Map/key ownership after
caller and table release, through **128 future key rounds**, aliasing,
overwrite/delete/clear, reentry and final release under ASan/UBSan/leak checks.
Twelve independent refusals remain. Exact budget boundaries **1516 / 1639**
check **32 / 31 cutoffs**.

**0995a758** promotes historical `parameter_object`, preserving its name,
**7b592b83** source identity, **five functions and five calls**. It passes native
execution in both layouts with GCC/Clang and sanitizers; saved setter/getter
callables retain **128 fresh/aliased keys** after caller release and free them
with the final Map owner. The collection now checks **eight native programs,
20 source rows, 22 typed observations and 14 distinguishing mutations**.
All **171 historical refusal sources** retain their exact bytes; only this
proved case changes routing. Its proof completes at budget **3442**, with
**31 cutoffs** checked. The complete lit rerun passes as recorded below.

**4680d90e** extends independent mixed BigInt error-retention proof to Mod.
It grants neither successful completion nor native BigInt/effect permission.
The matrix checks **57 rows, 43 live states and 3451 cutoffs**. The strict fixture
has **737 claims, 760 observed sites, 24 unclaimed and 101/139 precision**, with
zero soundness violations, partial or pending checks. Initial array failures
were stale test category predicates; their source-preserving correction passes.
Next escape witness: mixed BigInt **Pow**, exact **06f94afe**, still conservative.

The user requested splitting every compiler test over 1,000 lines using Git
moves. **681bd899**, **cdcdc0c2** and **e37a4343** split the C++ tests, escape
checker/fixture and Map-presence lit cases. Named test headers and ordinary C++
translation units keep each original executable and main order. Fixture chunks
reassemble the exact **95,603 bytes** (SHA-256 **c03c5607**); expanded checker
includes reproduce the original checker. C++ bodies/literals and all **99 JS
cases / 100 RUN commands** are preserved. Two blank inter-case separators were
removed at file ends. The Python split preserves **198 helper/class bodies**,
**134 source/data entries**, **199 import scopes**, and the original main AST
when its extracted observation phase is inlined. All eleven original oversized
files are split; the maximum remaining compiler test file is **987 lines**.
The browser corpus's `p5-api-probe.js` is still **1,416 lines** under Claude's
active claim; its eventual split is recorded in the synchronization journal.

The requested folder organization is complete: **321 tracked test files** moved
using locked `git mv` in six commits. **d75b4a51** groups 49 escape/type files;
**db2ecc05** groups 39 ownership/host/exception files; **00ef1597** groups 37
runtime, packaging, core and support files; **b3475276** groups 57 native fixtures,
checks, specialization and importer files. **a085fb54** groups 66 native pass,
provider, export and ownership files; **72cd44b3** groups 73 lowering files.
The test root retains CMake/lit configuration and its new directory guide;
CTNative's root retains only its recursive `.gitignore`. Native lowering uses
`Maps/`, `Objects/`, `Closures/`, `Scalars/`, `Exceptions/`, `Admission/` and
`Emission/`. See `ctcompile/test/README.md` for the full hierarchy.

Fixture and test bodies remain intact; only include/import/RUN/build paths and
reference comments change. The final inventory retains **523 CTest names in the
same order, 168 lit cases and 1,215 RUN lines**. Python helper CLI/import checks
and all 134 Map source/data entries are preserved. The first three folder gates
pass **59/59, 32/32 and 33/33** affected CTests. The next full monorepo run passes
**522/523 in 2325.55 seconds**, with all **151 browser tests** passing; its only
failure is `ctcompile_lit` timing out at **1500.12 seconds** after **167/168**
lit cases pass. The unchanged Map ownership matrix exceeds that tight deadline.
The complete native-suite rerun after the 66 moves passes **168/168 in 1509.23
seconds** using `ctest --timeout 2400`. That 2,400-second suite limit is now
registered in CMake so the standard CTest gate uses it. The final lowering/importer gate passes
**67/67 in 54.84 seconds**. All **1,214 frozen local/devbox inputs** agree
before and after the full and affected rerun workflows. All 523 CTests are thus covered across the full run
and affected reruns; this is not a single green full run. Stable format22 passes
all **800 files**; bundled23 retains the same nine pre-existing differences.
Claude's **45ff3d01** browser refactor branch remains outside these measurements.
Its journal records the deleted GPU/compositor APIs and other public-header
changes. Re-read that journal and the actual subsystem headers after integration;
the compiler still uses the existing plain C++ platform boundary.
Folder evidence: `/tmp/ctcompile-organized-full-ctest.log`,
`/tmp/ctcompile-native-root66-ctest.log`,
`/tmp/ctcompile-native-lowering73-ctest.log` and
`/tmp/ctcompile-test-folder-final-audit.json`.

The requested **filename cleanup is complete**: **193 files renamed** across
all **414 reviewed test files**, with standard, upstream and already-clear names
retained. **89fc5763** renames 45 analysis/runtime files; **5a0f3cec** renames 19
importer, target, comparison, guard and packaging files; **1c72b01d** renames
129 native tests, helpers and fixtures. All moves used locked `git mv`.
**04de66be** updates eight production comments that reference the moved tests;
production behavior is unchanged. Current documentation links and command
examples now use the final folders and filenames.

The source check preserves **61 fixture programs / 370,700 bytes**, all **134
catalog entries**, **49 Python files / 61 import edges**, **168 lit cases /
1,215 RUN lines**, and the **987-line** maximum. All 523 CTest names/order and
1,214 frozen local/devbox inputs agree. Names pass Windows collision and
reserved-name checks. The standard README documents the layout and conventions;
27 existing Git-ignored Python caches are recorded and untouched.
Evidence: `/tmp/ctcompile-final193-verification.json`.

The analysis/runtime filename wave passes **44/44 CTests in 222.40 seconds**;
the importer/target wave passes **10/10 in 10.03 seconds**, including **45/45
lit cases**. The final **347-step devbox rebuild passes**. Its full CTest run
completes **169 passing tests**, then the user explicitly waives further tests
for this filename-only round. The ongoing native lit process and its children
were stopped under the existing devbox lock; the full 523-test run is therefore
**incomplete by request, not a reported full pass**. No further test run is
required for this cleanup. Stable format22 passes **800 files**; bundled23
retains the same nine pre-existing differences. Logs, hashes and the waiver
record are under `/tmp/ctcompile-names-final193-*`.

The **read-only ponytail audit** recommends three remaining cuts: unused
`tools/mingw/build-shaderc-mingw.sh` (**121 lines**), obsolete phase/configure
comments in `MLIRContextSetup.cpp` and `test/cmake/Lit.cmake` (**33 lines**), and
the two unread `CTCOMPILE_STANDALONE` assignments (**2 lines**). Total:
**156 lines, zero dependencies**. None was applied. The requested test cleanup
is separate; Claude's browser cuts are on his incoming branch. Full evidence:
`/tmp/ctcompile-ponytail-audit-final.json`.

**Incoming runtime dependency:** Claude's 637e4a40 branch includes 552a4ba0,
which invokes getters and setters on the implicit Object.prototype chain.
The current generic escape classifier still marks property receivers NEITHER
using the old data-only fallback assumption (`CTJS/IR/Ops/Properties.td`).
A **220-byte retained-this witness** now measures the gap on both matching
devbox binary sets: source SHA-256 **441a8930**, program **a99e2d04fc92995e**,
function **1 / readInherited**, PC **1 / obj**. Both compilers claim Confined.
Current main records one confined object and zero escaped; incoming 637e4a40
records zero confined and one escaped through globals. The incoming checker
exits **1 with one soundness violation**; main exits **0**. Both collections
have zero unresolved or unchecked target observations. Raw commands, binary
hashes and observations are in `/tmp/ctcompile-nd3-probe-results/summary.json`
and its `main/` and `incoming/` directories. This diagnostic probe is separate
from the green filename gate. An intercepted assignment also cannot prove an
own slot; that setter follow-up remains a static witness.
Repair the receiver effect/proof and add getter/setter/iteration retention
regressions before treating the runtime integration as gated. Property sinks
alone are insufficient: Iterable must keep Carry while sinking, load provenance
must record both edges, and contents analysis must refuse ordinary-object writes
without an own-data/prototype proof. `/tmp/ctcompile-nd3-repair-scope.md` records
the exact production and test paths. Native field admission has separate guards;
no end-to-end native miscompile has been measured here.
Current main still has the old runtime and passes its ND-3 pin. Once the proof
repair and runtime are integrated, change the existing `typeof` probe in
`test/Analysis/Escape/Cycle.cpp` from `undefined` to `number`, retain the explicit
chain's numeric 42 check, update divergence status and rerun the affected
escape/oracle and integration gates. The source-preserving pin patch under
`/tmp/ctcompile-nd3-after-runtime-integration/` is only one part of that work;
`proof-safety-audit.md` records the missing proof. Do not apply the pin alone.
Keep the runtime's corrected semantics; native getter admission is unchanged.
The automatic integration watcher was stopped using Claude's documented stop
option after the false Confined claim was measured. A clean compiler checkout
allows Claude to commit; integrate the runtime after the compiler proof repair
and its measured gate.

Measured gates so far: the initial merged-tree build passes **751 steps**; the
feature rebuild passes **302 steps** without warnings. After the C++ splits,
all **15 focused CTests pass in 224.65 seconds**, and the three regrouped
Map-presence lit tests pass in **0.64 seconds**. Stable clang-format **22.1.8**
passes **800 files**; bundled 23 retains the same nine baseline differences.
**2172add2** commits the Python split after its seven-program native/lifetime
check passed again. The full monorepo gate passes **522/523 CTests in 2228.37
seconds**: **151/151 browser tests**, and **371/372 compiler tests**. Its lit
CTest passes **167/168** cases in **1407.83 seconds**; the sole failure is the
historical `parameter_object` expectation, now admitting all five functions.
Its exact fresh-key setter/size-getter source now passes the focused execution
and lifetime check above. The corrected **ctcompile_lit CTest passes 1/1**, with
**168/168 lit cases in 1499.87 seconds** (1499.80 internally; 1499.88 total).
Thus all **523 CTests** are covered as passing across the original run and the
corrected rerun; the initial failure remains recorded. All **1,214 source hashes**
match the devbox before and after the corrected run, with only the six intended
test files differing from the initial full run. Independent evidence:
`/tmp/ctcompile-retained-composite-audit.json`.

Fresh exact Bootstrap Data measurements preserve these source identities:

| Program | SHA-256 prefix | Functions | Raw/prepared calls | Native, both optimization modes |
| --- | --- | ---: | ---: | ---: |
| Browser | `80a6fd87` | 7 | 42 | 0/7 |
| CommonJS | `cc6c3960` | 7 | 42 | 0/7 |
| AMD | `821e07a5` | 8 | 43 | 0/8 |
| Ordinary publication | `8359592c` | 7 | 40 | 0/7 |

All **77 typed Number observations** agree between Node and the interpreter.
Fresh prepared contracts still refuse with `property receiver lacks a fresh
own-data object proof`; both native modes preserve the refused operations and
source calls. Browser/CommonJS/ordinary prefix probes each resolve **24 calls**
and summarize **23 provider calls**, keeping **one outer allocation, three
nested allocations, one callback and two global writes** at runtime. Browser
and CommonJS residuals have **40 calls**, ordinary remains at **40**; fresh
residual contracts still admit **0/7**. Prefix work does not claim complete
ownership, and no native execution of these wholly refused Data programs is
claimed. Evidence: `/tmp/ctcompile-retained-exact-data-final-results.json`.

**Next: ordinary named object owners for actual Bootstrap Data.** The exact
field-bearing/nested-Map Data methods still need independent ownership and
effect proofs. Block-const sibling **b6d341ad** currently refuses **0/7**, but
that particular source imports globals because of the VM/compiler block-scope
bug fixed by Claude's **d99ddf7b**, which is not merged here. Remeasure it after
integration; do not treat that bug as a permanent native limitation. Ordinary
`var` key **7573e89b** remains an independent global-owner refusal and is the
smallest next slice. Prove one empty allocation, one unconditional initialization
dominating every load, and key-only arguments across the complete captured-Map
family. Keep each actual `LoadGlobal` separate from its allocation identity.
The existing `environmentProblem()` checks initialization order, so the
sole-store lookup alone is not evidence of a current unsound admission. The
missing piece is a checked allocation/store/load edge that downstream ownership
and type consumers can revalidate. Follow `scalarGlobalRead()`'s live-edge pattern
without primitive categories, preserving the actual `LoadGlobal` in
`HostMethodArgument`. Publish only after the complete environment, fingerprint
and budget proof succeeds; revalidate in `OwnedGlobalMethods`, then consume that
edge in NativeMap, ObjectIdentity, TypeInference and typed global emission.
Reject second stores (including after the last call), early reads, field mutation,
unknown consumers, payload/return escapes and incompatible later arguments.
The true `var` sibling companion **600b8fb6** additionally needs immutable aliases
and two distinct allocation identities. Full native Bootstrap and direct browser
API integration remain unfinished.

Evidence: `/tmp/ctcompile-retained-{focused,execution,probe}.log`,
`/tmp/ctcompile-test-splits-focused.log`,
`/tmp/ctcompile-retained-key-execution-final.log`,
`/tmp/ctcompile-{map-tests,escape,large-test}-split-*.json`.


## Borrowed object keys and mixed BigInt Div, 2026-09-10

Resumed the **15:44–15:45 UTC** object-key/Div thread at **183a10c0**, including
three dirty Div files. The earlier interrupted mutation gate and codex-wip
recovery were already complete. Three agents handled independent escape,
owner-test and execution work; root serialized every build and commit. No
browser or runtime source changed.

**e9802e24** proves a direct empty entry object passed to a captured method as
an independent object actual/formal edge, with no primitive category. Every
actual use and every current invocation is checked; each object formal can only
be borrowed by the captured Map's `has`. Native identity and callable carriers
are still independently derived. Mixed primitive/object formals, field-bearing
or named objects, mutations, returns and storing an object key remain refused.

The exact **20d4806e** source advances **0/4 → 4/4 native in both modes**,
keeping all four calls and Number `trace=0`. **0dfded6a** executes that source,
a local-alias variant, repeated fresh actuals and two object formals: **four
programs, sixteen typed Node/interpreter observations and seven distinguishing
mutations**. Explicit/deduced GCC/Clang output retains actual allocations,
call arguments and `Map.has`, with no Script symbols. A saved getter survives
root/table destruction, **128 future object-key rounds**, reentry and final
Map/key destruction under ASan/UBSan/leak checks. The budget boundary is **1688**,
with **31 cutoffs** checked. Ten ownership/effect controls and a separate
complete-owner mixed-key carrier control retain their refusals.

All five owner/host CTests pass **5/5 in 217.69 seconds**. New source/prepared
owner matrices cover **20/18 rows and all 1348/1295 incomplete budgets**.
A test-only forged schema attribute initially invalidated its own restoration
contract; the corrected test removes it before checking the original contract.
An independent review found no additional production issue.

**c20f43a4** finishes mixed BigInt Div's independent error-retention proof,
without granting native BigInt or successful-completion permission. It checks
**57 rows, 43 live states and 3410 cutoffs**. The strict fixture records **721
claims, 741 observed sites, 21 unclaimed and 100/137 precision**, with zero
violations/partial/pending. Eight escape CTests pass **8/8 in 9.22 seconds**.
Historical fixture bytes and Div sources remain intact; Mod controls stay
conservative. The full build completes **263 steps without warnings**. Stable
clang-format **22.1.8 passes 745 files**; bundled 23 has the same nine baseline
differences. The fresh complete gate at **a129764d** passes **512/517 CTests
in 2227.17 seconds**, including **all 372 compiler tests** and **166/166 lit
cases** (1442.85 seconds internally; CTest 1443.04). The five browser failures
are `selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`;
each diagnostic exactly matches the previous gate. The full-run rebuild reports
no work, separately from the earlier 263-step build. All **14 committed/local/
devbox code/test hashes** match the frozen inputs. The fresh artifact census
verifies **417 native programs, 1668 GCC/Clang executables and 88 sanitized
binaries across 44 lifetime families**. Eight new C++ variants preserve their
allocations, calls and `Map.has`; those programs' sixteen ordinary and two
sanitized executables have no Script/AOT symbols.

**Next: retaining actual object keys through Bootstrap Data.** Exact
**7381e2fb** adds `t.set(e, 1)` before the same `t.has(e)` and remains unowned
**0/4** (typed Node/interpreter `trace=1`). It needs an ordinary Map-to-key owner
and all later set/get/delete uses proved through the existing host seam.
Field-bearing **698def06** and named **7573e89b** actuals remain separate owner
obligations. Numeric-seeded **5eba229d** now has complete ownership but stays
**0/4** at the closed Number/Object key variant's missing native carrier;
that is distinct from object ownership. These two bounded extensions can proceed
independently: `HostContract` proves exact Map-to-key retention and same-family
sibling uses; lowering supplies the already-proved Number/Object key variant.
Keep object-valued payloads, named/field-bearing keys and unknown effects separate.

The fresh postfull Data probe preserves browser/CommonJS/AMD source hashes
**80a6fd87/cc6c3960/821e07a5** and all **19/19/20 typed observations**. They remain
**0/7, 0/7 and 0/8** native in both modes, with **42/42/43 calls** preserved.
Ordinary publication **8359592c** remains **0/7**, with **19 observations and
40 calls**. Every observation matches Node and the interpreter. The owner reason
remains `property receiver lacks a fresh own-data object proof`. Browser,
CommonJS and ordinary prefix analyses finish **24 resolved calls and 23 provider
summaries**; freshly fingerprinted residual contracts still refuse ownership.
Exact Data and full native Bootstrap remain unfinished.

Artifacts: `/tmp/ctcompile-object-resume-{build-all,escape,owners,execution}.log`,
`/tmp/ctcompile-object-resume-{probe,execution}/`, and the fourteen-input
`/tmp/ctcompile-object-resume-frozen.json`. Full gate:
`/tmp/ctcompile-object-resume-full.log` and
`/tmp/ctcompile-object-resume-full-detail.log`. Exact Data:
`/tmp/ctcompile-object-resume-exact-data/`. Artifact audit:
`/tmp/ctcompile-object-resume-audit.json` and
`/tmp/ctcompile-object-resume-full-cpp/`.

## Interrupted mutation gate recovered, 2026-09-10

Resumed the pending **a1ce9b02** gate from the **05:22:09 UTC** journal and
its interrupted **13:50** recovery. The checkout was clean. The original local
`/tmp` evidence was lost after reboot; the devbox retained **163 completed CTests**
(**158 pass**, the same five recorded browser failures) and an incomplete CTest
trailer after **166/166 lit passes**. All **14 code/test inputs** match committed,
local and devbox bytes. Older codex-wip/CallDirectOp and source-split repairs were
already gated ancestors; no missing implementation was discarded.

`tools/remote-build.sh all` reports **no work to do**. Resuming CTest from its
checkpoint passes the remaining **354/354 tests in 2262.99 seconds**, including
lit in **1486.85 seconds**. Together the two runs account for **512/517 CTests**
and **all 372 compiler tests**. This is a recovered complete gate, not a new
517-test run; no combined wall time is claimed. The five failures are `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors`. The original build's
recorded **241 steps without warnings** is preserved separately.

The new artifact census verifies **413 native programs**, **1652 fresh GCC/Clang
binaries**, and **86 sanitized binaries across 43 lifetime families**. Fourteen
source hashes match again after the run. Ten inspected C++ variants retain their
real Map/size/field operations without Script symbols. Stable clang-format
**22.1.8** passes **745 files**; bundled 23 retains the nine recorded differences.
No browser/runtime source changed.

The interrupted postfull probe is also complete. Exact Data browser/CommonJS/AMD
remain **0/7, 0/7, 0/8** with **19/19/20** matching typed observations; ordinary
publication remains **0/7**. Ten root/table object-key probes and their future
observer agree with Node and the interpreter. Four evaluated-Number controls
admit **4/4 in both modes** and execute **32 standalone GCC/Clang binaries**:
they preserve the original root/table reads and Map calls. Their six object-key
or later-object counterparts remain **0/4**; three future-observer mutations
distinguish the expected result. The allocation-preserving **2507446b** control
still independently refuses the unused extra allocation.

Continue **20d4806e**, the fresh-object `t.has(e)` argument. Prove its actual/formal
identity and ordinary owner through the existing host seam and native object-key
carrier. Do not treat a primitive alternative, first invocation or startup
prefix as object ownership. Field-bearing/named objects, nested Data Maps and
callback effects remain separate obligations. Full native Bootstrap is unfinished.
Evidence: `/tmp/ctcompile-recovered-gate/`, `/tmp/ctcompile-data-resume-full.log`,
`/tmp/ctcompile-data-resume-full-detail.log`, and
`/tmp/ctcompile-data-resume-{inventory,cpp-audit,devbox-hashes}.json`.

## Known Map mutation sizes and mixed BigInt multiplication, 2026-09-10

Resumed interrupted iteration 6 from the **04:24:48–04:27:51 UTC**
synchronization journal, explicitly abandoned at **04:30:18**. The checkout at
**8541686d** was clean; the interrupted thread had claims but no code edits.
The older codex-wip recovery and source-split fixes were already gated ancestors.
Three agents continued independent proof, execution and escape work; root alone
serialized every devbox operation and commit. No browser/runtime source changed.

**58983ccc** carries exact Map cardinality through independently proved
insertions, overwrites and present/absent deletions. It queries membership before
mutation, for the actual runtime instance, and retains the complete bounded
candidate census. Unknown key relations, alias effects and an exceeded
64-candidate census still invalidate the fact. Saved SSA sizes retain their
actual read-time value. Source ownership and native presence independently
rederive the proof; neither cardinality nor a schema family supplies membership.

The exact **56679e2d** insertion and **887e65fc** absent-deletion sources now
admit **5/5 in both modes**, preserving sixteen calls and Number `trace=1`.
All twelve historical continuation sources keep their bytes and call counts.
**a03cc3fd** executes **23 native programs**, **38 typed Node/interpreter
observations**, **26 discriminating mutations**, and **six refusal/repair
families**. Explicit/deduced GCC/Clang programs retain actual Map/size/field
operations and have no Script symbols. A saved leaf survives **128 future calls**,
both flags, owner/table release, reentry and final Map/leaf release under
ASan/UBSan/leak checks. Work budgets **7059/8252** check **32/30 cutoffs**.
All **855 historical source rows across 65 helpers** remain unchanged.
The complete **238-case** refusal census finds no unexpected classification changes.

The corrected **16-step build is warning-free**. Type inference plus all eight
escape CTests pass **9/9 in 10.86 seconds**; ownership/host/provider checks pass
**5/5 in 228.29 seconds**; Map-presence lit passes **1/1 in 1.47 seconds**.
New proof controls cover **26 owner rows per source/prepared form**, **48 type
rows**, **12 live type edits**, and **16 lowering sources**. Four owner budget
families check **17 cutoffs** each at **13383/13335/13261/13213** work. All 83
historical lowering sources remain intact. Stable clang-format **22.1.8** passes
all **745 files**; bundled 23 retains the same nine baseline differences.

**0b25968b** proves independent error retention for mixed BigInt **Mul**,
using each original operand's independently known primitive category. It grants
no successful-completion, native BigInt carrier or effect proof. Every structural
continuation remains checked. New tests cover **57 rows, 43 live states and
3369 budget cutoffs**. The fixture records **705 claims, 722 observed sites,
18 unclaimed, 99/135 precision**, with zero violations/partial/pending; **60
checker corruptions reject**. All **169 historical named functions** and
**87,049 prior fixture bytes** remain intact. The first escape run passed
**7/8 in 10.45 seconds**; only old Mul refusal expectations needed the measured,
source-preserving promotions before the corrected gate above.

The complete monorepo gate was pending at this checkpoint with all **14 code/test
inputs frozen and committed**. The recovery above completes it; the original local
`/tmp/ctcompile-mutation-*` gate artifacts were lost after reboot.

**Next: actual Bootstrap Data ownership and object-valued Map arguments.**
Fresh explicit contracts preserve exact browser/CommonJS/AMD sources
**80a6fd87/cc6c3960/821e07a5**, their **19/19/20 typed observations**, and
**42/42/43 calls**. Native remains **0/7, 0/7, 0/8** in both modes, with
`property receiver lacks a fresh own-data object proof`. Browser/CommonJS prefix
analysis completes **24 resolved calls and 23 provider summaries**; a separately
fresh contract after specialization still refuses native ownership. An ordinary
publication companion **8359592c** preserves every Data-method byte and all 19
observations, but also refuses **0/7** with forty calls. Startup evaluation is
therefore not a future ownership proof.

A source-derived `t.has(e)` object-formal isolate **20d4806e** refuses **0/4**
with four calls. Preserving its unused object allocation while changing only the
argument to Number clears the source getter boundary but still refuses native
ownership (`owned global method table has another allocation`). These are
separate obligations, not a working repair. The next implementation must prove
ordinary object arguments/identity and their owners through the existing host
seam, keeping unknown future calls conservative. Nested Data Maps and diagnostic
callbacks remain later obligations. Full native Bootstrap and direct browser API
integration remain unfinished.

The next escape probe is mixed primitive BigInt **Div**, **8a20a9a8**:
Node/interpreter Number `mixedDivTrace=63`; its literal claims remain conservative.
See `bootstrap-provider-next.md` and `escape-load-evidence.md` for exact evidence.
Focused artifacts: `/tmp/ctcompile-mutation-{type-escape,owners,lowering,execution}.log`,
`/tmp/ctcompile-mutation-{probe,all-refusals}/`, and
`/tmp/ctcompile-bootstrap-next-boundary/{sources,results,future}.json`.

## Equal branch Map sizes and mixed BigInt subtraction, 2026-09-09

Continued the exact **2d1763eb** boundary in **f7c42a09**'s handoff,
`bootstrap-provider-next.md` and the **02:47:18 UTC** synchronization journal.
The checkout was clean; interrupted deletion/unary-Plus work, codex-wip recovery
and source-split repairs were already committed and gated. Three agents handled
independent proof tests, execution and escape analysis. No browser or runtime
source changed.

**bb8c52d3** retains a separate exact cardinality when both structural arms
prove the same size for the actual Map instance. Different surviving keys need
not imply different sizes. This fact supplies neither common key membership nor
absence. Writes/deletes invalidate mutable cardinality; clear starts an exact
zero. Complete key censuses may independently recover a size. Saved SSA sizes
keep their actual read-time value. Both source ownership and native presence
rederive these facts, preserving all arms, aliases and conservative work limits.

The unchanged **2d1763eb** source and **b0d72c4e** repair now admit **5/5 in
both modes**, with all **15 calls** and Number `trace=1` preserved. **4a624e9e**
executes **18 native programs**, **30 typed Node/interpreter observations**,
**17 mutations** and **six refusal/repair families**. Generated explicit/deduced
GCC/Clang C++ retains actual size/Map/field calls without Script symbols. A saved
leaf survives **128 future calls**, both flags, owner/table release, reentry and
final Map/leaf release under ASan/UBSan/leak checks. Budgets **6715/8028** check
**29/30 cutoffs**. All **811 historical source rows across 63 helpers** and all
**12 continuation sources** preserve their bytes.

A complete **234-case** refusal census found exactly two further intended
historical promotions: **3dd12150** and **caabd2de** now use saved size one and
a later definite write of key one. **6a8262ad** executes both **5/5 in both modes** under both
compilers and layouts, preserving ten calls and Number `trace=0`. Their source
bytes and original evaluated operations are unchanged; only their admission
expectations advance. All other census classifications remain unchanged.

Focused ownership/host checks pass **5/5**, including **22 new owner rows and
six live edits per source/prepared form**, with **17 sampled join cutoffs** at
**12697/12566** work. Type tests cover **44 new homogeneous/mixed rows and
eight live edits**. The initial combined run passed **5/6 in 220.86 seconds**;
old disjoint-join expectations needed promotion, and new unknown-call controls
needed the existing whole-family boxed refusal instead of an optional type.
The corrected **four-step warning-free build** passes **9/9 CTests in 10.41
seconds** (type plus all eight escape checks). Map-presence lit passes **1/1
in 1.28 seconds**. All **745 C++ files** pass stable clang-format **22.1.8**;
bundled 23 retains the nine preexisting differences. A devbox shutdown and a
temporary runner's missing Node path were recovered before these corrected gates.

**a43858ac** proves independent error retention for dynamic Sub with exactly
one independently known BigInt and one non-BigInt primitive. It grants no normal
completion, native BigInt carrier or effect proof. The VM's independent Undefined
error carrier cannot become a Number/BigInt proof. Tests cover **57 rows, 43 live
states and 3328 budget cutoffs**. Fixture precision is **98/133**, with **689
claims, 703 observed sites, 15 unclaimed** and zero violations/partial/pending;
**60 checker corruptions reject**. The initial **7/8 escape run in 11.09 seconds**
exposed ten intended historical Sub-retention promotions; their exact source
remains, alongside ten separate mixed-Mul refusal controls. All prior fixture
source bytes and named-function recordings remain intact.

The complete monorepo build finishes **241 steps without warnings** and passes
**512/517 CTests in 2302.35 seconds**, including **all 372 compiler checks**.
All **166/166 lit cases** pass in **1493.74 seconds** (CTest **1493.83**),
including **390 native programs and 42 lifetime families**. The only failures
are the established browser tests `selectors`, `frames`, `element_attrs`,
`vm_async` and `early_errors`; all five failure outputs are byte-identical to
the preceding complete gate. No compiler correction or second full run followed.

All **14 frozen code/test hashes** match committed, local and devbox files.
The artifact census verifies **1560 fresh GCC/Clang binaries and 84 sanitized
binaries** from this run. Ten inspected explicit/deduced C++ variants retain
real size, branch and Map operations with ordinary owners and no Script symbols;
the three field-read witnesses retain their actual present-object lookups and
field reads. All five positive type and four escape corpus oracles report zero
violations. Escape precision remains **98/133** for the fixture and **0/64,
0/16 and 0/20** for Bootstrap, p5 and Phaser, with the existing p5 partial
unchanged. Fresh native coverage remains **19/574 Bootstrap, 39/4754 p5 and
45/7725 Phaser** in both modes, with zero pruned; exact Bootstrap Data remains
**0/7, 0/7 and 0/8**.

**Next native boundary: known mutations after an equal-cardinality join.**
Twelve fresh typed Node/interpreter sources and eight mutations agree. Source
**56679e2d** inserts key three after the arms leave key one or key two, so both
paths now have size two. The subsequent saved-size read still refuses **0/5**;
evaluated-read/literal-two repair **a11c4179** admits **5/5**. Both modes preserve
five functions, sixteen calls and Number `trace=1`, for either startup flag.
Deleting definitely absent key three similarly loses the known size one.
Saving the size before mutation or rebuilding a complete census with clear
already admits. Continue mutable cardinality only with independent key-relation
proofs; possible insertion/overwrite or deletion must remain conservative.
The exact source and repair are in `bootstrap-provider-next.md`.

The next escape boundary is mixed primitive BigInt **Mul**, measured at
**d5a61e1a**: Node/VM Number `mixedMulTrace=63`, twelve literal sites and three
independent Errors; all twelve compiler claims remain conservative. Full native
Bootstrap and direct browser API integration remain unfinished.

Evidence: `/tmp/ctcompile-map-join-{proofs,corrected-proofs,corrected-lowering,execution,historical}.log`,
`/tmp/ctcompile-map-join-{all-refusals,probe}/`,
`/tmp/ctcompile-map-join-final-frozen.json`,
`/tmp/ctcompile-after-join-size-final/`,
`/tmp/ctcompile-after-mixed-sub-boundary.{log,rec,claims}` and
`ctcompile/docs/escape-load-evidence.md`. Full evidence:
`/tmp/ctcompile-map-join-full{,-detail}.log` and
`/tmp/ctcompile-map-join-{audit,inventory,cpp-audit,devbox-hashes}.json`.

## Saved Map sizes after deletion and BigInt unary Plus, 2026-09-09

Resumed the **14 uncommitted compiler paths at 8b66e83c** identified by the
**01:29:02**, **01:37:53** and **01:39:25 UTC** synchronization journal entries.
The interrupted delete-size implementation, execution matrix and unary Plus
proof are now committed. Older codex-wip recovery and source-split fixes were
already ancestors. Three agents reviewed the independent proof, execution and
escape paths; root serialized every devbox operation and commit. No browser or
runtime source changed.

**e04810c8** preserves the complete possible-key census through Map deletion,
removing only keys proved SameValueZero-equal to the deleted key. Possible
aliases stay in the upper bound and independently lose definite membership.
Native presence rederives this for the actual runtime instance. Unknown initial
contents, unequal branch states and exceeded 64-candidate budgets remain
conservative; deletion cannot recover a discarded census. Immutable saved sizes
retain the value at their actual read position through later mutations.

The historical **d5a66fc6** delete-last and **aeaca3a0** delete-one-of-two sources
now admit **5/5 in both modes**, preserving all ten calls and Number `trace=1`.
The original formal-key **6aa0868a** and **adeaad91** also advance to **5/5**.
**b33125d1** executes **21 native programs**, **31 typed Node/interpreter
observations**, **17 discriminating mutations** and **six refusal/repair
families**. GCC/Clang explicit/deduced output retains actual Map/size/field calls
with no Script symbols. The saved-size/object lifetime runs **128 future calls**,
both flags, owner/table release, independent reentry and final Map/leaf release
under sanitizers. Budgets **4915/7523** check **30/32 cutoffs**. All **761 prior
source rows across 61 helpers** and ten continuation sources retain their bytes.

The corrected **19-step build is warning-free**. Focused CTests pass **14/14 in
231.66 seconds**, including all eight escape checks; Map-presence lit passes
**1/1 in 0.95 seconds**. Owner tests cover **31 rows and six live edits** per
source/prepared form, with exhaustive deletion cutoffs **10244/10562 source**
and **10079/10406 prepared**. Type inference covers **52 rows/12 live edits**.
Stable clang-format **22.1.8 passes all 745 files**; bundled 23 reports the same
nine preexisting differences. The inherited build failure was a new test's
mixed `TypedValue`/`Value` initializer list; explicit `mlir::Value` conversions
fixed it. The valid signed-zero and 64/65-candidate tests passed unchanged.

**61fe4e2d** proves that unary Plus on an independently known BigInt raises an
independent TypeError which cannot retain unpublished fresh locals. It grants
no native BigInt carrier, successful-completion or effect proof; every
structural continuation is still checked. Tests cover **24 rows/17 live states**,
a wide snapshot and **1461 work cutoffs**. The fixture has **673 claims/684
observed sites/12 unclaimed**, **97/131 precision**, and zero violations;
**60 checker corruptions reject**. Historical sources remain intact.

The complete full devbox build finishes **241 steps without warnings** and
passes **512/517 CTests in 2102.19 seconds**, including **all 372 compiler
checks**. All **166/166 lit cases** pass in **1306.16 seconds** (CTest
**1306.24**), including **370 native programs/41 lifetime families**. The only
failures are the established browser tests `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors`; all five failure outputs are
byte-identical to the preceding full gate. This is one full-suite run, with no
compiler correction or second full gate afterward.

All **14 frozen code/test hashes** match committed, local and devbox files.
The final artifact census verifies **1480 fresh GCC/Clang binaries** and **82
sanitized binaries** from this run. Ten inspected explicit/deduced C++ variants
retain evaluated sizes, real delete/set/clear calls and ordinary owners without
Script symbols; the two present-object witnesses retain actual lookups followed
by their field reads. All five type and four escape oracles report zero
violations. Fixture precision is **97/131**; corpus precisions remain **0/64,
0/16 and 0/20**, with the existing p5 partial unchanged. Fresh native coverage
remains **19/574 Bootstrap, 39/4754 p5 and 45/7725 Phaser** in both modes, with
zero pruned; exact Bootstrap Data remains **0/7, 0/7 and 0/8**.

**Next native boundary: equal cardinality across different branch keys.**
Twelve fresh typed Node/interpreter probes, two future observers and six
mutations agree. Source **2d1763eb** deletes key one on one arm and key two on
the other, leaving size one on both arms. Its post-join saved size still refuses
**0/5**, while evaluated-read/literal-one repair **b0d72c4e** admits **5/5**.
Both preserve five functions, fifteen calls and Number `trace=1`. Both startup
flags behave alike; analogous disjoint setters also refuse. Saving the exact
size inside each arm already admits **5/5**. Unequal size-one/size-two arms
remain refused with distinct observations. Continue the independent exact-size
fact across structural joins without choosing a startup arm or inventing common
key presence. `bootstrap-provider-next.md` contains the exact source.

The next independent escape boundary is mixed BigInt/Number subtraction.
Probe **c71cb43f** gives Node/VM Number `mixedSubTrace=63`, twelve literal sites,
21 instances and three independent Errors; all twelve claims remain
conservative. A future proof must preserve the VM's independent Undefined error
carrier and cannot infer normal completion. Full native Bootstrap and direct
browser API integration remain unfinished.

Evidence: `/tmp/ctcompile-delete-finish-{build,proofs,lowering,execution}.log`,
`/tmp/ctcompile-delete-finish-frozen.json`,
`/tmp/ctcompile-after-delete-size-final/{sources,results,mutations}.json`,
`/tmp/ctcompile-after-bigint-plus-boundary.{log,rec,claims}` and
`ctcompile/docs/escape-load-evidence.md`.
Full evidence: `/tmp/ctcompile-delete-finish-full{,-detail}.log`,
`/tmp/ctcompile-delete-finish-{audit,inventory,cpp-audit,devbox-hashes}.json`.

## Exact finite saved Map sizes and BigInt comparisons, 2026-09-09

Continued the explicit **544f425b** saved-one boundary from the previous
handoff, `bootstrap-provider-next.md` and the **22:00:36 UTC** journal,
starting clean at **d7ebbb73**. Saved-zero and String/BigInt work was already
gated. The codex-wip recovery and source-split fixes are ancestors; both old
lens branches remain superseded by **6520fcf2**. Three agents handled execution,
independent proof tests and mixed primitive BigInt retention. No browser or
runtime source changed.

**9781743a** proves an exact saved Map cardinality when the complete possible-key
upper bound equals the independently known distinct-entry lower bound. A clear
starts a complete census, proved-equal writes deduplicate, and both structural
arms must justify any joined fact. The census has a charged **64-candidate**
limit and refuses at 65 without truncating evidence. Saved size facts describe
the actual read position and survive subsequent mutations. Native presence
independently rederives facts for the actual runtime instance; aliasing writes
and unknown effects invalidate mutable facts. Exact numbers use IEEE bit
encoding. No trusted report, runtime carrier or Script dependency was added.

The unchanged historical **544f425b** now admits **5/5** in both modes, with all
eight calls and Number `trace=0` preserved. Its **d4120093** literal-one repair
also admits. **880ec3ce** adds the focused matrix of **35 sources**, with **23 native
programs** and twelve refusal/repair families. All **42 typed Node/interpreter
observations** agree; seventeen source mutations discriminate the results.
All **702 historical source rows across 59 helpers** and all eight continuation
sources retain their bytes. GCC/Clang explicit/deduced output retains evaluated
size reads, real Map calls and ordinary owners without Script symbols. The
saved-one/object lifetime executes **128 future calls**, both flags, owner/table
release, independent reentry and final Map/leaf release under sanitizers.
The twelve refusal/repair families pass fresh/stale forgeries and reruns;
execution work budgets **4523/6683** each check **31 cutoffs**.

A stronger own-field witness exposed a separate observation boundary. Raw
**f4c6f000** has complete ownership but remains **0/5** because global `trace`
is optional. Exact entry `+ 0` repair **1d39e071** and field-comparison repair
**91e905f8** each admit **5/5**, keeping five functions/eight calls and Number
`trace=1`. Their emitted code must retrieve the present object using the saved
size and then read its field. The entry repair correctly keeps a nullable
setter result and converts it at the global observation. The raw source remains
an explicit refusal, preserving its method graph and published call operands.

Corrected focused gates pass **6/6 proof CTests in 197.42 seconds** after an
**11-step warning-free build**, and the Map-presence lit case passes **1/1 in
0.70 seconds**. Owner tests cover thirty rows in each source/prepared form,
three exhaustive budget families and live stale/fresh key/order controls.
Cutoffs are **9594/9896/11641 source** and **9401/9712/11503 prepared**. Type
inference covers **44 rows/10 live edits**. Seventeen new lowering sources join
39 preserved ones and an external fixture, **57 translation RUNs** total.
Stable clang-format **22.1.8 passes all 745 files**; bundled 23 retains the same
nine preexisting differences.

The initial proof run passed **4/6 in 175.50 seconds** and caught a real new
bug: `NumberAttr::get` needs raw IEEE bits, not a numeric double converted to
an integer. Own-field and independent presence tests exposed the incorrect
one/two encodings; their expectations stayed unchanged. `llvm::bit_cast` fixes
the encoding and supports the LLVM object targets' language settings. Earlier
DenseMap initialization and `std::bit_cast` compile failures are preserved.
Initial ternary execution evidence predates that correction and is not final
validation. Later test-only failures identified the raw field's optional-global
refusal, its nullable setter ABI and the owner-true refusal preparation path.
SSH connection failures were recovered with serialized start/allow-ip calls.

**51018c86** proves mixed primitive BigInt Eq/Lt/Le/Gt/Ge retention from each
original operand's independently established category. It infers no comparison
value, native BigInt carrier or effect/completion contract. Eq covers **92
rows/50 live states/5206 budget cutoffs**; each relation covers **100/52/5498**.
The new source family measures **32 literal sites/64 instances/50 retained**;
**96 checker corruptions** reject. Corrected escape checks pass **8/8 in 9.35
seconds**, improving fixture precision **81/122 -> 96/129**, with **657 claims,
665 observed sites, nine unclaimed** and zero violations/partial/pending. The
first **7/8 in 10.44 seconds** exposed exactly ten justified old child-retention
expectation changes; historical source bytes and observations remain intact.
Of 68 typed Boolean Node/VM observations, 64 agree. Four existing differences
remain documented: large String/BigInt relational rounding and Boolean/BigInt
equality at `false == 0n` and `1n == true`.

The complete full devbox build finishes **246 steps without warnings** and
passes **512/517 CTests in 2138.23 seconds**, including **all 372 compiler
checks**. All **166/166 lit cases** pass in **1326.24 seconds** (CTest
**1326.42**), including the integrated **350 native programs/40 lifetime
families**. Only the five established browser tests fail: `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors`; their failure output is identical
to the previous full run. All **15 frozen code/test hashes** match local files,
committed code and the devbox. This is one complete full-suite run; no compiler
correction or repeat full gate followed it.

All five type and all four escape oracle checks report zero violations.
Fixture precision is **96/129**; corpus precision remains **0/64, 0/16 and
0/20**, with the existing p5 partial result unchanged. Fresh native coverage
remains **19/574 Bootstrap, 39/4754 p5 and 45/7725 Phaser**, both optimization
modes, with zero pruned. Exact Bootstrap Data remains **0/7, 0/7 and 0/8**.
Independent source, execution and full-gate audits reconcile the evidence.
Final full-run C++ for the historical source, both field repairs and the saved
lifetime retains actual size/set/clear/present-lookup calls and ordinary owning
callables without Script symbols. Each field repair has a real present-object
lookup followed by the actual field read; lifetime observers are test-only.

**Next: exact cardinality after deleting a proved key.** Ten fresh typed
Node/interpreter probes agree. Literal-key delete-last source **d5a66fc6**
remains unowned **0/5**; its **315c5f00** repair preserves the evaluated size
read and all ten calls, uses literal zero and admits **5/5**, both with Number
`trace=1`. Removing one of two keys also refuses, while its literal-one repair
admits. Saved-two-before-delete already admits. Preserve possible alias keys,
actual read position, both structural arms, immutable snapshots and independent
native instance proof. Formal-key delete-last and its historical literal-zero
candidate both still refuse; an additional real clear repairs them. Full native
Bootstrap and direct browser API integration remain unfinished.

The next independent escape boundary is unary Plus on a proved BigInt throwing
an independent TypeError. A fresh probe has Node/VM Number `trace=63`, twelve
literal sites/21 instances and three separate thrown Errors without source
allocation claims. All twelve compiler claims remain conservative; unknown
original operand categories cannot use the observed failure as a proof.

Evidence: `/tmp/ctcompile-map-one-{proofs-final,lowering,execution-complete}.log`,
`/tmp/ctcompile-map-one-refusals-final.log`,
`/tmp/ctcompile-map-one-field-candidates-results.json`,
`/tmp/ctcompile-map-one-all-refusals-final.log`,
`/tmp/ctcompile-after-one-size-boundary-final-{results,sources}.json`, and
`/tmp/ctcompile-bigint-comparison-{frozen,vm-audit}.json`.
Full evidence: `/tmp/ctcompile-map-one-full{,-detail}.log`,
`/tmp/ctcompile-map-one-final-audit.json`,
`/tmp/ctcompile-map-one-final-execution-independent-audit.json`,
`/tmp/ctcompile-map-one-full-cpp-inspection.json`, and
`/tmp/ctcompile-bigint-comparison-full-audit.json`.

## Exact saved zero and String/BigInt retention, 2026-09-09

Continued the explicit **49663558** saved-size boundary from the previous
handoff, `bootstrap-provider-next.md` and the **19:24:19 UTC** journal,
starting clean at **c8cd482a**. Previous String-field/Pow work was complete;
codex-wip recovery and source-split repairs are ancestors. The two unmerged
compiler lens branches remain superseded by **6520fcf2**. Three agents handled
execution, independent proof tests and String/BigInt retention. No browser or
runtime source changed.

**5217c13c** proves exact zero at a captured Map size read only after every
reaching path clears the Map with no intervening possible write. Zero lower
bounds and empty intersected entry lists supply no such fact. Saved numbers
retain their evidence after mutations; selected values need independently zero
yields on both arms. **a8c77455** independently derives native presence facts
for the exact cleared runtime instance, invalidates mutable emptiness on any
possibly aliasing write, and preserves immutable saved zeros. SameValueZero
checks use actual literal bits, including both zero encodings. No trusted
annotation or new runtime carrier was added.

The unchanged **49663558** source advances **0/5 -> 5/5 native** in both modes,
with all eight calls and Number `trace=1` preserved. All thirteen earlier
sources preserve their bytes: nine now admit and four wrong-time/one-arm
controls remain unowned. **f3bbd184** passes **14 focused native programs**,
**23 typed Node/interpreter probes**, nine refusal/repair families, thirteen
source mutations and one future-call observer. Both GCC/Clang and both printing
modes pass the no-Script-symbol gate. The saved-zero/object lifetime passes
**128 future calls**, both flags, owner/table release, independent entry
reexecution and final Map/leaf release under ASan/UBSan/leak checks. Budgets
**4523/6224** each check **31 cutoffs**. All **665 historical helper rows across
57 helpers** preserve their source bytes. The full integration passes **327
programs/39 lifetime families**.

The warning-free focused build passes **five of six proof CTests in 180.99
seconds**; only new type expectations failed. Homogeneous local Number Map
reads intentionally expose an optional result. All fourteen original bodies
remain with that expectation; fourteen additional mixed-store variants request
the independent presence proof and check the actual inferred type. The corrected
four-step build passes **9/9 CTests in 9.11 seconds**, including type inference
and all eight escape checks. Owner tests cover **21 rows per source/prepared
form**, exhaustive budgets and live reorderings. Type tests cover **28 rows/eight
live edits**. All ten new lowering sources pass within the existing Map-presence
lit test, **1/1 in 0.55 seconds**. The exact JavaScript `-0` source remains an
owning-source refusal at unary Neg; its literal-zero repair and independent
raw negative-zero-bit proofs remain. Stable clang-format **22.1.8 passes all
745 files**; bundled 23 retains the same nine preexisting differences.

**e42f2d24** proves String/BigInt Add/Concat retention from each original
operand's independent primitive category. String-producing Add origins get a
charged per-path fact; ambiguous Add results cannot authorize later BigInt
operations. This supplies no native BigInt carrier or completion/effect
contract. Add and Concat each cover **97 rows/37 live states**, with **3850/4104
budget cutoffs**. The new source family measures **32 literal sites/64
instances/50 retained**; **96 checker corruptions** reject. Fixture precision
improves **77/115 -> 81/122**, with zero oracle violations. All sixteen typed
Node/VM checks agree. The first escape run passed **7/8 in 9.09 seconds**; its
29 failed assertions overlooked that mutating a shared constant also changes
an array index. Only those expectations changed; production/source bytes stayed
fixed. The corrected full escape subset passes within the 9/9 gate above.

The warning-free **241-step** full build passes **512/517 CTests in 1926.71
seconds**, including **all 372 compiler checks**. All **166/166 lit cases**
pass in **1178.31 seconds** (CTest **1178.50**), including the integrated
**327 programs/39 lifetime families**. Only the five established browser tests
fail: `selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.
All **15** frozen code/test hashes match local files, committed code and devbox
sources. This is one complete full-suite run after an SSH preflight failure;
no compiler correction or rerun was needed.

All five type and all four escape corpus checks report zero violations.
Fixture precision is **81/122**; the existing p5 partial result is unchanged.
Fresh coverage remains **19/574 Bootstrap, 39/4754 p5 and 45/7725 Phaser**,
both optimization modes, with zero pruned. Exact Bootstrap Data remains
**0/7, 0/7 and 0/8**. Independent source/gate audits preserve all historical
sources and reconcile every CTest/detail result. Final emitted C++ retains real
size reads, Map mutations, ordinary owners and saved callables without Script
symbols; lifetime observers belong only to test instrumentation.

**Next: exact one from a saved size after clear/set.** Eight fresh typed
Node/interpreter probes agree. Historical **544f425b** remains **0/5** in both
modes; its **d4120093** repair retains the evaluated size read and all eight
calls, substitutes literal one, and admits **5/5**, both with Number `trace=0`.
A positive lower bound does not establish equality with one. Preserve complete
possible-key cardinality, actual read position, aliases, both structural arms
and independent native presence. Startup-only emptiness and deleting the last
key remain separate refusals; their literal-zero variants also refuse. The
identical-arm size-one probe has nine raw/eight prepared calls due to existing
branch coalescing. Full native Bootstrap and direct browser APIs remain open.
The next separate escape boundary is mixed primitive BigInt comparisons.

Evidence: `/tmp/ctcompile-map-zero-{probe-results,execution-probe,source-hashes}.json`,
`/tmp/ctcompile-map-zero-{proof,corrected,lowering,execution}.log`,
`/tmp/ctcompile-after-zero-size-boundary-{results,sources}.json`,
`/tmp/ctcompile-string-bigint-frozen.json` and
`/tmp/ctcompile-string-bigint-checker-audit/audit.json`.
Full evidence: `/tmp/ctcompile-map-zero-full{,-detail}.log`,
`/tmp/ctcompile-map-zero-final-audit.json` and
`/tmp/ctcompile-map-zero-full-hashes.json`.

## Owning String object fields and BigInt Pow, 2026-09-09

Continued the explicit **88d51f7d** String-field boundary in the previous
handoff and `bootstrap-provider-next.md`, starting clean at **164fe8c2**.
The interrupted String-global work was already gated and committed; the old
codex-wip recovery is an ancestor. The two unmerged compiler lens branches are
superseded by the nesting-depth fix **6520fcf2**. Three agents independently
handled execution tests, proof controls and Pow retention. No browser/runtime
source changed.

**d83f6b83** proves owning String leaf fields in captured Map methods using
current allocation/alias identity, definite own-field initialization and
read-time primitive facts. Actual stored SSA lattices independently determine
types. **c4fa24e3** admits String/nullable String fields and checks the complete
store census across candidate functions before call-component admission.
Equal emitted member names must have compatible carriers. Final emission uses
owning `nullable_string` members, getters and setters, initial Undefined, and
exact tag checks for narrower String or absent reads. A later overwrite cannot
change an earlier String copy. Mixed storage remains a diagnostic.

The unchanged **88d51f7d** source advances **0/5 -> 5/5 native** in both modes,
retaining all seven calls and Number `trace=2`; its Number repair still admits.
**a680b093** passes **12 focused native programs**, **18 typed Node/interpreter
comparisons**, six refusal/repair families, six emitted-tag controls and five
future-call observers. GCC/Clang explicit/deduced binaries link no Script/VM
symbols. The saved long String lifetime spans **128 future calls**, both flags,
field mutation, Map deletion, owner/table release, reentry and final Map/leaf
release under ASan/UBSan/leak checks. Budgets **3512/4408** check **31/30 cutoffs**.
All **636 historical helper rows across 55 helpers** preserve their source bytes.
The integrated test passes **314 programs/38 lifetime families**, retaining
every prior program and lifetime family.

**40081968** preserves the remaining native refusals after a complete
**205-source/410-mode** historical audit. Exactly two sources become owned:
**2467a8cc** still refuses **0/5** at String-versus-Number equality and
**016b5aaf** still refuses **0/6** at a mixed Object/String Map value. Their
source bytes, call graphs, exact repairs and fresh/stale proof controls remain.
**49906726** separately checks actual absent-field narrowing from a shared
String member; String, Null and Undefined tags execute correctly under both
printing modes and compilers.

All six type/host/owner CTests have passing focused results: four checks in
**126.18 seconds** (including two escape checks), then four host/owner checks
in **37.07 seconds**. Added controls cover **13 source/prepared owner rows**,
**18 type/presence rows**, ten live type mutations, fingerprints and budgets.
The two field lit cases pass in **0.18 seconds**. Earlier test expectations
mistook global `undefined` and mixed call-result refusals for field admission;
the original sources remain, with independent `void 0` repair positives and
cross-function census controls. No production change was needed for those
expectation corrections. Stable clang-format **22.1.8 passes all 745 files**;
bundled 23 retains the same nine preexisting differences.

**927128a0** independently proves dynamic BigInt Pow retention origins.
Both operands need their own original category; normal results and independent
negative/oversized-exponent Errors add no object-retention edge. This establishes
no native BigInt carrier or completion/effect guarantee. All **eight escape
CTests pass in 8.95 seconds**. Pow covers **139 rows/51 live states/5299 budget
cutoffs**. Its source family records **28 literal sites/60 instances/38 retained**
and four independent Errors; **38 checker mutations** reject. Fixture precision
improves **72/109 -> 77/115**, with zero oracle violations. The existing VM cap
for small bases remains an explicit Node/VM difference.

The warning-free **241-step** full build passes **512/517 CTests in 1885.14
seconds**, including **all 372 compiler CTests**. The **166/166 lit cases** pass
in **1142.24 seconds** (CTest **1142.32**). The only failures are the five
established browser tests: `selectors`, `frames`, `element_attrs`, `vm_async`
and `early_errors`. All **21** changed code/test hashes agree across frozen
inputs, local files, committed **40081968** and devbox sources. There was one
complete full-suite run; no compiler correction or rerun was needed.

Fresh coverage remains **19/574 Bootstrap, 39/4754 p5 and 45/7725 Phaser**, both
modes, zero pruned. Exact Bootstrap Data remains **0/7, 0/7 and 0/8**. All four
escape corpus oracles report zero violations; fixture precision is **77/115**.
Successful String/BigInt Add/Concat retention remains the next independent
escape increment; native BigInt and completion/effect contracts remain open.

**Next: exact zero from a saved `Map.size` after `clear()`.** Thirteen fresh
sources agree on typed Node/interpreter results. The historical **49663558**
eight-call source remains **0/5** in both modes; its **33aa4c4a** repair keeps
the evaluated size read and uses literal zero, admitting **5/5**. All twelve
nonliteral controls remain unowned. Exact emptiness needs independent live
source and native-presence facts; a zero lower bound or empty known-entry list
cannot prove it. Full Bootstrap and direct browser API integration remain open.

Evidence: `/tmp/ctcompile-string-fields-{focused,execution-gate,execution,precommit-gate}.log`,
`/tmp/ctcompile-string-fields-{boundary-results,execution-probe}.json`,
`/tmp/ctcompile-after-string-fields-boundary-{results,sources}.json` and
`/tmp/ctcompile-bigint-pow-measurement.json`. Full evidence is
`/tmp/ctcompile-string-fields-gate-final-evidence.json` and
`/tmp/ctcompile-string-fields-full{,-detail}.log`.

## Owning String globals and recovered execution, 2026-09-09

Resumed **21 uncommitted String files at 0ea9aaed**, identified in the
**14:51:39 UTC** synchronization journal and predecessor diffs after the
**14:53:51** explicit abandonment. Older codex-wip recovery and source-split
repairs were already landed. Three agents audited proof tests, recovered
execution tests and measured the next boundary; root finished the Map harness
after two agents hit rate limits. No browser/runtime source changed.

**0e041bba** extends exact scalar-global evidence to String origins while
retaining one earlier store, original SSA scope/order, the complete environment
and future callable family, fingerprints and bounded work. Type inference
continues subscribing to the actual stored SSA lattice; categories supply no
type or value. **4b0a1199** selects owning per-binding storage from the complete
store-type census and checks the exact String tag before printing quoted,
percent-encoded bytes. Empty String remains distinct from Null or Undefined.

The unchanged **3a99e34c** String-copy source and **f0a03c19** literal-copy
candidate advance **0/5 -> 5/5 native** in both modes, preserving eight calls
and all five typed observations. **b4505df6** executes the historical String
method result at **3/3**, plus empty and escaped UTF-8/NUL examples. Saved
`std::function<std::string()>` callables and copied bytes survive owner/global
release. Explicit/deduced GCC/Clang binaries link no Script/VM symbols.

**417cd0ac** completes the interrupted Map execution gate. All **52 focused
native programs**, **71 typed Node/interpreter probes**, **24 emitted wrong-tag
or missing-store controls**, fresh/stale forgeries, prepared reruns and **19
refusal/exact-repair families** pass. The refusals separate **12 ownership** and
**seven complete-owner** boundaries. Every one of **596 historical helper rows
across 55 helpers** and all **24** original source/candidate hashes is preserved.
Budgets **9271/9316** check **31/32 cutoffs**. The new long-String lifetime keeps
snapshots across **128 future calls**, both flags, owner/table release, mutation
of the original globals, independent entry reexecution and final Map/leaf
release under ASan/UBSan/leak checks.

The warning-free **12-step** tools/proof build initially passed **5/6 CTests
in 146.70 seconds**. Two new assertions wrongly refused existing nullable
String-key routes; their unchanged Null/Undefined sources now require complete
ownership with **zero invented String scalar edges**. The warning-free two-step
rebuild passes the corrected owner test in **113.20 seconds**. All six focused
proof checks therefore have passing results. Source/prepared queries each have
**93 rows**, including exhaustive owning/empty String host budgets **7781/7852**.
Type inference covers **122 rows/80 live edits**, including actual pending,
String, mixed, optional and boxed states.

The corrected six lowering lit cases pass in **0.29 seconds**. Their first run
had a new optional-String diagnostic expectation wrong: storage rejects that
carrier before the output check. The first execution run stopped in its new
Node Null mutation observer; encoding Null separately repaired the test without
changing source or production. Both initial failures remain in the evidence.
Stable clang-format **22.1.8 passes all 745 files**; bundled 23 retains the same
nine preexisting differences. The initial full run freezes **23** code/test
inputs; the corrected lit rerun adds only one diagnostic test, **24** in all.

The warning-free **246-step** full build passes **511/517 CTests in 1817.89
seconds**. The five established browser failures are `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors`; the only compiler failure is
lit **164/165 in 1096.42 seconds** (CTest **1096.48**). Its integrated Map test
passes all **302 native programs/37 lifetime families**. **9582188d** preserves
every historical divergence source and updates the obsolete String-global load
refusal to the still-unproved call-result store. A no-work rebuild and focused
lit rerun pass **1/1 in 0.08 seconds**. The complete corrected lit rerun passes
**165/165 in 1096.14 seconds** (CTest **1096.20**, total **1096.21**),
including all **302 Map programs/37 lifetime families**. All **24** final code/test
hashes match local files, committed HEAD, frozen inputs and devbox sources.
Across the initial full run plus corrected lit rerun, all **372 compiler CTests**
and **512/517 total** have passing results; only the five established browser
failures remain. These are one full run and one lit-only rerun.

Fresh coverage remains **19/574, 39/4754 and 45/7725**, both modes, zero
pruned; exact Bootstrap Data remains
**0/7, 0/7 and 0/8**. The four escape corpus oracles have zero violations;
fixture precision remains **72/109**. The independent BigInt Pow probe agrees
on all **16 common checks** and measures the existing oversized-exponent gap:
Node's four small-base cases succeed, while the VM raises four RangeErrors.
Pow retention-origin analysis remains the next separate escape increment; no
native BigInt or normal-completion guarantee was added.

**Next: owning String fields on proved ordinary objects.** A fresh **13-source**
probe agrees on typed Node/interpreter observations in every case. The exact
**88d51f7d** seven-call String-field source remains **0/5** at the live captured
method proof; its Number-field repair is **5/5**. String field reads, saved
long bytes and alias writes also remain refused. Extend the existing captured
Map field proof, independent field admission and per-field owning emission
together, preserving actual store joins and definite initialization. Exact zero
after clear, full native Bootstrap and direct browser APIs remain unfinished.
See `bootstrap-provider-next.md` for the exact source and measured boundary.

Evidence: `/tmp/ctcompile-string-complete-{proof,query-rerun,lowering-rerun,methods,execution-rerun}.log`,
`/tmp/ctcompile-string-complete-{source-hashes,source-audit}.json`, and
`/tmp/ctcompile-after-string-boundary-{results,sources}.json`. Full evidence is
`/tmp/ctcompile-string-gate-final-evidence.json`; the independent Pow probe is
`/tmp/ctcompile-string-nextpow-results.json`.

## Definite Boolean globals and recovered execution gate, 2026-09-09

Resumed four uncommitted Boolean execution files at **1138dbfd**, identified
in the **11:02:47/12:11:22 UTC** synchronization journal and the interrupted
execution log; the **12:13:13** loop exit explicitly abandoned their claims.
The older codex-wip recovery and browser source-split repairs were already
landed. Three agents recovered execution controls, audited escape evidence
and investigated the next String boundary. No browser/runtime source changed.

Previously committed **df304fdf** extends the live scalar-global proof to
Boolean origins. Actual stored SSA lattices still determine types, and the
complete source-store census independently chooses Number or Boolean output.
Both observation helpers require the exact runtime tag; output preserves
`true`/`false` rather than accepting numerically equal values.

**ab61f9ac** completes the interrupted execution gate. The original
**681c8895** Boolean copy and **d3a90c01** literal-copy candidate now admit
**5/5 native** in both modes, preserving five typed observations and eight
source calls. A fresh devbox run passes **34 native programs**, **49 typed
source/candidate probes**, **14 wrong-tag/null/missing-store mutations**, fresh
and stale forgeries, prepared reruns, and the new mixed Number/Boolean Map
lifetime. The lifetime retains snapshots across **128 future calls**, both
branches, owner/table release, independent reentry and final Map/leaf release.
Explicit/deduced GCC/Clang output links no VM symbols. Boolean budgets
**9271/9351** check **31/29 cutoffs**, with no rollback interval.

The interrupted failure was a test classification: optional and mixed Boolean
initializers contain conditional entry operations and fail the ownership
proof. The corrected gate checks **eight ownership**, **five Map-identity**
and **two global-storage** refusals and their exact repairs. All **557**
historical helper rows, **24** historical source hashes and **49** current
source strings remain unchanged by the correction. Saved devbox temporary
artifacts were unavailable, so the final focused run regenerated every native
program instead of relying on those earlier binaries.

The fresh no-work tools build passes **all 14 focused CTests in 149.81 seconds**,
including type inference, host/owner queries and all eight escape checks.
Source/prepared scalar queries each cover **54 rows** and exhaustive live
proof budgets. Already committed **1138dbfd** proves dynamic BigInt Div/Mod
retention origins; its five frozen code/test hashes, exact source-row checker
and **34** mutation controls were independently audited. The fresh fixture
reports **72/109** precision and all four escape oracles report zero violations.
This proves retention only, not native BigInt admission or completion/effects.

The initial full gate builds **247 steps without warnings** and completes
**511/517 CTests in 1704.85 seconds**. Its lit test passes **163/165 in
980.26 seconds** (CTest 980.47): two historical tests still expected Boolean
results to refuse. **7dc796b7** promotes the unchanged method-table Boolean
source to **3/3** execution and adds a false result; **48885ae1** promotes the
unchanged five-call Map Boolean source to **4/4**. Their focused explicit/deduced
GCC/Clang executions pass. The method test passes in **20.95 seconds**; a
**197-case** census preserves all **173** historical refusal classifications.
The initial Map run completed all **283 programs/36 lifetime families** before
its stale refusal check. The corrected inventory is **284 programs/36 families**.
No production code changed during these test repairs. The complete corrected
lit rerun passes **165/165 in 1040.72 seconds** (CTest **1040.78**), including
all **284 programs/36 lifetime families** and the ten method-table programs.
All **28** final code/test hashes match local files, committed HEAD, frozen
inputs and devbox sources.

Across the initial full run and corrected lit rerun, **all 372 compiler CTests**
and **512/517 total CTests** have passing results. This is not a second full
517-test run. The five established browser failures
are `selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.
Fresh native coverage remains Bootstrap **19/574**, p5 **39/4754**, Phaser
**45/7725** in both modes, with zero pruned. Exact Bootstrap Data remains
**0/7 CommonJS, 0/7 browser, 0/8 AMD**. Stable clang-format **22.1.8 passes all
745 files**; bundled 23 retains the same nine preexisting differences.

**Next: owning String globals and exact typed observations.** The unchanged
**3a99e34c** String copy remains **0/5** at the scalar-read/Map-identity boundary;
its **f0a03c19** literal-copy candidate remains **0/5** at Number/Boolean-only
global storage. Extend live String evidence together with independently typed,
owning per-binding storage and byte-preserving output. Empty, missing and null
must remain distinct; actual optional/mixed/boxed lattices remain authoritative.
String leaf fields, exact zero after clear, full Bootstrap initialization and
direct browser API integration remain open. See `bootstrap-provider-next.md`.

Evidence: `/tmp/ctcompile-boolean-gate-{focused,execution,full,full-detail}.log`,
`/tmp/ctcompile-boolean-corrected-lit.log`,
`/tmp/ctcompile-boolean-gate-{initial,final}-evidence.json`,
`/tmp/ctcompile-boolean-gate-final-inventory.json`,
`/tmp/ctcompile-boolean-completion-census.json`,
`/tmp/ctcompile-boolean-execution-completed-frozen.json` and
`/tmp/ctcompile-boolean-corrected-source-hashes.json`. The initial 27-input
manifest and failed full-run evidence remain separate from the corrected gate.

## Constant-only Number globals and signed BigInt shifts, 2026-09-09

Resumed the exact **3c1dfd95** constant-only Number continuation from
**68bcc183**, `bootstrap-provider-next.md` and the **08:45:54 UTC** sync journal.
The tree/index started clean; codex-wip recovery and source-split build repairs
were already landed. Three agents handled proof tests, execution/lifetime
checks and a separate escape-analysis increment. No browser/runtime source
changed, and no push occurred.

**06c4a649** allows the existing scalar-read proof to have no published-call
dependencies for constant-only Number expressions. Original SSA scope, one
earlier store, the complete environment and the complete owning method family
remain mandatory. Nonempty dependencies still belong to that family. Number
categories supply neither values nor native types; type inference continues
subscribing to the actual stored value, including pending/optional/boxed states.

The unchanged eight-call **3c1dfd95** source advances **0/5 -> 5/5 native** in
both modes, preserving `copy=7, first=1, fixed=7, second=2, trace=12`.
All **24** original source/candidate probes agree on Node/interpreter output:
**five original and nine candidate programs admit**, while ten retain their
independent ownership, Map-identity or global-carrier refusals. The warning-free
**32-step** tools build and **15-step** proof build pass all **six focused
CTests in 131.63 seconds**. Source/prepared scalar queries each cover **28 rows**,
**11 published/13 constant live edits**, two inaccessible Number arms and every
**7802/7781/7873/7852** host budget cutoff. Type inference checks **40 rows,
16 edits** and pending -> i32 -> f64 -> optional -> boxed propagation.

**4d2906d1** gates **17 native programs**, **27 typed observations**, three
unowned/seven complete-owner refusals with exact edits, fresh/stale forgeries,
prepared/emitted scalar dataflow and reruns. Explicit/deduced GCC/Clang builds
preserve signed zero, Number NaN and actual scalar loads/arithmetic with no VM
symbols. The new sanitizer lifetime exercises **128 future calls**, both flags,
owner/table release, independent entry execution, final Map destruction and
separately retained leaf release. Constant aliases and Number snapshots survive
leaf mutation. Budgets **9271/9441** check **31/32 cutoffs**, with no rollback
interval. All **173 historical refusal classifications**, **514 helper rows**
and **24 prior source hashes** are unchanged; only the intended historical
`scalar_constant_only` admission changes.

**d7e4f154** adds independently proved static BigInt **Shl/Shr**
origins. Signed shifts can raise an independent RangeError; the proof establishes
local retention only, never normal completion, no-throw effects or native BigInt
admission. The live Node/interpreter preprobe agrees on all **16 checks**.
The first warning-free **ten-step** build passes seven of eight escape CTests;
the fixture incorrectly expected a source-literal claim for the interpreter's
implicit RangeError. The correction preserves all source and production bytes,
pins the exact shift/source coordinates and requires that one independently
retained error separately, while every source allocation still needs its claim.
The corrected no-work resync passes **all eight escape CTests in 8.81 seconds**.
Each shift checks **119 rows, 47 live states and 4592 budget cutoffs**. The
source family records **24 sites, 47 instances and 35 retained**, plus the
separate implicit Error. Fixture precision is **68/103** and all four escape
oracles report zero violations; corpus precision remains **0/64, 0/16, 0/20**.

Stable clang-format **22.1.8 passes all 745 files**; bundled 23 retains the same
nine unrelated differences and changed C++ passes both. Independent production,
source-preservation and generated-artifact audits pass. The warning-free
**241-step** full build finishes **512/517 CTests in 1698.62 seconds**, including
all **372 compiler checks**. Lit passes **165/165 in 979.39 seconds** (CTest
979.46), including the integrated **266 programs and 35 lifetime families**.
Type inference passes in **0.06 seconds**, shared ownership
in **97.49**, seeded host Maps in **36.60**, escape arrays in **0.39**, and
exception recovery in **1.18**. The only failures are the established browser
tests `selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.
All **fourteen** final code/test hashes match local files, committed HEAD, the
frozen inputs and devbox sources; the full gate repeats all four zero-violation
escape oracles and the signed-shift source/budget controls.

Remeasured native coverage remains Bootstrap **19/574**, p5 **39/4754**, Phaser
**45/7725** in both modes, with **zero pruned**. Exact Bootstrap Data remains
**0/7 CommonJS, 0/7 browser, 0/8 AMD**. The isolated constant-global improvement
does not establish full Bootstrap initialization.

**Next: definite Boolean globals and typed observations.** The exact
**681c8895** Boolean copy source retains five functions/eight calls and complete
ownership but remains **0/5** at the Number-only Map identity boundary. Its
**d3a90c01** literal-copy candidate removes that boundary but remains **0/5** at
`store to global fixed requires a numeric global`. Boolean read evidence alone
cannot close this: admission and final observations must use independently
proved Boolean types and preserve exact runtime tag checks. String globals need
owning storage/output too. Exact zero after clear, String leaf fields, full
Bootstrap initialization and direct browser API integration remain unfinished.
See `bootstrap-provider-next.md` for the exact source, hashes and proof sites.

Evidence: `/tmp/ctcompile-constant-{baseline,first}.json`,
`/tmp/ctcompile-constant-{proof,smoke,execution,escape,escape-corrected}.log`,
`/tmp/ctcompile-constant-refusal-census.json`,
`/tmp/ctcompile-constant-{original,lifetime}.cpp`,
`/tmp/ctcompile-constant-inventory.json`,
`/tmp/ctcompile-constant-full{,-detail,-hashes}.log`, and
`/tmp/ctcompile-constant-final-evidence.json`.

## Definite scalar-global initialization and static BigInt origins, 2026-09-09

Resumed the exact **8003b4bc** alias continuation in **53692779**,
`bootstrap-provider-next.md` and the **06:48:45 UTC** synchronization journal.
The tree/index started clean; codex-wip recovery and browser source-split
repairs were already landed. Three agents handled query tests, execution
review and escape analysis. Root recovered the execution agent's partial
sources after two rate limits and owns every devbox gate and commit.
No browser/runtime source changed and no push occurred.

**396b7e46** uses the fresh complete owner proof only to remove implicit
Undefined from an exact scalar global load. The single indexed store, live
initialization, binding and original SSA value must agree. The load subscribes
to that actual value's lattice: pending stays pending, later i32/f64/boxed
widening propagates, and a host Number category cannot invent a native type.
Dynamic globals, missing/duplicate stores and incomplete/stale proofs retain
their conservative behavior. Final admission and global storage are unchanged.

The unchanged eight-call **8003b4bc** alias and seven-call **06efa534** direct
observation advance **0/5 -> 5/5 native** in both modes, preserving trace=12/1.
Three new alias-chain/arithmetic/saved-branch programs also reach 5/5. All
**21** source probes agree on Node/interpreter observations: **15 native,
four unowned, two complete-owner Map-identity refusals**. The result-fed key
now independently has a definite `js_num` formal instead of nullable scalar;
its actual double-key/object Map schema is unchanged.

The warning-free **57-step** build passes five host/owner tests in the initial
**5/6 CTest run, 129.63 seconds**. Its only failure was a new duplicate-store
owner expectation: ownership can remain complete while scalar edges are empty.
Corrected tests pass after the warning-free **12-step** rebuild. Type inference
passes in **0.05 seconds**, covering **20 rows, eight live edits**, exact and
incomplete budgets, source/prepared forms and forced pending/i32/f64/boxed
subscription stages. All six relevant compiler tests pass across these runs.

**bbedee5b** gates **19 focused native programs**, four unowned/two complete-owner
refusal families, exact edits, prepared/emitted scalar graphs and fresh/stale
forgeries. Both explicit/deduced GCC/Clang forms link no VM. A new alias lifetime
family exercises **128 future calls**, both branch flags, owner/table release,
pre-reentry observations, independent entry execution and final Map/leaf release.
Aliases survive retained-leaf mutation. Together with the previous saved-scalar
family both sanitizer lifetimes pass. Corrected stale controls preserve raw
calls without demanding a fresh proof's direct-call rewrite; the stronger
resolved-call assertion remains on the fresh complete-owner path.
Budgets **9249/9307** each check **32 cutoffs**, with no rollback interval.
All **173 historical refusal classifications** and **506 helper rows across
53 helpers** remain unchanged. All nineteen executions and corrected remaining
controls pass across runs; the integrated **250-program/34-lifetime** gate
also passes in the final full run.

**74db2857** adds static BigInt Add/BitAnd/BitOr/BitXor retention categories,
requiring independently original BigInt operands and charged per-path state.
Shifts, mixed/opaque operands and unsupported dynamic operations still refuse.
This is retention evidence, not native BigInt admission or an effect guarantee.
The Node/interpreter preprobe agrees at **65535**. The first escape run passes
seven tests; a new generic enum-mutation setter crashes the arrays test.
Restoring typed setters preserves the malformed-enum control. Its warning-free
**two-step** rebuild passes arrays in **0.36 seconds** (total 0.37), so all
eight escape tests pass across runs. Each static kind checks **105 rows,
39 live states and 3706 cutoffs**, plus a **128-work** snapshot. The six new
sources measure **24 sites, 48 instances, 38 retained**; fixture precision is
**64/98**, all four oracles report zero violations, and corpus precision stays
**0/64, 0/16, 0/20**.

Stable clang-format **22.1.8 passes all 745 files**; bundled 23 retains the
same nine unrelated differences and changed C++ passes both. Independent
production, source-preservation and generated-artifact audits pass. After an
initial SSH timeout before any build, server start and allow-ip restore access.
The warning-free **243-step** full build completes **512/517 CTests in
1649.36 seconds**, including all **372 compiler tests**. Lit passes **165/165
in 928.06 seconds** (CTest 928.26). Type inference passes in **0.09 seconds**,
shared Map ownership in **89.21**, host contract in **0.18**, seeded host Maps
in **37.01**, and escape arrays in **0.45**. Only the five established browser
failures remain: `selectors`, `frames`, `element_attrs`, `vm_async`,
`early_errors`. All twelve final code/test hashes match local files, committed
HEAD, the frozen input and the final devbox sources. The full run rechecks the
static BigInt matrix and all four zero-violation escape oracles.

Fresh corpus coverage stays Bootstrap **19/574**, p5 **39/4754**, Phaser
**45/7725** in both modes, with **zero pruned**. Exact Bootstrap Data stays
**0/7 CommonJS, 0/7 browser, 0/8 AMD**. No complete Bootstrap gain follows from
these isolated alias sources.

**Next: constant-only Number global edges.** The exact eight-call **3c1dfd95**
source in `bootstrap-provider-next.md` still proves ownership but refuses native
Map identity because `fixed = 7; copy = fixed` has no published-call dependency.
Its exact **41a33e40** literal-copy repair reaches 5/5 and preserves all five
observations. HostContract and OwnedGlobalMethods both require nonempty call
dependencies today. Extend only Number origin/initialization evidence after
complete live scope/order/write/environment and ownership proofs; empty call
dependencies must not create a native type. Keep pending/boxed/optional lattice
values, dynamic globals, multiple writes and effects conservative. The next
boundary doc names the proof/test sites and direct empty-dependency controls
for stale/exhausted evidence, invalid SSA scope, unknown calls and non-Number
initializers.
All **24** next source/candidate-edit observations agree after applying the
reference's documented String encoding and Number NaN formatting conventions.
Only two candidate edits admit; the other ten remain refusals. Exact zero after
clear, String leaf fields, full Bootstrap and direct browser API integration
remain unfinished.

Evidence: `/tmp/ctcompile-alias-{baseline,first-census}.json`,
`/tmp/ctcompile-alias-{proof,focused,corrected-arrays,execution,controls}.log`,
`/tmp/ctcompile-alias-refusal-census.json`,
`/tmp/ctcompile-alias-{original,lifetime}.cpp`,
`/tmp/ctcompile-alias-next-corrected.json`,
`/tmp/ctcompile-alias-full{,-detail,-hashes}.log`, and
`/tmp/ctcompile-alias-final-evidence.json`. The source/lifetime inventory is
`/tmp/ctcompile-alias-inventory.json`.

## Saved Number globals and BigInt binary origins, 2026-09-09

Resumed the exact **d74ae2ee/867378b1** saved-global continuation in
**1d965169**, `bootstrap-provider-next.md` and the **05:14:01 UTC** journal.
The tree/index started clean; the older codex-wip recovery and browser source
split repairs were already merged. Three agents handled query tests,
execution/lifetimes and the independent escape increment; root owns every
devbox gate and commit. No browser/runtime source changed and no push occurred.

**801794d8** proves per-load saved Number edges through the existing
HostContract/OwnedGlobalRoots seam. Each edge records its single earlier entry
store, original SSA value and completed published-result dependencies. Source
order, scope, all writes, current callable identities and the whole method
family prove independently. Both host and owner publish these edges only after
complete success within their shared budget. NativeMap exempts only those exact
reads; constructor, prototype, reflection and unknown-effect guards remain.
Categories never become values or native types. Existing lowering is reused.

The unchanged eight-call saved-results and saved-size witnesses advance
**0/5 -> 5/5 native** in both modes, preserving **trace=3/12**. Their three/two
scalar stores and loads, arithmetic and every call remain. Both execute under
explicit/deduced GCC 13 and Clang 18 with exact interpreter output for every
requested scalar global and no Script/VM/AOT symbols. All **31** historical
continuations agree on Node/interpreter observations: **22 native, nine unowned**.

The first **33-step** tool build is warning-free. A new test range-loop copy
warning was corrected with a const reference; the warning-free **eight-step**
rebuild passes all **five host/owner CTests in 126.27 seconds**. Source/prepared
queries each check **16 rows, 11 live edits and every 7802/7873 host cutoff**,
plus existing owner cutoff families. Stable formatter **22.1.8 passes all 745
files**; bundled 23 retains the same nine unrelated differences, with changed
C++ passing both versions. Fourteen committed core code/test hashes match
their frozen inputs and HEAD; final validation is recorded below.

**b953ab00** proves independently original BigInt Add/Sub/Mul result origins.
Their separate per-path category cannot flow into a non-BigInt assumption.
Mixed/object inputs, static BigInt operations and Div/Mod/Pow remain refused;
this is retention evidence, not native BigInt emission or an effect guarantee.
The warning-free **ten-step** build passes **eight escape CTests in 9.32
seconds**. Each operator checks **97 rows, 29 live states and 3098 cutoffs**,
plus a **128-work** snapshot. Five new sources measure **20 sites, 40 instances
and 32 retained**; one unchanged historical child newly proves confined.
Fixture precision is **61/93** and all four oracles report zero violations;
corpus precision stays **0/64, 0/16, 0/20**. Fifteen arithmetic premeasurement
checks agree; the separately refused object-conversion check differs (Node
65535 versus VM 32767 overall), recorded for the runtime owner.

**1bd5b5ac** gates **14 programs** (two historical saved sources, their two
inline repairs and ten new positives), **four unowned/four complete-owner
refusal families**, exact repairs, prepared/emitted dataflow, fresh/stale
forgeries and reruns. One new ASan/UBSan/leak lifetime family exercises **128
future calls**, both Boolean arms, owner/table release, reentry, final Map
release and an independently retained leaf. Number snapshots survive every
mutation and owner release. Budgets complete at **9920/9249**, checking
**30/32 cutoffs**, with no speculative rollback interval. The integrated full
gate passes all **245 programs and 33 lifetime families**.

All **18** new Node/interpreter probes agree: **ten native, four unowned and
four complete-owner refusals**. The final **173-case** refusal census has no
errors; all **169 historical cases** retain source hashes, function/call counts
and both-mode classifications. All **472 historical helper rows across 50
helpers**, the original 31 continuation sources and first 15 new probes
preserve their source bytes. The first focused test run caught two new
expectation errors: the result-fed key uses the existing nullable scalar
parameter carrier, and duplicate writes plus arithmetic fail host proof.
Only expectations changed; the corrected full focused gate passes.

The complete **242-step build has zero warnings**. Full CTest finishes
**512/517 in 1652.00 seconds** (exit 8): all **372 compiler tests pass** and
**140/145 browser tests pass**. Only the five established browser failures
remain: `selectors`, `frames`, `element_attrs`, `vm_async`, `early_errors`.
Lit passes **165/165 in 935.40 seconds** (CTest 935.46). Final owner and seeded
host queries pass in **87.82/37.75 seconds**; ExceptionRecovery passes in
**1.19 seconds**. All eight escape tests pass again, retaining the fixture and
corpus measurements above.

Remeasured native coverage remains **Bootstrap 19/574, p5 39/4754, Phaser
45/7725** in both modes, with zero pruned functions. Exact Data stays **0/7
browser/CommonJS and 0/8 AMD**. The isolated saved-global improvement does not
establish complete Bootstrap initialization.

All **18 final code/test hashes** match local files, committed HEAD, the frozen
snapshot and final devbox sources. Inspected generated sum/lifetime C++ retains
actual scalar stores/loads, arithmetic, Map mutation, saved callables and leaf
ownership. Saved Numbers survive mutation, reentry, owner destruction and the
independent leaf's final release. No Script/VM/AOT symbols occur; weak lifetime
observers are test instrumentation.

**Next: definite initialization in native global-load type inference.**
The exact eight-call alias source **8003b4bc**, trace=12, now passes Map
identity but refuses at `store to global alias may be null or undefined`.
The existing global lattice starts with Undefined even for this exact earlier
store. Use the fresh scalar edge only as initialization evidence, then subscribe
to and join the actual stored-value lattice; never manufacture NumType from a
host category. Preserve dynamic-global, multiple-write and stale/incomplete
proof refusals and leave the final observation gate intact. Its exact `first + 0`
repair preserves every call and both alias operations and reaches 5/5. The
seven-call direct `trace = first` source reaches the same separate boundary.
Exact zero-size after clear, String leaf fields, full Bootstrap and direct
browser API integration remain unfinished; no full-Bootstrap coverage gain is
claimed. The previous checkpoints below retain their historical measurements.

Evidence: `/tmp/ctcompile-scalars-{first,final-census}.json`,
`/tmp/ctcompile-scalars-{query2,escape,smoke}.log`,
`/tmp/ctcompile-scalars-{proof,core}-frozen.json`, and
`/tmp/ctcompile-scalars-{sum,snapshot}.cpp`,
`/tmp/ctcompile-scalars-{execution,full,full-detail,full-hashes}.log`,
`/tmp/ctcompile-scalars-final-{frozen,evidence}.json`,
`/tmp/ctcompile-scalars-final-{sum,lifetime}.cpp`, and the historical source/
refusal audit JSON files. All work is committed locally; the shared tree and
index are clean.

## Published Number arithmetic and BigInt unary origins, 2026-09-09

Resumed interrupted iteration 21 from **62282da1**, the **03:29:26 UTC**
journal's exact arithmetic witness and the abandoned **03:34/03:35** test
claims. The tree/index started clean. The older codex-wip recovery and browser
source-split build repairs were already landed; no other branch was changed.
Three agents handled query tests, execution/lifetimes and escape analysis; root
recovered the escape agent's unfinished audit after its rate limit.

Committed **0468fed4** proves entry arithmetic over published Number results,
**83c32f0c** gates execution/lifetimes, and **6f62fbaf** records computed BigInt
unary escape categories. No browser/runtime source changed; no push.

The unchanged **379ccc** eight-call witness advances **0/5 -> 5/5 native**,
trace=3, in both modes. Its eight-call arithmetic-free repair remains 5/5,
trace=1. Add/Sub/Mul/Div/Mod/Pow require independently exact Number operands;
result expressions can feed later same-method arguments only through the
bounded invocation worklist and complete future-call census. Saved scalar
aliases keep their source order. Categories never become concrete values or
select branches. Source SSA scope, including both yield operands, is checked
independently of selected-arm effect order. Standard Number/Map lowering is
reused, with no new runtime dependency.

All **31** continuation sources agree on Node/interpreter observations and both
admission modes: **20 native 5/5, nine unowned 0/5, two owner-complete 0/5**.
All **12 earlier continuation hashes** and **414 historical helper rows** stay
unchanged. The **18-program** focused execution gate passes explicit/deduced
GCC/Clang and no-VM checks, **nine refusal/exact-repair controls**, **two saved
global carrier refusals**, reruns/forgeries and **two sanitizer lifetime
families**. Saved callables exercise 128 future calls, both Boolean arms,
owner/table release, reentry and final Map/leaf release. Numeric snapshots
survive owner destruction. Budgets complete at **9752/12792**, each checking
31 cutoffs, with no speculative rollback interval. A **169-case historical
refusal census** has zero errors and identical classifications in both modes.

The warning-free **21-step** combined build passes **19/20 CTests in 143.14
seconds**; its sole failure was three misspelled opcodes in new escape tests.
The corrected **two-step** rebuild passes all **eight escape CTests in 9.00
seconds**. All twenty focused tests pass across these runs. Host/owner tests
check **41 rows per source/prepared form**, stale/fresh edits and exhaustive
budgets. **5bd3e76f** adds four condition-scope controls and a host-only guard;
the warning-free seven-step rebuild passes both affected CTests **2/2 in
118.77 seconds**. Each form now checks **20 scope controls**. Entry SCF still
retains the existing owner refusal.

BigInt Neg/BitNot results retain a separate original category through saved
reads and path copies. Every non-BigInt consumer excludes that category; mixed,
opaque and binary BigInt operations remain refused. Each unary passes **83
rows, 15 live states and 2257 retention cutoffs**, with a **128-work** snapshot
control. Five new sources measure **20 sites, 40 instances and 32 retained**;
one unchanged historical child newly proves confined. Fixture precision is
**58/89**, including added coverage; all four escape oracles report zero
violations and corpus precision stays **0/64, 0/16, 0/20**. The exact unary
premeasurement agrees at trace=65535. This proves retention, not native BigInt
admission, allocation success or a no-throw/effect contract.

Stable formatter **22.1.8 passes all 745 files**; changed C++ also passes
bundled 23, whose complete check retains the same nine unrelated differences.
The complete warning-free **250-step** build finishes **512/517 CTests in
1583.01 seconds** (CTest exit 8). All **372 compiler tests pass**. Only the
five established browser failures remain: `selectors`, `frames`,
`element_attrs`, `vm_async`, `early_errors`. Lit passes **165/165 in 874.79
seconds** (CTest 874.98), including **233 published Map programs and 32
sanitizer lifetime families**. The final owner and seeded-host queries pass
in **84.19/35.63 seconds**; ExceptionRecovery passes in **1.18 seconds**.

Remeasured native coverage stays **Bootstrap 19/574, p5 39/4754, Phaser
45/7725** in both modes, zero pruned. Exact Data remains **0/7
browser/CommonJS and 0/8 AMD**. All four escape oracles again report zero
violations, with the fixture/corpus counts above. No full-Bootstrap gain is
claimed from the isolated arithmetic improvement.

All **fourteen final code/test hashes** match local files, committed HEAD,
the frozen snapshot and the devbox after the gate. Inspected focused sum and
lifetime C++ retains actual Number arithmetic, Map mutation, leaf fields and
owning saved callables; it contains no `ctbrowser::script` symbol or VM/AOT
runtime dependency. The lifetime observer separately checks final Map/leaf
release.

**Next: native Map identity across independently proved saved scalar globals.**
The exact eight-call saved-results source **d74ae2ee** now has a complete host
owner, but remains 0/5 at `standard Map identity is unproved with other
host/global value reads`. Its actual three result stores, three scalar loads,
two additions and every call survive preparation. The exact inline repair
restores the admitted 379ccc source without dropping calls or arithmetic.
The eight-call size-snapshot source **867378b1**, trace=12, has the same
boundary and an arithmetic-preserving inline repair. Extend the live
HostContract/OwnedGlobalRoots -> NativeMap seam with independently proved
scalar reads; a spelling, observation or report must not authorize a global.
Exact zero-size after clear and String leaf fields remain separate refusals.
Full native Bootstrap and direct browser API integration remain unfinished.

Evidence: `/tmp/ctcompile-numeric-{baseline,first,census}.json`,
`/tmp/ctcompile-numeric-{focused-build,focused-focused,escape2,execution}.log`,
`/tmp/ctcompile-numeric-refusal-census.log`,
`/tmp/ctcompile-numeric-{full,full-detail,full-hashes}.log`,
`/tmp/ctcompile-numeric-{final-frozen,final-evidence}.json`,
`/tmp/ctcompile-numeric-final-{sum,lifetime}.cpp`, and
`/tmp/ctcompile-numeric-saved.mlir`. See `bootstrap-provider-next.md` for the
exact next source and its independently gated repair.

## Captured Map.clear and exact BigInt relations, 2026-09-08

Resumed the precise `Map.clear()` continuation in **`b1e8ba6b`**, the previous
HANDOFF and the **02:13:49 UTC synchronization journal**. The tree/index began
clean; the older `codex-wip-20260907` recovery was already merged. Three agents
worked on query controls, native execution/lifetimes and escape proofs. Root
finished the query agent's final audit after it hit a rate limit.

Committed locally: **`1c352a82`** proves captured clear and arbitrary-key absence,
**`c2b49f99`** gates native execution/lifetimes, and **`9f969651`** proves exact
BigInt relational escape origins. No browser/runtime source changed; no push.

Both original seven-call clear sources advance **0/5 -> 5/5 native**, trace=1,
in both optimization modes. Standard zero-argument clear returns Undefined and
reuses existing native Map lowering. Every invocation starts with unknown Map
contents. Clear establishes a complete possible-key census; subsequent sets
record every possibly present key, and branches union those possibilities.
Exact absence intersects across both arms, including clear versus exact delete.
Cross-arm comparisons cannot borrow the other arm's narrowed types. Saved
object, field and Undefined reads retain their read-time values. Live receiver,
callee, arity, source scope, effects and the entire published family still prove
independently; annotations supply no authority. All historical source bytes and
22 previously measured clear programs remain intact.

The warning-free **22-step** focused build passes **19/20 CTests in 125.60
seconds**. Its sole failure is an older escape expectation: the exact original
BigInt relational child is now confined. Updating only that verdict preserves
all historical JavaScript and allocation/retention counts. The corrected fixture
passes **1/1 in 0.21 seconds** after a no-work rebuild. All twenty focused tests
therefore pass across these runs; this is not a second twenty-test run.

Each source/prepared host/owner query passes **29 clear rows**, eight scope
mutations, stale/fresh live edits and exhaustive work budgets. Host completions
are **4324/5441 source, 4419/5574 prepared**; owner completions are
**8853/9982 source, 8653/9820 prepared**. The **21-program** execution gate passes
Node/interpreter and explicit/deduced GCC/Clang, no-VM symbols, **five refusal
and exact-repair controls**, reruns and **three sanitizer lifetime families**.
Saved callables survive owner/table release and 128 future calls, reentry and
final Map/leaf release. A separately retained leaf remains readable after the
Map dies, then expires when its final owner releases it. Native budgets complete
at **4121/4866**, checking **31/30 cutoffs**, with no speculative rollback interval.

Each BigInt Lt/Le/Gt/Ge query passes **60 rows, 32 live states and 2684 retention
cutoffs**, plus the 64-work snapshot; Eq repeats **52/30/2448**. The new source
family measures **20 sites, 40 instances and 32 retained**. Fixture precision is
**55/85**, including one historical confinement improvement and added coverage;
all four escape oracles report zero violations, with corpus precision unchanged
at **0/64, 0/16, 0/20**. This is retention evidence, never a native BigInt carrier
or a no-throw/effect proof. Supported premeasurement agrees at **trace=65535**;
separate refused mixed String/object conversions differ, **Node 15 versus VM 8**,
and are journaled for the runtime owner.

Stable formatter **22.1.8 passes all 745 files**; changed C++ also passes bundled
23, whose complete check retains the same nine unrelated differences. Twelve
first-full code/test hashes match the frozen inputs and devbox sources. The full
**240-step build has zero warnings**. Initial CTest finishes **511/517 in 1420.17
seconds**: **371/372 compiler, 140/145 browser**. Lit passes **164/165 in 738.29
seconds** (CTest 738.36); all 215 positive Map programs and 30 lifetime paths ran
before the old `seeded_cleared` assertion demanded an unowned source. Clear now
completes that owner, while native admission stays **0/5** at an unsupported
nullable Number Map-key carrier. The same browser selectors, frames,
element_attrs, vm_async and early_errors failures remain. Exception recovery
passes in **1.20 seconds**.

**`7011c79e`** corrects that classification without changing JavaScript or
compiler production. The original nine-call source keeps exact carrier
diagnostics and prepared producer/consumer, receiver, callee and capture edges.
Its repair substitutes `has(0)` for `clear()`, retaining all nine calls. After a
no-work rebuild, all **twelve carrier families** pass in both modes with exact
repairs, fresh/stale forgeries and reruns; HostContract CTest passes **1/1 in
0.16 seconds**. The complete corrected lit rerun passes **165/165 in 776.42
seconds** (CTest 776.49, total 776.50). All **372 compiler tests have passing
results across the full run and corrected rerun**; this is not a second
517-test run. The published Map gate completes **215 programs and 30 lifetime
families**. The corrected execution inventory differs only in this test driver.
Actual emitted saved-field and saved-Undefined lifetime C++ preserves real
allocation, field writes, Map set/get/clear and reseeding, with no
Script/context/value or AOT runtime symbols.

**`5f7b5a2f`** separately corrects only the final printed seeded-refusal count:
the named case sets contain **nine ordinary refusals and three carrier refusals**.
Local, child and devbox audits prove the entire driver AST outside that final
print is identical to the executed version. The report-only devbox sync requires
no build work and checks the corrected counts. All twelve final code/test hashes
match local sources, committed HEAD and final devbox sources; the first-full,
corrected-execution and report-only snapshots are retained separately.

The fresh full corpus gate remains Bootstrap **19/574**, p5 **39/4754**, Phaser
**45/7725** in both modes, zero pruned; exact Data remains **0/7 browser/CommonJS,
0/8 AMD**. These local proofs do not yet improve full-bundle admission.

**Next boundary: entry arithmetic over independently proved published results.**
All **twelve continuation sources** agree on Node/interpreter observations and
both admission modes: **four reach 5/5, eight remain unowned 0/5**. The exact
original eight-call expression adds three Number-returning method calls and
returns trace=3, but fails at
`` unsupported provider behavior through `ctjs.binary` ``.
The eight-call repair retains all three calls and removes only their addition;
it reaches 5/5, trace=1. Saved-result addition, Number-plus-literal and an added
result used as the next method's key also refuse. Preserve actual operands,
source order and all future method input categories when proving arithmetic;
String concatenation and object coercion need their own evidence.
`capturedMapParameters()` needs to trace expressions over its completed-result
categories. Keep those categories separate from `primitive()`/`truth()` values;
an unknown Number must never become a fabricated constant that selects a branch.

The exact zero-size read after clear is a separate boundary: an eight-call
Number-key witness remains 0/5, while its literal-zero repair preserves the
actual size read and reaches 5/5. Existing size facts are lower bounds. The
seven-call String leaf-field source also remains 0/5; its numeric repair is 5/5.
Full native Bootstrap and direct browser API integration remain unfinished.

Evidence: `/tmp/ctcompile-map-clear-{build,focused,fixture2,execution,full}.log`,
`/tmp/ctcompile-map-clear-{frozen,next,first}.json`,
`/tmp/ctcompile-map-clear-{corrected-evidence,corrected-frozen,final-report-frozen}.json`,
`/tmp/ctcompile-map-clear-{carrier-focused,carrier-ctest,corrected-lit}.log`,
`/tmp/ctcompile-map-clear-{final-report-check,corrected-remote-hashes}.log`,
`/tmp/ctcompile-map-clear-final-{field,lifetime}.cpp`, and
`/tmp/ctcompile-map-clear-next.py`. The temporary continuation runner was corrected
to account for the saved-result source's three additional numeric globals; no
source or observation was changed.

## Definite captured Map absence recovery and BigInt equality, 2026-09-08

Recovered the twelve uncommitted compiler files left by the interrupted
**00:35:04 UTC iteration 19**. The continuation came from **`60b744e1`** and
its 00:21/00:33 synchronization journal entries. The older
`codex-wip-20260907` recovery was already merged; no other branch was changed.
Three agents recovered query tests, execution/lifetimes and escape analysis.

Committed locally: **`f05c3e0b`** proves definite captured Map absence,
**`3825f3cf`** gates its execution and lifetimes, and **`ca219c28`** proves
exact BigInt-pair equality escape origins. No browser/runtime source changed
and nothing was pushed.

The exact seven-call `local_absence_delete_undefined` source advances
**0/5 -> 5/5 native**, trace=1, in both optimization modes. The original local
seven-call and historical nine-call post-delete comparisons now also reach
**5/5**, trace=0. Repeated deletion, saved Undefined across reseeding, disjoint
writes and two nonidentical deleting branches work. Every source allocation,
field write, Map operation and published call remains. All **297 historical
helper rows** and **31 previous continuation sources** preserve bytes.

Absence is separate from uncertain membership. Exact deletion establishes it;
possibly aliasing writes invalidate it; branch joins require it on both arms.
Saved reads keep their read-time values. Deletion also preserves prior payload
facts conditional on presence, needed by the nondeleting arm of a later join.
Scalar/flag, Map receiver, method callee and object uses independently check
source dominance. Reports cannot authorize a proof. No new runtime carrier or
Script dependency was added.

Initial devbox access required `start`/`allow-ip`. The first build caught a
const op-wrapper in the new tests. The corrected warning-free **13-step**
build produced **19/20 CTests in 96.87 seconds**: only seeded Map tests failed.
Erasing the old when-present payload caused historical guarded/nullable
regressions; restoring that payload fixes them. Five historical assertion
sites now check exact Undefined or a Number fallback, preserving all **777
fixture/source literal tokens**. The warning-free **six-step** rebuild passes
both affected CTests in **91.56 seconds**. All twenty focused tests have passing
results across these runs; this is not a second full twenty-test run.

Each source/prepared host/owner form passes **30 absence rows**, live/fresh
forgeries, eight scope controls and exhaustive budgets. The **21-program**
execution gate passes Node/interpreter and explicit/deduced GCC/Clang, no-VM
symbol checks, **ten refusal/repair controls**, reruns and **two sanitizer
lifetime families**. Saved callables survive owner/table release, exercise
128 future calls and both branch flags, and release every Map/leaf after
reentry and final release. Native budgets finish at **4160/4866**, testing
**32/30 cutoffs**, with no speculative rollback interval.

BigInt Eq requires two independently proved original BigInt constants; mixed,
opaque, computed and relational BigInt operands remain refused. All eight
escape tests pass: **52 rows, 30 live states, 2280 retention cutoffs**, and a
64-work snapshot. Four new sources measure **16 sites, 32 instances and 26
retained**; fixture precision is **53/81**, all four oracles have zero violations,
and corpus precision remains **0/64, 0/16, 0/20**. This supplies retention evidence,
not a native BigInt carrier or effect proof. The premeasurement's refused
object-to-BigInt cases still differ: Node 4095 versus VM 1023; its first ten
checks agree at 1023. The discrepancy is journaled for the runtime owner.

Stable formatter **22.1.8 passes all 745 files**; changed C++ files also pass
bundled 23, whose complete check retains the same nine unrelated differences.
The full **250-step build has zero warnings**. Initial CTest finishes
**511/517 in 1339.11 seconds**: **371/372 compiler, 140/145 browser**. Lit is
**164/165 in 672.49 seconds** (CTest 672.69); the sole compiler failure is the
published Map test's historical `seeded_deleted` unowned expectation, after its
193 positive programs/lifetime paths executed. That unchanged source now has a
complete owner but stays **0/5 native** at an unsupported nullable Number Map
key. The same five browser failures remain: selectors, frames, element_attrs,
vm_async and early_errors. Exception recovery passes in **1.19 seconds**.

**`4eb2390d`** corrects the historical classification after an independent
**155-case, zero-error** refusal census in both modes. Eleven original sources
now assert complete owners, exact unsupported-carrier diagnostics and preserved
prepared result/formal, receiver, callee and capture operands. Their exact
repairs and stale/fresh forged reports pass. The unchanged ten-call empty-String
deletion source reaches **6/6 native**, trace=2, using the existing nullable
String key and owning String payload types. Its focused explicit/deduced
GCC/Clang execution and no-VM checks pass. Independent future calls distinguish
Undefined, empty and distinct String keys (observation 255); three blind controls
differ. All **328 historical source/refusal rows plus 33 metadata rows** preserve
bytes. Compiler production is unchanged by this test correction.

The complete corrected lit rerun passes **165/165 in 709.94 seconds** (CTest
710.00 seconds, total 710.01). All **372 compiler tests have passing results
across the first full run and corrected rerun**; this is not a second 517-test run.
The published Map gate passes **194 programs and 27 lifetime families**. All twelve
corrected code/test hashes match local sources, committed HEAD and final devbox
sources; the first-full inventory is retained separately. Actual emitted C++
preserves allocation, field write, Map set/delete/get, the saved owning read and
live comparison without Script, context/value or AOT runtime symbols.

**Next boundary: captured `Map.clear()` admission, then whole-Map absence.**
The existing host method whitelist/arity rejects even a saved-object identity
read across clear, while NativeMap and EmitC already have standard clear
support. Fresh get-after-clear additionally needs proof for arbitrary keys,
subsequent possibly aliasing writes and branch joins. All **29 continuation
sources** agree across Node and the interpreter and both admission modes:
**five are 5/5 native; 24 stay unowned 0/5**, preserving prepared calls. Both
original clear sources have seven calls/trace=1 and stay 0/5; the exact clear-to-delete repair is 5/5. All twenty additional clear
variants also refuse, including saved fields, fluent aliases, repeated clear,
unseen keys and surviving branches. Entry numeric addition (eight calls,
trace=3) and String fields (seven calls, trace=2) still refuse. Fresh full corpus
measurements remain Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725** in
both modes with zero pruned; exact Data **0/7 browser/CommonJS, 0/8 AMD**.
Full native Bootstrap and direct browser API integration remain unfinished.

Evidence: `/tmp/ctcompile-absence-recovery-{build,build2,build3,focused,focused3,execution,full}.log`,
`/tmp/ctcompile-absence-carrier-focused.log`, `/tmp/ctcompile-absence-corrected-lit.log`,
`/tmp/ctcompile-absence-corrected-{frozen,evidence}.json`, the separate first-full
inventory, and `/tmp/ctcompile-map-absence-recovery-audit/`.

## Strict fresh object comparisons and primitive Add/Concat, 2026-09-08

Committed locally on `ctcompile-v1`: **`2593acd7`** recognizes comparison-only
fresh object identities, **`316818b2`** gates distinct/saved execution and
lifetimes, and **`267545cd`** proves primitive Add/Concat escape origins.
This resumes the exact continuation in **`bf2fd02e`** and the **22:48:00
synchronization journal**. The starting tree/index was clean; interrupted
`codex-wip-20260907` was already recovered and merged. Three agents handled
identity tests, execution/lifetimes and escape proofs. No browser/runtime
source changed, no history was rewritten and nothing was pushed.

The two unchanged comparison-only sources advance **0/5 -> 5/5 native** in
both modes: local **six calls/trace=0**, historical **eight calls/trace=0**.
Their exact saved-object repairs remain **5/5**, with **six/eight calls** and
**trace=1**. Node, the interpreter and standalone explicit/deduced GCC/Clang
agree. The emitted comparisons consume the actual saved Map read and fresh
allocation, preserving all two/three distinct objects and historical numeric
field writes. Comparing operands never joins their schemas or runtime identities.
All **295 historical helper rows**, all **297 current rows** and the six exact
prior continuation sources preserve their bytes.

New candidate families require a complete strict-comparison use census.
Closed calls, immutable captures and supported SCF forwarding retain their
existing independent source proofs; unknown producers and outgoing ownership
edges refuse. A field-bearing comparison family separately checks the live
ordinary-property environment, exact Map method spelling/receiver/arity,
constructor source, known direct calls and every operand's source dominance.
The optional freshly checked global owner permits only its ordinary source
roots. Host/native report annotations supply no such authority. Unknown effects,
coercing observations, dynamic/prototype/accessor fields and untracked region
yields refuse. Type/field admission remains separate. No new runtime carrier
or platform implementation was introduced.

The focused gate passes **15/15 CTests in 18.85 seconds**, including **36 identity
rows**, **37 live/fresh mutation states**, all eight escape tests and type oracles.
The four exact native programs pass **four sanitizer lifetime families** across
128 future calls, owner/table release, Map mutation, reentry and final Map/leaf
release. Test observers independently retain every fresh leaf, check pairwise
distinct addresses and numeric fields after Map release, then release the leaves.
Both post-delete sources stay unowned **0/5**, retaining all **seven/nine calls**;
exact repairs and stale/forged reports pass. Native budget sweeps finish at
**3790/4542**, testing **32/29 cutoffs**, with no speculative rollback interval.

Add/Concat require both original operands to be independently primitive and
non-BigInt. This proves whole-frame retention only; it establishes no concrete
Number/String tag, constant/key, normal-completion or no-throw contract.
Each of seven dynamic binary kinds passes **84 rows, 49 live states and 4127
retention cutoffs**; Eq/Lt/Le/Gt/Ge each pass **84/34/3527**, with the existing
**64-work** snapshot. Four escape oracles report zero violations; fixture
precision is **52/78**, adding coverage to 49/72. Seven new source functions
measure **28 sites, 56 instances and 44 retained**; historical sources/counts
remain unchanged. Corpus precision stays **0/64, 0/16, 0/20**, p5 partial=1.
The independent Node/VM semantic probe agrees at **trace=4095**.

The initial unit gate passed but fixture claims came from an executable omitted
from the target list. Relinking it restored the source oracle without changing
sources or expectations. Independent review found unsupported region yields and
unrelated malformed field operands missing from the new census; both were fixed
and regression-tested before committing. Formatter **22.1.8 passes all 745 files**;
`tools/format.sh --check` with bundled development 23 retains the nine existing
unrelated differences. All **thirteen code/test hashes** match committed HEAD,
the corrected frozen input and final devbox sources.
The full **245-step generated build passes with zero warnings**. Initial CTest
finishes **511/517 in 1313.13 seconds**: **371/372 compiler** and **140/145 browser**
tests pass. Lit is **164/165 in 658.70 seconds** (CTest **658.90 seconds**); its
published Map test passes all **172 programs and 25 lifetime families**. The sole
compiler failure is an existing diagnostic assertion: blanket clearing of
`ctnative.object_reason` erased the earlier closure argument census's specific
refusal. **`8e603ce5`** clears only this new comparison census's reason prefix.
The original source/test expectations remain unchanged. Its warning-free four-step
rebuild, type CTest (**0.04 seconds**) and exact diagnostic lit (**0.10 seconds**)
pass. The complete corrected lit rerun passes **165/165 in 658.71 seconds**
(CTest **658.78 seconds**, command **658.79 seconds**). All **372 compiler tests
have passing results across the original full run and corrected rerun**; a second
complete 517-test run was not performed. The five established browser failures
remain `selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.
ExceptionRecovery passes in **1.18 seconds**. Fresh native corpus counts remain
Bootstrap **19/574**, p5 **39/4754** and Phaser **45/7725** in both modes, zero
pruned; exact Data remains **0/7 browser/CommonJS and 0/8 AMD**. Actual emitted
C++ retains three fresh leaves, three numeric field writes, the saved owning Map
read and live strict comparison without Script/VM symbols.

**Next boundary: definite absence for object-valued captured Map reads.** All
**31 unchanged probes** agree across Node and the interpreter and have identical
admission in both modes: **13 reach 5/5; 18 remain unowned 0/5**. The exact
`local_absence_delete_undefined` source sets an object, deletes that same key,
then compares the fresh `get` result with `void 0`: **five functions, seven calls,
trace=1, 0/5 native**. Its full source and hash are recorded in
[bootstrap-provider-next.md](bootstrap-provider-next.md). Unseeded six-call and
numeric-payload seven-call absence controls **already reach 5/5**. Preserve these
positive controls. The historical seven/nine-call object comparisons remain
unowned 0/5; the saved-before-delete repair remains 5/5.

In `HostContract/CapturedMapBody.cpp`, `present=false` covers possible absence
as well as absence and cannot authorize Undefined. Track known absence separately
across exact-key deletion, potentially aliasing writes and branch joins; saved
earlier reads retain their own values. Repeated deletion, saved Undefined across
reseeding, known disjoint writes and possible formal-key aliases remain refused.
Exact reseeding already reaches 5/5. `clear()` is a separate unsupported host
method: even its saved-identity control refuses, while replacing it with exact
`delete()` reaches 5/5.

The temporary runner initially assumed source and prepared call counts matched.
LiftToSCF merges identical branch blocks in four probes, reducing eight raw calls
to seven prepared calls. The completed runner preserves every source hash and
records both counts; refusals preserve all prepared calls. These collapsed
branches cannot validate a surviving two-arm absence join. Add a nonidentical
safe branch-positive witness in the next slice; one-arm deletion and conditional
reseeding remain refused for both Boolean startup values. Entry numeric addition,
String/object carriers, exact Bootstrap Data, full native Bootstrap initialization
and direct browser API integration remain unfinished. No full-bundle coverage
gain is claimed.

Evidence: `/tmp/ctcompile-comparison-identity-{build4,focused2,execution3,full,full-detail,lit-rerun,lit-detail,next-final}.log`,
`-evidence.json`, `-next.json`, `-final-hashes.json`, `-final-remote-hashes.log`,
`-format22-corrected.log`, `-final-distinct.cpp` and `-final-lifetime.cpp`.

## Initialized own-field results and arithmetic escape gates, 2026-09-08

Committed locally on `ctcompile-v1`: **`aac9fd27`** adds live per-read field
initialization, **`56569199`** adds primitive arithmetic escape origins, and
**`507fe153`** gates raw field results and saved lifetimes. This resumes the
exact next boundary in **`e385f885`** and the **21:32:43 synchronization
journal**. The starting tree/index was clean; `codex-wip-20260907` was already
recovered, gated and merged. Three agents handled inference regressions,
execution/lifetimes and escape proofs. No browser/runtime files changed,
no history was rewritten and nothing was pushed.

All **seven unchanged raw field-return sources advance 0/5 -> 5/5 native** in
both modes: direct, Map.get, guarded get, saved overwrite, saved delete, saved
alias write and the future-argument lifetime program. Their source call counts
remain **5/6/6/7/7/6/8** and traces **1/1/1/1/1/2/2**, agreeing across Node,
the interpreter and standalone explicit/deduced GCC/Clang, without VM symbols.
All **295 historical helper source rows** and sixteen prior boundary probes
preserve their bytes. The raw saved setter passes ASan/UBSan, stack lifetime
and leak checks across 128 future numeric calls, owner/table release, Map
replacement/deletion, reentry and final Map/leaf release. Both independently
retained leaves and later alias field writes are observed.

The new bounded query lives in `NativeObject/Fields`. Standard Map recognition
remains an upstream prerequisite; schema groups, Map presence/type annotations
and host/provider reports supply no allocation or initialization authority.
Live same-function allocation origins follow exact-instance/key Map writes and
reads. Saved aliases survive Map mutation. Branches intersect origin and field
facts; cross-call and loop-carried origins refuse. Unknown effects permanently
invalidate that path's field environment, including for later allocations.
Live constructor/method keys, receiver/arity, source dominance, prototype and
accessor checks reject stale annotations. Imported frame/root bookkeeping and
machine constants have no source property effects and are handled explicitly.

`fieldIsAssignedBefore` consumes fresh cached per-read facts for owning
identities; existing direct closed-object dominance remains. Inference removes
only implicit absence, preserving **every explicit stored type**, even an
Undefined or Boolean on another allocation in the same schema. A pending value
join stays pending until its stores are inferred. Existing field emission
already converts nullable storage into the proved scalar; no new runtime
carrier or browser implementation was needed.

Final focused gate: **14/14 CTests in 17.08 seconds**, including type inference,
all eight escape analysis/claims tests and four type oracles, all with zero
soundness violations. Inference checks **37 table rows**, live/fresh mutations,
cross-scope sources, unknown-before-allocation effects, and exhaustive budgets
**89/89/30/112**. Seven-program execution repeats pass with two full-schema
carrier refusals and exact repairs, three forged-report families and prepared
reruns. Native budgets finish at **3464/3957/4555**, checking **31/33/29 cutoffs**,
with no natural speculative rollback interval.

The escape increment accepts Sub/Mul/Div/Mod/Pow only from two independently
proved primitive non-BigInt original operands. Each kind passes **80 rows,
49 live states and 3941 retention cutoffs**; Eq/Lt/Le/Gt/Ge each pass **80 rows,
34 states and 3425 cutoffs**, plus the exact **64-work** wide snapshot. Four
escape oracles report zero violations. New sources measure **20 sites,
40 instances, 32 retained**; historical source bytes/counts are unchanged.
Fixture precision is **49/72**, adding coverage to 47/68; corpus precision stays
**0/64, 0/16, 0/20** (p5 partial=1). Primitive conversion guards may throw an
unrelated RangeError: this is whole-frame retention evidence, not a no-throw,
normal-completion or native effect contract.

The initial build exposed LLVM container API use and four missing dependent
`template` keywords in the escape tests; both were corrected. Independent review
caught continuing entry intersection work after budget exhaustion and unknown
effects before fresh allocations. Both were fixed before native commits. Sticky
invalidation first conservatively refused imported frame/machine constants;
explicit bookkeeping handling restored all seven exact execution sources. The
source programs and the unknown-effect refusal rule were preserved.

Formatter **22.1.8 passes all 745 files**; `tools/format.sh --check` with the
bundled development 23 still reports the same nine unrelated existing diffs.
All **fourteen code/test paths** byte-match committed HEAD, frozen gate input
and the final devbox sources. The full **246-step generated build passes with
zero warnings**. CTest finishes **512/517 in 1267.11 seconds**: all **372 compiler
tests** and **140/145 browser tests** pass. Only the five established browser
failures remain: `selectors`, `frames`, `element_attrs`, `vm_async`, `early_errors`.
All **165/165 lit cases pass in 619.29 seconds** (CTest **619.49 seconds**),
including the integrated **170 published programs and 21 lifetime families**.
ExceptionRecovery passes in **1.15 seconds**. All eight escape tests pass again;
all four escape oracles retain zero violations and fixture **49/72**. The four
type corpus oracles also report zero soundness violations.

Fresh native corpus counts remain Bootstrap **19/574**, p5 **39/4754** and
Phaser **45/7725** in both modes, with zero pruned. Exact Bootstrap Data remains
**0/7 browser/CommonJS and 0/8 AMD**. Inspected emitted field/lifetime C++ keeps
fresh object allocations, owning saved Map reads, runtime field/Map operations
and scalar return conversion. It contains no Script/VM context or value symbols.
The saved lifetime harness verifies independent retained leaves after the Map's
final release; its weak observers are test instrumentation, not native storage.

**Exact next boundary: comparison-only fresh object identity recognition.**
Fresh six-case measurements preserve original source/calls and Node/interpreter
agreement in both modes. `local_identity_distinct_fresh` (**5 functions,
6 calls, trace=0**) and `historical_object_distinct_identity` (**5/8/0**) have
complete owners but remain **0/5 native**. `prepareNativeObjectIdentities`
skips families with neither Map-key nor Map-payload use; their comparison-only
fresh RHS allocations therefore lack ObjectIdentityType. The exact saved-object
repairs (**5/6/1** and **5/8/1**) remain **5/5**. Extend fresh identity recognition
with a complete independent strict-comparison use census, preserving actual
allocations and scalar field writes. Never merge comparison operands into one
runtime identity or infer object origin from a schema family.

Post-delete reads are a separate host boundary: the unchanged local **5/7/0**
and historical **5/9/0** controls remain unowned **0/5**, with every original
call preserved. `present=false` currently includes possible absence, so it
cannot prove Undefined. A later slice needs definite exact-key absence across
mutations and branch joins. Entry numeric addition, String/object carriers,
exact Bootstrap Data, full-bundle native initialization and direct browser API
integration remain unfinished. No full-bundle coverage gain is claimed here.

Evidence: `/tmp/ctcompile-field-presence-{initial,build,build2,build3,focused,
focused-detail,execution,final-focused,final-focused2,final-focused3,next,full}.log`,
`-root-hashes.json`, `-next.json` and `-format22-final.log`. Final measurements
are in `-full-detail.log`, `-postgate.log` and `-evidence.json`; inspected emitted
files are `-final-field.cpp` and `-final-lifetime.cpp`. The temporary `-next.py`
probe names and preserves the six exact next-boundary sources.

## Saved leaf readback and relational escape gates, 2026-09-08

Committed locally on `ctcompile-v1`: **`5599ae86`** proves method-local
object readback and **`b98efac0`** proves primitive relational escape origins.
**`4d616b79`** adds the seventeen-program execution and saved-lifetime gates.
This resumes the exact next boundary in **`5d2d843a`** and the **20:26:24
synchronization journal**. The initial tree/index was clean; interrupted
`codex-wip-20260907` was already recovered, gated and merged. Three agents
handled host tests, native execution/lifetimes and escape proofs. No browser
or runtime source changed. No history was rewritten and nothing was pushed.
The unmerged September 4 lens witnesses concern the closure-depth defect
already fixed separately in `6520fcf2`; its code and regression fixture remain
in the current tree.

The historical **eight-call saved-identity source advances 0/5 -> 5/5 native**
in both modes with Node/interpreter/native trace=1. A local Map.get now retains
its independently proved allocation origin. Map overwrite/deletion changes
membership but cannot retarget that saved alias. Fixed scalar own-field reads
require definite local initialization; writes through either alias update the
same allocation's field facts. Both branch arms must preserve those facts.
Every object value must dominate its consumer, including live malformed-source
queries. Exact field-read operations join the host/environment/owner census.
Object-valued public arguments/results, unknown incoming object origins,
accessors, dynamic fields, String fields and retaining object graphs still refuse.

The standalone gate exposed a real emission defect: present object Map.get
returned the finite payload wrapper into a shared object pointer. The new
`map_get_present_identity` copies the owning pointer only when independently
proved presence and ObjectIdentity result type authorize it. No implicit
conversion, collector, VM context or value model was introduced.

The corrected focused gate passes **12/12 CTests in 83.03 seconds**. Host tests
add **26 rows per raw/prepared form**, with exhaustive budget cutoffs
**3759/4368/3840/4468**; owner tests add **13 rows per form**, cutoffs
**8276/9115/8062/8920**. Exact source operation vectors, stale/fresh forged
facts, and two cross-branch invalid-SSA mutations per proof/form pass.

The independent escape increment permits Lt/Le/Gt/Ge only from two separately
proved primitive non-BigInt original origins. Eq and all four relational kinds
each pass **75 rows, 34 live states, 3300 retention cutoffs** and the wide
snapshot's exact **64 work units**. All eight escape CTests pass; all four
oracles report zero violations. Fixture precision is **47/68**, versus 44/64:
one historical child gains a proof and new coverage adds two proved/four
observed. New sources measure **20 sites, 40 instances, 32 retained**; old
loose-equality sources retain **24/44/33**. Corpus precision stays
**0/64, 0/16, 0/20**, with p5 partial=1. The initial 11/12 run failed only
five new handler rows expecting the wrong refusal category; the source was
already safely refused. Relational coercion can hit a recursion guard; this
whole-frame retention proof provides no normal-completion/no-throw contract.

The complete new execution gate passes **17 native programs**, all **5/5** in
both modes and explicit/deduced GCC/Clang, with no VM symbols. It checks live
Map/field operations, **nine complete-owner native refusals**, **eleven host
refusal/repair families**, fresh/stale markers and prepared reruns. Budget
sweeps finish at **4439/4368/5045**, with **30/31/31 cutoffs** and no natural
speculative rollback interval. All **257 historical helper source rows** and
all sixteen previous boundary sources retain their bytes. The saved setter
and size callables pass ASan/UBSan, stack lifetime and leak checks across 128
future calls with varying numeric values, caller-key mutation, overwrite/delete,
owner/table release, independent reentry and final Map release. Weak witnesses
check both fresh leaves die at each call; an independently retained original
leaf survives to its own last release and contains the later scalar write.

The final **244-step generated devbox build passes warning-free**. Full CTest
is **512/517 in 1257.06 seconds**: all **372 compiler tests** and **140/145
browser tests** pass. Only the five established browser failures remain:
`selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.
All **165/165 lit cases pass in 606.48 seconds** (CTest **606.54 seconds**),
including **163 published programs and twenty lifetime families**.
ExceptionRecovery passes in **1.17 seconds**. All eight escape CTests pass
again; four source-oracle runs retain zero violations and fixture **47/68**.

Fresh native corpus counts remain Bootstrap **19/574**, p5 **39/4754** and
Phaser **45/7725** in both modes, with zero pruned. Exact Bootstrap Data remains
**0/7 browser/CommonJS and 0/8 AMD**. Formatter **22.1.8** passes all **745
files**; the bundled 23-development formatter reports existing differences in
nine unrelated files, which were not edited. All **seventeen code/test paths**
byte-match committed HEAD, frozen gate input and final devbox sources. Actual
emitted saved-identity and lifetime C++ retain fresh allocations, owning read
copies, runtime Map/field operations and strict comparisons, without Script/VM
context or value symbols.

**Exact next boundary: independently proved own-field presence in native type
inference.** The unchanged direct/get/guarded/saved field-return sources now
have complete ownership but remain **0/5 native**: the identity-field schema
always starts with Undefined, so a Number field result is nullable and cannot
be the numeric global `trace`. `TypeInference.cpp` deliberately seeds every
`nativeObjectFieldGroup` read with `absentType`; a store on another allocation
must never remove that seed. Introduce a separate live per-read origin and
initialization proof before narrowing it. Reuse `fieldIsAssignedBefore` for
exact direct receivers and the existing `NativeObject/Fields` and
`NativeMap/Presence` analysis seams for Map-loaded aliases. Keep the schema's
full value-type join; only remove implicit absence when this read proves it.
An explicit Undefined write must remain. `NativeObject/ValueFlow` groups
schemas, never runtime instances. Initially refuse cross-call/loop-carried
origins and retain conservative facts on any proof-budget exhaustion. Existing
field emission already converts nullable storage into the inferred scalar.
Explicit numeric field comparisons already execute natively; the exact raw
field-return controls remain intact.
Comparison-only fresh objects (including the historical eight-call distinct
source) separately remain owner-complete **0/5**. A fresh post-delete read
(the historical nine-call control) remains unowned **0/5**. The repeated-key
trace=3 source also needs the entry's numeric addition proof. Exact Bootstrap
Data, full-bundle native initialization and direct browser API integration are
still unfinished; this slice makes no full-bundle coverage claim.

Evidence: `/tmp/ctcompile-leaf-readback-{initial,build,build2,build3,focused,
focused2,focused2-detail,probe,probe2,execution,full,full-detail,postgate}.log`,
the original sixteen
sources in `/tmp/ctcompile-leaf-object-next.json`, and
`/tmp/ctcompile-leaf-readback-{candidates,boundary,root-hashes,evidence}.json`.
The inspected emitted files are `-final-identity.cpp` and `-final-lifetime.cpp`.

## Method-local leaf object ownership and equality gates, 2026-09-08

Saved locally on `ctcompile-v1`: **`e9f8e33c`**, method-local leaf object
ownership in published Maps, and **`5e2cb6b2`**, primitive loose-equality escape
origins; **`2efcbbe2`** adds published leaf execution and lifetime gates. This
resumes the exact seven-call object boundary in **`e533a865`**
and the **18:56:37 synchronization journal**. The initial tree/index was clean;
`codex-wip-20260907` was already recovered, gated and merged. Three agents
handled independent host tests, execution/lifetimes and escape proofs. No
browser/runtime source changed, history was rewritten or push performed.

The exact seven-call `{}` and `{value: 1}` setter programs advance **0/5 ->
5/5 native** in both modes with Node/interpreter/native **trace=2**. The old
four-function empty-object payload refusal now admits **4/4**, trace=1, with
its source unchanged. All three pass standalone GCC/Clang and the no-VM-symbol
check. Every invocation allocates its actual leaf; source keys, field writes,
Map mutations, calls and numeric results remain runtime operations.

The host proof independently checks fresh method-local objects and ordinary
fixed fields containing Number, Boolean, Null or Undefined. The only longer
lived owner is the captured Map. Object keys, fields containing objects,
object-valued public arguments/results, dynamic/prototype/accessor operations
and object reads remain outside this proof. Before proving any invocation,
the complete sibling census disables the primitive-only unknown-read guarantee
if a sibling allocates an object. A definite local primitive write can still
prove its own read. Exact allocation/write records reach the environment and
owner censuses; provider entry tokens supply no authority. Native preparation
reruns the existing object identity/field proof after Map recognition and
before its final live owner query. No emission code or runtime carrier changed.

All four host/owner CTests pass in the combined focused gate. New host tests
have **25 rows per raw/prepared form**, with exhaustive cutoffs
**3227/3460/3298/3531**. Owner families have exhaustive cutoffs
**7730/7969/7506/7745**, exact operation vectors and fresh/stale source mutations.
The initial integration rebuild caught LLVM's default SmallVector size limit;
its richer live callable record now uses explicit zero inline storage.

The escape increment permits Eq only with two independently proved primitive
non-BigInt original origins. **70 rows, 34 live states, 3017 retention cutoffs**
and a wide snapshot's exact **64 work units** pass. The initial focused run is
**11/12 in 71.88 seconds**: one new source used global `undefined`, which
correctly withheld confinement. That source is preserved as a refusal; a
separate `void 0` repair passes. The corrected **2/2 CTest rerun in 0.26 seconds**
checks **24 sites, 44 instances, 33 retained**, zero violations and fixture
precision **44/64**, adding coverage to **42/58**. All eight escape CTests
pass across those two runs; corpus precision remains **0/64, 0/16, 0/20**.
A source audit qualifies prior Neg/Plus evidence: the recursion guard can
throw an unrelated RangeError. This whole-frame retention proof supplies no
normal-completion or no-throw/effect contract to future native consumers.

Commit **`2efcbbe2`** gates **eight new source programs** with
**7/7/7/7/9/7/7/5 calls** and **2/2/2/2/1/2/2/1 traces**, plus the unchanged
four-call/four-function historical object payload. All nine pass both modes,
explicit/deduced GCC/Clang and the no-VM-symbol gate. Six independent future
object observers check exact identities and scalar fields. Saved numeric
size/set/erase callables survive owner/table release, caller-key mutation,
independent reentry and 128 overwrite/delete cycles. Weak witnesses prove
reclamation after overwrite, deletion and final Map/callable release; an
independently retained leaf survives until its own last owner releases it.
The saved family passes ASan/UBSan and leak checks.

All **thirteen new refusal/repair families**, positive fresh/stale forgeries,
prepared reruns and the historical mixed Object/String carrier control pass.
Three budget sweeps finish at **8550/10134/19225**, checking **32/33/31 cutoffs**
with no natural speculative rollback interval. All 236 historical helper
source rows preserve their bytes. The original saved/distinct/deleted identity
refusals preserve their exact sources, observations and 8/8/9 calls.

The first lifetime harness exited 93 because an observer `weak.lock()`
temporary remained alive through a later deletion/expiry check in the same
full expression. The corrected harness ends each read first, retaining every
assertion. No compiler or source fix was needed. The corrected sync then lost
SSH; `server.sh start` and `allow-ip` restored access before the complete focused
execution gate passed. The initial failure and interrupted sync logs remain.

The final **244-step generated devbox build passes warning-free**. CTest is
**512/517 in 1199.12 seconds**, with all **372 compiler tests** and **140/145
browser tests** passing. Only the five established browser failures remain:
`selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.
All **165/165 lit cases pass in 551.23 seconds** (CTest **551.43 seconds**),
including **146 published programs and nineteen lifetime families**.
ExceptionRecovery passes in **1.18 seconds**. All eight escape CTests pass
in this full run, with zero oracle violations and fixture **44/64**.

Native corpus counts remain Bootstrap **19/574**, p5 **39/4754** and Phaser
**45/7725** in both modes, with zero pruned. Exact Bootstrap Data remains
**0/7 browser/CommonJS and 0/8 AMD**. This increment advances the isolated
published-object boundary; it does not increase full-bundle coverage.
Formatter **22.1.8** passes all **745 files**. All nineteen code/test paths
byte-match committed HEAD, frozen gate input and final devbox sources. The
actual emitted field and lifetime programs contain fresh owning leaf
allocations, runtime Map mutations and no Script/VM context or value symbols.

**Exact next boundary: method-local object readback and identity.** The
unchanged eight-call saved-read source stores `{value: 1}`, reads it back,
overwrites/deletes the Map entry and compares the saved value with its original
object. Node/interpreter give trace=1, but both modes remain unowned **0/5**.
A distinct equal-field comparison is trace=0 with eight calls; a fresh read
after deletion is trace=0 with nine calls. These require independently proved
object result origins, presence and identity without authorizing object-valued
public arguments/returns.

A final sixteen-case devbox probe isolates smaller steps. All cases have five
functions, agree in Node/interpreter, preserve every source call and remain
unowned **0/5** in both modes. Raw/prepared host analysis gives the same
`property call lacks a current source getter proof` refusal. Direct own-field
read is **five calls, trace=1**. A saved same-key Map.get identity comparison,
a get/field read and its strict-identity guarded variant are each **six calls,
trace=1**. Two Map-stored objects compare distinct in **seven calls, trace=0**;
a comparison-only fresh object is **six calls, trace=0** and separately lacks
native object-family admission. Saved reads after replacement/deletion stay
trace=1; a later scalar write through the original alias gives trace=2;
three future calls over repeated/distinct keys give trace=3. These are
measured refusals, not native execution gains. Resume the exact historical
8/8/9-call controls with these smaller source proofs, preserving saved aliases
independently of the current Map entry. Full sources, host reports and both
native modes are in `/tmp/ctcompile-leaf-object-next.json`; the source audit
is in `-next-proposals.json`.

The old nested leaf-writing sibling now has complete ownership but remains
**0/6 native** at its mixed Object/String Map carrier. String fields, the
original eleven-call object/String witness, exact Bootstrap Data, browser API
integration, general exports and native throwing-call admission remain unfinished.

Evidence: `/tmp/ctcompile-leaf-object-{compile,host,host2,native-prep,focused,
escape-rerun,smoke,execution,execution2,execution3,full}.log`, `-boundary.json`, `-saved.cpp`,
`-root-hashes.json`, `-snapshot.txt`, `-format-final.log`, `-full-detail.log`,
`-evidence.json`, `-postgate.log`, `-final-hashes.json`, `-final-field.cpp` and
`-final-lifetime.cpp`.

## Nested published Map results and arithmetic unary checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`d7148fcf`**, per-invocation results before
the complete method census; **`1392478f`**, arithmetic unary escape origins;
and **`a1b11e80`**, nested-result execution and saved lifetimes. This resumes
the exact same-setter dependency in **`a8da7c27`** and the **17:53:27
synchronization journal**. The initial tree/index was clean. The interrupted
`codex-wip-20260907` is already an ancestor, recovered and gated in `5307abf`.
Three agents handled independent host tests, published execution controls and
escape proofs. No browser/runtime source changed, history rewrite or push.

The exact **fifteen-call**, trace=2 `set(set(get(false)))` source advances
**0/6 -> 6/6 native** in both modes. Every invocation first proves its own
arguments and complete body from independent primitive categories, starting
with unknown Map contents. Only completed, source-ordered result edges can
supply another invocation. A second mandatory proof joins **all** actuals and
checks every sibling body before publishing the owning family. Scratch body
facts never leak into the final plan. Cycles, forward references, foreign or
unknown results, bad later actuals and unsafe siblings remain refusals.
This never substitutes startup observations for future input categories.
Emission code and owning nullable String carriers are unchanged.

The focused gate passes **12/12 CTests in 61.47 seconds**. Host tests add
**21 rows per raw/prepared form**, four exhaustive budget families
**2211/3274/2264/3362**, and eight live refused states per budget fixture.
Four owner families per form cover all actual orderings; four exhaustive
cutoffs are **5100/7779/4957/7572**, with seven live refused states per family.
The fresh/stale same-tag forward-edge control separates source ordering from
cycles and type mismatches. Two formerly refused but acyclic old unit sources
now complete their generalized family proofs.

All **four new source programs** pass both modes, explicit/deduced GCC/Clang,
Node/interpreter/native identity and the saved-result sanitizer harness. Their
call counts are **15/15/18/15**, traces **2/1/5/0**. The saved nested result owns
its bytes through same-key Boolean overwrite/deletion, caller/result mutation,
owner release, independent reentry and final Map release. All **nine old/new
host-result refusal/repair families** pass fresh/stale annotations and reruns;
five are new. Three budget sweeps complete at **26292/51642/19726**, each with
**31 cutoffs**, and no natural speculative rollback interval. All historical
source programs retain their bytes. The complete **137-program/eighteen-lifetime** driver also passes in the
full lit gate below.

Each arithmetic Neg/Plus/BitNot escape family passes **34 rows, nineteen live
states and 1779 retention cutoffs**, plus a wide snapshot's exact **64 work
units**. Current original origins must independently prove primitive non-BigInt
inputs; opaque/object/array/BigInt inputs still refuse. The result supplies a
Number origin, never a value, index or branch choice. String conversion may
allocate ordinary C++ temporaries; allocation success is not proved. The new
source family measures **sixteen sites, 28 instances and 21 retained**. Four
escape oracles report zero violations. Expanded-fixture precision **42/58**
adds coverage relative to **40/54**; Bootstrap/p5/Phaser remain **0/64, 0/16,
0/20**. Catchable BigInt outcomes still require completion-path evidence.

The full **243-step generated build passes warning-free**. CTest finishes
**512/517 in 1170.30 seconds**: all **372 compiler tests** and **140/145 browser
tests** pass. Only the established `selectors`, `frames`, `element_attrs`,
`vm_async` and `early_errors` failures remain. All **165 lit cases pass in
529.85 seconds**, CTest **530.05 seconds**, including the complete
137-program/eighteen-lifetime driver. Exception recovery passes in **1.19
seconds**. No corrected rerun was needed for this full gate.

Formatter **22.1.8** passes all **745 files**. All twelve code/test paths match
committed HEAD, frozen input and final devbox sources. The inspected saved C++
keeps the getter -> inner setter -> outer setter result operands; each setter
copies its nullable result before Boolean overwrite/deletion. Both inspected
files contain no Script/VM context/value symbols. Native corpus counts remain
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725** in both modes, zero
pruned. Exact Data remains **0/7 browser/CommonJS, 0/8 AMD**. The devbox briefly
lost SSH access; `server.sh start` and `allow-ip` restored it before the combined
gate. No local C++ build ran and no browser/runtime source changed.

**Exact next boundary: published method-local leaf object ownership.**
A smaller **seven-call/five-function** source now isolates the first gap:
`set(key) { const item = {}; state.set(key, item); return state.size; }`,
a sibling `size()`, and calls with `'x', 'x', 'y'`. Node/interpreter give
**trace=2**, but both modes remain unowned and **0/5 native**. A Number-valued
field `{value: 1}` gives the same result. Exact primitive repairs `item = 1`
and `item = 'instance'` retain seven calls and trace=2 and admit **5/5** in both
modes. Keys remain String, all published returns remain numeric; object-valued
host arguments/results are not prerequisites for this first step.

Reuse existing native object identity, closed value flow, Map storage and
fixed scalar fields. The independent host body still rejects object creation
and permits primitive Map values only; provider object tokens cover entry
allocations and cannot authorize future setter-local objects. The original
**eleven-call**, trace=2 `{value: 'instance'}` witness remains unowned **0/6**
and additionally mixes object/String Map payloads and needs owning String
fields. Those are separate from the isolated leaf-owner step.

A following **eight-call** saved-object identity source reports trace=1 in
Node/interpreter, while a distinct equal-field object gives trace=0 (eight
calls) and a fresh read after deletion gives trace=0 (nine calls); all three
remain **0/5** in both modes. These isolate later exact identity and retention.
The earlier small nested String-trace controls now all prove ownership, but
remain **0/4 native** at the separate numeric-global export restriction; adding
a numeric comparison retains its separate host-prefix refusal. The original
dual-nested local conditional retains its callee-identity refusal. Full Bootstrap
Data, browser API integration, general exports and native throwing-call admission
remain unfinished. Full accepted/next sources are in `native-owned-global-maps.md`
and `/tmp/ctcompile-nested-method-object-next.json`.

Evidence: `/tmp/ctcompile-nested-method-compile.log`, `-boundary.json`,
`-baseline.log`, `-focused.log`, `-native.log`, `-format.log`,
`-input-hashes.json`, `-snapshot.txt`, `-full.log`, `-full-detail.log`,
`-evidence.json`, `-postgate.log`, `-final-hashes.json`, `-saved.cpp`,
`-mixed.cpp`, `-object-next.json` and `-next.json`. The first `-host.log`
records the SSH failure before its build; the corrected combined gate passes.

## Finite host Map results and static-binary checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`ed1a833c`**, finite nullable host Map
results; **`ac3d0d43`**, non-BigInt static binary escape origins; and
**`fe867e6f`**, published result execution/lifetime controls. This resumes the
exact acyclic host-result boundary in **`ae8e021a`** and the **16:28:03
synchronization journal**. The initial shared tree/index was clean.
`codex-wip-20260907` is already merged; its recovery was gated in `5307abf`.
Three agents handled independent host tests, published execution controls and
escape analysis. No browser/runtime source changed, history rewrite or push.

The exact **fifteen-call**, trace=1 `get -> set -> size(key)` witness advances
**0/6 -> 6/6 native** in both modes. The separate host body proof now retains
finite primitive payload alternatives through exact writes, possible-alias
writes and conditional joins. Only definite presence supplies read evidence;
saved SSA results survive later overwrite/deletion. The complete method census
still generalizes formal categories for future calls and checks every body.
It uses no native schema or Presence annotation as host proof authority.
The existing owning nullable String carrier suffices; emission code is unchanged.

All **five new source programs** pass both modes, explicit/deduced GCC/Clang,
Node/interpreter/native identity and the saved-result sanitizer harness. Their
call counts are **15/15/19/16/18**, with traces **1/2/4/2/1**. The saved value
reaches a typed nullable `size` callable after Boolean overwrite/deletion,
caller/result mutation, owner release, independent reentry and final Map release.
The initial lifetime harness rewrite incorrectly changed `map->size()` inside
a generated helper; it now transforms only the appended observer. The corrected
saved case passes. Four independent unknown/missing/deleted/aliasing host
refusals preserve their original calls and reject fresh/stale forged reports;
exact repairs restore admitted sources with traces **2/1, 2/1, 1/2, 2/1**.
The nested setter control remains separately refused. Five positive forgery
families and three budget sweeps pass: **18043/18696/18804** work units and
**29/30/32 cutoffs**, with no natural speculative rollback interval.

Four host/owner CTests pass in **37.72 seconds**. The seeded test's corrected
rerun and all eight escape CTests pass together in **21.62 seconds**; seven
local Map lit cases pass in **43.80 seconds**. Host tests add **sixteen rows
per raw/prepared form**, exhaustive cutoffs **4727/4984/4899/5186**, and fresh/
stale mutations. Three owner families independently cover nullable reads,
possible aliases and saved results. Two old expectations now retain literal
truthiness: a false payload takes its Number fallback, while an exact empty
String becomes Null. A paired truthy-Boolean refusal remains. This does not
specialize future formal categories to startup observations.

Each of the seven static binary kinds passes **31 rows, eighteen live states
and 1283 retention cutoffs**, plus a wide snapshot's exact **64 work units**.
Both current operand origins must independently exclude BigInt; opaque and
BigInt inputs still refuse. The result supplies a Number origin, never an
inferred value or branch choice. String conversion can allocate ordinary C++
temporaries, so no absence/success-of-allocation claim is made. The new source
family measures **twelve sites, twenty instances, fifteen retained**. Four
escape oracles report zero violations. Expanded-fixture precision **40/54**
adds coverage relative to **39/51**; Bootstrap/p5/Phaser stay **0/64, 0/16, 0/20**.

The **253-step generated devbox build passes warning-free**. Initial CTest
finishes **511/517 in 1141.97 seconds**: **371 compiler tests** and **140/145
browser tests** pass. The five recorded browser failures remain, plus one old
published-driver expectation: `shortcircuit_nullable` now has complete host
ownership but still refuses its optional Bool/String `scf.if` intermediate.
The driver had already passed all **133 positive programs, seventeen lifetime
families and budget sweeps**. Its other **164 lit cases** passed.

Commit **`077328ae`** corrects only that control, retaining the original
seventeen-call trace=3 source, prepared result edges and exact native refusal.
Its eighteen-call trace=2 repair admits **6/6** in both modes. The complete
refusal tail passes all fresh/stale/rerun controls. The corrected CTest lit gate
passes **1/1 in 526.35 seconds**, including **165/165 lit cases in 526.28 seconds**
and the entire **133-program/seventeen-lifetime** driver. Across the full run
and this corrected rerun, all **372 compiler tests** and **140/145 browser tests**
pass. The five existing browser failures are `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors`; this is not a single 512/517 run.

Formatter **22.1.8** passes all **745 files**. All twelve code/test paths match
committed HEAD, frozen input and final devbox sources. Native corpus counts
remain Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725** in both modes,
zero pruned. Exact Data remains **0/7 browser/CommonJS, 0/8 AMD**. Exception
recovery passes in **1.18 seconds**. The inspected saved-result C++ copies its
nullable value before same-key Boolean overwrite/deletion and passes that
owned result to `size(key)`. Both inspected C++ files contain no Script or VM
context/value symbols. No browser/runtime source changed.

**Exact next boundary: a setter result used by another call to the same setter.**
The **fifteen-call**, trace=2 `set(set(get(false)))` source remains unowned and
**0/6 native** in both modes. `Values.cpp` requires every actual before checking
a method body; one setter actual is its own unpublished result, so the complete
method worklist stalls. Nullable payload storage/results are now proved for
the distinct-method control. A next increment needs independent evidence for
this dependency without treating the first startup call as the entire census.
The eleven-call object-payload source also remains unowned, trace=2 and **0/6**.
The dual-nested local conditional retains its separate callee-identity refusal.
Full Bootstrap Data, object identity/fields, browser API integration, general
exports and native throwing-call admission remain unfinished.

A smaller four-function host-proof control is now measured independently.
`set(key) { state.set(key, key); return state.get(key); }`, a direct Null call,
and `set(set('future'))` use **seven calls**, Node/interpreter String `"future"`,
no owner and **0/4 native**. Removing the outer call gives six calls and complete
ownership; removing the Null call gives an all-String **six/five-call** pair
with the same ownership distinction. Both direct repairs still have **0/4
native** because storing String to `trace` hits the separate numeric-global
export restriction. Moving Null after the nested call does not repair the
census. These isolate host proof only; the fifteen-call acyclic program remains
the admitted native control. Sources and measurements are in
`/tmp/ctcompile-nullable-host-results-next.json`.

Evidence: `/tmp/ctcompile-nullable-host-results-host.log`, `-focused.log`,
`-native.log`, `-corrected.log`, `-boundary.json`, `-format.log`,
`-input-hashes.json`, `-snapshot.txt`, `-full.log`, `-evidence.json`,
`-regate2.log`, `-lit-final.log`, `-lit-final-detail.log`, `-postgate.log`,
`-final-hashes.json`, `-saved.cpp`, `-mixed.cpp` and `-next.json`. The first native
log preserves the lifetime-harness failure; the first full log preserves the
obsolete ownership expectation. Corrected focused and full lit gates pass.
Complete accepted and next sources are in `native-owned-global-maps.md` and the
published driver.

## Finite mixed Map read and total-unary checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`c4d6bf07`**, finite nullable payload facts
for mixed Maps, and **`a539c3fa`**, TypeOf/Void escape origins. This resumes the
exact readback boundary in **`4f5e248d`** and the **15:15:11 synchronization
journal**. The starting tree was clean. `codex-wip-20260907` is an ancestor;
its interrupted recovery was already gated in `5307abf`. Three agents handled
published execution controls, independent local tests and escape analysis.
No browser/runtime source changed. No history was rewritten or push performed.

The exact **fourteen-call**, trace=2 and **nineteen-call**, trace=3 witnesses
advance **0/6 -> 6/6 native** in both modes. Per-instance/key payload facts now
retain finite String/Null/Undefined alternatives through writes and joins.
A present read seeds `Opt<Str>` before monotone inference can widen it to the
whole Boolean/nullable String schema. Conditional/saved writes remain candidates
without a direct literal String set. The existing owning extraction copies
bytes and preserves tags; no new runtime carrier was added. Unknown payloads,
missing presence and unrepresented mixed results still refuse.

All **four new published programs** pass both modes, explicit/deduced GCC/Clang,
Node/interpreter/native identity and the saved-payload sanitizer harness.
The latter preserves a nullable read through same-key Boolean overwrite,
deletion, caller/result mutation, both future flags, independent reentry and
final Map destruction. Three direct missing/deleted/aliasing controls retain
complete host ownership but refuse native admission; their exact repairs
restore the accepted fourteen-call program, **6/6** in both modes. Source traces
are **0/2, 0/2, 3/2** for refusal/repair. Fresh/stale scalar and nullable
forgeries and reruns pass. Budgets **17773/20330/30183/13049** pass
**30/32/31/31 cutoffs**, with no natural speculative rollback interval.

The local gate passes **69 observations and 39 refusals**, including both
layouts, isolated helper emission, fresh/stale CTJS-only proof rederivation
and ASan/UBSan. The first run passed six other lit cases but exposed a separate
callee-identity limit: two nested conditional method lookups merge into an
`scf.if` result. That original source remains a refusal. The positive uses an
initial write plus conditional overwrite, preserving the payload join under
test. The corrected local case passes in **42.63 seconds**. Four host/owner
CTests pass in **27.85 seconds**. Formatter **22.1.8** passes all **745 files**;
the local pinned formatter 23 disagrees on preexisting files. Fifteen code/test
paths match committed HEAD and the frozen gate input.

Eight escape CTests pass in **8.35 seconds**. Each new TypeOf/Void family
passes **twenty rows, twelve live states and 976 retention cutoffs**, plus a
wide snapshot's exact **64 additional work units**. The new source family
measures **eight sites, 28 instances, 21 retained**; prior copy **21/39/23**,
switch **4/10/3** and negation **4/16/12** families stay unchanged. Four oracles
report zero violations. Expanded-fixture precision **39/51** adds coverage
relative to **37/49**; corpus precision stays **0/64, 0/16, 0/20**. TypeOf's
primitive String allocation is not claimed inert; source Void already imports
as Undefined. Coercing unary kinds, loops and native lifetime consumers remain
separate work.

The final **247-step devbox build passes warning-free**. CTest finishes
**512/517 in 1113.08 seconds**: all **372 compiler tests** and **140/145 browser
tests** pass. Only the five recorded browser failures remain: `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors`. All **165 lit cases
pass in 489.13 seconds**, including the complete **128-program published driver
and sixteen lifetime families**. Exception recovery passes in **1.19 seconds**.
Fresh native corpus counts remain Bootstrap **19/574**, p5 **39/4754**, Phaser
**45/7725** in both modes, zero pruned; exact Data remains **0/7 browser/CommonJS,
0/8 AMD**. Four escape oracles again report zero soundness violations.

All fifteen code/test paths match committed HEAD, the frozen input and the
final devbox source. The inspected saved-read C++ copies `nullable_string`
before same-key Boolean overwrite and deletion, then returns the owned value.
Both inspected generated files contain no Script or VM context/value symbols.

**Exact next boundary: a nullable Map read used by another published method.**
The acyclic **fifteen-call** `get -> set -> size(key)` program has Node/interpreter
**trace=1** but **0/6 native** and no host owner proof in both modes.
`CapturedMapBody.cpp::entry_fact` retains only one tag, losing the setter's
finite nullable result before the complete actual/formal worklist reaches
`size(key)`. Preserve those alternatives in the host body's own bounded proof;
native Presence facts cannot stand in for that independent proof. A nested
`set(set(...))` fifteen-call, trace=2 source additionally has a self-dependent
method census and is a separate boundary. The eleven-call object-payload case
still has no owner proof, trace=2 and **0/6**. Full Bootstrap Data, browser
integration, general exports and native throwing-call admission are unfinished.

Evidence: `/tmp/ctcompile-mixed-nullable-full.log`, `-evidence.json`,
`-postgate.log`, `-final-hashes.json`, `-saved.cpp`, `-mixed.cpp`, `-compile.log`,
`-focused.log`, `-local.log`, `-positives.log`, `-controls.log`, `-next.json`,
`-input-hashes.json` and `-snapshot.txt`. The first local failure remains in
`-focused.log`; the corrected local and direct owner/refusal controls pass
separately and in the final complete gate. Complete next sources
are in `native-owned-global-maps.md` and devbox
`/tmp/ctcompile-mixed-nullable-next/`. SSH interrupted after the focused gate
when the home IP rotated; starting the box and refreshing `allow-ip` restored it.

## Nullable Map payload and logical-negation checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`6e150949`**, owning nullable Map payloads,
and **`b3ecab58`**, bounded logical-negation escape producers. This continues
the exact payload boundary in **`1c7985a5`** and the **14:07:41 synchronization
journal**. The starting tree was clean; `codex-wip-20260907` is an ancestor and
its interrupted recovery was already gated in `5307abf`. Three agents handled
published execution/lifetime tests, independent local tests and escape analysis.
No browser source or runtime semantics changed. No history was rewritten or
push performed.

The eleven-call `state.set(key, key)` and twelve-call readback witnesses
advance **0/6 -> 6/6 native** in both modes, retaining Node/interpreter trace=2.
Nullable payloads own `nullable_string`; closed Boolean composition uses
`std::variant<bool, ctnative::nullable_string>`. Both layouts preserve String,
Null, Undefined and empty String. An ordinary nullable read returns Undefined
on a miss; exact mixed reads require independent presence and payload facts.
Saved String reads copy their bytes before overwrite/deletion. Payload-only
helper emission is checked without a nullable-key function in the same module.
Other optional storage, unrepresented temporaries and nullable/mixed snapshots
remain refused. The eighteen-call mixed write admits **6/6**, trace=3;
the fourteen-call payload identity case admits **6/6**, trace=4.

The first published driver passed all **123 positives and fifteen lifetime
families**, then found a deleted-read fixture incorrectly expecting refusal.
That read now compiles and correctly returns Undefined. The corrected
**thirteen-call**, trace=0 positive checks all four input tags independently
from its return tag. All **six payload programs** pass both modes,
explicit/deduced GCC/Clang, Node/interpreter/native identity checks and the
saved-payload sanitizer harness. It keeps Strings alive through caller-buffer
mutation, overwrite/deletion, both future flags, independent reentry and final
Map destruction. Three host refusals and the mixed full-schema read refusal
pass both modes and fresh forged facts. All **124 programs** and **fifteen
lifetime families** pass in the final complete driver, including the
deleted-read fresh/stale forgery controls.

Fresh/stale payload forgeries and budgets **17270/20256/13049** pass
**31/30/31 cutoffs**, with no natural speculative rollback interval. The local
gate passes **63 observations and 34 refusals** under both layouts, including
isolated payload helpers and ASan/UBSan. Four host/owner CTests pass in
**26.44 seconds**; seven targeted lit cases pass in **37.98 seconds**. The
local test now clears its own split output so a removed refusal cannot persist.
Formatter **22.1.8** passes all **745 files**. All **eighteen session code/test
paths** match committed HEAD, frozen gate input and final devbox source.
The full **247-step generated build succeeds without warnings**. Final CTest
passes **512/517 in 1045.44 seconds**: **372/372 compiler** and **140/145 browser**
tests. Only the recorded `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors` failures remain. All **165/165 lit cases pass in 440.08 seconds**;
exception recovery passes in **1.17 seconds**. Log:
`/tmp/ctcompile-nullable-payloads-full.log`.

The independent escape proof adds only total `LogicalNot` as a noncapturing
Boolean origin. It proves no input value or branch liveness. Eight escape
CTests pass in **8.23 seconds**, including **22 rows, eleven live mutation
states, 946 retention cutoffs** and a wide snapshot's exact additional budget.
Four execution oracles report zero violations. The separate negation family
measures **four sites, sixteen instances, twelve retained**; earlier copy
**21/39/23** and switch **4/10/3** families stay unchanged. Expanded-fixture
precision **37/49** adds coverage relative to **36/48**, with no corpus gain:
Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**. Other total unary producers,
loops and native lifetime consumers remain separate work.

Fresh native components remain **Bootstrap 19/574, p5 39/4754, Phaser 45/7725**
in both modes, zero pruned. Exact Data remains **0/7 browser, 0/7 CommonJS,
0/8 AMD**, zero pruned. The payload increment advances the focused published
programs; whole-Bootstrap native coverage is unchanged.

**Exact next boundary: finite nullable payload facts in mixed storage.** Add
one temporary Boolean write/delete to the accepted readback. The resulting
**fourteen-call**, trace=2 source has complete host ownership but remains
**0/6 native** in both modes. The original mixed nineteen-call readback has
the same boundary, trace=3. `Presence.cpp::write` collapses the independently
known String/Null alternatives to `Unknown`, so the read inherits the broad
storage union. Preserve the finite per-instance/key payload evidence and seed
`OptStr` before monotone inference widens it. The existing owning extraction
can return the proved subset; a general mixed optional scalar carrier is not
required for these witnesses. An object payload still has no owner proof,
eleven calls, trace=2 and **0/6**. Full Bootstrap Data, browser integration,
general exports and native throwing-call admission remain unfinished.

Evidence: `/tmp/ctcompile-nullable-payloads-compile.log`, `-focused.log`,
`-native.log`, `-controls.log`, `-boundary.json` and `-full.log`. The first
published-driver failure is retained in `-native.log`; the corrected focused
gate passes in `-controls.log`, and the complete driver passes in the final full
gate. Final counts and source hashes are in `-evidence.json`, `-final-hashes.json`
and `-postgate.log`; frozen input is recorded in `-snapshot.txt` and
`-input-hashes.json`. Inspected `-string.cpp` and `-mixed.cpp` own their payloads
and emit no Script symbol or VM context. Complete next sources are in
`native-owned-global-maps.md` and devbox `/tmp/ctcompile-nullable-payloads-next/`.

## Nullable Map-key and switch-selector checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`5e63d993`**, owning nullable Map keys, and
**`643501db`**, bounded strict switch selectors in escape analysis. This resumes
the exact nullable-key boundary in **`178f65e9`** and the **13:07:05 synchronization
journal**. The tree started clean; the old `codex-wip-20260907` recovery was
already gated in `5307abf` and its branch is an ancestor. Three agents handled
published execution tests, independent local tests and escape selectors while
the root implemented the native/Bootstrap boundary. No browser source or runtime
semantics changed. No history was rewritten or push performed.

Nullable String keys reuse owning `nullable_string`; Boolean composition uses
`std::variant<bool, ctnative::nullable_string>`. Both storage layouts compare
tags before bytes. Null, Undefined, empty String and tag-looking text remain
distinct. Key-only spelling and admission preserve separate payload/snapshot
refusals. A normalized operation can extract a proved String without narrowing
a second use of its original nullable argument. Numeric-only helper emission
is checked independently of other String functions in a module.

The **eleven-call** homogeneous and **eighteen-call** original witnesses advance
**0/6 -> 6/6 native** in both modes, with Node/interpreter **trace=2/3**. The
**thirteen-call** String/Null/Undefined/empty String identity case admits **6/6**
with **trace=4**, versus **trace=2** for normalization. Boolean composition and
the **nineteen-call** second-use case also admit **6/6**. All source calls remain.
All **118 positive programs** pass explicit/deduced GCC/Clang and identity checks;
the **fourteen lifetime families** pass sanitizers. The new saved setter owns its
keys through caller-buffer mutation, deletion/reinsertion, independent reentry
and final Map release. Fifteen nullable identity observers and **46 identity
mutations** include 25 new mutations; nine new structural mutations discriminate.

The first published driver stopped after all positives on a new forged-output
comparison: only input filenames in provenance comments differed. The correction
normalizes those known filenames and preserves coordinates, comments and code.
Focused fresh/stale key/read/write forgeries, both new refusal families and
budgets **17225/29477/20285/12808** pass, with **29/31/31/32** cutoffs and no natural
speculative rollback interval. Four host/owner CTests pass in **26.38 seconds**;
seven targeted lit cases pass in **29.72 seconds**. The local gate passes **59
observations and 28 refusals** under both layouts, plus the isolated numeric
payload test. Formatter **22.1.8** passes all **745 files**. The final warning-free
**247-step generated build succeeds**. CTest passes **512/517 in 1024.81 seconds**:
**372/372 compiler** and **140/145 browser** tests. Only the recorded `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors` failures remain. All
**165/165 lit cases pass in 419.57 seconds**, including the complete published
driver after the provenance correction; exception recovery passes in
**1.17 seconds**. All **nineteen code/test paths** match committed HEAD, frozen
gate input and final devbox source. Inspected nullable-key output copies owned
Strings into tag-aware storage and preserves Null/Undefined, with no Script
symbol or VM context.

Escape contents admit only noncapturing `StrictEq` and `ToBoolean` producers.
Every structural edge remains checked; coercing comparisons/conversions and
unknown roots, contents, returns, keys and copy endpoints still refuse. Eight
escape CTests pass in **8.35 seconds**: **28 new rows, eleven live mutations,
1,079 retention cutoffs** and the wide snapshot's exact additional budget. Four
oracles report zero violations. The historical copy family stays **21 sites,
39 instances, 23 retained**; a separate source-switch family measures **four
sites, ten instances, three retained**. Expanded-fixture precision **36/48**
adds coverage relative to **35/47**; corpus precision remains **0/64, 0/16, 0/20**.
This is not a corpus precision gain. Other primitive selector producers and
loops still need complete proofs.

Fresh native components remain **Bootstrap 19/574, p5 39/4754, Phaser 45/7725**
in both modes, zero pruned. Exact Data remains **0/7 browser, 0/7 CommonJS,
0/8 AMD**, zero pruned. The new key support advances the focused published
Map programs; it does not yet increase whole-Bootstrap native coverage.

**Exact next boundary: nullable payload storage.** Change only the small
accepted setter from `state.set(key, true)` to `state.set(key, key)`. The
**eleven-call**, **trace=2** program retains complete host ownership but stays
**0/6 native** in both modes with `Opt<Str>` keys and payloads. Returning
`state.get(key)` instead of size produces **twelve calls**, the same owner,
trace and refusal. Implement nullable String payload storage before composing
Boolean with it, retaining independent read facts and snapshot refusals. An
object payload `{value: 'instance'}` preserves eleven calls and trace=2 but
has no host owner proof and remains **0/6**; object identity/fields are a
separate Bootstrap obligation. Full native Bootstrap Data, browser integration,
general exports and native throwing-call admission remain unfinished.

Evidence: `/tmp/ctcompile-nullable-keys-focused.log`, `-escape.log`, `-controls.log`,
`-full.log`, `-evidence.json`, `-boundary.json` and `-postgate.log`. The initial
published-driver failure remains in `-native.log`; the complete driver passes
in the final full gate. Frozen input is recorded in
`/tmp/ctcompile-nullable-keys-snapshot.txt`; generated output is saved in
`/tmp/ctcompile-nullable-keys-string.cpp` and `-mixed.cpp`. Complete next sources
are in `native-owned-global-maps.md` and devbox
`/tmp/ctcompile-nullable-keys-next/`.

## Nullable Map methods and opaque-register checkpoint, 2026-09-08

Commits **`6f13212`** (opaque entry transport), **`fa29d49`** (finite host
alternatives) and **`8d80629`** (native nullable methods) resume the threads left
in flight in the **11:17:14 synchronization journal**, abandoned at **11:17:55**.
The starting tree was clean. The old `codex-wip-20260907` recovery was already
gated in `5307abf` and its branch is an ancestor. Three agents handled host
controls, native execution tests and escape analysis while the root implemented
the native/Bootstrap boundary. No history was rewritten, browser or runtime
source changed, or push performed. `7edb8a8` makes primitive-alternative equality
compatible with LLVM's C++17 analysis target without extension warnings.

The host dependency worklist retains finite primitive alternatives through
completed producer results and the entire actual/formal census. It joins all
actuals before admitting String with Null/Undefined; an early Null/Undefined
pair cannot reject a later String. Unknown producers, unsupported mixtures and
unseeded cycles still refuse. Parameter facts widen truthiness within each
category, preserving future branches. Host units pass **82 earlier plus 32
nullable rows**, each in source/prepared form, every **3848/4001** nullable and
**5150/5368** census budget cutoff, exact endpoints and live mutations.

Native callable signatures reuse owning `nullable_string`. An independently
rederived **per-operation** Map-key fact keeps `key || 'missing'` String storage
precise without narrowing the original SSA value or a second unnormalized use.
Extraction copies the selected String before homogeneous storage or mixed-key
wrapping. Number key proofs preserve independently inferred integer widths.
The normalized nullable and ternary programs advance **0/6 -> 6/6 native** in
both modes, retaining all **eighteen source calls** and Node/interpreter
**trace=3**. `void 0` supplies the Undefined literal; bare `undefined` remains
an unproved host global under this contract.

The published gate passes **110 complete programs**, including seven nullable
cases, five new proof refusals and two nullable-key carrier refusals. Both modes,
explicit/deduced GCC/Clang, exact identity observations, fresh/stale key/read/write
forgeries, reruns and budget cutoffs pass. The **thirteen lifetime sanitizer
families** include a saved nullable getter that sees only false during startup,
then both future flags. Its Strings survive overwrite/deletion, caller-buffer
mutation, independent reentry and destruction of both Maps. **Twenty-one** new
source mutations distinguish String, Null, Undefined and empty String behavior.
The three-way getter retains four `if`s plus a live Null/Undefined `?:` selection;
its native identity observer checks both tags. No Script symbols or VM context
appear in the inspected owning String output.

The escape proof transports exact opaque entry `!ctjs.value` identities through
acyclic successor vectors, separately from known local origins. Opaque values
cannot become keys, stored contents, roots, returned values or copy endpoints.
Every entry visit and opaque snapshot spends budget. Eight escape CTests pass,
including **19 rows, eight live mutations, 664 retention cutoffs** and the wide
snapshot control. Four execution oracles report zero violations. The unchanged
fixture improves **34/47 -> 35/47**, retaining **21 sites, 39 instances and 23
retained instances** in the copied-path family. Corpus precision stays **0/64,
0/16, 0/20**. Comparison/Boolean producers for source switches remain the next
bounded escape proof.

Validation: focused CTest **12/12 in 34.43 seconds**, seven final targeted lit
cases **7/7 in 27.64 seconds**, and formatter **22.1.8**, **745 files**. The final
**252-step generated build succeeds without warnings**. Final CTest passes
**512/517 in 969.20 seconds**: **372/372 compiler** and **140/145 browser** tests.
Only the recorded `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors` failures remain. All **165/165 lit cases pass in 375.26 seconds**;
exception recovery passes in **1.14 seconds**. The first full run was **511/517**:
the integer-width and textual-branch lit failures were corrected before this
final gate. All **28 code/test paths** match committed HEAD, frozen input and
the final devbox source. Inspected `/tmp/ctcompile-nullable-string.cpp` retains
owning saved Strings through subsequent writes/deletes and final nullable return,
with no Script symbol or interpreter context.

Fresh native components remain **Bootstrap 19/574, p5 39/4754, Phaser 45/7725**
in both modes, zero pruned. Exact Data remains **0/7 browser, 0/7 CommonJS,
0/8 AMD**. Corpus escape precision stays **0/64, 0/16, 0/20** within the existing
execution/environment limits; p5 retains one partial observation. Evidence:
`/tmp/ctcompile-nullable-full.log`, `/tmp/ctcompile-nullable-evidence.json` and
`/tmp/ctcompile-nullable-postgate.log`. Frozen input is recorded in
`/tmp/ctcompile-nullable-native-snapshot.txt`. Refresh source mtimes when swapping
frozen inputs so Ninja does not reuse objects newer than restored changed sources.

**Exact next boundary: nullable Map-key storage.** The original eighteen-call
program now has complete host ownership, but stays **0/6** in both modes with
**trace=3** and `Opt<Variant<Bool, Str>>` keys. A smaller **eleven-call** witness
isolates `Opt<Str>` keys: complete ownership, **0/6**, **trace=2**. Adding one
temporary Boolean key gives **thirteen calls**, the mixed nullable key schema,
the same trace and refusal. A **thirteen-call** String/Null/Undefined/empty
String witness gives **trace=4**, complete ownership and **0/6**; normalizing
its keys gives **trace=2** and **6/6**. Implement owning tag-aware nullable String
keys in both storage layouts before composing Boolean with them. Null, Undefined
and empty String must stay distinct; do not coerce real null keys through
`string_text` or implicitly admit nullable payloads and mixed snapshots.
Complete sources and measured evidence are in `native-owned-global-maps.md`
and `/tmp/ctcompile-nullable-boundary.json`. Complete Bootstrap Data and general
native Bootstrap initialization remain unfinished.

## Short-circuit Map and executed copy-path checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`58decfe`**, scalar Map short-circuit
alternatives, and **`0f1a1a0`**, executed object-copy path witnesses. This resumes
`shortcircuit_same_tag` from **`b806b26`** and the **09:59:24 synchronization
journal**. The tree started clean; the old CallDirectOp recovery was already
landed in `5307abf`, and its branch is an ancestor. No history was rewritten,
browser source changed or push performed. Three agents supplied independent
native execution/lifetime tests, host proof controls/audit and escape witnesses.

The exact `(has && get) || fallback` getter advances **0/6 -> 6/6 native** in
both modes, preserving all **eighteen calls**, three getter conditionals and
Node/interpreter **`trace=2`**. The semantic proof retains finite primitive
alternatives partitioned by truthiness, refining only the tested SSA value.
Every structural arm's effects remain checked, even with literal predicates.
Unknown alternatives stay unknown. Membership, entry payload tags and saved
scalar alternatives remain independent. A live native write proof rederives
`ctnative.map_write_type`; a wider SCF temporary may supply a proved exact
scalar write without narrowing its Map schema or trusting input annotations.
Bool/String temporaries own `std::variant<bool, std::string>` values; a plain
truthiness visitor and copied extraction preserve String lifetime. Bool/String
signatures, returns, captures, fields and coercions remain refused.

The published gate passes **103 complete programs**, eight new short-circuit
programs, nine new guard/effect/tag refusals and **twelve lifetime sanitizer
families**, in both modes with explicit/deduced GCC/Clang, Node/interpreter,
forged/rerun checks and **24** discriminating source mutations. Present empty
String, false and zero still select the fallback. The long String getter sees
only false during startup; saved native callables later use both flags and keep
owning results through overwrite/deletion, independent reentry and final Map
release. First complete native budgets are **19555/20373/10371/11606**, with
**31 cutoffs each** and no natural speculative rollback interval. Log:
`/tmp/ctcompile-shortcircuit-native.log`.

Host units pass **82 rows each in source/prepared form**, every **3356/3494**
short-circuit, **3031/3169** guarded and **2567/2692** conditional budget cutoff,
exact endpoints and live forged read/key/condition/yield mutations. Local mixed
Maps pass **49 observations and 21 refusals** under both storage layouts,
Node/interpreter, GCC/Clang and ASan/UBSan, including inverted falsy refinement
and a standalone Bool/String temporary. All seven targeted lit cases pass in
**28.51 seconds**. Focused CTest passes **12/12 in 31.25 seconds**. Homebrew
clang-format **22.1.8** passes **744 files**, and whitespace checks pass. Inspected
`/tmp/ctcompile-shortcircuit-string.cpp`: live branches and owning saved Strings
through write/delete, with no Script symbols or interpreter context.

The parallel escape increment changes **no production analysis**. Four source
functions and eight runtime calls cover conditional copy source/target aliases,
replacement/deletion, every switch case/default and saved-child return graphs.
Exact source-coordinate joins check **21 sites, 39 instances and 23 retained
instances**, preserving all earlier families. Four execution oracles report zero
violations. Expanded-fixture precision is **34/47**, versus **29/41** on the
smaller fixture; this adds five proved-confined and six observed-confined sites,
not a corpus precision improvement. Bootstrap/p5/Phaser remain **0/64, 0/16,
0/20**. Raw unknown predicate transport and comparison/Boolean producers for
source switches remain outside the complete contents proof. See
`ctcompile/docs/escape-load-evidence.md` for the next bounded proof.

The full **252-step generated devbox build succeeds**. Final CTest is
**512/517 in 953.39 seconds**: **372/372 compiler** and **140/145 browser** tests.
Only the recorded `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors` failures remain. All **165/165 lit cases** pass (`ctcompile_lit`,
**351.96 seconds**), and exception recovery passes in **1.19 seconds**. All four
execution oracles again report zero violations, with precision **34/47, 0/64,
0/16, 0/20** under their existing environment and execution coverage limits.
Fresh native components remain **Bootstrap 19/574, p5 39/4754, Phaser 45/7725**
in both modes with zero pruned. Exact Data remains **0/7 browser, 0/7 CommonJS,
0/8 AMD**. Complete native Bootstrap initialization remains unfinished.
Evidence: `/tmp/ctcompile-shortcircuit-full.log` and
`/tmp/ctcompile-shortcircuit-evidence.json`.

All **25 changed code/test paths** match committed HEAD, frozen gate input and
final devbox source. The inspected String output retains live branches and
owning saved results through subsequent writes/deletes, without Script symbols
or an interpreter context. Checkpoint docs were committed as `c0741b1`; this
update records the completed full gate. No browser or runtime semantics changed.

**Exact next boundary:** add `return result || null` to the accepted short-circuit
getter, keeping its existing Map writes and both standalone `set(get(flag))`
calls followed by `size()`. Node/interpreter agree on **3**, but both native
modes remain **0/6**, all **eighteen calls retained**, with no host owner proof.
The accepted short-circuit and ternary controls both admit **6/6**, eighteen
source calls, Node/interpreter **2**. Fresh nullable ternary, normalized
`key || 'missing'` consumer and object-payload witnesses each remain **0/6**,
eighteen calls, trace **3**. Directly storing `(has && get) || null` retains
**seventeen calls** with the same trace and refusal. Complete next source is
in `native-owned-global-maps.md` under "Next boundary", and on the devbox at
`/tmp/ctcompile-shortcircuit-next/nullable_or.js`. Measured evidence:
`/tmp/ctcompile-shortcircuit-boundary.json`.

A read-only audit identifies the first proof boundary: the host body reduces
known String/Null alternatives to one optional tag, so `HostContract/Values.cpp`
cannot supply the consuming setter's parameter fact. Carry finite result and
parameter evidence through that dependency worklist without authorizing an
unseeded cycle. Reuse the existing owning `nullable_string` carrier; stored
callable admission and `EmitC/MethodTables.cpp` still lack its signature support.
The original setter additionally stores a real null key, while the normalized
consumer control isolates that obligation. Optional key/storage schemas, object
identity, export ABI and complete native Bootstrap Data remain separate work.

## Guarded Map reads and copied-object checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`677714b`**, guarded saved scalar Map reads
across conditional deletion, and **`72ca88e`**, fixed own-object copy contents
and retention. This resumes `guarded_saved_read` from **`ca99089`** and the
**09:08:55 synchronization journal**. The starting tree was clean. The old
CallDirectOp recovery was already landed in `5307abf` and its branch is an
ancestor; no history was rewritten. Separate agents supplied execution gates,
host unit/audit controls, copy analysis and executed source witnesses. No browser
source changed or push was performed.

The exact guarded ternary advances **0/6 -> 6/6 native** in both optimization
modes, preserving all **eighteen calls** and Node/interpreter **`trace=2`**.
The deletion-free and straight-line fallback controls give **3** and **1** and
remain admitted. Host/native proofs retain payload tags valid whenever present
across deletion joins, independently of definite membership. Only a live `has`
for the same Map/key restores membership. Joins intersect record keys and tags;
unknown contents never become a proved absence. Possible-alias writes still
join payload tags; exact writes replace them. Stale guards, unsupported effects
and incomplete budgets withhold the complete proof. Deleted records do not
inflate size bounds. Every structural branch and runtime call remains.

The published gate passes **95 complete programs** and **eleven lifetime
sanitizer variants**, with both modes, explicit/deduced GCC/Clang, forged/rerun
controls and sixteen new discriminating mutations. Eight new refusal families
cover missing tags, wrong Map/key guards, stale has values, mutation inside an
arm, incompatible tags and literal predicates. The long String getter runs only
false during startup; later saved C++ callables use both flags and own both
selected strings through overwrite/delete, independent reentry and final Map
release. Native budgets first complete at **18690/19232/19232/10792**, checking
**29/31/31/29** cutoffs, with no natural speculative rollback interval.
Log: `/tmp/ctcompile-guard-focused.log`.

Host units pass **44 rows each in source/prepared form**, all **2866/3004**
guarded and **2504/2629** conditional budget cutoffs, exact endpoints and live
forged read/guard edits. Local mixed Maps pass **35 observations and seventeen
refusals** across both storage layouts, Node/interpreter, GCC/Clang and
ASan/UBSan. Seven targeted lit cases pass in **28.21 seconds**. Initial focused
CTest passes **12/12 in 30.69 seconds**. Inspected
`/tmp/ctcompile-guard-string.cpp`: real `map_has` and branch, owning String
selection and saved copy through subsequent writes/deletes; no Script symbols
or interpreter context.

The parallel copy proof admits only exact fresh ordinary own-data source and
target objects. It charges a source snapshot before writes, records copied
edges separately from direct Stored witnesses and preserves every historical
copy edge for cycle refusal. Runtime copy lookup can invoke getters in general;
this proof excludes descriptors, accessors, prototypes and unknown effects.
Arrays/external endpoints remain refused. No native ownership admission changes.

Copy units pass **39 rows, eleven key controls, seventeen live states, one
wide snapshot, one missing-lattice control and 2794 retention budget cutoffs**,
plus five path-explosion cutoffs. Three executed functions add ten sites/instances
with five retained. All previous source families retain their expectations.
All four oracles report zero violations; expanded-fixture precision is **29/41**,
while Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**. Combined focused CTest passes
**12/12 in 30.19 seconds**. Log: `/tmp/ctcompile-guard-copy.log`.
Homebrew clang-format **22.1.8** passes **743 files** and whitespace checks pass.

The full **243-step generated devbox build succeeds**. Final CTest is
**512/517 in 918.68 seconds**: **372/372 compiler** and **140/145 browser** tests.
Only the recorded `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors` failures remain. All **165/165 lit cases** pass (`ctcompile_lit`,
**317.48 seconds**), and exception recovery passes in **1.23 seconds**. All four
execution oracles again report zero violations; precision stays **29/41, 0/64,
0/16, 0/20**. Corpus observations remain limited by their existing environments
and execution coverage. Fresh native components remain **Bootstrap 19/574,
p5 39/4754, Phaser 45/7725** in both modes with zero pruned. Exact Data remains
**0/7 browser, 0/7 CommonJS, 0/8 AMD**. Complete native Bootstrap initialization
is unfinished. Final evidence: `/tmp/ctcompile-guard-full.log` and
`/tmp/ctcompile-guard-evidence.json`.

All **fourteen changed code/test paths** match committed HEAD, frozen gate input
and the final devbox source. The inspected String output retains its live guard,
branch, reads, writes and owning saved result without Script symbols or an
interpreter context. Checkpoint docs were saved as `015189d`; this update records
the completed full gate. No browser source or runtime semantics changed.

**Exact next boundary:** replace the accepted guarded ternary with
`(state.has('other') && state.get('other')) || state.get('')`. Keep the seed,
conditional deletion, saved write/read/delete chain and two standalone
`set(get(false))`, `set(get(true))` calls followed by `size()`. Node/interpreter
still give **2**, but both native modes remain **0/6**, all **eighteen calls
retained**, with no host owner proof. The ternary control is **6/6** with the same
trace. The intermediate Boolean/String `&&` result needs proof that its truthy
`||` arm retains a String; the host's single optional scalar tag currently loses
that distinction. Prove live result alternatives and connect the native proof
without selecting a startup value or using the storage schema as authority.

Fresh guarded `result || null`, nullable ternary, normalized consumer-key and
object-payload witnesses each remain **0/6**, eighteen calls, Node/interpreter
**3**. A second guarded nullable return keeps nineteen calls with the same result
and refusal. These witnesses differ from the preceding checkpoint's nullable
ones. Full next source is committed in `native-owned-global-maps.md` under
"Next boundary", and on the devbox at
`/tmp/ctcompile-guard-next/shortcircuit_same_tag.js`; measured evidence:
`/tmp/ctcompile-guard-boundary.json`. Keep observer calls as standalone statements.
Nullable carriers, object identity, export ABI and native Bootstrap Data remain
separate obligations.

## Conditional Map values and object-deletion checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`53b44b9`**, saved scalar Map values across
conditionals, and **`55be8e9`**, fixed own-field deletion contents and retention.
This resumes `saved_join` from **`7a4ebf9`** and the **08:16:09 synchronization
journal**. The tree started clean. Interrupted CallDirectOp recovery was already
landed in `5307abf`; its old branch is an ancestor. No history was rewritten,
browser source changed or push performed. Separate agents supplied execution
checks, escape analysis and a host-proof audit with unit controls.

The exact conditional specimen advances **0/6 -> 6/6 native** in both optimization
modes, retaining all **sixteen calls** and Node/interpreter **`trace=3`**. The
always-empty straight-line control gives **2**. Both arms of each captured-method
`scf.if` are checked, including literal predicates. Mutable Map contents
intersect across arms; saved scalar results get a tag only when both yields
independently prove the same type. Zero-result conditionals include the implicit
unchanged path. Publication and factory execution remain unconditional, raw
multi-block bodies remain refused, and nesting is bounded to 32 levels.

The host proof lives in `HostContract/CapturedMapBody.cpp`. Native preparation
recognizes selected saved-read candidates before monotone inference, while
independent presence/payload analysis supplies the actual type. The census
alone authorizes no scalar fact. Missing results, differing tags, unknown
actuals and input annotations remain insufficient.

The published gate passes **89 complete programs**, including six conditional
programs, four new missing/deleted/mixed-tag refusals and **ten lifetime sanitizer
variants**. It checks both optimization modes, GCC/Clang explicit/deduced C++,
all original calls and mutation observations. A long String case calls only
`false` during startup; saved C++ callables later use both flags, preserve two
independent strings across overwritten/deleted entries and final Map release,
and survive independent reentry. First complete budgets for the four new probes
are **17934/18476/18476/10166**, with **32/31/31/31** cutoffs and no natural
speculative rollback interval. Log: `/tmp/ctcompile-conditional-native.log`.

The local mixed-Map gate passes **27 observations and twelve refusals** under
both storage layouts, Node/interpreter, GCC/Clang and ASan/UBSan. All **seven
targeted lit cases pass in 28.11 seconds**. Host units pass **25 conditional rows
per source/prepared form**, every **2501/2626** incomplete budget, exact endpoints
and live forged-marker edits. The final host CTest passes in **3.22 seconds**.
Logs: `/tmp/ctcompile-conditional-checkpoint3.log` and
`/tmp/ctcompile-conditional-boundary.json`. The expanded local gate first exposed
a missing selected-value candidate; its implementation is included in `53b44b9`.
A mixed-tag write is rejected before the read, and its test checks that exact
stage. No native program names Script symbols or an interpreter context.

The parallel contents proof handles exact own String-field deletion on fresh
objects, preserving saved reads and every historical cycle edge. It passes
**30 deletion rows, eighteen key controls, fourteen live states, one missing-lattice
control and 1663 retention cutoffs**. Three executed functions add five sites and
two retained instances; the deleted self-cycle remains `Stored`. All four
execution oracles report zero violations. Expanded-fixture precision is **24/36**;
Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**. Combined focused CTest passes
**12/12 in 29.32 seconds**. This changes no native ownership admission.

Homebrew clang-format **22.1.8** passes **743 files** and whitespace checks pass.
The full **243-step generated devbox build succeeds**. Final CTest is
**512/517 in 887.20 seconds**: **372/372 compiler** and **140/145 browser** tests.
Only the recorded `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors` failures remain. Lit passes **165/165 in 290.02 seconds**;
exception recovery passes in **1.18 seconds**. All four execution oracles again
report zero violations, with precision **24/36, 0/64, 0/16, 0/20**. Corpus
observations remain bounded by their existing environment and execution limits.
Fresh native components remain **Bootstrap 19/574, p5 39/4754, Phaser 45/7725**
in both modes with zero pruned. Exact Data remains **0/7 browser, 0/7 CommonJS,
0/8 AMD**. Complete native Bootstrap initialization remains unfinished.
Evidence: `/tmp/ctcompile-conditional-full.log` and
`/tmp/ctcompile-conditional-evidence.json`.

All **eighteen changed code/test paths** match committed HEAD, frozen gate input
and final devbox source. Inspected `/tmp/ctcompile-conditional-string.cpp`: the
getter selects into an owning `std::string`, retains a separate saved copy and
returns another owning read after entry overwrite/deletion; there are no Script
symbols or interpreter contexts. Checkpoint docs were committed as `e0060af`;
this final update records the completed full gate.

**Exact next boundary:** after seeding `'' -> ''` and `'other' -> 'future'`, run
`if (flag) state.delete('other');`, then select the saved value with
`state.has('other') ? state.get('other') : state.get('')` and keep the existing
write/read/delete chain. The two standalone `set(get(false))`, `set(get(true))`
calls followed by `size()` give Node/interpreter **2**, but native remains **0/6**
in both modes with **all eighteen calls retained** and no host owner proof.
Replacing the conditional deletion with `state.has('other')` gives **3** and
admits **6/6**. Keeping the conditional deletion but replacing the entire
ternary with `state.get('')` gives **1** and admits **6/6**, retaining **sixteen
calls**. Source: `/tmp/ctcompile-conditional-next/guarded_saved_read.js` on the
devbox; the complete source is in `native-owned-global-maps.md` under "Next
boundary". A constant-false ternary still checks its missing arm and is also
refused; it is not a straight-line getter control. The next proof needs live
`has`-guard membership plus a payload tag valid whenever that key is present,
retained across deletion joins. Membership alone cannot establish that tag.
Nullable results (`result || null`) and object identity payloads separately remain
**0/6** with Node/interpreter **4** and **6**. Exact Bootstrap Data remains open.

## Saved Map read/write and switch-retention checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`d9a4b04`**, independent scalar Map read/write
facts, and **`d370807`**, bounded switch contents and retention. This resumes
`saved_read_write` from **`11803bd`** and the **07:37:42 synchronization journal**.
The starting tree was clean. The earlier interrupted CallDirectOp recovery was
already landed in `5307abf`; the old WIP branch is an ancestor. No history was
rewritten, browser source changed or push performed. Disjoint agents supplied
the published execution gate, switch proof and read-only audit.

The exact saved-read/write specimen advances **0/6 -> 6/6 native** in both
optimization modes, with Node/interpreter **`trace=1`** and all **twelve source
calls preserved** in emitted code. A proved-present scalar read keeps its own
Boolean, Number or String tag after known source-entry mutations. A later write
uses that independent fact without narrowing the complete Map storage schema.
Entry membership, possible aliases and callee effects still invalidate mutable
contents. Branches intersect saved SSA facts. Unknown/missing reads, arbitrary
local parameters and input annotations supply no scalar evidence.

The published gate passes **83 complete programs**, including seven saved-chain
programs in both modes and all **nine lifetime sanitizer variants**. Saved
strings survive source overwrite/deletion, a second write/read, final Map
release, caller-buffer mutation and independent reentry. Three new missing or
deleted-read controls retain every call under fresh/stale Bool/String forgeries
and reruns. Wrong-tag and later-overwrite programs retain their real Boolean
result (**2**); saved String/Boolean/Number witnesses return **1**.
Native admission first completes at **8150/8340/8334/8871** steps for the four
new budget probes, with **30/32/32/32** cutoffs checked and no natural speculative
rollback interval. Log: `/tmp/ctcompile-saved-gate2.log`.

The local mixed-Map gate passes **21 observations** across associative and
ordered storage, with Node/interpreter, GCC/Clang, explicit/deduced output and
ASan/UBSan. Ten live refusal controls pass. The new missing-read writeback is
rejected at optional storage admission, before the mixed-read diagnostic; its
test now checks that precise refusal. All **seven targeted lit cases** pass in
**28.28 seconds**, including CTJS-only binding-time and partial-evaluation paths.
The combined focused CTest gate passes **12/12 in 29.05 seconds**.
Log: `/tmp/ctcompile-saved-checkpoint.log`. No native program contains Script
symbols or an interpreter context.

Switch contents enumerate default and every case with exact independent
origin/container/frame states and successor operands. Unsupported paths, loops,
external values, cycles and incomplete budgets retain original verdicts. The
gate passes **21 switch rows, six live states, four malformed controls, 1526
retention cutoffs and five path-explosion cutoffs**. All four execution oracles
report zero violations. Precision stays **22/33** for the fixture and **0/64,
0/16, 0/20** for Bootstrap/p5/Phaser. This adds no native ownership admission;
loops, external contents and native lifetime consumers remain separate work.

Homebrew clang-format **22.1.8** passes **742 files** and whitespace checks pass.
The full **243-step generated devbox build succeeds**. CTest finishes
**512/517 in 864.73 seconds**, comprising **372/372 compiler** and **140/145
browser**. Only the recorded `selectors`, `frames`, `element_attrs`, `vm_async`
and `early_errors` failures remain. All **165/165 lit cases** pass in **264.08
seconds**; source exception recovery passes in **1.16 seconds**.
Log: `/tmp/ctcompile-saved-full.log`; evidence:
`/tmp/ctcompile-saved-evidence.json`. All eleven changed code/test paths
byte-match the final devbox input and committed source; all thirteen frozen
code/test/document paths matched the implementation checkpoint.

Fresh component coverage remains **19/574 Bootstrap**, **39/4754 p5** and
**45/7725 Phaser** in both optimization modes, with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS**, **0/8 AMD**. These are
compile-admission counts, not complete native initialization or execution.
The inspected `/tmp/ctcompile-saved-string.cpp` makes two owning `std::string`
copies, retaining each across its source entry's overwrite and deletion.
The second copy returns by value to the live setter. No Script symbols,
interpreter context or collector occur in the emitted program.

**Exact next native boundary:** selecting a saved value with
`flag ? state.get('other') : state.get('')` remains **0/6 native** in both modes,
with **all sixteen calls retained** and **no host owner proof**. Seed `''` with
`''` and `'other'` with `'future'`, select the saved value in `get(flag)`, then
run the existing write/read/delete chain. Call `set(get(false))` and
`set(get(true))`, then observe `size()`: Node/interpreter agree on **3**.
Replacing the selection with `state.get('')` yields **2** and admits **6/6**.
The next proof must handle live control-flow joins in the host method body as
well as native scalar facts; an observed startup branch cannot authorize future
calls. `HostContract/Values.cpp` currently requires a single-block method
and does not admit truthy/branch/yield operations in that body. Keep the two
calls as standalone statements: placing their results in a weighted numeric
observer also loses ownership for the straight-line control. Final evidence:
`/tmp/ctcompile-saved-boundary-final.json`; exact sources:
`/tmp/ctcompile-saved-next/saved_join.js` and `saved_join_always_empty.js` on the
devbox. Exact Bootstrap Data and complete native initialization remain unfinished.

## Current mixed-Map and object-contents checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`6e68d3f`**, closed mixed Map keys/payloads
with independent exact read evidence. This resumes the unfinished native
boundary from **`6e7dd81`** and the **06:13:42 synchronization journal**.
The starting tree was clean; the interrupted CallDirectOp recovery was already
landed in `5307abf`, and `codex-wip-20260907` is an ancestor. No history was
rewritten, browser source changed or push performed.
The parallel fixed own-property contents increment is saved as **`0a3000a`**.
Two implementation agents and a separate audit supplied disjoint work; service
interruptions were recovered before their results were integrated.
Integration repairs are **`c0777fb`** (semantic Map tags remain usable by
CTJS-only analysis callers) and **`2086c63`** (printing comparisons preserve
genuine program headers).

The three preceding `result_seeded_mixed_contents`, `result_seeded_join_reseed`
and `result_seeded_bool_string_contents` probes advance **0/6 -> 6/6 native**,
with Node/interpreter traces **2/3/2** and all **9/10/9 calls retained**.
Storage uses exact Bool/Number or Bool/String `std::variant` alternatives.
Each mixed read separately proves membership and a scalar payload from the
last literal write on every path. Schema inference still retains every stored
alternative. Key comparison preserves false versus zero and numeric SameValueZero;
string reads return owning copies. Unknown payloads and missing reads refuse.

The published gate passes **76 complete programs** with Node/interpreter and
explicit/deduced GCC/Clang agreement, no Script symbols and all **eight lifetime
sanitizer variants**. Native admission first completes at **7658/7910/7658/8367**
steps for the three original mixed specimens and saved-string specimen, with
**30/30/30/31** checked cutoffs and no natural speculative rollback interval.
The new local gate passes **nine observations across associative/ordered Maps**
and **seven refusal controls**, including possible receiver aliases, callee
writes, different branch tags, deletion, nonliteral payloads and forged facts.
A dead-alternative case checks that homogeneous emission retains proved presence.
The existing representation gate now passes **14 observations**, including the
unchanged literal mixed-storage source. All **seven targeted lit cases** pass.
Logs: `/tmp/ctcompile-mixed-native2.log`, `/tmp/ctcompile-mixed-checkpoint2.log`
and `/tmp/ctcompile-mixed-lit-final.log`.

The formatter passes **742 files** with Homebrew clang-format **22.1.8**.
The final combined focused CTest gate passes **12/12 in 29.04 seconds**. All
**29 changed code/test paths** byte-match committed and frozen input. The full
**243-step generated devbox build succeeds**; CTest finishes **512/517 in
832.07 seconds**, comprising **372/372 compiler** and **140/145 browser**.
Only the same `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors` failures remain. All **165/165 lit cases** pass in **236.87
seconds**, and exception recovery passes in **1.16 seconds**.
Final log: `/tmp/ctcompile-mixed-full2.log`; extracted evidence:
`/tmp/ctcompile-mixed-evidence.json`.

The initial full run exposed native dialect loading in standalone binding-time
and partial-evaluation callers. Payload proofs now retain semantic tags and
create native types only during type inference. All seven failing cases and
the new mixed test pass **8/8 in 10.02 seconds**, including valid forged tags.
The printing gate removes only the include adjacent to the pin macro, retaining
every genuine program include in its exact comparison. All **39/39 printing
CTests** pass in **28.40 seconds**. Logs: `/tmp/ctcompile-mixed-fix.log` and
`/tmp/ctcompile-print-gate.log`. The failed initial full run is retained as
`/tmp/ctcompile-mixed-full.log`; it is superseded by the complete rerun above.

Fresh native component coverage remains **19/574 Bootstrap**, **39/4754 p5**
and **45/7725 Phaser** in both optimization modes, with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS**, **0/8 AMD**. Full native
Bootstrap initialization remains unfinished. Final emitted C++ inspected at
`/tmp/ctcompile-mixed-saved-string.cpp` copies a proved `std::string` from the
finite variant, overwrites and deletes its source entry, then returns the
owning saved value. No interpreter context, collector or Script symbols occur.

**Exact next native boundary:** `saved_read_write` remains **0/6 native** in
both optimization modes with complete host ownership, all **12 calls retained**
and Node/interpreter **`trace=1`**. Starting from
`payload_result_sources()["result_seeded_bool"][0]`, replace the getter body with:

```js
state.set('', '');
const saved = state.get('');
state.set(false, true);
state.set(false, saved);
const result = state.get(false);
state.delete(false);
return result;
```

The native proof clears payload evidence at the nonliteral write. Propagate
independent scalar facts through the saved read/write chain while retaining
every mutation, alias invalidation and missing-result refusal. Replacing that
write with `true` yields **2** and admits **6/6**; moving the second read after
deletion also yields **2**, loses host result evidence and remains **0/6**.
Fresh final evidence: `/tmp/ctcompile-mixed-boundary-final.json`; source on the devbox:
`/tmp/ctcompile-mixed-next/saved_read_write.js`.

The independent contents query now tracks exact own String properties on fresh
objects, including saved reads, object/array aliases and return reachability.
The retention consumer checks every write across both container kinds, so
transient or mutually exclusive cycles preserve original escape verdicts.
Keys are bounded to 256 bytes; missing properties, `__proto__`, prototypes,
accessors, external values, loops and incomplete proofs refuse. This adds no
native ownership admission. The gate passes **31 object rows, eleven keys,
twelve live states and 1269 retention cutoffs**, plus **38 array contents
rows/14 keys**, 20 retention rows/661 cutoffs, 26 frame rows/444 cutoffs and
17 conditional rows/877 cutoffs with five path-explosion controls.
All four execution oracles report zero violations; precision remains **22/33**
for the fixture and **0/64, 0/16, 0/20** for Bootstrap/p5/Phaser.
Log: `/tmp/ctcompile-object-focused2.log`.

Review exposed an older unsound String-array-index assumption. A literal or
object-loaded String `"0"` write yields **Node 2 versus interpreter 1** because
the runtime retains the old element; its String read yields **Node 1 versus
interpreter undefined**. Complete contents now refuses String array reads
and writes, retaining numeric indices and own String object fields. Direct,
loaded and live Number/String key changes under forged markers guard the fix.
Runtime sources and source oracle expectations remain unchanged; the mismatch
is in the synchronization journal and `/tmp/ctcompile-object-key-oracle.json`.
Next escape work remains loops/external values and native lifetime consumers.

## Preceding Map payload and conditional-array checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`d16f763`** (homogeneous boolean and owning
string Map payloads) and **`ab10057`** (bounded conditional-array retention).
The starting tree was clean at `7dde504`; this resumes the exact Bool/String
boundary in that handoff and the **05:27:53 synchronization journal**. The
interrupted CallDirectOp work was already recovered in `5307abf`, and
`codex-wip-20260907` is an ancestor. The old unmerged closure-nesting fix is
also already present in `ClosureLifting/Bindings.cpp`. No history was rewritten.
Two disjoint implementation agents supplied the execution gate and array work;
a third audited the lowering boundary. No browser source changed or push occurred.

`result_seeded_bool` and `result_seeded_string` advance **0/6 -> 6/6 native**,
with complete host owner/result proofs and Node/interpreter **`trace=2`**.
The existing owning Map storage now accepts `bool` and `std::string` values.
Ordinary reads preserve false/empty versus missing through existing nullable
carriers; proved-present reads and string value snapshots return owning copies.
Boolean value snapshots and mixed key/payload schemas remain refused. Every
source call, runtime mutation, lookup and consuming argument remains executable.

The published gate passes **69 complete native programs** with Node/interpreter
and explicit/deduced GCC/Clang agreement, no Script symbols, and all **seven
sanitizer lifetime variants**. Saved strings survive overwrite, deletion,
Map destruction, caller-buffer mutation and independent reentry. False, empty,
overwritten and saved-string witnesses return **1**; blinded controls return
**2**. Two deleted-payload refusals preserve all calls under fresh/stale forged
markers and reruns. Native admission first completes at **7474/7474/8008** steps
for the original boolean/string and saved-string programs, checking **32/32/30**
cutoffs. No natural speculative rollback interval was observed. Source/prepared
cardinality proof budgets remain **2278/2372**.

The local Map representation gate passes **13 observations**, including five
new observations independently checked against Node/interpreter. GCC/Clang,
explicit/deduced forms and ASan/UBSan cover missing reads, false/empty values,
long embedded-NUL strings, returned nested Maps and copied value snapshots.
All six targeted lowering lit tests pass after correcting new harness/FileCheck
expectations; no production proof was weakened. Logs:
`/tmp/ctcompile-payloads-native.log`, `/tmp/ctcompile-payloads-lit.log` and
`/tmp/ctcompile-payloads-lit-final.log`.

Array contents now enumerate bounded acyclic `cf.br`/`cf.cond_br` paths with
independent exact origin, array-slot and frame state. Both edges are checked,
and joins replay each predecessor so a strong overwrite cannot erase another
path's aliases. Returned reachability and conservative all-write cycle checks
cover every exit. Loops, unsupported effects and incomplete work still refuse;
an external truthy predicate does not prove an external element or root safe.
The combined focused gate passes **12/12 CTests in 29.38 seconds**: the new
**17 conditional rows, five live states, 738 retention cutoffs and five bounded
path-explosion controls**, plus the existing 35 contents/20 retention/26 frame
rows. Four execution oracles report zero violations. Fixture precision remains
**22/33**; Bootstrap/p5/Phaser remains **0/64, 0/16, 0/20**. Native ownership
consumers remain separate. Log: `/tmp/ctcompile-payloads-focused.log`.

The full generated devbox build succeeds (**243 build steps**). The combined
gate finishes **512/517 CTests in 808.06 seconds**: **372/372 compiler** and
**140/145 browser**. Its five failures exactly match the preceding gate:
`selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`. No browser
source changed. All **164/164 lit cases** pass in **208.85 seconds**, and source
exception recovery passes in **1.15 seconds**. Log:
`/tmp/ctcompile-payloads-full.log`; extracted evidence:
`/tmp/ctcompile-payloads-evidence.json`.

Fresh native component coverage remains **19/574 Bootstrap**, **39/4754 p5**
and **45/7725 Phaser**, in both optimization modes with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS** and **0/8 AMD**. Complete
native Bootstrap initialization remains unfinished.

The frozen source passes `tools/format.sh --check`: **742 files** using
Homebrew clang-format **22.1.8**. All **19 changed code/test paths** byte-match
the frozen devbox input and committed implementation. Emitted C++ inspected at
`/tmp/ctcompile-payloads-saved-string.cpp` copies the read into a `std::string`,
then overwrites and deletes the source entry in the same owning `std::map`,
returns the saved value and passes it to the live setter. No interpreter context,
collector or Script symbols occur in the native program.

**Exact next native boundary:** `result_seeded_mixed_contents`,
`result_seeded_join_reseed` and `result_seeded_bool_string_contents` remain
**0/6 native** despite complete host owner/result proofs. Fresh Node/interpreter
traces are **2/3/2**, with all **9/10/9 source calls** retained. Both keys and
values have mixed schemas: Bool/Number or Bool/String. Extend closed finite key
comparison, storage, set conversion and exact read/result facts together;
one final get's tag cannot narrow the entire Map schema. Preserve SameValueZero,
false versus numeric keys, owning strings and independent presence evidence.
Evidence: `/tmp/ctcompile-payloads-boundary.json`.

Unseeded published reads still need independent result evidence across the
complete family. Current invocations do not authorize arbitrary future callers.
Exact Bootstrap Data, realm owners, complete throwing-call components and live
caller-depth/reentry proofs remain unfinished. Array loops, external stored
values and other containers are still outside the complete contents query.

## Preceding distinct-key Map and array-chain checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`c900f84`** (distinct-key Map size bounds)
and **`d863dc3`** (array retention through unconditional branch chains). The
starting tree was clean at `310c9a3`. The interrupted CallDirectOp/frame work
was already recovered and gated in `5307abf`, `e8d5cdb` and `f063455`; this
session continued the exact `seeded_size_two_entries` boundary recorded in that
handoff and the 04:49 synchronization journal. Older unmerged branches were
audited and are browser work, not an unfinished native recovery. Main work and
two disjoint implementation agents were integrated; a third audited the next
payload and source-exception boundaries. No browser source changed or push occurred.

The published Map specimen now seeds keys 0 and 1, deletes `state.size`, then
returns `state.get(1)`: **0/5 -> 5/5 native**, with Node/interpreter **`trace=2`**.
Host result and native presence analyses independently count a pairwise-distinct
subset of definite keys in the same runtime Map. Each read examines at most
**64 candidates**; saved lower bounds survive later mutation. Aliasing SSA keys,
signed zeros and NaN encodings cannot inflate cardinality. Host work is charged
per candidate/comparison, and incomplete proofs expose no callable/property facts.
The 64/65-candidate boundary is intentional, not the next implementation target.

The gate passes **63 complete native programs**, eight size programs and ten
size-key refusals, with explicit/deduced GCC/Clang, Node/interpreter agreement,
no interpreter symbols and all six existing sanitizer lifetime variants.
Runtime calls and result transport remain intact. Source/prepared cardinality
proofs check all **2278/2372** incomplete budgets; native completion is
**5931/9391** for two entries/saved emptied state, with **30** cutoffs each.
The separate nested-Map presence lit gate passes **seven positives and fourteen
refusals**. Logs: `/tmp/ctcompile-cardinality-focused2.log` and
`/tmp/ctcompile-cardinality-lit2.log`.

The array query now follows only acyclic `cf.br` chains with one predecessor per
destination and exact value/frame forwarding. Joins, conditional flow, loops,
unknown values/effects and incomplete work still refuse. The combined focused
gate passes **12/12 CTests in 28.45 seconds**: **35 contents rows, 20 retention
rows, 26 frame rows**, twelve live frame/control-flow states, **500 retention
and 387 frame budget cutoffs**, and four execution oracles with zero violations.
Fixture precision stays **22/33**; Bootstrap/p5/Phaser precision stays
**0/64, 0/16, 0/20**. Native ownership consumers remain separate.

The full generated devbox build succeeds (**243 build steps**). The combined
gate finishes **512/517 CTests in 788.70 seconds**: **372/372 compiler** and
**140/145 browser**. Its five failures remain the recorded browser `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors`; no browser source
changed. All **164/164 lit cases** pass in **187.86 seconds**, and source
exception recovery passes in **1.20 seconds**. Log:
`/tmp/ctcompile-cardinality-full.log`; extracted evidence:
`/tmp/ctcompile-cardinality-evidence.json`.

Fresh native component coverage remains **19/574 Bootstrap**, **39/4754 p5**
and **45/7725 Phaser**, in both optimization modes with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS** and **0/8 AMD**. Complete
native Bootstrap initialization remains unfinished.

The current frozen source passes `tools/format.sh --check`: **742 files** with
Homebrew clang-format **22.1.8**. All **12 changed code/test paths** byte-match
the frozen devbox input and committed implementation. Emitted C++ at
`/tmp/ctcompile-cardinality-two-entries.cpp` was inspected: the two seeds, size
read, deletion and typed lookup execute against the same owning `std::map`,
with no interpreter context, collector or Script symbols.

**Exact next native boundary:** `result_seeded_bool` and
`result_seeded_string` in the existing Map gate remain **0/6 native** despite
complete host owner/result proofs; Node/interpreter both produce **`trace=2`**.
A fresh post-gate probe retains **all eight source calls** in each refusal and
confirms the completed size specimen at **5/5**, with its owner proof and
`trace=2`. Evidence: `/tmp/ctcompile-cardinality-boundary.json`.
`LoweringSupport.cpp` refuses homogeneous Bool/UTF8 String Map payload carriers.
Extend carrier selection/spelling, `replaceMap` construction and generic Map
reads together, reusing owning string/nullable scalar types. Preserve false and
empty-string versus missing, saved string lifetime and mixed-payload refusals;
string-value snapshots need owning copies or an explicit refusal. Full native
Bootstrap Data, realm owners and future external callers remain unfinished.

Source exceptions next need one transaction covering the complete call
component, standalone throwing helpers, invoke admission/emission and owning
saved state, including pruning only provably dead invocation tuple slots.
A live whole-entry caller-depth/reentry proof must discharge frame-entry
failure separately: the public AOT contract returns `CT_AOT_FAILED`, never a
catchable JS payload. The existing 32-level analysis bound does not prove the
runtime caller stack safe. Retain complete rollback until every component
member lowers. Array contents next need conditional/join/loop flow, external
values and other containers before broader ownership consumers.

## Preceding Map.size and imported-array checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`e8d5cdb`** (nonempty Map.size snapshots)
and **`f063455`** (imported array frame/root retention). This session resumed
the dirty compiler work recorded in the synchronization journal at 03:52/04:00,
abandoned at 04:02:32. The earlier reverted source-completion thread was already
recovered in `5307abf`/`5b0b602`, including the corrected `CallDirectOp` builder;
no branch was rewritten. Two agents completed disjoint proof/test work and a
third audited the next boundary. No browser source changed; no push occurred.

The [published Map gate](native-owned-global-maps.md) now admits
`state.set(0, 1); state.delete(state.size); return state.get(0)` as the producer
in `host.slot.set(host.slot.get())`: **0/5 -> 5/5 native**, preserving
Node/interpreter **`trace=1`**. Host result and native presence proofs each
establish a nonempty size snapshot from a definite entry in the actual Map.
Saved numbers survive later mutations; initial zero sizes, equal positive keys
and equal snapshots cannot borrow disjointness. All runtime calls remain.

**58 complete native programs** pass Node/interpreter and explicit/deduced
GCC/Clang execution, including all six existing sanitizer lifetime variants
and the no-interpreter-symbol gate. Three new size programs and three
observationally distinct refusals cover saved/current size, empty Maps and
aliasing. Source/prepared host proofs check all **2212/2299** incomplete
budgets. The new presence lit test passes three positives and seven refusals.
Logs: `/tmp/ctcompile-size-focused.log`, `/tmp/ctcompile-size-native.log`,
`/tmp/ctcompile-size-lit.log`. Emitted C++ was inspected at
`/tmp/ctcompile-size-dynamic-delete.cpp`: seed, size, delete and typed lookup
execute against the same owning Map, with no VM context or collector.

The [array retention query](escape-load-evidence.md) validates entry-first
frame creation, exact active roots and a matching exit immediately before
return. A no-successor/no-region entry scan proves the importer's extra default
return block unreachable independently of solver flags. Frame-entry failure
still precedes tracked allocations; this is no native nonthrowing permission.
The gate passes **8/8 CTests in 8.45 seconds**, including **19 frame rows,
eight live states and 248 budget cutoffs**, all four escape units and all four
execution oracles with zero soundness violations. Nine new source functions
exercise **19 sites / 21 instances / 11 retained instances**. On this same
expanded fixture, observed confined-site precision advances **18/33 -> 22/33**
after supporting the dead fallback block. Bootstrap/p5/Phaser observed precision
remains **0/64, 0/16, 0/20** in these script-mode probes. Log:
`/tmp/ctcompile-size-escape2.log`. Native ownership consumers remain separate.

The full generated devbox build succeeds (**411 build steps**). The combined
gate finishes **512/517 CTests in 770.50 seconds**: **372/372 compiler** and
**140/145 browser**. Its five failures are the previously recorded browser
`selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`; no browser
source changed. All **164/164 lit cases** pass in **174.21 seconds**, and the
restored source-exception recovery test passes in **1.15 seconds**. Log:
`/tmp/ctcompile-size-full.log`; extracted evidence:
`/tmp/ctcompile-size-evidence.json`. All **16 changed code/test paths** byte-match
the frozen snapshot and committed implementation.

Fresh native component coverage remains **19/574 Bootstrap**, **39/4754 p5**
and **45/7725 Phaser**, in both optimization modes with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS** and **0/8 AMD**. Complete
native Bootstrap initialization remains unfinished.

The frozen current source passes `tools/format.sh --check`: **742 C++ files**,
Homebrew clang-format **22.1.8**, matching the preceding accepted checkpoint.
The ignored local toolchain's clang-format 23 instead flags nine baseline files;
those were left untouched. Whitespace checks pass, and all session changes are
committed locally without a push.

**Exact next native boundary:** `seeded_size_two_entries` seeds keys 0 and 1,
deletes `state.size`, then returns `state.get(1)`. It is freshly measured at
**0/5 native**, no owner proof and every source call intact; Node/interpreter
both produce **`trace=2`**. Evidence: `/tmp/ctcompile-size-boundary.json`.
Derive a bounded cardinality lower bound from independently distinct definite
keys in both analyses; the number of facts is not a size proof when keys can
alias. Unseeded reads, mixed payload carriers, exact Bootstrap Data, general
realm owners and future-call contracts remain unfinished. Source throwing-call
work next needs complete native component admission and owning payload/state
emission; the restored normal-return inference alone cannot erase fallible
frame entry. The array query next needs reachable control flow, external values
and other containers before broader ownership consumers.

## Preceding per-key Map, array retention and transitive-call checkpoint, 2026-09-07

Saved locally on `ctcompile-v1`: **`ec2dc20`** (bounded array retention
consumer), **`327a3c5`** (disjoint per-key Map facts), and **`fd90ea9`**
(transitive source invocation completions). Main native work and two independent
agent implementations are integrated. No browser source changed; no push occurred.

The [published Map boundary](native-owned-global-maps.md) now accepts
`get() { state.set(0, 1); state.set(1, 2); return state.get(0); }` as the
producer in `host.slot.set(host.slot.get())`, advancing **0/5 -> 5/5 native**
with Node/interpreter `trace=1`. Bounded per-key facts preserve earlier payloads
across independently disjoint writes/deletes. SameValueZero treats both zero
encodings and every NaN payload as equal. Possibly aliasing mutations discard
old facts before a set installs its own independently proved payload tag.
The separate native presence analysis invalidates cached `has` observations
consistently; transitive call summaries remain conservative. No call or lookup
is evaluated away, and each invocation begins with unknown contents.

The native gate passes **47 complete programs**: fifteen **4/4**, twenty-six
**5/5**, six **6/6**, matching Node/interpreter and explicit/deduced GCC/Clang
execution with no linked interpreter symbols. Seven new programs cover disjoint
writes/deletes, overwritten earlier keys, reseeding, nine live keys and
string/boolean keys. All six existing Map/table/callable lifetime variants pass
ASan/UBSan, use-after-scope/return and leak checks. Nine seeded proof refusals
retain every source call; string/boolean/mixed payload carriers remain separate.

The combined Map/escape gate passes **7/7 CTests in 22.54 seconds**;
log: `/tmp/ctcompile-map-keyfacts-units.log`. Per-key source/prepared host proofs
complete at **2145/2230** steps; owner proofs at **5072/4962**. Disjoint-delete
owner proofs complete at **5116/5007**. Every smaller budget withholds the entire
proof. First source native completion is **5721** for the earlier key and
**5700** for the disjoint delete, with **30/31** checked cutoffs and no natural
speculative rollback interval. These are work limits, not speedup claims.
Native execution log: `/tmp/ctcompile-map-keyfacts-focused2.log`.

The [array retention consumer](escape-load-evidence.md) independently recomputes
complete local contents, all-write acyclicity and return reachability before
changing an unretained `Stored` site to `Confined`. Returned children keep their
original storage witness, including saved reads and loaded aliases. Cycles,
transient cycles, unknown effects, missing lattices and incomplete budgets
preserve the original verdicts. **18 retention rows, seven live states and 454
budget cutoffs** pass alongside the **31 contents rows, 14 key controls and 209
sink-table rows**. All four execution oracles report zero violations; corpus
precision is unchanged. Native admission does not consume these verdicts.

The [source invocation prerequisite](native-source-invocations.md) follows a
complete acyclic source call family with exact stacked formal/actual contexts.
Normal returns belong to the selected callee; descendant throws can supply its
unwind. Unknown effects, recursion, depth above 32 and incomplete work refuse
without changing the original function. Four transitive cases recover
**1/2/1/2 invokes** with **7/10/9/12 original checks** available for rollback,
at **3737/6601/5106/9031** steps. All **5106** transitive-budget prefixes,
**3534** source-binding prefixes, **937** effect prefixes and nine new refusal
controls pass. Recovery CTest passes in **1.19 seconds**, then the complete lit
CTest in **159.07 seconds**; log: `/tmp/ctcompile-map-keyfacts-invocations.log`.
Ordinary native throwing-call admission and payload/state emission remain
unfinished.

The full frozen generated devbox gate passes **475/475 CTests in 720.98
seconds**: **367 compiler / 108 browser**, including **163/163 lit cases in
149.67 seconds**. Log: `/tmp/ctcompile-map-keyfacts-full.log`. All thirteen
changed compiler code/test paths byte-match the three committed implementations.
Browser bytes are the committed `3494d44` baseline (unchanged since `7a755dd`),
excluding Claude's unmerged WPT branch and ten browser carryover paths. Compiler
formatting and whitespace checks pass; the whole-tree formatter flags only
untouched browser `style/selector.hpp`, `DOM/document.cpp` and
`Style/css/selector.cpp`.

Fresh native component counts remain **19/574 Bootstrap**, **39/4754 p5** and
**45/7725 Phaser**, in both optimization modes with zero pruned functions.
Exact Data remains **0/7 CommonJS**, **0/7 browser** and **0/8 AMD**. These are
component counts, not complete applications. Evidence:
`/tmp/ctcompile-map-keyfacts-evidence.json`. The emitted
`/tmp/ctcompile-map-keyfacts-parameter.cpp` was reviewed: both seed writes and
the typed `map_get_present` remain, and the result flows into the runtime setter.
There are no interpreter symbols, VM contexts or collector dependencies.

**Exact next native boundary:** `seeded_dynamic_write` replaces the disjoint
write with `state.set(state.size, 2)` before `return state.get(0)`. It remains
**0/5 native** with no owner proof and every source call intact; fresh Node and
interpreter runs both produce `trace=1`. Its runtime key may alias key 0, so the
proof discards the earlier payload fact despite both
payloads being numeric. Next prove a complete type join across possible
same-key overwrites independently of presence; a possibly aliasing delete
still needs its own presence proof. Unseeded gets, unsupported payload carriers,
full Bootstrap Data, realm ownership and future external callers remain open.
Source invocation integration still needs complete native component admission
and owning payload/state emission. The array consumer next needs checked
imported frame/root bookkeeping and an executed source/oracle witness; raw
imports contain `ctjs.frame_enter`, which the complete query still refuses.
Broader contents and native ownership consumers remain unfinished.

## Preceding seeded-Map, contents and source-binding checkpoint, 2026-09-07

Saved locally on `ctcompile-v1`: **`6986611`** (complete bounded local array
contents), **`b2466a0`** (seeded Map.get results across published calls), and
**`e4b2d5f`** (live source callee bindings before invocation recovery). The main
native boundary and three disjoint agent workstreams were integrated. Two agents
hit service limits after implementation; their work was retained and validated.
No browser files were changed and no push was performed.

The [published Map boundary](native-owned-global-maps.md) now accepts
`get() { state.set(0, 1); return state.get(0); }` as the producer in
`host.slot.set(host.slot.get())`, advancing **0/5 -> 5/5 native** with
Node/interpreter `trace=1`. Each invocation begins with unknown contents. A
bounded last-write fact connects an exact SSA/equal constant key to the payload's
independently proved primitive tag. A later set replaces the fact; delete clears
it. The complete body/use proof must finish before publishing a result tag.
Native Map preparation separately proves instance/key presence and infers the
whole Map schema. No lookup or call is evaluated away.

The gate passes **40 complete native programs**: fifteen **4/4**, nineteen
**5/5**, six **6/6**, matching Node/interpreter and explicit/deduced GCC/Clang
execution with no linked interpreter symbols. Five new producer variants cover
seeded reads, repetition, overwrites, growing runtime keys and distinct formal
actuals. The old seeded local nullable-key boundary also advances **0/4 -> 4/4**.
All **six lifetime variants** pass ASan/UBSan, use-after-scope/return and leak
checks; the new growing getter exercises independent saved/fresh Maps through
1024 further calls each. Seven new presence refusals retain all source calls;
three carrier refusals keep complete ownership but reject string/boolean/mixed
Map payloads. Fresh forged presence markers cannot authorize a result.

Focused validation passes **7/7 CTests in 18.26 seconds**, then all forty native
programs/lifetime variants. Log: `/tmp/ctcompile-map-presence-integrated.log`.
The seeded source/prepared host proofs check every incomplete budget at
**2075/2153** steps; source/prepared owner proofs at **5016/4899**. First native
completion is **5558**, with **30** cutoffs and no natural speculative rollback
interval. This does not claim a performance improvement.

The separate [array contents prerequisite](escape-load-evidence.md) recomputes
exact own elements/read origins/overwrites and return reachability for fresh
local arrays in one straight-line block. It follows loaded array aliases and
bounded cycles without choosing an owner. Unknown values, holes, calls, throws,
publication, prototypes and control flow refuse with no proof records.
**31 contents rows, 14 index controls and seven live mutation states** pass,
including every incomplete budget, alongside the unchanged **209 escape rows**
and all four zero-violation execution oracles. No escape verdict, type lattice
or native admission consumes this new query yet.

The [source invocation prerequisite](native-source-invocations.md) now proves
initialized immutable source callee bindings before dropping their status edges
in `EffectCheckedInvocations`. It checks all current declarations, loads, uses,
call identities and module effects; bounded leaf completion facts preserve
primitive payload and argument state. The three source cases recover
**1/2/1 invokes** with **7/10/9 original checks** available for rollback, at
**3521/6068/4875 steps**. All **3521** assignment-budget prefixes, **937**
effect-budget prefixes, nineteen live binding mutations and uncalled-throw
controls pass. Focused CTests pass **2/2 in 130.38 seconds**, including
**163/163 lit cases**; log: `/tmp/ctcompile-map-presence-invocations.log`.
Ordinary native throwing-call admission remains unchanged.

The full frozen generated devbox build passes **475/475 CTests in 693.86
seconds**: **367 compiler / 108 browser**, including **163/163 lit cases in
130.49 seconds**. Log: `/tmp/ctcompile-map-presence-full.log`. Its compiler bytes
match the committed implementation. Browser bytes are committed `9b6c0d4`
baseline content (unchanged since `7a755dd`), excluding Claude's unmerged WPT
branch and ten browser carryover paths. Compiler formatting passes; the
whole-tree check flags only untouched browser `style/selector.hpp`,
`DOM/document.cpp` and `Style/css/selector.cpp`.

Fresh native component counts remain **19/574 Bootstrap**, **39/4754 p5** and
**45/7725 Phaser**, in both optimization modes with zero pruned functions.
The exact Data probes remain **0/7 CommonJS**, **0/7 browser** and **0/8 AMD**;
AMD includes the delayed-factory registration function. These are component
counts, not complete native applications. Evidence:
`/tmp/ctcompile-map-presence-evidence.json` and
`/tmp/ctcompile-map-presence-bootstrap.json`. The emitted
`/tmp/ctcompile-map-presence-parameter.cpp` was reviewed: `map_get_present`
produces a typed numeric result passed to the runtime setter; the seed, lookup,
setter and final getter remain calls on the same owned Map. No interpreter
symbols or VM context appear.

**Exact next native boundary:** `seeded_earlier_key` inserts
`state.set(1, 2)` before the producer's `return state.get(0)`. The source gate
retains every call and refuses **0/5 native** with no owner proof because the
last-write fact forgets key 0. Fresh Node/interpreter runs both produce
`trace=1`. Extend to bounded per-key contents with independent key-disjointness
proofs and conservative invalidation for possibly aliasing writes/deletes.
Unseeded gets remain refused; string/boolean/mixed Map payload carriers remain
separate **0/6** boundaries despite complete host proofs. Full Bootstrap Data,
realm ownership and future external callers remain unfinished. Source invocation
integration next needs complete native call-component admission and owning
payload/state emission. The array query needs supported exposure/indirect
retention consumers before it can change escape verdicts.

## Preceding Map-result and invocation-effects checkpoint, 2026-09-07

Saved locally on `ctcompile-v1`: **`0977572`** (live invocation effect validation)
and **`c18b94b`** (published Map arguments from independent call results). Main
implementation and two parallel agent workstreams were integrated. The escape
contents agent hit a service rate limit before implementation; no contents
proof or escape verdict changed. No browser files were changed and no push was
performed.

The [published Map boundary](native-owned-global-maps.md) now accepts
`host.slot.set(host.slot.get())`, advancing **0/5 -> 5/5 native** and retaining
Node/interpreter `trace=1`. The initial getter, setter mutation and final getter
all remain runtime calls. The existing manifest, standard Map identity, owning
Map environment and typed callable carriers remain required. A complete current
call census precedes a bounded dependency worklist. Only a completed producer
body/effect/use proof publishes a primitive result tag; consumers declared
before producers wait for another pass. No recursive property-call query or
optimistic tag seeds a cyclic family. `Map.get` has no definite result tag here.

The native gate passes **34 complete programs**: fourteen **4/4**, fifteen
**5/5**, five **6/6**, comparing Node/interpreter and explicit/deduced GCC 13 and
Clang 18. Nine new variants cover nested results, reversed member order, aliases,
repeated calls, two mutating actuals, boolean results, an owning string and a
formal return. The argument-order witness observes **3**, while reversed actuals
observe **4**. Generated C++ checks preserve producer SSA operands and runtime
call order; linked binaries contain no interpreter symbols. All five existing
lifetime variants pass ASan/UBSan, use-after-scope/return and leak checks in both
forms. Nine new result-proof refusals retain all original calls. The additional
undefined-result case proves ownership but refuses the unsupported Map key at
**0/6**, retaining its prepared calls and actual-result edges.

The focused gate passes **3/3 CTests in 7.50 seconds**, followed by the complete
native program/lifetime/refusal gate. Log: `/tmp/ctcompile-map-results-integrated2.log`.
Source/prepared result proofs check every incomplete budget at **4789/4656**,
including live producer-return mutations and stale/fresh fingerprints.
Two-method, three-method and parameterized owner units complete at
**2877/2785**, **6603/6418** and **2883/2792**. First complete source admission
budgets are **1239/50004/1353/3297/3261/5336**, with **31/31/31/29/32/29**
cutoffs for ordinary, sixteen-call, growing, shared-growing, parameter and result
specimens. No natural speculative rollback interval is reached; no performance
improvement is claimed.

The parallel [source invocation increment](native-source-invocations.md) adds
`EffectCheckedInvocations` to the existing recovery transaction. It validates
normal/catch operations while their original status edges still exist, before
adopting a clone. Primitive predecessor proofs include both edges to a shared
successor and reject unknown alternatives. An unconditional numeric result is
not a nonthrowing producer proof. The unit passes thirteen live effect mutations,
all **924** incomplete work budgets, exact completion and exact rollback. The
three imported direct-call fixtures retain their original checks because their
global callee lookups lack independent live binding/getter proofs. Default
recovery and ordinary native throwing-call admission remain unchanged.

The full frozen generated devbox build passes **475/475 CTests in 674.94
seconds**: **367 compiler / 108 browser**, including **163/163 lit cases in
113.29 seconds**. Log: `/tmp/ctcompile-map-results-full.log`. Post-gate evidence
collection was rerun successfully with CMake's recorded Node executable after
the SSH environment lacked `node` on PATH; the CTest run itself passed.
All nine committed source/test/exception-document paths byte-match the frozen
gate. Browser bytes are the committed `ce728c0` baseline (unchanged since
`7a755dd`), excluding Claude's unmerged WPT branch and ten browser carryover
paths. Compiler formatting passes; the whole-tree check flags only the known
untouched browser `style/selector.hpp`, `DOM/document.cpp` and `Style/css/selector.cpp`.

Fresh native component counts remain **19/574 Bootstrap**, **39/4754 p5** and
**45/7725 Phaser**, in both optimization modes with zero pruned functions.
These are component counts, not complete native applications; exact Data stays
**0/7** per mode. Evidence: `/tmp/ctcompile-map-results-evidence.json`.
The emitted `/tmp/ctcompile-map-results-parameter.cpp` was reviewed: a typed
numeric getter result is passed to the runtime setter, and both callables own
the same Map. No VM context or collector appears.

**Exact next native boundary:** `result_seeded_map_get` changes the producer to
`get() { state.set(0, 1); return state.get(0); }`, keeping `set(get())` and the
same five functions. The fresh gate records **0/5 native**, Node/interpreter
`trace=1`, every original call retained, and no owner proof. Current named
refusals include `uses its own closure` and an unproved boxed parameter.
Add independent live contents, presence and result-type evidence
before admitting the consuming formal. Neither a completed ownership proof nor
an observed first-call value supplies that evidence. The unseeded Map.get result
also remains refused. Nullable Map keys within one method remain a separate
**0/4** carrier boundary. Exact Bootstrap Data, realm ownership, future external
callers, complete contents analysis and full native application startup remain
unfinished. Source invocation integration next needs live callee-binding effects,
the complete native call component, and owning payload/state emission.

## Preceding typed-Map and provenance checkpoint, 2026-09-07

Saved locally on `ctcompile-v1`: **`84c4b89`** (bounded load provenance),
**`ce2fd8a`** (primitive actuals for published Map methods), **`f81d803`**
(checked source invocation recovery prerequisite), **`f188373`** (typed argument
execution/lifetime gate), and **`c7a3569`** (native boundary documentation).
Three disjoint agent workstreams were integrated and validated. No browser
files were changed and no push was performed.

The [published Map boundary](native-owned-global-maps.md) now accepts
`set(key) { state.set(key, 1); return state.size; }` beside a zero-argument
getter. Calling it with `"x"` advances **0/5 -> 5/5 native**, retaining
Node/interpreter `trace=1`, under the fingerprinted manifest and standard Map
identity. The family census discovers all current calls before checking bodies,
independently classifies every actual, and retains each live SSA operand and
formal with a consistent primitive tag. The prepared Map environment precedes
explicit arguments. Existing typed owners and `std::function` carriers suffice;
there is no new runtime model, VM dependency or future-call ABI authority.

The expanded Map gate passes **25 complete programs**: fourteen **4/4**, ten
**5/5**, one **6/6**, matching Node/interpreter and explicit/deduced GCC 13 and
Clang 18 binaries. Six new variants cover string/number/boolean keys, repeated
distinct actuals, a local alias and two parameters. A fifth lifetime variant
retains typed string setter/getter callables after root/table release, changes
caller buffers, checks **1024 further mutations each** against independent
saved/fresh Maps, and observes destruction through weak witnesses. Both forms
pass ASan/UBSan, use-after-scope/return and leak checks. Ten argument refusals
retain every call operand. Missing/extra/mixed actuals, objects, callbacks,
uncalled parameterized siblings, global initialization defects and circular
sibling-result proofs remain refusals.

The focused gate passes **8/8 CTests** in **14.10 seconds**, followed by all
twenty-five native programs and five lifetime variants. Log:
`/tmp/ctcompile-arguments-integrated-focused4.log`. Every one of the **17
committed source/test paths** byte-matches the frozen full-gate snapshot.
The full frozen generated devbox build passes **475/475 CTests** in **652.00
seconds**: **367 compiler / 108 browser**, including **163/163 lit cases** in
**91.45 seconds**. The browser source is the committed `e90db7d` baseline
(the same browser tree content as `7a755dd`), excluding Claude's unmerged WPT
branch and live carryover. Log: `/tmp/ctcompile-arguments-full-gate.log`.
All compiler files pass formatting; the whole-tree check flags only the same
untouched browser `style/selector.hpp`, `DOM/document.cpp` and
`Style/css/selector.cpp`. The ten browser carryover paths remain untouched.

Measured first complete admission budgets are **1236/49476/1350/3285/3249** for
ordinary, sixteen-call, growing, shared-growing and parameterized shared
specimens, checking **31/30/32/29/33** cutoffs including the sixteen immediately
below completion. No natural speculative rollback interval is reached. The
new census increases proof work; no performance improvement is claimed.
Source/prepared owner units check every incomplete budget at **2865/2773**
(two methods), **6576/6391** (three) and **2871/2780** (parameterized).

The parallel [load-provenance query](escape-load-evidence.md) propagates
diagnostic candidates through local contents, repeated loads, loaded storage
targets, successor operands and loops. It keeps all external alternatives and
every later sink even after the first escape verdict. Work exhaustion and
incomplete inputs remain separate markers. **209 unit rows** and all four
execution oracles pass with zero violations. Bootstrap covers **2946 reads /
15551 sink operands**, **400 stored-site / 624 exposure-site edges**, and zero
newly propagated exposure edges; all **588** supported graphs converge, while
**102** have incomplete inputs. p5 and Phaser gain **324/52** propagated exposure
edges. No load lattice, escape verdict or native admission is changed. Complete
data-property/contents and indirect-retention proofs are still required.

The internal [checked invocation recovery mode](native-source-invocations.md)
now connects normal and unwind invoke completions to the enclosing try using a
value-only tuple. It preserves pre-call state and publishes assignments only on
normal return. The structural unit passes three direct-call fixtures with
**one/two/one invokes** and **seven/ten/nine original checks**, including saved
state, argument mutation, malformed edges, fallible prefixes, budgets and exact
rollback. The default mode and all ordinary native throwing-call refusals stay
unchanged. Live effect admission for other status edges, a complete native call
component and owning exception emission remain prerequisites. The four-source
execution/import regression still retains **16 functions / 11 observations**.

Fresh full native component counts remain **19/574 Bootstrap**, **39/4754 p5**,
**45/7725 Phaser** in both optimization modes, with zero pruned functions. These
are component counts, not complete native applications. Corpus, unit and budget
evidence: `/tmp/ctcompile-arguments-evidence.json`. The emitted setter/getter
C++ was reviewed in `/tmp/ctcompile-arguments-parameter.cpp`: a typed string
argument reaches the Map mutation at runtime and both methods own the same Map.

**Exact next native boundary:** the retained `parameter_call_result` source
calls `host.slot.set(host.slot.get())`, preserving the same five functions.
A fresh devbox check measures **0/5 native** and Node/interpreter `trace=1`,
with every original call and named refusal retained. Current reasons include
`uses its own closure` and an unproved boxed parameter; the failed owner
proof supplies no typed-call authority. Evidence:
`/tmp/ctcompile-arguments-boundary.json`.
Classify the producing call's result from independent live result/effect
evidence before admitting the consuming formal. Do not recurse through the
family's own property-call query as authority. Preserve the initial getter,
setter mutation, final getter, publication and their runtime order.

Nullable Map results used as keys remain a separate **0/4** presence/carrier
boundary; exact Bootstrap Data remains **0/7** per mode. Object keys/payloads,
realm ownership, future external callers, source throwing-call admission and
complete contents analysis remain unfinished. Full native Bootstrap is not yet
an executable native application.

## Preceding shared-Map checkpoint, 2026-09-07

Saved locally: **`c7a849c`** (recovered Map iterator correction), **`fec186e`**
(iterator documentation), **`296cf33`** (shared published Map methods),
**`e41893a`** (direct-load evidence), **`78c2b15`** (source invocation-state
regression), **`881c434`** (boundary checkpoint), and **`f339fe1`** (JSON
signed-zero inventory witness). All 28 inherited compiler changes were
recovered and split by concern; browser carryover was left untouched. No push
was performed.

The next [published Map boundary](native-owned-global-maps.md) is implemented
with a fingerprinted host manifest and explicit standard Map identity:
two zero-argument methods sharing one mutable Map advance **0/5 -> 5/5 native**;
a three-method table admits **6/6**. Complete live proofs follow every captured
closure, fixed publication field, primitive body and current call. They require
one Map identity and the exact source function chain. Preparation validates all
methods before lifting and unboxes the shared cell after every member. It uses
existing typed owners and callables, with no interpreter/collector dependency.
The shared specimen stays **0/5** without the manifest or Map identity.

The integrated lit run passes the nineteen-program shared-Map gate:
fourteen **4/4**, four **5/5** and one **6/6** programs match Node/interpreter
and explicit/deduced GCC 13/Clang 18 binaries. A fourth lifetime variant retains
setter and getter independently after root/table release, churns 4096
allocations, reenters with a distinct Map and performs 1024 further mutations
and reads. ASan/UBSan, use-after-scope/return and leak checks pass in both forms;
weak witnesses confirm destruction at the last callable release.

The parallel [direct-load evidence](escape-load-evidence.md) links live property
reads to direct writes sharing known local allocation sites. Links retain
other keys, later writes and repeated dynamic instances, and do not change load
result lattices, escape verdicts or native admission. Four corpus claims gates
pass with zero oracle violations. The unit increment adds twelve rows and two
live read-base mutations.

The [source invocation gate](native-source-invocations.md) retains four programs,
sixteen source functions and eleven observations for pre-call assignment state,
a prior normal call, argument mutation and receiver/key/getter/argument order.
Source recovery must connect invoke continuations to the enclosing try
completion and preserve other status edges until effect admission; wrapping the
call alone or removing the explicit-throw guard is insufficient.

The focused devbox gate passes **8/8 CTests** in **79.86 seconds**, including
**163/163 lit cases**, all **200 escape unit rows**, and the four corpus escape
oracles with zero violations. Both proof-unit and source regression corrections
were rebuilt and rerun. Log: `/tmp/ctcompile-native-integrated-focused2.log`.
All compiler files pass the formatter; the whole-tree check flags untouched
browser `style/selector.hpp`, `DOM/document.cpp` and `Style/css/selector.cpp`.
A stale header ABI in the first build was fixed by refreshing frozen compiler
source/header timestamps so every dependent object rebuilt. The full frozen
build then passes **474/474 CTests** in **622.05 seconds**: **366 compiler** and
**108 browser** tests, including **163/163 lit cases** in **70.01 seconds**.
All fifteen previously failing Map/snapshot/partial-evaluation/deforestation
execution checks pass. The browser baseline is committed `7a755dd` tree content;
Claude's unmerged WPT work is excluded. Log:
`/tmp/ctcompile-native-integrated-full-gate.log`.

The measured first complete native admission budgets are **1031** (ordinary),
**8516** (sixteen calls), **1132** (growing) and **1906** (shared growing), with
**32/30/31/33 cutoffs** checked. Every specimen preserves its source through the
sixteen budgets immediately below completion. No natural speculative-clone
rollback interval is reached. Shared source/prepared owner units independently
check every incomplete budget: **1582/1521** for two methods and **2367/2279**
for three methods.

The full-run load census covers **2946 Bootstrap property reads**, with **138
candidate links across 48 reads**, zero linked stored-site edges and zero
invalid links or unresolved bases. It classifies **486/588 functions complete**,
with 102 partial; these are census markers, not contents or confinement proofs.
The existing storage census remains **2611 writes / 400 site edges**. p5 has
**3047 links / 754 linked reads**, Phaser **7425 / 571**. All four execution
oracles report zero violations. Full native component admission remains
**19/574 Bootstrap**, **39/4754 p5**, **45/7725 Phaser** in both optimization
modes, with zero pruned functions. These are component counts, not whole native
applications. Evidence: `/tmp/ctcompile-native-integrated-evidence.json`.

The incoming runtime JSON parser correction makes the old malformed-input
inventory witness agree with Boost. `f339fe1` uses `JSON.parse('-0')` instead:
the VM preserves `8000000000000000`, while Boost's integer parsing produces
`0000000000000000`. The separate devbox inventory check passes **1/1** in
**0.01 seconds**, retaining **35 rows / 50 probes** (**29 agreements / 21
expected divergences**). No runtime or native JSON admission changed. Log:
`/tmp/ctcompile-native-json-inventory-gate.log`.

**Exact next native boundary:** the shared setter takes one key parameter,
`set(key) { state.set(key, 1); return state.size; }`, while the getter stays
zero-argument. Called with `"x"`, this retained source measures **0/5 native**
and Node/interpreter `trace=1`. Discover every current method call before
checking the family bodies; independently classify actuals, keep their SSA
operands, and record per-call formal/actual evidence plus per-method primitive
parameter tags. `HostContract/Values.cpp` currently rejects explicit indirect
actuals, assumes three source/four prepared arguments and excludes parameters
from its primitive-body proof. Existing capture lifting already prepends the
Map environment before explicit arguments. The proof must not authorize itself
through property-call recursion or turn one startup value into future-call
permission. A typed external ABI is still a separate obligation.

Nullable Map results used as keys remain a separate **0/4** carrier/presence
boundary. Exact Bootstrap Data remains **0/7** per mode. Full native Bootstrap,
source throwing-call recovery/admission/emission, and complete contents/points-to
propagation through loads and indirect exposures remain unfinished.

## Preceding native Map-effects checkpoint, 2026-09-07

Five work commits are saved locally on `ctcompile-v1`: **`e52897f`** (complete
direct storage census), **`4b36cd1`** (checked invocation normal-return flow),
**`6642912`** (live primitive Map effects), **`a6b0807`** (native mutation and
lifetime gate), and **`0510988`** (Unicode trim inventory correction). No push
was performed. Fifteen dirty compiler files and one untracked document from the
interrupted loop were recovered and split by concern; browser edits were not
included.

The [published Map method](native-owned-global-maps.md) now supports standard
`size`, `set`, `get`, `has` and `delete` over a completely checked primitive
content/use graph. The `state.set("x", 1); return state.size` specimen advances
**0/4 -> 4/4 native** with a fingerprinted host manifest and standard Map
identity, retaining runtime mutation and `trace=1`. The original default and
no-intrinsic modes remain **0/4**. Source allocations, all four functions,
wrapper/factory calls and publication remain runtime operations.

Fourteen **4/4** programs match Node, the interpreter and standalone GCC 13 /
Clang 18 explicit/deduced binaries with no VM symbols. Ordinary, mutating and
growing methods pass owning-lifetime ASan/UBSan, use-after-scope,
stack-use-after-return and leak checks in both forms. Saved callables survive
root/table release, and reentry creates distinct Map owners. The growing method
mutates on every call; saved and fresh environments retain independent sizes
through **1024 further calls each**. Thirty source refusals, three independent
carrier refusals and contract/rerun controls pass.

The first complete admission budgets are **1012** (ordinary), **8257** (sixteen
calls) and **1113** (growing), with **31/31/32 cutoffs** checked. All sixteen
budgets immediately below each cutoff preserve the source graph. None exposes
a naturally reached speculative-clone rollback interval; existing scalar/table
rollback controls remain. Owning a primitive Map does not prove a supported
native key/value/result carrier.

The escape increment records every direct `Stored` operand, including later
stores and values whose first sink was different, with stored-value and target
aliases. Unsupported targets, nested regions and whole-frame refusals preserve
partial evidence with `complete=false`. No escape verdict is weakened and no
native consumer uses this census as a contents proof. All **188 unit rows**,
fixture and Bootstrap oracle gates pass, with zero violations. See
[direct storage evidence](escape-storage-evidence.md).

The invocation increment joins only the protected helper's normal return
operands after a fresh bounded completion query. Passed arguments and SSA joins
retain widening dependencies; transitive callees' returns do not join the outer
result. Fifteen added rows and five live-mutation checks pass, including forged
nothrow markers, unknown effects, recursion and exhausted work. Ordinary calls
remain conservative on throw exits. Source recovery/admission/emission still
need to consume the explicit invocation regions; the explicit-throw guard is
unchanged. See [native exceptions](native-exceptions.md).

Claude's `61416fc` makes JavaScript trim Unicode WhiteSpace/LineTerminator
characters. The compiler inventory now marks both the ASCII helper and Boost
candidate divergent, with NBSP/BOM witnesses and a pinned Boost locale. The
focused test passes **35 rows** (**19 exact / 14 divergent / 2 refused**) and
**50 probes** (**29 agreements / 21 expected divergences**). No runtime code or
native string admission changed.

The focused devbox build and **6/6 CTests** pass in **1.37 seconds**, followed
by the complete fourteen-program native gate. The trim CTest passes **1/1** in
**0.01 seconds**. Logs are `/tmp/ctcompile-map-effects-recovery-focused3.log`
and `/tmp/ctcompile-map-effects-recovery-full-gate.log`.

The frozen `86df9b1` full build succeeded; CTest measured **454/474** in
**605.43 seconds**, including **161/161 lit cases**. Five failures belonged to
that older browser snapshot. Fifteen compiler differential failures exposed
raw `Map.keys()`/`values()` being treated as arrays after runtime `e6c77fc`
changed them to iterator objects. That is a compiler boundary to fix, not a
reason to reverse the runtime correction.

The follow-up in `NativeMap/SnapshotCopies.cpp` requires each iterator to have
one proved `Array.from` argument use in the same block, with only constants,
root bookkeeping and proved Array builtin lookups between them. Direct reads,
publication, mutation, repeated consumption and consumption across effects
refuse. The six execution fixtures now explicitly materialize their intended
arrays without changing observations. Partial evaluation recognizes only the
freshly proved builtin/copy environment; actual snapshot evaluation stays
runtime work. That interrupted correction is now saved as `c7a849c`; the
current checkpoint above records its ten-source regression and passing full
generated gate. No browser sources were edited.

`7a755dd` records the full-run direct-storage corpus census: Bootstrap has
**2611 writes / 400 site edges**, with **29 multiple-store** and **17
other-first-sink** sites. All first Stored witnesses are covered and execution
oracles report zero violations. Both optimization modes still measure native
**19/574 Bootstrap**, **39/4754 p5**, and **45/7725 Phaser**. These are component
admission counts, not whole-program native compilation. Compiler formatting and
whitespace pass; the whole-tree formatter flags only browser files left in the
shared checkout. Historical full-suite counts below predate the iterator
correction; use the current checkpoint above for its validation.

**Exact next native boundary:** two zero-argument published methods sharing the
captured mutable Map. The retained setter/getter specimen measures **0/5
native**, while Node and the interpreter both produce `trace=1`. The live cell
census currently accepts capture by only the selected closure, and the owner
query requires one method and four functions. Extend both proofs to all methods
and their shared owner before Data's parameters/results. Separately, a
`Map.get` result used as a key remains nullable and refuses **0/4** despite
complete ownership and Node/interpreter `trace=1`; it needs presence/type
proofs. Full native Bootstrap remains unfinished.

**Parallel next boundaries:** recover importer throwing calls into invocation
regions and connect both checked completion flows to admission and C++ emission.
Escape precision still needs contents/points-to propagation through loads,
indirect transfers and every exposure before weakening `Stored`.

## Preceding native captured-Map checkpoint, 2026-09-07

Five work commits are saved locally on `ctcompile-v1`: **`357ba41`** (captured
Map source ownership), **`5f54358`** (direct storage-target diagnostics),
**`3c9d402`** (the actual imported indirect factory callback), **`fcfc55b`**
(explicit invocation completions and payload type flow), and **`3c04ccc`**
(native owning Map publication). No push was performed. The abandoned loop's
18 dirty compiler files were recovered, reviewed and split by concern; no
browser edits were taken into these commits.

The [captured Map publication specimen](native-owned-global-maps.md) advances
**0/4 -> 4/4 native** with an explicit fingerprinted host manifest and standard
Map identity. Its default and no-intrinsic modes remain **0/4**. The four source
functions, runtime Map allocation, wrapper/factory calls, publication and size
read remain. The real importer leaves `factory()` indirect inside its wrapper;
the complete source proof now follows that actual callback before any native
preparation. Exact public/framed imported IR is a unit fixture alongside the
direct and prepared variants.

Preparation validates the original fingerprint and reconstructs native facts
on a disposable clone. Existing callback specialization, capture lifting and
cell unboxing connect the immutable environment to shared Map/table/root owners
and an owning callable. Complete live proofs are required after each changed
graph and again at final admission. Standard Map preparation may pass only
ordinary-root reads authorized by that live owner proof. Global publication
remains `StoredGlobal`; general global loads stay external.

Six **4/4** programs match Node, the interpreter and standalone explicit/deduced
GCC 13/Clang 18 binaries with no VM symbols. Both forms pass ASan/UBSan,
use-after-scope, stack-use-after-return and leak checks. The lifetime harness
retains root, table and callable separately after entry, releases globals,
churns allocations and invokes the saved callable after root/table release.
Weak witnesses observe the actual captured Map: reentry creates a distinct Map,
and each expires when its last callable owner is released. Twenty-two source
refusals plus missing/stale/forged contracts, reruns and work limits pass.

Native admission first completes at **1009 steps** for the ordinary specimen
and **8209** for sixteen getter calls. Each gate checks **33 cutoffs**, including
the sixteen immediately below completion, preserving source operations and
signatures after failure. Neither fixture naturally reaches an interval where
the original proof succeeds but the clone proof exhausts; do not claim a new
Map-specific speculative rollback execution. Existing scalar/table rollback
controls remain. The ownership unit independently checks every incomplete
budget for all five fixtures: **861**, **885**, **1009**, **848** and **581 steps**
for direct source, indirect source, exact imported source, lifted and specialized
forms respectively.

Escape diagnostics now classify the direct storage target of each first
`Stored` witness as local-confined, local-escaping, local-mixed,
external-or-mixed, primitive or unresolved. This is evidence for the contents
backlog, not a points-to proof: a confined packing array can expose its children
to a spread callee, and later stores can retain elsewhere. Every existing
escape verdict stays unchanged. The unit passes **173/173 rows**, and fixture
and Bootstrap oracle gates pass with zero violations. The fixture's **12**
first-witness sites split into **2** local-confined, **6** local-escaping,
**2** external-or-mixed and **2** unresolved. Bootstrap's **170** split into
**93** local-escaping, **13** external-or-mixed and **64** unresolved, with no
local-confined target. p5 has **1304** sites (**25** local-confined); Phaser has
**1861** (**12** local-confined). These diagnostic counts do not license any
new confinement claim.

The parallel exception increment adds `ctjs.invoke`, `invoke_exit` and
`invoke_yield`. The normal continuation alone receives the call result; the
unwind continuation receives an implicit payload and explicit pre-call state.
The verifier rejects intervening work, result use outside normal dispatch and
state defined inside the invocation body. A fresh payload query follows live
explicit throws and direct callees, bounded to **4096 operations / 32 helpers**;
unknown effects remain boxed. Thirteen inference rows, two round-trip forms
(including zero results/state) and eleven malformed-IR controls pass. Source
throwing calls are still unsupported; the existing native exception suite
remains **52/52 functions and 39 observations**.

The full serialized devbox build and gate pass **471/471 CTests** in
**566.41 seconds**: all **366 compiler tests** and **105 browser tests**, including
**161/161 lit cases** in **50.33 seconds**. At the implementation gate,
all **575 C++ files** pass `tools/format.sh --check`; whitespace checks pass.
Native coverage with optimization disabled remains Bootstrap **19/574**, p5
**39/4754** and Phaser **45/7725**, with no pruned functions.

The final formatter run still passes every compiler file and flags only Claude's
new live edit in `ctbrowser/lib/Script/builtins/internal.hpp`. It is journaled
and left untouched; the log is `/tmp/ctcompile-map-recovery-final-format.log`.

The evidence audit found a source timestamp race in `OwnedGlobalMethods.cpp`:
the exact imported fixture arrived via rsync after its object was built, but
retained an earlier mtime, so Ninja skipped the new unit body. Refreshing only
that source timestamp and rebuilding runs all five fixtures successfully;
related ownership/host/type CTests pass **4/4** in **0.34 seconds**. The other
changed compiler `.cpp` objects postdate their remote source arrival. No code
change was needed. When editing during a remote build, inspect or refresh the
modified source timestamp before trusting the next incremental build.

Logs are `/tmp/ctcompile-map-recovery-full-gate.log`,
`/tmp/ctcompile-map-recovery-exact-unit.log` and
`/tmp/ctcompile-map-recovery-format4.log`; measured budget, corpus and storage
evidence is `/tmp/ctcompile-map-recovery-evidence.json`. Compiler changes are
committed; Claude's new browser work remains independent in the shared tree.

**Exact next native boundary:** the published getter currently permits only
`state.size`. The retained refusal specimen first executes
`state.set("x", 1)` and then returns `state.size`: **Node/interpreter `trace=1`,
native 0/4**.
Extend the complete callable/environment proof to supported standard Map
operations and their effects before widening to Bootstrap Data's multiple
methods and arguments/results. Existing native Map/type machinery is already
available; completed startup summaries cannot authorize later calls. Exact Data
remains **0/7** per CommonJS/browser/realm-fallback mode. Typed exports, mutable
publication slots, general realm owners, reentry and full initialization remain
unfinished.

**Parallel next boundaries:** recover source throwing calls into the new
invocation regions, add a checked normal-return transfer, and connect admission
and emission to the existing target exception contract. Upstream ordinary call
inference stays conservative when a callee has throw exits, independently of
the new payload proof. Do not relax the explicit-throw guard. Escape precision
still requires complete contents/points-to evidence before weakening `Stored`.

## Preceding native exported-getter checkpoint, 2026-09-07

Three work commits landed locally on `ctcompile-v1`: **`8455458`** (nested-region
escape retention), **`fecb9af`** (exception payloads through defined EmitC
helpers), and **`be76faa`** (native owning global method tables). No push was
performed. The incoming uncommitted escape work was completed and saved first.

The [exported constant getter](native-owned-global-methods.md) advances
**1/3 -> 3/3 native** with an explicit `host-manifest`; its default remains
**1/3**. The existing live source graph now connects fixed field stores and
loads in the returned-table census. Narrow preparation preserves the real
receiver, then global field types, whole-component admission and emission use
the existing shared owner/table and owning callable carriers. No runtime or
escape semantics changed: publication is still `StoredGlobal`, and general
global loads remain external.

Preparation validates the original manifest before working on a clone. It
rebuilds native source-operation facts, prepares only the checked uncaptured
table, and requires a complete live proof of the transformed graph before using
it. Final admission rebuilds the proof again. This also fixes stale
`ctnative.method` annotations that could otherwise erase a real scalar/table
field initialization or observation store. Fresh-fingerprint execution controls
cover both owners; a stale manifest cannot enter preparation.

Eight complete **3/3** getter variants match Node/interpreter and explicit/deduced
GCC 13/Clang 18 output without VM symbols. Owner, table and callable are retained
independently after entry; global release, allocation churn, reentry, distinct
identity, weak expiry and invocation after table release pass lifetime checks.
Both forms pass ASan/UBSan, use-after-scope, stack-use-after-return and leak
checks. Twenty-four source refusals, four unsupported observation-result types,
stale/forged/rerun controls and the unchanged default boundary pass. The scalar
gate now covers **eight 1/1 programs**, including both stale-marker controls.

The imported getter needs **499 proof steps** before preparation and **510**
afterwards. Budgets **499 through 509** discard the speculative clone without
leaking source allocation/field/call rewrites and retain **1/3** admission.
These counts differ from the smaller handwritten query unit, whose completed
budget remains **361**; the scalar unit remains **164**.

[Native exception target validation](native-exceptions.md) now follows defined
`emitc.call` helpers and requires homogeneous escaping number, boolean or owning
string payloads to agree with the surrounding catch. Locally caught throws do
not escape; catch rethrows do. Each verification uses a fresh bounded query
(4096 operations, 32 active helpers), refusing unresolved/external definitions,
recursion, mismatches and exhausted budgets. Opaque calls retain their existing
foreign-exception contract. Five positive and 13 refusal controls pass, along
with explicit/deduced/hoisted GCC/Clang execution and owning string sanitizers.
The fixture checks pre-call state, normal-return-only assignment publication,
exact-once cleanup, local catch/rethrow, negative zero and foreign exceptions.
This is a target prerequisite; **source throwing calls are still unsupported**.
The existing source suite remains **52/52 functions and 39 observations**.

Escape analysis now sinks implicit nested-region captures and retains
whole-frame suspend/late-arguments refusals even without explicit SSA operands.
Nested allocation sites receive no CFG-only confinement verdict. Twenty-one
additional rows bring the unit to **157/157**; escape unit, fixture oracle and
Bootstrap oracle CTests pass **3/3** in **0.59 seconds**.

The complete compiler rebuild and CTest gate pass **366/366 tests** in
**547.65 seconds**, including **159/159 lit cases** (lit **48.54 seconds**).
The shared full devbox build then succeeds, and its CTest run passes
**468/471 tests** in **565.53 seconds**. All **366 compiler tests** pass again
against the updated runtime, including all **159 lit cases** in **48.50 seconds**.
The three failures are in Claude's active browser work: `bindings_basics`
(`isTrusted` own accessor), `bootstrap_layout` (computed-style baselines), and
`property_attributes` (old builtin name/length expectations). They are journaled
in `AGENT-SYNC.md`; no browser files were edited by Codex. The initial unused
lambda capture build failure was fixed by Claude in **`3d30c82`**.

All **574 C++ files** pass the final `tools/format.sh --check`; compiler whitespace
checks pass. Logs are `/tmp/ctcompile-native-session-compiler-gate.log`,
`/tmp/ctb-build4.log` and `/tmp/ctcompile-native-checkpoint-final-format.log`.
The monorepo CTest gate is not wholly green while those browser failures remain.

**Exact next native boundary:** extend the complete live callable/source-owner
graph to the immutable captured Map environment through wrapper return and
global publication, preserving allocation identity and shared ownership.
Connect that proof to existing Map/capture/table types and final component
admission. The Map publication specimen remains **0/4**; its **4/4** native gate
is proposed. Exact Bootstrap Data stays **0/7** in each CommonJS/browser/realm
fallback mode. Prefix completion cannot authorize future callers. Typed export
ABI, mutable slots and general realm owners remain further work. Full native
Bootstrap is unfinished.

The next proof must preserve the specimen's four-function entry/wrapper/factory
chain; current host calls allow only literal uncaptured getters and current
owners require entry-local publication in three functions. After extending
those proofs and capture preparation, `prepareNativeMaps()` must also consume
proved owning-root reads: its standard-builtin check currently refuses every
other host/global value read. Enabling Map preparation alone is insufficient.
See [the exact implementation boundaries](bootstrap-provider-next.md).

**Parallel next boundaries:** represent source throwing calls with an explicit
exceptional call edge carrying the pre-call register vector and an owning
payload, then use the now-tested target helper contract. Publish assignment
results only on normal return; do not relax the current explicit-throw guard.
General source handlers, uncaught entry adapters and mixed/object payloads
remain separate. Escape precision still needs contents/points-to evidence before
weakening retained elements' `Stored` verdicts. See
[the Bootstrap boundary](bootstrap-provider-next.md).

## Preceding checked-getter and protected-helper checkpoint, 2026-09-07

Five work commits landed locally on `ctcompile-v1`: **`688461c`** (spread escape
lifetimes), **`fd756f9`** (checked native helper callees), **`12c1b6b`** (deferred
generator invocation refusal), **`94024fb`** (current host getter proof), and
**`2772cd6`** (fixed global method-table source ownership). No push was performed.

[Current host getters](native-host-callables.md) now expose live
`HostCallableEdge` records for an uncaptured literal-return getter's actual
call, property read, preceding initialization, source closure and function.
Both indirect and already resolved calls preserve their receiver and operands.
The complete contract validates source-program provenance, undefined lexical
receiver, unused implicit arguments and effect-free literal bodies. Its unit
finishes at **295 charged steps**; all smaller budgets withhold slot and call
edges. Eight positive and 22 refusal programs pass, including replacement,
receiver/capture/effect controls, stale/forged contracts and generators/async.

The [owning source graph](native-owned-global-methods.md) connects the exported
constant getter's root, sole factory invocation, returned table, fixed method
and actual calls. It requires three straight-line functions and rejects extra
publications, allocations, factory invocations, schema extension and rewrites.
Its unit covers indirect/direct/repeated calls, detached tables/callables,
reordered initialization and semantic mutations; **361 charged steps** complete
the base query and **all 361 incomplete budgets** refuse atomically. The scalar
owner query now measures **164 steps**, previously 162, after host accounting
changes. `StoredGlobal` and external-global escape rules remain unchanged.

The exported getter stays **1/3 native** with and without `host-manifest`.
Explicit-manifest lowering reports a proved source owner but retains the
numeric-field and closure-value native refusals. Source allocation, publication,
property and call counts remain unchanged; Node/interpreter `trace=42` still
agrees. Reusing the original manifest after partial native lowering refuses its
changed fingerprint. No native table binary or new table lifetime result is
claimed. Captured Map publication remains **0/4**, and exact Bootstrap Data
remains **0/7** in CommonJS/browser/realm-fallback modes.

[Protected helper resolution](native-exceptions.md) follows branch successor
register vectors in a bounded query, independently checking every incoming
callee definition. It preserves `ctjs.check` status and exception snapshots,
including mixed-predecessor and work-exhaustion refusals. The exception gate now
passes **20 programs, 52/52 functions and 39 observations** across Node,
interpreter and explicit/deduced GCC/Clang. Numeric/string defaults and owning
string ASan/UBSan lifetime checks pass. Twenty-two source refusals, late effects,
forged/rerun reports, mixed incoming callees and a 2100-block budget control pass.

The callable review found an importer hole: a generator without `yield` looked
like an ordinary eager function. Import now refuses every generator invocation
until its deferred iterator semantics are represented, retaining skipped source
identities and global-store accounting. Ordinary async returns retain the
existing promise-wrapper refusal. This fix has its own importer regression.

The spread audit found no production escape solver defect. Seven added unit
rows bring the suite to **136/136**. The oracle records **106 observed sites,
103 claims, zero violations and 16 sound confined claims**. Six source/packing
arrays are confined while three literal elements/receivers are retained.
Constructor-created objects stay explicitly unclaimed; mutation controls reject
both unsound child confinement and unnecessary packing-array escape claims.

Serialized full devbox gate: **469/469 CTests**, **157/157 lit cases**, in
**555.03 seconds**. Final source-program provenance and extra ownership controls
pass a subsequent **4/4 targeted CTest gate**, including all **157 lit cases**,
in **44.31 seconds** (lit **44.24 seconds**). All **568 C++ files** pass formatting;
whitespace checks pass. Default/disabled native coverage remains Bootstrap
**19/574**, p5 **39/4754**, Phaser **45/7725**. Exact Data provider progress stays
24 resolved calls, 23 completed summaries and 19/19/24 matching observations.

**Exact next native boundary:** consume the now-existing live source graph in
`ClosedValueFlow` and the returned-method-table census, then carry the existing
owning table carrier through global field types, final call-component admission
and emission. The **3/3** standalone getter gate and post-entry lifetime checks
are still proposed. The manifest path skips closure lifting; any new preparation
must validate the original fingerprint and reconstruct proof for transformed IR
without silently refreshing a stale manifest. Do not rediscover these source
edges or treat their reports as native ownership permission. Captured Maps and
future-call/typed-export contracts follow. Full native Bootstrap is unfinished.

**Next exception boundary:** the throwing `fail()` target now resolves, but
recovery still says `native try/catch needs an explicit throw in its active
handler`. Add a call-region exceptional edge carrying pre-call register state
and an owning payload. Publish assignment results only after normal return;
relaxing the existing `throws == 0` guard cannot model unwinding before
`try_exit`. General finally/nested handlers, uncaught entry adapters and
mixed/object payloads remain separate work. Escape precision next needs a
contents/points-to proof before weakening the element's `Stored` verdict.

## Preceding scalar-owner checkpoint, 2026-09-07

Four more work commits are local on `ctcompile-v1`: **`c822d24`** (composed
global escape regressions), **`98df646`** (live ordinary-global owner query),
**`05b672a`** (closed primitive catch helpers), and **`91627e5`** (native owning
scalar global storage). No push was performed.

[Checked ordinary global owners](native-owned-globals.md) implement the scalar
export gate: `var host = {}; host.slot = 42; var trace = host.slot;` advances
**0/1 -> 1/1 native** with an explicit
`--ctnative-lower-to-emitc="host-manifest=driver.json"`. The manifest fingerprints
prepared IR and selects the root and observations. The live query requires a
complete host proof, one straight-line script, one fresh ordinary allocation,
one binding publication and one fixed numeric field initialization. It is
rebuilt for final admission. Source allocation/publication order remains;
generated global storage and loads own a `std::shared_ptr` to the concrete field
class. Owner storage is separate from driver-selected scalar observations.
`StoredGlobal` and external global-load escape semantics are unchanged.

Six complete programs admit **1/1 each**, with six selected observations matching
Node, interpreter and standalone explicit/deduced GCC 13/Clang 18 output.
Both generated forms pass ASan/UBSan, use-after-scope, stack-use-after-return
and leak checks. The harness retains the owner after entry returns and its
global is reset, churns allocations, runs entry again, checks distinct live
identities, then observes weak-owner expiry after release. Fifteen source
refusals plus stale/forged/rerun and budget controls pass. The live-query unit
completes at **162 charged steps**; all **162 incomplete budgets** refuse
atomically. Fingerprinting retains the host analysis's existing whole-module
hashing behavior. Explicit-manifest lowering preserves prepared source instead
of running default rewrites that would invalidate the fingerprint; callers
must prepare IR before creating the manifest. No report or type marker grants
ownership. Without this option the scalar export stays **0/1**.

[Native exceptions](native-exceptions.md#closed-catch-helper-boundary-2026-09-07)
now admit private primitive nonthrowing helpers in catch bodies, including
transitive calls and owning string arguments/results. The live effect query
is bounded to 4096 helper operations and 32 active helpers. The source gate
passes **16 programs, 38/38 functions and 31 observations** across Node,
interpreter and explicit/deduced GCC/Clang; numeric/string defaults and string
lifetime sanitizers pass. Twenty source refusals, late helper mutation,
forged/rerun reports, depth/work limits and prior recovery/wrong-state controls
pass. A helper loaded inside `try` still flows through `ctjs.check` register
vectors and remains unresolved; actual throwing callees are not implemented.

The escape audit found no production solver defect. Seven unit controls cover
global aliases, mixed fresh/external phi and loop flow, containment, and an
overwritten binding whose object remains globally retained through an alias.
Five added oracle sites make seven objects: five retained through globals and
two confined alternatives. The full oracle measures **91 observed sites,
89 claims, zero violations and ten sound confined claims**.

Final serialized devbox gate: **468/468 CTests**, including **156/156 lit
cases**, in **544.10 seconds**. Tightened boolean/string field refusal controls
also pass the full lit rerun (**36.79 seconds**). All **564 C++ files** pass
formatting; whitespace checks pass. Default and disabled native coverage stays
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725**. Exact Data probes remain
**0/7 native** in CommonJS/browser/realm-fallback modes; their existing provider
progress remains 24 resolved calls, 23 completed summaries and 19/19/24 matching
observations. Full native Bootstrap initialization is unfinished.

**Next native boundary:** connect the exported fixed field to its actual owning
table and current uncaptured getter, consuming live callable/environment proof
in closed value flow, method-table analysis and final admission. The measured
getter gate is still **1/3**; **3/3 is proposed**. Complete host analysis still
refuses callable/provider paths, and explicit-manifest lowering skips closure
lifting, so new preparation must preserve or reconstruct valid proof without
silently rebinding a stale manifest. Then address the captured Map export
(currently **0/4**) and future-call/typed-export contracts. See
[the exact next boundary](bootstrap-provider-next.md).

The parallel exception boundary is to preserve and resolve checked callee value
flow, then add an exceptional call-region edge carrying the pre-call register
snapshot and an owning payload type through the closed native component.
Publish an assignment result only on normal return. The current `try_exit`
cannot represent unwinding before that completion. An explicit uncaught-entry
adapter, general finally/nested handlers and mixed/object payloads remain
separate work; normal-return prefix facts do not authorize exception paths.

## Preceding object-payload checkpoint, 2026-09-07

Four work commits are local on `ctcompile-v1`: **`0a4a7c9`** (export-boundary
evidence), **`c358c1b`** (owning primitive exceptions), **`454a886`** (provider
object payloads), and **`6cab1a0`** (live owning-field query regressions).
No push was performed.

[Provider object payloads](native-provider-objects.md) add opt-in
`follow-provider-objects=true`, requiring publication/read/mutation following.
The exact CommonJS/browser/realm-fallback programs additionally enable
diagnostic/callback following. They now resolve **24 calls** and complete
**23 provider summaries**, up from 20 and 18. Each records **68 reads, seven
sets, three deletes, three distinct nested Maps, one callback and two global
writes**. The actual initialized `instance` survives Map storage and both
getter returns as one identity. Current scalar fields follow alias mutation,
replacement and named/computed deletion/reinsertion. Maps, objects and callback
globals commit together only after normal return; runtime effects remain.
Thirty-eight source cases, provenance/refusal controls and incomplete-budget
checks pass. All **19/19/24 exact observations** agree across Node, interpreter
and boxed script/wrapper execution, including GC stress. Source/vendor hashes
and the seven-function denominator are unchanged; native admission stays **0/7**.

CommonJS/browser prefix following reaches the end. The fallback completes every
Data call and then stops at the appended `scriptThis === this` observer:
`unproved comparison behavior at ctjs.compare`. It has no remaining provider
boundary. Realm comparisons and the following missing-own-property observer
reads remain outside this object proof; all runtime observations still pass.

[Native exceptions](native-exceptions.md) now own homogeneous number, boolean
or string payloads and catch state through `js_exception<T>`. Completed
scratch-register computations no longer block recovery, while every discarded
implicit exception edge still requires nonthrowing admission. Thirteen source
programs admit **27/27 functions** with **25 matching observations** in Node,
the interpreter and explicit/deduced GCC/Clang output. Numeric and owning-string
default-optimization checks pass. Source/target string lifetime tests pass
ASan/UBSan, use-after-scope, stack-use-after-return and leak checks. Thirteen
source refusals, six additional target-verifier controls and the prior budget
and wrong-state controls pass. Mixed/null/undefined/object payloads, throwing
callees and general finally/nested source handlers remain unsupported.

[Export-boundary evidence](native-export-boundary.md) isolates the next native
consumer: a scalar global root has a complete live host proof but stays **0/1**
native. An exported constant-getter table is **1/3**; a Map-backed table whose
startup prefix completes is **0/4**; the confined local control is **4/4**.
All four observations match Node/interpreter. Reports, forged annotations and
reruns never supply native ownership. The owning-field audit found no production
defect; its new unit controls rebuild an initially successful query after a late
rewrite, lost initialization dominance or owner escape, retaining an independent
valid owner and rejecting stale markers.

Final serialized devbox gate: **467/467 CTests**, including **155/155 lit
cases**, in **540.22 seconds**. All **559 C++ files** and the exception printer
include pass formatting; whitespace checks pass. Default and disabled native
coverage stays Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725**.

**Next native boundary:** implement an explicit owner for the checked ordinary
scalar global root, preserving `StoredGlobal` and external escape semantics.
Carry its live proof through type inference, global/field admission and emission;
keep owning storage separate from driver-selected observation globals. The
**0/1 -> 1/1** scalar gate is proposed, not implemented. Then connect one fixed
export field to its current uncaptured getter (**1/3 -> 3/3**, proposed), before
captured Map tables and future-call/typed-export proofs. See
[the exact next boundary](bootstrap-provider-next.md). Full native Bootstrap
initialization is unfinished. In parallel, exceptions next need a closed throwing
callee and pre-call assignment state, followed by an explicit uncaught-entry
adapter; prefix facts cannot authorize exceptional continuations.

## Preceding native checkpoint, 2026-09-07

[Provider diagnostics and callback effects](native-provider-diagnostics.md)
now extend the optional mutation prefix through checked Map.keys/Array.from
snapshots, string messages and the actual source recorder's scalar global writes.
Enable `follow-provider-diagnostics=true` and `follow-provider-callbacks=true`
alongside publication/read/mutation following. Both new options default off.
The exact CommonJS/browser/realm-fallback programs advance **13 to 20 resolved
calls** and **11 to 18 completed summaries**, with 52 reads, five sets, three
deletes, one callback and two global writes. All 62 Node/interpreter/boxed
observations agree, including GC stress. Provider Maps, callback globals and
reports commit together only after normal return; runtime bodies and effects
remain unchanged. Native admission stays **0/7** in each mode. The
[next provider boundary](bootstrap-provider-next.md) is an ordinary object
payload, followed by native ownership and call proofs for exported users.

[Native JavaScript exceptions](native-exceptions.md) now recover one acyclic
handler and emit real typed C++ throw/catch. The recovery preserves throw-site
register state, uses LLVM CFG-to-SCF without a fork, and requires numeric
payloads and proved nonthrowing primitive operations. Unsupported structure,
types/effects, call components and exhausted work retain the original CFG.
Mutable slots carry catch-visible state; copied catch bindings use the existing
const analysis. General throwing callees, nested handlers, finally completions
and foreign-call adapters require further work. A finally that already reduces
to an equivalent unconditional return can use the recovered completion shape.
Seven source programs admit 2/2 functions each, with fourteen observations
matching Node/interpreter and standalone GCC/Clang in explicit/deduced modes.
The guarded specimen also passes default optimizations. Eleven refusals,
zero/tight work limits, seventeen malformed-IR controls and an executed
wrong-state control pass. One refusal records the existing null-property
interpreter/Node discrepancy separately; it is never admitted as native.
Exception support remains separate from callback effects and exported ownership.

[Private Map mutation summaries](native-provider-mutations.md) now extend the
host prefix under `follow-provider-mutations=true`, requiring both publication
and provider-read following. A bounded transaction carries Map state across
completed method calls, including nested allocations and primitive/object keys.
The exact CommonJS/browser/realm-fallback probes advance **4 → 13 resolved
calls**, with eleven completed method summaries, 31 reads, five sets and one
unsuccessful delete. Two distinct nested Maps retain allocation/invocation
provenance. Method bodies, observer branches and runtime effects remain intact.
Without diagnostic following, the boundary remains the conflict arm's
`load_global "console"`; native admission remains **0/7** in each mode. All 62 Node/interpreter/boxed observations
agree, including GC stress. Thirty-eight focused cases and finite work-limit
controls check identities, rollback and refusal behavior. The option is off by
default and never falls back to the old empty-Map model after a mutation.

Callback lifting now accepts an already resolved `ctjs.call_direct` use when its
symbol matches the actual supplied closure. It preserves the call's receiver,
arguments and metadata while removing a proved call-only callback parameter.
The fixture admits 10/10 functions in indirect, resolved and mixed call forms,
with four matching observations in explicit/deduced GCC/Clang builds. Eight
proof refusals and two malformed-signature controls cover unproved identity,
mixed targets, argument-window observations and constructor calls.

[Owning method-table fields](native-owned-method-table-slots.md) now connect a
returned table through one fixed own-data field on a confined local object.
The exact six-function fixture advances **0/6 → 6/6 native**, returning 4211.
Field loads copy the existing owning handle, preserving callable/Map lifetime
after the container dies. The bounded structural query is explicitly consumed
by returned-table flow and rebuilt for final admission; existing capture and
Map checks still apply. Global/realm owners, slot rewrites, uncertain
initialization and incompatible incoming schemas remain refused.
The additional lifetime and shared-field-family fixtures admit 14/14 and 10/10
functions; their seven combined observations (including the six-function
specimen) agree with the interpreter in explicit/deduced GCC/Clang builds and
ASan/UBSan runs. Twenty-one refusal cases, forged/rerun annotations and every
incomplete budget cutoff retain the boundary.
External sample `../ctcompile-samples/07-owning-method-table-fields` contains
the specimen's source and generated C++. All seven samples pass 18 observations;
the first six generated files remain byte-identical to the preceding checkpoint.

Native C++ now spells JavaScript numbers through `using js_num = double;`.
[Returned closures](native-returned-closures.md) now appear directly at their
creation site inside the factory. Their owning init-captures copy source values,
preserving live bindings and shared Map identity. Each lambda uses independent
final-IR names, const/constexpr analysis and deduced-type pins. Nested emission
restores the surrounding function's state. Other direct/address uses retain the
lifted definition; writable captures and recursive or oversized expansions
retain helpers. Sample 5 declares and returns a local `ctn_lambda` inside
`makeCounter_1`, with no `ctn_bind_fn_2` helper. Anonymous closure names use
numbered suffixes when needed; source binding names take precedence.
Used parameters lose redundant generated void casts after final cleanup.

[Maps](native-maps.md) use `std::map` and `.find()` when the admitted module has
no snapshot/iteration observations. SameValueZero lookup handles NaN and signed
zero; modules with snapshots retain insertion order, including after fusion.
Numeric string Maps use `ctnative::string_to_number_map` and a named factory.

The [host-prefix contract](native-host-prefix.md) adds explicit
`follow-publication=true`. It follows a checked straight-line factory's normal
return through its fresh method table and resolves the first actual method call.
The exact CommonJS/browser/realm-fallback probes select 2/5/6 branches, resolve
both the factory and `Data.get`, and retain one runtime Map allocation, three
capture edges and one publication write. Source replacement of a method or the
table selects the replacement's actual target. Separate factory invocations keep
separate resources. The default remains the earlier factory-boundary mode.
Allocation, publication and method effects stay runtime; native admission remains
0/7 on the exact probes. Unknown effects, descriptor changes, mutable captures,
stale manifests and exhausted work retain their conservative boundaries.
The six publication differentials compare 104 observations across Node, the
interpreter and the boxed script/wrapper, including compiled GC-stress runs.

The additional opt-in `follow-provider-reads=true` requires publication
following. It summarizes normal-return paths over still-empty private Maps,
using the actual factory invocation and immutable capture identities. The exact
three probes now resolve factory, `Data.get`, `Data.remove` and `Data.set` calls,
then stop before the first mutation path. Two completed method summaries each
contain one empty-Map `has` read; the methods return null and undefined through
their original control flow. All method bodies,
observer branches and runtime effects remain; native admission is still 0/7.
The three new differentials pass 62 observations under Node, the interpreter
and boxed script/wrapper execution, including GC stress. The focused suite
checks 23 source cases plus stale/forged contracts and work limits.

Final devbox gate: **464/464 CTests**, **152/152 lit cases**, **547.27 seconds**;
all **557 C++ files** pass formatting; `git diff --check` passes. Owning table fields pass
ASan/UBSan, stack-use-after-return and leak checks in explicit and deduced forms.
Existing closure and string-snapshot sanitizer regressions remain green.
Default native coverage stays
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725**; exact Data probes remain
**0/7 native** in CommonJS/browser/realm-fallback modes (the separate AMD probe
remains **0/8**). Full Bootstrap initialization is still unfinished.

[Constant-expression bindings](native-constexpr-bindings.md) combine the existing
backward immutability proof with forward target binding-time analysis. Typed
scalar literals and checked exact operations become `constexpr`; parameters,
heap values, opaque calls, invalid/inexact operations and mutable join storage
retain their previous policy. Source BTA reports cannot authorize C++ constant
evaluation. Explicit and deduced declarations share the same qualification and
exact type pins.

[Returned closures](native-returned-closures.md) with concrete signatures now
use a `std::function` alias and a creation-site lambda with its source body inside. Explicit init-captures own their
values, including shared Map handles. They preserve alias mutation and lifetime
after factory return. Unsupported admitted signatures keep the owning tuple
representation; closure admission and escape requirements are unchanged.

[Const bindings](native-const-bindings.md) now qualify native C++ locals and
by-value parameters using backward binding-mutability data flow. Writes, unknown
reference uses and lvalue/capture aliases keep bindings writable. Explicit and
deduced output agree, including exact const type pins and shallow pointer const.
Loop/join storage remains mutable, while `catalog`, `score_1` and `score_2` gain
const where their uses permit it. Compiler-owned helper operand contracts describe
C++ const acceptance without claiming purity or immutable heap contents.

[JavaScript source names](native-source-names.md) now survive into native C++.
The sample's parameter remains `catalog`; its initial and returned scores use
`score_1` and `score_2`, with other intermediates in the same numbered family.
Optional bytecode debug tables supply provenance without changing semantic IR.
One function-wide allocator avoids collisions with types, symbols, macros,
anonymous temporaries and nested loops. Both explicit and deduced output pass
GCC/Clang execution checks, and unmarked boxed output retains its spelling.

[Native C++ literal printing](native-literals.md) now keeps ordinary strings
readable (`std::string("price", 5)`) and spells finite doubles concisely
(`100.0`, `0.1`, `-0.0`). The shared byte-safe formatter preserves embedded NUL,
UTF-8/WTF-8 and escaping boundaries; the C++ printer preserves round-trip float
precision and deduction types. Both GCC and Clang pass 12 string cases covering
all 256 bytes and 168 floating-point bit patterns. All six sample pairs have
been regenerated and retain their 17 expected observations.

The native entry now defaults to bounded primitive precomputation followed by
private reachability pruning. `--ctnative-lower-to-emitc=optimize=false` disables
both; `precompute=false` and `prune-unreachable=false` disable either independently.
Heap PE, direct-call specialization, deforestation and supercompilation remain
opt-in. Type, effect, BTA and ownership proofs remain required. See the
[defaults policy](native-optimization-defaults.md) and [roadmap](native-pe-roadmap.md).
The default/disabled differential preserves eight observations and reduces its
generated C++ from 7,859 to 7,022 bytes with readable literals, source names and
const/constexpr bindings after the call-order correction.
Coverage now accounts for pruned functions without shrinking the source denominator;
historical admission floors run separately with defaults disabled.

[String-key snapshots](native-string-snapshots.md) now lower Bootstrap's exact
`Array.from(map.keys())[0]` diagnostic expression for confined standard Maps.
Owning vectors and nullable strings preserve undefined, null and empty strings,
insertion order and lifetime after mutation. The fixture admits 10/10 functions
with 24 observations and passes ASan/UBSan/leak checks. The Map/Array identity and
confinement proofs remain mandatory; arbitrary hosts and iterators are unsupported.

The first [checked host-slot analysis](native-host-slots.md) implements a
fingerprinted closed-source contract, fresh allocation identity and own-data
publication flow. It exposes usable edges only after the entire contract passes;
unknown effects, stale manifests and repeated factory identities refuse. Exact
CommonJS/browser probes find 24/23 candidate slot edges, but all three complete
contracts remain refused and native admission stays unchanged. Connecting this
proof to retained callables and supported host effects is still open.

[Host entry-prefix specialization](native-host-prefix.md) now consumes a narrower
live proof without claiming the complete host contract. The exact CommonJS and
browser wrappers select two and five UMD branches respectively and resolve one
actual factory closure each. All seven source functions and the runtime factory
call remain; native admission stays 0/7. Initial Map/Array and realm identities
are explicit embedding contracts. Unknown effects, callback exposure, reentry,
stale manifests and exhausted work retain the original control flow. This stage
is opt-in and does not execute initialization early.

The [Bootstrap host/export oracle](native-bootstrap-host-contract.md) covers
publication slots, AMD retention, mutable methods, receivers, exceptions and
error reentry. The [classic-script receiver](native-bootstrap-script-this.md)
now has a stable realm identity independent of the writable `globalThis` binding.
Both interpreted and compiled entries receive it; modules receive undefined.
Together with the preceding call-order/accessor fixes, ctbrowser now agrees with
Node on all **11/11** audit cases. Accessor closures and the realm receiver survive
forced GC. The preceding compiler change intentionally changed boxed Bootstrap output to
11,226,071 bytes, SHA-256
`721e6095554b20eb2241367283ae1b02c032c771c858ca582af974c6754c2528`.

Six readable JavaScript/native-C++ sample pairs live outside the checkout in
`~/Downloads/claude/ctcompile-samples/`. They cover loops, strings, snapshots,
component lifetime, returned closures and explicit heap PE. All 17 observations
agree under GCC, Clang and the interpreter; the directory includes expected
output, a regeneration driver and artifact hashes.

[Owned scalar component fields](native-object-fields.md) now preserve fields
through the exact Bootstrap getter and owning Map payloads. Explicit C++ members
hold number/boolean/null/undefined values; aliases retain one mutable owner after
factory return, replacement, removal and clearing. Field schemas start from
undefined even when another allocation stores the same key. Mixed lookup results
need an exact SSA identity guard or optional-object truthiness guard. The fixture
admits 15/15 functions and compares 29 observations, including reversed guards,
missing fields and distinct allocations. ASan/UBSan/leak checks pass.

[PE call-target proofs](native-pe-call-proof.md) close a concrete wrong-code route:
a native variant and the retained boxed callable could both return undefined while
mutating the caller's object differently. The evaluator now resolves actual
callable identity and checks alternate bodies from copies of the same pre-call
heap. Exact primitive attributes, anchored old identities, bijective fresh
identities, aliases and ordered Map entries must agree under shared work limits.
Disagreement retains the call through the existing transactional prefix path.
This proves a concrete evaluation, not arbitrary runtime dispatch equivalence.

[Scalar supercompiler generalization](native-supercompilation.md) creates a child
configuration retaining only static arguments shared with a whistled ancestor.
Forgotten positions stay dynamic through literal resets; ancestor promises and
bodies remain unchanged. Exact folding runs first, and budgets retain the generic
alternative. The new fixture has 14 configurations, 12 folds and 9 generalizations;
all 23 functions are native with 16 matching observations. Heap-aware contexts,
generalization over heaps and multi-result search remain planned.

[Precomputation](native-precomputation.md) now expresses six scalar replacements
in PDLL. Invocation-scoped native callbacks consult the analysis; PDLL constructs
constants and replaces roots. Runtime producers, budget charges and native region
splicing retain their previous semantics. LLVM 23 supports captured callbacks;
both precompute and supercompile declare the required PDL dialect dependencies.
The differential-test harness also accepts CMake's wrapped diagnostic whitespace
while still requiring the intended wrong-output failure.

Earlier implemented stages remain covered: immutable closure/cell heaps,
conditional BTA effect queries through fields/Maps/captures, exact-tuple direct
specialization, restricted Lumberhack snapshot projection and bounded private
helper pruning. Pruning follows symbolic and numeric closure edges, retaining
original boxed callees and published declarations. See their linked implementation
documents and the [source layout](source-layout.md).

Full native Bootstrap remains unfinished. The selected wrapper, factory return,
retained Map/cell captures and bounded private mutation paths are now proved.
The local storage prerequisite,
[owning method-table fields on confined objects](native-owned-method-table-slots.md),
now admits its six-function fixture fully, with lifetime and independent-state
checks. Its live store-to-load proof connects existing table/capture ownership
to a confined field. The implemented
[transactional Map mutation summaries](native-provider-mutations.md) retain
runtime effects and stop before unknown error/reentry behavior. The next
provider step is designed in [the next provider proof](bootstrap-provider-next.md)
and needs a live effect/reentry proof for console and snapshot calls;
their names or initial intrinsic identities alone are insufficient.
Prefix observations cannot make exports private.
Global/realm storage still needs native ownership/call analysis for exported
callables, supported provider/error effects and current mutable slot values.
Initial provider identity alone does not authorize Map execution or console
errors during PE. Explicit script
receivers, boxed public parameters and open method-table shapes still refuse
native admission. Unchecked mixed-result property access also needs stronger
presence/refinement evidence or an exception boundary; the initial numeric
catch implementation is described in [native exceptions](native-exceptions.md).
Keep exact vendor probe
coverage distinct from the component fixtures. Recursive Lumberhack fusion,
shared mutable capture environments and region splitting remain separate work.

## Earlier compiler bring-up checkpoint

The remaining sections preserve earlier implementation history and its original
measurements; they are not the current native status.

* Repo: `/mnt/c/Users/aange/Downloads/claude/compile-time-browser`
* Branch: **`ctcompile-v1`**, working tree clean
* Suite: **108/108** via `./tools/remote-build.sh`, and **asan 58/58**
* **`ctcompile/docs/ctcompile.md` is the tool's own documentation** — the CLI,
  what it refuses and why, the manifest, and the gaps.
* **Read `ctcompile/docs/plans/ctcompile.md` first** — it is the running plan in
  the house style (Done / Next, measured rungs) and it records every decision
  below with its reasoning. This file is a pointer to it, not a substitute.

The master plan is 21 markdown files in `../ctcompile-plan/` (NOT in the repo).
`00-START-HERE.md` routes by phase; **`01-objective-and-ground-truth.md`
overrides every other file**. Its six non-negotiables matter: `ctjs` is the
parser only, the bytecode is a REGISTER machine, there is no CI, EmitC is the
primary backend, sources are in `lib/`, build on the devbox.

## What is done

* **Phase -1** — the monorepo split. `ctbrowser/` is the CMake configure root;
  `ctcompile/` is a sibling project.
* **Phase 0** — six inventories the build checks, two differential comparators,
  and a recorded startup baseline. See `ctcompile/docs/baseline/*.json`.
* **Phase 2** — the AOT ABI, and its gate is met: `ctbrowser/include/ctbrowser/aot/aot_helpers.def`,
  68 helpers over 83 of the 93 opcodes (84 `CT_AOT_COVERS` rows; `type_of` is
  served by two helpers on purpose). **THE TABLE IS DONE; PHASE 2'S GATE IS
  NOT** — the plan's gate is "VM code calls a hand-authored AOT closure through
  the real runtime ABI", and no helper has a body, no `function_proto` has a
  native entry, and nothing has ever called one. The record said "done" without
  that distinction; it is a contract, and a good one, that has not been
  executed.
  The runtime now at least COMPILES it: `ctbrowser/lib/Script/aot_contract.cpp`
  is a translation unit of nothing but `static_assert`s, in `ctbrowser-script`,
  so every preset checks it. Until 2026-08-21 the only file in the repository
  that included `aot.hpp` was `ctcompile/test/Inventories.cpp`, and `browser`,
  `browser-no-llvm`, `asan`, `tsan` and `windows` all build with
  `CTBROWSER_ENABLE_PROJECTS` empty — so the ABI and `EngineContract.hpp` were
  parsed in exactly one configuration out of six.
* **Phase 1** — the product. CLI documented in `ctcompile/docs/ctcompile.md`,
  a JSON manifest (`--manifest`, and a copy inside every bundle), stable program
  identities (`program_id` is the source hash the runtime matches on), and both
  format versions exposed rather than copied. `--mode` declares Phase 1's three
  modes and refuses the two that need code generation.
* **Phase 3** — mixed-mode dispatch, centralised. `script/dispatch.hpp` is the
  one read of `aot_entry` in the engine and carries the six transition
  counters; `unittests/unit/aot_dispatch` asserts all six, because every arm
  returns the same answer whether it dispatched or not. Before it, `aot_entry`
  was read at ONE line - `op::call` - so a compiled body was reachable from
  interpreted JavaScript and from nowhere else: not from `context::call` (every
  DOM event, timer, promise job and `apply`), not from `new`, and not from a
  program's top level, which for ctcompile is the ordinary case.
* **Phase 4** — AOT GC shadow frames. `context::set_gc_stress` collects at every
  safepoint, which is the only way the ABI's `is_safepoint` obligations can be
  exercised at all: nothing collects while script runs in an ordinary build.
  **It found two real use-after-frees on `new` the first time it ran**, neither
  in code this phase wrote — see the plan. `ct_aot_slots` is the ABI row a body
  needs to keep a value where the collector can see it, and `context::rooted` is
  the general "a C++ scope is holding this across a call" mechanism.
* **A WHOLE FUNCTION RUNS THROUGH THE ABI.** `unittests/unit/aot_program`
  hand-compiles `total(items, scale)` — a loop, an interned name, a property
  read through a getter, an indexed read, a comparison, both binary families, a
  call back into the interpreter, the failure poll — and checks it against the
  interpreter running the same source, including under forced GC. **That is the
  Phase 12A oracle's shape, working on one function.** It also found the
  safepoint in `context::invoke` sitting before arguments were rooted.
* **Phase 10 — the argument strategy is DECIDED and its experiment passed.** A
  three-way panel chose: Phase 10 normalises calls and each backend materialises
  the arguments, with each parameter's ROLE **derived constexpr from
  `aot_helpers.def`** rather than written down twice. The decisive experiment —
  classify all 69 rows before writing any MLIR — gives **zero unknowns**, and
  `ctcompile_inventories` asserts it.
  **The finding that justifies the whole design:** `uint32_t op_kind` is a
  `ctbrowser::script::op` **bytecode opcode**, not a CTJS enum ordinal —
  `aot_bridge/operators.cpp` does `static_cast<op>(op_kind)`. Passing the CTJS ordinal
  would compile `**` into whatever `op(5)` is. A backend must spell it by name.
* **Phase 10 — started: one conversion pattern, matching on the INTERFACE.**
  `ctjs-opt --ctjs-lower-to-runtime` turns CTJS operations into `func.call`s on
  the real helper symbols, and the pass **names no operation** — which the plan
  calls the acceptance criterion. **Its arity check fired immediately and was
  right**: most helpers are not "frame + operands" (`ct_aot_binary_op` is
  `(fr, op_kind, lhs, rhs, out)` where the kind is an attribute and `out` is an
  out-parameter). A mismatch declines the match rather than failing the module,
  so what it declines is the work list for the rest of the phase.
  **Retired 2026-09-12**: the rest of the phase was never written, the EmitC
  backend names each op with `dyn_cast` and consumes no `func.call`, and the
  shared piece the postmortem below asked for is RuntimeHelpers.hpp's role
  table, which stays. The pass, its lit test and `values_only` are gone.
* **Phase 9 — THE IMPORTER WORKS AND ITS GATE IS MET.** Real bytecode functions
  translate into CTJS MLIR: **p5.js imports 3,200 of its functions and phaser
  6,069**, and both modules verify. `ctjs-translate --ctbrowser-js-to-ctjs f.js`
  is the fastest way to see it; `--ctbrowser-bytecode-to-ctjs` takes an image.
  What still refuses is counted, not guessed: `closure` 556 (needs a producer
  for `!ctjs.program`), `gather_rest` 173, `iterable` 117, `make_arguments` 111.
* **Phase 8 — the CTJS dialect exists: 36 operations in ODS**, round-tripped,
  every verifier diagnostic tested under `-verify-diagnostics`, docs building.
  The operations name real `ctbrowser::aot::helper_id` enumerators through
  `CTJS_RuntimeOp`, so **an operation cannot claim a helper the runtime does not
  declare** — which is what ties the dialect to the ABI Phases 2–6 built.
* **Phase 7 — MLIR is stood up and its gate is met.** `ctjs-opt` and
  `ctjs-translate` build and run, the CTJS dialect's five types round-trip, and
  the lit suite runs as ctest #108 so the gate this repo actually uses covers
  it. **MLIR is not built by default and must not be**: `CTCOMPILE_ENABLE_MLIR`
  is OFF, and a runtime-only configure was verified WITH MLIR installed, which
  is the case that matters.
  To build it: `-DCTBROWSER_ENABLE_PROJECTS=ctcompile -DCTCOMPILE_ENABLE_MLIR=ON
  -DCMAKE_PREFIX_PATH="/home/linuxbrew/.linuxbrew;/home/linuxbrew/.linuxbrew/opt/llvm"`.
* **Phase 6 — the throwing tier works.** `ct_aot_catch_land` was recorded in the
  ABI as **unimplementable as written**, found by trying, with two possible
  fixes written down and neither taken "without a compiled `try` to test it".
  `unittests/unit/aot_throw` is that `try`, and the fix is a third:
  `call_frame::landed_slot`, which keeps the helper's signature. `CT_AOT_PAD_BIT`
  is defined now too, with the measurement `aot.hpp` was waiting for.
* **Phase 5** — in progress, and further than it looks. **29 of the ABI's 69
  rows have bodies** (was 4), including the interned-name pool the whole
  property family was blocked on. Two real extractions with the plan's discipline —
  `context::binary_op_static` and `context::binary_op`, the fourteen binary
  operations, one commit each with the suite green — and sixteen rows that
  needed no extraction, only a shim over a function the runtime already had.
  The flags-consistency test the plan asks for is in `Inventories.cpp`.
* **Phase 15** — a working program image, wired into the page load.
  `ctbrowser/{include,lib}/…/program_image.*` writes and reads a compiled
  `script::program`, validated exhaustively, and `browser::set_script_image()`
  uses it.

## The number that justifies the project

From `ctbrowser/docs/performance.md`: a whole p5 page load is 17.5% lexing,
15.1% `declare_local`, 7.6% `collect_captured_names` — and **1.4%
`context::run_loop`, the entire interpreter**. About forty percent of a page
load is READING JavaScript; 1.4% is executing it. So this compiler's value is
overwhelmingly in what it **deletes from startup**.

Measured, `ctcompile/docs/baseline/page-load.json`, p5-basic.html on the devbox:

| p5-basic.html, three classic scripts | ms |
|---|---|
| `load_html` compiling its own scripts | **69.65** |
| `load_html` handed one image per `<script>` | **19.93** |
| | **71% of the page load** |
| **editing the sketch only** | **19.77 — 3.5x, 1 of 3 recompiled** |

## There is an MVP, and it works

```
ctcompile app/ -o myapp        then ./myapp
```

`ctcompile` loads the entry page once with the engine that will run it, asks
that engine which scripts it compiled and which resources it reached for,
compiles each classic `<script>` to a program image, packs page + resources +
images into a bundle, and appends the bundle to a copy of `ctrun`, a fixed
launcher built like any other tool. The output is one executable. Nothing is
generated and no linker runs — a linked ELF does not care what follows its last
section, so packaging is a file copy plus a trailer.

Measured on the devbox, p5-basic.html, seven runs each, whole-process wall clock
including startup and rendering a frame:

| | ms |
|---|---|
| `ctbrowse p5-basic.html`, reading the JavaScript | **78.0** |
| the packaged executable, run from `/tmp` | **47.3** |

That is the honest end-to-end figure, and it also settles a reasonable
objection: the packaged binary is 15 MB against ctbrowse's 3 MB, because
`this_executable_bytes()` reads the whole launcher back at every start to find
its own trailer. Reading 12 MB more still wins by 30 ms.

**IT IS VALIDATED BY COMPARING RENDERS, not by exit codes.** Seven example
pages package and run; six render byte-identically to the same page loaded from
source, and the seventh did too once a real defect was fixed. That comparison is
the only thing that found the defect, and it is now `ctcompile_package`'s last
arm:

> A packaged application is SEALED - it answers from what it carries and never
> from the disk - and the vendored OFL faces are loaded THROUGH the asset
> registry. So the first sealed build silently dropped to the bitmap font.
> Exit 0, rendered, looked worse. The packager now asks for the faces the way
> `run_app` does, which puts them in `requested()`, and records the DIRECTORY in
> the bundle because it is part of the registry key.

The test took two tries to mean anything, which is worth remembering: the first
version compared two bitmap-font runs (everything in that file sets
`CTBROWSER_FONTS=font8x8`), and the second still passed with the fonts blinded,
because the packaged arm inherited `CTBROWSER_FONT_PATH` and found the faces
under the names the packaging machine had recorded. **The packaged arm is now
given nothing** - no font path, and a working directory that is not the
application's, which is what "copy it and run it" means.

**WHAT IT DOES NOT DO IS GENERATE NATIVE CODE.** The bytecode still runs on the
interpreter. This deletes the *parse*, which is ~40% of a page load; the
interpreter is 1.4%. Phases 7–12A are the rest and are not started.

## What the last session did

1. **The source hash was 4.16 ms of that page load.** It is now
   `boost::hash2::xxhash_64`, 0.181 ms. **Do not "improve" it to a four-lane
   FNV over 64-bit words** — that was tried, it is faster (0.127 ms), and it
   collides on 50,678 of 262,145 single-byte edits of real p5.js. The plan
   explains why, and `ctcompile/test/ProgramImage.cpp` keeps that hash as a
   blinded control so the case that catches it can be watched failing.
2. **`page-load.json` re-recorded**, 53% → 72%.
3. **Operand validation**: the per-operand switch became a bound table, 19.74 →
   19.18 ms, 15 of 15 paired runs. The "suspect fast path" the previous handoff
   proposed was NOT implemented and should not be — see the plan.
4. **`function_proto::nested` deleted.** Nothing ever wrote it; its only reader
   was a ratchet check that could not fire. Image format 1 → 2, and the image is
   19 KB smaller for p5, 128 KB for babylon.
5. **The measurement tools are built by `all` now** — see the trap below.
6. **ONE PROGRAM PER `<script>`.** The image is keyed per script, so p5 is baked
   once and editing a sketch no longer invalidates 4.5 MB. It is also a
   conformance fix: a parse error or a throw in one script no longer stops the
   next, and each script is its own microtask checkpoint. What it removed is a
   forward call from an earlier script to a later script's function — Chrome
   makes that a ReferenceError too.
7. **Five defects found by adversarially reviewing that split**, three of them
   the split's own and two older: a dead script's `try` catching the next
   script's `throw` (`context::execute` never cleared `handlers_`), and a
   use-after-free on synchronous navigation, now fixed by queueing the load.
8. **The `asan` preset works again** — 29 of 52 tests were failing on a
   heap-use-after-free in the CSS parser that fires on every browser
   construction. 52 of 52 now.

9. **`finally` was wrong on six of nine specified behaviours** and is rewritten
   as a completion record. One of them lost exceptions outright. p5_api moved
   172 → 175.
10. **The 65,535 proto ceiling was three stray casts**, and Babylon sat at 49%
    of it. Gone; 140,001 functions verified.
11. **The fingerprint now hashes what the compiler EMITS**, not only which
    opcodes exist — a canary compiled and folded. The `finally` rewrite is
    exactly the change it was blind to.

12. **The MVP above**, and then an adversarial review of it that found eight
    defects in the packaging path — every one of them SILENT, in the sense that
    the application ran and produced the right document:
    * **module scripts were invisible.** `script_sources()` lists classic
      scripts only and there is no image path into `load_module`, so a page of
      modules packaged as "0 scripts compiled" and the guard that asks whether
      packaging worked read a truthful, useless zero. `module_sources()`
      publishes them now; the packager refuses them and so does the launcher.
    * **the guard was gated on "some images arrived"**, so the case where NONE
      arrived — the most obviously broken package there is — was the one case it
      skipped.
    * **the probe never ticked the page.** `fetch` and `img.src` queue their
      requests and are drained from `tick`; p5 loads in `preload` and Phaser in
      the first game step. Every sprite, atlas and level was missed with no
      warning. It now runs the page until it stops asking (ceiling 60 frames);
      p5-basic settles after one.
    * **the packager resolved assets through a second, base-less registry**
      whose probe order differed from the one that answered the page — the exact
      second copy of the rule `assets.hpp` spends a paragraph forbidding.
    * **a packaged application fell back to the filesystem**, probing the
      working directory first, so a missing resource was answered by whatever
      sat next to the user. Registries can be SEALED now, and `run_bundle` does.
    * `read_bundle` bounded each blob and not the total; `bundle_write_error()`
      was a channel nothing ever wrote to, behind a header promising a check
      that was never implemented.

    All six new guards were removed one at a time and watched going red, each
    for its own message. The one that could NOT be falsified is `write_bundle`'s
    refusal of >4G entries or a >4G name — reaching it needs a bundle no machine
    here can hold. It is written and untested, and that is better said than
    implied.

## Do these next

1. **NOT Phase 16A or 16B, on this corpus.** `docs/baseline/page-load-profile.json`
   profiles what an image-loaded page load actually spends: HTML parsing is
   0.0%, CSS and style 0.5%, layout and paint absent. A compiled DOM blueprint
   and a compiled style program target under one percent between them. 16B is
   still *unblocked* — `engine::for_each_rule` exists — it is just not worth
   doing next for these pages.
2. **The image LOADER is now the largest single item on the path**, at 26%, and
   its operand pass alone is 7.49% — fifteen times the whole CSS engine. That
   is where the next startup millisecond is.
3. **FINISH PHASE 10 FROM THE DECIDED DESIGN.** The next steps, in order, are in
   the panel's verdict: an `OpcodeMapping.hpp` giving `BinaryKind ->
   script::op` spelled by name (never a literal); a `ctjs.runtime_call`
   operation carrying the helper, its role vector and its literals, with a
   verifier that the roles consume the operands and literals exactly; then the
   EmitC slice — `!ctjs.value -> !emitc.opaque<"ctbrowser::script::value">`,
   out-parameters as `emitc.variable` plus `emitc.apply "&"`, and the status
   compared against `ct_aot_status::ok` **by name**, never a baked number
   (`aot.hpp` says outright "THE PRECEDENCE IS THE CONTRACT; THE NUMBERS ARE
   NOT"). Note `ct_aot_enter` fails with a NULL POINTER, not a status.
4. **WIDEN THE IMPORTER.** The importer's refusals are
   counted in `ctjs.skipped` and printed as warnings, so the work list writes
   itself — run it over a corpus and read the histogram. `closure` is the
   largest single item and needs a producer for `!ctjs.program`, which is a
   design question rather than a mapping.
   **Handlers are NOT imported yet**: `push_handler`/`pop_handler` map to
   operations but the importer has no handler-stack reconstruction, so any
   function with a `try` is refused. The design for it is in the Phase 9 brief —
   abstract interpretation over the CFG with a stack of push offsets, since
   there is **no handler table** in `function_proto`.
4. **The rest of Phase 5, and Phase 6.** Phases 1–4 are done and their gates
   are met; Phase 5 is 26 of 69 rows.
   **`ct_aot_intern_name` is the one hard blocker on the path to a minimal
   compiled function.** Every property helper's key is a `const ct_aot_name *`,
   the row asks for an owning immortal pool that does not exist, and
   `lookup_property` today takes a `const std::string &`. Until it exists,
   `o.x` cannot be emitted at all — which is why `ct_aot_get_index` is
   implemented and `ct_aot_get_prop` is not.
   After that, the cheapest real extractions per opcode bought are
   `ct_aot_cell_get`/`ct_aot_cell_set` (four opcodes for eight lines, and they
   unblock every captured variable). Leave `ct_aot_construct` (~90 lines),
   `ct_aot_instance_of` (~56) and `ct_aot_set_index` (~36) until last.
   PREVIOUSLY: **Phases 4, 5 and 6** / **Phases 1–6**, the runtime preparation. Phase 2's gate is MET as of
   2026-08-22 — `ctbrowser/lib/Script/aot_bridge/` has four helper bodies and
   `unittests/unit/aot_basics` calls a hand-authored compiled function from
   interpreted JavaScript. Doing it falsified `ct_aot_catch_land`, which cannot
   be implemented as written; the row says so now. The throwing tier and Phases
   1, 3–6 are still open. WAS: Phase 2's TABLE is done and its GATE is not — nothing has ever called a hand-authored AOT function through
   the ABI, which is the cheapest way to find out whether 1,881 lines of
   contract are right before 68 helper bodies depend on them.

## Known problems, not yet acted on

* **lit LIVES IN A VIRTUAL ENVIRONMENT.** brew's llvm bottle ships FileCheck but
  no llvm-lit, and both Ubuntu's python and brew's refuse `pip install` under
  PEP 668. `python3 -m venv ~/.lit-venv && ~/.lit-venv/bin/pip install lit`, and
  `tools/Brewfile` says so where somebody provisioning a box will read it. With
  no lit, `check-ctcompile` reports that it is unavailable rather than silently
  running nothing.
* **THE ABI TABLE'S LINE CITATIONS ARE SYSTEMATICALLY STALE.** Every row cites
  the runtime that owns its semantics by file and line; Phases 3–5 moved several
  hundred lines of `run_loop.cpp`, `call.cpp` and `vm.hpp`. Six citations
  pointed past the end of a file and are repaired **as names**;
  `ctcompile_def_citations` keeps that class out. **Many more still land inside
  their file while naming a handler that has since moved**, and no machine can
  see that. If you follow a citation and find something else, the row is stale
  rather than wrong about the semantics — the claims were checked, the addresses
  were not re-checked afterwards. Cite by name in anything you touch.

* **A `<script src>` that ships its source TWICE.** `write_image` defaults to
  `keep_source` and `ctcompile` takes the default, so p5.js is 4.5 MB as an
  `asset` (which the run-time walk must re-read to reproduce the hash) and again
  inside its 7.3 MB image. Dropping the source is not free — it is whether
  `f.toString()` returns the text or `[native code]`, and p5's own error system
  reads it — so this is a real decision, not an oversight to tidy.
* **`ctrun` ignores `argv` once a bundle is appended.** `myapp --help` silently
  starts the application.
* **`this_executable_bytes()` is `/proc/self/exe` only**, so a packaged
  application on Windows finds no bundle and prints usage. The cross build
  exists; this half of it does not.
* ~~The 65,535 proto ceiling~~ — FIXED 2026-08-21, it was three casts.
* **OLD, KEPT FOR THE REASONING:** the 65,535 proto ceiling was at 49% on a
  corpus that already existed. Three
  of four `op::closure` emitters cast the function index to `uint16` before the
  32-bit `with_bx` (`compile_function_decl` in `statements/functions.cpp`, `expressions.cpp:95`,
  `classes.cpp:156`; `classes.cpp:109` does not). Above 65,535 protos the
  COMPILER builds the wrong closure. Babylon is 31,905. The image writer refuses
  such a program rather than freezing the bug into a file.
* **The image is keyed to a whole page's concatenated scripts**, because
  `browser::run_scripts` compiles every classic `<script>` into ONE program. So
  editing an inline sketch invalidates the image for the 4.5 MB bundle beside
  it. Splitting per-script is an engine change: `compile_program` hoists
  function declarations across the whole concatenation, so a call in the first
  script to a function declared in a later one works today and would stop.
  **This is what stands between the current win and "bake p5 once, reuse it",
  and it is the highest-value thing left on this path.**
* **`aot_gc` PROVES MUCH LESS OUTSIDE `asan`.** It asserts correct answers under
  forced GC in every build, but a rooting bug is a use-after-free, and reading
  freed memory usually returns the right bytes. Every one of its guards was
  falsified under `asan`, and that is where a regression in them will show.
* **THE `asan` AND `tsan` PRESETS ARE NOT IN THE GATE.** `tools/remote-build.sh`
  runs the default preset only, and the CSS use-after-free above sat there
  through every green run until somebody built asan by hand. It is 52 of 52 now
  and nothing will notice when that stops being true. Running asan in the gate
  costs a second configure and build; deciding that is the next person's call.
* **`ctbrowser`'s benchmarks are still `EXCLUDE_FROM_ALL` with no aggregate**,
  which is the defect that invalidated the first computed-goto measurement and
  then this session's first page-load reading. Fixing them is the same three
  lines as `ctcompile-tools`.
* **The corruption fuzz prints a count it does not assert** —
  `ProgramImage.cpp` reports "1615 of 3205 offsets still loaded" and nothing
  pins it. Pinning it was considered and not done: the number depends on the
  fixture's compiled bytecode, which Phases 13 and 14 renumber deliberately, so
  a ratchet there would churn without signal. If validation changes, prove
  equivalence differentially instead — see the plan's note on the 60,000-mutation
  digest, which is how the bound-table rewrite was shown to be the same function.
* **`@font-face`** — fixed for `url()` in `a2ef736`, but the style engine still
  records a page font only when the family and url are string tokens elsewhere;
  check before assuming.

## How to work here (learned the hard way)

* **BUILD ON THE DEVBOX, ALWAYS**: `./tools/remote-build.sh` from the repo root.
  The WSL box has 7.5 GiB and has been taken down by local builds twice.
* **The devbox self-deallocates after 30 idle minutes.** When ssh times out:
  `cd ../infra/azure-build-server && ./server.sh start`.
* **The devbox shell is zsh, which does NOT word-split unquoted variables.**
  `CXX="clang++ -O2"; $CXX foo.cpp` fails as one word. Inline your flags.
* **Chain gates with `&&`, never `;` — AND NEVER THROUGH A PIPE.** A `;` after
  `tools/format.sh --check` let an unformatted commit through once; on
  2026-08-21 `./tools/format.sh --check | tail -1 && git commit` did it again,
  because a pipeline's exit status is the LAST command's and `tail` always
  succeeds. Redirect to a file and read it, or check the status first.
* **A green build does not mean the binary you are about to run was built.**
  `EXCLUDE_FROM_ALL` targets are not in `all` AT ALL, so `cmake --build`
  rebuilds the engine, relinks every test, reports 97/97 — and leaves an
  excluded executable at whatever revision someone last built by hand. That has
  now produced a wrong number in this tree three times
  (`docs/history/computed-goto.md`, `docs/performance.md`, and
  `ctcompile/docs/baseline/page-load.json`). The ctcompile measurement tools are
  fixed — `ctcompile-tools ALL` in `ctcompile/tools/CMakeLists.txt` — but
  **`ctbrowser`'s benchmarks still have it**, so anything measured with
  `ctbrowser-test-bench_*` must be built explicitly and checksummed.
  Separately, `rsync -az` preserves mtimes, so restoring a file can leave ninja
  thinking it is current; `touch` it. Distrust any figure that exactly matches
  the arm you were replacing.
* **No hardware perf counters on the devbox** (it is a VM) — `perf stat` reports
  `<not supported>`. Use callgrind for attribution, dhat for allocation, and an
  **interleaved A/B of two binaries** for wall clock. Not a before-and-after
  across sessions: the from-source page-load arm moved 7 ms between sessions
  with no commit that could explain it.
* **Profile the thing itself.** One callgrind run profiled a binary that
  compiled the program to build the image, and the compile drowned the load.

## The discipline that has been earning its keep

* **The positive case is one line; the negative cases are the file.** Every
  comparator here is verified against a deliberately BLINDED implementation, and
  every negative case must be seen going red. The source hash's cases go
  further: the blinded hashes live in the test permanently, and each case
  asserts that its control DOES collide, so a case that stops proving anything
  says so instead of passing.
* **Prove a guard is load-bearing by removing it, and say so plainly when it
  does not go red.** Done for both validation fixes in `59d0339` (one did, one
  did not) and for the Boost.Hash2 configure check, which was verified by
  pointing `CTBROWSER_BOOST_INCLUDE_DIR` at a Boost without Hash2.
* **Silence is not success.** Assert counters, never trust output. That guard
  caught a "58x speedup" that was a loader refusing every corpus.
* **Correct yourself in the record.** Four claims have now been committed and
  later corrected here. The most recent: a hoist that "the compiler cannot do"
  and measurably did not need, reverted with the measurement in the plan.
* **A fast algorithm that is quietly wrong is worse than a slow one.** The
  four-lane FNV was faster than what shipped and would have made the image cache
  accept stale code on one edit in five. Prefer somebody else's algorithm AND
  somebody else's code; check it against a third party's answers.

## Phase 10: what was decided, and what was refuted

**`ctjs.runtime_call` was designed, reviewed and NOT BUILT.** A three-lens
adversarial panel refuted it against the checkout. Its only novel content was
carrying the helper as a string, which converts the project's one *build-error*
ABI check — `CTJS_RuntimeOp` concatenates the name into a `helper_id`
enumerator — into a pass-time lookup. Worse, an `OpInterfaceRewritePattern` that
matches every implementer and *produces* an implementer re-matches its own
output until the iteration cap. The role walk it existed to hold is right and
belongs in a header both backends call, not in an IR node.

**The role table now reads the ABI's *failure tier*, not just `may_throw`.**
37 rows declare `may_throw` and only 24 return a status; the rest fail in the
RAISE tier, where the result is always well-formed and the caller polls
`ct_aot_failed` at back-edges. `ct_aot_enter` is in neither tier — it returns
NULL. Emitting a status test after `ct_aot_new_object` tests nothing.

**Two committed checks were wrong and are corrected.** `classify_return` missed
`ct_aot_to_int32`, the row the `.def` exempts by name ("a signed int32 return
that is DATA, not a status"); the mechanical tell is that it takes no frame
handle, and it is the only int32_t row that does not. And `values_only` admitted
the out-parameters it claimed to exclude, because `"uint64_t *out"` starts with
`"uint64_t "` — six rows passed a check whose comment said they could not.

**The shape trait found four live defects the moment it existed.**
`CTJS_ABIShaped` compares every runtime operation's ODS declaration against its
helper's row. It caught `load_upvalue`/`store_upvalue` (an `$index` attribute
against a helper with nowhere to put it — every captured-variable read compiled
to `undefined`), `instanceof` (a `!ctjs.value` result against a `uint32_t` 0/1),
and `delete_property` (a result against a helper that answers with a status).
**There is deliberately no operand-count rule**: the dialect is higher-level
than the ABI, so supplying *fewer* arguments is normal and only excess is
checkable.

**The EmitC entry shape is pinned and compiles.**
`test/Lowering/EmitC/entry-shape.mlir` is the target, not any pass's output.
Callees must be **qualified** (`ctbrowser::aot::ct_aot_*`) because the
`extern "C"` prototypes live inside that namespace — the table's `symbol` is the
LINKER name, not the callee string. `emitc.call_opaque` emits no declaration, so
the TU just includes `aot.hpp`; `emitc.declare_func` is broken in this LLVM
(drops parameter types). `--declare-variables-at-top` is mandatory, because the
NULL test gives every body two blocks.

**THE PIPELINE IS CONNECTED.** `echo 'function f(a) { return a; }' |
ctjs-translate | ctjs-opt --ctjs-lower-to-emitc | mlir-translate --mlir-to-cpp`
produces a translation unit that compiles against the real `aot.hpp`.
`test/Lowering/EmitC/end-to-end.mlir` runs all four stages and the last one is a
C++ compiler. The backend can barely do anything - it refuses almost every
function and records why as `ctjs.not_lowered` - and that is the point: every
operation added from here is an increment on something that demonstrably works.

**Three runtime facts the backend had to be told, none guessable from the IR:**

* **`argv` dies at `ct_aot_enter`.** It is an interior pointer into
  `context::registers_` and `enter` resizes that vector. Parameters are read
  before the call; `ct_aot_slots` cannot recover it, since that hands back the
  compiled frame's own span rather than the caller's window.
* **`new.target` and the callee cannot be delivered at all.** The importer
  prepends three implicit arguments and only `receiver` is in the entry
  signature. `ct_aot_new_target` and `ct_aot_callee` are declared in `aot.hpp`
  and **defined nowhere** — a call to either is a link error. Two more gaps sit
  behind that: `ct_aot_enter` never sets `call_frame::closure`, so `callee`
  would answer `undefined` anyway, and nothing sets `pending_new_target_` on the
  compiled `new C()` path.
* **`--mlir-to-cpp` miscompiles a parallel copy on a block-argument edge** in
  LLVM 22.1.8 — measured by compiling and running it, see
  `block-argument-hazard.mlir`. The importer's register file *is* block
  arguments, so this is every function with a loop that permutes two registers.
  Non-entry block arguments must become `emitc.variable`, reads before writes.
  Until that exists the backend refuses any function with more than one block.

**Smaller things worth not rediscovering:** `--declare-variables-at-top` is
mandatory (EmitC refuses multi-block functions without it) and it declares
every value at the top, so a `const` local is a build error — the argv pointer
is cast once instead, because the signature must stay assignable to
`ct_aot_entry_fn`. `$` in a symbol compiles only as a GCC/Clang extension. And
`%cxx` in `test/lit.cfg.py` is what makes the compile step available to any
EmitC test.

**Control flow compiles now too.** `function g(a) { if (a) { return 1; } return
2; }` reaches a translation unit that compiles. The pipeline is
`ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments`, and **that
order is a correctness requirement**: the first pass emits block arguments, the
second removes them, and what reaches `mlir-translate` must have none.

`--emitc-eliminate-block-arguments` gives each non-entry block argument an
`emitc.variable`, reads it at the top of its block and writes it on each
incoming edge — so every read precedes every write. **Edges are split rather
than assigned in place**, because `cf.cond_br %c, ^B(%x), ^B(%y)` is legal and
carries different values into one block; assigning both sets before the branch
runs both on whichever path is taken. In-place assignment is correct for every
single-successor terminator, which is exactly why the swap test does not catch
it — that case has its own function.

**Number constants are spelled from bits**, never as a decimal literal:
`value::number(std::bit_cast<double>(UINT64_C(...)))`. The attribute carries the
double's bit pattern precisely because `-0.0` and NaN payloads do not survive a
decimal round-trip, and printing decimal would discard that at the last step.

**The out-parameter/status pattern is done**, which is the shape most of the ABI
has. `status_call()` writes it once: a local for the result, its address, the
call, a test against `ct_aot_status::ok` **by name**, and a block split so the
result is loaded only on the surviving path — which the row requires, not merely
permits (`*out` is written only on `CT_AOT_OK`). The failure edge is shared per
function and **tests for `unwound` before leaving**: on that status the unwinder
has already destroyed this frame, so an unconditional `ct_aot_leave` pops
somebody else's.

`a + b`, `!a`, `+a`, `void a`, `a === b`, `a == b` and the four relational
operators all compile now.

**A GAP IN THE ABI, worth knowing before designing against it:** *no row boxes a
machine quantity into a JavaScript value.* `ct_aot_strict_equals` returns a
`uint32_t`, `ct_aot_compare` an `int32_t` ordering, `ct_aot_to_number` a
`double` — and there is no `ct_aot_from_bool` and no `ct_aot_from_double`. In
C++ that boxing is `value::boolean(b).bits()`, a **member call on a temporary**,
which `emitc.call_opaque` cannot spell because its entire output is
`callee(args)`. The backend therefore emits two `static inline` shims into its
own translation unit rather than adding rows to a runtime ABI for a compiler's
convenience. If the ABI ever grows those rows, the shims go.

**The relational operators are not negations of one another.** `ct_aot_compare`
can answer `unordered` — a NaN on either side — which makes all four false,
`>=` included. `a >= b` as `!(a < b)` makes `NaN >= NaN` true. Each is built
from equality tests against the orderings that make it true. Unlike the status
enum, **the ordering's numbers are contractual** and `aot.hpp` says so.

**COMPILED VALUES ARE ROOTED IN THE FRAME, and this was a real shipped bug.**
`a + b + c` kept the first addition's result in a plain C++ local across the
second `ct_aot_binary_op`, which is a safepoint. The collector is precise; a
value in a native frame is reachable from nothing. Under `set_gc_stress` the
compiled body returned six characters where the interpreter returned
sixty-five, and ASan called it a heap-use-after-free. **Without stress it was
correct every time**, which is why every other test passed.

The tell was an inconsistency in our own file: it refused string constants and
`typeof` because "ct_aot_new_string is a safepoint, and nothing roots the result
yet" — while admitting six operations with exactly that property.

Every produced value now goes into a frame slot immediately, and **the span is
re-fetched at every store**: the row says the pointer "IS VALID UNTIL THE NEXT
SAFEPOINT AND NOT ONE INSTRUCTION LONGER". Storing once suffices because the
collector marks and deletes rather than moving. Slots are never reused — a leak
bounded by the frame beats a liveness analysis that is wrong once.

**`ctcompile_gc_roots` is the only test that runs generated code against the
real runtime with the collector hostile**, and it is the only kind that can see
this class of defect: a use-after-free nothing collects is invisible, because
the freed memory still holds the right bytes. It compiles `gc-roots.js` through
the real pipeline at build time. Removing the parking makes it report 6
characters against 65 while "collector idle" still passes.

**Two ordering hazards are now refused rather than documented.**
`--emitc-eliminate-block-arguments` walks `emitc.func` only, so run *before* the
lowering it silently does nothing and the block arguments reach `mlir-translate`
— measured: `sl(10,20,2)` answers 20 where 10 is correct, every tool exiting 0.
It now refuses to run when a `ctjs.func` remains. And `ctjs.frame_exit` must be
the last thing before the return, or the shared failure path leaves the frame
twice (harmless — `leave` truncates to its own index — but unchecked).

**Property reads and calls compile.** A call is the first operation needing the
frame for something other than rooting: `ct_aot_call` takes a **contiguous**
`argv`, and the arguments are rooted in scattered slots — so each call site
reserves a run in the register window and copies them in just before the call.
In the frame, not a C++ array: the call is a safepoint that runs user JavaScript
before reading them. The GC test now exercises exactly that.

**ONLY 32 OF THE 69 ABI ROWS HAVE IMPLEMENTATIONS.** `aot.hpp` declares all of
them; `lib/Script/aot_bridge/` defines 32. A call to one of the other 37 **compiles
perfectly and fails at link** — and that shipped: `ct_aot_global_get` and
`ct_aot_negate` were emitted for two commits with a green suite, because every
EmitC lit test uses `-fsyntax-only`. `runtime_defines()` in `CTJSToEmitC.cpp` is
the list, and **`ctcompile_linkable`** keeps it honest in both directions by
linking a TU that exercises everything the backend accepts.

**Two limits that are upstream of the backend**, found writing that fixture:
`+a` never arrives — the *importer* has no CTJS operation for `op::to_number`,
so `ctjs.unary plus` is reachable only from hand-written IR. And `undefined` is
a **global read** in JavaScript, so it needs the helper with no body; an
uninitialised local is the same value and reaches nothing.

**The four tests that can see what lit cannot**, and each catches a failure the
other three are green for:

| test | asks | why nothing else can |
|---|---|---|
| `ctcompile_differential` | is the ANSWER right? | fluent, linkable, rooted code can still compute the wrong thing |
| `ctcompile_gc_roots` | do values survive a collection? | a use-after-free nothing collects still holds the right bytes |
| `ctcompile_linkable` | do the symbols exist? | a declared-but-undefined helper compiles perfectly |
| `%cxx` in each lit test | does it agree with `aot.hpp`? | a signature the backend invented looks fluent |

The differential test's inputs **separate** the lowerings rather than covering
them — an object with a `valueOf` for the two `+` families, `NaN` for the
relational operators, `0` against `"0"` for the equalities. A case whose answer
is the same whether or not the compiler is right is worse than no case.

**The two global rows are implemented** (`context::global` and
`define_global` — the same lines the interpreter runs, so the tiers cannot
drift), so globals compile and run. That is the pattern for the rest: most rows
say "DELEGATES TO" a `context` method that already exists.

**Next**: `ct_aot_set_index` and the other 36 unimplemented rows are the
critical path now — the backend can lower more than the runtime can execute.
Either implement rows in `lib/Script/aot_bridge/`, or widen into what is already
implemented: `ct_aot_new_object`, `ct_aot_new_array` and `ct_aot_truthy` are
there, `ct_aot_append` and `ct_aot_construct` are not, so object and array
literals are half-reachable.

## Using subagents

The last two sessions used `Workflow` heavily and it paid for itself. Ask agents
to REFUTE a design against the code, not to agree with it. Their best output has
been defects in already-committed work: two memory-safety holes in the image
loader, and two stale standing decisions in `ctbrowser/docs/` that this session's
Boost floor change had invalidated.
