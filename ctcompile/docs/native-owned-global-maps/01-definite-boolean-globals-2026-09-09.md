[Back to native-owned-global-maps.md](../native-owned-global-maps.md)

## Definite Boolean globals, 2026-09-09

**df304fdf** extends exact scalar-read evidence to Boolean origins and adds
independently typed Number/Boolean global observations. Types still come from
actual stored SSA lattices and the complete store set; each output helper
checks the exact runtime tag. The original **681c8895** and literal-copy
**d3a90c01** advance to **5/5 native** with eight calls and five observations.

**ab61f9ac** completes the interrupted execution tests: **34 native programs**,
**49 typed probes**, **14 wrong-tag/null/missing-store controls**, forgeries,
prepared reruns and a new **128-future-call** mixed Boolean/Number/Map lifetime
pass on the devbox. Explicit/deduced GCC/Clang binaries contain no VM symbols.
Snapshots survive both branches, owner/table release, independent reentry and
final Map/leaf destruction. Budgets **9271/9351** check **31/29 cutoffs**.

The corrected refusal census has **eight ownership**, **five Map-identity**
and **two global-storage** cases. Optional/mixed Boolean entry conditionals
fail the complete ownership proof, before storage admission. Exact diagnostics
and repairs are checked; **557** historical helper rows, **24** historical
hashes and all **49** current source strings remain unchanged by the repair.
Fresh **14/14 focused CTests pass in 149.81 seconds**. The initial warning-free
247-step full gate passes **511/517 CTests in 1704.85 seconds**, with five known
browser failures and lit **163/165**. Two historical Boolean-result refusals
were stale; **7dc796b7/48885ae1** preserve their exact sources and gate native
method/Map results at **3/3 and 4/4** with Boolean output. Focused executions
and all **197** refusal-census cases pass, preserving the **173** historical
classifications. The initial Map test executed **283 programs/36 lifetimes**
before the stale guard. The corrected full lit rerun passes **165/165 in
1040.72 seconds** (CTest **1040.78**), including all **284 programs/36 lifetimes**.
All **28** final code/test hashes match local files, committed HEAD, the frozen
inputs and devbox sources. Across the initial full run and corrected rerun,
all **372 compiler CTests** and **512/517 total** have passing results; this is
not a second full 517-test run. Only the five established browser failures
remain. Corpus coverage stays **19/574, 39/4754, 45/7725** in both modes, zero
pruned; exact Bootstrap Data stays **0/7, 0/7, 0/8**. Final evidence is
`/tmp/ctcompile-boolean-gate-final-evidence.json`.

Next are owning String globals: unchanged **3a99e34c/f0a03c19** copy/literal
sources remain **0/5**, respectively at live scalar-read/Map identity and
Number/Boolean-only storage. String proof, actual type subscriptions, owning
storage and exact byte/tag observations must advance together. Full Bootstrap
and direct platform integration remain unfinished; see `bootstrap-provider-next.md`.

## Constant-only Number globals, 2026-09-09

`06c4a649` extends the existing exact scalar-load evidence to constant-only
Number expressions with no published-call dependencies. Single-store
initialization, original SSA scope/order, complete environment/owner proof and
actual stored-value type subscriptions remain mandatory. The unchanged
**3c1dfd95** source now admits **5/5** in both modes with eight calls and all
five observations preserved. Six focused proof CTests pass in **131.63 seconds**.

`4d2906d1` passes **17 native programs**, **27 typed observations**, ten
refusal/edit families and a new **128-future-call** sanitizer lifetime. Real
scalar copies/arithmetic and Map/leaf effects remain. Aliases survive owner
release, leaf mutation, independent reentry and final Map/leaf destruction.
Budgets **9271/9441** check **31/32 cutoffs**. All **173** historical refusal
classifications, **514** helper rows and **24** original source hashes remain.
The integrated **266-program/35-lifetime** gate passes in the final full run.
The warning-free **241-step** build finishes **512/517 CTests in 1698.62 seconds**,
including all **372 compiler checks** and **165/165 lit cases in 979.39 seconds**.
Only the five established browser failures remain. Fourteen code/test hashes
match local files, HEAD, the frozen inputs and devbox sources. Native coverage
remains Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725** in both modes,
with zero pruned; exact Bootstrap Data remains **0/7, 0/7, 0/8**.

The next **681c8895** Boolean copy still refuses Map identity. Its exact
**d3a90c01** literal-copy candidate removes that refusal but reaches the separate
numeric-only global admission/output boundary. Both remain **0/5** with complete
ownership and unchanged five scalar observations. Extend independently proved
Boolean edges and typed observation output together; retain exact tag checks,
optional/boxed refusals and actual SSA lattice authority. String globals need
owning storage too. See `bootstrap-provider-next.md` and `HANDOFF.md`.

## Definitely initialized scalar aliases, 2026-09-09

`396b7e46` removes only implicit absence for an exact scalar load proved by the
complete current owner. The live single store, binding and original value must
agree with the global index. Actual SSA lattices still supply every type:
pending, widening and boxed results are not replaced by host Number categories.
Dynamic globals, duplicate writes and incomplete/stale proofs retain refusal.
The original eight-call alias **8003b4bc** and seven-call direct observation
**06efa534** advance 0/5 to **5/5** in both modes, preserving trace=12/1.

`bbedee5b` passes nineteen focused native programs, four unowned/two complete-owner
refusal families, prepared/emitted dataflow, forgeries/reruns and two saved-scalar
sanitizer lifetimes, including the new alias family. Saved aliases survive 128
future calls, both flags, owner/table destruction, checks before reentry and
final Map/leaf release and mutation. Budgets 9249/9307 each check 32 cutoffs.
All 173 old refusal classifications and 506 historical helper rows are unchanged.
All six host/owner/type CTests pass across the initial run and corrected type
expectation. The independent type query covers twenty rows, eight live edits
and forced pending/i32/f64/boxed propagation. The integrated **250-program,
34-lifetime** gate passes, with **165/165 lit cases in 928.06 seconds** (CTest
928.26). The warning-free **243-step** full build completes **512/517 CTests in
1649.36 seconds**, including all **372 compiler checks** and only the five
established browser failures. All twelve code/test hashes match committed HEAD,
frozen inputs and the devbox. Fresh corpus coverage stays Bootstrap **19/574**,
p5 **39/4754**, Phaser **45/7725** in both modes, with zero pruned; exact Data
remains **0/7 CommonJS/browser, 0/8 AMD**.

