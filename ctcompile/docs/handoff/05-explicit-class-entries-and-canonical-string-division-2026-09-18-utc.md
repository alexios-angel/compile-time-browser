[Back to HANDOFF.md](../HANDOFF.md)

## Explicit class entries and canonical String division, 2026-09-18 UTC

Continued clean **18cc8b1b** after the **11:14:38 AGENT-SYNC journal** closed
its predecessor. Resumed the explicit class-entry boundary recorded in HANDOFF;
September 7 WIP was absent. Independent agents implemented the escape slice,
reviewed the proof, and drafted native tests. Root recovered the test draft after
its agent reached a service limit and narrowed it to the implemented boundary.

**1ed820cb** allows class preparation to select an explicit imported entry
under `closed-source-v1`. The existing inert-declaration proof checks its script
wrapper. Explicit parameters must be unused; callee identity can only create
local closures, and receiver/new.target checks remain. Every original body still
receives the complete effect/prototype census. The script entry keeps its old
argument restriction. Wrapper, publication, constructors and method bodies remain
for subsequent representation proofs. **No new DOM provider or native execution
admission is claimed.** Two entries prepare; source and adversarial checks retain
the used-parameter, callee, wrapper, prototype, unknown-call and DOM refusals.

**33732dbd** reuses the existing canonical decimal parser for either String
operand of bounded division/remainder. Exact quotient/nonzero divisor and signed
magnitude rules are unchanged; saved values retain their original identities
through CFG/SCF transport. Noncanonical strings, fractional quotients, zero
divisors and changing/repeated producers remain unproved. All eight original
source-oracle bodies remain intact.

Focused devbox validation only; evidence: `/tmp/ctcompile-class-entry/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`: **8 build actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.97s; total 0.98s)**.
  Dense/induction/structured rows: **589 / 670 / 309**; budget cutoffs:
  **23,271 / 45,318 / 22,645**.
- Exact `Analysis/Escape/escape-claims/signed-division.test`: **1/1 (0.13s)**,
  **26 observed sites / 13 sound / zero violations / 13 of 15 precision**.
- Exact `CTNative/Lowering/Objects/class-entry.mlir`: **1/1 (0.76s)**,
  **2 preparations / 4 Node/interpreter observations / 26 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (193.47s)**;
  **146 source observations / 364 native executions / 292 unprepared refusals /
  176 preparation refusals**, plus **16 constructed-method executions / 20 refusals**,
  **8 original r executions / 4 refusals**, **4 key executions / 6 refusals**, and
  **11 prepared native refusals**. Global-chain first complete budget: **418**.

Eight selected source/test hashes match locally and on the devbox. The class
regression checks both optimization settings, explicit/deduced C++ and GCC/Clang;
emitted C++ contains no ctbrowser dependency or prototype/home metadata. Its
original Config r/M/F/H/W specimen still measures **a=7** in Node/interpreter
and refuses preparation.

The new entry fixture initially omitted required closed-source manifest fields,
then counted retained metadata/unused constants as live class operations, and
named an ambient-call control so it called itself. Those fixture defects were
corrected; the production implementation needed no post-build correction.
Required pinned `tools/format.sh --check` retains **26 existing diagnostics in
nine HEAD-identical files**. Changed-file pinned formatting, Black, Python syntax
and diff checks pass. Full CTest/compiler lit, DOM lit, broad corpus/native
matrices, WPT and test262 were skipped. No browser source/runtime changes or push.

**Exact next:** join class and DOM effects, then normalize constructor/method
receivers before `prepareDOMEntry`. The existing constructor lifter already proves
new.target, primitive returns, prototype immutability and closed receiver uses.
Its direct calls carry an explicit object receiver and undefined callee;
`DOMSource` currently expands live closure calls and maps the receiver to the
entry receiver. It needs a proved receiver-aware normalization before those
representations compose. All original method/getter bodies must keep their effect
obligations, including unused bodies; a residual-only proof after erasing them
or a union of intrinsic lists is insufficient. The present class provider remains
closed-source-only and observed entry parameters still refuse. Remaining Config,
inheritance, full H Unicode, retained callbacks, the application driver and further
String bitwise/shift/power conversions remain open. Whole-Bootstrap/Button/Data
measurements remain historical.


## Shared DOM preparation and canonical String products, 2026-09-18 UTC

Started at clean **a4a35dcc**. The **10:57:53 AGENT-SYNC journal** explicitly
closed the previous interruption; September 7 WIP was absent. Resumed its
recorded class/DOM composition boundary. Three agents investigated native tests,
proof composition and escape analysis; the two native agents reached service
limits without edits. Root completed the native work and gated the escape draft.

**2c4eb46a** extracts the existing DOM preparation transaction from
`LowerToEmitC` into `HostContract/prepareDOMEntry`, declared in `Preparation.h`.
The caller now reuses one URI/helper/element-guard/iteration sequence, including
wrapper removal, callback visibility, undefined/force normalization and final
source reproof. Source IR and the refreshed host contract publish together only on
success. The existing URI source checks two successful providers and six
stale-manifest/missing-intrinsic/zero-budget refusals, preserving both inputs.
This is a prerequisite refactor; it admits no new class/DOM programs.

**9336d642** reuses the canonical decimal String parser for either operand of
bounded multiplication, then applies the existing signed-magnitude and overflow
proof. Saved String values, zero and negative Number results retain their source
identities through CFG/SCF transport. Noncanonical/range/BigInt inputs, repeated
producers and changing backedges remain refused. All eight original oracle
function bodies remain intact; two former String refusals are now proved.

Focused devbox validation only; evidence: `/tmp/ctcompile-class-dom/`.

- Explicit targets: `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`, `ctjs-translate`,
  `ctjs-opt`, `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`
  and `ctcompile-tool`. Initial four-target escape build: **5 actions**;
  final eight-target build: **10 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.93s; CTest total 0.94s)**.
  Dense/induction/structured rows **585 / 639 / 295**; budget cutoffs
  **23,096 / 43,185 / 21,327**.
