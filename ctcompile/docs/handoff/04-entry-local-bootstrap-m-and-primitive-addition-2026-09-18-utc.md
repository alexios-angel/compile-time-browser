[Back to HANDOFF.md](../HANDOFF.md)

## Entry-local Bootstrap M and primitive addition, 2026-09-18 UTC

Started clean at **92192514**. The **16:07:54 AGENT-SYNC closure** confirmed
all interrupted work committed; September 7 WIP is already an ancestor. Both
agents' logs and historical unmerged branches were reviewed. Continued the
original W/r/H helper boundary in HANDOFF and master 00/24. Parallel agents
supplied an escape draft and source audit; root completed the three-file escape
draft and native fixtures after agent service limits.

**5d12efaa** composes exact entry-local helpers with DOM classes. A helper's
original entry call may be ordinary or already resolved, but its closure and
receiver must be proved. Its intrinsic loads, Number `toString` and raw exception
CFG survive into existing typed DOM and URI/JSON proof. These deferrals also
force complete class-method probes. Global-holder targets, constructors, getters
and helpers without that entry call retain the stricter census; unused bodies
cannot disappear without proof. The public class pass remains closed-source-only.

Ordinary lifted helpers discard a direct-call callee operand only for the exact
target with an unused callee argument. All such checks precede closure deletion,
so deleting a child first cannot change acceptance. Nested callee dependencies
remain refused. The original module and contract survive every preparation
failure; no browser source, new value representation or VM dependency was added.

The complete vendor-pinned **M** now executes as `M(shape.read())`, covering
Number/null paths, valid JSON and malformed URI/JSON fallback. All **123 existing
source/expected fixture entries** remain unchanged. New original-M fixtures use
an explicit **1,000,000-step** budget because the conservative class-lift size
bound exceeds the default; the production default remains **100,000**.
Native output still passes the Script/AOT symbol and class-dispatch exclusions.

**1d13c777** proves bounded Boolean/null addition through the existing primitive
conversion, explicitly excluding String concatenation first. Saved operands,
signed cancellation, CFG/SCF transport and zero/unit lengths retain their original
identities. String, BigInt, undefined, unknown/changing/repeated operands and
out-of-bound results remain unproved. Existing source bodies were preserved;
false/null zero-index and zero-length expectations now reflect the proof.

Focused devbox evidence: `/tmp/ctcompile-helper-effects/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`:
  **8 initial actions, 5 first-retry actions, 6 final actions**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.86s; total 3.87s)**,
  with twelve added transaction controls per DOM provider.
- Exact `ctcompile_host_contract`: **1/1 (0.46s; total 0.47s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (44.52s)**,
  **160 Node/interpreter observations, 8 native executions, 982 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (196.01s)**;
  **154 observations, 380 native executions, 308 unprepared / 190 preparation
  refusals**, plus 16 constructed-method executions / 20 refusals, 8 original-r
  executions / 4 refusals, 4 prototype-key executions / 6 refusals and 11
  prepared-source refusals. This passed before the final DOM-only ordinary-call
  and cleanup corrections; its public-pass path was unchanged by those edits.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **3 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.09s)**.
- Exact `Analysis/Escape/escape-claims/primitive-addition.test`: **1/1 (0.12s)**,
  **29 sites / 11 sound / 11 of 17 precision**, zero violations.
- Exact `Analysis/Escape/escape-claims/{negative-add,add-cancellation}.test`:
  **2/2 (0.22s)**; each **20 sites / 8 sound / 8 of 12 precision**, zero violations.

The initial transaction run failed **3.77s (total 3.78s)** on retained callee
bookkeeping and the original-M size budget. After those corrections, transaction
and contract passed **3.79s / 0.45s**, but class DOM failed **7.44s** because the
conditional argument left M's call ordinary. Proving that exact local call and
making cleanup order-independent produced the final passes above. A nested-helper
refusal pins the latter boundary. Escape checks needed no retry.

All **10 tested source/test hashes** match the devbox. Required
`tools/format.sh --check` reports **26 pre-existing diagnostics in nine
HEAD-identical files**; changed C++ formatting, Black, Node syntax and
`git diff --check` pass. Full CTest, full compiler lit, complete DOM suites,
broad corpus/native matrices, WPT/test262 and whole-Bootstrap replay were skipped.
No whole-bundle gain is claimed. No browser edits or push.

**Exact next:** original W/r/H still needs callable captures and complete helper
identity/effect composition. A class method capturing sibling M is outside the
constructor-only capture proof; nested callee dependencies and unused global
holder slots remain strict. Continue that boundary without artificial source
calls, then F's callback/RegExp and full H. W also requires `Object.entries`,
destructuring's `__ctbrowser_iter_open`, original `s`, RegExp/TypeError and spread
proofs before inheritance. Preserve the existing W/W+r/W+r+H refusals; the last
specimen still omits `s`. Full H Unicode, retained callbacks, broader escape
conversions and the application driver remain unfinished. Native Bootstrap and
the overall plan are not complete.

## Mixed class/DOM intrinsics and primitive subtraction, 2026-09-18 UTC

Started clean at **051ab3d2**. The **15:45 AGENT-SYNC closure** confirmed the
interrupted Error/power drafts were committed. September 7 WIP is already an
ancestor; historical isolated branches were retained. Continued the exact
W/r/H boundary recorded by HANDOFF and master 00/24. Parallel agents supplied
escape implementation and a source audit; root completed native fixtures after
agent service limits. No browser implementation or runtime semantics changed.

**690ff8b6** composes the existing DOM intrinsic declarations with the class
helper and optional Error. Class preparation proves and consumes only its own
identities; the DOM declarations survive all private method probes and final
reproof. Entry/method intrinsic loads and Number `toString` use that existing
typed proof. Every affected method is probed, even one with no call. The original
source census rejects intrinsic replacement before any method disappears.
Constructors, getters and helpers retain their stricter census.

