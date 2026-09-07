# Handoff: continuing ctcompile

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

## Current native exported-getter checkpoint, 2026-09-07

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

The final compiler gate passes **159/159 lit cases** through **1/1 CTest** in
**48.64 seconds**. A full `tools/remote-build.sh` attempt stopped before CTest
on Claude's concurrent `ctbrowser/lib/Shell/bindings/exceptions.cpp:162` unused
`this` lambda capture under `-Werror`; it is reported in `AGENT-SYNC.md` and was
left untouched. The last full formatter run reports only Claude's live
`Shell/bindings/document.cpp`; all compiler C++
files and compiler whitespace checks pass. Logs are
`/tmp/ctcompile-native-integration-gate2.log` and
`/tmp/ctcompile-native-session-full-gate.log`. A complete new full-suite result
is not yet claimed; the preceding successful full gate is recorded below.

**Exact next native boundary:** extend the complete live callable/source-owner
graph to the immutable captured Map environment through wrapper return and
global publication, preserving allocation identity and shared ownership.
Connect that proof to existing Map/capture/table types and final component
admission. The Map publication specimen remains **0/4**; its **4/4** native gate
is proposed. Exact Bootstrap Data stays **0/7** in each CommonJS/browser/realm
fallback mode. Prefix completion cannot authorize future callers. Typed export
ABI, mutable slots and general realm owners remain further work. Full native
Bootstrap is unfinished.

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
  `aot_bridge.cpp` does `static_cast<op>(op_kind)`. Passing the CTJS ordinal
  would compile `**` into whatever `op(5)` is. A backend must spell it by name.
* **Phase 10 — started: one conversion pattern, matching on the INTERFACE.**
  `ctjs-opt --ctjs-lower-to-runtime` turns CTJS operations into `func.call`s on
  the real helper symbols, and the pass **names no operation** — which the plan
  calls the acceptance criterion. **Its arity check fired immediately and was
  right**: most helpers are not "frame + operands" (`ct_aot_binary_op` is
  `(fr, op_kind, lhs, rhs, out)` where the kind is an attribute and `out` is an
  out-parameter). A mismatch declines the match rather than failing the module,
  so what it declines is the work list for the rest of the phase.
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
   2026-08-22 — `ctbrowser/lib/Script/aot_bridge.cpp` has four helper bodies and
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
  32-bit `with_bx` (`statements.cpp:613`, `expressions.cpp:95`,
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
them; `aot_bridge.cpp` defines 32. A call to one of the other 37 **compiles
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
Either implement rows in `aot_bridge.cpp`, or widen into what is already
implemented: `ct_aot_new_object`, `ct_aot_new_array` and `ct_aot_truthy` are
there, `ct_aot_append` and `ct_aot_construct` are not, so object and array
literals are half-reachable.

## Using subagents

The last two sessions used `Workflow` heavily and it paid for itself. Ask agents
to REFUTE a design against the code, not to agree with it. Their best output has
been defects in already-committed work: two memory-safety holes in the image
loader, and two stale standing decisions in `ctbrowser/docs/` that this session's
Boost floor change had invalidated.
