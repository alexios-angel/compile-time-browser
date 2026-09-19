[Back to escape-load-evidence.md](../escape-load-evidence.md)

## Exact BigInt-pair equality recovery, 2026-09-09

This continues the interrupted **00:26:15/00:27:41 synchronization journal**
and its inherited compiler/test diff after the **00:35:04 loop failure**.
The preceding `267545cd` work left BigInt producer categories refused. This
slice adds only `CompareKind::Eq` with **two independently proved original
BigInt constants**. The live VM's `loose_equals` compares both BigInt digit
values before entering any conversion path. Saved array/own-field reads and
acyclic edge operands keep their original values through later overwrites
and deletion. The result is an independent Boolean origin; its truth value,
literal key, structural liveness and native effect contract are not inferred.
Opaque, mixed-category and computed BigInt inputs and every BigInt relational
comparison remain refused. Calls, handlers, publication and unknown effects
still invalidate the complete frame proof. No runtime source changed.

Before the interrupted production edit, the parent's serialized VM reference
and Node agreed at **1023 on the first ten semantic checks**. They distinguish
equal/different BigInts, unequal values beyond Number precision, saved operand
values after overwrite/deletion, branch-selected constants and mixed Number
reference controls. The complete probe gives **Node 4095 versus VM 1023**:
the two remaining checks expose object-to-BigInt loose equality skipping an
object's `valueOf` and thrown value in the current VM. Those inputs remain
outside the proof. This is an unresolved runtime differential, not a passing
native/VM comparison. Evidence:
`/tmp/ctcompile-escape-bigint-equality-semantics.js` and
`/tmp/ctcompile-map-absence-build1.log`.

The additive unit table passes **52 rows, 30 live mutation states and 2,280
retention budget cutoffs**, plus a 32-result snapshot requiring exactly **64
extra work units**. It checks each incoming/saved operand independently,
both structural overwrite arms,
returned saved children, result forwarding/rooting, nonliteral keys and late
effects. Each operand, original constant and comparison kind is mutated under
forged completion/confinement reports. Every incomplete contents/retention
budget publishes no proof and preserves original escape verdicts. The
unchanged five primitive comparison kinds each repeat **84 rows, 34 live states
and 3,527 retention cutoffs**; all seven dynamic binary kinds repeat
**84/49/4,127**.

Four added source functions measure **16 sites, 32 instances and 26
retained**: independently saved equal/different BigInt operands, opaque
actuals, mixed Number inputs and a separately returned child. The source
oracle joins exact program/function/pc coordinates; only the first family's
child gains a new confinement claim. All historical JavaScript bytes and
existing oracle-family expectations remain unchanged. Expanded-fixture
precision is **53/81**, with **zero violations, partial or pending claims**.
This adds one proved-confined and three observed-confined sites to the preceding
**52/78**; it measures added source coverage, with no historical/corpus precision
improvement claimed. Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**, including
p5's existing single partial observation. All four escape oracles report
**zero soundness violations**, and every historical family count passes.

Local Node checks **87 combined fixture calls** and **34 new discriminating
observation mutations**, including exact saved bytes, equality/inequality,
source/target identity and independent retained children. Removing the added
source block reproduces the pre-increment committed fixture byte-for-byte (SHA256
`ae2fa6b5af14db567f69146899a72fb40d628d28e6c9afa19385d544c3aa484f`).
Evidence: `/tmp/ctcompile-escape-bigint-equality-node.{py,js,json}`.

After correcting unrelated new Map-query test const wrappers, the focused
**13-step rebuild passes with zero warnings** and **all eight escape CTests
pass**. The array executable takes **0.23 seconds**, including the new BigInt
rows, and the fixture oracle takes **0.19 seconds**. The surrounding focused
run finishes **19/20 in 96.87 seconds**; its sole failure is the separate
`ctcompile_host_contract_seeded_maps` test. The corrected affected-test rerun
passes **2/2 in 91.56 seconds** after retaining when-present Map payload evidence
across exact deletions and updating precise Undefined/fallback assertions with
the original source literals intact. All 20 focused tests therefore have passing
results across those two runs.
The formatter using **22.1.8 passes all 745 files**; changed-path whitespace
checks pass. All five code/test paths remain byte-identical to the pre-gate
frozen hashes. These measurements come from
`/tmp/ctcompile-absence-recovery-{build2.log,focused.log,format22.log}` and
`/tmp/ctcompile-escape-bigint-recovery-frozen.json`.

The BigInt increment is committed as **`ca219c28`**. The first full generated
devbox build completes **250 steps with zero warnings**. CTest finishes
**511/517 in 1339.11 seconds**: **371/372 compiler** and **140/145 browser**
tests pass. Lit passes **164/165 in 672.49 seconds** (CTest **672.69 seconds**).
Its sole failing case is the published Map driver: the historical `seeded_deleted`
control still expects absent ownership, while exact deletion now proves an
Undefined result and complete ownership. Native admission remains **0/5** at
the separate optional numeric Map-key carrier boundary. The five browser
failures remain `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors`. This first full run is **not a complete compiler pass**.

All eight escape CTests pass again in that full run. BigInt repeats **52 rows,
30 live states, 2,280 retention cutoffs** and the **64-work** snapshot; the
five primitive comparison and seven dynamic binary families retain **84/34/3,527**
and **84/49/4,127** respectively. The four escape oracles again report **zero
soundness violations**. Fixture precision stays **53/81**, zero partial/pending,
with exact **16 sites/32 instances/26 retained** in the BigInt family and every
historical source-family count unchanged. Bootstrap/p5/Phaser remain **0/64,
0/16, 0/20**, including p5's existing one partial observation. Native corpus
counts remain **19/574, 39/4754, 45/7725** in both modes; exact Data remains
**0/7 browser/CommonJS and 0/8 AMD**. These are separate from escape coverage.

All **five escape code/test hashes** independently match the original frozen
input, committed HEAD and full-gate devbox source. The parent's complete
snapshot comparison also reports all **twelve code/test paths** matching its
frozen devbox input. Both actual generated absence/lifetime C++ artifacts have
no Script/VM context/value or AOT symbols; the method retains its allocation,
numeric field write, Map set/delete/get and live strict comparison. In the saved
case the later reseed remains after the actual read, preserving the saved value.