- Exact `Analysis/Escape/escape-claims/signed-product.test`: **1/1 (0.11s)**,
  **33 observed sites / 17 sound / zero violations / 17 of 19 precision**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.44s; CTest total 3.45s)**,
  including the new complete DOM preparation transaction checks.
- Selected existing DOM String fixtures `helper_object_method`,
  `helper_object_extracted`, `helper_object_multiple`, `helper_regex_original_h`:
  **16 Node/interpreter observations / 8 combined GCC/Clang executions /
  24 provenance checks / 88 refusals**.
- Selected `nullable_uri_original`, `nullable_uri_helper` and
  `dataset_guarded_values`: **20 URI + 5 dataset Node/interpreter observations /
  8 combined GCC/Clang executions**, including returned String lifetime and
  HTML/SVG dataset checks. These and the four holder fixtures cover both DOM
  providers, optimization settings and explicit/deduced C++.
- Existing `EXPLICIT_FORCES` source: **8 native executions**, ordinary DOM
  provider, both optimization settings/layouts and GCC/Clang. All emitted and
  linked native checks exclude Script/AOT dependencies. These selections do not
  constitute complete DOM lit cases or a broad native matrix.

Both final gate wrappers exit **0**; ten selected input hashes match the devbox.
The initial array check failed **0.97s total** on six assertions for the original
`"0" * 0` shrink. Its source stayed intact; the expectation now records the
proved empty array. Required pinned `tools/format.sh --check` retains **26
existing diagnostics in nine HEAD-identical files**. Stable formatting passes
**918 C++ / 109 Python / 105 web files**; changed-file pinned formatting and
diff checks pass. Full CTest/compiler lit, class lit, complete DOM lit cases,
broad corpus/native matrices, WPT and test262 were skipped. No browser source,
runtime semantics or historical Bootstrap measurements changed; no push.

**Exact next:** prove class construction/prototype normalization inside an
explicit DOM element entry before calling `prepareDOMEntry`. The audit found
three concrete gaps: class preparation requires script entry index 0; it requires
constructed instances and leaves their `ConstructOp`/method bodies; DOM helper
expansion/entry proof has no ordinary class-instance representation, and the
host-manifest path skips the ordinary closure/constructor lifter. Preserve the
complete original body/effect and receiver proof while joining those paths.
An intrinsic-list union cannot supply those obligations. The original Config
r/M/F/H/W defaults specimen's last measurement remains **a=7** in Node/interpreter
with preparation refusal; it was not rerun here. Remaining Config, inheritance,
full H Unicode, retained callbacks and the application driver remain open.
Escape String Div/Mod and additional unary/shift conversions are still unproved.

## Native global helpers and canonical String unary snapshots, 2026-09-18 UTC

Started at clean **c32fee6a**. The **10:33:17 AGENT-SYNC journal** closed the
previous interruption, and September 7 WIP was already merged. Continued its
exact prepared-global-holder boundary. Independent agents drafted tests and
escape work and reviewed the native proof; root completed both drafts after
service limits.

**6774a550** expands proved global callable-holder reads into direct private
helper calls during class preparation, after the complete source/prototype and
host-binding checks. Targets have no captures or receiver/new.target/callee
observations. Missing arguments retain undefined padding; surplus arguments
refuse. Holders requested through host roots/observations refuse removal.
Calls and effects stay in order; reads/loads follow private method clones through exit normalization.
Holder storage and unreferenced helper definitions then disappear. Native output
uses the existing direct-function lowering, with no global object carrier.

The four original holders now execute natively (**32 added executions**). A new
loop/return-dispatch specimen covers remapped reads, short calls and an unused
slot (**8 executions**, result **38**). Both optimization settings, explicit/deduced
C++ and GCC/Clang pass. Unsafe order, replacement, aliasing, identity, receiver
and ambient-effect controls remain. The complete original
r/M/F/H/W defaults specimen still returns **a=7** in Node/interpreter and refuses
preparation.

**53d6db64** reuses the bounded canonical decimal parser for unary Plus/Neg String
snapshots. It preserves original result identities, signed magnitudes, saved
values and CFG/SCF transport. Noncanonical strings, changing backedges and
negative own indices remain unproved. All six original source-oracle functions
remain byte-identical; two add retained/noncanonical controls.

Focused devbox checks only; exact evidence: `/tmp/ctcompile-global-native/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`: initial **9 build actions**. Array expectation
  retry: **2 actions**; the fixture-only native retry needed no build work;
  native cleanup retry: **3 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.94s)**. Dense/induction/
  structured rows **585 / 628 / 291**; budget cutoffs **23,079 / 42,090 / 21,009**.
- Exact `Analysis/Escape/escape-claims/signed-unary.test`: **1/1 (0.11s)**,
  **26 observed sites / 13 sound / zero violations / 13 of 15 precision**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (191.22s)**;
  **146 source observations / 364 native executions / 292 unprepared refusals /
  176 preparation refusals**, plus **16 constructed-method executions / 20 refusals**,
  **8 original r executions / 4 refusals**, **4 key executions / 6 refusals**, and
  **11 prepared native refusals**. Global-chain first complete proof budget: **418**.

Both final wrappers exit **0**; nine selected source/test hashes match locally
and on the devbox. The class execution checks reject any `ctbrowser::` dependency
or prototype/home metadata in emitted C++.

The initial array check failed **0.93s** on sixteen stale assertions; original
sources stayed intact, with canonical zero/unit expectations corrected and an
additional noncanonical stale-solver mutation retained. The first class attempt
failed **33.78s** because the new fixture lacked the intended dispatch shape;
the next failed **33.60s** on an unused private helper body. The final cleanup
removes that definition only after all holder rewrites and empty symbol scans.
Required pinned `tools/format.sh --check` retains **26 existing diagnostics in
nine HEAD-identical files**; stable formatter **916 C++ / 109 Python / 105 web**,
changed-file pinned formatting, Black, syntax and diff checks pass. Full CTest,
full compiler lit, DOM String tests, broad corpus/native matrices, WPT and
test262 were skipped. No browser source/runtime changes or push.

