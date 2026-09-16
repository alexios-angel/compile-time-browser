# What native Bootstrap needs next

## Current boundary: original snapshot iteration, 2026-09-16

**a607afc6 / d9112248** compile the original filtered dataset `for...of` for count,
ordered Strings and saved snapshots. Explicit original Array iterator/open/next/close
premises permit private specialization; the existing dataset/filter proof validates
the snapshot prefix, and the complete entry is reproved. Loop completion preserves
exact terminal tuples and scalar state. Each String extraction requires an immutable
vector, zero/unit index and its exact dominating length guard. C++ uses owning
vectors/strings and `vector.at`; no Script/VM/GC or browser implementation was added.
Loop DOM writes, vector mutation, helper escape, wrong guards/forwarding and incomplete
proofs refuse. Original source loops were not replaced with `.length` observations.

Focused **3/3 CTests (0.89s) / 2/2 lit (34.66s) PASS**; dataset **15 sources / 45
Node-VM source-double observations / eight GCC-Clang binaries / 228 refusals**, both
providers/policies/layouts, HTML/SVG and lifetime sanitizer. Escape remains **895 sites /
40 sound / zero violations / 40 of 172 precision (23.3%)**; all **1,123 historical rows**
match. The independent negative-Sub review found no defect; no new escape precision
gain is claimed.

Complete build and **305/305 CTests (2261.10s) / 256/256 lit (1977.19s) PASS**,
wrapper **0**, with no skips. All **1,774 frozen input hashes** match locally and on
the devbox before documentation edits. Fresh full Bootstrap remains **19/574 native /
0 of 47 globals**, Data **7/7**, and Button **4/86** with **22 Node-VM lifecycle
observations**. Both Bootstrap policy reports and Data/Button reports are byte-identical
to the previous gate; no full-bundle admission gain is claimed. Original M retains
**24 nine-register blocks / handler ^bb12**. The full WPT/test262 corpus measurement
was not rerun. Stable formatting passes **879 C++ / 104 Python / 105 web**; the required
pinned formatter reproduces the unchanged **nine files / 26 diagnostics** baseline.

**Exact next, measured in all four provider/policy modes:** the original count loop
now compiles **two native functions**, as does the filter. Full original
`H.getDataAttributes` now refuses **DOM helper branch contains an unproved local
identity**. Its filter closure is created inside the continuation of `if (!t)`, so
branch-local callable scheduling (or proof of that exact valid-element guard) remains
before full H admission. The isolated original `n.replace(/^bs/, "")` loop and the
following `charAt(0).toLowerCase() + slice(1)` loop both refuse **DOM property read
lacks a proved receiver and supported member**. The smallest independent String step
is exact anchored ASCII-prefix removal through ordinary String operations, with
original String/RegExp/factory identities and complete effect/use proof.

The next-key witness measures six Node/VM cases: ASCII, empty, emoji and supplementary
Deseret agree; `bsÉtage` gives Node `étage` versus VM `Étage`, and `bsİtem` gives Node
`i` + U+0307 + `tem` versus VM `İtem`. Current VM charAt/slice use byte positions and
lowercase is ASCII-only. The original expression selects one UTF-16 code unit in JS;
lowercasing the first full Unicode code point would also mishandle the Deseret case.
Record/coordinate this oracle boundary before claiming general key normalization;
the filter alone does not prove an ASCII suffix. No runtime expectations changed.

Live values can reuse public `ctbrowser::dataset_value`, but indexed-key provenance,
same-element presence and mutation epochs must be proved; missing own properties
have Undefined/prototype semantics. Loop-local M calls and dynamic result assignment
remain separate proofs: collisions preserve assignment order and `__proto__` uses its
inherited setter. Full Config/inheritance/defaults, retained callbacks and the
application driver remain open. The read-only `next-key-review.md` and executable
`next-probe.py` under the evidence directory describe these seams.

Claude's **6edb7421** runtime changes remain outside this gate and need fresh oracle
validation when integrated. Evidence: `/tmp/ctcompile-iteration-resume/`; details in
HANDOFF. Earlier entries below are historical.

## Previous boundary: snapshot length and original iteration, 2026-09-16

**0f3fbe91** proves exact `.length` reads on owning dataset-key and filtered String
snapshots, reusing existing native `vec_length`. The filter still requires its original
Array/Object/String identities and callback confinement; every snapshot requires no
vector mutation and complete proof budgets. Dataset checks pass **12 sources / 33
Node-VM source-double observations / eight GCC-Clang binaries / 150 refusals**, HTML/SVG
and lifetime sanitization. Original `for...of` sources remain unchanged and refused.

**e1bbd9e4** proves increasing dynamic Sub array latches with bounded original negative
Number strides, including literal negation and an unchanged carried stride. New escape
oracle: **15 sites / six sound / zero violations / six of eight precision (75%)**.
Historical **1,123-row** snapshot still matches: **895 sites / 40 sound / zero
violations / 40 of 172 precision (23.3%)**. Array suites cover **567 dense / 174
induction / 123 structured rows**. Three old diagnostic assertions now expect exact
MissingElement instead of UnknownIndex; all three sources still refuse, and the
corrected focused gate passes **3/3 CTests (0.91s) / 3/3 lit (27.32s)**.

Full **305/305 CTests (2262.84s) / 256/256 lit (1983.71s), wrapper 0**; **1,772 frozen
hashes** verified locally and on the devbox. Fresh full Bootstrap remains **19/574
native / 0 of 47 globals**, Data **7/7**, Button **4/86** with **22 Node-VM lifecycle
observations**. Both policy reports and Data/Button/next reports are byte-identical to
the prior gate. No skips or pruning and no full-bundle admission gain. Stable formatting
passes; the required pinned formatter has the unchanged **nine files / 26 diagnostics**
baseline. No browser implementation or runtime semantics changed. Evidence:
`/tmp/ctcompile-dataset-iteration/`; details in [HANDOFF](HANDOFF.md).

