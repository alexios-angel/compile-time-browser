[Back to escape-load-evidence.md](../escape-load-evidence.md)

## Owning String gate and BigInt Pow review, 2026-09-09

The recovered String-global proof and owning output landed in **0e041bba**
and **4b0a1199**, with execution gates in **b4505df6/417cd0ac**. All five escape
code/test files remain byte-identical to **1138dbfd**. The contents proof derives
original SSA identities on every structural path, independently of native
String types or global observations; calls, global operations, handlers and
unknown effects retain their existing refusals.

The initial full gate at **5bb3a633** builds **246 steps without warnings** and
passes **511/517 CTests in 1817.89 seconds**, including **371/372 compiler
checks**. Lit passes **164/165 in 1096.42 seconds** (CTest **1096.48**). Its sole
failure is a historical String refusal expecting the old load diagnostic;
the unchanged program now refuses its unproved result store. The test-only
correction **9582188d** passes its focused case in **0.08 seconds**. The other
five failures remain `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors`. The complete corrected lit rerun is **pending**. All **23**
initial hashes agree across frozen inputs, devbox, local files and the committed
source; the corrected manifest adds the diagnostic test as a separate 24th input.

All **eight escape CTests pass** in the initial full run. Arrays take **0.43
seconds**, the source fixture **0.32**. Div and Mod each retain **130 rows,
47 stale/fresh states and 4802 cutoffs**. Their source family remains **28
literal sites, 54 instances and 38 retained**, plus **two independent implicit
Errors at pc25**, each without a source allocation claim. All four escape
oracles report **zero violations**: fixture precision **72/109**, Bootstrap
**0/64**, p5 **0/16**, Phaser **0/20**. The fixture retains **540 claims,
546 observed sites and seven unclaimed sites**; p5's single partial claim is
unchanged. The four type oracles also report zero violations.

A separate safe source probe reviews the next Pow boundary. Node and the devbox
interpreter both pass **sixteen common checks, trace=65535**, covering exact
results, saved categories, mixed-input errors and two independently retained
negative-exponent Errors. Four small-base cases above the VM's unsigned-32-bit
exponent cap differ: Node reports **capTrace=15, capErrors=0**; the VM reports
**capTrace=0, capErrors=15**. Only bases **0, 1 and -1** use these large
exponents, so no large result allocation is attempted. The reference prints
three actual Number globals and explicitly skips seven BigInt/object globals.
The cap checks establish four RangeError outcomes; they do not measure those
Errors' identities or retention.

Pow remains outside the retention proof. The next bounded increment must prove
both original BigInt operands, charge its normal result in the existing per-path
category set, and preserve complete publication/call/handler/effect refusals.
Its independent negative and oversized exponent exits need source-backed
retention witnesses. Native BigInt carriers, allocation success, completion and
no-throw contracts remain separate boundaries; the cap divergence stays visible.

Evidence: `/tmp/ctcompile-string-complete-full{,-detail}.log`,
`/tmp/ctcompile-string-complete-full-hashes.json`,
`/tmp/ctcompile-string-audit-full-escape.json`,
`/tmp/ctcompile-string-audit-nextpow-review.json` and
`/tmp/ctcompile-string-nextpow-results.json`. The probe is
`/tmp/ctcompile-string-audit-nextpow-semantics.js`, SHA256
`b6f5dbfe7d30e34c37efaab48183f033fa049b40eb639a9a27fe6115858b4c76`.


**Corrected lit gate, 2026-09-09.** The pending rerun above is complete:
**165/165 lit cases pass in 1096.14 seconds** (CTest **1096.20**, total
**1096.21**), with exit status zero. Independent review verifies all 165 unique
PASS rows and the integrated **302 native Map programs/37 lifetime families**.
All **24** final hashes agree across frozen inputs, sources before and after the
devbox run, local files and HEAD. The initial 23 inputs and all five escape
files remain unchanged; the latter still match **1138dbfd**. Combining the
initial full run with this lit-only rerun gives **372/372 compiler checks** and
**512/517 CTests** with passing results, leaving the five browser failures above.
These are two separate runs; no second full 517-test run is claimed. The escape
results and measured Pow cap divergence above remain unchanged, and Pow
retention/native admission remains future work. Evidence:
`/tmp/ctcompile-string-corrected-lit{,-detail}.log`,
`/tmp/ctcompile-string-corrected-final-hashes.json` and
`/tmp/ctcompile-string-audit-full-escape.json`; its separate `-initial.json`
snapshot preserves the initial full-gate audit.


## Computed BigInt exponentiation, 2026-09-09

Continued the separate Pow boundary recorded after the **164fe8c2** String
checkpoint and the **1138dbfd** Div/Mod increment. Dynamic Pow now accepts two
independently proved original BigInt operands and charges its normal result in
the existing per-path BigInt category set. Saved reads, chained results and
forwarded SSA values retain their original category after stores and deletion.
Both structural paths remain live; a computed result supplies no concrete value,
array index, property key, normal-completion fact or native carrier.

The VM implementation is unchanged. `bigint_pow` in
`ctbrowser/lib/Script/bigint.cpp` checks negative and above-`0xFFFFFFFF` exponents
before computing digits. `bigint_binary` in `Script/vm/coerce.cpp` calls
`give_or_throw`; `make_error` allocates a new Error holding message/stack strings
and a preexisting prototype, without either operand or a local object edge.
The complete frame still refuses publication, calls, explicit throws, handlers
and unknown effects. Consequently an independent early Error cannot retain
unpublished fresh locals. Allocation success, no-throw contracts, normal
completion and native BigInt admission remain unproved. Static Pow, unsigned
shifts, mixed/object/opaque inputs and BigInt String conversion remain refused.