**Exact next:** jointly prove class initialization and the existing DOM provider
for original H. Class preparation currently permits only closed-source-v1 with
class helper/optional Error; DOM entry/source contracts remain separate. Reuse
`LowerToEmitC`'s existing `withProvedClone` sequence (URI, DOM helpers, element
guards, iteration, reproof) and public H/M/F helpers. Merely combining intrinsic
allow-lists would omit body/effect obligations. Original Number/JSON/URI,
RegExp/TypeError and iterator/destructuring paths remain open, along with the
remaining Config work, inheritance, full H Unicode, retained callbacks and the
application driver. Whole-Bootstrap/Button/Data numbers remain historical.

## Recovered global holders and String-left subtraction, 2026-09-18 UTC

Resumed **0d85f2d9** with five uncommitted predecessor files. The
**10:13:29 AGENT-SYNC journal** marked that iteration abandoned; its claims and
`git diff` identified global-holder tests and the String-left escape draft.
No September 7 WIP remained unmerged. Independent agents recovered both test
areas and reviewed the proof; root finished integration after service limits.

**acd89055** extends the shared callable-holder proof to a unique global
publication in the script entry. All fixed callable slots must precede publication,
and the entry prefix must be incapable of invoking source code. This establishes
order for subsequent cross-function reads without trusting resolver annotations.
Every binding write, holder alias and callable use is checked; receiver, callee
and new.target observation still refuse. Every slot body, including unused slots,
keeps the complete effect census. An unrelated earlier call deliberately remains
outside this bounded proof; widening needs a complete caller-order analysis.

Four global holders now pass class preparation. Their original global stores,
loads, callable slots and calls remain, and **all eight optimized/unoptimized
native attempts still refuse**. This adds preparation coverage, not global native
execution. Early/indirect calls, late slots, replacement, alias writes, detached
calls, identity/receiver observation and ambient effects remain refusal controls.

**752b0177** reuses the existing canonical decimal parser for the left String
operand of subtraction. Saved Number lengths, either/both String operands,
negative snapshots, exact cancellation and CFG/SCF transport keep their original
result identity. Noncanonical strings and changing backedges remain unproved.
Two historical length expectations were corrected with their original source
bodies retained; a noncanonical stale-solver mutation remains a refusal.

Focused devbox validation only; final wrapper exit **0**, eleven selected input
hashes match locally and remotely:

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-tool`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`.
  Initial build: **37 actions**; expectation-only retry: **2 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.99s)**.
  Dense/induction/structured rows: **585 / 599 / 283**; budget cutoffs:
  **22,931 / 40,266 / 20,317**.
- Exact `Analysis/Escape/escape-claims/signed-subtraction.test`: **1/1 (0.12s)**;
  **48 sites / 26 sound / zero violations / 26 of 28 precision**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (174.47s)**;
  **144 source / 324 native / 288 unprepared / 173 preparation refusals**,
  plus **16 ordinary executions / 20 refusals**, **8 original r executions /
  4 refusals**, **4 key executions / 6 refusals**, and **19 prepared native
  refusals**. Global-holder-chain first complete proof budget: **416**.
- Existing DOM String fixtures `helper_object_method`, `helper_object_extracted`,
  `helper_object_multiple`, `helper_regex_original_h`: **16 Node/interpreter
  observations / 8 combined GCC/Clang executions / 24 provenance / 88 refusal
  checks**, both providers, optimization settings and C++ layouts. Generated
  output and linked symbols pass Script/AOT exclusion. This selects existing
  helpers; it is not the full DOM String lit case.

The first array run failed **0.92s** on thirteen assertions from those two old
expectations. The first class run failed **18.96s** because its new slot-count
assertion mixed identical SSA names from different functions; the check now
examines the script entry. Both corrections changed tests only.
Required pinned `tools/format.sh --check` retains **26 diagnostics in nine
HEAD-identical files**. The stable formatter workflow passes **916 C++ / 109
Python / 105 web files**; changed-file pinned formatting, Black, Python/shell
syntax and diff checks pass. Full CTest/compiler lit, full DOM String case,
broad corpus/native matrices, WPT and test262 were skipped. No browser source
or runtime semantics changed. Evidence: `/tmp/ctcompile-global-resume/`.

**Exact next:** give the four global holders a native lowering using the shared
proof and existing closure machinery. `ClosureLifting/Methods.cpp` still rejects
`StoreGlobalOp` in `usesCloseTheShape`; global receiver provenance also needs a
consumer. Promote the existing `PREPARED_ONLY` cases only after native execution.
Then compose class preparation with the existing DOM provider for original H.
The complete original r/M/F/H/W defaults specimen still returns **a=7** in
Node/interpreter and refuses preparation; global identity proof alone supplies
no DOM effects. Reuse the public H/M/F helpers. Iterator/destructuring,
RegExp/TypeError exits, remaining Config helpers, inheritance, full H Unicode,
retained callbacks and the application driver remain open. Whole-Bootstrap,
Button and Data counts remain historical.

## Local callable holders and signed String subtraction, 2026-09-18 UTC

Started at clean **46e8ae18**. Its **09:50:04 AGENT-SYNC journal** explicitly
closed the preceding interruption; September 7 WIP was absent. Continued the
recorded Config/H boundary with independent escape implementation and native
proof review. The Config audit agent hit a service limit before editing; root
completed its recommended extraction.

**b83da7d5** shares DOMSource's existing local callable-holder identity/order
proof through `analyzeLocalCallableObject` in ClosedCallable. Class preparation
adds exact callee-use checks, unique closure storage, and non-observation of
receiver, new.target and callee. Held helpers are checked before their enclosing
direct callers, so an arrow may save lexical `this` without reading it. Every
slot body, including unused slots, still receives the complete effect census.
The class pass leaves holder operations intact for the existing closure lifter.
Four local method/arrow/branch/argument-order specimens add **32 native
executions**, both optimization settings, both C++ layouts and GCC/Clang.
Replacement, alias writes, detached calls, receiver observation, ambient effects
and global holders remain refusal controls. DOM preparation retains its existing
behavior and diagnostics; browser source and runtime semantics were not changed.