An independent survey of historical driver refusals identifies eleven controls
that now have complete ownership but still refuse unsupported native carriers,
plus one former refusal that now admits **6/6**. **`4eb2390d`** corrects only
the test classifications, preserving their JavaScript and compiler production.
The focused correction passes all eleven carrier families in both optimization
modes, with exact repairs, live producer/consumer/capture operands, fresh/stale
forgeries and prepared reruns. The unchanged ten-call empty-String deletion
program passes explicit/deduced GCC/Clang execution and future Undefined,
empty-String and distinct-key observations. Its additional positive increases
the integrated driver from 193 to **194 programs**, with **27 lifetime families**.

The complete corrected `ctcompile_lit` rerun passes **165/165 in 709.94 seconds**
(CTest **710.00 seconds**, total command **710.01 seconds**, exit **0**), including
the complete published Map driver. All **372 compiler tests now have passing
results across the initial full run and corrected lit rerun**. This is not a
second complete 517-test run; the five established browser failures remain.
The BigInt implementation and all five escape code/test hashes are unchanged
through the test-only correction. Escape precision and the separate refused
object-to-BigInt runtime differential above remain as measured.
The final corrected **twelve code/test hashes** independently match local files,
committed HEAD, the corrected frozen input and the devbox after the successful
rerun, including all three updated Python test files.

Evidence:
`/tmp/ctcompile-absence-recovery-{full.log,full-detail.log,evidence.json,remote-hashes.log}`,
`-final-absence.cpp` and `-final-lifetime.cpp`,
`/tmp/ctcompile-absence-carrier-focused.log`, and
`/tmp/ctcompile-absence-corrected-lit{.log,-detail.log,-exit.txt}`,
`/tmp/ctcompile-absence-corrected-{frozen.json,remote-hashes.log}`.

Remaining boundaries include mixed/computed BigInt conversions and arithmetic,
other primitive conversions, loops, callee summaries and native lifetime/effect
consumers. This proof introduces no native BigInt carrier or Script dependency.

## Exact BigInt-pair relational origins, 2026-09-09

This continues the original BigInt producer boundary in **`ca219c28`** and
its completed **02:13:49 synchronization journal**. `Lt`, `Le`, `Gt` and `Ge`
now accept two independently proved original BigInt constants, reusing the
existing saved-value and acyclic edge provenance. Neither operand can borrow
the other's proof or a later slot value. Mixed, opaque and computed BigInt
operands remain refused; the result contributes only an independent Boolean
origin. This introduces no native BigInt carrier, native admission or runtime
source change.

The current VM's `compare_relational` calls `to_primitive` on each operand,
then compares exact BigInt digits. `to_primitive` returns actual BigInt inputs
before looking up user methods, but still enters its recursion-depth guard.
The same retention argument as primitive relational comparisons therefore
applies: an unrelated guard error cannot retain this complete frame query's
unpublished fresh locals. Calls, handlers, publication and unknown effects
still invalidate the proof. Normal completion, no-throw behavior, allocation
success, truth values, literal keys and structural liveness are not proved.

Before production changes, the parent's serialized devbox VM and Node agree
at **65535 on all sixteen semantic checks**. These distinguish all four
operators, equality and strict ordering, exact values beyond Number precision,
zero, saved values after overwrite/deletion and branch-selected constants.
A separate excluded-input probe gives **Node 15 versus VM 8**: mixed String
comparisons beyond Number precision and an object's `valueOf` returning a
BigInt disagree; its throwing-object check agrees. Those inputs remain refused.
The previous object-to-BigInt equality discrepancy remains separately recorded
above. Evidence: `/tmp/ctcompile-escape-bigint-relational-semantics.js`,
`-refused-semantics.js` and `/tmp/ctcompile-map-clear-semantic.log`.

The shared table retains the historical Eq rows and runs all four relational
kinds independently. Each new kind adds computed-input refusals and saved
BigInt reads across Number overwrite/deletion. Live operand, original constant,
comparison-kind and structural overwrite mutations run under forged proof
reports; every incomplete contents/retention budget must publish no proof and
preserve the original escape verdicts. Each kind has a separate 32-result
snapshot requiring exactly 64 extra work units. The first devbox gate passes
**60 rows, 32 live states and 2684 retention budget cutoffs per relational
kind**. The historical Eq table keeps **52 rows and 30 live states**, with
**2448 cutoffs** now that its four relational-kind mutations complete. The
five primitive comparison kinds retain **84/34/3527**, and the seven dynamic
binary kinds retain **84/49/4127**. The array executable passes in **0.26
seconds**.

Five additive source functions exercise saved operands, opaque actuals, mixed
Number inputs, computed BigInts and independently retained children. The
checker joins every observation at its exact program/function/pc coordinate
and preserves all historical source bytes and observation counts. The first
fixture run reports **55/85 precision**, zero violations, partial or pending
claims, then fails one historical verdict expectation. The original
`objectFrameRelationalBigInt` compares branch-selected original `1n`/`2n`
constants against `2n` through all four kinds; its deleted child now proves
confined. Only that expected verdict changes from Stored to confined. The
corrected fixture rerun passes **1/1 in 0.21 seconds** (test **0.20 seconds**) and checks
the new **20 sites, 40 instances and 32 retained** exactly. All historical
family counts pass, including the earlier BigInt Eq family's **16/32/26**.
The precision increase from **53/81 to 55/85** includes one newly confined
historical child plus one proved-confined and four observed-confined sites
in the added source family. Corpus precision stays **0/64, 0/16, 0/20**, including p5's existing one partial
observation; all four oracles report zero soundness violations.