Number conversion now executes with entry-local classes and an unused throwing
Error getter, using existing ctbrowser behavior. An ignored pure intrinsic result
is inert; observable identity use, arbitrary coercion receivers, unknown effects,
missing/duplicate identity and late failures still refuse. Failed preparation
preserves the original module and contract. Native output retains the no-Script/
AOT and no-class-dispatch checks. All **109 pre-session source/expected entries** remain unchanged.

**18e56dac** reuses bounded primitive Number conversion for Boolean/null
subtraction. Saved operands, negative results and CFG/SCF transport retain their
original identities; primitive property keys and unknown/changing/repeated inputs
remain unproved. Four old expectations changed without source rewrites.

Focused devbox evidence: `/tmp/ctcompile-mixed-intrinsics/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`:
  **10 initial actions**, **4 after the production correction**, then **three
  2-action test-only rebuilds**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.80s; total 3.81s)**;
  eleven additional transaction controls per DOM provider.
- Exact `ctcompile_host_contract`: **1/1 (0.48s; total 0.49s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (36.29s)**,
  **140 Node/interpreter observations, 8 native executions, 814 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (201.27s)**;
  **154 observations, 380 native executions, 308 unprepared / 190 preparation
  refusals**, plus 16 constructed-method executions / 20 refusals, 8 original-r
  executions / 4 refusals, 4 prototype-key executions / 6 refusals and 11
  prepared-source refusals. Original W/W+r/W+r+H refusal controls remain intact.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **3 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.06s; total 1.07s)**.
- Exact `Analysis/Escape/escape-claims/{primitive-subtraction,signed-subtraction}.test`:
  **2/2 (0.23s)**, zero violations. Primitive: **26 sites / 11 sound /
  11 of 15 precision**; signed: **48 / 26 / 26 of 28**.

The first transaction run failed **3.77s** because the class census rejected
Number `toString`; the production correction delegates that exact read to typed
receiver proof. Two later runs (**3.76s / 3.84s**) identified an incorrect new
refusal expectation for an ignored pure Number return. Its original source became
a positive; a DOM write using the identity supplies the actual refusal. A fourth
run (**3.79s**) caught its stale contract assertion. After transaction/parser
passed, class DOM failed **0.19s** on duplicated mixed-request setup in the direct
helper test loop. Removing that duplicate required only a Python sync and the
successful exact class-DOM retry above. No later production change or replay of
passing compatibility/escape tests was needed.

All **11 tested source/test hashes** match the devbox. Required
`tools/format.sh --check` retains **26 pre-existing diagnostics in nine unchanged
files**, verified byte-identical to HEAD. Changed formatting, Black, Node syntax
and `git diff --check` pass. Full CTest, full compiler lit, complete DOM suites,
broad corpus/native matrices, WPT/test262 and whole-Bootstrap replay were skipped.
No browser edits or push.

**Exact next:** complete original **W/r/H** helper effects through typed DOM proof,
starting M's Number/JSON/URI exception chain and F/H helper identities. Mixed
declarations now compose; constructors/getters/helpers still use the stricter
class census. W also needs `Object.entries`, destructuring's
`__ctbrowser_iter_open`, original `s` (`Object.prototype.toString.call`),
RegExp/TypeError and spread proofs. The current W+r+H specimen does not include
`s`; retain that refusal control and include the original helper in a later full
specimen. Then address inheritance. Full H Unicode, retained callbacks, the
application driver and broader primitive conversions remain unfinished. Escape
next can address Boolean/null addition with explicit exclusion of String
concatenation. Native Bootstrap and the overall plan are not complete.

## Declared Error class/DOM composition and primitive powers, 2026-09-18 UTC

Resumed **eight dirty tracked files plus the primitive-power oracle at c2108572**,
left by the **15:25:30 AGENT-SYNC loop failure**. The original **15:11:07 journal**,
diff and current handoff identified the interrupted Error/primitive-power thread.
September 7 WIP is already an ancestor; both agents' logs and unmerged branches
were reviewed. Parallel agents completed escape recovery and part of the audit;
root finished native fixtures after service limits.

**4bcc196c** composes the existing declared Error binding/getter proof with DOM
class preparation. Requests may declare the class helper plus optional unique
`Error`. The complete original source and initial bindings are proved before
those declarations are consumed. An unused literal-message throwing `NAME`
getter can then be erased. Referenced throws, including reads in unused methods,
still reach the final typed DOM proof and refuse. Error replacement inside the
entry or in a suffix, escaping payloads, effectful messages, missing/duplicate
identity, late unknown DOM effects and exhausted budgets remain refusals; failed
preparation preserves both source and contract. No new Error representation,
VM dependency, browser implementation or ownership carrier was added.

The class fixture retains all **102 previous source bodies and expectations**.
Its former optional-Error declaration refusals now lower those exact requests
successfully; duplicate Error declarations supply a separate refusal control.
Native output still passes its Script/AOT symbol and class-dispatch checks.

**a5a08550** reuses the existing exact primitive Number conversion for
Boolean/null power bases and exponents, keeping the bounded zero/unit identities,
negative parity, saved operands and CFG/SCF transport. Original primitive keys,
unknown/changing/repeated producers and general powers remain unproved. All old
bodies remain intact. The focused oracle exposed two stale `unit-power.test`
expectations for the already supported original `(-1) ** "2"` body; only its
claims changed to confined, with zero observed soundness violations.

Focused devbox evidence: `/tmp/ctcompile-error-finalize/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`: **10 actions**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.78s; total 3.79s)**;
  ten added transaction controls per DOM provider.
