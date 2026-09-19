[Back to escape-load-evidence.md](../escape-load-evidence.md)

## Noncapturing logical negation

This resumes the next boundary recorded after **`643501db`** and in the
**`1c7985a5`** handoff. The complete contents query admits only `ctjs.unary not`
from the unary family. `Operators.td`, the `logical_not` opcode contract and
`ctbrowser/lib/Script/vm/{run_loop,coerce}.cpp` agree: the operation inspects
truthiness and negates it, yielding an independent Boolean without allocating,
throwing, retaining the input or invoking `valueOf`/`toString`. The query records
no truth value, input alias or branch liveness. Every other unary kind still
refuses, including `typeof` and `void`, whose result proofs remain separate.

The Boolean may be stored, returned or forwarded and rooted in a matched local
frame. Its input remains opaque when it began as an entry value. Keys, contents,
returns, roots and copy endpoints still require their independent existing
proofs. Unsupported producers and effects refuse even when negation consumes
their results. Both structural conditional arms remain checked, including an
unsupported arm after a literal zero's negation. Budget exhaustion discards all
contents evidence and leaves every original escape verdict unchanged.

The devbox unit family passes **22 rows**, **eleven live mutation states**
under forged completion/confinement markers, and a wide snapshot with 32 extra
negation origins. Their measured additional cost is **64 work units**, one
producer and one copied origin per extra result. Every row and live state
sweeps every incomplete contents/retention budget and the exact endpoint.
The arithmetic unary kinds, `typeof` and `void` each have refusal controls.
Saved child identity, primitive return/storage, opaque/local joins, invalid
Boolean keys, forbidden opaque uses and a later retained arm are covered.
All **946 incomplete retention budget cutoffs** pass.

One separate executed-source witness negates `0`, `1`, empty String and
nonempty String `"0"`. It deletes the original and copied child field before
returning both distinct containers, the selected alias and the Boolean result.
Its source-coordinate checker observes **four sites, sixteen instances and
twelve retained instances**, with the old child confined on all four calls.
The historical copy-path family remains **21 sites, 39 instances, 23 retained**;
the preceding source-switch family remains **four sites, ten instances, three
retained**. All earlier family expectations pass unchanged.

All **eight escape CTests pass in 8.23 seconds**, and all four execution oracles
report **zero soundness violations**. Expanded-fixture precision measures
**37/49**, with zero partial or pending claims, versus the preceding **36/48**.
The separate negation witness contributes one additional proved-confined site
and one observed-confined site. This is additional coverage, not a precision
gain on the historical fixture. Bootstrap/p5/Phaser precision remains
**0/64, 0/16, 0/20**, including p5's existing single partial observation.
Focused log: `/tmp/ctcompile-nullable-payloads-focused.log`. The final 247-step
generated build succeeds without warnings. Full CTest passes **512/517 in
1045.44 seconds**, all **372 compiler tests**, with only the five recorded
browser failures. All **165 lit cases** pass in **440.08 seconds**. The four
oracles repeat the same precision and zero-violation results in that gate.
All eighteen session code/test paths match committed HEAD, frozen input and
final devbox source. Evidence: `/tmp/ctcompile-nullable-payloads-full.log`,
`-evidence.json` and `-postgate.log`.

Local Node syntax and execution pass **fifteen fixture calls**, sixteen explicit
identity assertions and two extra object-selector probes whose coercion
counters remain zero. Eight new negation mutations discriminate removed or
duplicated negation, coercing equality, forced alias selection, retained child
fields and copied-container aliasing. The eight historical copy-path and six
source-switch mutations also continue to discriminate. Evidence:
`/tmp/ctcompile-escape-negation-node.js` and its `.py` generator. Homebrew
clang-format **22.1.8** passes all **745 files**; whitespace checks pass.
No runtime behavior or native ownership admission changes. Other total unary
producers, loops and native lifetime consumers remain separate work.

## Noncapturing typeof and void

This continues the next producer boundary in **`b3ecab58`**, the **`4f5e248d`**
handoff and the **15:15:11** synchronization journal. Complete contents now
admit `ctjs.unary typeof` and `ctjs.unary void` as independent primitive
origins. TypeOf produces a String containing a type name; Void produces
Undefined after its operand has already been evaluated. Neither result contains
an object/array reference to the input, and neither operation invokes user code.
Opaque inputs remain opaque for every other use.

The TypeOf proof is deliberately narrower than a no-allocation or nonthrowing
claim. `ctbrowser/lib/Script/vm/coerce.cpp::type_of` inspects tags and callable
identity without property access or conversion. `vm/run_loop.cpp` allocates a
fresh primitive String from that name. The opcode row marks allocation and the
fatal allocation ceiling, with no JS reentry or catchable exception. Existing
contents proofs already admit local allocation; admitting this primitive
terminal neither elides its allocation nor proves that allocation succeeds.
The String's bytes are not inferred, even for a literal operand. Its use as
an own-property key still requires an independent exact-key proof and refuses
here. Numeric array indices and copy/container endpoints also remain unproved.

Void's ODS and boxed lowering contracts return Undefined from an already
evaluated SSA operand. A preceding call, publication or unsupported producer
still refuses the whole contents transaction. The real source compiler emits
the operand's bytecode and then `load_undef`; source `void` therefore provides
an execution control for discarded results and preserved effects, while the
unit fixture independently exercises `ctjs.unary void` itself. No runtime or
ODS behavior changes. Neg, Plus and BitNot remain refused because they can
invoke conversion code. No result supplies an input alias, a branch-liveness
fact, a known opaque value or native ownership admission.

Each kind passes **twenty unit rows**, **twelve live mutation states**
under forged completion/confinement markers, and a wide snapshot with 32 extra
origins costing exactly **64 additional work units**. Every row and live
state sweeps all incomplete contents/retention budgets and the exact endpoint.
Controls cover primitive return/storage/rooting, local/opaque joins, retained
saved reads, unsupported operand evaluation, opaque uses, unknown effects,
invalid result keys/indices and a later retained structural arm. All 22
historical logical-negation rows and eleven mutation states remain; their two
TypeOf/Void rows and TypeOf mutation now expect the independently proved
complete result instead of refusal. Each new kind passes **976 incomplete
retention budgets**; the preserved negation family now passes **1,010** because
those previously refused paths complete.