Local Node checks **97 combined fixture calls and 51 new discriminating
observation mutations**. They distinguish all four Boolean results, selected
source/target identity, saved operand values and independent retained children.
Removing the additive source block reproduces the committed historical fixture
byte-for-byte (SHA256
`efe782b4b6414436af7d0587d60c8d742cb504f495a939ec1c93fd9565718899`); the
only historical expectation change is the justified child verdict above. Evidence:
`/tmp/ctcompile-escape-bigint-relational-node.{py,js,json}`. Changed C++ files
pass both Homebrew clang-format **22.1.8** and bundled **23**, and changed-path
whitespace checks pass. The five code/test hashes are frozen in
`/tmp/ctcompile-escape-bigint-relational-frozen.json`. The first **22-step
build has zero warnings**; the surrounding focused gate finishes **19/20 in
125.60 seconds**, with the fixture expectation above as its only failure.
Evidence: `/tmp/ctcompile-map-clear-{build,focused}.log`. The correction requires
no C++ rebuild: the second generated configure reports no work to do. The
corrected fixture rerun therefore gives passing results for all twenty focused
tests across the initial run and the rerun, including all eight escape tests;
this is not a second complete twenty-test run. The complete formatter using
22.1.8 passes all **745 files**. All five code/test hashes independently match
the parent's corrected frozen input. Evidence:
`/tmp/ctcompile-map-clear-{build2,fixture2,fixture2-detail,format22}.log` and
`-frozen.json`.

The increment is committed as **`9f969651`**. The full **240-step generated
build completes with zero warnings**. Initial CTest finishes **511/517 in
1420.17 seconds**: **371/372 compiler** and **140/145 browser** tests pass.
Lit is **164/165 in 738.29 seconds** (CTest **738.36 seconds**). Its sole
failing case reaches the historical `seeded_cleared` expectation: this
unchanged nine-call program now has complete ownership after clear, while
native admission still refuses the unsupported nullable Number Map-key
carrier. This initial run is not a complete compiler pass. The five browser
failures remain `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors`; exception recovery passes in **1.20 seconds**.

All eight escape tests pass again in that full run. BigInt Eq repeats
**52/30/2448**, and each relational kind repeats **60/32/2684** with its
64-work snapshot. Primitive comparison and dynamic binary families retain
**84/34/3527** and **84/49/4127**. The four execution oracles again report
**zero soundness violations**. Fixture precision stays **55/85**, with zero
partial or pending claims, exact new **20/40/32** observations and every
historical family count preserved. Bootstrap/p5/Phaser escape precision stays
**0/64, 0/16, 0/20**, including p5's existing one partial observation. Native
corpus counts separately remain **19/574, 39/4754, 45/7725** in both modes,
zero pruned; exact Data stays **0/7 browser/CommonJS and 0/8 AMD**.

**`7011c79e`** corrects only the historical native test classification,
preserving the original source, nine-call clear-to-has repair, unsupported
carrier diagnostic and prepared producer/consumer/capture operands. The
focused check passes all **twelve absent-result carrier families** in both
modes, including original/repair observations, fresh/stale forgeries and
prepared reruns. The complete corrected lit run passes **165/165 in
776.42 seconds** (CTest **776.49 seconds**, total **776.50 seconds**, exit
**0**), including **215 native programs and 30 lifetime families**. All
**372 compiler tests have passing results across the initial full run and
corrected rerun**. This is not a second complete 517-test run; the five
established browser failures remain.

A later summary-only correction, **`5f7b5a2f`**, reports the actual **nine
ordinary seeded refusals plus three carrier refusals**. Independent AST
comparison confirms that only `main()`'s final print changes; every test
function, generated source, call and loop matches the executed driver. Its
final no-work build and summary check pass, with no second semantic-run claim.
The executed driver and final reporting driver retain separate hashes and
frozen inputs.

All **twelve executed code/test hashes** match the corrected snapshot and the
devbox immediately after lit. All **twelve final code/test hashes** separately
match current files, committed HEAD, the reporting snapshot and the final
devbox. The five escape code/test hashes remain unchanged from their original
corrected fixture freeze throughout these gates and report-only changes.
Evidence: `/tmp/ctcompile-map-clear-{full,full-detail,corrected-lit,corrected-lit-detail,corrected-remote-hashes,final-report-check}.log`,
`-corrected-lit-exit.txt`, `-evidence.json`, `-report-audit.json`,
`-corrected-frozen.json` and `-final-report-frozen.json`. Independent local
hash audits use the same prefix with `-child-first-full-audit.json`,
`-child-corrected-hash-audit.json` and `-child-final-hash-audit.json`.

Remaining producer boundaries include mixed/computed BigInt conversions and
arithmetic, other primitive conversions, loops, callee summaries and native
lifetime/effect consumers. Computed BigInt arithmetic needs explicit original
category evidence before admission: `primitiveNonBigIntOrigin` currently
recognizes admitted unary/binary results as non-BigInt. Extending only the
operator whitelist would lose that invariant. The new relational producers
return Booleans and preserve it; the refused conversion discrepancies above
remain unresolved runtime observations.


## Computed BigInt unary categories, 2026-09-09

`Neg` and `BitNot` now retain an independently proved BigInt origin, including
chained results and saved array/own-field reads across overwrite and deletion.
A separate per-path set records those original computed results. Every
non-BigInt consumer excludes that set, and copying a path charges its category
entries before allocation. Eq/Lt/Le/Gt/Ge accept two independently proved
BigInt origins; mixed, opaque and other computed BigInt operands still refuse.
Unary Plus, dynamic/static BigInt binary arithmetic, numeric array indices and
own String keys retain their separate boundaries. This is retention evidence,
not native BigInt admission, constant evaluation or a no-throw/effect proof.

This resumes the computed-category boundary after `9f969651`. Root recovered
the child agent's unfinished audit and formatting after its rate limit. The
preimplementation Node and live interpreter probe agrees on all sixteen checks
at **trace=65535**. All historical fixture bytes remain identical, with five
additive functions measuring **20 sites, 40 instances and 32 retained**. The
original unary-BigInt child now proves confined; its source and observation
counts remain unchanged. Fixture precision is **58/89**, combining that
historical improvement with added coverage. All four oracles report **zero
violations**, and corpus precision remains **0/64, 0/16, 0/20** (p5 partial=1).