- Exact `ctcompile_host_contract`: **1/1 (0.46s; total 0.47s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (32.92s)**,
  **120 Node/interpreter observations, 8 combined native executions, 720 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (200.61s)**;
  **154 source observations, 380 native executions, 308 unprepared refusals,
  190 preparation refusals**, plus 16 constructed-method executions / 20 refusals,
  8 original-r executions / 4 refusals, 4 prototype-key executions / 6 refusals,
  and 11 prepared-source refusals.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **3 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.04s; total 1.05s)**.
- Exact `Analysis/Escape/escape-claims/{primitive-power,bounded-power,
  power-identities}.test`: **3 passes** in the initial four-case **0.42s** run.
  `unit-power.test` alone failed FileCheck on the two stale expectations above;
  its test-only retry passed **1/1 (0.11s)**. No production correction or passing-test replay was needed.
- All four power oracles report zero violations. Primitive power: **24 sites /
  9 sound / 9 of 13 precision**; bounded/unit power: each **26 / 13 / 13 of 15**;
  power identities: **26 / 11 / 11 of 15**.

Required `tools/format.sh --check` retains **26 pre-existing diagnostics in nine
unchanged files**, verified byte-identical to HEAD. Changed C++ formatting,
Black, Python/Node syntax and `git diff --check` pass. All **12 tested source/test hashes** match the devbox.
Full CTest, full compiler lit, complete DOM suites, broad corpus/native matrices,
WPT/test262 and historical whole-Bootstrap replay were skipped. No browser edits
or push.

**Exact next:** complete original **W/r/H** class/DOM effects and binding
composition. Optional Error now composes for unused throwing getters; mixed
class/DOM intrinsic declarations, TypeError, Object/RegExp/iterator operations
and full helper identities remain unsupported. Preserve every original method
and `NAME` throw while proving them. Original W, W+r and W+r+H controls still
refuse at `r`, `H`, and an unproved call/binding/effect respectively. Then address
inheritance. Full H Unicode, retained callbacks, the native application driver
and broader primitive conversions remain open. Native Bootstrap and the overall
plan are unfinished.

## Config defaults and primitive bitwise snapshots, 2026-09-18 UTC

Resumed **six dirty files at d7e13751**, abandoned by the **14:17:21 AGENT-SYNC
loop failure**: omitted/defaulted Config argument tests and the primitive-bitwise
draft. Both agents' recent commits and unmerged branches were reviewed; September
7 WIP was already an ancestor. Parallel agents worked on native fixtures, escape
recovery and a proof audit; root finished their drafts after service limits.

**b871feb2** proves original omitted/defaulted method arguments through the
shared typed DOM boundary. The existing lifter already supplies undefined for
omitted arguments. Exact strict-undefined comparisons select a result kind only
after both arms and their effects pass proof; the private candidate then folds
the selected arm in source order and receives independent final reproof. Null
does not trigger defaults. Unknown effects in skipped defaults still refuse.
Literal/static-getter defaults, fresh empty `DefaultType`, transitive calls and
argument/default/body write order execute. All 85 previous source bodies and
expectations, plus the interrupted default-order body, remain intact.

Fixed cell reads may occur in later `scf.if` arms; confined field reads may occur
in later structured branches/loops. Writes stay in their original blocks, and
captures/aliases retain the existing census. DOM writes expose their already
proved undefined result. Nullable `getAttribute` Strings pass through a four-line
native conversion to the existing ctbrowser attribute API, including `"null"`.
Entries proved to return only undefined emit ordinary C++ `void` signatures and
returns. The two old DOM write refusal bodies now execute unchanged. No browser
source, VM dependency, collector or ownership carrier was added.

**abdeeec1** shares exact primitive Number conversion with Boolean/null bitwise
and shift snapshots. Both operand orders, signed/unsigned results, saved inputs,
CFG/SCF transport and original primitive-key refusals are checked. Unknown,
changing/repeated and out-of-bound results remain unproved. Twenty-four old
false/null zero-index expectations and two zero-length expectations changed
without rewriting their source bodies.

Focused devbox evidence: `/tmp/ctcompile-default-complete/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery` and `ctcompile-test-host-contract`:
  initial complete default gate **7 actions**, final void-return retry
  **58 actions, pass**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.79s)**,
  including twelve added default-argument transaction controls.
- Exact `ctcompile_host_contract`: **1/1 (0.47s; total 0.48s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (30.54s)**;
  **116 Node/interpreter observations, 8 combined native executions, 678 refusals**.
- Exact `CTNative/Browser/native-dom-strings.test`: **1/1 (143.35s)**;
  **787 Node/VM observations, 8 GCC/Clang binaries**, both providers/policies/layouts;
  **1,052 source, 44 provenance/depth, 24 method, 241 capture,
  101 replacement, 22 branch and 27 completion refusal checks**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`:
  **1/1 (203.07s)**; **154 source observations, 380 native executions,
  308 unprepared refusals, 190 preparation refusals**. Its additional controls
  report **16 constructed-method executions / 20 refusals, 8 original-r executions /
  4 refusals, 4 prototype-key executions / 6 refusals, 11 prepared-source refusals**.
- Exact `CTNative/Browser/native-dom.test`: **1/1 (244.35s)**;
  **20 entries, 41 refusal controls**, both policies/layouts with GCC/Clang,
  linking DOM/Core and selector-only Style.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`:
  **3 initial / 2 retry actions, pass**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.05s)**.
- Exact `Analysis/Escape/escape-claims/{primitive-bitwise,signed-bitwise,
  primitive-division}.test`: **3/3 (0.32s), zero violations**. Primitive bitwise
  and division each **24 sites, 9 sound, 9 of 13 precision**; signed bitwise
  **26 sites, 11 sound, 11 of 15 precision**.