**Exact next:** the original filtered `for...of` needs proofs of helper identity,
original Array iterator behavior and scalar loop state, including the before-region
index switch and inactive final completion slot. Snapshot length is available; indexed
String reads still need an exact integral in-bounds proof before using existing
`vec_at`. The current completion copier handles one function-result value, not the loop
condition/yield tuple. Original IR and a preserved count/order/snapshot regression draft
are linked in HANDOFF. Then key normalization, live dataset values, M composition and
result writes remain; `__proto__` assignment has setter semantics. Matching/live F keys,
full Config/inheritance/defaults, retained callbacks and the driver remain open.

## Previous boundary: filtered dataset iteration, 2026-09-16

**32155832** compiles Bootstrap's original dataset key filter through an ordinary
native predicate and `std::copy_if`, with original Object/Array/String identities,
default Array species, callback confinement and complete-budget proofs. Dataset:
**11 sources / 29 Node-VM observations / eight binaries / 146 refusals**, HTML/SVG
and lifetime sanitization. **bf7a56d5** shares public Core surrogate normalization
with both native String concatenation and host-prefix evaluation. **4bf39afe**
proves the frontend's exact static-getter home setup. **56a39f38 / 0e0fa2d7 /
9659f0bf** recover moved-bytecode refusals, the measured escape snapshot and source
drivers. No browser implementation changed; generated code names no Script/VM/GC.

Full **305/305 CTests (2238.51s) / 255/255 lit (1954.77s), wrapper 0**;
**1,771 frozen input hashes** verified locally and on the devbox. Focused **3/3
CTests / 13/13 lit** pass. Stable formatting passes; the required pinned formatter's
unchanged **nine files / 26 diagnostics** reproduce from HEAD. Fresh Bootstrap is
**19/574 native / 0 of 47 globals** (previous global denominator 43), without skips
or pruning. Data is **7/7**, Button **4/86**; Node and VM now agree on all **22 original
Button lifecycle observations**. No full-bundle admission gain is claimed.

Escape: **1,123 rows**, changed PCs/hash and **79 Stored-to-Passed reasons**, with
all observations and confinement unchanged; **895 sites / 40 sound / zero violations /
40 of 172 precision**. Previously landed **618f5775** commuted Add induction is
included: **15/15 sites / 6 of 8 precision / zero violations**; current dense,
induction and structured suites have **567 / 151 / 109 rows**.

**Exact next:** count-only `for...of` over the filtered snapshot refuses **DOM helper
completion observes an inactive value**; full original `H.getDataAttributes` refuses
**DOM helper completion requires acyclic structured source**, each in all four
provider/policy modes. Prove the exact owning-vector iterator path and scalar loop
state before reusing existing vector/SCF lowering. Then dynamic key normalization,
live dataset values, M composition and result writes remain; `__proto__` assignment
has setter semantics. Matching/live F keys, inheritance/defaults, retained callbacks
and the driver remain open. Original M preserves **24 nine-register blocks / handler
^bb12**. Evidence: `/tmp/ctcompile-filter-1624/`; details in HANDOFF.

## Previous boundary: dataset keys, 2026-09-16

**2dbd73b6 / 3d80df96, 2026-09-16 UTC:** owning HTML/SVG dataset key snapshots
through public DOM, and exact bounded Number dynamic Add induction. Dataset input
namespaces and original Object.keys are explicit host premises. Five source-double
observations, eight native binaries/160 observations, lifetime sanitizer/50 refusals;
new escape oracle 15/15 sites, 6/8 precision, zero violations. **beb5c1db** replaces
the stale `i += 2` refusal with executed borrow/identity coverage, while String
strides still refuse (80 native executions / 24 copy controls / 38 refusals).
The first full run exposed only that stale expectation; its logs are archived and
the complete suite was rerun.
Full **288/288 CTests (2307.84s), 254/254 lit (2039.47s), wrapper 0**;
1,725 frozen hashes verified. Stable formatting passes; pinned baseline unchanged.
Bootstrap **19/574, 0/43 globals**, Data **7/7**, Button **4/86**, and all 1,123
historical escape rows are unchanged. No browser/runtime edits or full-bundle gain.

**Next:** original dataset `.filter(t => t.startsWith("bs") &&
!t.startsWith("bsConfig"))` refuses callback escape in all four modes; prove callback
and intrinsic identities, including default Array species, over the key snapshot.
Full H still refuses loop completion.
Live value reads need Undefined/prototype semantics, then M normalization and dynamic
writes; `__proto__` assignment differs from own-data spread. Inheritance/defaults,
retained callbacks and driver remain open. Real Chromium/Shell comparison measured
numeric-key ordering and stale saved-dataset differences. Chromium also includes
namespaced data attributes that the shared DOM core skips; native retains that
platform limitation. All three differences are journaled for Claude.
Details in HANDOFF and `/tmp/ctcompile-dataset-keys/measured.json`.

## Previous boundary: guarded Config spread, 2026-09-16