The first warning-free **21-step** combined build passes **19/20 CTests in
143.14 seconds**. Only the new escape test fails: three test opcode spellings
used underscores instead of `bitand`, `bitor`, `bitxor`. Correcting those names
changes no compiler behavior or JavaScript. The **two-step** rebuild passes
all **eight escape CTests in 9.00 seconds**. Each unary kind passes **83 rows,
15 live states and 2257 retention cutoffs**; the wide snapshot charges an
additional **128** work units for 32 computed origins and their categories.
BigInt Eq repeats **52/30/2448**, each relational kind **60/32/2712**.

Stable formatter 22.1.8 passes all 745 C++ files; changed C++ also passes
bundled 23, whose full check retains the same nine unrelated differences.
The complete warning-free **250-step** build passes **512/517 CTests in
1583.01 seconds** (CTest exit 8). All **372 compiler tests pass**; only the
five established browser failures remain. Lit passes **165/165 in 874.79
seconds**, including **233 native Map programs and 32 lifetime families**.
The full run repeats both unary **83/15/2257** checks, fixture **58/89**, all
four zero-violation oracles and corpus precision **0/64, 0/16, 0/20**.
Native corpus counts remain **19/574, 39/4754, 45/7725** in both modes, zero
pruned; exact Data remains **0/7 browser/CommonJS and 0/8 AMD**. All fourteen
final code/test hashes match local files, committed HEAD and devbox sources.
Evidence:
`/tmp/ctcompile-numeric-{focused-build,focused-focused,escape2}.log`,
`/tmp/ctcompile-numeric-escape-semantics.log`,
`/tmp/ctcompile-numeric-{full,full-detail,full-hashes}.log`,
`/tmp/ctcompile-numeric-{final-frozen,final-evidence}.json`.

Next: independently categorized BigInt binary results and their exceptional
cases, additional local producers, loops and callee summaries. No binary
operator can inherit the non-BigInt result assumption merely from its opcode.
Native lifetime and effect consumers remain separate work.

## Computed BigInt Add/Sub/Mul categories, 2026-09-09

Continues the exact binary-category boundary after `6f62fbaf`, named in the
preceding section and latest HANDOFF. Dynamic `Add`, `Sub` and `Mul` now accept
two independently proved original BigInt operands. Each result enters the
existing separately charged per-path BigInt set. Constant, computed, saved and
forwarded origins retain their own category across overwrites and deletions;
no operand grants category evidence to the other. The same binary SSA result
can independently be a Number or BigInt on different structural paths.
Non-BigInt consumers still exclude the BigInt set, including after a branch
snapshot or a saved array/field read. No source branch is selected from these
category facts.

The VM's `binary_op`/`bigint_binary` paths in `vm/coerce.cpp` combine BigInt
digits into an independent result. Add first enters `to_primitive`'s depth
guard. The whole-frame proof rejects publication, calls and handlers, so its
unrelated early Error cannot retain unpublished local objects. This remains
retention evidence only: allocation success, normal completion, native BigInt
admission and no-throw/effect contracts are unproved. Static BigInt arithmetic,
dynamic Div/Mod/Pow/Concat, mixed categories and object/opaque conversion
remain separate refusals. Explicit zero-divisor, negative-exponent, handler
and thrown-object tests keep those boundaries visible.

Before implementation, the unchanged sixteen-check source
`/tmp/ctcompile-escape-bigint-binary-semantics.js` measured **Node trace=65535**
and **interpreter trace=32767**. The first fifteen checks agree: signed values
beyond exact Number range, Add/Sub/Mul, chained results, unary consumers,
saved field/array reads, mixed String addition and mixed Number TypeErrors.
The last object `valueOf` returning a BigInt control disagrees. That source and
its differing observation are preserved; object inputs remain refused and no
runtime behavior was changed. The interpreter log is
`/tmp/ctcompile-scalars-bigint-semantics.log`.

The warning-free **ten-step** devbox build passes all **eight escape CTests
in 9.32 seconds**. Each binary operator passes **97 rows, 29 stale/fresh live
states and 3098 retention budget cutoffs**, including exhaustive incomplete
contents budgets and exact completion endpoints. A 32-result path snapshot
checks **128 additional work units** for operations, categories and both
snapshot entries. Both unary families now pass **83 rows, 15 live states and
2296 cutoffs**; Eq passes **52/30/2448** and each relational family passes
**60/32/2740**. The array test completes in **0.32 seconds**.

Five additive JavaScript functions cover saved operands, separate Number/BigInt
paths, opaque actuals, mixed comparisons and an independently retained child.
Local Node verifies all **ten calls and twelve discriminating mutations**, plus
exact arithmetic and object identities. All historical JavaScript bytes are
unchanged. The source-coordinate oracle measures **20 sites, 40 instances and
32 retained** for the new family. The old `objectFrameBigIntRelationalComputed`
child now has its own Add category proof, so only that historical verdict is
promoted; its source and allocation/retention counts remain intact. Fixture
precision is **61/93**, compared with the preceding **58/89**: the numerator
includes one historical improvement and two newly covered confined sites.
The fixture reports **61 sound claims, zero partial and zero pending**, and
all four execution oracles report **zero violations**. Corpus precision stays
**0/64 Bootstrap, 0/16 p5 and 0/20 Phaser**; p5 retains its existing one partial.
The existing early corpus runtime errors still bound those observations.

Stable clang-format 22.1.8 passes all **745 C++ files**; changed C++ also passes
bundled 23. Whitespace and JavaScript syntax checks pass. The five code/test
hashes still match their frozen inputs; the doc-only measurement update has
its own refreshed hash in `/tmp/ctcompile-escape-bigint-binary-frozen.json`.
No browser/runtime source changed. Focused evidence:
`/tmp/ctcompile-scalars-escape-build.log`, `/tmp/ctcompile-scalars-escape.log`,
`/tmp/ctcompile-scalars-format22.log`,
`/tmp/ctcompile-escape-bigint-binary-node.js` and the frozen hash file.

