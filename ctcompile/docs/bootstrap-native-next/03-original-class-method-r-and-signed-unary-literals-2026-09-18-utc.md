[Back to bootstrap-native-next.md](../bootstrap-native-next.md)

## Original class-method r and signed unary literals, 2026-09-18 UTC

**aa5aa952** closes the original class-method `r(null)` boundary by keeping
literal Number/empty-String keys distinct from unknown keys in the conservative
prototype census. The existing constructor lift and scalar caller proof now
produce **4 native executions**, both C++ layouts and GCC/Clang, **a=7** throughout.
An empty-string control adds **4 executions**; six collision/dynamic/large-key
controls refuse. Original source, unoptimized and unprepared controls remain.
No browser file or emitted runtime dependency was added. Number-key proof stays
below magnitude **1e15** pending the shared formatter's int64 cast-order fix,
reported to the browser agent through AGENT-SYNC.

**1dd56617** independently preserves signed unary literal snapshots through the
shared escape transfer. Exact arrays CTest passes **1/1 (0.93s)**: dense/induction/
structured **585 / 559 / 273** rows. Signed-unary oracle: **20 sites / 8 sound /
zero violations / 8 of 12 precision**. Four selected lit cases pass **4/4 (160.75s total)**, including class initialization
(**120 source / 292 native** plus ordinary/helper/refusal controls), prototype
scalars (**48 native / 2 refusals**) and constructor controls (**8 native /
18 refusals**). Seven selected input hashes match the devbox; workflow exit **0**.
HANDOFF records exact targets, retries, hashes and skipped coverage. Pinned
formatting retains **26 existing diagnostics**; stable **916/109/105**, changed
pinned formatting, Black, syntax and diff checks pass. Full suites and broad
matrices were skipped. Evidence: `/tmp/ctcompile-class-guards/`.

**Next:** Config/H callable-holder identity/order and class/DOM contract
composition. The full original r/M/F/H/W specimen still refuses preparation;
H.getDataAttribute's lexical-this arrow is outside the class helper proof.
Its public DOM implementation already exists. Iterator/destructuring,
RegExp/TypeError exits, remaining Config helpers, inheritance, full H Unicode,
retained callbacks and the application driver remain open. Historical broad
Bootstrap counts were not replayed.

## Closed scalar guards and original Bootstrap helpers, 2026-09-18 UTC

**b680e2a0** adds complete private-caller scalar facts to the existing guard
folder, including after closure lifting; implicit/escaping/alternate callers and
CFG/SCF entries remain excluded. **24832a56** executes the original Bootstrap r
body with null input through a closed declaration and a local closure: **8 native
executions**, optimized, both C++ layouts and GCC/Clang, **a=7** throughout.
The immediate global form and original class-method probe remain refused. The
new complete original r/M/F/H/W defaults specimen returns **a=7** but still fails
class preparation's effect census; no body was removed.

Independent **3f798821** proves bounded negative-one Number power parity.
Exact arrays CTest **1/1 (0.92s)** and three power oracles **3/3 (0.22s)** pass;
each oracle has **26 sites / zero violations / 11 of 15 precision**. Scalar
precomputation **3/3**, defaults **1/1**, primitive fields and final class case
**1/1 (149.98s)** pass. Class counts: **120 source / 288 native**, plus the new
**8 r executions**, ordinary executions and refusal controls recorded in HANDOFF.
Ten selected source hashes match the devbox. Required pinned formatting retains
**26 pre-existing diagnostics**; stable **916/109/105** formatting, Black, syntax
and diff checks pass. Full suites and broad matrices were skipped; browser source
and runtime semantics were unchanged. HANDOFF records exact targets, intermediate
failures and access repair. Evidence: `/tmp/ctcompile-r-guards/`.

**Next:** original r's class/prototype caller proof and Config/H DOM-provider
composition; then iterator/destructuring and RegExp/TypeError exits, inheritance,
full H Unicode keys, retained callbacks and the application driver. General Number
powers remain outside the escape proof. Historical whole-Bootstrap counts were
not replayed.