The exact next **3c1dfd95** constant-only Number load still lacks the required
published-result dependency; **41a33e40** replaces only its copy with literal 7
and reaches 5/5 with identical observations. Full Bootstrap and direct browser
API integration remain unfinished. See `HANDOFF.md` for measured gates.

## Saved Number global reads, 2026-09-09

`801794d8` supplies per-load initialization/value/result-dependency edges only
after the complete current host and owner proofs. NativeMap uses them only to
exclude an independently proved Number read from its global identity refusal;
ordinary ownership lookup and native type inference remain separate. Every
source write, SSA scope, source order, actual callable and future method input
must still qualify. Partial budgets and stale/forged reports expose no edges.

The exact d74ae2ee saved sum and 867378b1 size snapshot now admit **5/5** in both
modes, retaining all eight calls and trace=3/12. Both compile/run with exact
requested scalar observations under explicit/deduced GCC/Clang and no-VM checks.
All five host/owner CTests pass in **126.27 seconds**, including 16 rows and
11 live edits per form, every 7802/7873 host cutoff and prior owner budgets.
`1bd5b5ac` passes fourteen focused programs, four unowned and four complete-owner
refusal families with exact repairs, live dataflow/forgery controls and the new
128-call sanitizer lifetime. Number snapshots outlive all Map/leaf mutations
and owner release. Budgets 9920/9249 check 30/32 cutoffs. The 173-case refusal
census preserves all 169 historical classifications and hashes. The full
245-program/33-lifetime gate passes. The warning-free 242-step full build
finishes **512/517 CTests in 1652.00 seconds**, including all **372 compiler
checks** and **165/165 lit cases in 935.40 seconds** (CTest 935.46). Only the
five established browser failures remain. All eighteen code/test hashes match
local files, HEAD, the frozen input and final devbox sources. Generated C++
retains actual scalar/Map effects and ownership with no Script/VM/AOT symbols.
Fresh corpus coverage stays Bootstrap **19/574**, p5 **39/4754**, Phaser
**45/7725** in both modes; exact Data remains **0/7, 0/7, 0/8**.
[HANDOFF.md](../HANDOFF.md) records the complete measurements.

A saved alias `const alias = first` now passes Map identity but fails final
global observation because inference still adds Undefined at every global
load. Keep that independent refusal and the original source; the exact
`first + 0` repair retains calls, the alias store and its read and admits.
[bootstrap-provider-next.md](../bootstrap-provider-next.md) records the exact
next source and the initialization/lattice proof it needs.

## Entry arithmetic over published Number results, 2026-09-09

`0468fed4` admits Add/Sub/Mul/Div/Mod/Pow only from independently exact Number
categories. It keeps invocation worklist facts private until the complete
family recheck succeeds, including all future input categories. Expressions
can feed later calls; saved globals checked in source order retain their category.
No category is an evaluated constant or branch predicate. Actual SSA scopes and
both SCF yields prove independently. Existing native Number and Map lowering
emits the runtime operations unchanged.

`83c32f0c` gates eighteen programs, both optimization modes, explicit/deduced
GCC/Clang and no-VM symbols. The original eight-call sum is now **5/5**, trace=3;
its eight-call arithmetic-free repair remains **5/5**, trace=1. Size-returning
operands distinguish evaluation order, NaN keys retain SameValueZero identity,
and two sanitizer lifetime families exercise 128 future calls, both Boolean
flags, owner/table release, reentry and final Map/leaf release. Nine ordinary
refusal/repair families and two saved-global owner-complete refusals pass,
including stale/fresh reports, reruns and exact live operand preservation.
Budgets **9752/12792** each check 31 cutoffs, with no speculative rollback interval.
All 414 historical helper rows and twelve continuation source hashes are intact;
169 historical refusal cases have zero census errors.

Saved global result sources remain **0/5 native** after their host proof succeeds:
NativeMap's global-read guard recognizes owned root loads but has no independent
scalar-read proof. Their exact inline repairs preserve arithmetic and calls and
are **5/5**. This is the next boundary; see
[bootstrap-provider-next.md](../bootstrap-provider-next.md) for exact sources and
[HANDOFF.md](../HANDOFF.md) for measured gates. The complete warning-free 250-step
build passes **512/517 CTests in 1583.01 seconds**: all **372 compiler tests**
pass, with only the five established browser failures. Lit passes **165/165
in 874.79 seconds**, including **233 published Map programs and 32 sanitizer
lifetime families**. Fourteen final code/test hashes match committed HEAD and
the devbox. Inspected generated sum/lifetime C++ retains real Number arithmetic,
Map/leaf ownership and callable captures without a Script or AOT runtime.
Remeasured full Bootstrap stays **19/574** in both modes; exact Data remains
**0/7 browser/CommonJS, 0/8 AMD**. Full native Bootstrap and direct browser API
integration are open.

## Standard clear and arbitrary-key absence, 2026-09-08

`1c352a82` proves zero-argument captured clear and its Undefined result;
`c2b49f99` gates 21 native programs and three saved-callable lifetime families.
Both original seven-call clear sources now reach **5/5 native**, trace=1, in
both modes. All original 22 clear source hashes are preserved. Each invocation
starts with unknown contents; clear closes the set of possibly present keys,
sets add possibilities, and joins union them. Per-key absence intersects,
including implicit clear absence on the other arm, without borrowing path-local
type refinements. Saved values and when-present payload facts stay independent.