**fcd4a0c2 / 344b514d: guarded Config spreads and negated finite guards,
2026-09-16 UTC.** Recovered the abandoned 09:15:56 drafts before new work.
Original one/two-spread Config slices now own their JSON results, preserving
own-key order, overwrites and source failure snapshots. Complete fresh-target and
no-later-mutation proofs gate copying; no generic JSON member/capture authority.
Negated inclusive array guards preserve bounded Number, retention and budget proof.
Focused **4/4 CTests and 4/4 lit**; JSON covers **18 sources / 486 observations /
eight binaries / 260 refusals**, with lifetime sanitization. New escape oracle:
**15/15 sites, 6/8 precision, zero violations**; array rows **567/118/100**.
Full **288/288 CTests (2305.41s), 252/252 lit
(2029.51s), wrapper 0**, 1,721 frozen hashes verified. Stable formatter
passes; pinned 26-diagnostic baseline unchanged. Bootstrap **19/574, 0/43 globals**;
Data **7/7**; Button **4/86**; all **1,123 historical escape rows** unchanged.
No browser/VM changes or full-bundle gain. Evidence `/tmp/ctcompile-spread-resume/`; details in HANDOFF.

**Exact next native boundary:** original `H.getDataAttributes(e)` refuses in all
four provider/policy combinations at `DOM helper completion requires acyclic
structured source`. Its loop, `Object.keys(t.dataset).filter(...)`, dynamic key
normalization and per-key `M(t.dataset[n])` reads remain intact. Public
`dom/dataset.hpp` already supplies owning `dataset_entries` and `dataset_value`;
no browser extraction is needed for those reads. Prove the original iteration and
Object identity, namespace eligibility, key snapshot versus live reads, missing-key
semantics and dynamic writes. Its `e["__proto__"] = value` assignment has setter
semantics, unlike spread's own-data definition. Full Config additionally needs
`r(e)`, inherited defaults/initialization, retained callbacks and the driver.
Matching/live F keys remain refused. The independent plan25 continuation is bounded
Number `i += 1` induction: the shared latch recognizer still requires
`BinaryStaticOp`, although dynamic Add already uses `boundedNumberSum`.


## Previous boundary, 2026-09-16

**c127ba96** compiles Config's original
`"object" == typeof H.getDataAttribute(element, "config")` and the String tag.
Original M/H retain their nullable guards, source lookup order and both failure
snapshots. Emitted C++ observes an owning `ctbrowser::json_value` through standard
variant alternatives and returns an owning String. Null, arrays and objects all
report `"object"`; this does not prove object-only member access.

Focused **3/3 proof CTests (3.88s) / 3/3 lit (55.27s) PASS**. JSON covers **15 sources /
342 Node-VM observations / 8 GCC-Clang binaries / 172 refusals**, both
providers/policies/layouts and post-document lifetime sanitization.
Integrated build and **288/288 CTests (2252.04s) / 251/251 lit (1973.33s) PASS**, wrapper
exit **0**. All **1,720 frozen source/submodule hashes** match the devbox and integrated
**4c4f8e7b**; documentation changed afterward.
The gate includes Claude's **28878c6c / 0b1e0911** EmitC audit and its shared plain
C++ helper header. [The DOM contract](native-dom-entry.md) describes admission;
[HANDOFF](HANDOFF.md) records integration and evidence.

**Exact next native boundary:** adding Config's following spread
`{..."object" == typeof parsed ? parsed : {}}` refuses in all four modes at
`DOM helper branch contains an unproved local identity`. The branch-local empty
object reaches `DOMSource.cpp` before the final `copy_props` proof. Preparation
currently rejects the nested constructor; its later object census also assumes
every top-level constructor is a callable method holder and erases it. Preserve
proved data constructors through both stages, then require complete entry proof
of the spread and ownership. Null contributes no entries, arrays contribute indexed
entries, and the object tag alone grants no member proof. The existing escape
`CopyProps` certificate covers fresh fixed own-data objects, not runtime JSON keys
or enumeration order; generic native lowering has no `CopyPropsOp` case. Reuse
`carrier::json` ownership, explicitly constructing its object alternative for `{}`
(default `json_value{}` is null), and prove key order and overwrite behavior.
Spread is shallow: copying or moving an owning tree also needs a proof that surviving
aliases cannot distinguish it. Preserve numeric/duplicate/`__proto__` keys, array
indices without `length`, fallback inputs and post-document ownership in source tests.
Then compose dataset/config merging through the existing public `dom/dataset.hpp`.
Original M still has **24 nine-register blocks / handler ^bb12** before preparation.
Matching/live F keys, inherited static/object-valued defaults, initialization,
retained callbacks and the application driver remain open. Pending browser/runtime
branches remain Claude-owned and require fresh differential validation when landed.

**42a2bd80** independently extends the existing array induction proof to
`length > index`, preserving strictness, source evaluation order and all ownership
checks. Fifteen CFG/SCF cases and an original-source recorder/claims oracle pass;
the new oracle reports 9/9 observed sites, 3/5 precision and zero violations.

**40f1f3ef** already supplies the hoisted-declaration prerequisite for retiring
Claude's temporary bare-var write restoration in **7ad52ce2**. Runtime changes
remain Claude-owned.

Fresh full Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
with no skips or pruning. DOM Data remains **7/7**, Button **4/86**, with 22 Node
observations and the unchanged VM inheritance failure. Their measured reports and
all **1,123 escape rows** are identical to `/tmp/ctcompile-m-gate/`; historical
escape precision remains **40/172**, with zero violations. No full-bundle gain
is claimed.

The earlier milestones below are historical context, not alternative next steps.

**f017e1ea / dcd213d3 / 1772fc4f / 523e631d** compile the pinned original
Bootstrap Data probe with three direct DOM inputs: **3,218 bytes, 7/7 functions,
23 calls and 19 observations**. The nonmovable document session privately owns
Data, the recorder and the payload alias. All five input-alias partitions pass
Node/interpreter comparison, both policies/layouts/compilers, lifetime sanitizers
and mutation/privacy controls. See [HANDOFF](HANDOFF.md) for the final gate and
full-bundle measurements. The earlier survey below records superseded Data steps.