Two separate executed-source functions add five TypeOf calls (Undefined, Null,
Number, Boolean and String) and two Void calls. Both retain distinct original
and copied containers and the selected alias, after deleting their old child
fields. Void additionally returns a visible field write on the selected
container and an Undefined result. The new source-coordinate family measures
**eight sites, 28 instances and 21 retained instances**. All older source
families, calls and exact claim expectations pass unchanged: the copy-path
family stays **21 sites, 39 instances, 23 retained**; source switch stays
**four sites, ten instances, three retained**; negation stays **four sites,
sixteen instances, twelve retained**.

All **eight escape CTests pass in 8.35 seconds**, and all four execution oracles
report **zero soundness violations**. Expanded-fixture precision measures
**39/51**, with zero partial or pending claims, versus the preceding **37/49**.
The two new witnesses each add a proved-confined and an observed-confined site;
these are additional coverage, not a precision gain on the historical fixture.
Source Void already imported as constant Undefined, so its new witness does
not measure a gain from admitting `ctjs.unary void`. Bootstrap/p5/Phaser
precision remains **0/64, 0/16, 0/20**, including p5's existing single partial
observation. Focused log: `/tmp/ctcompile-mixed-nullable-focused.log`.

Local Node syntax and execution pass **22 combined fixture calls**, **32
identity assertions** and three object-selector probes whose conversion
counters stay zero. All **nineteen new observation mutations** discriminate
wrong type names, lost/aliased identities, omitted deletion, a retained unary
operand, omitted Void effects and writes to the wrong container. The eight
historical copy-path, six source-switch and eight negation mutations continue
to discriminate. Evidence: `/tmp/ctcompile-escape-total-unary-node.js` and its
`.py` generator. Homebrew clang-format **22.1.8** passes all **745 files** in
the parent's frozen snapshot, and whitespace checks pass.

The final 247-step devbox build passes warning-free. CTest finishes **512/517
in 1113.08 seconds**, with all **372 compiler tests** passing and only the five
recorded browser failures. All **165 lit cases pass in 489.13 seconds**. The
four escape oracles again report zero soundness violations and the same fixture
and corpus counts above. Native Bootstrap remains **19/574**, exact Data
**0/7 browser/CommonJS and 0/8 AMD**; this escape work adds no measured native
corpus coverage. Final logs: `/tmp/ctcompile-mixed-nullable-full.log`,
`-evidence.json` and `-postgate.log`.

The next bounded producer candidate is the supported `ctjs.binary_static`
family, requiring its own result/effect and refusal/budget evidence. Loops and
native lifetime consumers remain separate work.

## Static binary results after excluding BigInt

This resumes that exact producer boundary from **`a539c3fa`**, the
**`ae8e021a`** handoff and the **16:28:03** synchronization journal. Complete
contents now admit all seven verified `ctjs.binary_static` kinds only after
both operands have independently known, non-BigInt origins on the current
structural path. An explicit whitelist recognizes primitive non-BigInt
constants, fresh objects/arrays, previously admitted total primitive producers
and earlier proved static Number results. Forwarded values and saved reads
use their original identity, including after the source slot is overwritten.
No alias lattice, inferred native type or completion annotation supplies this
proof. The result is a separate primitive Number origin, without an inferred
value, index, property key, input alias or branch-liveness fact.

The exclusion is required by the actual VM contract, rather than by a blanket
purity assumption. `Operators.td`, `bytecode_opcodes.def` and
`vm/coerce.cpp::binary_op_static` agree that the seven operations cannot
reenter user code, but their BigInt path can allocate and throw catchable
TypeError/RangeError. Mixed operands and unsigned BigInt shifts throw; signed
BigInt shifts can also fail. Even a successful literal BigInt pair stays
outside this Number-only proof. Opaque arguments refuse on either side.
After independently excluding BigInt, the static conversions return Number
without an input alias, a catchable JS throw or user conversion. They can
allocate ordinary C++ temporaries: `number_format.cpp::string_to_number`
uses `std::string without_point` to parse a trailing decimal point. The initial
no-allocation comment was incorrect and was corrected during review. As with
the earlier TypeOf proof, absence of allocation or fatal/foreign failure is
not proved. Fresh objects use those same static conversions in the VM; this
is an existing documented deviation from source JavaScript object coercion.
This change does not assert that source behavior agrees on objects and changes
no runtime operation.

The unit table passes **31 rows per kind**, both BigInt/opaque operand positions,
all previously admitted primitive producer origins, saved reads before a
BigInt/non-BigInt overwrite, retained structural arms and unsupported effects.
Each kind passes **18 live states** under forged completion/confinement markers,
including all six invalid static kinds and an in-place Number-to-BigInt
constant mutation. Every table/live state uses the existing complete contents
and retention budget sweeps, including **1,283 incomplete retention budgets
per kind**; the wide snapshot's 32 extra results cost exactly **64 work units**.
All earlier contents/retention families pass unchanged.

Three executed-source functions separately exercise the seven Number
operations, an opaque numeric observation and a successful literal BigInt
pair. The high-bit input and shift count 33 distinguish signed/unsigned
results, truncation and masking. All returned graphs preserve distinct
source/copied containers after deleting their child fields. The two controls
keep conservative Stored claims despite observed confinement; source Number
observations cannot establish an opaque argument's tag. The new source family
measures **twelve sites, twenty instances and fifteen retained instances**.
All older source-coordinate expectations pass unchanged.