Each source/prepared host/owner query passes 29 rows, eight scope controls,
three live spelling/receiver/arity mutations per budget fixture and exhaustive
work limits. Node/interpreter, explicit/deduced GCC/Clang, no-VM checks, five
refusal/repair families and three ASan/UBSan/leak lifetime families pass. Future
128-call loops retain callables across owner/table release and reentry, free
every Map, and keep one observed leaf only until its final independent release.
Native budgets complete at 4121/4866 with 31/30 cutoffs. Twenty focused CTests
pass across the first 19/20 run and corrected escape-fixture rerun.

The full warning-free 240-step build completes at **511/517 CTests in 1420.17
seconds**, with the same five browser failures and one old Map classification.
Lit passes **164/165**; all 215 positive programs and 30 lifetime paths ran before
the unchanged nine-call `seeded_cleared` source was incorrectly required to stay
unowned. **`7011c79e`** checks its now-complete owner and exact unsupported nullable
Number Map-key carrier instead, preserving all source calls and operands. Its
clear-to-has repair also retains nine calls. All twelve carrier families and
HostContract CTest pass after the correction. The complete corrected lit rerun
passes **165/165 in 776.42 seconds**, completing **215 programs and 30 lifetime
families**. All **372 compiler tests have passing results across the full run
and corrected rerun**; no second 517-test run is claimed. A separate report-only
fix, `5f7b5a2f`, counts the nine ordinary seeded refusals from the named case set;
all checks outside the final print remain identical to the executed driver.
See [HANDOFF.md](../HANDOFF.md) for measurements and source evidence.

Next is entry numeric addition over proved method results. The exact eight-call
three-result expression remains unowned 0/5; an eight-call repair keeps all
calls and removes only their addition, reaching 5/5. Exact zero Map size and
String leaf fields remain separate boundaries. Full native Bootstrap and direct
browser API integration remain unfinished.

## Definite absence after exact deletion, 2026-09-08

`f05c3e0b` and `3825f3cf` recover the interrupted object-valued Map absence
slice. Exact seven-call Undefined and seven/nine-call post-delete identity
sources now reach **5/5 native** in both modes. The **21-program** focused gate
passes explicit/deduced GCC/Clang and Node/interpreter, ten exact refusal/repair
controls and two ASan/UBSan/leak lifetime families. Saved callables survive
128 future calls, owner/table release and reentry before final Map/leaf release.

The source proof keeps definite absence independent of possible absence and
preserves when-present payload facts through deletion. Aliasing writes clear
absence; surviving nonidentical branches intersect it; saved read values never
retarget. Complete scope checks cover scalar/flag, Map, method and object uses.
Thirty rows per source/prepared host/owner form and exhaustive budgets pass.
Historical fixtures remain intact; exact-delete expectations now assert precise
Undefined/Number results. Both affected CTests pass in **91.56 seconds** after
repairing the initial guarded-payload regression. Native budget completions are
**4160/4866**, with **32/30 cutoffs**. The initial full build passes 250 steps
warning-free; CTest is **511/517 in 1339.11 seconds**, with 371/372 compiler tests
passing. The same five browser failures remain. Lit is **164/165**: after 193
positive programs/lifetime paths, the old `seeded_deleted` check wrongly expects
an unowned source. Its owner now completes while the nullable Number Map-key
carrier still refuses.

**`4eb2390d`** updates those classifications after a 155-case census in both
modes. Eleven unchanged sources retain complete ownership and exact unsupported
type diagnostics, prepared result/actual/formal edges, live receiver/callee/capture
operands, exact repairs and fresh/stale report checks. The original ten-call
empty-String deletion program reaches **6/6 native**, trace=2. It uses the existing
nullable String key and owning String payload types; explicit/deduced GCC/Clang,
Node/interpreter and no-VM checks pass. Future Undefined/empty/distinct-key calls
produce observation 255 and distinguish three blind controls. All 328 historical
source/refusal rows and 33 metadata rows preserve bytes.

The complete corrected lit rerun passes **165/165 in 709.94 seconds** (CTest
710.00). The published Map gate passes **194 programs and 27 lifetime families**.
All **372 compiler tests have passing results across the first full run and
corrected rerun**. Twelve final code/test hashes match local sources, committed
HEAD and final devbox sources. Next is captured `Map.clear()` admission, followed
by arbitrary-key absence after clear. Exact Data/full Bootstrap and direct
browser API integration remain unfinished. See [HANDOFF.md](../HANDOFF.md).

## Strict fresh object comparisons, 2026-09-08

`2593acd7` admits comparison-only fresh allocations after a complete independent
strict-use census. The unchanged local six-call and historical eight-call
programs advance **0/5 -> 5/5 native**, both with trace=0; exact saved-object
repairs remain **5/5**, trace=1. `316818b2` gates all four under explicit/deduced
GCC/Clang, matching Node and the interpreter, without Script/VM symbols.

Comparisons do not merge operand schema groups or allocation identities. Scalar
field families prove their own closed property environment, including live Map
spelling/receiver/arity, known source calls and operand dominance. Unknown effects,
coercing observations, dynamic/prototype/accessor fields, outgoing ownership edges
and untracked SCF yields refuse. A freshly checked source owner can authorize its
ordinary roots; report attributes cannot. Existing Map families retain their census.

The focused gate passes **15/15 CTests**; identity tests cover **36 rows and 37
live/fresh states**. Four lifetime families check 128 future calls, owner/table
release, replacement/deletion, reentry, distinct retained leaves and final release
under ASan/UBSan/leak checks. Budget sweeps finish at **3790/4542**, with **32/29
cutoffs**. All 297 current helper sources and six previous continuation sources
preserve bytes. The full warning-free 245-step build completes at **511/517
CTests**: the five existing browser failures and one diagnostic lit failure.
The published Map test passes all **172 programs/25 lifetime families**.
`8e603ce5` preserves the earlier closure census's specific refusal sentence;
its rebuilt type test and original diagnostic lit pass, with no source/expectation
changes. The complete corrected lit rerun passes **165/165 in 658.71 seconds**;
all **372 compiler tests have passing results across the full run and rerun**.
Thirteen code/test paths match HEAD, final frozen input and final devbox sources.