The raw matrix includes dynamic Pow as a producer and a consumer of independently
proved unary/binary BigInts. It checks each operand separately, mixed primitives,
saved contents after overwrite/deletion, separate Number/BigInt path categories,
frame/edge transport, retained children, stale solvers and forged completion
markers. Zero, negative, cap-boundary and wide exponents cannot hide later
publication, calls or unknown effects. Wide snapshots charge both origin and
category entries; every incomplete retention budget keeps its refusal.

Seven new source families measure **28 literal sites, 60 instances, 38 retained
and 22 confined**. The negative and cap helpers each run normally once and throw
twice. Negative Errors appear at **pc26**, independently of literal coordinates
**5/9/13/34**; cap Errors appear at **pc25**, independently of **5/9/13/33**.
All **four implicit Errors** are retained through thrown roots and have no source
allocation claim. Each result literal runs only once, after normal arithmetic;
the two error paths leave both unpublished containers confined. Source assertions
check distinct Error identities, Error tags and saved message/stack categories.

The cap source uses base **1**, so it never attempts a huge result. Node produces
two normal results and zero cap Errors; the VM produces two distinct RangeErrors.
This is the existing divergence, preserved explicitly. Both implementations pass
the negative Error identity and normal-result controls. The historical
`objectFrameArithmeticBinaryBigInt` source is byte-identical and now proves its
child confined: Pow was its final missing category. **357/358** historical
checker rows are unchanged; that exact child is the sole promoted row. Removing
the new fragment restores every byte of the prior fixture.

The warning-free tools build and first focused devbox run pass **4/4 CTests in
126.18 seconds**, including arrays in **0.45 seconds** and the source fixture
in **0.57**. Independent replay of the recorded data reports **zero violations,
partial or pending claims**, with fixture precision **77/115**, **577 claims**,
**585 observed sites** and **nine unclaimed sites**. The added exact-coordinate
checker passes that recording and rejects **38 independent corruptions** of
Errors, claims, literals and source operations. Stable clang-format **22.1.8**
passes all **745 files**; bundled 23 retains the same nine baseline differences.
The checker-enhanced fixture rerun and complete suite are pending at this
checkpoint; no fresh full gate is claimed here.

Evidence: `/tmp/ctcompile-string-fields-focused.log`,
`/tmp/ctcompile-bigint-pow-fixture.{rec,claims}`,
`/tmp/ctcompile-bigint-pow-{frozen,measurement}.json` and
`/tmp/ctcompile-escape-pow-checker-audit/audit.json`.
The next separate escape boundary includes successful String/BigInt Add/Concat:
the existing `objectFrameAddConcatBigInt` source still keeps its Stored claim.
Native BigInt and effect/completion contracts remain independent work.

**Enhanced escape gate, 2026-09-09.** The pending fixture rerun above is now
complete. A warning-free six-step rebuild passes **all eight escape CTests in
8.95 seconds**, including arrays in **0.45**, the strengthened fixture in
**0.34**, and all four corpus-oracle checks. Detailed output records Pow's
**139 rows, 51 stale/fresh live states and 5299 retention budget cutoffs**, plus
one wide snapshot. Div and Mod now each cover **132 rows, 47 live states and
4965 cutoffs**, including their new independently proved Pow operands.
The five code/test hashes remain frozen; the exact source observations and
38-corruption checker audit above are unchanged. The complete suite remains
pending. Evidence: `/tmp/ctcompile-string-fields-precommit-gate.log` and
`/tmp/ctcompile-string-fields-arrays-detail.log`.