**6bf2351f** extends the existing canonical decimal String offset proof to negative
subtraction results and negative left Number snapshots. It preserves original
result identity, saved lengths, CFG/SCF transport and the bounded magnitude domain;
noncanonical/coercible alternatives, overflow and changing backedges remain
unproved. Original formerly refused String bodies are retained as positive checks.

Focused devbox validation only; ten selected input hashes match locally/remotely:

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-tool`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`.
  Initial three-target build: **34 actions**; caller-order correction: **3**;
  final seven-target build: **7 actions**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (172.22s)**,
  **130 source / 324 native / 260 unprepared / 159 preparation refusals**,
  plus **16 ordinary executions / 20 refusals**, **8 original r executions /
  4 refusals**, **4 key executions / 6 refusals**, and **11 prepared native
  refusals**. The local-holder arrow's first complete proof budget is **131**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.92s)**.
  Dense/induction/structured rows: **585 / 580 / 278**; budget cutoffs:
  **22,879 / 39,132 / 19,684**.
- Exact `Analysis/Escape/escape-claims/signed-subtraction.test`: **1/1 (0.12s)**;
  **34 sites / 18 sound / zero violations / 18 of 20 precision**.
- Selected existing DOM String fixtures: `helper_object_method`,
  `helper_object_extracted`, `helper_object_multiple`, `helper_regex_original_h`.
  **16 Node/interpreter observations**, **8 combined GCC/Clang executions**
  across both providers, optimization settings and layouts, **24 existing method
  provenance checks**, **88 existing helper-object refusal checks**. Generated
  output and linked symbols pass the Script/AOT exclusion. This was a focused
  invocation of existing helpers, not the full `native-dom-strings.test` case.

The first class run failed **7.48s** on the new arrow caller-order gap, fixed
before the passing rerun. The temporary DOM harness first lacked Node on bare
SSH's PATH, then selected Clang 18 without `std::expected`; using lit's configured
Node and DOM Clang resolved both harness failures. Only the DOM probe was rerun;
no source changes or broad test replay were needed. Final DOM wrapper exit **0**.
Required pinned `tools/format.sh --check` retains **26 diagnostics in nine
HEAD-identical files**. Stable formatter workflow **916/109/105**, changed-file
pinned formatting, Black, Python syntax, shell syntax and diff checks pass.
Full CTest/compiler lit, full DOM String case, broad corpus/native matrices,
WPT and test262 were skipped. Evidence: `/tmp/ctcompile-config-holder/`.

**Exact next:** extend the shared holder proof to original H's global publication
and cross-function initialization order, then compose class and DOM provider
contracts. The complete original r/M/F/H/W defaults specimen still returns **a=7**
in Node/interpreter and refuses class preparation; local SSA holder proof does
not authorize global H or its DOM effects. Reuse the existing public H/M/F DOM
helpers. Iterator/destructuring, RegExp/TypeError exits, remaining Config helpers,
inheritance, full H Unicode keys, retained callbacks and the application driver
remain open. Whole-Bootstrap/Button/Data counts remain historical.

## Original class-method r and signed unary literals, 2026-09-18 UTC

Started at clean **07764b25**. Its **09:33:03 AGENT-SYNC journal** closed
the preceding interruption; no September 7 WIP remained unmerged. Continued the
recorded original class-method **r** boundary. Independent agents investigated
escape precision, Config/H composition and prototype soundness; two hit service
limits, so root completed and gated the frozen escape draft.

**aa5aa952** distinguishes exact String keys, including the empty string, and
bounded Number keys from unknown keys in the existing prototype-method census.
It reuses public `ctbrowser::number_to_string`; it adds no emitted platform or
runtime code. Bootstrap r's numeric `[0]` read no longer blocks the unrelated
`read` method. Constructor lifting can remove the proved prototype, and the
existing private-caller scalar proof then folds the original `r(null)` guard.
The unchanged original class-method specimen now returns **a=7** in Node,
interpreter and native C++: **4 optimized native executions**, explicit/deduced
output and GCC/Clang. Unoptimized and unprepared forms remain refused. The
empty-string IR control adds **4 executions**; negative-zero/name collisions,
NaN, both infinities, a large finite key and an unknown key retain **6 refusals**.
Successful output contains no ctbrowser or prototype metadata.

The Number-key proof deliberately remains bounded to `abs(value) < 1e15`.
Review found that `ctbrowser/lib/Core/number_format.cpp` casts to `int64_t`
before its range test. The compiler avoids that existing large-value hazard;
the browser finding is recorded in AGENT-SYNC. No browser file was changed.

**1dd56617** preserves original bounded negative Number literals through unary
Plus/Neg using the same shared contents transfer as held signed snapshots.
CFG/SCF transport, original result identity, signed zero, retention and budget
checks are covered. Coercion, fractional/nonfinite/out-of-domain values and
changing backedges remain unproved. Two historical `-(-1)` length expectations
were corrected to retain the unit-length array; their source bodies and the
stale-solver mutation check remain.

Focused devbox checks only (final wrapper exit **0**, seven selected input
hashes match locally and remotely):