Next is definite absence for **object-valued** captured Maps. Thirty-one
unchanged probes agree with Node/interpreter and in both modes: **13 reach 5/5,
18 remain unowned 0/5**. Setting an object, deleting its exact key and comparing
a fresh `get` with `void 0` remains **0/5**, seven calls, trace=1. The unseeded
six-call and numeric-payload seven-call controls already reach **5/5**. The exact
historical seven/nine-call object comparisons remain unowned **0/5**; saved-before-
delete repairs stay admitted. `present=false` is insufficient to prove absence.
Saved values, possible key aliases, disjoint writes and branch joins need
independent facts; exact reseeding already works. `clear()` is a separate host
method gap, including when comparing a saved object. Four identical-arm probes
collapse eight raw calls into seven prepared calls; they do not validate a live
two-arm join. Full Bootstrap Data and browser API integration remain open. See
[bootstrap-provider-next.md](../bootstrap-provider-next.md) for the exact source,
and [HANDOFF.md](../HANDOFF.md) for final gate evidence.

## Initialized local fields, 2026-09-08

`aac9fd27` removes implicit absence from a field read only after a fresh bounded
allocation/initialization proof. `507fe153` gates all seven unchanged raw direct,
Map-loaded and saved-alias field results: **5/5 native** in both modes with
Node/interpreter and explicit/deduced GCC/Clang agreement. The raw saved setter
passes future numeric arguments and ASan/UBSan/leak lifetime checks. No runtime
carrier or browser source changed.

Exact allocation origins remain independent of schema families and current Map
entries. Saved reads survive overwrite/delete and see later scalar alias writes.
Both branches must preserve initialization. Unknown effects also invalidate
later fresh allocations; calls/loop-carried origins, accessors, stale constructor
or method source and cross-scope values withhold the proof. Full schema type
joins retain explicit Undefined/Boolean stores on any allocation. Two exact
complete-owner carrier refusals and their one-edit repairs gate that distinction.

The final focused gate passes **14/14 CTests in 17.08 seconds**, seven native
programs and the raw lifetime family. Inference budgets are **89/89/30/112**;
native budgets **3464/3957/4555**, with **31/33/29 cutoffs**. All 295 historical
helper source rows and sixteen prior probes preserve bytes. The full 246-step
build passes warning-free. CTest is **512/517 in 1267.11 seconds**, all **372
compiler tests** and **165 lit cases** passing, including **170 programs and
21 lifetime families**. Only the five existing browser failures remain.
Fourteen code/test paths match HEAD/frozen/final devbox; inspected emitted C++
retains runtime field operations, fresh leaves and saved owning reads without
Script/VM symbols. See [HANDOFF.md](../HANDOFF.md).

The next measured gap is comparison-only fresh identity recognition: exact
six/eight-call programs have complete owners but remain **0/5**. Their saved
identity repairs remain **5/5**. Exact seven/nine-call post-delete sources remain
unowned **0/5** and need a separate definite-absence proof; `present=false` is
not sufficient. Full Bootstrap Data and browser API integration remain open.


The [publication specimen](../../test/CTNative/Exports/boundary.js)
admits **4/4 native** with a fingerprinted `host-manifest` that selects
`host.slot`, the numeric `trace` observation and `initial_intrinsics: ["Map"]`.
Without the manifest, or without its explicit standard Map identity, admission
remains **0/4**. The method may now mutate its Map with standard `set`, `get`,
`has` and `delete` operations over primitive contents. A published setter and
getter sharing that Map now advance **0/5 -> 5/5 native**, preserving `trace=1`.
A three-method variant admits **6/6**. Full native Bootstrap Data is unfinished.
The shared specimen stays **0/5** without the manifest or standard Map identity.
The setter may take independently proved primitive arguments, including the
live result of its sibling getter. The `set(key)` version below advances
**0/5 -> 5/5**, with `trace=1` and the getter still zero-argument.

```js
var host = {};
(function(factory) {
    host.slot = factory();
})(function() {
    const state = new Map();
    return {
        get() { return state.size; },
        set(key) { state.set(key, 1); return state.size; }
    };
});
host.slot.set(host.slot.get());
var trace = host.slot.get();
```

The compiler proves one ordinary root, one wrapper invocation, one factory,
one immutable capture binding and one standard Map allocation, initially empty.
The returned table has distinct fixed methods, each with a current call using
its actual table receiver. A complete family census follows every closure that
captures the same binding, checks its publication and body, and retains every
Map read and call. Methods use primitive keys, primitive or independently checked method-local
leaf payloads, and exact receivers and arities. An uncalled or unsafe sibling withholds the
complete owning plan. Separate Map allocations cannot inherit a shared owner.
Repeated capture loads and fluent `set` returns alias the same Map. Publication
and the entire source function chain remain; allocation, mutations, calls and the
observation execute at runtime. No previous invocation supplies a later result.

Before checking any family body, the query discovers and validates every current
method call. A bounded dependency worklist classifies actuals from primitive
source expressions or already completed producer proofs. Every formal receives
the complete union of its actual categories. The current
boundary permits one primitive category or a finite String/Null/Undefined set;
other mixed parameters remain refused. The proof retains each live SSA operand,
formal and alternative set. It widens truthiness within each category, so an
observed startup flag never selects a future branch. Distinct strings or numbers
need not have the same value.
The body may treat a formal as primitive only after this evidence succeeds.
Source and prepared functions retain exact arities, receiver/callee identity,
and the prepared Map environment before the explicit arguments. Only a complete
body/effect/use census publishes finite result alternatives: `size` is numeric,
`has`/`delete` are boolean, and literal or typed-formal returns retain their category. A
consumer declared before its producer waits for a later worklist pass. No
optimistic tag seeds a circular dependency, and no recursive `propertyCall`
query supplies authority. A `Map.get` can retain a local `set`'s independent
payload tag when its key is the same SSA value or a SameValueZero-equal primitive
constant. Each invocation starts with unknown contents. Bounded per-key facts
preserve definite presence and an independently optional payload tag. A possibly
aliasing `set` preserves presence and joins the old and new payload tags: equal
proved tags survive, while differing or unproved tags become unknown. A later
possible write cannot recover an unknown tag; an exact-key write replaces it.
A possibly aliasing `delete` removes definite membership while retaining the
payload tag valid whenever present. Independently disjoint keys preserve both. Different primitive tags are disjoint without
coercion; different SSA names of the same primitive type may alias. Both zero
encodings and every NaN payload denote the same Map key. Each set also records
its own key and payload independently of any earlier join. Every fact comparison
spends work budget.
Only the complete method body and use census publishes its result alternatives.
Unique initialized global actuals are checked against the full initialization proof.