**Completed full gate, 2026-09-09.** The pending full run above is complete:
a warning-free **241-step** build passes **512/517 CTests in 1885.14 seconds**,
including **all 372 compiler CTests** and **166/166 lit cases in 1142.24 seconds**
(CTest **1142.32**). Independent audit verifies every CTest result and all 166
unique lit PASS rows. The five failures remain the established browser tests
`selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.

All **eight escape CTests pass**, with arrays in **0.55 seconds** and the
strengthened fixture in **0.38**. Pow retains **139 rows/51 live states/5299
cutoffs**; Div/Mod each retain **132/47/4965**. All four escape and all four type
corpus oracles report **zero violations**. Fixture precision remains **77/115**,
with **577 claims, 585 observed sites and nine unclaimed sites**. The complete
source checker again confirms **28 literal sites/60 instances/38 retained** and
**four independent implicit Errors** at their pinned negative/cap coordinates.
Bootstrap, p5 and Phaser escape precision remains **0/64, 0/16 and 0/20**;
p5's existing single partial claim remains separate.

All **21** final code/test hashes agree across frozen inputs, devbox sources,
local files and HEAD. The five escape inputs still match **927128a0**; the
historical source preservation and 38-corruption checker audit remain unchanged.
This is one completed full run. Evidence:
`/tmp/ctcompile-string-fields-full{,-detail}.log`,
`/tmp/ctcompile-string-fields-full-hashes.json` and the independent
`/tmp/ctcompile-bigint-pow-final-audit.json`. The Pow cap divergence and separate
native BigInt, String/BigInt conversion and effect/completion boundaries above
remain unchanged.


## Successful String/BigInt Add and Concat, 2026-09-09

Continued the explicit next escape boundary in **c8cd482a** and the preceding
Pow checkpoint. Dynamic Concat now accepts independently proved original
primitive operands, including BigInt. Dynamic Add accepts String/BigInt only
when an original operand independently proves String. String literals, TypeOf
and admitted Concat results have their fixed category; an Add result acquires a
separately charged String category only on a path that proved a String input.
Unknown Number/String Add results never borrow that fact. Saved array and own
field reads preserve their original category after overwrite, deletion and
forwarding; independent paths can give one Add SSA result different categories.
No constant value, key, array index, native BigInt carrier or type-lattice fact
is inferred from this retention evidence.

The unchanged VM `Script/vm/coerce.cpp` processes Concat before `bigint_binary`,
converting primitive BigInt digits directly through `to_string`. Add first calls
`to_primitive`, then selects String concatenation before mixed-BigInt errors.
These primitive conversions neither call user code nor retain an input object.
Add's depth guard still permits an independent Error exit. Complete frame
proofs continue refusing publication, calls, handlers, explicit throws and
unknown effects; no allocation-success, normal-completion or native no-throw
contract follows. Symbols, object/opaque coercions, other mixed arithmetic and
mixed String/BigInt comparisons remain separate refused boundaries.

The raw matrix checks both operands and independently produced categories,
String/Number/BigInt path separation, stale solvers, forged completion markers,
saved reads, retained children, frame/edge transport, exact-index refusal and
all incomplete retention budgets. A wide snapshot adds 32 results: String Add
charges 128 units for producers, categories and both copied sets; Concat charges
64 because its String category is intrinsic to the admitted operation.

Eight new source families record **32 literal sites, 64 instances, 50 retained
and 14 confined**. The unchanged `objectFrameAddConcatBigInt` source now proves
its child confined. **385/386 historical checker rows** remain identical; that
child is the sole promoted row. Removing the new fragment restores every byte
of the prior fixture. The checker independently pins all eight source bodies,
32 allocation/observation coordinates and unique source claims. Its replay
passes the baseline and rejects **96 independent corruptions** of allocations,
observations, claims and source operations.

The first focused run passed **7/8 escape CTests in 9.09 seconds**. The sole
failure was in five new/promoted live mutation expectations: changing the shared
zero constant to BigInt/String also changed the later array index. The analysis
correctly refused `UnknownIndex`; the tests now require that exact boundary,
with all source and production bytes unchanged. The source fixture passed in
**0.51 seconds**. Independent recorder replay reports **zero violations,
partial or pending claims**, **617 claims, 625 observed sites and nine unclaimed
sites**, with fixture precision **81/122**, up from **77/115**.

Corrected arrays, strengthened fixture, the typed interpreter probe and the
full suite are pending at this checkpoint; no fresh full gate is claimed.
Local Node passes all **16** exact-fragment typed cases (`stringBigIntTrace=65535`)
and the two separately recorded mixed comparisons (`stringBigIntMixedTrace=3`).
Existing source semantics gaps, including the Pow exponent cap, remain unchanged.
The next independent escape increment is mixed primitive BigInt comparison
retention, starting from the still-Stored `objectFrameStringBigIntMixed` source;
its observed values cannot establish native semantics or effects.

Evidence: `/tmp/ctcompile-map-zero-escape.log`,
`/tmp/ctcompile-string-bigint-{arrays.log,fixture.rec,fixture.claims}`,
`/tmp/ctcompile-string-bigint-{frozen,measurement,node}.json`,
`/tmp/ctcompile-string-bigint-checker-audit/audit.json` and the typed probe
`/tmp/ctcompile-string-bigint-semantics.js`, SHA256
`331cf5104aa9574cd6307d71b903c9a2d727c9107cf79f5ba9139690860f6e05`.


**Corrected focused gate, 2026-09-09.** The pending focused checks and typed
interpreter probe above are complete. A warning-free four-step rebuild passes
**9/9 CTests in 9.11 seconds**, including **all eight escape CTests** and the
independent type-inference test. Arrays pass in **0.47 seconds** and the
strengthened fixture in **0.38**. Add covers **97 rows, 37 stale/fresh live
states and 3850 retention budget cutoffs**; Concat covers **97 rows, 37 live
states and 4104 cutoffs**. Both wide snapshot checks pass. Pow retains **139
rows/51 states/5377 cutoffs**, and Div/Mod each retain **132/47/5043** after
additional valid Concat consumers. The initial **29 failed assertions across
five mutation states** remain in the earlier log; only their exact-index
expectations changed, with no production or source change.

The interpreter prints **stringBigIntTrace=65535** and
**stringBigIntMixedTrace=3**, exactly matching Node for all sixteen typed source
calls and the two separate mixed-comparison observations. Both output globals
are independently reported as Numbers; the object holder and eight function
globals are explicitly skipped. This agreement does not admit the mixed
comparison retention boundary or change known wider comparison differences.
All five code/test inputs match the frozen manifest. The source-coordinate
checker, its **96 rejected corruptions**, historical source preservation and
**81/122 zero-violation** fixture evidence above remain unchanged. Full-suite
validation is still pending. Evidence:
`/tmp/ctcompile-map-zero-corrected.log`,
`/tmp/ctcompile-string-bigint-arrays-corrected.log` and
`/tmp/ctcompile-string-bigint-vm-corrected.log`.


**Completed full gate, 2026-09-09.** The pending full suite above is complete:
a warning-free **241-step** build passes **512/517 CTests in 1926.71 seconds**.
The five failures remain the established browser tests `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors`. The nonzero CTest status is **8**;
no compiler failure or corrective rerun occurred in this complete run.

All **eight escape CTests pass**, including arrays in **0.56 seconds** and the
strengthened fixture in **0.41**. The independently audited detailed output
preserves String/BigInt Add's **97 rows/37 live states/3850 budget cutoffs** and
Concat's **97/37/4104**, with both wide snapshots. Pow retains **139/51/5377**;
Div and Mod each retain **132/47/5043**. The source checker again confirms
**32 literal sites, 64 instances and 50 retained**, alongside the separate
historical Pow, Div/Mod and signed-shift Error witnesses.

All four actual escape corpus oracles report **zero violations**. The fixture
retains **617 claims, 625 observed sites, nine unclaimed sites and 81/122
precision**, with zero partial or pending claims. Bootstrap, p5 and Phaser
remain **0/64, 0/16 and 0/20**; p5's established single partial claim remains
separate. The deliberately wrong oracle self-tests are separate checks and
retain their expected violations.

All **15** final source/test hashes agree across frozen inputs, recorded devbox
sources, local files and HEAD. All **five escape code/test inputs** also match
**e42f2d24**. The historical fixture bytes, source-coordinate assertions and
**96-corruption checker audit** remain unchanged. This is one completed full
suite; the initial focused expectation failures remain preserved above. Mixed
primitive BigInt comparison retention is still the next separate escape
boundary; native BigInt, effect/completion contracts and known broader source
semantics differences remain open. Evidence:
`/tmp/ctcompile-map-zero-full{,-detail}.log`,
`/tmp/ctcompile-map-zero-full-hashes.json` and
`/tmp/ctcompile-string-bigint-full-audit.json`.


## Mixed primitive BigInt comparison retention, 2026-09-09

Continued the explicit next boundary in **d7ebbb73**, its HANDOFF and the
preceding String/BigInt checkpoint. Loose equality and all four relational
kinds now accept independently proved original primitive categories, including
mixed BigInt operands. Saved array and own-field reads preserve their original
categories after replacement, deletion and forwarding. A single Add result may
have Number, String or BigInt origins on independent paths. Object and opaque
origins still refuse, including a path that merely observed primitive actuals.
The analysis proves no comparison value, key, branch liveness or native carrier.

The unchanged VM's `loose_equals` uses digit equality, String parsing or static
numeric conversion for these primitives. Its BigInt/Boolean path also enters
`to_primitive`'s depth guard; every relational kind enters that guard. Primitive
inputs return before property lookup or a user callback. The guard can produce
an independent Error, which cannot retain this whole-frame query's unpublished
fresh locals. Calls, handlers, explicit throws and publication remain refused.
No normal-completion, allocation-success or no-throw/effect contract follows.

The raw matrix checks both operand positions, six original primitive categories,
computed producers, three independently visited result categories, saved values,
retained children, stale/fresh solvers, forged completion markers and all
incomplete retention budgets. Equality passes **92 rows, 50 live states and
5206 budget cutoffs**; each relational kind passes **100 rows, 52 states and
5498 cutoffs**. Every wide snapshot checks the exact additional **64** work units
for 32 independent results and their copied origins. Existing ordinary primitive
comparison matrices pass **84 rows, 34 states and 3778 cutoffs** per kind.

Eight new source families record **32 literal sites, 64 instances, 14 confined
and 50 retained**. Five new child sites are proved confined; opaque, object and
separately retained-child controls preserve their conservative claims. Exactly
ten historical child claims advance from Stored to confined: the original
`objectFrameLooseEqualityBigInt`, the Equality/Relational/Unary/Binary/Static/
Shift/DivMod/Pow Mixed families and `objectFrameStringBigIntMixed`. Their complete
recorded lifetime observations remain identical. Removing the new fragment
restores all **76,401** prior fixture bytes, including historical comments.
Every other historical checker row remains unchanged. New source-body hashes,
32 independent allocation/observation coordinates and unique matching claims
pass strict replay; **96 independent evidence/source corruptions** reject.

The initial focused run passed **7/8 escape CTests in 10.44 seconds**, with arrays
passing in **0.53**. Its sole failure was the historical loose-equality BigInt
child's old Stored expectation. The independent recording audit reconciled all
ten promotions, without changing production, raw test or source bytes. The
strengthened focused gate now passes **8/8 in 9.35 seconds**, including arrays in
**0.52** and the fixture in **0.43**. Fixture precision advances **81/122 ->
96/129**, with **657 claims, 665 observed sites, nine unclaimed sites and zero
violations, partial or pending claims**. Bootstrap/p5/Phaser remain **0/64,
0/16 and 0/20** with zero violations; p5 retains its established single partial.
Stable clang-format 22 passes all **745** files; bundled 23 retains the same nine
baseline differences. Full-suite validation is pending at this checkpoint.

A separate exact-fragment probe makes **16 calls** and observes **68 Boolean
globals** in both Node and the interpreter. **64 agree**. Both
`"9007199254740993" < 9007199254740993n` and the reversed `>` return false in
Node but true in the VM; `false == 0n` and `1n == true` return true in Node but
false in the VM. The original String-to-Number rounding and Boolean equality
differences remain unchanged. Sixteen independent comparison/deletion source
mutations alter the observed controls. These observations supply no native
comparison semantics; retention remains sound because every structural arm is
checked and primitive comparison results carry no input object identity.

The next separate retention boundary is a known BigInt operand's unary Plus
TypeError. Existing raw Plus refusal rows remain. The source probe
`/tmp/ctcompile-after-bigint-comparison-boundary.js` checks a known BigInt/Number
branch, a separately retained child and opaque actuals: three functions, six
calls, Node Number `plusEarlyTrace=63`. Its VM/escape measurements are pending;
no further admission or completion/effect proof is claimed.

Evidence: `/tmp/ctcompile-map-one-escape{,-corrected}.log`,
`/tmp/ctcompile-bigint-comparison-{fixture.rec,fixture.claims,measurement.json}`,
`/tmp/ctcompile-bigint-comparison-checker-audit/audit.json`,
`/tmp/ctcompile-bigint-comparison-{preservation,node,vm-audit,frozen}.json` and
`/tmp/ctcompile-bigint-comparison-semantics.js`, SHA256
`427127f50e7d55a1359ce30d8fec25877235df11894e4886ca7c0044620b719f`.


**Measured next unary Plus boundary, 2026-09-10.** The pending probe above now
agrees between Node and the interpreter: all six calls give Number
`plusEarlyTrace=63`, with three normal results and three TypeErrors. Its source
SHA256 is `9f6436a7abe84092242955d03af31f12730b4db4fccd72f1d1d8606fdd8ebb55`.
The recorder measures **12 literal sites, 21 instances, 11 confined and ten
retained**, plus **three independent Errors** rooted through `thrown:1` at
pcs **24, 28 and 17**, respectively. Those Error coordinates have neither
source allocations nor compiler claims.

The known-primitive `primitivePlusEarly` child is confined on both calls;
`primitivePlusRetained` keeps its child on the normal call and releases it on
the throwing call. `primitivePlusOpaque` also observes two confined children,
while its unknown future argument remains outside the proof. All three
functions still claim their nine child/container sites Stored and their three
result objects Returned: **zero confined claims**. The top-level driver alone
is unimported because it contains more than one protected region; each tested
function imports and records two entries. This identifies the next bounded
retention increment, without changing Plus admission or completion/effect
contracts. The five comparison code/test hashes remain identical to
**51018c86**. Full-suite validation remains pending. Evidence:
`/tmp/ctcompile-after-bigint-comparison-boundary-{vm,oracle}.log`, the matching
`.rec` and `.claims`, and `-audit.json`.


**Mixed primitive BigInt comparisons: full-gate completion, 2026-09-10.**
The committed inputs at **39f4eddc** complete the shared devbox build in
**246 steps with zero warnings**. The single full CTest run passes **512/517
in 2138.23 seconds**; status **8** records only the established `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors` browser failures.
All **372 compiler CTests** pass, including **166/166 unique lit cases** in
**1326.24 seconds** (CTest **1326.42**).

All eight escape analysis/claims CTests pass, including arrays in **0.54** and
the fixture in **0.49 seconds**. The full raw output confirms equality's
**92 rows / 50 live states / 5206 budget cutoffs**, each relational kind's
**100 / 52 / 5498**, and ordinary primitive comparisons' **84 / 34 / 3778**.
The four escape oracles report zero violations: fixture **96/129**, Bootstrap
**0/64**, p5 **0/16** and Phaser **0/20**. Fixture counts remain **657 claims,
665 observed sites and nine unclaimed sites**, with no partial or pending
claims; p5 retains its established single partial. The five type oracles also
report zero violations.

All **15 code/test hashes** agree across the final frozen manifests, devbox,
local files and committed tree. The five escape inputs equal **51018c86**;
all prior fixture bytes and the measured **96 checker-corruption rejections**
remain preserved. The first **7/8** focused result and its ten justified old
claim corrections remain recorded above. The separate typed probe's four
known VM comparison differences remain unchanged; they supply no comparison
value, effect, completion or native BigInt contract. The measured unary Plus
TypeError case remains the next retention boundary, with zero confined claims
in its twelve-site probe and all existing Plus refusal tests unchanged.

Evidence: `/tmp/ctcompile-map-one-full{,-detail}.log`, `.status`,
`/tmp/ctcompile-map-one-full-hashes.json`,
`/tmp/ctcompile-bigint-comparison-full-audit.json` and
`/tmp/ctcompile-bigint-comparison-full-evidence-frozen.json`.


**Known BigInt unary Plus: resumed and gated, 2026-09-10 (`61fe4e2d`).**
The interrupted five-file increment from the **01:27:51 / 01:37:36 UTC**
journals is complete. Plus accepts an independently proved original BigInt
only for this whole-frame retention query: its TypeError has no edge to
unpublished fresh locals. Every structural continuation remains checked;
unknown original operands, calls, handlers and publication remain refused.
The independent Number carrier does not establish successful completion,
no-throw effects or native BigInt admission.

Raw tests pass **24 rows, 17 stale/fresh live states and 1461 incomplete
retention budgets**, including exact completion and the wide snapshot's
additional **64 work units**. Original source/IR cases remain intact. The
three previously measured function bodies are preserved byte-for-byte;
the fixture retains all **81,403** historical bytes outside its new fragment.

The source family measures **12 literal sites, 21 instances, 11 confined
and ten retained**, plus three independent thrown Errors at pcs **24, 28
and 17**, without source allocation claims. The early child's two instances
are now proved confined; the saved-child and opaque-argument controls retain
their conservative claims. Local Node observes Number `primitivePlusTrace=63`;
the interpreter executes the same exact assertion in the fixture gate.
The strict checker passes its recorded-data replay and rejects **60 independent
source/evidence corruptions**, including Error coordinates, routes, retention,
forged claims and invented source allocations.

The resumed build first failed at the new test's mixed `TypedValue`/`Value`
initializer list. Two explicit `mlir::Value` conversions repair the C++ syntax,
with production and JavaScript unchanged. The corrected devbox build completes
**19 steps without warnings**; all **14/14 focused CTests pass in 231.66
seconds**, including all eight escape tests (arrays **0.52**, fixture **0.68**).
Fixture precision is **97/131**, with **673 claims, 684 observed sites,
12 unclaimed sites and zero violations, partial or pending claims**. This
adds source coverage to the preceding **96/129** fixture. The three corpus
precisions remain **0/64, 0/16 and 0/20**; all four escape oracles report zero
violations, with p5's existing single partial unchanged. Stable clang-format
**22.1.8 passes all 745 files**; bundled 23 retains nine baseline differences.
Full-suite validation is pending at this checkpoint.

Evidence: `/tmp/ctcompile-delete-resume-build.log`,
`/tmp/ctcompile-delete-finish-{build,proofs}.log`,
`/tmp/ctcompile-bigint-plus-fixture.{rec,claims}`,
`/tmp/ctcompile-bigint-plus-checker-audit/audit.json`, and
`/tmp/ctcompile-bigint-plus-code-{initial-frozen,frozen}.json`.


**Measured next boundary: mixed BigInt/Number subtraction, 2026-09-10.**
The unchanged `/tmp/ctcompile-after-bigint-plus-boundary.js` (SHA256
`c71cb43f25792dc118b3c9bfeeeac0b38e3c219f83d0eee57a1067bd91092bd8`)
uses `input - 0` in known Number/BigInt, saved-child and opaque-argument
functions. All six calls agree between Node and the interpreter with Number
`mixedSubTrace=63`: three normal returns and three TypeErrors. Its **12 literal
sites / 21 instances / 11 confined / ten retained** and independent thrown
Errors at pcs **25, 29 and 18** mirror the lifetime boundary above. All twelve
compiler claims remain conservative (nine Stored, three Returned); the early
child's two confined observations do not yet produce a proof. Only the
multiple-handler driver is unimported; each measured function imports and runs
twice. `bigint_binary` rejects mixed kinds before user conversion and supplies
an independent Undefined carrier on failure. A future retention increment must
prove both original categories and inspect every structural continuation;
observed opaque actuals, result values, completion and native effects remain
separate obligations. Evidence: the matching `.log`, `.rec` and `.claims`, plus
`/tmp/ctcompile-after-bigint-plus-boundary-node.json`. No further implementation
or full-suite completion is claimed here.


**Unary Plus full-gate completion, 2026-09-10.** The final devbox build completes
**241 steps without warnings**; the single full run passes **512/517 CTests in
2102.19 seconds**, including all **372 compiler tests** and **166/166 lit cases
in 1306.16 seconds** (CTest **1306.24**). Only the five established browser
failures remain, with byte-identical output. All eight escape tests pass again
(arrays **0.53**, fixture **0.45 seconds**): Plus **24 rows / 17 live states /
1461 budget cutoffs**, fixture **673 claims / 684 observed / 12 unclaimed**,
precision **97/131**, and zero violations, partial or pending claims. All four
escape oracles report zero violations; corpus precision and p5's existing
partial remain unchanged. All **14 code/test hashes** match frozen inputs,
local files, committed code and the devbox. The initial compile failure,
**60 rejected checker corruptions**, historical source bytes and measured mixed
subtraction boundary remain preserved. Evidence:
`/tmp/ctcompile-delete-finish-full{,-detail}.log`,
`/tmp/ctcompile-delete-finish-devbox-hashes.json` and
`/tmp/ctcompile-bigint-plus-full-audit.json`.


**Mixed BigInt subtraction retention, 2026-09-10 (`a43858ac`).**
Resumed the exact **c71cb43f** boundary above. Dynamic Sub now accepts
independently proved original primitive categories when exactly one is BigInt.
`bigint_binary` rejects the pair before lookup or user conversion; its TypeError
cannot retain unpublished fresh locals. The VM's independent **Undefined**
failure carrier supplies neither a BigInt/Number value nor normal completion.
Every structural continuation remains checked; calls, handlers, publication,
object/opaque operands and other mixed arithmetic stay conservative. No native
BigInt carrier or effect permission follows.

The corrected devbox gate passes **9/9 CTests in 10.41 seconds**, comprising
type inference and all eight escape checks (arrays **0.54**, fixture **0.50
seconds**), after a **four-step warning-free build**. Mixed Sub passes **57 rows,
43 stale/fresh live states and 3328 incomplete retention budgets**, including
the wide snapshot's additional **64 work units**. The first **13-step
warning-free build / 7-of-8 escape gate in 11.09 seconds** is preserved: ten
historical comparison-then-Sub cases still expected refusal. Their exact IR
bodies now assert all three structural paths; ten separate mixed-Mul cases
retain the remaining-operation refusal. Corrected comparison coverage is
**94 Eq rows / 5374 cutoffs** and **102 rows / 5666 cutoffs** per relational kind.

The three **c71cb43f** function bodies are byte-identical in the expanded
fixture. All **84,197 historical UTF-8 bytes** remain; **165 named functions**
retain identical recording headers, **510 literal allocations, 516 observations
and 505 claims**. New coverage has **12 literal sites / 21 instances / 11
confined / ten retained**, plus three independent thrown Errors at pcs **25,
29 and 18** with no literal claims. The early child's two instances are proved
confined; saved-child and opaque-argument claims remain conservative. Node and
the interpreter execute Number trace **63**, including three TypeErrors.
The strict recorded-data replay passes and rejects **60 independent source,
literal, claim and Error corruptions**.

Fixture precision is **98/133**, with **689 claims, 703 observed sites, 15
unclaimed and zero violations, partial or pending claims**. This adds coverage
to the preceding **97/131** fixture. All four escape oracles report zero
violations; corpus precision remains **0/64, 0/16 and 0/20**, with p5's existing
partial unchanged. The five frozen code/test hashes match local and committed
inputs. Stable clang-format **22.1.8** and whitespace checks pass. Full-suite
validation remains pending at this checkpoint.

Evidence: `/tmp/ctcompile-map-join-escape.log`,
`/tmp/ctcompile-map-join-corrected-proofs.log`,
`/tmp/ctcompile-mixed-sub-fixture.{rec,claims}`,
`/tmp/ctcompile-mixed-sub-fixture-audit.json`,
`/tmp/ctcompile-mixed-sub-checker-audit/audit.json`, and
`/tmp/ctcompile-mixed-sub-code-{initial-frozen,frozen}.json`.


**Measured next boundary: mixed BigInt multiplication, 2026-09-10.**
The exact `/tmp/ctcompile-after-mixed-sub-boundary.js` (SHA256
`d5a61e1a505a0302c8f6ad0d2652e7164bdb0f654bc6df215e56db02417276ad`)
uses `input * 1` in known Number/BigInt, saved-child and opaque-argument
functions. All six calls agree between Node and the interpreter with Number
`mixedMulTrace=63`: three normal returns and three TypeErrors. The recorder
measures **12 literal sites / 21 instances / 11 confined / ten retained**, plus
three independent Errors at pcs **25, 29 and 18** without source claims. All
**twelve compiler claims remain conservative** (nine Stored, three Returned),
including the early child's two confined observations. Each measured function
imports and runs twice; only the multiple-handler driver is unimported.
The next proof must independently establish both original operand categories
and preserve every structural continuation and the VM's Undefined error carrier;
observed opaque actuals authorize no future category, completion or native effect.
Evidence: the matching `.log`, `.rec`, `.claims`, `-node.json` and `-audit.json`.
All five committed subtraction code/test hashes remain unchanged; full-suite
validation is still pending at this checkpoint.

**Mixed subtraction full-gate completion, 2026-09-10.** The final devbox build
completes **241 steps without warnings**. One complete run passes **512/517
CTests in 2302.35 seconds**, including **all 372 compiler checks** and **166/166
lit cases in 1493.74 seconds** (CTest **1493.83**). The five established browser
failures (`selectors`, `frames`, `element_attrs`, `vm_async`, `early_errors`)
have byte-identical output to the preceding complete run.

All eight focused escape tests pass in that full run: arrays **0.53 seconds**,
fixture **0.49 seconds**. Mixed Sub repeats **57 rows, 43 live states and 3328
budget cutoffs**; the source checker confirms **12 literal sites, 21 instances,
10 retained and three independent Errors**. All four escape oracles report
zero violations. Fixture precision remains **98/133**, with **689 claims,
703 observed sites and 15 unclaimed sites**, zero partials and zero pending.
The three corpus oracle summaries match the preceding full run: precision
**0/64, 0/16 and 0/20**, including p5's existing single partial.

All fourteen final code/test hashes match frozen, local, committed and devbox
inputs; the five escape files remain exactly **a43858ac**. The strict checker
baseline and all **60 corruption rejections** independently match the current
checker. Earlier failure/correction evidence and the measured next mixed-Mul
boundary above are preserved. Evidence: `/tmp/ctcompile-mixed-sub-full-audit.json`,
`/tmp/ctcompile-map-join-full.log` and `/tmp/ctcompile-map-join-full-detail.log`.


**Mixed BigInt multiplication retention, 2026-09-10 (`0b25968b`).**
Resumed the exact **d5a61e1a** boundary from the interrupted **04:27:51 UTC**
journal, explicitly abandoned at **04:30:18**. Dynamic Mul now shares Sub's
independent original-operand proof: exactly one known BigInt and one known
non-BigInt primitive reach `bigint_binary`'s independent TypeError before
lookup or user conversion. Its Undefined failure carrier supplies neither
BigInt nor Number value evidence. Every structural continuation remains checked;
publication, calls, handlers and object/opaque operands remain conservative.
No normal-completion, native carrier or effect permission follows.

The corrected **16-step warning-free build** passes **9/9 CTests in 10.86
seconds**, comprising type inference and all eight escape checks (arrays
**0.66**, fixture **0.62 seconds**). Mul passes **57 rows, 43 stale/fresh live
states and 3369 incomplete retention budgets**, including exact completion and
the wide snapshot's additional **64 work units**. Primitive Mul's shared matrix
also passes **84 rows, 49 live states and 4378 cutoffs**. The initial **nine-step
warning-free build / 7-of-8 escape gate in 10.45 seconds** exposed historical
Mul refusal expectations; their source and operand order remain unchanged.
Only independently justified expectations advance, including the original
Sub-to-Mul live mutation and ten comparison-then-Mul paths. Ten separate Div
cases retain the remaining-operation refusal. All **1511 historical IR string
tokens** are preserved outside the new test cases.

The three **d5a61e1a** function bodies are byte-identical in the fixture. All
**87,049 historical UTF-8 bytes** remain; **169 named functions** retain identical
recording headers, **522 literal allocations, 531 observations and 517 claims**.
New coverage measures **12 literal sites / 21 instances / 11 confined / ten
retained**, plus three independent thrown Errors at pcs **25, 29 and 18** with
no literal claims. The early child's two instances are proved confined;
saved-child and opaque-argument claims remain conservative. Node and the
interpreter execute Number trace **63**, including three distinct TypeErrors.
Seven source mutations distinguish the observations. The strict recorded-data
checker passes its baseline and rejects **60 independent source, literal,
claim and Error corruptions**.

Fixture precision is **99/135**, with **705 claims, 722 observed sites,
18 unclaimed and zero violations, partial or pending claims**. All four escape
oracles report zero violations; corpus precision remains **0/64, 0/16 and
0/20**, with p5's existing partial unchanged. All five frozen code/test hashes
match local files and committed **0b25968b**. Stable clang-format **22.1.8
passes all 745 files**; whitespace checks pass. Full-suite validation remains
pending at this checkpoint.

Evidence: `/tmp/ctcompile-mutation-{initial-build,initial-escape,focused-build,type-escape}.log`,
`/tmp/ctcompile-mixed-mul-fixture.{rec,claims}`,
`/tmp/ctcompile-mixed-mul-fixture-audit.json`,
`/tmp/ctcompile-mixed-mul-checker-audit/audit.json`,
`/tmp/ctcompile-mixed-mul-local-preservation.json` and
`/tmp/ctcompile-mixed-mul-code-{initial-frozen,frozen}.json`.


**Measured next boundary: mixed BigInt division, 2026-09-10.** The unchanged
`/tmp/ctcompile-after-mixed-mul-boundary.js` (SHA256 **8a20a9a8**) uses
`input / 1` in known Number/BigInt, saved-child and opaque-argument functions.
All six calls agree between Node and the interpreter with Number
`mixedDivTrace=63`: three normal returns and three TypeErrors. Its **12 literal
sites / 21 instances / 11 confined / ten retained** and three independent
thrown Errors at pcs **25, 29 and 18** have **twelve conservative compiler
claims** (nine Stored, three Returned); the Errors have no literal claims.
Each measured function imports and runs twice; only the multiple-handler
driver is unimported. Continue original-operand category proofs while preserving
every structural continuation and the independent Undefined error carrier;
observed opaque arguments provide no completion or native effect permission.
Evidence: the matching `.log`, `.rec`, `.claims`, `-node.json` and `-audit.json`.
All five multiplication code/test hashes remain frozen; the full suite is still
pending at this checkpoint.


**Mixed multiplication full-gate recovery, 2026-09-10.** The pending
**a1ce9b02** gate was resumed before new implementation. The predecessor's
recorded build check reports **241 steps and zero warnings**. Its recovered
CTest log and checkpoint contain **158/163 passes**, including all eighteen
compiler tests in that prefix and the five established browser failures:
`selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.
The original lit stdout reports **166/166 in 1423.29 seconds**, but its CTest
completion was interrupted. The unchanged rebuild reports no work; `ctest -F`
then passes the remaining **354/354 in 2262.99 seconds**, including a complete
lit rerun: **166/166 in 1486.64 seconds** (CTest **1486.85**). Together these
runs validate **512/517 tests**, all **372 compiler checks** and **140/145**
browser checks. This is combined recovery evidence; no single full-run elapsed
time was measured.