The registered `ctcompile_native_bootstrap_button_probe` now preserves an
**original Button construction/toggle/disposal probe**: **16,194 bytes / 86
imported functions / 4 native**, both optimization policies, no skipped functions.
It retains vendor lines **1–330 and 420–433**, including Config (`W`),
BaseComponent (`B`), Button (`U`) and live helpers. Its element is an explicit
JavaScript test double; this is a source/progress gate, not native DOM support.
Node passes **22 lifecycle observations**, including Data identity, config parsing,
two toggles and disposal's otherwise easy-to-miss event-registry mutation.

The interpreter now agrees with Node on all 22 lifecycle observations. The original
static probe and the separate inherited-method/getter witnesses also agree.
The original source remains pinned; native still claims only 4/86 functions and
continues to refuse the complete component. These updated observations are recorded
by **9659f0bf** after Claude's runtime compliance changes.

**075bdd9c** supplies the first trusted class-initialization slice. The explicit
`ctnative-specialize-class-initialization` pass binds the complete source fingerprint
and the host's initial `__ctbrowser_class_defined` identity. Only a complete local
base-class/setup/use census can remove its unobservable descriptor effects. The
empty/default and numeric own-field cases pass **16 native executions**, with
**26 unprepared refusals / 17 preparation refusals**. Methods, inheritance,
field-initializer closures, reflection, helper mutation and unknown source effects
remain refused. This pass is separate from the DOM Data provider and does not yet
prepare original Button. [Host contract details](native-host-slots.md) describe the
input and its limits; [HANDOFF](HANDOFF.md) records the full-gate status.

**fae7cac3** extends preparation to immutable local base-class methods. It proves
exact prototype/home uses, primitive constructor returns and unobservable method
identity before installing bindings on instances. Existing constructor and receiver
lowering then independently prove initialization order and method-key immutability.
The gate passes **40 class native executions / 48 unprepared / 30 preparation
refusals**, plus **8 plain constructed-method native executions / 10 refusals**.

**67867ae0** additionally admits chained calls through those immutable local methods,
reusing the existing receiver fixpoint. Nested mutation, argument evaluation order,
independent instances and unused receivers pass **72 class native executions**;
the expanded source gate retains **72 unprepared / 42 preparation refusals**.

**1fa7709e** makes immutable methods available during construction. The exact
prototype seeds the existing receiver fixpoint after preliminary constructor checks;
full constructor/method admission still gates lowering. Later instance stores never
supply earlier constructor methods. The preserved constructor-call/order sources
now execute as native C++ with results **8 / 132**. The expanded class gate passes
**104 native executions / 88 unprepared / 50 preparation refusals**, plus ordinary
method controls. The local proof remains capture-free and does not prepare Button.

**f6ee9bea** admits local scalar static getter chains under that same complete
class/host proof. Getter dependencies are acyclic and clone work is bounded before
mutation; every expansion stays at its original read. The class gate now passes
**152 native executions / 138 unprepared / 82 preparation refusals**, preserving
**69 source observations**. Static metadata collisions remain refused with separate
Node/interpreter controls. Bootstrap's inherited receivers and object-valued
`Default`/`DefaultType` getters are beyond this local scalar proof.

**Next compiler boundary: inherited instance and static-getter receivers**, default
derived forwarding, lexical `super` and observable `this.constructor`; then compose
with DOM Data ownership. BaseComponent calls `_getConfig` during construction and
observes `DATA_KEY`. Config merges defaults/dataset/config and reads inherited
`DefaultType`/`NAME`. These source operations cannot be erased. The interpreter's
static-inheritance discrepancy remains separately measured.

**3d45614c** now admits the unchanged `prototype-written.js` source through a
complete local immutable scalar-prototype census. Defaults initialize fresh fields
before constructor execution; inherited and constructor reads, conditional shadowing,
borrowed receivers and independent Number/Boolean instances pass 40 native executions.
The full constructor proof controls receiver eligibility. The original inherited-method source now executes unchanged under **1fa7709e**.
Remaining refusals retain late/alias/replacement mutation, new.target, constructor
arguments, arrows, the mutable helper and unsupported String/Null/Undefined field storage.
`unwritten-key.js` still refuses its inherited constructor observation. **158f1fef**
rejects constructing unused-this arrows. Its preserved source exposes another oracle
discrepancy: Node throws TypeError, while the interpreter's inline construct opcode
returns 7. Native refuses; the runtime finding is journaled for Claude.

**fef19039** additionally admits definite local String fields as `std::string`.
The preserved literal-primitives prototype now executes unchanged, bringing that
group to **48 native executions**. Its original saved-string gate passed **8 native
executions**. **f854d2f6** adds definite String length inference and `std::size`
emission; the expanded group passes **32 native executions / 10 refusals**, including
the unchanged original length source. ND-1's Unicode byte count remains separately
measured against Node. **be8781ac** now carries definite String fields across one
closed direct object-argument borrow, with exact initialization before every call
and all callee writes retained in the type join. Saved strings own their bytes.
**3e494a80** also proves stored-method callable provenance, keeping initialization
at every actual direct call and all callee writes in the type join. The String gate
passes **96 native executions / 30 refusals**. **ff125088** completes forwarded
parameters through the closed object-argument census and exact caller initialization
proof; the expanded gate passes **136 native executions / 52 refusals**. Mixed/possibly
absent String storage remains a separate proof. See HANDOFF for current gate status.

HostContract now accepts the helper's explicitly declared initial identity as well
as Map and Array. The local class pass accepts only its helper declaration; the
existing realm descriptor guard still rejects prototype writes. Broader composition
needs a shared complete provenance/use proof, not a helper-name exemption.
Component publication retains `_element` and `_config`, beyond Data's current
scalar-field leaf proof. See HANDOFF for measured gates and the final full-gate status.