## Recovered declaration borrows and bounded powers, 2026-09-18 UTC

**`fe2f5b7f`** finishes the interrupted hoisted-helper object-borrow draft:
the existing closed declaration and all-caller census now carry forwarded object
arguments. Saved strings survive mutation; mixed/short calls, callable escape,
replacement and alias deletion retain refusal controls. The unchanged
`local-helper-order` class case now executes natively (**122**).

**`7133398b`** separately proves exact bounded Number zero/one-base powers,
preserving signed-zero identity, saved lengths and CFG/SCF invariance.
Focused array CTest **1/1 (1.74s)**; two power lit cases **2/2 (0.20s)**,
each **26 sites / zero violations / 11 of 15 precision**. Primitive/class lit
**2/2 (151.50s)**: **152 / 288 native executions**, plus the ordinary/refusal
controls recorded in HANDOFF. Explicit six-target build: **38 actions**;
workflow **0**, **933 input hashes** verified. Stable format **916/109/105** passes;
required pinned check retains the same **26 diagnostics in nine unchanged files**.
Full suites and broad matrices were skipped; no browser source changed.
SSH access was repaired with the authorized start/allow-ip workflow.
Evidence: `/tmp/ctcompile-native-close/`.

**Next:** original r's nullable property flow still lacks a closed shape.
Original r+W Config still reaches missing **H**; compose class and DOM proof
authority through the existing HostContract passes and public DOM helpers.
Object.entries/destructuring, RegExp/TypeError exits, inheritance, full H Unicode
keys, retained callbacks and the application driver remain unfinished.
Whole-Bootstrap/Button/Data counts remain historical.

## Local helper boundary measured, 2026-09-18 UTC

**`27cb4729`** adds bounded exact parameterized-helper proof to class preparation,
including closed hoisted globals, structured bodies, unused arrow receivers and
literal Number keys. Scalar/branch/argument-order cases execute natively; mutable
object arguments and original `r` property reads remain explicit native refusals.
The complete W-only Config fixture still refuses `r`; a new source-pinned original
`r` + W fixture reaches **`H`**, with **a=7** in Node/interpreter for both.

Exact class lit: **1/1 (148.54s), 119 source / 280 native executions**, plus
**16 ordinary executions**, **14 prepared native refusals**, and the remaining
unprepared/preparation controls recorded in HANDOFF. New helper proof budget:
**419**. Independent **`725713cf`** adds bounded exponent-zero/one snapshots:
array CTest **1/1 (0.89s)**, two escape lit cases **2/2 (0.22s)**, new oracle
**26 sites / zero violations / 11 of 15 precision**. Final three-target rebuild
had no work; workflow **0**, **1,081 selected input hashes** verified. HANDOFF /
Current native work record intermediate failures, exact targets and formatting.
Full suites and broad matrices were skipped; no browser source changed.

**Next:** prove original `r` object argument shapes and compose original H's DOM
host contract with Config; then Object.entries/destructuring, RegExp/TypeError,
inheritance and DOM/default composition. Native throws, H Unicode keys, retained
callbacks and the application driver remain unfinished. Historical whole-Bootstrap
counts were not remeasured. Evidence: `/tmp/ctcompile-config-helpers/`.

## Unused getter chains closed, 2026-09-18 UTC

After **`5927fdb6`** recovered declared-Error getter preparation, **`a427abbe`**
removes unused throwing-getter chains in reverse dependency order while preserving
live direct calls. The source regression first reproduced retained dead Error
bodies; it now compiles with both policies/layouts and GCC/Clang. Ordinary-method
Error construction remains outside this getter proof.