All eight escape checks pass in the resumed run; arrays take **0.64 seconds**
and the fixture **0.53**. Mixed Mul repeats **57 rows, 43 live states and 3369
retention cutoffs**. The source checker confirms **12 literal sites, 21 instances,
ten retained and three independent Errors**. Fixture precision remains
**99/135**, with **705 claims, 722 observed sites, eighteen unclaimed and zero
violations, partial or pending claims**. All four actual escape oracles report
zero violations; corpus precision remains **0/64, 0/16 and 0/20**, with p5's
existing single partial unchanged.

All fourteen code/test hashes match the committed, frozen, local and devbox
inputs; the five escape files remain exactly **0b25968b**. The resumed artifact
census verifies **413 native programs, 1652 fresh GCC/Clang executables and 86
sanitized binaries across 43 lifetime families**. Stable clang-format **22.1.8**
passes all **745 files**. The earlier focused results, failure/correction record
and measured next Div boundary above remain preserved.

Evidence: `/tmp/ctcompile-mul-recovery-audit.json`,
`/tmp/ctcompile-recovered-gate/Temporary/LastTest.log.tmp`,
`/tmp/ctcompile-data-resume-full.log` and
`/tmp/ctcompile-data-resume-full-detail.log`.


## Mixed primitive BigInt Div recovery, 2026-09-10