Config still reads attributes and dataset when defaults are empty. **8547ad64**
lifts dataset name conversion, supported-property reads and ordered entries into
`ctbrowser/dom/dataset.hpp` and the DOM library. The Shell binding is now an
adapter over that core; native callers receive owning optional strings or vectors
of String pairs. Writes and removal reuse the public document namespace APIs.
The direct DOM/Core client and **8 WPT files / 47 subtests** pass, with identical
WPT results before/after; the complete **601/601 CTest / 176/176 lit** gate passes.

Native synchronous entries lower proved String-name `getAttribute` calls through
`get_element_attribute` in the public DOM library. The Shell binding uses the same
core. Results are owning `std::optional<std::string>` values; absent and empty
remain distinct, and copies survive later mutations and document destruction.
The source gate checks both DOM providers, printing layouts and optimization
policies with Node, the VM, GCC and Clang. **a41b3bc3** adds strict
null/String comparisons, optional String truthiness and definite String + String
names/values. Missing and empty are both false in Boolean observations; saved reads
keep their copied value after mutation. **0aa8dd47** expands
closed, capture-free straight-line calls at their original call sites, then
rechecks the entire DOM body. Nested name construction, repeated calls and saved
reads pass **105 Node/VM observations / eight GCC-Clang binaries**. Callable
identity, captures, recursion and unsupported effects remain refusals; this adds
no nullable-to-String coercion.
Bootstrap's original `getDataAttribute` (vendor line **263**) computes its name
through `F` and feeds the optional result to `M`. Exact local constant calls to
verbatim F now prove its replacement callback is never invoked, including distinct
names. Captured H/F calls now compose for the original set/remove methods.
`M` still requires source branches, `Number`, `toString`, `typeof`, URI
decoding, JSON parsing and exceptions. Preserve these source operations when
composing `_mergeConfigObj`'s actual `H.getDataAttribute(e, "config")` call.
The exported entry still declares every explicit parameter as an element; local
helpers receive their proved actual arguments. **1aa5c2c2** now proves unique local
object-held helper slots and exact method receivers under the isolated standard
Object-prototype premise. The expanded gate passes **145 Node/VM observations /
eight GCC-Clang binaries / 224 source refusals / 24 method provenance checks**,
with the earlier 41 provenance/depth and four budget/fingerprint controls intact.
**d1c1ab96** additionally proves local immutable leaf captures, including object-held
methods: **173 Node/VM observations / eight GCC-Clang binaries / 45 capture
provenance-budget controls**. It reuses the shared cell/closure proof and checks
assignment-before-read/call, then substitutes each invocation independently.
**18f4519b** composes local captured callable/holder graphs under the unchanged
leaf rules, with composed depth bounded by **e81304b4**: **213 Node/VM observations / eight GCC-Clang binaries / 304 source
refusals / 43 provenance-depth / 24 method / 53 capture controls**. Consumers
expand before cells and callable holders are retired; the complete DOM proof still
gates publication. The original two optional-return sources execute unchanged.
**122715d8** additionally expands nested capturing helpers and proves forwarded
immutable slots at each invocation: **249 Node/VM observations / eight GCC-Clang
binaries / 336 source refusals / 44 provenance-depth / 24 method / 74 capture controls**.
The original forwarded optional-return source executes unchanged. Shared leaf queries
remain unchanged; mixed slots and composed depth receive the private DOM proof.
**4ef7649c** additionally proves immutable block-local setup of one exported DOM
entry: **285 Node/VM observations / eight GCC-Clang binaries / 392 source refusals /
44 provenance-depth / 24 method / 113 capture-initialization-budget controls**.
A private invocation follows all initialization, including writes after publication;
existing expansion eliminates every setup cell/holder/callable before DOM reproof.
**2da43c9a** additionally prepares one uniquely called uncaptured local factory returning
the exported entry: **309 Node/VM observations / eight GCC-Clang binaries / 448 source
refusals / 44 depth / 24 method / 161 capture-initialization controls**. Exact source
creator/callee/argument/publication proof precedes cloning at the original call, then
complete capture and DOM reproof gate emission. **823b71b8** also proves one constant
String selection from a returned fresh table of own callable slots, with the same
complete identity, source-order and capture proof. Every unselected callable still requires an actual invocation;
this does not admit the complete Bootstrap export table. Top-level global bindings
and nested/captured factories remain refused. Original Bootstrap still needs its
complete factory/global initialization and H/F/M source graph. The expanded gate
passes **341 Node/VM observations / eight native binaries / 508 source refusals /
241 capture controls**; complete **602/602 CTests and 176/176 lit pass**. Full Bootstrap
remains **19/574 native**, DOM Data **7/7**, and Button **4/86**.
**ab7bf2a6** proves the verbatim vendor `F('config')` body in isolated native DOM
actions. The provider fixes the initial String/RegExp prototype chains and reserved
literal factory; exact local calls supply a common constant String, and the checked
no-match path never invokes its exclusively used callback. Matching/live inputs,
differing helper arguments, other patterns/flags and mutation still refuse. The
captured H/F graph and full factory/global initialization remain separate boundaries;
this action probe does not admit the full Bootstrap bundle. Original M still needs
live optional-value normalization, branches, Number/toString, URI decoding, JSON
and exceptions. See [HANDOFF](HANDOFF.md) for measured gates.

The compiler still refuses dataset operations. Bootstrap's original
`getDataAttributes` (vendor lines **253–261**) needs `Object.keys`, filtering, a
loop and dynamic reads. Prove those source uses and preserve live read order;
a missing supported own property alone does not prove the prototype lookup misses.
Native optional strings must use ordinary `std::optional<std::string>` storage.
Keep Bootstrap's parsing and config merge in its source. Disposal calls `P.off`,
which writes `uidEvent` and initializes registry state even without listeners;
it cannot be omitted. Retained events will need a plain platform callback seam.