Exact class lit passes **1/1 (137.06s): 109 source observations / 256 native
executions**, plus ten native throw refusals; proof budgets are unchanged.
Independent **`bf363c05`** adds signed Shr/UShr escape snapshots: exact array CTest
**1/1 (0.89s)**, three selected escape lit cases **3/3 (0.33s)**, new oracle
**26 sites / zero violations / 11 of 15 precision**. HANDOFF and Current native
work record exact builds, corrected historical expectation, hashes and formatting.
Full suites and broad matrices were skipped; no browser source changed.

Complete original Config still returns **a=7** and refuses **`r` in
`_mergeConfigObj`**. Local helper identity/arguments and H composition are next;
Object.entries/destructuring, RegExp/TypeError exits, inheritance/DOM defaults,
full H Unicode keys, retained callbacks and the application driver remain open.
Evidence: `/tmp/ctcompile-error-closeout/`.

## Recovered Error getter boundary, 2026-09-18 UTC

**`5927fdb6`** preserves literal or declared-Error static getter exits through
capture-free direct calls, including transitive getter dependencies. Error needs
one literal string and an exact constructor/new-target identity; escaping payloads,
ambient effects, coercion and replacement refuse. Native throw representation
remains unproved. Exact class lit passes **1/1 (133.69s): 107 source observations /
248 native executions**, with ten preserved native throw refusals. The explicit
three-target rebuild had no work; all **1,290 hashes** match. Full suites and
broad matrices were skipped; HANDOFF records exact checks and formatter baseline.

Complete original Config still returns **a=7** and now refuses **`r` in
`_mergeConfigObj`**, after passing the throwing-NAME proof. Composable local helper
and H contracts, iterator/type-check/TypeError exits, inheritance and DOM defaults
remain. Signed-bitwise recovery **`2904c366`** and its measured oracle are recorded
in Current native work. Evidence: `/tmp/ctcompile-error-closeout/`.

## Recovered literal-method throws, 2026-09-18 UTC

**`552db1c7`** preserves outer CFG exits and literal primitive throws in proved
local methods during class preparation. Every body retains the full effect and
receiver checks; object/parameter throws and ambient effects refuse. Native
lowering still refuses these exceptional paths. **`3d0af951`** separately
preserves bounded signed Number BitNot snapshots for array retention.

Exact class lit passes **1/1 (135.10s): 98 source observations / 240 native
executions / 196 unprepared / 128 preparation refusals**, plus **16 ordinary
executions / 20 refusals** and **four prepared throw refusals**. The first class
run exposed a test assertion confusing unused key literals with property reads;
only that assertion changed before the passing rerun. Escape array CTest **1/1
(0.88s)** and selected `signed-bitnot` / `signed-subtraction` / `signed-unary`
lit **3/3 (0.32s)** pass. The BitNot oracle reports **26 sites / 11 sound /
zero violations / 11 of 15 precision**. Exact build, formatter baseline, hashes
and skipped broad suites are recorded in HANDOFF; no browser source changed.

**Next:** complete original Config still returns **a=7** in Node/interpreter,
but preparation now refuses its `NAME` getter's `new Error(...)` as **static
getter body is not a closed expression**. Prove Error identity/effects and
throwing getter expansion separately; original Object.entries/destructuring,
RegExp/type checking and TypeError exits still follow. Literal-method throw
completion, inheritance, DOM/default composition, H Unicode keys, callbacks and
the application driver remain open. Whole-Bootstrap counts remain historical.
Evidence: `/tmp/ctcompile-throw-finalize/`.

## Local constructor getter reads, 2026-09-18 UTC

**`f5795916`** compiles proved local `this.constructor.Default` / `DefaultType`
reads and equivalent exact-instance reads through existing getter expansion.
Fresh allocation identity survives each read and getter dependency; reads inside
normalized dispatch follow their private clones. Replacement constructor returns,
constructor mutation, identity escape and inherited classes refuse.