The complete **242-step** devbox build finishes with **zero warnings**. CTest
passes **512/517 in 1652.00 seconds** (exit 8): all **372 compiler tests** and
**140/145 browser tests** pass; only the five established browser failures
remain (`selectors`, `frames`, `element_attrs`, `vm_async`, `early_errors`).
Lit passes **165/165 in 935.40 seconds** (CTest 935.46), including **245
published Map programs and 33 sanitizer lifetime families**. The full run
repeats each BigInt binary **97/29/3098** check and the **20/40/32** source
family. All four escape oracles again report **zero violations**, fixture
precision remains **61/93**, and corpus precision stays **0/64, 0/16, 0/20**
with p5's existing one partial.

Native corpus coverage remains **Bootstrap 19/574, p5 39/4754 and Phaser
45/7725** in both modes, zero pruned; exact Data remains **0/7 browser and
CommonJS, 0/8 AMD**. The five escape code/test hashes are unchanged and are
included among **18 final code/test hashes** independently verified against
local files, committed HEAD, frozen inputs and final devbox source hashes.
Full evidence: `/tmp/ctcompile-scalars-final-evidence.json`,
`/tmp/ctcompile-scalars-full.log`, `/tmp/ctcompile-scalars-full-detail.log` and
`/tmp/ctcompile-scalars-full-hashes.log`.

Remaining boundaries include static and exceptional BigInt arithmetic, mixed
conversions, loops, callee summaries and native lifetime/effect consumers.

## Computed static BigInt categories, 2026-09-09

Continues the static category boundary recorded after **`b953ab00`**. Static
`Add`, `BitAnd`, `BitOr` and `BitXor` now require two independently proved
original BigInt operands before recording an independent BigInt result in the
existing charged per-path category set. Saved reads keep that category after
an array or own field changes value; each structural path can give the same
SSA result its own Number or BigInt category. Every existing non-BigInt
consumer still excludes the BigInt set. No value, branch choice, literal key
or native carrier follows from this retention evidence.

The audited `binary_op_static` path reaches `bigint_binary` before its static
Number conversions. These four exact-pair arms allocate independent digits
without calling user conversion or retaining operand object identities. The
existing non-BigInt static path is unchanged, including its separate known
fresh-object behavior. Static signed shifts can fail, unsigned shifts throw,
and mixed/opaque operands remain refused. Dynamic bitwise opcodes are not
borrowed from the static form. Dynamic Div/Mod/Pow/Concat and unknown operations
remain separate boundaries. Allocation success, normal completion, native
BigInt admission and no-throw/effect contracts are not established.

Before production changed, the sixteen-check source
`/tmp/ctcompile-alias-bigint-semantics.js` returned **trace=65535** in both Node
and the unchanged devbox interpreter; root recorded the latter in
`/tmp/ctcompile-alias-bigint-semantics.log`. Checks distinguish wide and signed
bit operations, chained results, saved field/array origins, Number/BigInt path
categories, mixed-input TypeErrors and unsigned-shift refusal. Source `&`, `|`
and `^` reach static VM operations. Static Add is covered by the raw IR matrix
and the shared exact-digit implementation audit; the source addition check
uses dynamic Add, so it is not presented as a source static-Add measurement.

The existing BigInt binary test matrix now exercises all three dynamic and
four static admitted operation forms. It retains two-sided provenance checks,
saved field/array reads, complete structural arms, stale versus fresh solver
queries under forged markers, every incomplete contents/retention budget and
exact endpoints. A wide snapshot requires **128 additional work units** for
32 separately categorized results. Cross-operation controls reject mixed
Number/BigInt consumers and distinguish unsupported dynamic bitwise forms from
static bitwise forms. The earlier static Number matrix preserves its object
and Number controls while giving exact BigInt pairs their separate category.

Six additive JavaScript functions exercise saved operands, separate Number and
BigInt paths, opaque inputs, mixed comparisons, a successful but still refused
shift, and an independently retained child. Local Node checks all **twelve
calls and nineteen discriminating mutations**. The first devbox oracle
measures **24 sites, 48 instances and 38 retained** for this new family. The
original `objectFrameStaticBinaryBigInt` child now proves confined with its
source and allocation/retention counts unchanged. All historical JavaScript
bytes are preserved. Fixture precision is **64/98**, versus **61/93**: one
historical improvement plus two proved and five observed new confined sites.
The fixture has **64 sound claims, zero partial and zero pending**. All four
oracles report **zero violations**; corpus precision stays **0/64, 0/16,
0/20**, with p5's existing one partial.

The first **twelve-step** build has zero warnings. The focused gate passes
**8/9 CTests in 9.80 seconds**, including type inference and seven escape
CTests. The array unit crashes while its new shared test matrix mutates an
invalid enum through generic `Operation::setAttr`; gdb stops in
`BinaryKindAttr::getValue` during the first dynamic binary mutation. The test
now uses each operation's generated `setKindAttr` accessor, preserving the
intended malformed enum rather than an invalid null inherent property. The
invalid-kind and all other controls remain; production and JavaScript source
are unchanged by this correction. This first focused run is not a complete
escape-test pass.

The corrected **two-step** rebuild has zero warnings. The array CTest passes
in **0.36 seconds** (CTest total **0.37 seconds**), so all **eight escape CTests
pass across the two runs**. Each static BigInt operator passes **105 rows,
39 stale/fresh live states and 3706 retention budget cutoffs**, plus the
**128-work** snapshot. The three dynamic BigInt operators each pass
**105/41/3774** with the expanded cross-form controls; both unary families
pass **83/15/2348**. BigInt Eq remains **52/30/2448**, and each relational
family remains **60/32/2740**. The seven static Number forms retain all
**31 rows and 18 live states**, with **1316** cutoffs for Add/And/Or/Xor and
**1283** for shifts. All original non-BigInt primitive operator rows pass.
The final warning-free **243-step** full build completes **512/517 CTests in
1649.36 seconds**, including all **372 compiler checks**. The arrays test
passes in **0.45 seconds** and remeasures all the row/state/cutoff counts above;
all eight escape CTests pass in this single run. Fixture precision remains
**64/98**, all four oracles have zero violations, and corpus precision stays
**0/64, 0/16, 0/20**. The only failures are the five established browser tests:
`selectors`, `frames`, `element_attrs`, `vm_async`, `early_errors`. All twelve
final session code/test hashes match local files, HEAD and the devbox. The full
summary and detailed output are `/tmp/ctcompile-alias-final-evidence.json` and
`/tmp/ctcompile-alias-full-detail.log`.