The initial five-action native build passed; class DOM failed **0.14s** because
116 observations exceeded two-digit lexical ordering. Three-digit names fixed the
harness. Subsequent class DOM failures **8.76s / 9.13s / 8.99s / 8.92s** exposed
strict-undefined proof, nested fixed-cell reads, nested confined-field reads and
used DOM write results respectively. Intermediate native builds ran **64 / 6 / 5
actions**; exception recovery passed **3.72s / 3.80s / 3.73s**, and host contract
passed **0.47s / 0.49s** after its target was added. The initial arrays run failed
**1.03s** with 144 assertions from 24 stale expectations; the original bodies were
preserved. The initial complete default gate also passed class DOM **31.13s**,
exception recovery **3.74s (total 3.75s)** and host contract **0.47s**. The subsequent
native DOM run failed **203.40s** because the unchanged returned-removeAttribute
source exposed a nullable scalar return carrier. The proof now publishes exact
undefined-return evidence for ordinary C++ `void` output; its C++ client adds
a compile-time void-return assertion. A 66-action scheduled rebuild stopped after
nine attempts on a misplaced proof assignment (undeclared `result`); correcting
that placement produced the final 58-action build. Only exception recovery,
host contract, class DOM and native DOM were repeated for this change. Native
DOM then failed **199.36s** on three unused variables in the new C++ test client,
after the void/no-carrier checks passed. Existing-style void suppressions fixed
the client; its unchanged source case moved first, and only native DOM was
rerun using test-only sync. Final checks above pass; no test was repeated after
its final pass.

All 21 tested source/test hashes match the devbox. Final DOM, escape retry and
hash wrappers exit **0**. Required `tools/format.sh --check` retains **26 existing
diagnostics in nine HEAD-identical files**. Changed pinned formatting, Black, Python/Node
syntax and diff checks pass. Full CTest/compiler lit, complete DOM, broad
corpus/native matrices, WPT/test262 and historical whole-Bootstrap replay were
skipped. No push.

**Exact next:** compose complete original Config **W/r/H** with the existing
class/DOM proof, including declared Error/TypeError, Object/RegExp/iterator and
helper identities/effects. Preserve the unused throwing `NAME` getter and every
method body. Existing original-source controls still refuse W at global `r`,
W+r at global `H`, and W+r+H at an unproved call/binding/effect. The DOM class
projection still accepts only the class helper declaration; declared Error
composition is an explicit next seam. Then address inheritance. Full H Unicode,
retained callbacks, the application driver and broader primitive escape
conversions remain open. Native Bootstrap and the overall plan remain unfinished.

## Transitive method arguments and primitive division, 2026-09-18 UTC

Started clean at **2ac4ab0d**. The **13:43:50 AGENT-SYNC session closure**
confirmed the interrupted argument work was committed; this session continued
its recorded Config transitive-call boundary. September 7 WIP was absent.
Parallel agents supplied native tests, an escape draft and a proof audit; root
completed the drafts after agent service limits.

**4ab08f61** proves parameterized methods reached transitively from original
entry calls on each actual instance. A bounded census follows exact
`this.method(...)` edges through that instance's definitions. The existing
shared private proof retains real arguments and field writes in source order.
Independent zero-parameter probes cannot supply parameter authority. Unused
formals, uncalled instances, invalid/dead calls and recursive graphs still refuse.
The previous transitive-only refusal now executes unchanged; all 19 prior positive
and 42 prior refusal source bodies remain intact. Twelve new transaction checks
cover both DOM providers. No browser/runtime code or ownership carrier changed.

**ac4cf776** reuses exact primitive Number conversion for Boolean/null division
and remainder snapshots. Both operand orders, signed results, saved operands,
CFG/SCF transport and original primitive-key refusals are checked. Zero divisors,
nonintegral quotients and unknown/changing/repeated operands remain unproved.
Two old Boolean divisor expectations changed; their source bodies did not.

Focused devbox evidence: `/tmp/ctcompile-transitive-config/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` and
  `ctcompile-test-exception-recovery`: **7 actions, pass**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.77s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (26.74s)**;
  **88 Node/interpreter observations, 8 combined native executions, 526 refusals**.
  Both providers, C++ layouts and optimization settings pass GCC/Clang and
  Script/AOT exclusion.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`:
  **1/1 (202.17s)**; **154 source observations, 380 native executions,
  308 unprepared refusals, 190 preparation refusals**.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`:
  **3 initial / 2 retry actions, pass**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.03s; total 1.04s)**;
  **656 dense / 796 induction / 342 structured rows**, with
  **25,381 / 52,692 / 26,991** budget cutoffs. Initial run failed **1.00s**:
  two stale Boolean expectations and two malformed new repeated-SCF fixtures
  caused nine assertions. Fixed the expectations and new fixture SSA.
- Exact `Analysis/Escape/escape-claims/{primitive-division,primitive-product,
  signed-division}.test`: **3/3 (0.33s), zero violations**. Division/product:
  each **24 sites, 9 sound, 9 of 13 precision**; signed division:
  **26 sites, 13 sound, 13 of 15 precision**.

All nine source/test hashes match the devbox. Final native, escape retry and hash
wrappers exit **0**. Required `tools/format.sh --check` retains **26 existing
diagnostics in nine HEAD-identical files**; changed pinned formatting, Black,
Python/Node syntax and diff checks pass. No test was repeated after its final
focused pass. Full CTest/compiler lit, complete DOM, broad corpus/native matrices,
WPT/test262 and historical whole-Bootstrap replay were skipped. No push.

**Exact next:** preserve original omitted/defaulted arguments in W's
`_getConfig → _mergeConfigObj(t, e)` and
`_typeCheckConfig(t, e = this.constructor.DefaultType)` calls. This session
proves the transitive prerequisite, not complete W. Full Config W/r/H, its unused
Error/TypeError and other effect obligations, inheritance, full H Unicode,
retained callbacks, the application driver and broader primitive escape conversions
remain open. Native Bootstrap and the overall plan remain unfinished.