A construct/toggle/dispose lifecycle can run entirely within one entry call.
The current entry recreates its Data/recorder/payload on every invocation, as
the source does. Persistent initialization followed by later actions requires a
separate ownership proof; deleting the existing resets changes the program.
Full native Bootstrap startup, components, retained callbacks and the application
driver remain unfinished.

## Earlier boundary survey

Surveyed on 2026-09-13 at `a4458ae1`; shared DOM token extraction landed as
`1e71c6ad` after the browser and compiler gates described in HANDOFF. Bootstrap is
**5.3.8**, **133,701 bytes**, SHA256
`5b29f1692a632853edc37b45bc1deedd595a777c9b234e8262ccd74ebfcf3d65`.

The subsequent [typed DOM entry](native-dom-entry.md) connects source functions
to real document/node handles and the shared token/attribute APIs. Its separate
action gate covers synchronous borrowed parameters, identity, mutation and errors.
Retained DOM-backed Data keys, original Button receivers/construction and full
bundle initialization remain the next boundaries; the survey below is not a
native Bootstrap completion claim. Synchronous query entries now call public
DOM containment and Style matching/closest, including Bootstrap's delegated
`[data-bs-toggle="button"]` ancestor lookup. They borrow the caller's live
Style engine and keep nullable closest results local to identity comparisons.
The shared closest implementation preserves `:scope` and shadow boundaries.

The separate `closed-source-session-v1` provider now uses the same manifest fields
and complete source ownership proof as `closed-source-v1`, and additionally requires
a captured method table. It emits Data methods as members of a noncopyable,
nonmovable table. Every source method read must feed its proved direct call;
an extracted callable cannot carry the captures away.
The existing `closed-source-v1` owning-callable contract stays available.

The Data session now owns its outer Map by value; its methods pass that member
directly to their source functions. The three redundant capture tuples and their
initialization are gone, and the table occupies exactly its Map storage.
Measured on 2026-09-14 with GCC13.3/Clang18.1.3: **48 → 24 bytes** per table;
the same generated probe loses **1793 bytes / 34 lines**, excluding test assertions.
Saved child Maps and payloads retain their independent ownership;
removal, reinsertion and session destruction must preserve those saved values.
The table and ordinary object carriers keep their existing ownership contract.
This completes the method-call and outer-Map prerequisites for a Data + DOM session;
it accepts no DOM parameters or keys. Owning atoms, document, then Data state in
a single session and proving document-domain key provenance remain the next boundary.
The registered `ctcompile_native_data_session` gate uses the same pinned Data
probe, with direct call order, typed Node/VM observations and lifetime checks.

The compiler now separately records complete-family `outerKeyObjects`: source
allocations used only as direct outer Map keys, with every actual use and named
alias rechecked. Payloads, child keys, field uses, transport and outer snapshots
exclude that role; ordinary owning-object admission remains. The pinned Data
probe has three such allocations (`element`, `other`, `absent`), excluding its
payload. This is source-use evidence, not DOM provenance or permission to retain
an externally supplied handle. Recovery commits `722ddd82` and `cb76be27` passed
the full **562/562 CTest / 168/168 lit** gate on 2026-09-14. The generated Data
probe remains byte-identical, and full Bootstrap admission remains **19/574**.
The combined Data/DOM boundary still requires explicit DOM origins across the
Data family and Data storage attached to the document lifetime. The subsequent
owning action and direct-array milestones below narrow the remaining work.

The separate `ctbrowser-dom-session-v1` action provider now owns atoms, its document
and an optional live Style engine in a nonmovable C++ class. Explicit element inputs
must belong to that document; all owner comparisons precede handle validation and
source effects. This completes the synchronous DOM owner, with no retained Data
keys or callbacks. Connecting it to Data still requires actual DOM origins in the
complete family proof and removal of escaping shared table/global ownership for
that mode. Distinct input parameters can alias the same node and must not be
assumed unequal. See [native-dom-entry.md](native-dom-entry.md) for the entry API.

The complete-family proof now records outer-key formal parameters separately from
caller allocation roles (**23d360db**). A getter's parameter can retain its role
when the caller also passes that same object as a sibling payload. DOM origins,
conservative input aliases and nonescaping storage must still be proved separately.

