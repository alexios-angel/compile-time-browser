[Back to bootstrap-native-next.md](../bootstrap-native-next.md)

## Inherited method targets and bounded offset overwrites, 2026-09-19 UTC

**fb7d6a3d** admits inherited ordinary methods on local explicit-super chains,
including constructor calls and new nonoverriding leaf methods. Five new positive
sources add 40 executions; all earlier source sections remain. Slot insertion
preserves dominance, unused constructor reads refuse, and inherited bodies cannot
shadow leaf methods. **9876bc23** adds bounded Number-offset overwrite proofs,
retaining reload disjointness, exact replay, saved children and cycles.

Focused host **1/1 (0.48s)**; class initialization/DOM lit **2/2 (307.87s total)**:
**178 observations / 452 main native executions**, with 356
unprepared/218 preparation refusals. DOM remains 632 observations/eight
executions/4,910 refusals. Arrays **1/1 (1.29s total)**; escape lit **3/3 (0.11s)**.
New oracle: **48 sites / seven sound / 7 of 13 confined precision**, zero
violations/partial/pending/unclaimed. Seven hashes match. Formatter retains
26 baseline diagnostics in nine unchanged files; changed checks pass.

**Next:** nearest-definition override targets (existing observation 21), then
lexical-super selection distinct from leaf-this dispatch (existing observation
118), preserving every shadowed body. Authentic W/B/Qi still needs captured
constructor helpers/static methods, complete DOM helpers, configuration,
selectors/events/Popper. Own-data provenance, broader ownership/control flow and
the application driver remain. Full suites/broad matrices skipped; no
browser/Script or compliance change. Resume provenance, exact commands, first
probe's test-helper error and skips: HANDOFF and `/tmp/ctcompile-inherited-1438/`.

**Next native:** select the nearest method definition for a proved override,
while retaining all shadowed bodies in the complete source census. The existing
`inherited-method-override` observes 21; it currently refuses. Then prove lexical
`super` from the declaring method's immutable home and immediate base while
preserving the final receiver. `inherited-dispatch` observes 118 and preserves
W's shadowed `_getConfig`: B's constructor selects Qi's override, Qi's super
selects B, and B's ordinary merge call selects W. Reuse existing constructor
prototype and method receiver proofs; differing leaf targets may still refuse.

Complete authentic W/B/Qi remains beyond these local sources. W's current source
fixture stops at helper captures; the DOM route also needs B's captured constructor
helpers and ordinary static methods, complete H/r/s bodies, configuration,
selectors, events and Popper. Do not invent H calls or parameter authority, remove
shadowed methods, or just relax capture checks. Default rest/apply, replacement
returns, fields, new.target and broader constructor effects remain. Part 25 still
needs own-data definition provenance and broader ownership/control flow; the
application driver remains incomplete.


## Explicit super construction and visited-index reloads, 2026-09-19 UTC

**883cac8e** admits explicit `super(...)` statements for field-only local
class chains. The private candidate proves the fresh Boolean guard, one ordered
base invocation, immediate new.target forwarding, receiver binding, absent field
initializers and final receiver completion. Base bodies must return undefined
and cannot observe new.target. Argument producers and field writes stay in source
order on the same final receiver. Only completely proved, unconstructed base
closures with no remaining callable/symbol use disappear. A separately constructed
base remains. Refusal publishes no partial source transformation.

Four sources now execute natively: the predecessor's unchanged explicit-super
specimen, ordered derived writes, a three-level chain, and separate base/derived
instances. Their observations are **7, 312, 13 and 27**; optimized/unoptimized,
explicit/deduced C++ and both compilers give **32 new native executions**. Generated
C++ uses a stack object and borrowed receiver pointer with existing finite native
scalar helpers; no Script/VM or prototype storage appears. All **164** previous
split-file JavaScript specimens retain their statements. Nine source specimens
and eight guard/identity/budget mutations were added.

**0be3bd32** proves current-index overwrites whose actual `start + n * stride`
visits exclude every invariant guard-array reload. Reload indices already have
own-element bounds; fixed-write overlap still refuses. Exact replay, saved-child
retention, historical cycles and work limits remain. Six CFG/six SCF controls and
ten source functions cover starts, stride gaps, zero trips, aliases and overlap.

**Next native:** `ClassInitialization.cpp::examine` still refuses inherited method
keys before super normalization. Prove inherited target tables and original bodies,
keeping leaf `this` dispatch separate from lexical `super` selection. The original
W/B fixture still stops at its captured-helper proof; default derived rest/apply,
replacement/primitive returns, public field initializers, observed new.target,
constructor declarations/global writes and broader control flow remain outside
this slice. Exercise inheritance with the DOM provider and authentic W/B/Qi calls;
do not insert H calls, invent parameter facts or erase shadowed bodies. Real
configuration, selectors, events, Popper and the application driver remain.
Part 25 own-data definition provenance and broader ownership/control flow remain.
This is a native inheritance gain, **not whole-Bootstrap admission**.

Focused host 1/1 and class initialization/DOM lit 2/2 (304.36s total) pass.
The class case now has 166 observations and 412 main native executions; DOM
measurements remain unchanged. Parallel escape arrays 1/1 and three oracles pass.
Exact commands, initial failures, formatter baseline and skipped coverage are in
HANDOFF and `/tmp/ctcompile-super-1350/`. No browser/Script changes.


## Ordered class ancestry and disjoint overwrite reloads, 2026-09-19 UTC

**95120020** proves ordered local heritage edges: an earlier completed base,
a later derived completion, unique local constructor identities, and the exact
fresh attached prototype. Bases can pass setup validation without direct
construction. Receiver-use checks include method names from the proved base setup;
these names do not select inherited targets or establish the final receiver.
Self/repeated heritage, reordered base completion, a different prototype and an
undeclared helper are pinned by five refusal controls.

This advances **structural validation only**. Every derived class still refuses
before rewriting: the original default-derived and three-level dispatch sources,
and a new explicit-super source, reach `derived class requires receiver-preserving
super normalization`. The complete original Bootstrap W/B fixture instead reaches
`class method capture is not its constructor or an inert sibling helper` in W;
its manifest now also declares the original Error getter. No original JavaScript
statements were removed: all 163 earlier split-file bodies remain. No inheritance
execution, generated browser API, or whole-Bootstrap admission was added.