- Explicit targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`.
  Initial three-target IR probe: **5 build actions**. Final six-target gate:
  **6 actions**, followed by **2** after the length-expectation correction.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.93s)**.
  Dense/induction/structured rows: **585 / 559 / 273**; corresponding budget
  cutoffs: **22,876 / 37,780 / 19,126**.
- Selected lit cases: `Analysis/Escape/escape-claims/signed-unary.test` and
  `CTNative/Lowering/Objects/{class-initialization,constructor-refusals,prototype-scalars}.mlir`:
  **4/4 (160.75s total)**. Signed-unary oracle: **20 sites / 8 sound / zero
  violations / 8 of 12 precision**.
- Class gate: **120 source observations / 292 native executions / 240 unprepared
  refusals / 149 preparation refusals**, plus **16 ordinary executions / 20
  refusals**, **8 original r helper executions / 4 refusals**, **4 key-control
  executions / 6 refusals**, and **11 prepared native refusals**. The branching
  helper's preparation budget remains **419**. Prototype scalars: **7 source /
  48 native / 2 refusals**; constructor controls: **11 source / 8 native /
  18 refusals**.

The first final gate failed SSH reachability before building; authorized
`server.sh start` and `allow-ip` restored access. The next array run failed
**1.81s** on nine assertions from the two historical length controls; the new
CFG/SCF controls passed. Correcting those expectations required no compiler
change. Native and source bodies stayed unchanged. No browser/runtime semantics
changed.

Required `tools/format.sh --check` retains **26 diagnostics in nine HEAD-identical
files**. The same full formatter workflow with installed stable clang-format
passes **916 C++ / 109 Python / 105 web files**; changed-file pinned formatting,
Black, Python syntax and `git diff --check` pass. Full CTest/compiler lit, broad
corpus/native matrices, WPT and test262 were skipped. Evidence:
`/tmp/ctcompile-class-guards/`.

**Exact next:** compose Config's class proof with original H's callable holder
and the existing DOM provider. In the saved full r/M/F/H/W IR, H.getDataAttribute
is an arrow closure (`fn$18`) retaining lexical `this`, stored in H; it is outside the
class proof's direct-helper set, so the whole-module effect census refuses it.
Even proving its unused receiver leaves cross-function H identity/order and
separate class-versus-DOM intrinsic contracts in `ClassInitialization.cpp`,
`Contract.cpp` and `DOMSource.cpp`. Reuse the existing H/M/F public DOM helpers;
adding intrinsic names alone does not prove composition. Object.entries and
destructuring, RegExp/TypeError exits, remaining Config helpers, inheritance,
full H Unicode keys, retained callbacks and the application driver remain open.
General Number powers stay outside escape proof. Whole-Bootstrap/Button/Data
counts remain historical; no broad measurement was repeated.

## Closed scalar guards and original Bootstrap helpers, 2026-09-18 UTC

Started at clean **66174a17**. The interrupted declaration-borrow/bounded-power
thread was already closed in that commit and AGENT-SYNC's **08:54:45** journal;
no September 7 WIP remained unmerged. Continued the recorded original **r/H**
boundary, with independent escape implementation and caller-proof review.

**b680e2a0** joins scalar argument facts across the complete callers of private,
capture-free functions. The symbolic pass reuses its existing primitive folding
and branch selection, including after closure lifting exposes direct calls.
Calls and effects remain. Public/script entries, escaping or alternate-dispatch
callables, observed callee identities and arguments objects remain excluded;
CFG/SCF arguments stay unknown. Both optimization switches retain their controls.

**24832a56** pins Bootstrap's original **r** body in a closed global declaration
and a local closure. With **null** input and optimization enabled, both now
execute natively: **8 executions**, explicit/deduced C++, GCC/Clang, **a=7** in
Node/interpreter/native, with no ctbrowser or prototype metadata in output.
The immediate global-call form still lacks closed-caller authority. It remains
unchanged as a refusal control, alongside both unoptimized forms (**4 refusals**).
The original class-method r probe also remains a native refusal. A new fixture
includes original **r + M + F + all four H methods + W** without pruning bodies:
Node/interpreter return **a=7**, while preparation refuses an unknown call,
binding or reflective effect before the older named-H boundary.

**3f798821** separately proves exact negative-one base power parity for bounded
signed integer Number exponents. Saved lengths, original result identity and
CFG/SCF transport survive; coercion, nonfinite/fractional/out-of-domain exponents,
changing backedges and general powers remain unproved.

Focused devbox checks only:

- Six explicit targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **6 build actions**.
  Subsequent three-target native builds performed **9**, then **4** actions;
  final class rebuilds had no work.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.92s)**. Dense/induction/
  structured rows: **585 / 528 / 265**; budget cutoffs: **22,816 / 36,412 / 18,508**.
  `escape-claims/{unit-power,bounded-power,power-identities}.test`: **3/3 (0.22s)**;
  each reports **26 sites / 11 sound / zero violations / 11 of 15 precision**.
- `Precomputation/{arguments,precompute,precompute-pdll}.mlir`: **3/3 (0.10s)**;
  `Optimization/default-optimizations.mlir`: **1/1 (0.08s)**.
  `Lowering/Objects/primitive-fields.mlir` passed (**152 native / 62 refusals**)
  in the initial two-case selection, whose class case failed its new H diagnostic
  assertion (**73.19s** total). Compiler sources did not change afterward.
- Final exact `Lowering/Objects/class-initialization.mlir`: **1/1 (149.98s)**,
  **120 source observations / 288 native executions / 240 unprepared / 149
  preparation refusals**, plus **16 ordinary executions / 20 refusals**, the
  **8 r executions / 4 refusals**, and **12 prepared native refusals**.
  The branching-helper preparation budget remains **419**. Final wrapper exit
  **0**; six native and four escape input hashes match the devbox.

Initial focused runs caught an invalid short-call test operand list and an extra
analysis round affecting the existing public-only budget check; both were fixed.
The H diagnostic expectation and reference-runner stderr-statistics assertion were
corrected without changing source bodies. The immediate r call's public-function
refusal was retained. SSH access rotated mid-session; authorized `start` and
`allow-ip` restored it. No browser source or runtime semantics changed.

Required `tools/format.sh --check` still fails with **26 existing diagnostics in
nine files**: eight files are unchanged, and the flagged lines in the modified
`Symbolic/Facts.cpp` are unchanged. Stable full formatting passes **916 C++ /
109 Python / 105 web files**; Black, Python syntax and `git diff --check` pass.
Full CTest/compiler lit, broad corpus/native matrices, WPT and test262 were
skipped. Evidence: `/tmp/ctcompile-r-guards/`.

**Exact next:** carry the same caller authority through the original class-method
r/prototype boundary, then compose Config's class proof with the existing DOM
proof for H. `ClassInitialization.cpp`, `Contract.cpp` and `DOMSource.cpp` still
separate closed-source class intrinsics from DOM-provider authority; H's global
callable holder also needs cross-function identity/order proof. H.getDataAttribute,
M and F already use public DOM helpers, so no platform copy is needed.
Object.entries/destructuring, RegExp/TypeError exits, inheritance, full H Unicode
keys, retained callbacks and the application driver remain open. Whole-Bootstrap,
Button and Data coverage counts remain historical.

## Recovered declaration borrows and bounded powers, 2026-09-18 UTC

Resumed the eleven dirty compiler paths at `c675f002`, first recorded in
AGENT-SYNC at **05:02:53 / 05:20:56** and most recently abandoned at **08:07:19**.
Both interrupted drafts are now committed; no September 7 WIP remains unmerged.

**`fe2f5b7f`** extends the existing object-borrow census to unique closed hoisted
declarations. Every symbolic caller participates, including forwarded arguments;
caller and callee use the existing object-parameter carrier. Saved String fields
retain their owned bytes across mutation. Mixed/short calls, escaping or replaced
callables, and alias deletion retain refusal controls. The unchanged
`local-helper-order` class specimen now executes natively and returns **122**.

**`7133398b`** proves exact bounded Number powers with zero or positive-one
bases, including negative finite integer exponents for one. Original result
identity preserves signed zero; saved lengths and CFG/SCF transport retain the
same invariance and budget checks. General powers and unknown, coercible or
out-of-domain operands remain unproved.

Focused devbox validation (workflow exit **0**, **933 input hashes** matched
locally/remotely before and after):

- Explicit targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`; **38 build actions**.
- Exact `ctcompile_escape_analysis_arrays` CTest: **1/1, 1.74s** total.
  Dense/induction/structured rows: **585 / 512 / 259**; corresponding budget
  cutoffs: **22,816 / 35,040 / 17,884**.