The `ctbrowser-dom-data-session-v1` contract proves explicit DOM actual origins
across the complete family, separately from source allocations. Inputs may alias.
**e967db10** also proves the imported inert declaration wrapper and rejects detached
method reads or whole-table aliases. The CLI gate covers seven imported functions,
two external inputs, alias/reentry observations and eleven source refusals; the IR
matrix has 31 rows per source form. Native storage remains refused until it is
private to the document owner. The source entry recreates Data on each invocation;
emission must preserve that reset. See [Data input provenance](native-dom-entry.md#data-input-provenance).

**acf98caa** uses the complete current array contents proof to narrow direct reads.
The original read-fed value/index overwrite sources now emit unchanged, with both
policies/layouts/compilers and signed-zero observations gated. Whole-function
failure or an unproved index keeps the optional fallback; no vector alias ownership
permission was added. Final focused 14/14 CTests pass in 30.78s, including the
existing Data session and twelve new array checks.

The preserved `array-overwrite-loop.js` now improves **0/2 → 2/2 native**, both
policies, through **c4b7cf8c**. Exact `1/0` guard recovery exposes the existing
header/body contents certificate; checked upstream SCF while patterns remove
the temporary poison/flag transport. No escape proof was weakened. Every pattern
application checks for duplicate forwarding, including aliases introduced during
cleanup and moved nested loops. **c65ad9cb** also guards before-region passthrough
uses and makes dominance data local to each function, fixing Bootstrap's do-while
and p5/Phaser crashes exposed by the full gate. Corrected **5/5 lit / 47/47 CTests
PASS in 24.08s** cover all native claims, both loop policies/layouts/compilers, VM
observations and the complete oracle.
The corrected full gate passed **573/573 CTests in 1226.65s / 169/169 lit in
870.79s**, with all **1418 frozen inputs** matching local/devbox. Fresh full Bootstrap
remains **19/574 native / 0 of 43 globals**, both policies, no skips/prunes.
[HANDOFF](HANDOFF.md) records the measurements and the initial failed gate.

## What is measured

The final gate after **e967db10 / acf98caa** passes **585/585 CTests in
1283.39s**, including **170/170 lit in 846.62s**. All **1,422 frozen inputs**
match local/devbox. The Data, DOM, escape and full-bundle rows below were
remeasured with the same results; the new imported DOM Data contract remains
analysis-only. [HANDOFF](HANDOFF.md) names the evidence and exact next boundary.

| Gate | What it establishes | What remains outside it |
| --- | --- | --- |
| Full bundle: **19/574 native**, both optimization policies; no skipped or pruned functions | Admission of individual functions from the unchanged vendor source | Native initialization, an interactive component, or a native application |
| Browser Data/UMD probe: **7/7** with manifest, prefix specialization and explicit 1m budget | Ownership and execution of the extracted Data methods and wrapper, including their preserved observations | A real Window, DOM nodes, Bootstrap constructors, event registration |
| Data session probe: **7/7**, 23 direct calls and 19 typed observations | Nonmovable method table owns its outer Map by value; source methods access it directly without stored captures and cannot escape independently; compile-clean and saved-child/payload lifetime gates | Atoms/document ownership and retained DOM keys; table, child-Map and ordinary object ownership remain |
| Full bundle: **0/43 globals resolved** | Current module-wide global-name census refuses | This is not a count of 43 missing browser APIs |
| Called local array-overwrite fixture: **0/6 → 6/6 native**, both policies | Live own-element write proof, stored-value type joins and direct vector assignments; unchanged source | Broader loop forms and a preserved vendor admission gain |
| Called local array-overwrite-loop fixture: **0/2 → 2/2 native**, both policies | Exact imported guard normalization lets the existing complete contents proof authorize the unchanged source | Duplicate guard forwarding, broader aliases and a preserved vendor admission gain |
| Generic escape oracle: **40/172 precision**, zero violations in the recorded snapshot | Independent analysis evidence; complete current contents now feed the local vector density check | General retained-graph ownership and refined escape verdicts remain outside native emission |

Fresh full-source IR names the leading first refusals: **272 `this` receivers**,
**137 own closures**, **70 unproved boxed parameters**, **29 closures passed to
callees without a specialized native call contract**, and **15 lexical-`this`
arrows**. These counts identify current blockers; they do not predict how many functions a single fix
will admit.

The seven-function program is a **source-derived probe**, not the complete original
bundle. [The extractor](../../tools/check/bootstrap-data-probe.py) cuts the source
at the Data/transitionend boundary, returns Data and adds a test environment with
ordinary `{}` keys and scalar/leaf-object payloads. Its byte pins protect that probe.
They do not demonstrate DOM-backed Bootstrap startup.

The full-bundle census runs import, global resolution and native lowering without
a browser manifest. The registered host-prefix tests use the same Data extractor;
there is no registered full-bundle browser-manifest/native-startup gate yet. Function
admission is not a percentage of project completion. Reported refusal categories
show only each function's first failure; fixing one exposes its downstream failures.

## The next browser path

1. **Extend the typed browser entry to retained DOM-backed Data keys.**
   Synchronous borrowed element entry and direct token/attribute calls now exist
   through the `ctbrowser-dom-v1` provider. It cannot retain handles or combine its
   contract with the existing source-owned Data provider. The next proof must
   establish that each owning document outlives the stored keys and every future
   Data invocation, using ordinary C++ ownership.
   The existing owning-callable provider permits extracted callables to outlive
   their owner; its session variant prevents method extraction, but the table and
   global carriers can still escape. Borrowing an element into either escaping
   carrier would dangle. Also, `document` borrows its atom table. Extend the owned
   DOM session with private Data storage declared after atoms and document, so Data
   is destroyed first. Keep this distinct from the existing owning Data-callable
   contract, and give DOM keys their own provenance instead of treating them as
   source-created ordinary objects.
   The new analysis-only Data contract supplies explicit DOM origins and matches
   them to `outerKeyParameters` across the full method family. Its separate
   `HostMethodArgument.element` and `outerKeyInputs` evidence rejects payloads,
   child keys, outer snapshots, returns and input aliases through globals.
   Consume and revalidate that live evidence before type inference or emission;
   the ordinary `objectKeys` category also permits owning payloads and cannot
   authorize DOM storage. Distinct external parameters may denote the same node;
   the entry replay conservatively retains that uncertainty. Compare
   foreign document ownership before validation dereferences the owner pointer.
   Reuse `ctbrowser::document` and `node_id` from the public DOM API. Bind their
   identity, ownership and permitted calls through the existing HostContract,
   inference/admission and emission machinery. A host declaration must prove which
   receiver an operation belongs to; the spelling `querySelector` alone is not proof.
   Start with the preserved Data methods using actual DOM node keys: repeated lookup
   must preserve identity, distinct nodes must differ, and detaching a node must not
   free it or confuse Data entries. Prove a single document domain or preserve
   document identity too: node IDs from different documents can have equal bits.
   The document must outlive borrowed handles. `element_ref` currently supplies
   equality only, while the non-iterating native Map uses `std::less<K>`; prove its
   key ordering as well as the insertion-order representation. Test a source with
   no Map snapshots too: Bootstrap Data's child-key snapshot selects insertion-order
   storage module-wide and would otherwise hide that comparator path.
   The remaining implementation spans `OwnedGlobalRoots`, DOM input seeding in
   `TypeInference`, `LoweringSupport.cpp`'s Map/table carriers, and EmitC
   `OwnedGlobals.cpp`, `MethodTables.cpp` and `DOM.cpp`. Replacing only the outer
   Map leaves shared ownership in the table and global root. The imported inert
   wrapper now has a bounded source proof. Revalidate it before omission, preserve
   source-driven root/Map/table replacement on each invocation, and keep all Data
   access private. A persistent initialization/action split needs separate proof.
   This is a **Data + DOM** milestone with its own denominator.
2. **Compile a real Button action, then its construction and lifetime.**
   [Button.toggle](../../ctbrowser/vendor/bootstrap/bootstrap.bundle.js#L424) toggles
   `active` and writes `aria-pressed`. Two calls on a real node must produce
   active/true then inactive/false. An action-only bridge is useful, but the original
   `BaseComponent` constructor adds inheritance, `super`, `_element` and `_config`,
   static constructor getters and publication of `this` into Data. Current
   [constructor lifting](../lib/CTNative/Lowering/ClosureLifting/Constructors.cpp)
   explicitly refuses prototype access; closed scalar literals do not prove this
   object graph. Disposal must preserve registration and alias lifetimes.
3. **Support retained browser callbacks and real event delivery.**
   Bootstrap registers delegated document listeners during factory initialization,
   and queues jQuery hooks through `DOMContentLoaded`. EventTarget must retain typed
   callbacks with exact receiver, identity/removal, capture/once and cancellation
   semantics. A callback can remove listeners, dispose a component or reenter
   dispatch. Existing source-owned Map closure checks do not authorize a browser
   event queue to retain that closure.
4. **Add transitions and geometry after the synchronous path.**
   No-fade Alert still requires cancelable close/closed events, a lexical-`this`
   callback, element removal and disposal. Fade/Collapse additionally need timers,
   transitionend, shared completion state, layout flushes, computed styles and
   dimensions. Popper-backed components need more geometry. Each should use the
   same platform implementation as the interpreter.
5. **Connect native compilation to the application driver.**
   [The current CLI](../tools/ctcompile/ctcompile.cpp) packages bytecode images with
   a fixed VM launcher. It does not generate native C++. Reuse asset discovery and
   packaging, then require an executable that initializes the real library, handles
   a click and tears down without a Script dependency. Keep an untouched-bundle gate
   separate from component/action probes throughout.

## Reuse the browser, with the actual dependency boundary

The DOM, selector/style, layout and painting subsystems already expose ordinary
C++ APIs. The implementation namespace for `document`/`node_id` is `ctbrowser`;
`dom/` is their header directory. Lift missing platform behavior out of the VM
binding, make the binding convert values and call it, and let generated code call
that same core. Do not duplicate token parsing, selectors, events or layout in
ctcompile.

The existing [AOT entry ABI](../../ctbrowser/include/ctbrowser/aot/aot_entry.h)
explicitly uses `script::context` and boxed values. The current Shell target also
links Script. Neither is already a native browser entry. Extend the public seams
with typed C++ operations and owners while retaining that dependency boundary.
The old generator helper was retired in `71952bf3`; Bootstrap's immediate need is
callbacks/timers, not restoring unused generator machinery.

The initial shared-token extraction exposes `parse_ordered_tokens`, `validate_token`,
`update_tokens` and `toggle_token` in
[dom/token_list.hpp](../../ctbrowser/include/ctbrowser/dom/token_list.hpp).
They work for an associated attribute, not just `class`. The classList binding and
class-name collections reuse the same implementation. Native clients check token
validation and DOM-write results; the adapter preserves its existing behavior for
failed writes. Forced no-ops preserve raw whitespace/duplicates, while an actual
same-byte update still records a mutation. This prepares the Button API; it does
not add compiler DOM types or increase native Bootstrap admission.

## Parallel work worth doing

- Compiler: atoms/document ownership and DOM-key provenance in the direct-method
  Data session; then the exact
  constructor/prototype/receiver proof required by Button.
- Browser, in a claimed isolated worktree: shared event/timer behavior and typed
  callback ownership beyond the current token/attribute/selector APIs. Preserve
  browser behavior.
- Validation: real DOM/component observations, lifecycle and no-Script link gates;
  keep original full-source coverage visible.

CommonJS replacement/old-exports alias support remains valid provider work, but it
is not required to take the browser branch of Bootstrap's wrapper. The original
`confinedArray` now passes its bounded read-only zero/+1 escape proof.
`TypeInference::isDenseVectorSite` now consumes complete current `computeArrayContents`
write evidence for direct local overwrites. Stored values join the element type;
admission requires definite Number indices and values, and emission records accesses
before retyping invalidates the proof. The called fixture preserves its source while
moving from refusal to execution under both policies. The preserved counted-loop
fixture now also emits through checked guard normalization and the complete contents
proof. Broader loop/alias transport, retained graph ownership and a preserved vendor
admission gain remain separate work.

The `window.scrollTo` receiver in ScrollSpy triggers the current global-object
escape reason. [Native.cmake](../test/cmake/Native.cmake) also records why removing
that global guard alone does not resolve vendor host bindings: those names are not
closures declared in the bundle. A name-resolution workaround is not the browser
bridge.

Validation and landed commit IDs are recorded in [HANDOFF.md](HANDOFF.md).
Scratch evidence, exact full-source IR and standalone link commands are under
`/tmp/ctcompile-bootstrap-survey/` for this session.