**Next native:** normalize the original explicit-super constructor on a private
candidate. Prove the super guard cell and one ordered base call, preserve argument
and effect order, map the base body onto the same final receiver/new.target, and
handle bind-this, field initialization and derived completion. A first slice can
refuse replacement-object returns and fields; default derived constructors also
need their original rest/apply proof. Keep leaf `this` dispatch distinct from
lexical `super` target selection. Do not remove the current refusal merely because
ancestry or inherited method names were recorded. The original W/B source still
needs its captured helpers and complete bodies proved under the appropriate DOM
provider; do not invent H calls or parameter facts or erase shadowed W methods.
Configuration, selectors, events, Popper and the application driver remain.
Part 25 still needs own-data definition provenance and broader control flow and
ownership. Current-index stores disjoint from the actually visited induction
range are a separate escape continuation; this change handles fixed stores.

Focused host 1/1 and class-initialization/DOM lit 2/2 (303.86s total) pass.
Parallel 0943b287/c0924d11 complete disjoint-index overwrite proofs and the stronger
changed-key control; arrays 1/1 and selected escape lit 3/3 plus final 1/1 pass.
Exact commands, counts, initial failures and skipped coverage are in HANDOFF.
Full suites and broad matrices were skipped; no browser/Script change.


## Inheritance helper declarations and invariant overwrites, 2026-09-19 UTC

**1d0c953a** declares the mutable helpers emitted by original inheritance:
`__ctbrowser_class_heritage` (three arguments), `__ctbrowser_bind_this` (one),
`__ctbrowser_init_fields` (two), and `__ctbrowser_super_get` (three). The existing
manifest/binding proof accepts only their exact direct invocation shape with an
undefined receiver. Replacement, reflection, passing the callable itself, invalid
arity and conflicting declarations refuse. Identity supplies no argument or
effect proof; unknown helper effects still refuse, and DOM declarations remain
until their operations receive semantic normalization. Original W/B and dispatch
sources now declare the helpers and retain their precise inheritance refusal.
No source bodies or runtime operations were removed; this adds **no inheritance
or whole-Bootstrap admission**.

**Next native:** normalize an original ancestry chain on the private candidate.
The helper identities are now expressible; they are not semantic authority.
`ClassInitialization.cpp::examine` still needs completed base setup without direct
base construction, immutable prototype/home relationships, exact final receiver
and new.target, super guards/rebinding/field initialization, constructor returns
and argument/effect order. Start with explicit super; default derived constructors
also require rest/apply proof. Keep B -> Qi._getConfig dynamic dispatch distinct
from Qi's lexical super -> B._getConfig and B -> W._mergeConfigObj. Preserve every
original body, including shadowed W methods, without invented H calls or parameter
facts. Real configuration/selectors/events/Popper, Part 25 own-data definition
provenance and broader control flow/ownership, and the application driver remain.

Focused host **1/1 (0.48s)** and class initialization **1/1 (196.32s)** pass.
Parallel **2a00a6de** proves invariant own-index overwrite loops; arrays and three
escape cases pass with zero oracle violations. Exact commands, counts, initial
test failures and skipped coverage are in HANDOFF. No browser/Script change.


## Explicit inheritance refusal and preserved source, 2026-09-19 UTC

**6d42e0e8** diagnoses the existing constructor-use refusal as `class inheritance
requires proved heritage, receiver and super initialization`. Complete original
Bootstrap W/B classes and r/a helpers execute B's missing-element early exit in
a new regression; a separate three-level source preserves dynamic versus lexical
dispatch. Native still refuses both. The focused class-initialization case passes
**1/1 (196.14s)** with 156 source observations and 380 main native executions.
No inheritance or whole-Bootstrap admission is claimed. Commands, additional
control counts, intermediate parser failure and skipped checks are in HANDOFF.

The importer emits `__ctbrowser_class_heritage(derived, base, prototype)` before
ordinary class attachments. Continue on the existing private candidate: prove
helper identity, completed base setup, immutable prototype/home relationships,
super guards/rebinding/field initialization, constructor returns, actual
new.target and argument/effect order. Default derived constructors also need
rest/apply proof. Distinguish declaring class from final receiver: B constructor
-> Qi._getConfig; lexical super -> B._getConfig; same-this merge -> W._mergeConfigObj.
Inherited static getters use Qi as receiver. Shadowed W bodies remain unproved
without their own facts; never erase them or synthesize H calls. Real
configuration, selectors, events, Popper and the application driver remain.

Parallel **c27bd349** completes exact receiver aliases for guarded own-array
overwrites; arrays and three selected escape oracles pass. No browser/Script
source changed. Full suites and broad matrices were skipped.

## Constructor-origin calls are proved, 2026-09-19 UTC

**a0644324** closes a real prerequisite of B's constructor: native method
reachability now includes original `this.method(...)` calls made during
construction, then follows same-receiver transitive calls. Actual arguments,
instance state, initialization/write order and complete typed body proof remain.
Both providers reproduced the old constructor-only refusal before this change.
Focused transaction/host/class DOM checks pass; the class case measured 632
Node/interpreter observations, eight native executions and 4,910 refusals.

Continue with **original W/B/Qi inheritance**. Class initialization still requires
exact base/derived constructor, prototype, lexical-home and receiver provenance;
W and B have no direct construction on this path. Preserve original configuration,
selector, event and Popper dependencies and all H bodies. Constructor reachability
is not full inheritance or whole-Dropdown admission. The one-slot full-H specimen
remains refused. Independent **1d0d2a65** closes disjoint-array stride reloads;
focused arrays and three oracles pass. Commands, timings, formatting baseline and
skipped coverage are in HANDOFF. No browser/Script behavior changed.

## Follow Bootstrap's actual H callers, 2026-09-19 UTC

The unchanged `class_filter_full_h` calls only `H.getDataAttribute`. Under the
complete-original-body rule its unused slots have no DOM/String parameter
authority. More inert operations or fresh-allocation recognition cannot supply
that authority. Both providers still refuse `fn$7 / ctjs.create_object` in a
focused source probe with the existing one-million-step budget.

The actual combined path is Dropdown `Qi` in
`ctbrowser/vendor/bootstrap/bootstrap.bundle.js`:

- Constructor at line 1989 calls `B` (line 300), whose `_getConfig` (line 309)
  reaches `W._mergeConfigObj` (line 281). Its actual calls supply `e` and
  `"config"` to `H.getDataAttribute`, and `e` to `H.getDataAttributes`.
- `_getPopperConfig` (line 2063) calls `H.setDataAttribute(this._menu, "popper",
  "static")` at line 2082 when navbar/static configuration selects that path.
- `_completeHide` (line 2028) calls `H.removeDataAttribute(this._menu, "popper")`
  at line 2032 after the hide-event guard.

Continue with the original `Qi`/`B`/`W` source and actual receiver/configuration
flow. Inheritance, selectors, event results and Popper remain proof boundaries;
none is supplied by this call census. `W` alone calls only getters. The scrollbar
class `un` (line 2257) calls set/get/remove, but never `getDataAttributes`.
Keep the existing one-slot specimen as a refusal; do not insert calls, remove
siblings or copy platform implementations to manufacture authority.

Independent **75e341e0** admits bounded current-own-element overwrite loops in
escape analysis. Focused arrays **1/1**, three selected oracles **3/3**, and the
final cycle-extended oracle **1/1** pass; **37 sites, six sound, zero violations**.
Exact commands, timings, limitations and skipped coverage are in `HANDOFF.md`.
No whole-Bootstrap native gain or browser/runtime change is claimed.

## Confined unused local cells, 2026-09-19 UTC

**6c877519** proves private local-cell identities and every operation on their
arbitrary contents, plus recursively proved uncalled uncaptured nested bodies.
Source fixtures retain real cells, all earlier sources and all effect refusals.
Focused transaction **1/1 (4.62s total)**, host **1/1 (0.48s total)** and class DOM
**1/1 (283.35s; 620 observations, eight executions, 4,844 refusals)** pass.
Four hashes match; changed formatting and generated ownership/Script checks pass.
The repository formatter still reports 26 baseline diagnostics in nine unchanged
files. Full suites and broad matrices were skipped; exact commands and initial
fixture/build failures are in HANDOFF and `/tmp/ctcompile-h-1047/`.

Original full H now refuses at **fn$7 / ctjs.create_object**. Its complete
fresh-result/iterator/callback proof still lacks genuine parameter authority;
more inert operations cannot establish unknown DOM receivers or String keys.
Do not invent calls or remove unproved siblings. Standalone unused declarations
retain their separate class-initialization census. The original application call
graph, real own-data definitions, W, nested iterators and the application driver
remain. No whole-Bootstrap admission gain or browser/Script change is claimed.


## Unused conditional and early-return bodies, 2026-09-19 UTC

**104ab81c** proves every original arm of unused total conditional helpers;
**16dc5860** reuses the existing completion normalizer for their early returns.
Neither change supplies parameter facts. Source bodies, effect refusals, work
budgets and Script exclusions remain intact. This resumes **a76e1a53** and the
10:18 unused-H thread explicitly abandoned at 10:19:04.

Final focused transaction **1/1 (4.55s total)**, host **1/1 (0.48s total)** and
class DOM **1/1 (278.83s; 608 observations, eight native executions, 4,742 refusals)**.
Both code commits passed the focused selection; no build/test failed. All three
hashes matched the devbox at each commit. All 408 earlier driver bodies and 18
raw C++ JS bodies are preserved; 17 driver bodies and 17 transaction controls per
provider were added. Formatting retains 26 baseline diagnostics in nine unchanged
files; changed checks pass. Commands/evidence: repository HANDOFF and
`/tmp/ctcompile-h-1021/`. Full suites and broad matrices were skipped.

**Next:** unchanged `class_filter_full_h` now refuses at `native DOM class:
DOM class method body: native DOM source: unused DOM helper body contains an
unproved operation: fn$7 / ctjs.create_cell`. This is original
`H.getDataAttributes`' parameter cell; its local state, fresh result, iterator and
callback body still need complete independent proof. Do not merely whitelist
cells or allocate guessed parameter values. Its unknown `t` has no DOM authority;
uncalled setters likewise lack proved DOM receivers and String keys. Dynamic F
is already implemented for proved Strings. Do not insert calls, remove unproved
siblings or keep adding unrelated inert operators as a substitute for that proof.
Part 25 ordinary-object reloads still require actual literal own-data definition
semantics/provenance, preserving assignment setters. The parallel escape review
reconfirmed that prerequisite; no escape code or measurement changed.
W, nested iterators, complex completion, broader ownership and the application
driver remain. No whole-Bootstrap admission gain or new compliance count is claimed.


## Dynamic Bootstrap F inputs, 2026-09-19 UTC