Resumed the three dirty files left by the **15:44:36 UTC** journal at
**183a10c0**. **c20f43a4** extends the existing Sub/Mul proof to dynamic Div
with exactly one independently known original BigInt and one non-BigInt
primitive. Its independent TypeError cannot retain unpublished local objects;
the proof still checks every structural continuation. It grants no successful
completion, native BigInt representation, or general effect permission.

The shared Mul/Div matrix checks **57 rows, 43 live states and 3410 Div budget
cutoffs**, plus a wide snapshot. Ten historical comparison/Div sources retain
their bytes and now independently prove retention; ten separate Mod controls
remain refused. The exact **8a20a9a8** witness bodies enter the fixture unchanged.
All **89,904 preceding fixture bytes and 172 named functions** remain intact.
Node observes Number **63**, with seven distinguishing mutations checked.

The complete **263-step build is warning-free**. Eight focused escape CTests
pass **8/8 in 9.22 seconds**. The strict fixture records **721 claims, 741
observed sites, 21 unclaimed and 100/137 precision**, with zero soundness
violations, partial or pending claims. Div has twelve literal sites, twenty-one
instances and ten retained objects; its three independent Errors have no source
allocation claims. All four corpus checks pass; p5 retains its recorded partial
observation. Artifacts: `/tmp/ctcompile-object-resume-{build-all,escape}.log` and
`/tmp/ctcompile-div-local-preservation.json`.