Stable clang-format **22.1.8** passes all **745 C++ files**; the three changed
C++ files also pass bundled **23**. JavaScript syntax and whitespace checks
pass. All five code/test hashes remain unchanged after the corrected test
freeze; the doc-only measurement update refreshes its sixth hash. No
browser/runtime source changed and the child ran no build. Evidence:
`/tmp/ctcompile-alias-focused-build.log`, `/tmp/ctcompile-alias-focused.log`,
`/tmp/ctcompile-alias-escape-crash.log`,
`/tmp/ctcompile-alias-corrected-build.log`,
`/tmp/ctcompile-alias-corrected-arrays.log`,
`/tmp/ctcompile-alias-escape-static-node.js` and
`/tmp/ctcompile-alias-escape-static-frozen.json`.

## Computed signed BigInt shifts, 2026-09-09

Continues the source-backed signed-shift boundary after **74db2857** and the
latest **68bcc183** handoff. Static `Shl` and `Shr` now accept two independently
proved original BigInt operands. Their normal result uses the same separately
charged per-path BigInt category as other computed origins. Saved own-field
and array reads preserve that category across overwrite/deletion; each
structural path keeps its own Number or BigInt category. No exact value,
constant key, chosen branch or native carrier follows.

The current `binary_op_static` calls `bigint_binary` first. Its signed-shift
arms produce independent digits without user conversion. Negative counts
reverse direction; very large right shifts produce `0n` or `-1n` according to
the input sign. An oversized left shift, including a negative right-shift
count, instead throws an independent RangeError. The existing whole-frame
proof rejects calls, handlers and publication: its unpublished fresh locals
cannot become reachable through that Error. This is retention evidence only,
not allocation success, normal completion or a no-throw/effect guarantee.
`make_error` sets primitive message/stack strings and an existing prototype;
`current_stack` formats source names and offsets. No Error constructor, cause,
frame value or user conversion is involved, so this path adds no local object
edge to the Error. Unsigned BigInt shifts, dynamic bitwise/shifts, mixed/opaque operands, static
Sub/Mul and dynamic Div/Mod/Pow/Concat retain their separate refusals. Existing
non-BigInt static conversion behavior is unchanged.

Before production changed, all **sixteen** checks in
`/tmp/ctcompile-escape-bigint-shift-semantics.js` agreed at **trace=65535** in
Node and the unchanged devbox interpreter. Root's live measurement is
`/tmp/ctcompile-constant-shift-preprobe.log`. The checks distinguish wide and
signed results, zero/reversed counts, enormous right shifts, the two oversized
left-shift forms, mixed and unsigned TypeErrors, saved field/array operands,
path categories and chained unary/bitwise results. The enormous count is
9007199254740993n; the VM refuses a left allocation before attempting it.
This probes actual source static shifts, not an unexecuted raw dynamic form.

The shared raw IR matrix now includes both signed static shift producers.
Every old row remains, with only independently newly proved shift cases
changing expectation. Both operands, saved origins, Number-only consumers,
structural arms, forged markers and stale versus fresh solvers stay separately
checked. New zero/negative/enormous count rows preserve retention evidence but
still reject later effects, even when a concrete runtime shift would throw
first. Count mutations keep categories independently from values. Each wide
snapshot still requires **128 additional work units** for 32 operations,
category entries and both snapshot entries. The first devbox arrays test passes
in **0.41 seconds**. Both shift forms check **119 rows, 47 stale/fresh live
states and 4592 retention budget cutoffs**, including every incomplete budget
and exact completion endpoint. The older static BigInt forms now each check
**109/39/3928**; dynamic Add/Sub/Mul each check **109/41/3908**, and both unary
forms check **83/15/2374**. The static Number forms retain **31 rows and
18 states**, with **1316** cutoffs for Add/And/Or/Xor/Shl/Shr and **1283** for
UShr. BigInt Eq remains **52/30/2448**, each relational kind **60/32/2740**,
and every original non-BigInt primitive operator row passes.

Six additive source functions exercise saved results, Number/BigInt paths,
opaque and mixed refusals, an early RangeError and a separately retained child.
The caller keeps that Error after the callee's unpublished locals die, so the
recording measures **24 source sites, 47 instances and 35 retained**, plus a
separate implicit Error object. Its successful and throwing calls are both
present. The unchanged `objectFrameBigIntStaticShift` child now proves confined. Removing the additive block reproduces every
historical fixture byte exactly. Local Node verifies all **twelve observations
and 23 discriminating mutations**, including signed arithmetic, category
distinctions, overwritten contents and the retained Error's independent
identity. Stable clang-format **22.1.8** and bundled **23** pass the changed
C++; JavaScript syntax and whitespace pass.

The first warning-free **ten-step** build passes **7/8 escape CTests in
8.97 seconds**. The only failure is the new source-row checker demanding a
compiler literal-allocation claim for that implicit Error. The recording has
four source allocations at bytecode PCs **5/9/13/33** and one additional
object at **pc25**, made once and retained through the `thrown` root. The
location-bearing import independently identifies pc25 as the source static
`shl`, not a source object allocation. Production and all JavaScript remain
unchanged by the test correction.

The corrected checker pins the exact Early function body
(`2a35f86f51344a271fec92b345a6b0b5e748f2295cb454d501cf26e2b79df51c`),
all four literal PCs and their mandatory claims. Only the separately asserted
pc25 Error must have no source allocation claim; every other missing claim
still fails. Missing/duplicate/moved Error records, changed retention/root,
a forged claim, extra literal/unknown coordinates, removed literal claims or
coordinates, and a changed source shift each fail. The local CMake check
against the actual recording passes all historical and new families and
rejects all **eleven** mutations. It does not replace the complete devbox rerun.