**84b3357a** finishes the dynamic String F continuation from the abandoned 09:33
session. Original ``t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`)`` now runs on
proved dynamic Strings from dataset iteration. Generated C++ keeps the original
callback, uses public Core lowercase and ordinary String ownership, and preserves
nonmatching bytes. Every receiver, callback use, enclosure and initial intrinsic
remains proved; unknown/nullable/object inputs and changed callbacks still refuse.

Focused transaction **1/1 (4.51s)**, host contract **1/1 (0.48s total)**, class DOM
**1/1 (251.87s; 584 observations, eight executions, 4,446 refusals)**, RegExp
**1/1 (12.93s; 24 observations, four executions, 169 checks)**. Parallel
**995585be** completes saved digit String array-key escape proofs. All 15 tested
hashes match devbox; emitted ownership and Script/dispatch/link exclusions pass.
Formatting retains 26 baseline diagnostics/nine unchanged files; changed checks
pass. Exact commands, failures and skipped coverage: HANDOFF and
`/tmp/ctcompile-h-0944/`. Full suites and broad matrices were skipped.

**Next:** unchanged original `class_filter_full_h` now refuses with `native DOM
class: DOM class method body: native DOM source: unused DOM helper body contains
an unproved operation`. Prove the unused parameterized H bodies independently;
do not assume parameter facts, synthesize calls or remove unproved siblings.
DOM writes inside iteration still need backedge alias proof; the saved F result
survives a subsequent write after the loop. W, nested iterators, complex completion
and the application driver remain. No whole-Bootstrap gain is claimed.

## Known matching Bootstrap F inputs, 2026-09-19 UTC

**5b01f648** finishes the interrupted 08:55 F fixtures abandoned at 08:58:27.
Known matching inputs to original Bootstrap F now retain the full all-call input
census and prove the entire `'-' + t.toLowerCase()` callback. Only `/[A-Z]/g`
matching is added; public Core `ascii_lower` computes constant outputs, with
ordinary String comparisons selecting the proved result. Non-ASCII bytes stay
unchanged. Unknown inputs, changed callbacks, captures/effects and intrinsic
mutation still refuse. All 388 prior class bodies, 17 raw C++ JS bodies and 49
standalone RegExp refusal bodies stay intact; 11 matching standalone bodies now
have return-value and attribute-state differential checks.

Focused transaction **1/1 (4.48s total)**, host contract **1/1 (0.47s)**,
class DOM **1/1 (245.16s; 568 observations, eight executions, 4,290 refusals)**,
new RegExp-only lit **1/1 (12.95s; 24 observations, four executions, 169 checks)**.
All ten native/escape hashes match; emitted C++ and Script exclusions pass.
Formatting retains 26 baseline diagnostics/nine unchanged files; changed checks
pass. Parallel **da3602e3** finishes bounded ASCII String-index escape snapshots.
Exact commands, initial expectation/harness failures and skipped coverage are in
HANDOFF and `/tmp/ctcompile-h-0903/`; full suites/broad matrices were skipped.

**Next:** original `class_filter_full_h` still refuses at the DOM class-method
proof's `DOM direct helper contains an unproved closure or capture` (fresh probe).
Unused parameterized H slots still pass unknown keys to F; dynamic String
replacement and independent original H-body proof remain before publication.
Do not insert calls or delete unproved siblings. Object definitions, W operations,
nested iterators, complex completion and the application driver remain. No
browser/Script behavior changed and no whole-Bootstrap gain is claimed.

## Inert unused bodies and the remaining H boundary, 2026-09-19 UTC

**3c16788b** proves unused uncaptured straight-line leaves independently of
calls: literals, identity, typeof/Not/Void and strict equality. Retired slots
must have no numeric closure or symbol uses. Unknown calls, conversions,
properties and captures still refuse. All 373 previous source bodies stay
intact; 15 bodies and seven transaction controls per provider were added.

Focused transaction **1/1 (4.49s total)**, host contract **1/1 (0.48s)**,
class DOM **1/1 (246.04s; 532 observations, eight native executions, 4,116
refusals)**. Ten tested native/escape hashes match. Script/dispatch exclusions
pass; formatting retains 26 baseline diagnostics in nine unchanged files.
Parallel **6fdac943** finishes the interrupted ASCII String-length escape draft.
Exact commands, failures and source-oracle measurements are in HANDOFF and
`/tmp/ctcompile-h-0823/`. Full suites and broad matrices were skipped.

**Next:** original `class_filter_full_h` still refuses. Uncalled H setters
pass unknown keys into F, blocking its complete no-match replacement proof.
Implement general matching replacement and independent parameterized H-body
proof before publication; do not add calls or drop siblings from the specimen.
Object reloads still require real own-data literal definitions. W operations,
nested iterators, complex completion and the application driver remain.
No browser/Script semantics changed; no whole-Bootstrap gain is claimed.

## Full H instance methods and object-reload prerequisite, 2026-09-19 UTC

**6bca7b94** finishes the interrupted receiver-proof thread at **61f627fb**:
three dirty files from the **07:05 session**, explicitly abandoned by the
**07:07:11 AGENT-SYNC exit**. Complete original H/M/F now runs inside instance
methods when every H slot has actual calls, including constructor-stored DOM
receivers and fixed fresh-result aliases. Method stability ignores only a direct
fresh allocation distinct from the prototype/instance; all unknown aliases,
formals and real receiver writes still count. Constructor proof is unchanged.

All **361 prior source bodies** and **16 raw C++ JavaScript bodies** are preserved.
Twelve added bodies and six new transaction controls per provider cover the
boundary, including a local that changes from fresh to the real instance. The
old transaction control 148 now admits without changing its source. No browser
or Script semantics changed.

Focused passes: transaction **1/1 (4.35s; total 4.36s)**, host contract
**1/1 (0.51s; total 0.52s)**, and selected class DOM / class entry / constructor
refusals lit **3/3 (186.36s total)**. Class DOM: **492 observations, eight
GCC/Clang executions, 3,746 refusals**. All four changed source/test hashes match
the devbox; emitted C++ uses public Core/DOM helpers and ordinary owners, and
Script/dispatch exclusions pass. Required formatting retains **26 baseline
diagnostics in nine unchanged files**; changed checks pass. Exact commands,
first failed expectation and evidence: repository HANDOFF and
`/tmp/ctcompile-prototype-final/`.

**Next:** the unchanged `class_filter_full_h` calls only `H.getDataAttribute`;
its uncalled original slots still refuse at `DOM direct helper contains an
unproved closure or capture`. Prove their bodies independently before publication;
do not erase unproved siblings or insert calls into the specimen. Part 25's
object reloads need real literal own-data definition semantics/provenance first:
ordinary-object assignment currently cannot seed a safe contents snapshot.
This prerequisite is rechecked at the top of part 25 and journaled for Claude.
W operations, nested iterators, complex entry completion, broader part 25 and
the application driver remain. Full suites, broad matrices, WPT/test262 and
whole Bootstrap were skipped; no whole-bundle or escape-precision gain is claimed.

## Full H entry calls and invariant array lengths, 2026-09-19 UTC

Resumed **59d28a62** and the five dirty files from the **06:19 H-slot/own-length
threads**, abandoned at **06:21:33** in AGENT-SYNC. September 7 WIP is already an
ancestor. Three agents supplied escape review, H fixtures and native audit;
root completed the two service-limited threads and the focused gates.

**186afb38** admits complete original H/M/F with all four slots called from the
entry, beside a constructor-only class. Sibling String actuals bind before the
shared F callee expands; complete-use, identity and effect proofs remain. Two
new positives include distinct config/toggle inputs; 13 new refusals and six
transaction controls per provider cover the boundary. All 346 prior bodies stay
unchanged. **45365d98** finishes invariant own-array-length reloads, retaining
base/key identity, the read-only census, budgets and final-index bounds.

Focused passes: transaction **1/1 (4.38s)**, host contract **1/1 (0.46s)**,
class DOM **1/1 (174.59s; 480 observations, eight native executions, 3,584
refusals)**, arrays **1/1 (1.23s)**, length/prior-reload oracles **2/2 (0.10s)**.
New escape oracle: **38 sites / 14 sound / 14 of 27 precision**, zero violations.
All eight tested hashes match. Inspected C++ calls existing public Core/DOM
helpers with ordinary owners; Script/dispatch exclusions pass. Required format
check retains 26 baseline diagnostics in nine unchanged files; changed checks
pass. Exact commands and evidence: repository HANDOFF and `/tmp/ctcompile-h-resume/`.

**Next:** the class-method full-H case is blocked by the whole-module dynamic-write
census in `ClosureLifting/Methods.cpp`, which prevents prototype-method stability.
Its later unique-callable-slot diagnostic names the residual class prototype,
not H. Establish receiver provenance, then recheck constructor method-value reads
and independent unused H bodies before publication. W operations, nested iterators,
complex entry completion, broader part 25 and the application driver remain.
Full suites, broad matrices, WPT/test262 and whole Bootstrap were skipped; no
whole-bundle gain is claimed. Browser/Script semantics are unchanged.

## Original H key normalization and invariant array reloads, 2026-09-19 UTC

Resumed clean **dadadb5c** and its recorded original-H boundary in HANDOFF and
AGENT-SYNC's **05:48 closure**. September 7 WIP is already an ancestor; no dirty
predecessor work remained. Three agents worked on Core casing, escape reloads
and H proof review. Two reached service limits; root completed their work.

**794af039 / eaae9318**, atomically merged as **7361a3e5**, add a pinned Unicode
17 generator and public Core lowercase for one UTF-16 unit. **b484511f** admits
the unchanged original `H.getDataAttributes`, including M. A proved `charAt(0)`
may lowercase through Core; the exact first-unit-plus-tail expression preserves
separate dataset-key identity and a unique `__proto__` preimage. Ordinary output
collisions retain ordered overwrites. Whole-string casing and full H remain open.

**17dc99aa** proves repeated dense-array own-element reads in invariant latches.
Original base/key identity, the read-only loop census, primitive conversions,
64-layer/work budgets and final-index limits remain checked. Prior source bodies
are preserved; a genuinely varying key and loop mutation still refuse.

Focused passes: Core **1/1 (0.57s)**; native transaction **1/1 (4.42s)**; host
contract **1/1 (0.47s)**; class/assignment lit **2/2 (223.05s total)**; arrays
**1/1 (1.24s)**; four escape oracles **4/4 (0.12s)**. Class: **472 observations,
8 native executions, 3,448 refusals**. Assignment: **11 sources, 239 observations,
8 GCC/Clang binaries, lifetime sanitizer, 236 refusals**. Existing VM differences
are explicit: 40 class byte-index observations and five assignment casing results;
native agrees with Node. New escape oracle: **33 sites / 15 sound / 15 of 24
precision**, zero violations. All 19 changed source/test hashes match the devbox.
Required formatting retains 26 baseline diagnostics in nine unchanged files;
changed checks pass. Exact commands/failures: HANDOFF and `/tmp/ctcompile-h-lowercase/`.

**Next:** complete original H still refuses at `DOM helper object requires unique
own callable slots` in the shared closed-callable census. Prove its complete slot
set and unused bodies, then publication; do not erase unproved siblings. W's
Object.entries/destructuring/original s/RegExp/TypeError/spread, complex entry
completion, loop-nested iterators, broader part 25 and the application driver
remain. Full CTest/compiler lit, broad corpus/native matrices, WPT/test262 and
whole Bootstrap were skipped. No full-suite or whole-bundle gain is claimed.
Script/binding semantics are unchanged. Native Bootstrap and the plan remain unfinished.

## Repeated helpers across entry exits and budgeted invariant depth, 2026-09-19 UTC

**5c912e13** finishes the interrupted conditional-callee thread at **41527341**
(two dirty fixtures; AGENT-SYNC abandonment **05:26:57**). Original helpers called
before and after an entry early return now share the original-body proof; both
conditional arms must join the same frame state. All 310 previous bodies and ten
inherited bodies are preserved; ten prior refusals now admit, including nine
UTF-16 early returns. Thirteen new bodies and transactional frame/refusal checks
pass. Forty VM byte-index divergences remain explicit; native agrees with Node.

**c6fa3d11** extends invariant expression depth using the existing work budget
and a 64-layer stack limit. Primitive snapshots, backedges, conversions, bounds
and reload refusals remain. Three source oracles retain all original bodies.
Three parallel agents hit service limits; root completed their remaining work.

Focused passes: transaction **1/1 (4.39s)**, contract **1/1 (0.45s)**, class DOM
**1/1 (167.60s; 464 observations, 8 native executions, 3,398 refusals)**, arrays
**1/1 (1.25s)** and escape oracles **3/3 (0.13s)**, zero soundness violations.
All 11 tested hashes match. Changed checks pass; required formatting retains
26 baseline diagnostics in nine unchanged files. Exact commands, earlier failed
attempts and oracle measurements are in `ctcompile/docs/HANDOFF.md`; evidence:
`/tmp/ctcompile-callee-finish/`.

**Next:** original H's Unicode lowercase and normalized-key collision/prototype
proof. Keep dataset-key identity separate from output keys, including the
`__proto__` unique-preimage check; Script's ASCII casing remains unchanged.
`class_dynamic_conditional_nested_calls` still refuses at class completion
arithmetic. Loop-nested iterators, unused H slots/publication, W operations,
reloaded induction, broader part 25 and the application driver remain. Full
suites, broad corpus/native matrices, WPT/test262 and whole Bootstrap were
skipped; no full-suite or whole-bundle gain is claimed. No browser/Script changes
or push. Native Bootstrap and the overall plan remain unfinished.


## Conditional dataset iterators and invariant bitwise latches, 2026-09-19 UTC

Resumed **ce49f0dd** and the six dirty paths from the **04:59 conditional/bitwise
threads**, explicitly abandoned at the **05:01:02 AGENT-SYNC loop exit**.
**b290e7e0** admits original early-return class/helper iterators and new holder,
inverse-guard and nested-conditional specimens. The private snapshot prefix
retains condition producers and dominating operations; final entry proof still
checks both arms, effects, snapshot epochs and joined scalar state. All **301
prior class bodies are unchanged**; nine new bodies and six transaction controls
cover the extension. Five preserved bodies are now positive cases.

Parallel **470e4e82** reuses bounded bitwise/shift transfer for invariant loop
latches under the existing two-operation limit. Original snapshots, masked
counts, signed results, backedge identity, budget and final-index bounds remain
checked; CFG/SCF coverage and a fourteen-function source oracle pass.

Focused passes: transaction **1/1 (4.26s)**, contract **1/1 (0.46s)**, class DOM
**1/1 (144.97s; 408 observations, 8 native executions, 3,108 refusals)**, arrays
**1/1 (1.24s)** and four escape oracles **4/4 (0.15s)**. New oracle: **44 sites /
14 sound / 14 of 28 precision**, zero violations. Seven tested hashes match;
Script/dispatch exclusions pass. Required formatting retains **26 pre-existing
diagnostics in nine unchanged files**; changed checks pass. Exact commands and
limits are in `ctcompile/docs/HANDOFF.md`; evidence is under
`/tmp/ctcompile-conditional-final/`.

**Next:** full original H still needs Unicode lowercase and normalized-key
collision/prototype proof. A proved `charAt(0)` result permits a bounded public
Core casing primitive with pinned full-lower mappings, expanding U+0130 and
unchanged surrogates; regex case folding and the VM's ASCII casing cannot supply
it. Preserve dataset-key identity separately from output-key collisions and the
`__proto__` unique-preimage check. The helper called before and after an early
return (`class_dynamic_conditional_sequential`) still refuses at lifted closure
bookkeeping. Loop-nested iterators, unused H slots, global publication, W's
Object.entries/destructuring/original s/RegExp/TypeError/spread, deeper/reloaded
induction, broader part 25, the application driver and native Bootstrap remain.
Full suites, broad corpus/native matrices, WPT/test262 and whole Bootstrap were
skipped; no full-suite or whole-bundle gain is claimed. Browser/Script semantics
are unchanged. Temporary SSH access was removed; no push.

## Sequential dataset iterators and invariant Add/Sub, 2026-09-19 UTC

Resumed clean **541ed09b** from the **04:31:10 AGENT-SYNC closure** and its
recorded repeated-iterator boundary; no predecessor edits were pending and
September 7 WIP is already an ancestor. **56a37fd8** admits the original repeated
holder, class-method and direct-helper sources. Each sequential top-level
iterator reuses the complete proof of its prefix, including earlier loops and
intervening operations. All 293 prior class bodies remain unchanged; eight new
bodies cover saved results, writes, three calls and refusals.

Parallel **80849b2d** reuses bounded Add and signed Sub for invariant loop latches
within the existing two-layer limit. Twenty-nine older expression rows retain
their source bodies with newly proved outcomes; 70 added CFG/SCF rows and a
thirteen-function source oracle cover primitive coercion, snapshots and refusals.

Focused passes: transaction **1/1 (4.49s)**, contract **1/1 (0.47s)**, class DOM
**1/1 (133.81s; 388 source observations, 8 native executions, 2,942 refusals)**,
arrays **1/1 (1.19s)**, six escape oracles **6/6 (0.19s)** and 16 existing assignment
refusals. New escape oracle: **41 sites / 12 sound / 12 of 26 precision**, zero
violations. Eight tested hashes match; Script/dispatch gates pass. Formatting
retains 26 pre-existing diagnostics in nine unchanged files; changed checks pass.
Exact commands, fixture corrections and skipped coverage are in
`ctcompile/docs/HANDOFF.md`; evidence: `/tmp/ctcompile-sequential-iterators/`.

**Next:** original H still needs Unicode-correct lowercase and normalized-key
collision/prototype proof; keep Script's ASCII casing unchanged. Early-return
and nested iterators need a conditional-prefix/scalar-state proof. Unused H
slots, global publication and W's Object.entries/destructuring/original s/RegExp/
TypeError/spread remain before inheritance. Deeper/reloaded induction, broader
part 25, the application driver and native Bootstrap remain unfinished. Full
CTest/compiler lit, broad corpus/native matrices and WPT/test262 were skipped;
no full-suite or whole-bundle admission gain is claimed. No browser changes or
push; temporary SSH access was removed after verification.

## Direct dataset helpers and nested invariant latches, 2026-09-19 UTC

Resumed clean **1ffeea81** from the **04:11:27 AGENT-SYNC closure** and the
original helper refusal; September 7 WIP is already an ancestor. No predecessor
edits were pending. **e5426f3c** admits the unchanged `class_dynamic_helper` body:
original callback enclosures are proved before the helper receiver, then inert
direct callee operands use the existing captured-helper cleanup. Every callback,
argument and unused body still requires complete proof. All 279 prior class
source bodies are preserved; 14 new bodies cover order, snapshots and refusals.

Parallel **0414c4e0** reuses bounded scalar transfers for two invariant arithmetic
layers, with charged recursion and unchanged original snapshots. Twenty-four
older CFG/SCF expressions are now proved without changing their bodies; 58 new
rows and an eleven-function source oracle cover the extension.

Focused passes: transaction **1/1 (4.35s)**, contract **1/1 (0.46s)**, class DOM
**1/1 (123.43s; 364 source observations, 8 native executions, 2,754 refusals)**,
public class **1/1 (199.25s)**, arrays **1/1 (1.19s)**, six escape oracles **6/6
(0.15s)**. New oracle: **35 sites / 8 sound / 8 of 23 precision**, zero violations.
Ten tested hashes match; output Script/dispatch exclusions pass. Formatting
retains 26 existing diagnostics in nine unchanged files; changed checks pass.
First arrays failed on 13 older nested BitNot expectations, subsequently corrected
without changing source expressions. Exact targets, failures, commands and
skipped coverage are in `ctcompile/docs/HANDOFF.md`; evidence is under
`/tmp/ctcompile-class-helper/`. No broad/full-suite or bundle-admission claim.

**Next:** full original H still needs Unicode-correct lowercase and normalized
key collision/prototype proof. Dataset suffixes can be non-ASCII; keep the VM's
ASCII casing unchanged. Early-return/repeated iterators, unused H slots, global
publication and W's Object.entries/destructuring/original s/RegExp/TypeError/
spread remain before inheritance. Deeper/recomputed induction, broader part 25,
the application driver and native Bootstrap remain unfinished. No browser edits
or push; temporary SSH access was removed after verification.

## Class dataset loops and invariant powers, 2026-09-19 UTC

Resumed clean **24b6383e** from the **03:46:37 AGENT-SYNC closure**. The latest
work was committed; September 7 WIP is already an ancestor. Three parallel agents
supplied the escape implementation and class/casing findings before service
limits; root completed diagnosis, integration and focused validation.

**023bbf5e** admits the original `class_dynamic_method` and
`class_dynamic_capture` bodies. Fixed method cells may be read through later
loops/completion switches while writes and captures stay ordered. The shared
constructor census distinguishes closed instances from unrelated SSA values;
all formal parameters and prototype observations retain conservative checks.
Already-called methods use the complete original-call proof, avoiding a synthetic
second iterator. Unused methods still require their own proof. Receiver aliases,
actual parameters and loop branches are checked. All **290 previous source
specimens are unchanged**; **12 new bodies** cover successes and refusals.

**0e8a9705** shares the existing bounded scalar power transfer with one original
invariant Pow latch. Both operands must retain their original primitive snapshots;
only the existing exact zero/unit identities are admitted. Changing/nested inputs,
noncanonical conversions, zero strides, original property keys and final bounds
retain their checks. Existing expression bodies remain unchanged.

Focused passes: arrays **1/1 (1.21s; total 1.22s)** and four selected escape
oracles **4/4 (0.14s)**. New oracle: **34 observed sites / 8 sound / 8 of 22
precision**, zero violations, partial, pending or unclaimed sites. Native
transaction **1/1 (4.43s; total 4.44s)**, host contract **1/1 (0.48s; total
0.49s)**, class DOM **1/1 (117.31s)**: **348 source observations, 8 combined native
executions, 2,594 refusals**. The 20 existing VM byte-index divergences remain
explicit; native agrees with Node. Public class, constructor refusals and borrowed
argument refusals **3/3 (200.59s)**. Source/binary Script and dispatch exclusions
pass. Final eight source/test hashes match the devbox. Required formatting retains
**26 existing diagnostics in nine unchanged files**; changed checks pass.
Evidence: `/tmp/ctcompile-class-loop/`.

**Next:** full original H still stops at Unicode `toLowerCase` and normalized-key
collision/prototype proof. No correct public casing implementation exists; keep
Script's intentionally ASCII behavior unchanged. `class_dynamic_helper` still
stops at the class census's unsupported `ctjs.call_direct`. Loops behind early
returns and actual repeated iterators still require one top-level-iterator proof.
Unused H slots, global wrapper publication and W's Object.entries/destructuring/
original s/RegExp/TypeError/spread remain before inheritance. Nested/recomputed
induction and broader part-25 work remain. Full H, W/W+r/W+r+H, native Bootstrap,
the application driver and the whole plan are unfinished. No whole-bundle
admission gain or full-suite pass is claimed.

Exact commands, probe outcomes and skipped coverage are recorded in ctcompile/docs/HANDOFF.md.

## Confined class callbacks and invariant division, 2026-09-19 UTC

Resumed clean **768a71c5** from the **03:18:30 AGENT-SYNC closure**. Both agents'
commits and unmerged branches were reviewed; the September 7 WIP is already an
ancestor and the interrupted UTF-16 extraction was already landed. Three agents
supplied casing/alias analysis, fixture recommendations and a partial escape draft
before service limits; root completed, reviewed and gated the changes.

**6a1f3147** lets DOM class methods retain Bootstrap's original dataset
predicate when its callback saves lexical `this` but never reads it. The complete
original callback/callee/receiver census precedes clearing that inert operand.
Fixed method-local cells reuse the existing ordered identity proof; cell and
callback identities follow private method normalization. Constructor fields,
receiver aliases, actual parameters, early returns and repeated calls are covered.
Observed callback receivers, changing cells, escapes, invalid later inputs and unused
method effects still refuse. All prior fixture dictionary entries are unchanged;
three new strict-number-equality bodies and two unused borrowed-field bodies
are preserved as refusals.

**11f707a4** shares the existing bounded signed Div/Mod transfer with one invariant
quotient/remainder latch. Both operands must remain original literal or saved
primitive values; division additionally requires an integral quotient. Zero
divisors/strides, noncanonical/unknown/changing/nested operands, original property
keys and final-index bounds retain their checks. Ten older expressions retain their
source bodies with updated outcomes; 37 CFG/SCF rows and a nine-function source
oracle were added. Arrays **1/1 (1.15s; total 1.16s)** and three selected escape
oracles **3/3 (0.13s)** pass. New oracle: **28 observed sites / 6 sound / 6 of 18
precision**, zero violations, partial, pending or unclaimed sites.

Final native transaction **1/1 (4.40s)**, host contract **1/1 (0.46s; total
0.47s)** and class DOM **1/1 (107.33s)** pass. Class DOM checks **328 source
observations, 8 combined native executions and 2,416 refusals**. The 20 explicit
VM byte-indexing divergences remain pinned; native agrees with Node. Public class:
**1/1 (198.66s)**. Generated-source and binary Script/dispatch exclusions pass.
All **seven tested hashes** match the devbox. Required formatting retains 26
existing diagnostics in nine unchanged files; changed checks pass. Exact commands, retries and skipped coverage are in [HANDOFF](../HANDOFF.md).
Evidence: `/tmp/ctcompile-class-alias/`.

**Next:** original H still needs Unicode-correct lowercase and normalized output-key
proof. Script's String casing is deliberately ASCII-only and no public Unicode
casing implementation exists; extracting the ASCII binding cannot meet that
contract. Keep the VM behavior unchanged. Lowercase collisions must preserve
insertion/overwrite order and the `__proto__` setter constraint; transformed keys
must never acquire dataset-membership authority. `class_dynamic_method` stops at
nonlocal/unordered method-cell uses,
`class_dynamic_capture` at an observable lifted closure, and
`class_dynamic_helper` at its unsupported `ctjs.call_direct`. Original H retains
the typed property/member refusal. Repeated iterators, unused H slots and global
publication remain. W still needs Object.entries/destructuring/original s/RegExp/
TypeError/spread before inheritance. One invariant Pow latch and broader part-25
work remain. Full H, W/W+r/W+r+H, native Bootstrap, the application driver and the
whole plan remain unfinished. No full-suite or whole-bundle admission gain is claimed.

## Shared UTF-16 indexing and invariant products, 2026-09-19 UTC

Resumed clean **fb55eb20** and its original H Unicode boundary. The branch census
also found **ecca5b66**, a public UTF-16 extraction committed and gated on September
18 but never merged (AGENT-SYNC 02:05:29/02:11:42). **0eadd9a0** finishes that
interrupted thread by atomic merge from an isolated current-tip worktree. Existing
CharacterData conversion bodies now live in public Core; the binding calls those
same functions. Browser behavior and Script implementation are unchanged.

**aeb877ee** proves original `charAt(0)` and `slice(1)` calls on known Strings,
with explicit initial String identity and the same method receiver. Generated C++
uses `ctbrowser::wtf8_to_utf16`, ordinary `std::u16string::substr`, and
`ctbrowser::utf16_to_wtf8`. Empty suffixes, NULs, BMP text, surrogate halves and
rejoining are checked. Strict null comparisons now refine the original optional
String in the non-null arm through the existing predicate mechanism. Other
indices/coercions, wrong receivers, replacement and unproved null arms refuse.
All previous fixture entries remain unchanged; nine new early-return specimens
remain refused at the existing shadow-frame boundary.

Parallel **677f73ec** proves one product latch over literal or unchanged saved
primitive operands through the shared signed-product transfer. Nested/recomputed
operands, changing transport, unknown/noncanonical/zero strides and final-index
bounds retain their checks. Four older source expressions are preserved; 31
CFG/SCF rows and an eleven-function source oracle cover the increment.

Focused passes: Core/CharacterData **2/2 (0.67s)**; transaction **1/1 (4.32s)**,
host contract **1/1 (0.47s)**, class DOM **1/1 (106.62s)**, public class
**1/1 (201.81s)**. Class DOM checks **304 source observations, 8 native executions
and 2,228 refusals**. **20 Node/VM observations intentionally differ** because the
VM indexes bytes; each engine's expected result is explicit and native matches
Node. Existing nullable URI checks pass **44 positive lowerings / 64 refusals**.
Arrays **1/1 (1.96s; total 1.97s)** and four escape oracles **4/4 (0.93s)** pass;
new oracle **34 sites / 6 sound / 6 of 22 precision**, zero violations. All **13
tested hashes** match the devbox. Required formatting retains 26 diagnostics in
nine unchanged files; changed checks pass. Exact targets, preliminary failures
and skipped coverage are in HANDOFF; evidence: `/tmp/ctcompile-utf16-resume/`.

**Next:** original H still needs Unicode-correct `toLowerCase`, then proof for its
normalized assignment keys. Lowercasing can collide (`bsFoo` / `bsfoo`), so it
cannot inherit the injective prefix-removal proof; preserve insertion/overwrite
order and the `__proto__` setter constraint. Transformed keys must never gain
dataset-membership authority. Class-method aliases, receiver captures, repeated
iterators, every unused H slot and global wrapper publication remain. W still
needs Object.entries/destructuring/original s/RegExp/TypeError/spread before
inheritance. Full H and W/W+r/W+r+H remain refused. Broader induction and part-25
backlog, the application driver and native Bootstrap remain unfinished. No broad
suite, whole-Bootstrap replay, bundle-admission gain or push is claimed.


## Dataset loops beside class construction, 2026-09-19 UTC

**c1768e78** composes constructor-only classes with a local H slot's original
filter/for-of, dataset member reads and fresh-result writes, including
prefix-stripped keys. Dynamic operations retain complete typed DOM proof;
inactive loop completion uses exact continuation selection and MLIR arithmetic
folding. All 229 prior source entries and 14 effect checks remain unchanged.
Parallel **cf0ef348** proves unary latches over unchanged saved primitives.

Focused transaction/contract/class-DOM/public-class/assignment checks pass.
Class DOM records **260 source observations / 8 native executions / 1,968 refusals**;
assignment records **10 sources / 225 observations plus accessor traces / 8
binaries / lifetime sanitizer / 192 refusals**. **c420b0ae** repairs its one stale
member-read refusal, confirmed against the starting-source baseline. Arrays and
four escape oracles pass with zero violations; the new oracle has **31 sites /
8 sound / 8 of 20 precision**. All ten tested hashes match; formatting retains
26 existing diagnostics in nine unchanged files. Exact targets, timings,
preliminary failures and skipped coverage are in [HANDOFF](../HANDOFF.md).

**Next:** the complete original `class_dynamic_original` H.getDataAttributes
fixture now reaches the typed DOM member boundary. Prove the Unicode-correct
`charAt(0).toLowerCase() + slice(1)` key chain. In parallel, prove class-method
identity aliases without treating unrelated dynamic properties as method reads;
retain receiver-capture, helper-call-shape and repeated-iterator refusals. Then
complete unused H slots without invented authority, global wrapper publication,
and W's Object.entries/destructuring/original s/RegExp/TypeError/spread before
inheritance. Full H and W/W+r/W+r+H remain refused. Native Bootstrap and the
application driver are unfinished; no whole-bundle gain was measured.

## Combined M/filter callbacks, 2026-09-18 UTC

**bb96fa31** completes the combined callee-use proof from the previous handoff:
a local slot may capture original M and retain the original dataset predicate,
including through a class method capturing H. Each capture/callback use is proved;
complete typed DOM proof still covers callback bodies and actual inputs. Direct,
method and repeated-call/write-order specimens execute. Full original H remains
refused. Parallel **7cf800bc** proves bounded literal BitNot latches.

Focused transaction/contract/class-DOM/public-class checks pass; class DOM records
**248 source observations, 8 native executions and 1,818 refusals**. Escape arrays
and four selected oracles pass with zero violations; the new oracle has **30
sites / 9 sound / 9 of 19 precision**. All eight tested hashes match the devbox;
formatting retains 26 existing diagnostics in nine unchanged files. Exact targets,
timings and skipped coverage are in [HANDOFF](../HANDOFF.md). No full-suite, whole
Bootstrap or bundle-admission measurement was taken.

**Next:** prove original H.getDataAttributes dynamic dataset/output keys, loop and
Unicode normalization, then every unused H slot without invented parameter
authority. Global holders require wrapper publication. W's Object.entries,
destructuring, original s, RegExp/TypeError and spread remain before inheritance.
Preserve full-H and W/W+r/W+r+H refusals. Native Bootstrap and the application
driver are unfinished.