Native Map preparation separately rederives presence for these live published
reads using its existing instance/key analysis. A proved read uses the Map's
inferred payload type and `map_get_present`; other reads remain nullable.
The host result tag supplies neither the Map schema nor presence annotations.
The presence analysis uses the same primitive-key comparison for direct deletes,
invalidating both present-key facts and cached `has` observations across the
schema family unless their keys are independently distinct. Transitive call
summaries still invalidate the entire affected family conservatively.
Every source Map lookup, producing call and consuming argument remains runtime.

This is an ownership/effects proof. Native admission must still prove supported
key, value and result carriers. The gate includes homogeneous numeric, boolean
and owning-string values with numeric, string or boolean keys. A primitive result
alone is insufficient; mixed stored payloads still require a separate carrier proof.

The imported wrapper calls `factory()` indirectly. The host query follows
argument 3 of the entry's sole direct wrapper invocation to the exact supplied
uncaptured closure. It checks every transport use, source-program provenance,
argument count and receiver before following that call's return. This live
source proof succeeds before native preparation starts. A unit containing the
actual public, framed importer shape guards this boundary alongside the
previous direct-call specimens.

Preparation validates the supplied fingerprint, then works on a disposable
clone. Existing callback specialization, closure lifting and immutable-cell
unboxing connect the Map to a typed owning environment. They preserve the
wrapper/factory chain and the getter's real receiver. Native facts are rebuilt;
input annotations cannot erase a field initialization or observation store.
Every method's plan is checked before lifting any member. Cell unboxing waits
until all closures have acquired their Map environment; source and prepared
capture stages cannot mix. Each changed graph must pass a fresh complete
ownership query. Map preparation may pass only host reads identified by that
live ordinary-root proof; unrelated
host reads still prevent the standard Map identity proof. Final admission
reconstructs ownership again.

Generated C++ reuses the shared root, shared method table, shared Map handle
and typed `std::function` carriers, including `js_num(std::string)` for the
setter and `js_num()` for the getter. Each method captures the same Map owner
by value. Native output links neither the interpreter nor the collector. Escape
analysis still reports global publication as `StoredGlobal`; general global
loads remain external. The new owner does not require a weaker escape verdict.

## Measured possible-alias join gate, 2026-09-07

The retained producer below advances **0/5 -> 5/5 native**, preserving
Node/interpreter `trace=1` through the same published `set(get())` entry:

```js
get() { state.set(0, 1); state.set(state.size, 2); return state.get(0); }
```

The gate passes **55 complete programs**: fifteen **4/4**, thirty-three **5/5**
and seven **6/6**, with matching Node, interpreter and standalone explicit/deduced
GCC/Clang execution. Eight new cases cover possible and actual overwrites,
saved runtime keys, repeated writes, reseeding, runtime payloads and distinct
formal arguments. The actual-overwrite witness produces **2**; replacing the
getter with its old payload produces **1**. The generated calls and Map lookups
remain runtime operations. All six existing ownership/lifetime variants pass
ASan/UBSan, use-after-scope/return and leak checks; linked-symbol gates pass.
Thirteen seeded proof refusals retain every call. Four separate carrier
refusals include exact-key reseeding after an incompatible possible write: its
host result tag recovers, while the mixed Map schema still refuses native code.

Source/prepared host join proofs complete at **2172/2258** steps; owner proofs
at **5101/4992**. Repeated owner joins complete at **5186/5085**. Every smaller
budget withholds the entire proof. First source native completion is **5774**
for the dynamic write and **16339** for the parameterized variant, with **31/32**
checked cutoffs and no natural speculative rollback interval. These are work
limits, not speedups. Ownership CTest passes in **18.21 seconds**; host CTest
passes in **1.64 seconds**, followed by the complete native program gate.
Logs: `/tmp/ctcompile-map-joins-proof.log` and
`/tmp/ctcompile-map-joins-focused.log`. The full generated gate is recorded in
[HANDOFF.md](../HANDOFF.md) when complete.

## Preceding per-key gate, 2026-09-07

The producer below advances **0/5 -> 5/5 native**, preserving Node/interpreter
`trace=1` through `host.slot.set(host.slot.get())` and the final getter:

```js
get() { state.set(0, 1); state.set(1, 2); return state.get(0); }
```

The complete native gate passes **47 programs**: fifteen **4/4**, twenty-six
**5/5**, and six **6/6**, under explicit/deduced GCC and Clang output. Seven new
cases cover the earlier key, a disjoint delete, overwrites, reseeding, nine live
keys, and string/boolean keys. All producing lookups, consuming operands and
runtime calls remain. The existing six lifetime variants still pass ASan/UBSan,
use-after-scope/return and leak checks. Nine seeded proof refusals preserve every
source call; the three unsupported payload-carrier boundaries remain separate.

The per-key source/prepared host proofs complete at **2145/2230** steps; the
owner proofs at **5072/4962**. Disjoint-delete owner proofs complete at
**5116/5007**. Every smaller budget withholds the complete proof. Source native
admission first completes at **5721** for the earlier key and **5700** for the
disjoint delete, with **30/31** checked cutoffs and no natural speculative
rollback interval. These are work-limit measurements, not performance claims.
The initial focused CTest gate passes **2/2 in 14.16 seconds**, followed by all
47 native programs; log: `/tmp/ctcompile-map-keyfacts-focused2.log`.
The final frozen generated gate passes **475/475 CTests in 720.98 seconds**,
including **163/163 lit cases**; log: `/tmp/ctcompile-map-keyfacts-full.log`.
Fresh component counts and the browser baseline are in [HANDOFF.md](../HANDOFF.md).