- `Analysis/Escape/escape-claims/{bounded-power,power-identities}.test`:
  **2/2, 0.20s**. Each oracle reports **26 sites / 11 sound / zero violations /
  11 of 15 precision**.
- `CTNative/Lowering/Objects/{primitive-fields,class-initialization}.mlir`:
  **2/2, 151.50s**. Primitive fields: **152 native executions / 62 refusals**.
  Classes: **119 source observations / 288 native executions / 238 unprepared /
  148 preparation refusals**, plus **16 ordinary executions / 20 refusals** and
  **12 prepared native refusals**. Both optimization policies, explicit/deduced
  C++ and GCC/Clang pass; successful output has no ctbrowser/prototype metadata.
  The branching-helper proof still first completes at **419 steps**.

The initial gate failed SSH reachability before building. Authorized
`server.sh start` and `allow-ip` restored access; the retry passed without source
changes. Required `tools/format.sh --check` retains **26 diagnostics in nine
HEAD-identical files**. Stable formatting passes **916 C++ / 109 Python / 105 web
files**; changed-file pinned formatting, Black, Python syntax and
`git diff --check` pass. Full CTest/compiler lit, broad corpus/native matrices,
WPT and test262 were skipped. No browser source or runtime semantics changed.
Evidence: `/tmp/ctcompile-native-close/`.

**Exact next:** original W-only Config still refuses global **r**; original r+W
still refuses global **H**, with **a=7** in Node/interpreter. The original r method
probe prepares but its helper retains **a property read on an object that is not
a closed-shape literal**. Hoisted object borrowing does not prove this nullable
guard/property flow. Compose Config's closed-source class proof with the existing
DOM proof for H through `HostContract/ClassInitialization.cpp`, `Contract.cpp`
and `DOMSource.cpp`; merely allowing another intrinsic is insufficient.
H.getDataAttribute/M/F already use public DOM C++, so no platform copy is needed.
Object.entries/destructuring, RegExp/TypeError exits, inheritance, full H Unicode
keys, retained callbacks and the application driver remain open. Whole-Bootstrap,
Button and Data coverage counts remain historical.

## Local helper proofs and power identities, 2026-09-18 UTC

Started from clean `a0f1b0f7`: its interrupted Error/getter work was already
closed in the latest commits and AGENT-SYNC's **04:00** entry. Continued the
recorded **`r` / `_mergeConfigObj`** boundary, without reopening that recovery.

**`27cb4729`** proves exact parameterized local helpers during class preparation.
Cross-function globals reuse the closed hoisting-declaration proof, charged once
per binding. Structured helper bodies retain the complete effect census; unused
arrow `this` captures and literal Number keys are admitted. Replacement, observed
receivers, dynamic keys and ambient effects in uncalled helpers still refuse.
Scalar, branching and argument-order cases compile to ordinary C++ calls.

The original **W-only Config** fixture remains unchanged: Node/interpreter return
**a=7**, and preparation names missing global **`r`**. A separate fixture adds
Bootstrap's original `r` declaration verbatim; it also returns **a=7**, and now
names missing global **`H`**. The original `r` method probe passes preparation but
still refuses native property reads without a closed object shape. The mutable
object-argument helper likewise prepares but lacks native parameter typing.
Both source bodies remain as explicit native refusal regressions.

Final exact `CTNative/Lowering/Objects/class-initialization.mlir` lit passes
**1/1 (148.54s): 119 source observations / 280 native executions / 238 unprepared /
148 preparation refusals**, plus **16 ordinary executions / 20 refusals** and
**14 prepared native refusals**. Both optimization policies, explicit/deduced C++,
GCC and Clang run. Successful native output has no ctbrowser/prototype metadata.
The new branching helper first completes at **419** proof steps.