## Original class-method arguments and primitive products, 2026-09-18 UTC

Resumed four dirty files at **47a08427**, abandoned by the **13:27:02 AGENT-SYNC
loop failure**. The September 7 WIP was absent from unmerged branches. Agents
recovered native tests, extended primitive-product tests and audited the native
proof; root finished the drafts after two agent service limits.

**8f322043** proves parameterized class methods using original entry calls,
with each method called directly on each actual entry-local instance. One private
copy retains all those calls, their actual arguments and their source-position
field state. Zero-parameter methods retain independent probes, including unused
bodies. Unused parameterized methods and transitive-only invocation still refuse;
no synthetic parameter authority is introduced. Entry `scf.if`/`scf.yield` results
now reach the same complete typed DOM proof, including both branches. The public
closed-source pass and constructor/getter/helper census remain unchanged.

Five new native cases cover key/element arguments, repeated calls, field replacement,
two instances and transitive calls with direct witnesses. All pre-session fixture
bodies and observations remain unchanged. Dead-branch bad keys and unknown DOM
calls, missing host authority, later invalid arguments and uncalled second instances
remain refused. Ten added transaction checks cover both DOM providers.

**a5838467** shares the existing exact primitive conversion between unary
operations and multiplication. Boolean/null factors acquire bounded signed Number
facts only at the result; original keys and identities survive. Both operand orders,
saved factors and CFG/SCF snapshots are covered. Unknown/changing/repeated factors
remain unproved. Two original false/null multiplication zero-shrink bodies now admit
without source changes. No browser/runtime code or ownership carrier changed.

Focused devbox evidence: `/tmp/ctcompile-arguments-finish/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` and
  `ctcompile-test-exception-recovery`: **7 initial / 4 retry build actions**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.71s; total 3.72s)**; first
  **1/1 (3.64s)**. Covers successful publication and rollback for both providers.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (23.36s)**,
  **76 Node/interpreter observations / 8 native executions / 450 refusals**.
  Both DOM providers, C++ layouts and optimization settings pass GCC/Clang and
  Script/AOT exclusion. First run failed **6.10s** at entry short-circuit census;
  original fixture bodies were preserved while the proof gained typed branch handling.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (200.83s)**.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **3 initial /
  2 retry actions**. The first arrays run failed **1.00s** because two new
  structured tests expected one path instead of both; only expectations changed.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.99s; total 1.00s)**,
  **656 dense / 756 induction / 330 structured rows**, with
  **25,360 / 50,700 / 25,085** budget cutoffs.
- Exact `Analysis/Escape/escape-claims/{primitive-product,primitive-unary,
  signed-product}.test`: **3/3 (0.32s), zero soundness violations**.
  Product and unary: each **24 sites / 9 sound / 9 of 13 precision**;
  signed product: **33 sites / 17 sound / 17 of 19 precision**.
- All nine source/test hashes match the devbox. Final native, escape and hash
  wrappers exit **0**. No test was rerun after its final focused pass.

Required `tools/format.sh --check` reports **26 existing diagnostics in nine
HEAD-identical files**. Changed pinned C++ formatting, Black, syntax and diff checks
pass. Full CTest/compiler lit, complete DOM suite, broad corpus/native matrices,
WPT/test262 and historical Bootstrap replay were skipped. No push.

**Exact next:** prove transitive-only original method arguments for Bootstrap W's
`_getConfig → _mergeConfigObj / _configAfterMerge / _typeCheckConfig` chain without
inserting artificial source calls. Defaulted/omitted arguments, complete Config
W/r/H composition and inheritance remain open, as do full H Unicode, retained
callbacks, the application driver and broader escape conversion proofs. Native
Bootstrap and the overall plan remain unfinished.

## Constructor-stored DOM methods and primitive unary snapshots, 2026-09-18 UTC

Resumed dirty **9586e616** after the **13:02:02 AGENT-SYNC loop failure**:
two unfinished class DOM test files named the original `class_element` method
boundary. The September 7 WIP is already merged. Parallel agents recovered tests,
audited the proof and implemented the independent escape change; root finished
integration after agent service limits.

**3a933858** proves zero-parameter class methods against each actual entry-local
construction, including unused and transitive method bodies. Private copies reuse
the existing closure lift, field normalization and typed DOM validator. Each method
gets an independent probe; source entry parameters retain their original host
provenance. Unread method definitions can disappear only after all their bodies
pass. The final emitted entry is independently reproved and published atomically
with its contract. Constructor/getter/helper effects and public class preparation
retain their existing restrictions. An early eligibility check prevents retaining
pointers into method bodies that normalization replaces.

The original `class_element` now executes natively through `this.element` and
`Button.NAME`; no original fixture was rewritten. Two new unused-method positives
also pass, including a write that must never execute. Transitive field replacement,
fake second instances, detached methods, invalid arguments, dead-branch unknown
calls and nested constructions remain refused. No runtime or browser code changed.

**d2dc123f** gives original Boolean/null unary Plus/Neg/BitNot results exact
Number snapshots. Original primitive keys, unknown inputs, repeated producers and
changing predecessor values keep their refusals. Four old false/null zero-shrink
bodies now correctly admit; all original test bodies remain intact.

Focused devbox evidence: `/tmp/ctcompile-method-finish/`.