Exact class lit passes **1/1 (127.94s): 94 source observations / 240 native
executions / 188 unprepared / 122 preparation refusals**, plus **16 ordinary
executions / 20 refusals**. Both policies/layouts and GCC/Clang pass; dispatch
getter proof first completes at **1,362 steps**. Explicit three-target build
passes (223 dependency rebuild actions after another worktree's devbox sync),
with all **1,290 hashes** verified and workflow exit **0**. The initial getter
gate, replacement-object probe/guard, formatter baseline and exact skipped
coverage are recorded in HANDOFF. Full suites and broad matrices were not run.

Next: original Config's `_typeCheckConfig` still has three outer blocks and the
complete class remains an explicit refusal. Object.entries/destructuring,
RegExp/type checking, throwing NAME and exception exits need proof; inherited
receivers and DOM/default composition remain. Full H Unicode keys, callbacks and
the application driver remain open. The disjoint public UTF-16 extraction
`codex-unicode-core` / `ecca5b66` is tracked in AGENT-SYNC; Unicode casing and
normalized-key proof are still separate. Whole-Bootstrap counts remain historical.
Evidence: `/tmp/ctcompile-receiver-defaults/`.

## Recovered counters and signed subtraction, 2026-09-18 UTC

**`55e13932`** admits method-only static numeric counters; **`c0b82039`**
preserves exact negative-left Number subtraction snapshots. Both interrupted
drafts are now committed. Focused array CTest **1/1 (0.87s)**, escape lit
`signed-subtraction` / `negative-add` / `sub-snapshot` **3/3 (0.32s)** and class
lit **1/1 (117.62s)** pass. Class: **86 source observations / 216 native
executions / 172 unprepared / 113 preparation refusals**, plus **16 ordinary
executions / 20 refusals**. Subtraction oracle: **26 sites / 11 sound / zero
violations / 11 of 15 precision**. Explicit six-target build: eight actions;
all 1,290 input hashes match. Formatter baseline and skipped broad suites are
recorded in HANDOFF; no browser source changed.

Next: exact local constructor/default getter reads, then original Config's
iterator/throw exits, throwing NAME, inheritance and DOM/default composition.
The full original Config still refuses complete capture-free source functions.
Full H Unicode keys, callbacks and the application driver remain open; historical
whole-Bootstrap counts were not rerun. Evidence: `/tmp/ctcompile-config-recovery/`.

## Current boundary: method exit dispatch recovery, 2026-09-18

Recovered **`ab646056` / `63a021c1` / `2167f4d2`**: exact negative Number sums,
integer/index poison and while backedges, and budgeted normalization of proved
method exit dispatch. Complete effect/receiver checks remain; iterator/throwing
call authority is unchanged. The interrupted full original Config source is now
an executable refusal regression: Node/interpreter return **a=7**, every function
imports, and `_typeCheckConfig` still has three outer blocks.

Fresh exact class lit passes **1/1 (108.52s): 83 source observations / 200 native
executions / 166 unprepared / 108 preparation refusals**, plus **16 ordinary
executions / 20 refusals**. The explicit three-target build had no work. Recovered
escape CTest/lit and scalar results, initial failures and formatter baseline are
recorded in HANDOFF. No broad compiler/browser suites were run by this task.
Bootstrap **19/574 / 0 of 47 globals**, Button **4/86 / 22 observations** and Data
**7/7** remain historical.

**Next:** the original iterator counter uses `ctjs.binary_static add`, currently
missing from the ordinary-method effect census. Numeric increment/decrement is a
small prerequisite; exception/iterator proof, throwing `NAME`, exact constructor
provenance, inheritance and DOM/default composition still follow. Full H Unicode
keys, retained callbacks and the application driver remain open.

## Current boundary: structured class methods, 2026-09-18

**`9105a656`** admits structured branches and loops in proved ordinary local class
methods, including mutual method calls, through the existing receiver and complete
source-effect proofs. Constructors, getter expansion and setup stay linear;
functions still require one outer block. Unknown regions, ambient calls in nested
arms and method replacement refuse. No emitter or browser runtime helper was added.

Focused class lit passes **1/1 (103.87s): 78 Node/interpreter observations / 192
native executions / 156 unprepared / 100 preparation refusals**, plus **16 ordinary
constructor executions / 20 refusals**, both policies/layouts and GCC/Clang. The
nested-loop source's first complete budget is **278**. The previous mutual-method
cycle source remains unchanged and now executes natively.

**`b21653b9`** independently adds exact signed division/remainder snapshots.
Array CTest passes **1/1 (0.84s)**; three exact escape lit cases pass **3/3 (0.32s)**.
The new oracle measures **26 sites / 11 sound / zero violations / 11 of 15 precision**.
Four newly valid historical length controls retain their bodies with corrected
expectations. Both workflows verified **1,821 local/remote hashes** before docs.
Pinned formatting retains the known **26 diagnostics in nine unchanged files**;
stable formatting and changed-file checks pass. Full compiler/browser suites and
broad corpus/native matrices were not run by this task. No browser source changed.

**Next:** the complete original Config defaults-only probe still returns **a=7**
in Node/interpreter and imports every function, but refuses **class initialization
requires complete capture-free source functions**: `_typeCheckConfig$9` still has
three outer blocks after SCF lifting. Exceptional/iterator method handling,
throwing `NAME`, inherited `this.constructor` and DOM/default composition remain.
Full H needs its public UTF-16/Unicode case seam and normalized-key proof; retained
callbacks and the application driver are also open. Bootstrap **19/574 / 0 of 47
globals**, Button **4/86 / 22 observations** and Data **7/7** are historical, not
remeasured. Exact gates and failure recovery: HANDOFF and
`/tmp/ctcompile-structured-focused/`.

## Previous boundary: fresh Config defaults, 2026-09-17

**`f1450d60`** compiles the isolated original Bootstrap `Default` and `DefaultType`
empty getter bodies through the existing local class proof. Every getter read,
including a dependency read, gets a fresh allocation. Fully expanded getter
functions are removed only after complete closure and symbol-use checks. Getter references
from module, function and operation attributes, unresolved targets and incomplete
budgets refuse before mutation. General object returns, nonempty getter literals/stores and
inherited receivers remain outside this proof.

The focused class lit case passes **1/1 (92.67s): 72 Node/interpreter source
observations / 168 native executions / 144 unprepared / 93 preparation refusals**,
plus **16 ordinary constructed-method executions / 20 refusals**, both policies,
layouts and GCC/Clang. The new dependency's first complete budget is **267**.
The initial unused-getter return refusal led to the complete definition-removal
proof; no source case was dropped or generic object-return admission relaxed.

**`fe56eae8`** independently preserves bounded signed Number products. Exact array
CTest passes **1/1 (0.84s)** and four signed-number lit cases pass. Its new oracle
measures **26 sites / 11 sound / zero violations / 11 of 15 precision**. Explicit
build targets passed; all **1,820 hashes** match locally/remotely before docs.
Stable formatting passes; pinned formatting retains **26 diagnostics in nine
unchanged files**. Full suites and broad corpus/native/WPT/test262 runs were not run.

**Next:** the complete original Config class in a defaults-only wrapper still
refuses preparation: **class initialization requires complete capture-free source
functions**. Node/interpreter both return **a=7**, and import skips no functions;
`_typeCheckConfig$9` retains three blocks after SCF lifting. Full Config still needs
structured/exceptional method proofs, throwing `NAME`, inherited receiver and
`this.constructor` handling, and DOM/default composition. Full H's public
UTF-16/Unicode case seam and normalized-key proof, retained callbacks and the
application driver also remain. No browser source or runtime semantics changed.
Bootstrap **19/574 / 0 of 47 globals**, Button **4/86 / 22 observations** and Data
**7/7** are historical, not remeasured. See HANDOFF and
`/tmp/ctcompile-config-focused/` for exact focused gates and failure/recovery evidence.

## Previous boundary: Unicode keys after filtered prefix assignments, 2026-09-17

**`08a812a6`** finishes the interrupted dynamic-assignment branch: `73ae525d`,
`88e9f626` and `1d363a7b` are integrated with browser `0337cd15`. Recovery used
its passing **377-target build / 1 host CTest / 3 focused lit cases**, with all
**1,818 input hashes** reverified. The inherited full CTest was deliberately
cancelled under the new focused policy; it is not a full-suite pass.

**`58cb0b3a`** now compiles the original filtered
`result[n.replace(/^bs/, '')] = M(t.dataset[n])`. The callback proves that selected
keys start with `bs`; removing that prefix preserves uniqueness. The existing
single traversal, sole writer and final-own-data proof covers the possible
`__proto__` setter. This is separate from dataset membership; transformed dataset
lookups, weak/unfiltered predicates, repeated stripping/writes and incomplete
budgets refuse. Existing native String/JSON helpers are reused; no browser or
Script/VM/GC implementation was added.

The targeted build and exact host-contract/array CTests pass **2/2 (1.34s)**.
The assignment lit replay passes **1/1 (181.34s): nine sources / 210 Node-VM
observations plus Node accessor traces / eight GCC-Clang binaries / 196 refusals**,
with lifetime sanitizers. A preceding sanitizer compile timeout was resolved by
turning off inlining for that combined test compilation; all source cases,
`-O1`, sanitizer options and the 120-second timeout remain. Three focused escape
lit cases also pass for **`08d849c2`** bounded Add cancellation: its new oracle
measures **20 sites / eight sound / zero violations / eight of 12 precision**.
All **1,819 hashes** match locally/remotely before docs. Stable formatting passes;
pinned formatting retains **26 diagnostics in nine unchanged files**.

**Next:** one original full-H probe (`ctbrowser-dom-v1`, optimization off) still
refuses the unsupported DOM member read. The public UTF-16/Unicode-case seam for
`charAt(0).toLowerCase() + slice(1)` and its result-key proof remain. Ordinary key
collisions must retain ordered last-write values; prototype-key skipping still
requires its source proof. Coordinate the browser-owned seam through AGENT-SYNC.
Config/inheritance/defaults, retained callbacks and the application driver remain
open. Full CTest/lit and broad native/corpus/WPT/test262 replays were skipped.
Bootstrap **19/574 / 0 of 47 globals**, Button **4/86 / 22 observations**, and Data
session **7/7** are historical, not refreshed measurements. See HANDOFF and
`/tmp/ctcompile-prefix-focused/` for exact checks, failure/recovery evidence and
source manifests. Older sections below are historical checkpoints.


## Previous boundary: Unicode keys after ordered result writes, 2026-09-17

**`e8d5aae0` / `1576568d`** complete the next documented fresh-object seam:
receiver-only source writes survive structured branches/loops, and the complete
DOM proof admits constant non-`__proto__` String-key assignments with owning values.
Every write must finish before its target is observed; snapshots inside a mutation
loop refuse. Assignment and spread share ordered member updates. Results remain
public owning `ctbrowser::json_value` trees without Script/VM/GC dependencies.

The new assignment driver passes **six sources / 111 Node-VM observations / eight
GCC-Clang binaries / 88 refusals**, both providers/policies/layouts and lifetime
sanitizers. It also checks 111 Node accessor traces. Both earlier JSON refusal
sources retain their exact bodies as positives. Parsed nested trees keep the public
Core representation; the observation adapter checks raw result-key order before
applying JS JSON enumeration. No browser/runtime semantics changed.

**`eec9fcb9`** independently preserves exact negative Number Sub snapshots:
**20 source sites / eight sound / zero violations / eight of 12 precision**, plus
27 CFG/SCF rows. String coercions and unbounded/mutable latches remain refused.

Full **310/310 CTests (2087.43s) / 260/260 lit (1822.86s)** pass, with no skips;
all **1,796 input hashes** match locally/remotely. Fresh Bootstrap stays **19/574
native / 0 of 47 globals**, Button **4/86 / 22 lifecycle observations**, and the
original DOM Data session passes its **7/7** admission and lifetime assertions.
Stable formatting passes **890/108/105** files; pinned formatting retains the
same **nine unchanged files / 26 diagnostics**. No WPT/test262 corpus rerun.

**Exact next:** 44 admission probes show fixed result writes compile one function
and original M fixed-key dataset loops compile two, in all four provider/policy
modes. Full H advances to the same unsupported property read as isolated Unicode
`charAt(0).toLowerCase() + slice(1)`. Dynamic and `__proto__` writes retain their
explicit final assignment diagnostic. The current VM's byte/ASCII String behavior
cannot supply JavaScript Unicode semantics; the previous É/İ discrepancy was not
remeasured. Coordinate a public non-VM Unicode seam with the browser work.

A Node-only review suggests a later narrow proof for original H's single possible
`bs__proto__` key and final own-data observations. Generic repeated prototype writes
or previous spreads defeat that shortcut; it is not implemented or native-validated.
Full H, Config/inheritance/defaults, retained callbacks and the application driver
remain open. Claude's round-six/seven runtime changes are outside this frozen oracle.
See [HANDOFF](../HANDOFF.md) and `/tmp/ctcompile-assignment/` for measured results,
failed fixtures, source hashes, generated C++, probes and prototype counterexamples.

## Previous boundary: result assignment after guarded M loops, 2026-09-17

**`0a186321`** proves helper/capture initialization before enclosing structured
branches and loops, preserving actual arguments and URI/JSON exception continuations.
**`fe650e1c`** folds original element guards after existing proved helper/cell
expansion and before iterator preparation. Only validated nonnullable element
identities seed truth facts; nullable reads, joins and unproved cells do not.
Both interrupted drafts came from the 10:52:21 recovery / 10:55:26 abandonment
journal. No browser implementation or runtime semantics changed.

Full **310/310 CTests (2076.81s) / 258/258 lit (1815.84s)** pass with no skips;
all **1,793 input hashes** matched locally/remotely. Then test-only **`ff594bb6`**
added original M inside guarded value/attribute loops: updated dataset lit
**1/1 (52.75s), 28 sources / 112 Node-VM observations / eight binaries / 432
refusals**, including HTML/SVG and lifetime sanitization. Earlier cases remain intact.
New observations check result types; existing JSON tests check full returned values.
Updated input hashes also match. Stable formatting passes; pinned format retains
nine unchanged files / 26 diagnostics.

Fresh Bootstrap remains **19/574 native, 0/47 globals**, the original DOM Data
session **7/7**, and Button **4/86 / 22 lifecycle observations**. The unadapted
CommonJS Data census is separately **0/7**. The guard and M-loop seams are proved,
but full H still refuses **DOM helper branch contains an unproved local identity**.
Next: extend fresh-object/source-use and DOM SetProperty proofs together for
ordered result assignment, including collisions and the inherited `__proto__`
setter. Static/dynamic writes currently refuse identity/order; JSON spread does
not prove assignment semantics. Unicode key normalization still refuses and its
previous É/İ Node/VM discrepancy was not remeasured. Config/inheritance/defaults,
callbacks and the application driver remain open.

Evidence: `/tmp/ctcompile-guard-finish/`, including 36 isolated admission probes,
full reports, generated C++, manifests and review. Claude's pending round-six
runtime changes were outside this frozen tree. See HANDOFF for the complete measured
continuation; older sections below are historical.

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
`/tmp/ctcompile-dataset-iteration/`; details in [HANDOFF](../HANDOFF.md).

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
C++ helper header. [The DOM contract](../native-dom-entry.md) describes admission;
[HANDOFF](../HANDOFF.md) records integration and evidence.

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
and mutation/privacy controls. See [HANDOFF](../HANDOFF.md) for the final gate and
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
prepare original Button. [Host contract details](../native-host-slots.md) describe the
input and its limits; [HANDOFF](../HANDOFF.md) records the full-gate status.

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
and exceptions. See [HANDOFF](../HANDOFF.md) for measured gates.

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