The first targeted run used an old linked claims helper and retained ten stale
Div refusal expectations. Rebuilding all targets and promoting those exact
sources resolves both; no runtime behavior or historical source was changed.
The fresh complete gate at **a129764d** now passes **512/517 CTests in
2227.17 seconds**, including all **372 compiler tests** and **166/166 lit cases**.
The five browser failures exactly match the previous diagnostic bodies. The
full-run rebuild reports no work; the 263-step build above is separate evidence.
Arrays pass in **0.56 seconds**, repeating all Div rows/states/cutoffs; the
fixture passes in **0.54 seconds**, retaining **721 claims, 741 observed sites,
21 unclaimed, zero violations/partial/pending and 100/137 precision**. All four
actual corpus oracles retain zero violations, including p5's prior partial.
All fourteen committed/local/devbox code/test hashes match the frozen manifest.
The fresh native census verifies **417 programs, 1668 GCC/Clang executables and
88 sanitized binaries across 44 lifetime families**. Eight new object-argument
C++ variants retain the actual calls and allocations; their sixteen ordinary
and two sanitized executables have no Script/AOT symbols. Census and hashes:
`/tmp/ctcompile-object-resume-audit.json`. Full logs:
`/tmp/ctcompile-object-resume-full.log` and
`/tmp/ctcompile-object-resume-full-detail.log`.