**`725713cf`** preserves bounded signed Number power identities for exponents
zero and one, including original result identity, saved lengths and CFG/SCF
transport. General powers, unknown/coercible operands and out-of-domain Numbers
remain unproved. Exact `ctcompile_escape_analysis_arrays` CTest passes **1/1
(0.89s)**; `power-identities` / `signed-right-shifts` lit passes **2/2 (0.22s)**.
New oracle: **26 sites / 11 sound / zero violations / 11 of 15 precision**.
Arrays: **582 dense / 489 induction / 255 structured rows**, with **22,660 /
33,091 / 17,498** budget cutoffs.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`. The combined retry
built **six actions**; the two subsequent three-target class rebuilds each built
**three actions**; the final three-target rebuild had **no work**. The first
combined build caught a test-only replacement-helper typo, corrected to the
adjacent `std::string::replace` pattern. Intermediate class runs stopped at the
mutable-object refusal (**7.71s**), missing Number-key proof (**11.77s**), and
original `r` native shape refusal (**11.92s**); sources were preserved. Final
workflow exits **0** and verifies **1,081 selected input hashes** locally/remotely.
Evidence: `/tmp/ctcompile-config-helpers/`, especially `class-final-gate.log` and
`gate-retry.log`.

Required pinned formatting retains **26 diagnostics in nine HEAD-identical
files**. Stable formatting passes **916 C++ / 109 Python / 105 web files**;
changed-file pinned formatting, Black, Python syntax and `git diff --check` pass.
Full CTest/compiler lit, broad corpus/native matrices, WPT and test262 were
skipped. No browser source or runtime semantics changed. Three parallel agents
were launched; service rate limits stopped them after the helper-source finding
and a 20-line escape draft, which root finished, gated and committed separately.

**Exact next:** prove the object argument/shape flow needed by original `r`, and
compose original **H** with Config through the existing DOM host contract.
Object.entries/destructuring, RegExp/TypeError exits, inheritance and DOM/default
composition follow. Full H Unicode keys, native throws, retained callbacks and
the application driver remain open. Whole-Bootstrap/Button/Data counts remain
historical; no broad measurement was repeated.

## Getter cleanup and signed right shifts, 2026-09-18 UTC

The interrupted Error/getter thread is complete in **`5927fdb6`**, with its
recovered measurements below. Two subsequent concerns landed separately:

- **`a427abbe`** removes unused throwing-getter chains in reverse dependency order.
  Every remaining numeric getter closure has a direct symbol call, so MLIR's
  symbol-use check preserves live callees and removes dead dependencies. A source
  probe first reproduced the native refusal from retained, unreachable Error
  getters. The new regression now compiles; Error construction in an uncalled
  ordinary method still refuses. Proof cutoffs are unchanged.
- **`bf363c05`** preserves bounded signed Number `Shr`/`UShr` snapshots using the
  existing bitwise transfer. Counts wrap and mask to five bits; signed right shift
  keeps its sign, while unsigned shift retains the unsigned result. Original
  identity, saved lengths and CFG/SCF transport survive. Negative magnitudes never
  become own-index facts; coercible, unknown, out-of-domain and unstable inputs
  remain unproved. This deletes the duplicate right-shift transfer.

Final focused devbox checks: exact class lit **1/1 (137.06s)** with **109 source
observations / 256 native executions / 218 unprepared / 139 preparation refusals**,
plus **16 ordinary executions / 20 refusals** and **ten preserved native throw
refusals**. Both optimization policies, explicit/deduced C++ and GCC/Clang run;
source/native observations agree, and output contains no ctbrowser or prototype
metadata. The throwing-chain proof still first completes at budget **293**.
Explicit `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` build:
**five Ninja actions**, including the frozen escape core.

Exact `ctcompile_escape_analysis_arrays` CTest passes **1/1 (0.89s)**;
`signed-right-shifts`, `signed-bitwise`, `signed-bitnot` lit passes **3/3 (0.33s)**.
The new oracle reports **26 sites / 11 sound / zero violations / 11 of 15
precision**. Arrays measure **581 dense / 482 induction / 252 structured rows**,
with **22,609 / 32,619 / 17,251** budget cutoffs. The explicit array/claims/type-oracle/
translate build performed **three actions**; the array-only retry performed **two**.

The first array run failed **1/1 (0.92s)** on four assertions for one old control:
`-1 >>> -1` now proves length one. Its original body and retained-child check
remain; only its expected admission changed. Compiler code was unchanged for the
retry. The class and final escape workflows exit **0** and verify **1,287 / 1,291
input hashes** locally/remotely. The class manifest excluded the three escape
headers the child was still editing; its compiler core was frozen. Evidence:
`/tmp/ctcompile-error-closeout/{chain-gate,shifts-gate,shifts-final-gate}.log`.

Required final pinned formatting retains **26 diagnostics in nine unchanged
files**. Stable full formatting passes **916 C++ / 109 Python / 105 web files**;
the later Length expectation edit also passes both formatters. Changed-file checks,
Black and `git diff --check` pass. Full CTest/compiler lit, broad corpus/native
matrices, WPT and test262 were skipped. No browser source or runtime semantics
changed. Review and the independent escape implementation ran in parallel;
root serialized builds, reconciled results and committed each concern.

**Exact next:** complete original Config still returns **a=7** in Node/interpreter
and refuses **`r` in `_mergeConfigObj`**, after passing throwing-NAME preparation.
Prove the local helper's identity/arguments and compose H's existing host boundary;
do not waive the whole-source effect census. Object.entries/destructuring,
RegExp/type checking and TypeError exits, then inheritance and DOM/default
composition remain. Native throw completion, full H Unicode keys, retained
callbacks and the application driver are unfinished. Whole-Bootstrap/Button/Data
counts remain historical; separately owned browser/Unicode work is tracked in
AGENT-SYNC. No source draft from this session remains uncommitted.

## Recovered Error getters and signed bitwise snapshots, 2026-09-18 UTC

**`5927fdb6`** finishes the four dirty Error/getter paths found at `2904c366`:
AGENT-SYNC's **03:10:19** class-only gate and **03:21:38** recovery were
abandoned at **03:12:41 / 03:22:56**. The September 7 WIP is already integrated.

A separately declared standard `Error` identity permits only exact construction
with one literal string, whose result stays with its throw. Throwing local
getters and their transitive callers retain source functions and direct calls,
preserving abrupt completion and closure scope. Unused single getters can be
removed. Ambient effects, escaping payloads, coercible messages and replacement
of Error still refuse. This supplies no native exception representation.

Fresh focused devbox validation: explicit `ctjs-opt`, `ctjs-translate` and
`ctcompile-test-native-reference` build, **no Ninja work**; ten-source probe
**10 source / eight native / 20 unprepared / 11 preparation refusals**, plus
**six native throw refusals**; exact class lit **1/1 (133.69s)** with **107 source
observations / 248 native executions / 214 unprepared / 138 preparation refusals**,
plus **16 ordinary executions / 20 refusals** and **ten native throw refusals**.
Both optimization policies, explicit/deduced C++ and GCC/Clang run. The throwing
getter chain first completes at budget **293**. Workflow exits **0**; all **1,290
input hashes** match locally/remotely before documentation edits.

The first fresh probe failed because the replacement-Error control's interpreter
also prints `Error=9` before `a=7`. Its expected output now preserves that global;
original JavaScript is unchanged. The saved predecessor failures also corrected
anonymous getter-symbol matching and counting diagnostic text as throw operations.
The prior exact host CTest **1/1 (0.46s)** applies to the same C++ hashes; it was
not rerun. Evidence: `/tmp/ctcompile-error-closeout/` and
`/tmp/ctcompile-error-finalize/`.

Already landed **`2904c366`** preserves signed Number BitAnd/BitOr/BitXor/Shl
snapshots. Recovered focused evidence: array CTest **1/1 (0.88s)** and
`signed-bitwise` / `signed-bitnot` / `signed-subtraction` lit **3/3 (0.32s)**;
new oracle **26 sites / 11 sound / zero violations / 11 of 15 precision**.
Arrays measure **569 dense / 453 induction / 242 structured rows**, with
**22,315 / 31,116 / 16,283** budget cutoffs. These checks were not replayed.

Required pinned formatting retains **26 diagnostics in nine HEAD-identical files**;
stable formatting passes **916 C++ / 109 Python / 105 web files**, and changed-file
pinned formatting, Black and `git diff --check` pass. Full CTest/compiler lit,
broad corpus/native matrices, WPT and test262 were skipped. No browser source
or runtime semantics changed.

**Exact next:** complete original Config still returns **a=7** in Node/interpreter.
Preparation now gets past `NAME` and refuses the ambient **`r` load in
`_mergeConfigObj`**. Its local helper identity/arguments and H calls need a
composable source/host proof; Object.entries/destructuring, RegExp/type checking,
TypeError exits, inheritance and DOM/default composition follow. Standalone native
throw completion, full H Unicode keys, retained callbacks and the application
driver remain open. Whole-Bootstrap/Button/Data measurements remain historical.
The review also identified conservative retention of unused throwing-getter
chains; a focused follow-up is in progress, alongside signed right-shift snapshots.

## Recovered literal throws and signed BitNot, 2026-09-18 UTC

Resumed the seven dirty ctcompile paths at `b71d8034` from the **02:14:59 /
02:21:33 AGENT-SYNC drafts**, abandoned at **02:18:10 / 02:23:31**. Finished
both before taking new implementation work. September 7 WIP is already an
ancestor. The ctjs checkout matches the merged `d2664e9` gitlink.

**`552db1c7`** lets proved local methods retain outer CFG exits and literal
primitive throws during class setup preparation. The complete effect and
receiver census still checks every body; object/parameter throws and ambient
calls in uncalled arms refuse. Constructors, setup and getters stay linear.
Both throw fixtures preserve their original switch/throw exits; the getter
fixture removes its exact `this.constructor.Default` reads. Native completion
lowering still refuses these methods: preparation supplies no throwing-call,
iterator or exception-payload authority.

**`3d0af951`** preserves exact signed Number BitNot snapshots through unsigned
ToInt32 wrap, read-time length snapshots and CFG/SCF transport. Negative
magnitudes never become own-index facts. Coercible inputs, out-of-domain
Numbers, repeated producers and changing latches remain unproved.

Focused devbox validation: explicit `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle` build passed in
**231 dependency actions** after the browser merge. Exact
`ctcompile_escape_analysis_arrays` CTest **1/1 (0.88s)** and escape lit
`signed-bitnot`, `signed-subtraction`, `signed-unary` **3/3 (0.32s)** pass.
The BitNot oracle measures **26 sites / 11 sound / zero violations / 11 of 15
precision**. Arrays cover **569 dense / 384 induction / 222 structured rows**,
with **22,159 / 27,521 / 14,271** respective budget cutoffs.

The corrected exact `CTNative/Lowering/Objects/class-initialization.mlir` case
passes **1/1 (135.10s): 98 source observations / 240 native executions /
196 unprepared / 128 preparation refusals**, plus **16 ordinary executions /
20 refusals** and **four prepared throwing-method native refusals**. Both
optimization policies, explicit/deduced C++, GCC and Clang run. Literal throw
preparation first completes at budget **652**. Its explicit three-target rebuild
had no work. The final class workflow exits **0** and verifies all **1,289 input
hashes** locally/remotely before documentation edits.

The first class run failed **1/1 (36.59s)** because the interrupted test confused
an unused `"constructor"` key literal with a property read. Inspection confirmed
both getter reads were removed. The corrected assertion checks those operations;
the original JavaScript is unchanged. Only the class case was rerun afterward.

Required `tools/format.sh --check` retains **26 diagnostics in nine unchanged
files**; stable formatting passes **916 C++ / 109 Python / 105 web files**.
Changed-file pinned formatting, Black and `git diff --check` pass. Full CTest,
full compiler lit, broad corpus/native matrices, WPT and test262 were skipped.
No browser source or runtime semantics changed. Evidence:
`/tmp/ctcompile-throw-finalize/`.

**Exact next:** the complete original Config (`W`) still returns **a=7** in
Node/interpreter. Its preparation refusal now reaches **static getter body is
not a closed expression**: `NAME` constructs and throws `Error`. The current
class manifest declares only the class helper, so Error identity/effects and
throwing getter expansion need a separate proof. Original Object.entries /
destructuring, RegExp/type checking and TypeError exits, then inheritance and
DOM/default composition remain. Standalone literal-method throw completion also
remains unlowered. Full H Unicode keys, retained callbacks and the application
driver are unfinished. The separately owned Unicode extraction remains tracked
in AGENT-SYNC. Whole-Bootstrap/Button/Data counts remain historical.