## Preceding seeded-result gate, 2026-09-07

Commit `b2466a0` advances the seeded producer below from **0/5 to 5/5 native**,
retaining Node/interpreter `trace=1`:

```js
get() { state.set(0, 1); return state.get(0); }
// The entry retains host.slot.set(host.slot.get()) and the final getter.
```

The gate passes **40 complete native programs**: fifteen **4/4**, nineteen
**5/5**, and six **6/6**, in explicit/deduced GCC and Clang forms. Five new
producer cases cover the seed, repeated calls, overwrite, a runtime key and
distinct formal actuals. The previously nullable-key specimen is now **4/4**:
its local write independently proves the later lookup before using its result
as a key. The sixth lifetime variant seeds from the current Map size and reads
the new value after every call. Saved and fresh callables retain independent
Maps through 1024 further invocations each, then free them; both forms pass
ASan/UBSan, use-after-scope/return and leak checks.

Seven new contents/presence refusals preserve every source call. Three further
cases establish complete host ownership but refuse unsupported string, boolean
or mixed Map payload carriers. Missing intrinsic identity, stale/fresh forged
presence, reruns and work limits cannot bypass the live proofs.
Source/prepared host proofs complete at **2075/2153** steps; the owner proofs
complete at **5016/4899**. Every smaller budget withholds the complete family.
The seeded native specimen first completes at **5558**, with **30** checked
cutoffs; no natural speculative rollback interval is reached.

The integrated focused gate passes **7/7 CTests in 18.26 seconds**, followed
by the complete program/lifetime gate. Log: `/tmp/ctcompile-map-presence-integrated.log`.
The full generated devbox gate passes **475/475 CTests in 693.86 seconds**,
including **163/163 lit cases**. Log: `/tmp/ctcompile-map-presence-full.log`;
corpus and boundary measurements are recorded in [HANDOFF.md](../HANDOFF.md).

## Preceding result gate, 2026-09-07

Commit `c18b94b` passes **34 complete native programs**: fourteen **4/4**,
fifteen **5/5**, and five **6/6**, matching Node, the interpreter, and standalone
explicit/deduced GCC 13 and Clang 18 binaries. Nine new result cases cover the
exact `set(get())` boundary, reversed method declaration order, repeated calls,
local aliases, two mutating actuals, boolean `has`/`delete`, an owning string,
and a typed-formal return. The two-actual witness observes **3**; reversing its
arguments observes **4**. Generated C++ retains every producing call, consuming
SSA operand and final observation in order, and all linked-symbol gates pass.

The five existing lifetime variants below pass unchanged in both C++ forms;
no new runtime carrier was introduced. Nine new result-proof refusals retain
every original call. An additional implicit-undefined result establishes a
complete owner but refuses its unsupported Map key at **0/6 native**. That
control checks the retained prepared calls and their result operands; valid
ownership preparation is allowed to precede a carrier refusal.

Source/prepared result units check every incomplete budget at **4789/4656**,
including changed producer return tags, stale/fresh fingerprints and unknown
returns. Ordinary two-method, three-method and parameter units complete at
**2877/2785**, **6603/6418**, and **2883/2792**. The six source admission
budget specimens complete at **1239/50004/1353/3297/3261/5336**, checking
**31/31/31/29/32/29** cutoffs. None reaches a natural speculative rollback
interval; these numbers do not claim improved analysis performance.

Focused CTests pass **3/3 in 7.50 seconds**, followed by the full native
program/lifetime/refusal gate. Log: `/tmp/ctcompile-map-results-integrated2.log`.
The full frozen generated build passes **475/475 CTests in 674.94 seconds**,
including **163/163 lit cases in 113.29 seconds**. Log:
`/tmp/ctcompile-map-results-full.log`; evidence:
`/tmp/ctcompile-map-results-evidence.json`. [HANDOFF.md](../HANDOFF.md) records the
browser baseline and the separate evidence-collector Node-path correction.

## Preceding argument gate, 2026-09-07

`CTNative/Ownership/global-maps.test` covers **25 complete native programs**:
fourteen at **4/4**, ten shared setter/getter variants at **5/5**, and one
three-method variant at **6/6**. They cover publication, mutation, growing keys,
repeated growth, primitive operations, overwrite/deletion, fluent calls, and
boolean `has`/`delete` results used by later mutations. Shared variants check
reads before mutation and repeated calls. Six parameterized variants add string,
number and boolean keys, distinct repeated actuals, a method-local alias, and
two parameters for key and value. All twenty-five match Node, the
interpreter and standalone explicit/deduced GCC 13 and Clang 18 binaries.
Linked-symbol checks reject VM use.

The ordinary, mutating and growing methods pass ASan/UBSan, use-after-scope,
stack-use-after-return and leak checks in both C++ forms. The harness retains
owner, table and callable independently, releases the global, churns allocations
and invokes the callable after owner/table release. Weak witnesses observe the
actual Map allocation without retaining it. Reentry makes a distinct Map; each
Map expires when its last callable owner is released. The growing method uses
`state.set(state.size, 1)` on every call: saved and fresh environments retain
independent changing sizes through **1024 further invocations each**. A constant
result or a startup summary cannot satisfy this witness.

The shared growing-Map variant adds a fourth lifetime gate in both C++ forms.
It saves setter and getter independently, releases root/table/global owners,
churns **4096 allocations**, then reenters to create a distinct Map. Mutation
through the saved setter is visible to the saved getter and independent of the
fresh entry through **1024 further calls**. Releasing the setter retains the
Map through the getter; copying and releasing the last getter expires it.
The fresh getter likewise survives its root/table and frees its Map on release.