All **eight escape CTests pass** in the focused nine-test gate, which includes
the seeded host test and finishes in **21.62 seconds**. All four execution
oracles report **zero soundness violations**. Expanded-fixture precision is
**40/54**, with zero partial/pending claims, versus the preceding **39/51**.
The new family adds one proved-confined site and three observed-confined sites;
this adds coverage rather than measuring a precision gain on the historical
fixture. Bootstrap/p5/Phaser precision remains **0/64, 0/16, 0/20**, including
p5's existing single partial observation. Focused evidence:
`/tmp/ctcompile-nullable-host-results-focused.log`. No measured native corpus
gain is claimed for this escape proof.

Local Node syntax/execution passes **27 combined fixture calls**, **41 identity
assertions**, the three historical object-selector probes with zero conversion
calls and an opaque BigInt TypeError probe. All **28 new observation mutations**
discriminate changed operations, wrong shifts/masks, lost identities, omitted
deletions and changed BigInt result tags. All earlier **41 observation
mutations** continue to discriminate. Evidence:
`/tmp/ctcompile-escape-static-binary-node.js` and its `.py` generator.

The next producer boundary is catchable BigInt outcomes, requiring a separate
completion-path proof; it is not discharged by successful literal observations.
Loops and native lifetime consumers remain separate work.

The full **253-step generated build passes warning-free**. Its initial CTest
run passes **511/517 in 1141.97 seconds**: **371/372 compiler** and **140/145
browser** tests. The compiler failure was an old nullable short-circuit test
expecting no host owner; test-only **`077328ae`** now requires the complete
owner proof while preserving refusal of an unsupported native intermediate.
The complete refusal tail passes, followed by the corrected `ctcompile_lit`
CTest **1/1 in 526.35 seconds**, with **165/165 lit cases in 526.28 seconds**.
All **372 compiler tests pass across those two runs**; the five established
browser failures remain. Four escape oracles again report zero violations,
fixture **40/54** and corpus **0/64, 0/16, 0/20**. Native counts remain
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725** in both modes, with
zero pruned; exact Data stays **0/7 browser/CommonJS and 0/8 AMD**. Evidence:
`/tmp/ctcompile-nullable-host-results-evidence.json` and
`/tmp/ctcompile-nullable-host-results-lit-final-detail.log`.

## Arithmetic unary results from proved primitive inputs

This continues the producer thread in **`ac3d0d43`**, the **`a8da7c27`**
handoff and the **17:53:27** synchronization journal. Catchable BigInt binary
outcomes still lack a completion-path proof. The bounded increment instead
admits `ctjs.unary neg`, `plus` and `bitnot` only when the current structural
path independently supplies a primitive non-BigInt operand origin. The
whitelist contains Undefined, Null, Boolean, Number and String constants and
previously admitted primitive producers. Saved reads and forwarded values use
their original origin, including after a source field acquires a BigInt.
Neither native type facts, alias lattices nor completion annotations establish
this primitive proof. The legacy unary operand-role table is unchanged.

`Operators.td`, `Bytecode/OperatorTables.h`, `bytecode_opcodes.def` and
`vm/coerce.cpp` connect source minus/plus/complement to these operations.
`to_number_value` calls static `to_number` directly for the five admitted
primitive categories. It cannot invoke object `valueOf`/`toString` there, and
the BigInt TypeError path is excluded independently. Negation and complement
likewise return Number for these inputs. BitNot uses static conversion in the
VM, which deviates from source JavaScript on object inputs; all fresh objects
and arrays remain refused by this common primitive proof. An opaque input
also remains refused, even when every recorded call supplied a Number.

The result has its own primitive Number origin, with no operand alias,
numeric value, property key, array index or branch-liveness inference.
String parsing may allocate an ordinary C++ temporary for a trailing decimal
point. No absence or success of allocation, or absence of fatal/foreign
failure, is proved. Successful literal BigInt negation/complement are separate
result categories and remain refused; Plus's catchable BigInt TypeError also
remains outside this proof. Unknown producers, calls, publications and effects
continue to refuse the entire contents transaction.

The devbox unit family passes **34 rows per kind**, **nineteen live mutation
states** under forged completion/confinement markers, and a wide snapshot
requiring exactly **64 additional work units** for 32 extra origins. It checks
both incoming structural arms, object/array/opaque/BigInt refusals, all prior
primitive producer categories, original saved reads across tag overwrites,
retained child identities, forbidden result keys and preserved unsupported
effects. Every row and live state sweeps all incomplete contents/retention
budgets and the exact endpoint, including **1,779 incomplete retention budget
cutoffs per kind**. All historical contents/retention families pass unchanged.

Four new source functions separately exercise all three Number operators,
saved String/Boolean inputs before a BigInt field overwrite, opaque input
refusal and successful literal BigInt refusal. Null checks signed zero;
`"4294967297."` checks String conversion and 32-bit truncation. Returned graphs
retain distinct original/copied containers after child deletion, and the two
positive functions also return the selected alias. The source-coordinate
checker measures **sixteen sites, 28 instances and 21 retained instances**.
Earlier source families and their exact claim expectations are unchanged.

All **eight escape CTests pass** in the focused gate, and all four execution
oracles report **zero soundness violations**. Expanded-fixture precision is
**42/58**, with zero partial/pending claims, versus the preceding **40/54**.
The new family contributes two proved-confined and four observed-confined
sites; this adds coverage rather than measuring a precision improvement on
the historical fixture. Bootstrap/p5/Phaser precision remains **0/64, 0/16,
0/20**, including p5's existing single partial observation. The complete
focused gate passes **12/12 CTests in 61.47 seconds**. Evidence:
`/tmp/ctcompile-nested-method-focused.log`. No native corpus gain is claimed
for this escape increment.

Local Node syntax and execution pass **34 combined fixture calls** and their
numeric, tag, field and identity assertions. All **31 new observation
mutations** discriminate changed operators, lost signed zero/conversion,
wrong selected aliases, changed saved-field state, omitted deletion and
copied-container aliasing; all **69 historical mutations** still discriminate.
Additional opaque probes observe a BigInt TypeError, three source object
conversion calls and a thrown conversion value with one call. These object
probes explain the refusal and do not assert that the VM's static BitNot object
behavior agrees. Evidence: `/tmp/ctcompile-escape-arithmetic-unary-node.js`
and its `.py` generator. Homebrew clang-format **22.1.8** and whitespace checks
pass the changed C++ files. No runtime behavior or native admission changed.