- Built the seven explicit affected targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle`. Build rounds: **9, 10, 4, 2 actions**.
- Exact class DOM lit: **1/1 (19.13s)**, **56 Node/interpreter observations,
  8 combined native executions, 312 refusals**. Both providers, layouts,
  optimization settings, GCC/Clang and Script/AOT exclusion pass.
- Exact class initialization lit: **1/1 (200.18s)**.
- Exact exception recovery: **1/1 (3.57s)**, including 16 class/DOM transaction
  cases across both providers.
- Exact arrays CTest: **1/1 (0.99s; total 1.00s)**. Exact `primitive-unary.test` and
  `signed-bitnot.test`: **2/2 (0.22s), zero soundness violations**. The new unary
  oracle measures **24 sites / 9 sound / 9 of 13 precision**; signed BitNot
  measures **32 sites / 14 sound / 14 of 18 precision**. Arrays retain
  **656 dense / 734 induction / 326 structured rows**.

The initial DOM gate failed **4.16s** because an unused definition blocked the
existing lift. Subsequent **17.75s** and **19.27s** passes preceded added controls
and the early eligibility fix. Initial arrays failed **0.99s** only on the four
newly proved old expectations; their source bodies were preserved. The earlier
exception check passed **3.69s (total 3.70s)**. All eleven final source/test hashes
match the devbox and gate wrappers exit zero. All 45 prior class DOM bodies/signatures remain.
Required pinned formatting retains **26 existing diagnostics in nine unchanged
files**; changed formatting, Black, Python syntax and diff checks pass. The first
format scan also saw four transient diagnostics while the child was editing;
they are absent from the final scan.

**Next boundary:** extend method-argument provenance beyond zero-parameter methods,
without granting unused method formals host authority. The existing original
`unused_key_dom_method` still refuses. Complete Config W/r/H composition, inheritance,
full H Unicode, retained callbacks and the application driver remain open. Full
CTest/compiler lit, complete DOM suite, broad corpus/native matrices, WPT and
test262 were skipped; historical Bootstrap coverage was not replayed. Native
Bootstrap and the overall plan remain unfinished. No push.

## Captured local class getters, 2026-09-18 UTC

Resumed clean **1b941768** after the **12:36 AGENT-SYNC failure** explicitly
abandoned the previous capture work. The **12:34 journal** and this handoff named
`press$4`'s captured `Button.NAME` as the interrupted thread; no dirty draft
remained. September 7 WIP was already merged. Parallel agents supplied tests and
a proof audit. The escape agent hit a service limit before producing a draft.

**84f93391** proves captured reads of a method's own local constructor through
the existing static-getter proof. Local cells in class setup functions must have
ordered same-block uses, with every write storing the same SSA value and reads
or captures after initialization. Fixed aliases retain the constructor/receiver
use census, including chained aliases. Arbitrary captures, changing writes, early
reads, identity escapes, unknown effects and cyclic getters remain refused.
Getter expansion removes only the proved capture/cell plumbing after the complete
original body census. Shared immutable-capture rules and browser/runtime code
are unchanged; no owning runtime carrier was added.

Two pure captured NAME/DATA_KEY cases and one DOM getter-key case now execute
natively. Source refusals and four raw cell controls include a chained-alias
mutation found during parallel review. All pre-existing source bodies remain.
The preserved `class_element` now reaches **unknown call, binding or reflective
effect**, past its original capture refusal.

Focused devbox validation, evidence `/tmp/ctcompile-capture-finish/`:

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` and
  `ctcompile-test-exception-recovery`: **4 successful build actions**. The first
  build found a typed-value conditional mismatch in new code; corrected.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (16.28s)**, including
  both providers, C++ layouts, optimization settings, GCC/Clang and Script/AOT
  exclusion. Three preliminary failures (**3.15s / 3.46s / 3.82s**) showed that
  the new test also requested conditional-cell/entry-flow or Boolean-arithmetic
  support. The new fixture was narrowed to captured getter selection; the
  preserved `class_element` specimen was never rewritten.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (198.53s)**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.66s; total 3.67s)**.

All five source/test hashes match the devbox; native and hash wrappers exit **0**.
Required pinned `tools/format.sh --check` reports **26 existing diagnostics in
nine HEAD-identical files**; changed C++ formatting, Black, Python syntax and diff
checks pass. Full CTest/compiler lit, complete DOM suite, escape tests, broad
corpus/native matrices, WPT and test262 were skipped. Historical Bootstrap
measurements were not replayed. No browser changes or push.

**Exact next:** prove original `press$4` DOM effects through its constructor-stored
`this.element`: `classList.toggle` and `getAttribute`, including every unused
method/getter/helper body before erasure. The existing typed DOM receiver/field
proof and private preparation transaction are the seam; validating only the
residual called code would lose unused-body obligations. Public class preparation
remains closed-source-only. Config, inheritance, full H Unicode, retained
callbacks, the application driver and broader escape/conversion work remain open.
Native Bootstrap and the overall plan are unfinished.

## Original class/DOM composition and String BitNot, 2026-09-18 UTC

Started clean at **4961fffd** after the **12:09:01 AGENT-SYNC session closure**.
No interrupted edits remained and September 7 WIP was absent. Resumed the recorded
`class_key`/`class_order` composition boundary. Independent agents supplied focused
native tests, String BitNot and a read-only proof audit; root completed integration
after the audit agent hit a service limit.

**9c16dffd** reuses the original class proof and rewrite inside `prepareDOMEntry`'s
private candidate. DOM requests may supply exactly `__ctbrowser_class_defined`,
which the existing binding proof checks before consumption. Mixed intrinsic sets
remain refused. Only selected-entry parameter uses and unknown entry calls defer
to the final typed DOM proof. Every original constructor/method/getter/helper,
including unused bodies, retains the existing complete census. The public class
pass stays closed-source-only; its extracted rewrite body is unchanged.