A fifth lifetime gate retains the typed string setter and getter. It changes
the caller's string buffer after insertion, then supplies long changing keys
to saved and fresh callables through **1024 further mutations each**. The
independent Maps retain their own string contents after root/table release
and expire with the final callable. Both C++ forms pass the same sanitizers.

Thirty source refusals cover mutable captures, non-primitive contents, cycles,
wrong method receivers/arities, detached/escaping methods, Map publication,
replaced intrinsics/prototypes, constructor arguments, additional allocation or
factory invocation, observable receivers/arguments, effects and throws.
Stale/forged contracts, reruns, missing intrinsic identity and incomplete work
retain the source operations and signatures. Three further controls establish
that complete ownership still cannot supply a numeric export for nullable or
boolean results, or a supported nullable Map key.

Five shared-family source refusals cover an uncalled sibling, unknown effects,
Map return, method replacement and detached publication. Ten argument refusals
cover missing/extra/mixed actuals, objects, callbacks, unproved properties,
call-result actuals and both positions of a two-parameter method. They preserve
every current call operand. A freshly fingerprinted forged contract cannot
admit a mixed-tag family.
Proof units cover source/prepared two- and three-method families, every
incomplete work budget, mixed capture stages, live sibling receiver/upvalue
mutations and distinct Map identities. Both stale and freshly fingerprinted
mutations must refuse; restoring the source restores the proof.
Parameterized units additionally check live formal/actual evidence, both
explicit argument positions, environment offsets, initialization order,
duplicate/later global stores, sibling-result recursion and uncalled methods.

The integrated gate measures these current admission budgets:

| Budget specimen | First complete admission | Cutoffs checked |
|---|---:|---:|
| Ordinary getter | 1236 | 31 |
| Sixteen getter calls | 49476 | 30 |
| Growing Map method | 1350 | 32 |
| Shared growing Map methods | 3285 | 29 |
| Parameterized shared setter | 3249 | 33 |

Each checks the sixteen budgets immediately below completion. None naturally
reaches a cutoff where the original proof succeeds and the prepared clone's
proof exhausts. These are failed-attempt preservation checks, not a new
Map-specific speculative rollback measurement. Existing scalar/table rollback
controls remain.

The source/prepared owner units also check every incomplete budget: **2865/2773**
for two methods, **6576/6391** for three methods and **2871/2780** for a
parameterized setter/getter. The expanded independent call census increases
proof work, particularly for the sixteen-call case; no performance improvement
is claimed. The focused gate passes **8/8 CTests** in **14.10 seconds**, followed
by the complete twenty-five-program native execution/lifetime gate. Log:
`/tmp/ctcompile-arguments-integrated-focused4.log`. See [HANDOFF.md](../HANDOFF.md)
for the corpus measurements and frozen browser baseline. The full generated
devbox build passes **475/475 CTests** in **652.00 seconds**, including
**163/163 lit cases** in **91.45 seconds**; log:
`/tmp/ctcompile-arguments-full-gate.log`.

## Nonempty size snapshots, 2026-09-08

The retained `seeded_dynamic_delete` uses `state.delete(state.size)` before
the producer's `return state.get(0)`. It advances **0/5 -> 5/5 native**,
retaining Node/interpreter `trace=1` and every runtime seed, size read, delete
and lookup. Both the host result proof and native presence analysis independently
establish `size >= 1` from a definite entry in the exact runtime Map. That
immutable numeric snapshot is distinct from zero, negative/subunit literals and
NaN. Later mutation cannot change the saved number. Different positive keys or
different size SSA values alone do not establish disjointness.

The gate passes **58 complete programs**, including three new size cases and
three size-key refusals with discriminating observations. Node/interpreter and
explicit/deduced GCC/Clang agree; all six existing sanitizer lifetime variants
pass. Saved-size execution returns 2, whereas replacing the saved number with
the current size returns 3. Zero-size, equal-positive-key and equal-snapshot
refusals preserve every call under fresh/stale forged markers and reruns.
Existing incompatible-overwrite tests now seed key 1 so they still exercise
actual possible aliasing. Source/prepared host proofs complete at **2212/2299**
steps and withhold the entire family at every smaller budget. Native completion
is **5753** for dynamic delete and **8607** for the saved snapshot, with **31/29**
checked cutoffs. No natural speculative rollback interval was observed.

The new independent native-presence lit test passes its three positive and
seven refusal programs, covering `has` branches, saved reads, initial/emptied
Maps, different runtime instances and recursive clearing. The focused host/owner
CTests pass. Logs: `/tmp/ctcompile-size-focused.log`,
`/tmp/ctcompile-size-native.log`, `/tmp/ctcompile-size-lit.log`.
The full combined gate is recorded in [HANDOFF.md](../HANDOFF.md).

## Distinct-key size bounds, 2026-09-08

The `seeded_size_two_entries` producer seeds keys 0 and 1, deletes
`state.size`, then returns `state.get(1)`. It now advances **0/5 -> 5/5
native**, preserving Node/interpreter **`trace=2`**. Both analyses independently
construct a pairwise-distinct subset of definite entries in the exact runtime
Map. A size snapshot is at least the subset's cardinality; subsequent mutations
do not change that saved number. Distinct SSA names alone never count twice.
SameValueZero still equates signed zeros and all NaN payloads.

Each read examines at most **64 candidates**. This deliberately yields a lower
bound, not an exact size; keys outside the examined subset cannot increase it.
Host analysis charges every examined candidate and comparison to its existing
shared work budget. Native presence filters by exact runtime instance, preserves
known immutable snapshots across mutation, meets branch facts conservatively
and discards loop-carried assumptions. Neither analysis consumes a forged size
annotation. Source/prepared host proofs complete at **2278/2372 steps**, with
no callable/property proof at any smaller budget. The 64/65-candidate controls,
late seed/read/delete mutations and signed-zero/NaN duplicate controls pass.