The next BigInt boundary still requires completion-path evidence, rather than
successful literal observations. Loops and native lifetime consumers remain
separate work.

The final **243-step generated devbox build passes warning-free**. CTest
finishes **512/517 in 1170.30 seconds**: all **372 compiler tests** and
**140/145 browser tests** pass. Only the five established browser failures
remain: `selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.
All **165/165 lit cases pass in 529.85 seconds** (CTest **530.05 seconds**),
including the complete **137-program/eighteen-lifetime** published Map gate.
The four escape oracles again report zero violations, fixture **42/58** and
corpora **0/64, 0/16, 0/20**. Native coverage remains Bootstrap **19/574**, p5
**39/4754** and Phaser **45/7725** in both modes, with zero pruned; exact Data
remains **0/7 browser/CommonJS and 0/8 AMD**. All twelve session code/test
paths match committed HEAD, frozen gate input and final devbox sources.
Evidence: `/tmp/ctcompile-nested-method-full.log`, `-full-detail.log`,
`-evidence.json` and `-postgate.log`.

## Loose equality from two proved primitive origins

This continues the producer thread in **`1392478f`**, the **`e533a865`**
handoff and the **18:56:37** synchronization journal. Complete contents now
admit `ctjs.compare eq` only when **both** current original operand origins
independently prove Undefined, Null, Boolean, Number or String. Earlier admitted
primitive producers also supply independent origins. Objects, arrays, opaque
entry values and BigInt refuse on either side; proving one operand never proves
the other. Saved reads keep their original identity after a slot is overwritten
or deleted, and all structural incoming paths must pass independently.

`CTJS/IR/Ops/Operators.td`, `CTJS/Import/Bytecode/OperatorTables.h` and the
`loose_equal`/`loose_not_equal` opcode rows connect source `==` and `!=` to
Eq and Eq followed by Not. The new restriction follows the current
`ctbrowser/lib/Script/vm/coerce.cpp::loose_equals` implementation: the five
primitive categories use tag/nullish tests, primitive String equality or static
`to_number`. These paths never invoke user conversion or a reentry-depth guard,
throw a catchable JS exception, or carry either input's object identity.
String parsing can allocate C++ temporaries; neither absence nor success of
allocation is proved. The Boolean has no inferred truth value, property key,
array index, operand alias or branch-liveness fact. StrictEq and `operandRole`
are unchanged; the existing converted sink classifications remain conservative.

The devbox unit family passes **70 rows**, **34 live mutation states** under
forged completion/confinement markers, and a 32-origin wide snapshot requiring
exactly **64 additional work units**. Every row and live state uses the existing
exhaustive incomplete contents/retention budget sweeps and exact endpoint.
Controls cover each operand position, original primitive/object/BigInt reads
across array overwrites, own fields after overwrite/deletion, both structural
incoming arms, retained child identities, primitive storage/return/rooting,
invalid result keys, unsupported effects and all four relational kinds.
Number-to-BigInt in-place constant mutations and Eq/StrictEq/relational kind
changes defeat stale completion markers. All **3,017 incomplete retention
budget cutoffs** pass; all historical array contents/retention families pass.

The initial focused devbox gate passes **11/12 CTests in 71.88 seconds**,
including the four escape analysis tests and all three corpus claim tests. Its
fixture oracle reports **zero violations**, **43/63 precision**, zero partial
and pending claims, and the expected **twenty sites, 36 instances, 27 retained**
for the five new functions. The fixture CTest nevertheless fails because the
new `Released` child's predicted confinement was overbroad: its source spells
`other = choice ? 18 : undefined`, whose bare identifier imports as
`ctjs.load_global "undefined"` at line **2422** of
`/tmp/ctcompile-leaf-object-escape-raw.mlir`. `compile_ident` in
`Script/compile/expressions.cpp` emits `get_global` for that unbound identifier;
`CTJS/Import/Bytecode/Instructions.cpp` preserves the lookup. The whole-function
query correctly refuses that unsupported operation, regardless of the runtime
value it returned. This was a test expectation error, not a producer defect.
Only the `Saved` source proves confinement in this first run. All historical
source-coordinate families pass unchanged; Bootstrap/p5/Phaser remain
**0/64, 0/16, 0/20**, with zero soundness violations. Focused log:
`/tmp/ctcompile-leaf-object-focused.log`.

The corrected source family preserves the original `Released` function and
both calls byte-for-byte as a global-lookup refusal control. A separately named
`Literal` repair changes only that operand spelling to `void 0`, which imports
as constant Undefined; the two observations still agree in Node. The original
function-and-calls SHA256 is
`7e07592c147839ff38475ed69820f04004942fdb541a9bbaacc45a5eb1c27353`.
Six source functions now provide two positives (`Literal` and `Saved`) and
four controls (global lookup, opaque formal, BigInt and relational). The
corrected devbox rerun passes **2/2 CTests in 0.26 seconds**: all array unit
families, including the **70 rows/34 states/3,017 cutoffs** above, and the exact
fixture checker with **24 sites, 44 instances and 33 retained instances**.
The expanded fixture reports **zero violations**, **44/64 precision**, and
zero partial/pending claims. Relative to the preceding committed **42/58**,
this adds two proved-confined and six observed-confined source sites; it adds
coverage, rather than measuring a precision improvement on historical input.
All **eight escape CTests pass across the initial gate and corrected rerun**.
Every original source function, invocation and historical family expectation
is preserved. Evidence: `/tmp/ctcompile-leaf-object-escape-rerun.log`.
The final generated build/CTest gate is recorded below; no native corpus gain
is claimed.

Local Node syntax/execution passes **45 combined fixture calls**, with explicit
result/tag/field/identity checks. All **56 new observation mutations** and
**100 historical mutations** discriminate; additional opaque object probes
observe three `valueOf` calls and one thrown conversion value with one call.
These probes justify retaining opaque/object refusals and do not replace
current-IR proofs. Evidence: `/tmp/ctcompile-escape-primitive-equality-node.js`
and its `.py` generator; the preceding 43-call observations are preserved in
`/tmp/ctcompile-escape-primitive-equality-initial-node.{js,py}`. Homebrew
clang-format **22.1.8** and whitespace checks pass the changed paths. No runtime
behavior, producer logic or native admission changed in this correction.

A source audit also qualifies the preceding arithmetic-unary contract.
`coerce.cpp::negate_value` reaches `to_number_value`, whose unconditional
`vm.hpp::reentry_scope` can throw a catchable RangeError above depth **512**
before testing a primitive operand's tag. BitNot uses the guard-free static
conversion. Thus Neg/Plus exclude conversion of an object operand and the
BigInt TypeError; they do **not** prove normal completion or absence of all
runtime exceptions. Uncaught error description can itself consult Error
prototype accessors (`vm/call/invoke.cpp::describe_thrown`). The current sole
consumer, `refineArrayRetention`, proves the entire function without calls,
publication, handlers or closure creation. `vm.hpp::make_error` attaches only
an unrelated Error, primitive message/stack data and a pre-existing prototype;
an early exceptional exit cannot expose those unpublished fresh locals.
Caught unwind excludes the ending frame's register window in the oracle
(`vm.hpp::unwind_to_handler`/`each_root`). No retention counterexample was found;
this is source inspection, **not a measured deep-stack execution**. Comments
now explicitly forbid treating this retention evidence as a no-throw/effect
contract for future native consumers. Earlier measured unary behavior stays.

All four relational kinds remain a conservative next producer boundary. They
unconditionally call `to_primitive`, including its depth guard for primitive
operands, so they need their own completion/retention argument and controls.
BigInt comparison categories, loops and native lifetime consumers also remain
separate work.


The Eq increment is saved in **`5e2cb6b2`**. The final **244-step generated
devbox build passes warning-free**. Full CTest finishes **512/517 in 1199.12
seconds**, with all **372 compiler tests** and **140/145 browser tests** passing.
Only the five established browser failures remain: `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors`. All **165 lit cases pass in
551.23 seconds**, CTest **551.43 seconds**; exception recovery passes in
**1.18 seconds**. No corrective rerun was needed for this full gate.

All eight escape CTests pass again. The Eq unit family repeats **70 rows,
34 live states and 3,017 retention cutoffs**, and all historical unit families
pass unchanged. Four execution oracles again report **zero soundness
violations**. Expanded-fixture precision remains **44/64**, with zero
partial/pending claims and the exact **24-site/44-instance/33-retained** source
family. Bootstrap/p5/Phaser precision stays **0/64, 0/16, 0/20**, including
p5's existing single partial observation. Native corpus counts stay Bootstrap
**19/574**, p5 **39/4754** and Phaser **45/7725** in both modes, with zero
pruned; exact Data stays **0/7 browser/CommonJS and 0/8 AMD**. Final evidence:
`/tmp/ctcompile-leaf-object-full.log`, `-full-detail.log` and `-evidence.json`.
The completed postgate checks confirm that all **nineteen code/test paths**
match committed HEAD, frozen input and final devbox sources. Both actual emitted
leaf programs contain no Script/VM symbols. Evidence:
`/tmp/ctcompile-leaf-object-postgate.log` and `-final-hashes.json`.

## Primitive relational origins, 2026-09-08

This continues the exact relational boundary left by **`5e2cb6b2`** and the
**`5d2d843a`** handoff. `computeArrayContents` now accepts Lt, Le, Gt and Ge
only when **both original operand origins** independently prove Undefined,
Null, Boolean, Number or String. The proof follows saved reads and every
structural incoming arm; a later primitive overwrite cannot repair an older
object/BigInt origin. Existing Eq and StrictEq behavior is preserved. No type,
completion or confinement report supplies authority, and `operandRole` remains
unchanged. Results are independent Booleans with no inferred truth, key,
index, alias or branch-liveness fact.

The runtime argument is deliberately narrower than no-throw. Current
`Script/vm/coerce.cpp::compare_relational` passes both operands, in source order,
through `to_primitive`. On these independently proved primitive categories,
normal execution then uses String/String byte comparison or static numeric
conversion; neither invokes a user conversion or returns an input object.
The unconditional `to_primitive` depth guard can still throw an unrelated
RangeError above depth 512. `script/vm.hpp::make_error` attaches only primitive
message/stack data and a pre-existing error prototype. The entire contents
query refuses calls, publication, closure creation and handlers, so none of
its fresh local identities is available to an outer catch or an Error prototype
accessor reached by uncaught-error description. The unwind oracle excludes the
ending frame's register window (`unwind_to_handler`/`each_root`). This justifies
retention on an early exit, **not normal completion or an effect/no-throw
contract**. C++ String parsing may allocate; allocation success is unproved.
A further source-level audit found no publication counterexample. `make_error`
uses direct own-data writes, with no `cause`/options processing or Error
constructor call. `current_stack` serializes only function-proto names, indices
and instruction offsets into a String; it captures no frame values. The
importer preserves closure, cell and upvalue operations explicitly, and this
query rejects them, so a hidden closure cannot retain these fresh locals.
No dynamic deep-stack measurement is claimed. Native lifetime/effect consumers
still require their own proof. No runtime/browser source changed.

The independent unit table now runs for Eq and each of the four relational
kinds. Each passes **75 rows**, **34 live mutation states**, an exact
**64-work-unit** wide snapshot increment, and **3,300 incomplete retention
budget cutoffs**, alongside the exhaustive incomplete contents budgets.
Controls cover both operands, original array/field reads across replacement/deletion, every structural arm, retained aliases,
opaque/BigInt/object exclusions, invalid result keys, fresh/stale completion
markers, and actual publication before/after the comparison, retaining calls,
explicit throws and local exception handlers. The latter keep the exceptional
retention argument bounded to the function subset it audited.

The initial focused devbox run passes **11/12 CTests in 87.98 seconds**.
The only failure is five repetitions of the new handler row, one per comparison
kind: it expects `UnsupportedOperation` (2), while the existing non-cf successor
guard returns `UnsupportedControlFlow` (1) at `ctjs.push_handler`.
The query already refuses with empty evidence, and the retention refusal and
budget checks pass. Correcting only that expected enum fixes the test; a
nearby comment now explicitly limits the guard-free/catchable-throw statement
to Eq. No production behavior or JavaScript source changes in the correction.
The subsequent nineteen-step focused rebuild passes, followed by **12/12
CTests in 83.03 seconds**, including **all eight escape CTests**. All five
comparison kinds repeat the exact unit counts above, and historical array
contents/retention families pass. Evidence:
`/tmp/ctcompile-leaf-readback-focused.log`, `-focused2.log` and
`-focused2-detail.log`. The completed full gate is recorded below.

The historical `objectFrameLooseEqualityRelational` function and both calls
retain their exact source bytes. Its numeric/String results already ran in
the preceding oracle; only its child's claim changes from Stored to confined,
now independently checked against both observed instances. This is a precision
improvement on existing input, not new source coverage. The historical
loose-equality family keeps its **24 sites, 44 instances and 33 retained**;
all other historical family expectations pass unchanged. Five separate new
functions measure **20 sites, 40 instances, 32 retained**: saved String origins
after a BigInt field overwrite and deletion; unordered Undefined/invalid numeric String results; opaque
formal and literal BigInt refusal controls; and an independently saved child
still returned after both container fields are deleted. Four execution oracles
report **zero soundness violations**. Expanded-fixture precision is **47/68**,
with zero partial/pending claims. Relative to **44/64**, one already observed
child gains confinement; the new family contributes two proved-confined and
four observed-confined sites. Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**,
including p5's existing single partial observation. These escape measurements
do not increase native corpus admission.

Local Node execution passes **55 combined fixture calls**, preserving every
historical function and call. Independent assertions check lexical versus
numeric order, all four false outcomes for unordered values, reverse operand
results, exact source/target identities and selected aliases, and the returned
saved child. All **51 new observation mutations** and **156 historical
mutations** discriminate. Additional opaque-object probes observe four
`valueOf` calls and a thrown conversion value after one call; these justify
retaining the object/opaque refusal and do not replace the current-IR proof.
Evidence: `/tmp/ctcompile-escape-relational-node.{py,js,json}`. Homebrew
clang-format **22.1.8** and changed-path whitespace checks pass.

The relational increment is saved in **`b98efac0`**. The final **244-step
generated devbox build completes with zero warnings**. Full CTest finishes
**512/517 in 1257.06 seconds**: all **372 compiler tests** and **140/145 browser
tests** pass. The five established browser failures remain `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors`. All **165 lit cases
pass in 606.48 seconds** (CTest **606.54 seconds**), including the integrated
published Map driver. Its source census contains **163 programs and twenty
lifetime families**; those counts follow from the passing complete driver,
not an additional execution run. Exception recovery passes in **1.17 seconds**.

All eight escape CTests repeat successfully in this full run. Each comparison
kind repeats **75 rows, 34 live states and 3,300 retention cutoffs**; all four
execution oracles again report **zero soundness violations**. Fixture precision
stays **47/68**, zero partial/pending, with the exact **20/40/32** relational
and **24/44/33** historical equality family observations. Bootstrap/p5/Phaser
remain **0/64, 0/16, 0/20**, including p5's existing single partial observation.
Native corpus counts remain Bootstrap **19/574**, p5 **39/4754** and Phaser
**45/7725** in both modes, zero pruned; exact Data stays **0/7 browser/CommonJS
and 0/8 AMD**.

All **seventeen code/test paths** match committed HEAD, frozen input and final
devbox sources. Both actual emitted identity/lifetime C++ files retain owning
`map_get_present_identity` results across later mutations and contain no
Script/VM context symbols. Evidence:
`/tmp/ctcompile-leaf-readback-{full.log,full-detail.log,evidence.json,postgate.log}`,
`-root-hashes.json`, `-final-identity.cpp` and `-final-lifetime.cpp`.

Remaining boundaries include independently proved primitive conversions and
dynamic arithmetic, BigInt comparison categories, loops, callee summaries and
native lifetime consumers. These retain their existing conservative behavior.

## Primitive dynamic arithmetic origins, 2026-09-08

This continues the dynamic-arithmetic boundary left by **`b98efac0`** and the
**`e385f885`** handoff. `computeArrayContents` accepts dynamic Sub, Mul, Div,
Mod and Pow only when **both original operand origins** independently prove
Undefined, Null, Boolean, Number or String. Saved reads keep their original
identity through overwrites and deletions, and every structural incoming arm
must qualify separately. No type, completion or confinement annotation is
proof. Dynamic Add/Concat, all other dynamic kinds, objects, arrays, opaque
inputs and BigInt remain outside this increment. The operand-role table and
native admission are unchanged.

Current `Script/vm/coerce.cpp::binary_op` first checks `bigint_binary`.
Independently excluding BigInt makes that helper return false without
allocation or a JS throw; each admitted kind then converts both operands
through `to_number_value`. Normal primitive paths use static numeric conversion
and produce an independent Number, including NaN, infinity and signed zero.
Mod uses `fmod`; Pow uses the VM's `exponentiate`, including its +/-1 to NaN
special case. None of these paths returns an input object or invokes an object
conversion. The origin proof infers no numeric value, array index, property key
or branch liveness.

As with Neg/Plus, `to_number_value` enters the depth guard before inspecting
the primitive tag. Its unrelated RangeError cannot retain unpublished fresh
locals in this complete call/handler/publication-free query; the preceding
relational section's Error construction, stack serialization and unwind-root
audit also applies here. This is **retention-only evidence**, never normal
completion or a no-throw/effect contract. String parsing can allocate C++
temporaries, and allocation success remains unproved. No deep-stack dynamic
measurement or browser/runtime change is claimed.

The existing independent two-operand table is shared with the new arithmetic
family, preserving every comparison row and its operator-specific mutation
controls. Each kind passes **80 rows**, an exact **64-work-unit** wide snapshot
increment, and exhaustive incomplete contents/retention budgets. Each comparison
kind passes **34 live states and 3,425 retention budget cutoffs**; each arithmetic
kind passes **49 live states and 3,941 retention budget cutoffs**, including all
thirteen dynamic enum values and restoration. Controls cover both operands,
every structural arm, original array/field reads before
replacement/deletion, saved returned children, primitive rooting/storage,
invalid result keys, forged completion/confinement markers, and publication,
calls, explicit throws and local handlers. Five new cross-producer rows also
exercise the independent Number origins as inputs to every comparison kind.

The initial devbox build compiles production but finds four missing dependent
`template` keywords in the shared test helper: three `getOps` calls and one
`getDefiningOp`. Adding only those keywords fixes compilation; production
behavior and every JavaScript source byte are unchanged. The corrected
eleven-step rebuild and focused **14/14 CTests pass in 18.28 seconds**, including
**all eight escape CTests**. Historical array/object contents and retention
families also pass. Evidence: `/tmp/ctcompile-field-presence-build3.log`,
`-focused.log` and `-focused-detail.log`. The completed full gate is recorded below.

Five additive source functions measure **20 sites, 40 instances and 32
retained**, with confinement only for the two independently proved arithmetic
children. Opaque formal and successful BigInt controls retain Stored claims; an
independently saved returned child remains retained after both container fields
are deleted. Every historical source byte and historical
family expectation is preserved, including the historical relational
**20/40/32** and equality **24/44/33** families. All four execution oracles report
**zero soundness violations**. Expanded-fixture precision is **49/72**, with
zero partial/pending claims, versus the preceding **47/68**. This adds two
proved-confined and four observed-confined sites; it measures additional source
coverage, not precision improvement on historical source. Bootstrap/p5/Phaser
remain **0/64, 0/16, 0/20**, including p5's existing single partial observation.
These escape measurements do not increase native corpus admission.

Local Node execution passes **65 combined fixture calls**, **60 new observation
mutations** and **207 historical mutations**. Assertions distinguish operand
order, saved String values after a BigInt overwrite, finite results, NaN,
infinity, signed zero, BigInt result types, container identity and retained
saved children. An opaque object executes five `valueOf` calls; a throwing
conversion stops after one; five mixed/zero-divisor/negative-exponent BigInt
probes throw. These observations justify conservative refusal without replacing
current-IR proof. Evidence: `/tmp/ctcompile-escape-dynamic-node.{py,js,json}`.
Homebrew clang-format **22.1.8** and changed-path whitespace checks pass.

The arithmetic increment is saved in **`56569199`**. The final **246-step
generated devbox build completes with zero warnings**. Full CTest finishes
**512/517 in 1267.11 seconds**: all **372 compiler tests** and **140/145 browser
tests** pass. The five established browser failures remain `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors`. All **165 lit cases
pass in 619.29 seconds** (CTest **619.49 seconds**), including the integrated
published Map driver. Exception recovery passes in **1.15 seconds**.

All eight escape CTests repeat successfully. Each comparison kind repeats
**80 rows, 34 live states and 3,425 retention cutoffs**; each arithmetic kind
repeats **80 rows, 49 live states and 3,941 retention cutoffs**. All four execution
oracles again report **zero soundness violations**. Fixture precision stays
**49/72**, zero partial/pending, with the exact arithmetic **20/40/32**,
relational **20/40/32** and equality **24/44/33** family observations.
Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**, including p5's existing single
partial observation. Native corpus counts remain Bootstrap **19/574**, p5
**39/4754** and Phaser **45/7725** in both modes, zero pruned; exact Data stays
**0/7 browser/CommonJS and 0/8 AMD**.

All **fourteen code/test paths** match committed HEAD, frozen input and final
devbox sources. Both actual emitted field/lifetime C++ artifacts use native
object accessors and contain no Script/VM context/value symbols or AOT runtime
calls. These hashes and output checks were independently rechecked against the
root's evidence parser and final devbox hash log. Evidence:
`/tmp/ctcompile-field-presence-{full.log,full-detail.log,evidence.json,postgate.log}`,
`-root-hashes.json`, `-final-field.cpp` and `-final-lifetime.cpp`.

Remaining producer boundaries include primitive conversions, dynamic
Add/Concat and BigInt categories, followed by loops, callee summaries and native
lifetime/effect consumers. Each retains its existing conservative behavior.

## Primitive Add and Concat origins, 2026-09-08

This continues the producer boundary in **`56569199`** and the **`bf2fd02e`**
handoff. Dynamic Add and Concat now require **both original operand origins**
to independently prove Undefined, Null, Boolean, Number or String. The existing
bounded path state follows saved reads through later field overwrites/deletions
and checks every structural arm. A primitive result has no object alias, but
supplies no concrete Number/String tag, numeric value, literal property key or
branch-liveness fact. BigInt, object/array and opaque operands still refuse;
no annotation, host summary or observation supplies a missing origin proof.
The operand-role table and native admission remain unchanged.

The current VM's `Script/vm/coerce.cpp::binary_op` handles these two operations
separately. Add first calls `to_primitive` in source order, then either combines
primitive String bytes or performs static numeric addition. Concat applies
`to_string` directly to each input. The five admitted primitive categories
return before either String conversion can inspect an object or call user
code. String results are VM allocations and conversions may allocate C++
temporaries; this proof establishes neither absence nor success of allocation.
Add's unconditional `to_primitive` depth guard may still throw an unrelated
RangeError. The preceding Error/stack/unwind audit applies: this complete query
rejects calls, handlers, closure capture and publication, so its fresh local
identities cannot become retained through that guard. This remains **retention
only**, with no normal-completion, no-throw or native effect guarantee. Concat's
primitive path does not use that guard. No deep-stack execution is claimed.

Before production changed, local Node and the parent's serialized current
VM `ctcompile-test-native-reference` both measured **`trace=4095`** on twelve
checks. These distinguish Number versus String addition, operand order, NaN,
signed zero, Null/Undefined/Boolean formatting, Unicode bytes, infinity,
object `valueOf` for Add versus `toString` for Concat, the two thrown conversion
values, successful BigInt operations and a mixed BigInt TypeError. The object
and BigInt observations explain the conservative cut; they do not prove future
operands primitive. Source: `/tmp/ctcompile-escape-add-concat-semantics.js`.
No browser or runtime source changed.

The independent two-operand unit table preserves all historical rows and adds
four cross-producer cases: numeric Add, Concat and each String-producing Add
operand order. It passes **84 rows per kind**, including independent Add and Concat
runs. Each of the seven dynamic binary kinds passes **49 live states and 4,127
retention cutoffs**; each of Eq/Lt/Le/Gt/Ge passes **34 live states and 3,527
retention cutoffs**. Tests retain separate live operand/kind/constant mutations
under forged completion/confinement reports, every incomplete contents budget
and the exact **64-work** wide snapshot check. Unknown inputs, saved
BigInt/object origins, late publication/calls/throws/handlers and nonliteral
result keys remain negative.

Seven additive source functions measure **28 sites, 56 instances and 44
retained**: saved String addition after BigInt replacement, numeric addition,
saved Null/Undefined template inputs after object replacement, separate opaque
Add-only and Concat-only refusals, successful BigInt controls, and an independently
returned saved child. Every historical JavaScript byte and oracle-family
expectation is preserved. The new checker joins recording and compiler claims
by exact program/function/pc; only the three independently proved children gain
confinement. All four escape oracles report **zero soundness violations**.
Expanded-fixture precision is **52/78**, with zero partial/pending claims,
versus the preceding **49/72**. This adds three proved-confined and six
observed-confined sites; it is added source coverage, with no precision gain
claimed on historical source. Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**,
including p5's existing single partial observation. The historical arithmetic
**20/40/32**, relational **20/40/32**, equality **24/44/33** and all older family
expectations pass unchanged.

Local Node executes **79 combined fixture calls**, and **72 new plus 267
historical observation mutations** discriminate. Assertions check Number,
String, BigInt and NaN results, operand order, saved primitive bytes, independent
source/target identities, selected aliases and retained children. Separate
opaque-object calls observe two `valueOf` and two `toString` calls; throwing
versions stop after one conversion each. Evidence:
`/tmp/ctcompile-escape-add-concat-node.{py,js,json}`. Homebrew clang-format
**22.1.8** and changed-path whitespace checks pass; the complete formatter
checks all **745 files**.

The first 48-step devbox build compiles the new production and array tests,
and both selected units pass, but its target list omits `escape-claims`.
That older executable produces Stored claims for all three new children,
while the recorder correctly observes their confinement: the initial focused
result is **2/3 in 0.44 seconds**, with zero oracle violations but the expected
precision assertion failing. Rebuilding all escape targets relinks the claims
executable. No source, proof or expectation changes are needed. The corrected
**15-step build has zero warnings**, and focused **15/15 CTests pass in 18.85
seconds**, including **all eight escape CTests**. The source-family and unit
measurements above are from this corrected gate. Evidence:
`/tmp/ctcompile-comparison-identity-{build3.log,focused.log,build4.log,focused2.log}`
and `-format22.log`.

The Add/Concat increment is committed as **`267545cd`**. The full **245-step
generated devbox build completes with zero warnings**. Its CTest result is
**511/517 in 1313.13 seconds**: **371/372 compiler tests** and **140/145 browser
tests** pass. The compiler failure is `ctcompile_lit`, where **164/165 cases
pass in 658.70 seconds** (CTest **658.90 seconds**). The unchanged
`object-argument-refusals.mlir` catches an unrelated native identity census
clearing an earlier, more specific refusal diagnostic. **`8e603ce5`** limits
that cleanup to the census's own strict-comparison reasons; no source test,
proof semantics or escape implementation changes. The complete corrected lit
rerun passes **165/165 in 658.71 seconds** (CTest **658.78 seconds**, command
elapsed **658.79 seconds**). Thus all **372 compiler tests have passing results
across the initial run and corrected rerun**. This is not a second complete
517-test CTest run. The five browser failures remain `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors`. Exception recovery passes in
**1.18 seconds** in the full run.

All eight escape CTests pass again in the full run. The comparison kinds repeat
**84 rows, 34 live states and 3,527 retention cutoffs**; all seven dynamic binary
kinds repeat **84 rows, 49 live states and 4,127 retention cutoffs**. Four
execution oracles again report **zero soundness violations**. Fixture precision
stays **52/78**, zero partial/pending, with exact new **28/56/44** observations
and unchanged historical families. Bootstrap/p5/Phaser remain **0/64, 0/16,
0/20**, including p5's existing single partial observation. Native corpus
counts remain Bootstrap **19/574**, p5 **39/4754** and Phaser **45/7725** in
both modes, zero pruned; exact Data stays **0/7 browser/CommonJS and 0/8 AMD**.
These native counts are separate from the escape proof's additional coverage.

All **thirteen final code/test paths** match committed HEAD, final frozen
input and final devbox sources. Both actual emitted distinct-identity and
lifetime C++ artifacts contain no Script/VM context/value or AOT runtime
symbols. The distinct-identity method retains three fresh allocations and
field writes, an owning saved Map read, two Map writes, deletion and a runtime
identity comparison. The sanitizer observer remains separate test code.
Evidence: `/tmp/ctcompile-comparison-identity-{full.log,full-detail.log,lit-rerun.log,lit-detail.log,evidence.json}`,
`-final-hashes.json`, `-final-remote-hashes.log`, `-final-distinct.cpp` and
`-final-lifetime.cpp`. The full-run failure and corrected rerun are retained as
separate evidence, and the escape precision claim is unchanged.

Remaining producer boundaries include primitive conversions and BigInt
categories; loops, callee summaries and native lifetime/effect consumers also
retain their existing conservative behavior.