The existing closure lifter normalizes constructors/methods under a quadratic
IR-size ceiling (operation count times operations, operands and block arguments).
This bounds input size; the lifter does not yet expose a per-scan step budget.
Only closures proved lifted by this invocation and retaining solely inert roots
are removed. Supplied native reports are discarded, then direct receivers,
confined fields and all DOM effects are independently reproved before publishing
module and contract together. No browser/runtime code or ownership carrier changed.

**c23423ff** completes canonical decimal String handling for unary BitNot using
the existing bounded parser and exact signed complement calculation. Original
String identities and CFG/SCF snapshots survive. Noncanonical/signed Strings,
out-of-range values and changing/repeated producers remain unproved. All eight
original signed-bitnot oracle bodies are preserved.

Focused devbox validation only; evidence: `/tmp/ctcompile-dom-class-compose/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`, `ctcompile-test-exception-recovery`.
  Initial build found a missing include in new DOM preparation; corrected.
  Successful rebuilds took **8, 4 and 4 actions**, with explicit affected targets.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.98s)**.
- Exact `Analysis/Escape/escape-claims/signed-bitnot.test`: **1/1 (0.18s)**.
- Exact `ctcompile_exception_recovery`: final **1/1 (3.43s; total 3.44s)**,
  initial **1/1 (3.45s)**. Includes ten class/DOM transaction cases across both
  providers, checking successful publication and rollback after refusal.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: final **1/1 (15.55s)**,
  **44 Node/interpreter observations / 8 combined native executions / 208 refusals**.
  Preserved `class_key` and `class_order` now execute in the existing GCC/Clang,
  explicit/deduced, optimized/unoptimized, both-provider gate with Script/AOT
  exclusion. All 40 previous source bodies/signatures remain intact. Four new
  unused-body adversaries and mixed-authority controls remain refused.
  Initial run failed **3.35s** at the DOM manifest parser; corrected before the
  **15.59s** pass. Final run also covers stricter lifting-size accounting.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (191.53s)**.

All twelve changed source/test hashes match the devbox. Final build/native/evidence
wrappers exit **0**. Required pinned `tools/format.sh --check` retains **26 existing
diagnostics in nine HEAD-identical files**; changed-file pinned formatting, Black,
Python syntax and diff checks pass. Full CTest/compiler lit, complete DOM suite,
broad corpus/native matrices, WPT and test262 were skipped. Historical Bootstrap
measurements were not replayed. No push or browser source changes.

**Exact next:** the preserved `class_element` first refuses with **complete
capture-free source functions**. Its `press$4` has one upvalue: the local `Button`
binding used by `Button.NAME`. The entry stores the same constructor closure into
that cell twice, with method closure creation between the writes. Prove that
captured class/getter identity from original source before widening class
preparation; then prove original method DOM effects, including unused bodies.
A separate `unused_key_dom_method` probe still refuses at the complete original
unknown-call/effect census. Keep the original specimen and all bodies intact,
reuse existing capture/getter/receiver proofs, and retain transactional typed DOM
reproof. Config, inheritance, full H Unicode, retained callbacks, application
driver and broader Number/String conversions remain open. Native Bootstrap and
the overall plan are unfinished.

## Confined DOM fields and canonical String powers, 2026-09-18 UTC

Continued clean **86ebc737** after the **11:56:44 AGENT-SYNC journal** closed
its interrupted receiver work. Resumed the ordinary instance-field boundary
recorded in HANDOFF/master00; September 7 WIP was absent from unmerged branches.
Three agents handled escape analysis, native tests and a read-only composition
audit. Root recovered the five-file escape draft after service limits and reused
the existing native driver after its test agent stopped before creating files.

**15e2827b** forwards fields of fresh local objects after direct receiver
expansion. Every object use must be an ordered same-block root or ordinary
constant-key field read/write, with a preceding write for each read. Reads retain
the value at that source position across later writes. The confined allocation
and field operations disappear; all value producers remain for complete DOM
reproof. Element-valued fields become ordinary borrowed DOM operands. Empty
String keys work. Escapes, identity observations, self-stores, reserved/dynamic
keys, conditional writers and missing writes remain outside this rule. Objects
passed to live closures still reach holder classification before expansion.
No class-provider or original constructor proof was added.

**c57a40a4** reuses the existing canonical decimal parser for both operands of
bounded power identities. Existing zero/unit/parity arithmetic, signed Number
snapshots and source identities remain unchanged. Noncanonical/signed Strings,
out-of-range values, general powers and changing/repeated producers remain
unproved. All eight original bounded-power oracle bodies remain byte-identical.

Focused devbox checks only; evidence: `/tmp/ctcompile-dom-fields/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`: **7 build actions**, no build failures.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.96s; total 0.97s)**.
  Dense/induction/structured rows **652 / 697 / 319**; budget cutoffs
  **25,082 / 47,124 / 23,548**.
- Exact `Analysis/Escape/escape-claims/bounded-power.test`: **1/1 (0.11s)**,
  **26 observed sites / 13 sound / zero violations / 13 of 15 precision**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (13.89s)**,
  **44 Node/interpreter observations / 8 combined native executions /
  134 refusals**. Six new field sources execute alongside the two direct-receiver
  sources. Three original class sources and fourteen adversarial class bodies
  remain unchanged and retain preparation refusals. The initial run passed
  **14.10s / 128 refusals**; the final run adds three adversarial sources only.
- Selected existing `helper_object_method`, `helper_object_extracted`,
  `helper_object_multiple`, `helper_regex_original_h` sources: **16 Node/VM
  observations / 8 combined GCC/Clang executions / 24 provenance checks /
  88 refusals**. Both providers, optimization settings and explicit/deduced C++
  pass with emitted/linked Script/AOT exclusion. This is a selected DOM gate.