The first run already reports **68 sound claims, zero partial/pending and
precision 68/103**, compared with the preceding **64/98**. That combines one
historical improvement and three new proved confined sites; the two Early
containers each escape in the normal call, so their partly confined instances
are not new wholly confined sites. The implicit Error remains unclaimed and
adds no confinement credit. All four execution oracles report **zero
violations**; corpus precision remains **0/64, 0/16, 0/20**, including p5's
existing one partial observation.

The corrected no-work rebuild has zero warnings. All **eight escape CTests
pass in 8.81 seconds**, including arrays in **0.42** and the source fixture in
**0.28**. The run repeats both shift **119/47/4592** checks, the exact source
**24/47/35** family and the separately retained pc25 Error. Fixture precision
remains **68/103**, all four oracles have zero violations, and all historical
family counts and corpus precision remain unchanged. The full compiler/lit
gate remains pending; this is a focused escape pass.

The five code/test hashes and evidence document are frozen in
`/tmp/ctcompile-escape-bigint-shift-frozen.json`; only the source-row checker
hash changes after the first gate. No browser/runtime source changed, no
native BigInt admission was added, and the child ran no build. Evidence:
`/tmp/ctcompile-constant-{focused-build,escape,escape-corrected}.log`,
`/tmp/ctcompile-constant-escape-fixture.{rec,claims,mlir}`,
`/tmp/ctcompile-constant-escape-fixture-debug.mlir`,
`/tmp/ctcompile-escape-bigint-shift-node.js` and
`/tmp/ctcompile-escape-shift-checker-audit/audit.json`.

The subsequent full gate completes on the committed **d7e4f154** code: a
**241-step, zero-warning** build and **512/517 CTests in 1698.62 seconds**,
including all **372 compiler tests**. Only the five established browser
failures remain (`selectors`, `frames`, `element_attrs`, `vm_async`,
`early_errors`). Lit passes **165/165 in 979.39 seconds** (**979.46** under
CTest), including the published Map driver with **266 programs and 35 lifetime
families**. All eight escape tests pass; arrays takes **0.39 seconds** and
repeats both shift **119/47/4592** checks. The source family stays **24/47/35**
plus its separately retained Error; fixture precision stays **68/103**, all
four oracles have **zero violations**, and corpus precision and p5's partial
observation remain unchanged. Fourteen local, HEAD, frozen and final devbox
code/test hashes match. Independent log and hash audit:
`/tmp/ctcompile-escape-shift-final-audit.json`; full evidence is
`/tmp/ctcompile-constant-{full,full-detail,full-hashes}.log` and
`/tmp/ctcompile-constant-final-evidence.json`. The earlier failed fixture and
corrected focused gate above remain part of the record. The next bounded
origin review is BigInt Div/Mod/Pow and their independent error exits; it
remains unimplemented and makes no native admission or effect guarantee.

## Computed BigInt division and remainder, 2026-09-09

Continues the explicit Div/Mod/Pow review boundary after **d7e4f154** and
**99498fc1**, found in the preceding section and the **10:13:16 UTC** sync
journal. The current bounded increment covers dynamic `Div` and `Mod` with two
independently proved original BigInt operands. Source division and remainder
import as `binary`, not `binary_static`; that static operation's seven-kind
whitelist is unchanged. Normal results enter the existing separately charged
per-path BigInt category set. Neither operand can supply the other's proof;
saved reads keep their original category after a slot changes or disappears.
No concrete value, key, chosen branch or native carrier follows.

`binary_op` reaches `bigint_binary` before Number conversion. The Div/Mod arms
pass exact digits to `bigint_div`/`bigint_rem`; successful results own fresh
digits and carry neither operand object identity. A zero divisor instead calls
`throw_error` with a fixed primitive message. The same `make_error` path audited
for signed shifts stores primitive message/stack strings and an existing Error
prototype; it never calls user conversion or an Error constructor, reads cause
options, or captures local frame values. The complete frame proof separately
rejects calls, handlers and publication, so an Error leaving this frame cannot
retain its unpublished fresh objects. This proves retention only, never
allocation success, normal completion or a native no-throw/effect contract.

Pow remains refused. Its negative-exponent and large-exponent handling needs a
separate source-backed review, including the unconditional VM exponent cap for
small bases; this increment neither executes a huge allocation nor relaxes
that boundary. Mixed/opaque operands, unsigned or dynamic BigInt shifts,
unsupported static forms and BigInt Concat retain their separate refusals.
The original primitive non-BigInt operator rules remain intact.

Before production changed, the unchanged source
`/tmp/ctcompile-escape-bigint-divmod-semantics.js` (SHA256
`68898c346e2779e35f276373210627b72c9e74d7afc250a824c0c5d84ecd3d33`)
passed all **sixteen checks, trace=65535**, in Node and the current devbox
interpreter. Root's measurement is
`/tmp/ctcompile-boolean-divmod-preprobe.log`. Checks distinguish wide/signed
quotients and remainders, zero dividends/divisors, mixed-input TypeErrors,
saved own-field and array operands, Number/BigInt paths, chained results and
two independent caller-retained RangeErrors. The reference prints the Number
trace and explicitly reports six unsupported BigInt/object globals as skipped;
the trace contains the corresponding assertions, not a claim to print those
values through the native observation ABI.

The existing raw matrix now covers both Div/Mod producers. It retains every
old row, with only the independently newly proved exact-pair consumers changing
expectation. Both-sided provenance, saved origins, path categories,
Number-only consumers, invalid operator kinds, stale/fresh solver queries under
forged markers and complete budget cutoffs remain. Added divisor/value edits
preserve categories independently of values; zero divisors still cannot hide
later publication, calls or unknown effects. Pow with zero, negative and huge
exponents remains an explicit refusal. Each wide path snapshot still requires
128 extra work units for 32 operations, categories and both snapshot entries.
The interrupted recovery's devbox run passed all **eight escape CTests in
8.89 seconds**, recorded in `/tmp/ctcompile-boolean-recovery-escape.log`.
Div/Mod each check **130 rows, 47 stale/fresh live states and 4802 retention
budget cutoffs**; arrays pass in **0.43 seconds**. All four escape oracles report
zero violations. Fixture precision is **72/109**, with **540 compiler claims,
546 observed sites and seven unclaimed sites**; corpus precision remains
**0/64, 0/16 and 0/20**. That run predates the new source-row assertions below;
the fresh resumed gate now verifies them too.