The gate passes **63 complete native programs**, including eight size programs
and ten size-key refusals, with Node/interpreter and explicit/deduced GCC/Clang
agreement and no interpreter symbols. All six existing sanitizer lifetime
variants pass. New saved-bound observations distinguish rereading size after
growth and after deleting all seeded keys; deletion controls distinguish real
mutations from no-ops. Runtime calls, lookups and result operands remain intact.
First complete native admission is **5931** for two entries and **9391** for the
saved/emptied Map, each with **30** checked cutoffs and no natural speculative
rollback interval. The combined focused CTest gate passes **12/12 in 28.45
seconds**. The independent presence lit gate passes **seven positives and
fourteen refusals**, including `has` branches, different instances and loop
invalidation. Logs: `/tmp/ctcompile-cardinality-focused2.log` and
`/tmp/ctcompile-cardinality-lit2.log`.

## Homogeneous boolean and string payloads, 2026-09-08

The existing `result_seeded_bool` and `result_seeded_string` programs advance
**0/6 -> 6/6 native**, preserving Node/interpreter **`trace=2`** and every
producing call, Map read and consuming argument. Host owner/result proofs stay
independent of native Map inference. Storage uses the existing
`std::shared_ptr<ctnative::map_storage<K, V>>`, with `V` equal to `bool` or
`std::string`; construction uses `make_map<K, V>()`. No interpreter, collector
or new value model is involved.

Ordinary boolean reads use the existing optional scalar carrier; ordinary
string reads use the existing nullable owning string carrier. Proved-present
reads return the exact stored type by value. `false` and empty strings retain
their tags, and missing reads remain undefined. String value snapshots copy
into owning vectors after the existing immediate `Array.from` proof. Boolean
value snapshots and mixed stored payloads still refuse.

The published gate passes **69 complete programs**, including six payload
programs and all **seven sanitizer lifetime variants** in explicit/deduced
forms. False/empty and overwritten-payload witnesses return **1** while their
blinded controls return **2**. Saved strings survive source overwrite/deletion,
Map destruction, caller-buffer mutation and independent reentry. Both compilers,
Node and the interpreter agree; linked native binaries have no Script symbols.
Two new deleted-payload refusals preserve every original call under fresh/stale
forgeries and reruns. Three mixed-carrier refusals retain their complete owner
proof and the prepared producing/consuming call edge.

Native admission first completes at **7474** steps for each original boolean
and string program and **8008** for the saved-string program, checking
**32/32/30** cutoffs respectively. No natural speculative rollback interval was
observed. Log: `/tmp/ctcompile-payloads-native.log` (its initial local Map lit
attempt failed only a refusal harness's annotation comparison; the published
gate itself completed successfully). The current full gate and final local
Map results are recorded in [HANDOFF.md](../HANDOFF.md).

## Closed mixed Map storage, 2026-09-08

The preceding `result_seeded_mixed_contents`, `result_seeded_join_reseed` and
`result_seeded_bool_string_contents` boundary advances **0/6 -> 6/6 native**.
Node/interpreter traces remain **2/3/2**, and the generated programs retain all
**9/10/9 calls**. Complete host owner/result proofs remain independent of the
native storage proof. Exact Bool/Number and Bool/String schemas now use
`std::variant<bool, double>` or `std::variant<bool, std::string>` for keys or
payloads. False and numeric zero remain distinct; each numeric alternative
preserves SameValueZero. A read's exact result never erases another stored type.

The must-analysis derives each mixed read's payload type from definite literal
writes, separately from membership. A same-tag branch join preserves the type;
differing tags, possible overwrites and callee writes invalidate it. `has`
establishes presence only. The type is seeded before monotone inference can
widen dependent keys or results. Emission constructs the exact alternative and
copies a proved read's scalar result; saved strings keep their owning bytes.

The published gate passes **76 complete programs**, seven mixed programs and
all **eight sanitizer lifetime variants** in explicit/deduced forms. Both
compilers agree with Node/interpreter and emit no Script symbols. Mixed
false/zero keys, empty strings, false results and saved strings have distinct
blinded controls. Two deleted-result refusals retain all calls under forged
presence/type facts and reruns. Number/String storage stays refused.

Native admission first completes at **7658/7910/7658** steps for the preceding
three specimens and **8367** for the mixed saved-string specimen, checking
**30/30/30/31** cutoffs. No natural speculative rollback interval was observed.
Log: `/tmp/ctcompile-mixed-native2.log`. The separate local representation gate
passes nine observations under both storage implementations and seven mixed-read
refusals. Full gate results are recorded in [HANDOFF.md](../HANDOFF.md).

## Saved scalar reads through writes, 2026-09-08

Commit `d9a4b04` advances the preceding `saved_read_write` from **0/6 to 6/6
native** in both optimization modes, with Node/interpreter **`trace=1`** and
all **twelve calls preserved**. The independent native must-analysis retains
scalar read results separately from current Map contents. Overwriting or
removing the source entry does not change a saved Boolean, Number or owning
String. Writing that scalar to another entry establishes its exact payload
without removing any stored alternative from the schema. Possible aliasing
writes and callee effects still invalidate mutable entry facts; branch joins
intersect the saved SSA facts. Input annotations supply neither fact.

The published gate passes **83 complete programs**, including seven new saved
chains and all **nine sanitizer lifetime variants**. Missing first or second
reads and deleted results refuse in both modes under valid Bool/String forged
markers, stale manifests and reruns, preserving every source call. The saved
String chain survives overwrite/delete before the second write, another
read/overwrite/delete before return and final Map release. Native budget probes
complete at **8150/8340/8334/8871**, with **30/32/32/32** cutoffs checked.
Log: `/tmp/ctcompile-saved-gate2.log`. The local mixed gate passes **21
observations and ten refusals**, and seven targeted lit cases pass. Combined
focused CTest passes **12/12 in 29.05 seconds**. The full generated build
succeeds, with **372/372 compiler CTests** and **165/165 lit cases** passing;
overall **512/517** leaves only five existing browser failures. Final full-gate
evidence is in [HANDOFF.md](../HANDOFF.md).