All eight changed source/test hashes match the devbox; both gate wrappers exit
**0**. Required pinned `tools/format.sh --check` retains **26 existing diagnostics
in nine HEAD-identical files**. Changed-file pinned formatting, Black, Python
syntax and diff checks pass. Full CTest/compiler lit, exception recovery,
class-initialization and complete DOM suites, broad corpus/native matrices,
WPT and test262 were skipped. No browser source/runtime changes or push;
historical Bootstrap measurements were not replayed.

**Exact next:** compose the existing original class proof with DOM preparation
transactionally for preserved `class_key` and `class_order`. Keep the public class
pass closed-source-only. Reuse its proof/rewrite body on `prepareDOMEntry`'s private
candidate; initially require exactly the class helper intrinsic. Only entry
parameter uses and otherwise-unknown entry calls may defer to the final typed DOM
proof. Every original constructor/method/getter/helper, including unused bodies,
must retain the unchanged complete class effect census. Check the helper binding
with the existing closed-source binding proof before consuming its declaration.
Reuse `lowering_detail::closureLifter` under a bounded budget; remove only freshly
proved lifted closures whose remaining uses are inert roots, clear supplied
native reports, then independently recheck direct receivers and local fields.
Preserve entry effect producers through normalization and publish source/contract
only after full DOM reproof. Do not union intrinsic lists or trust residual code
alone. `class_element` still needs original method DOM-effect proof. Remaining
Config, inheritance, full H Unicode, retained callbacks, application driver and
broader Number/String conversions remain open. Native Bootstrap is incomplete.

## Direct DOM receivers and canonical String bitwise snapshots, 2026-09-18 UTC

Resumed dirty **43860a33** from the **11:38:29 AGENT-SYNC journal** and its
explicit **11:38:51 abandonment**. The interrupted files were the two-file
String bitwise draft and untracked `class_dom.py` / `class-dom.mlir`. September 7
WIP was absent from unmerged branches. Independent agents audited the native
proof and recovered tests; root completed escape drafts after service limits.

**3c4e3b1e** extends `DOMSource` to expand already normalized direct calls with
an exact private, capture-free target, no live closure creation, exact arity and
undefined callee/new.target. Each invocation binds its own actual receiver.
The existing body-cloning logic is shared with live closure expansion; live
closure receiver/lexical rules remain unchanged. Direct targets may observe their
receiver, but not callee/new.target or nested closures/captures. Complete source
bodies, recursion/depth/work limits, unvisited-function rejection and transactional
DOM reproof remain required. This supplies no new source constructor/prototype
proof or class-provider authority.

Two equivalent source/normalized-IR cases execute natively: one attribute read
and repeated reads/writes on two distinct element receivers. Three original
class/DOM specimens retain Node/interpreter observations and preparation refusals;
all fourteen adversarial class bodies remain. Generated and linked output calls
public DOM C++ and excludes Script/AOT dependencies and dynamic class dispatch.

**a1debd07** reuses the canonical decimal parser for both operands of bitwise
AND/OR/XOR and left/signed-right/unsigned-right shifts. Signed Number snapshots,
masked counts and original String identities survive CFG/SCF transport. Signed
or noncanonical String forms and out-of-range values remain unproved. All eight
original source-oracle function bodies remain byte-identical.

Focused devbox checks only; evidence: `/tmp/ctcompile-dom-recovery/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`, `ctcompile-test-exception-recovery`.
  Initial build: **8 actions**; two test-header rebuilds: **2 actions each**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.98s)**.
  Dense/induction/structured rows: **643 / 673 / 312**; budget cutoffs:
  **24,680 / 45,638 / 22,889**.
- Exact `Analysis/Escape/escape-claims/signed-right-shifts.test`:
  **1/1 (0.13s)**, **36 observed sites / 15 sound / zero violations /
  15 of 21 precision**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.48s)**, including the existing
  DOM preparation transaction and rollback checks.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (10.92s)**,
  **20 Node/interpreter observations / 8 combined native executions / 68 refusals**.
- Selected existing `helper_object_method`, `helper_object_extracted`,
  `helper_object_multiple`, `helper_regex_original_h` fixtures:
  **16 Node/interpreter observations / 8 combined GCC/Clang executions /
  24 provenance checks / 88 refusals**. Both providers, optimization settings and
  explicit/deduced C++ pass. This selection is not the complete DOM String suite.

All eight changed source/test hashes match the devbox. The final native wrapper
exits **0**. The initial array gate failed **0.97s (0.98s total)** on six stale
assertions for the original `"0" >>> "0"` shrink; its source remained intact and
its expectation now records the proved empty array. The first new native test
failed **0.16s** because the fixture expected an indirect call after the resolver
had already made it direct. Only the fixture changed after the production build.
Malformed call arity remains the IR verifier's responsibility.

Required pinned `tools/format.sh --check` reports **26 existing diagnostics in
nine HEAD-identical files**. Changed-file pinned formatting, Black, Python syntax
and diff checks pass. Full CTest/compiler lit, class-initialization and complete
DOM lit suites, broad corpus/native matrices, WPT and test262 were skipped.
No browser source/runtime changes or push; historical Bootstrap measurements
were not replayed.

**Exact next:** join complete original class and DOM effect proofs, then normalize
ordinary instance fields before DOM admission. `ClassInitialization` remains
closed-source-only and explicit entry parameters must be unused. `DOMSource`
classifies ordinary objects with field reads as callable holders, while `DOMEntry`
classifies fresh objects as JSON aggregates, rejects element-valued fields and
lacks ordinary instance-field reads. Reuse constructor lifting's existing
new.target, primitive-return, prototype and receiver proofs and the new direct
inliner; preserve every original unused method/getter's effect obligations before
erasure. The preserved `class_key`, `class_order`, and `class_element` specimens
are the next concrete boundary. Remaining Config, inheritance, full H Unicode,
retained callbacks, application driver and String power/other conversions remain
open. Native Bootstrap is not complete.