Seven additive source functions exercise saved operands, Number/BigInt paths,
opaque and mixed refusals, separate early Div/Mod errors and a retained child.
Local Node passes all **fourteen observations and 29 discriminating mutations**,
including exact signed arithmetic, original slot categories, separate object
identities and independently retained errors. Removing this additive block
reproduces every historical JavaScript byte exactly. Both clang-format 22.1.8
and bundled 23 pass the changed C++; whitespace and JavaScript syntax checks
pass.

This work was resumed after the **10:44:16 UTC** interrupted-loop journal.
All four frozen code/source hashes still match the predecessor's
`/tmp/ctcompile-escape-bigint-divmod-initial-frozen.json`; no production or
JavaScript changed during recovery. Its existing `.rec`, `.claims` and `.mlir`
artifacts at `/tmp/ctcompile-boolean-divmod.*` pin **28 source sites,
54 instances and 38 retained**, plus **two separate implicit Errors**.
Both `DivEarly` and `ModEarly` record the Error at **pc25**, with one object
retained via `thrown:1`. Their literal allocations remain **pc5/9/13/33**;
the result literal runs only on the successful invocation.

The completed source-row checker pins each original early-error function's
SHA256 independently, requires exactly one error record in each function and
forbids a compiler allocation claim for that implicit object. Every actual
literal still requires its exact compiler claim. Source-backed negative
controls keep opaque and mixed inputs at Stored and the saved child retained.
Local replay passes the real recording and rejects **34 independent mutations**
of source, error/claim rows and literal coordinates; evidence is
`/tmp/ctcompile-escape-divmod-checker-audit/audit.json`. The five code/test hashes
are frozen in `/tmp/ctcompile-escape-bigint-divmod-final-code.json`.

The resumed no-work resync passes **all eight escape CTests in 8.83 seconds**
with the completed checker. Arrays pass in **0.43 seconds**, the source fixture
in **0.31**. Both Div/Mod matrices retain **130 rows, 47 stale/fresh states and
4802 cutoffs**; the fresh fixture prints the exact **28/54/38** source family
and both independent Errors. All four oracles again report zero violations;
fixture precision remains **72/109**, and the three corpus precisions remain
**0/64, 0/16 and 0/20** with p5's existing single partial claim unchanged.
The original source, production and frozen test hashes remain unchanged.
Evidence: `/tmp/ctcompile-boolean-resumed-escape.log` and
`/tmp/ctcompile-escape-bigint-divmod-focused-audit.json`. The final full gate
remains pending; Pow and all native BigInt/effect boundaries remain unchanged.

**Committed recovery audit, 2026-09-09.** The implementation, source matrix and
completed checker landed together in **1138dbfd**. After the **12:13:13 UTC**
interruption, all five code/test hashes were independently checked against
that commit, current HEAD and the frozen manifest; every hash matches. The
focused log also matches its recorded SHA256. A fresh local replay of the
committed source-row assertions passes the original recording and rejects all
**34** mutations; the exact additive JavaScript block passes **14 observations
and 29 mutations** under Node. Removing that block reproduces every preexisting
fixture byte.
Replay evidence is `/tmp/ctcompile-escape-divmod-final-replay/audit.json`.
These checks validate the saved evidence; a new full devbox result is still
pending. The earlier signed-shift fixture failure and its exact-coordinate
checker correction remain recorded above.

**Initial full gate, 2026-09-09.** A fresh focused run first passes **14/14
CTests in 149.81 seconds**, including all eight escape checks. The full run on
**128e0029** then completes a warning-free **247-step** build and passes
**511/517 CTests in 1704.85 seconds**, including **371/372 compiler tests**.
Lit passes **163/165 in 980.26 seconds** (CTest **980.47**): two historical
Boolean-result cases still expect refusal after Boolean global output became
supported. The other failures are the established browser tests `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors`. The two lit expectations
are corrected in test-only commits **7dc796b7** and **48885ae1**; their complete
lit rerun is pending at this checkpoint.

All **eight escape CTests pass in that full run**, including arrays in **0.43
seconds** and the source fixture in **0.31**. Div and Mod each retain **130
rows, 47 stale/fresh states and 4802 cutoffs**. The exact source family remains
**28 sites, 54 instances and 38 retained**, plus **two independent Errors**.
All four oracles report zero violations; precision remains **72/109** for the
fixture and **0/64, 0/16, 0/20** for Bootstrap, p5 and Phaser. The existing p5
partial claim remains separate. All **27** initial code/test hashes match the
frozen inputs, **128e0029** and devbox sources. The corrected **28-input**
manifest includes the same five escape files, still byte-identical to
**1138dbfd**. No escape code or fixture changed during the lit corrections.

Evidence: `/tmp/ctcompile-boolean-gate-full{,-detail}.log`,
`/tmp/ctcompile-boolean-gate-initial-evidence.json` and the independent
`/tmp/ctcompile-escape-bigint-divmod-full-audit.json`. The initial failed full
run is retained separately from the pending corrected lit result.

**Corrected lit gate, 2026-09-09.** The complete rerun passes **165/165 lit
cases in 1040.72 seconds** (CTest **1040.78**, total **1040.80**), including
the **284-program/36-lifetime-family** Map integration gate. Combined with the
initial full run, **all 372 compiler checks and 512/517 CTests pass**; the five
browser failures listed above remain. These measurements comprise the initial
517-test run and a lit-only rerun. All **28** final code/test hashes agree
across the frozen manifest, local files, HEAD and devbox sources; the five escape
files still match **1138dbfd**. The escape results above and every prior lifetime
family remain unchanged. Evidence is
`/tmp/ctcompile-boolean-corrected-lit.log`,
`/tmp/ctcompile-boolean-gate-final-evidence.json` and the updated independent
`/tmp/ctcompile-escape-bigint-divmod-full-audit.json`. The next escape review
remains BigInt Pow's negative and oversized exponent errors, including the VM's
unconditional cap for small bases; native BigInt, completion and effect admission
remain separate unimplemented boundaries.
