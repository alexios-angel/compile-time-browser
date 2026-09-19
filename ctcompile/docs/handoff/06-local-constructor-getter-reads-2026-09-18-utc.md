[Back to HANDOFF.md](../HANDOFF.md)

## Local constructor getter reads, 2026-09-18 UTC

**`f5795916`** follows the recovered **`c0b82039` / `55e13932`** drafts below.
Constant getter reads through `this.constructor` or an exact local instance now
reuse the existing static-getter proof and expansion. The constructor identity
may feed only proved local getter reads and inert roots. Every read preserves
its own fresh allocation, including getter dependencies. Reads in normalized
method dispatch follow their private clones. No constructor/prototype value or
Script/VM/GC dependency is emitted.

Instance reads require a primitive constructor return: returning a replacement
object changes which constructor `new` exposes. The new source regression returns
**a=1** in Node/interpreter and now refuses preparation with that diagnostic.
Constructor writes, writes through the constructor, identity escape and inherited
classes remain refused, including effects in uncalled methods. Constructors and
getter bodies remain linear; this adds no iterator or throwing-call authority.

Final focused devbox gate: explicit `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference` build passes; exact
`CTNative/Lowering/Objects/class-initialization.mlir` **1/1 (127.94s)**.
It measures **94 source observations / 240 native executions / 188 unprepared /
122 preparation refusals**, plus **16 ordinary executions / 20 refusals**,
with both optimization policies, explicit/deduced C++, GCC and Clang. Getter
reads inside dispatch first complete at budget **1,362**. All **1,290 input
hashes** match locally/remotely; workflow exits **0**. The three-target build
performed **223 Ninja actions** after restoring the shared devbox source from
another worktree; this was a dependency rebuild, not a broad test run.

The first getter draft passed the same focused case **1/1 (131.18s)** with
**93 source observations / 240 native / 186 unprepared / 121 preparation
refusals**, after three build actions. Review then added the replacement-object
guard. Its first probe hit an unrelated conditional-wrapper refusal; the linear
comparison isolated the issue and confirmed that the pre-fix draft incorrectly
accepted preparation. Only the affected class case was rerun after the guard.

Required pinned formatting retains **26 diagnostics in nine unchanged files**;
stable formatting passes **912 C++ / 108 Python / 105 web files**, and the final
changed-C++ pinned check, Black and `git diff --check` pass. Full CTest/lit, broad
corpus/native matrices, WPT and test262 were skipped. No browser source or runtime
semantics changed in this task. Evidence: `/tmp/ctcompile-receiver-defaults/`,
including both gate logs/manifests, the probe and inspected emitted C++.

**Exact next:** the complete original Config (`W`) still returns **a=7** in
Node/interpreter and refuses complete capture-free source functions:
`_typeCheckConfig` retains three outer blocks. Its Object.entries/destructuring
iterator, RegExp/type checking and throw exits, plus throwing `NAME`, need their
own proof. Inherited receivers and DOM/default composition follow. Full H Unicode
key normalization, retained callbacks and the application driver remain open.
The disjoint **`codex-unicode-core` / `ecca5b66`** extraction is being integrated
by its own session; consult AGENT-SYNC for its status. It does not yet prove
Unicode casing or normalized dataset keys. Bootstrap **19/574 / 0 of 47 globals**,
Button **4/86 / 22 observations** and Data **7/7** remain historical measurements.

## Recovered method counters and signed subtraction, 2026-09-18 UTC

Resumed the seven dirty ctcompile paths at `6d6dcfbf` from the
**01:35:20 / 01:38:50 AGENT-SYNC drafts**, abandoned at **01:38:57**. Finished
both before new implementation. September 7 WIP is already an ancestor.

**`55e13932`** admits static numeric counters in proved ordinary class methods.
Increment/decrement loops retain break/continue/return dispatch; native admission
still requires Number operands. The whole-source census still rejects an ambient
call in an uncalled counter method. **`c0b82039`** preserves exact negative-left
Number subtraction snapshots, including bounded signed cancellation, source-length
mutation and CFG/SCF transport. Negative magnitudes never become own-index facts;
coercion, unknown inputs, overflow and unstable latches remain refused.

Focused devbox validation: explicit `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle` build passed in
**eight Ninja actions**. Exact array CTest **1/1 (0.87s)**; exact escape lit
`signed-subtraction`, `negative-add`, `sub-snapshot` **3/3 (0.32s)**; exact class
lit **1/1 (117.62s)**. Class: **86 source observations / 216 native executions /
172 unprepared / 113 preparation refusals**, plus **16 ordinary executions /
20 refusals**; increment dispatch first complete budget **1,210**. Subtraction
oracle: **26 sites / 11 sound / zero violations / 11 of 15 precision**. Arrays:
**569 dense / 359 induction / 216 structured rows**, with **22,159 / 26,292 /
13,661** budget cutoffs. Gate exit **0**, all **1,290 input hashes** verified
locally and remotely before documentation edits.

Required pinned formatting retains **26 diagnostics in nine HEAD-identical files**;
stable formatting passes **912 C++ / 108 Python / 105 web files**, and
`git diff --check` passes. Full CTest/lit, broad corpus/native matrices, WPT and
test262 were skipped. No browser source or runtime semantics changed. Review
agents hit service limits; root completed the reviews locally. Evidence:
`/tmp/ctcompile-config-recovery/next-gate.log` and `next.sha256`.

**Next:** exact local `this.constructor.Default` / `DefaultType` reads can reuse
the static-getter proof. Reads inside normalized methods need clone remapping.
The complete original Config regression still returns **a=7** in Node/interpreter
and refuses complete capture-free source functions (`_typeCheckConfig` has three
outer blocks). Iterator/throw proof, throwing `NAME`, inheritance and DOM/default
composition remain, along with full H Unicode keys, retained callbacks and the
application driver. Bootstrap **19/574 / 0 of 47 globals**, Button **4/86 /
22 observations** and Data **7/7** remain historical measurements.

## Recovered method dispatch continuation, 2026-09-18 UTC

Resumed `2167f4d2`, the sole dirty `class_initialization.py`, and the
**01:19:28 / 01:22:57 AGENT-SYNC checkpoint and interruption**. September 7 WIP
is already an ancestor. The interrupted thread had landed **`ab646056`** (exact
negative Number sums), **`63a021c1`** (integer/index poison and carried while
backedges), and **`2167f4d2`** (proved local method exit dispatch).

Method break/continue/return dispatch is normalized on budgeted private clones
only after complete source effects and receiver uses pass. Existing exception
normalizers handle switches and unused/all-poison results. Throwing calls and
iterators gain no authority. Integer poison retains its control type; an inert
empty EmitC marker keeps upstream SCF conversion from losing an empty-after
while backedge. Neither change adds a native runtime dependency.

Recovered focused evidence in `/tmp/ctcompile-dispatch-focused/`: array CTest
**1/1 (0.86s)**; escape lit `negative-add`, `add-cancellation`, `signed-division`
**3/3 (0.34s)**; negative-Add oracle **20 sites / eight sound / zero violations /
eight of 12 precision**. Class lit passed with **82 source observations / 200
native executions / 164 unprepared / 107 preparation refusals**, plus **16 ordinary
constructor executions / 20 refusals**, and dispatch's first complete budget
**1,196**. The combined class/scalar run had one scalar pipeline failure; after
adding the missing index conversion pass, the exact scalar replay passed **1/1
(0.51s)**. The minimized empty-after loop also passed; its pre-fix run timed out.
These are recovered measurements, not suites rerun by this continuation.

Fresh recovery gate: explicit `ctjs-opt`, `ctjs-translate` and
`ctcompile-test-native-reference` build had no work; exact class lit passes
**1/1 (108.52s): 83 source observations / 200 native executions / 166 unprepared /
108 preparation refusals**, plus **16 ordinary executions / 20 refusals**.
Required pinned formatting retains **26 diagnostics in nine HEAD-identical files**;
stable formatting passes **912 C++ / 108 Python / 105 web files**, and changed-file
Black plus `git diff --check` pass. Full CTest/lit, broad corpus/native matrices,
WPT and test262 were skipped. No browser source or runtime semantics changed.
Fresh evidence: `/tmp/ctcompile-config-recovery/class-gate.log` (exit 0).

The full original Bootstrap Config (`W`) source is now a permanent refusal
regression. It retains all getters and methods, imports every function, and
returns **a=7** in Node/interpreter; preparation still requires complete
capture-free source functions because `_typeCheckConfig` has three outer blocks.
The next native prerequisite is the static numeric increment used by its
iterator counter, then exact local `this.constructor.Default` / `DefaultType` reads; the full class additionally needs iterator/exception proof,
throwing `NAME`, inheritance and DOM/default composition. Full H's Unicode key
seam, retained callbacks and the application driver remain open. Bootstrap
**19/574 / 0 of 47 globals**, Button **4/86 / 22 observations** and Data **7/7**
remain historical measurements.

## Structured class methods and signed division, 2026-09-18 UTC

Continued clean `b965aa43` and the **23:35:19 AGENT-SYNC completion handoff**.
The previous interrupted threads were landed; `codex-wip-20260907` exists but is
already an ancestor of `ctcompile-v1`. This session resumed the recorded Config
method boundary. Agents split escape work, fixtures and proof review; after service
limits, root completed the drafts. Two agents later reviewed the frozen diffs
without findings. No browser source or runtime semantics changed.

**`9105a656`** admits structured branches and loops only inside proved ordinary
local class methods. Both complete recursive operation censuses and all receiver
uses remain checked. Every function still needs one outer block; constructors,
getter expansion and class setup retain their linear-body restrictions. Unknown
regions, nested ambient calls and method replacement refuse before mutation.
Existing lowering emits the methods without new runtime helpers. The original
mutual-method recursion fixture is now an execution positive with its body intact.

**`b21653b9`** preserves exact bounded signed Number division and remainder
snapshots. Div requires an integral quotient and a nonzero divisor; Mod follows
the dividend's sign. Negative magnitudes remain separate from index facts, and
zero retains its original result identity. Coercion, unknown/out-of-domain inputs,
fractional quotients and unproved loop invariance still refuse.

Focused devbox validation:

- Explicit `ctjs-opt`, `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims` and `ctcompile-test-type-oracle` build: **nine Ninja
  actions**. The class build additionally requested `ctcompile-test-native-reference`
  and had no work. The corrected array test rebuilt in **two actions**.
- Exact `ctcompile_escape_analysis_arrays` CTest: **1/1 (0.84s)**.
- Exact escape lit `signed-division`, `signed-product` and `add-cancellation`:
  **3/3 (0.32s)**. New signed-division oracle: **26 sites / 11 sound / zero
  violations / 11 of 15 precision (73.3%)**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (103.87s)**,
  **78 Node/interpreter observations / 192 native executions / 156 unprepared /
  100 preparation refusals**, both policies, layouts and GCC/Clang. Ordinary
  constructor controls add **16 executions / 20 refusals**. The nested-loop
  source's first complete work budget is **278**.

The first array run failed **24 assertions across four historical controls**:
`-1 / -1` preserves length one, `-1 % -1` clears it, and zero divided/remaindered
by a negative nonzero Number stays zero. Their bodies and live-mutation checks
remain; only the obsolete refusal expectations changed. No production fix or
fixture deletion was needed. Escape lit did not run after that initial failure.

Both final workflows exit **0**. Each verified **1,821 local/remote input hashes**
before documentation edits. Stable formatting passed **912 C++ / 108 Python /
105 web files**; the later length-expectation edit also passed the changed-C++
check. Required `tools/format.sh --check` retains **26 pinned-formatter diagnostics
in nine files byte-identical to HEAD**, not a pinned-format pass. Black and
`git diff --check` pass. Full CTest, full lit, broad corpus/native matrices, WPT
and test262 were **not run by this task**. The initial shared-lock wait was for
the independent browser workflow; it is not compiler validation.

**Exact next:** the unchanged complete original Config (`W`) defaults-only probe
still returns **a=7** in Node and the interpreter, imports every function, and
refuses **class initialization requires complete capture-free source functions**.
`_typeCheckConfig$9` retains three outer blocks after SCF lifting. Full Config
needs the exceptional/iterator method proof, throwing `NAME`, inherited
`this.constructor` and DOM/default composition. Full H still needs the public
UTF-16/Unicode case seam and normalized-key proof. Retained callbacks and the
application driver remain open. Bootstrap **19/574 / 0 of 47 globals**, Button
**4/86 / 22 observations**, and Data **7/7** remain historical measurements.
Evidence: `/tmp/ctcompile-structured-focused/`, including the preserved initial
failure, final gate logs, both source manifests and original full-Config probe.

## Fresh Config defaults and signed Number products, 2026-09-17 UTC

Continued `1af71cf8` and the **17:03:14 AGENT-SYNC handoff**. The earlier
prefix-assignment recovery was complete; the frozen signed-product draft in the
**17:10:37 journal** was reviewed, corrected and committed before the next native
change. Independent agents handled escape proofs, Config fixtures and deletion
review; root reconciled and gated their changes.

**`fe56eae8`** preserves exact bounded signed Number multiplication snapshots.
Negative magnitudes stay separate from nonnegative index facts; the product must
fit the existing 32-bit domain. The original result remains the value identity,
including signed zero. Source-length mutation, both operand orders, negative times
negative, zero trips, retained/released children and CFG/SCF transport are covered.
Coercible Strings/BigInts, unknown operands, unstable latches and overflow refuse.

**`f1450d60`** expands local static getters returning fresh empty objects, including
the exact Bootstrap `Default` and `DefaultType` bodies. Each original read gets its
own allocation, also through getter dependencies. After all reads are expanded,
the pass removes only the proved getter definitions and their closure/descriptor
setup. A charged census checks symbol references in module attributes and the body;
remaining getter references and unresolved targets refuse before mutation. Numeric
function indices remain stable. This does not admit general escaping object
returns, getter stores/nonempty literals, inherited receivers or the full Config
class. The existing complete host identity, callable-use and budget checks remain.

Focused validation on the devbox:

- Explicit targets `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle`: eight initial Ninja actions; the corrected array
  test rebuilt in two actions. The final class-only three-target build used four
  actions, including pass declaration generation.
- Exact `ctcompile_escape_analysis_arrays` CTest: **1/1 (0.84s)**. It covers **567
  dense / 296 induction / 196 structured rows**, with **21,961 / 20,847 / 11,765**
  respective budget cutoffs.
- Exact escape lit cases `signed-product`, `add-cancellation`, `sub-snapshot` and
  `signed-unary`: **4/4**. The new oracle measures **26 sites / 11 sound / zero
  violations / 11 of 15 precision (73.3%)**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (92.67s)**,
  **72 source observations / 168 native executions / 144 unprepared refusals /
  93 preparation refusals**. Ordinary constructed-method controls add **16 native
  executions / 20 refusals**. Both optimization policies, explicit/deduced output,
  GCC/Clang, independent allocations and exact getter removal pass. The new
  dependency source's first complete budget is **267**.

The initial array failure was six assertions for one old refusal: `-1 * 0` now
correctly clears array length as `-0`; its original body remains as a positive.
The first class run exposed unused getter definitions that still failed ordinary
escaping-object admission; the complete deletion proof resolves that failure.
The earlier five-case lit run was **four passes / one failure (57.00s)**; only the
class case was replayed after its fix. No failure was hidden by dropping a fixture.

Final class workflow exits **0**, with all **1,820 input hashes** matching locally
and remotely before documentation edits. Stable formatting passes **912 C++ /
108 Python / 105 web files**. Required `tools/format.sh --check` still reports
**26 pinned-formatter diagnostics in nine files byte-identical to HEAD**; this is
not a pinned-format pass. Black, changed-C++ formatting and `git diff --check` pass.
Full CTest, full lit, broad corpus/native matrices, WPT and test262 were **not run**,
following the user's repeated focused-check instruction. No browser source changed.

**Exact next:** one fresh probe retains the complete original Config (`W`) class
body in a defaults-only wrapper. Node and the interpreter both return **a=7**;
import skips no functions, but preparation refuses **class initialization requires
complete capture-free source functions**. All imported functions are capture-free;
`_typeCheckConfig$9` still has three blocks after SCF lifting. Full Config needs a
proof for its structured/exceptional methods, throwing `NAME`, inherited
`this.constructor` and default/DOM composition. Do not merely relax that census.
Full H still needs the public UTF-16/Unicode case seam and normalized-key proof;
its last admission result is from the prior session, not remeasured here. Retained
callbacks and the application driver remain open. Signed division/remainder
snapshots are an independent next escape item. Bootstrap **19/574 / 0 of 47 globals**,
Button **4/86 / 22 observations**, and Data **7/7** remain historical measurements.
Evidence: `/tmp/ctcompile-config-focused/` (logs, source manifests, passing generated
C++, array log, failure evidence and the complete Config probe).

## Filtered prefix assignments and Number cancellation, 2026-09-17 UTC

Resumed unmerged **`codex-dynamic-20260917`** from the **16:17:07 AGENT-SYNC
handoff** at `1d363a7b`; shared main started clean at `d2cc2b8c`. September 7 WIP
was absent. Locked merge **`08a812a6`** lands `73ae525d` (direct unique snapshot
assignments), `88e9f626` (callback census after frontend cell ordering), and
`1d363a7b` (fixture detach write boundaries). This completed the interrupted thread
before new implementation. Browser `0337cd15` is included in the tested oracle.

The predecessor's **377-target build, host-contract CTest 1/1 (0.45s), and focused
native-dom/native-dom-assignment/callback-startup lit 3/3 (249.23s)** passed.
All **1,818 input hashes** were reverified locally/remotely before landing.
Its inherited full CTest replay was deliberately stopped under the new user policy;
workflow exit **255** records cancellation, not a full-suite pass. No duplicate
full replay was started. Assignment recovery retained the 120-second sanitizer
compile timeout and all lifetime checks. The callback graph is **64 targets / 225
calls**: the added `D$8 -> fn$9` edge follows the frontend's cell-before-closure
order; six startup-reachable functions are unchanged. DOM fixture setup detaches
now drain their own logged writes, preserving entry no-mutation assertions.

**`58cb0b3a`** proves `n.replace(/^bs/, '')` as a fresh-result assignment key
when the original pure filter's true result implies an input beginning with `bs`.
The callback proof handles Boolean truth and structured joins; subsequent filters
preserve the subset. Removing the guaranteed prefix is injective. Existing single
traversal, sole writer, allocation placement and final-own-data observation checks
still govern the possible `__proto__` setter. Generated C++ reuses the existing
String replacement and ordered owning JSON assignment helpers. This capability is
separate from dataset membership: `t.dataset[n]` is proved, while
`t.dataset[n.replace(/^bs/, '')]` remains refused. Unfiltered two-preimage keys,
weaker predicates, repeated replacement/writes and incomplete budgets refuse.

**`08d849c2`** extends the existing bounded Number Add transfer to cancellation
of one negative and one nonnegative exact operand, in either order, when the
result is nonnegative. Original producer identity, read-time snapshots, latch
invariance and final-update bounds remain. Zero can initialize an index but cannot
prove loop progress. String/BigInt coercion, unknown values and out-of-domain
operands do not gain Number facts.

The eight requested build targets passed: `ctjs-opt`, `ctjs-translate`,
`ctcompile-tool`, `ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`, and
`ctcompile-test-type-oracle` (nine incremental Ninja actions). Exact CTests
`ctcompile_host_contract` and `ctcompile_escape_analysis_arrays` pass **2/2 (1.34s)**.
Lit `Analysis/Escape/escape-claims/{add-cancellation,sub-snapshot,signed-unary}.test`
passes **3/3**. The new oracle measures **20 sites / eight sound / zero violations /
eight of 12 precision (66.7%)**. Array checks cover **567 dense / 276 induction /
188 structured rows**, with **21,943 / 19,149 / 11,148** respective budget cutoffs.

The initial four-case lit selection was **three passes / one failure (249.77s)**:
the assignment driver's combined sanitizer compilation exceeded its existing
120-second timeout after six ordinary binaries had passed. Adding only
`-fno-inline-functions` compiled the exact 444 KB fixture in **14.29s**; all
sanitized assertions and output matched ordinary Clang. The test retains `-O1`,
all ASan/UBSan and lifetime options, every fixture, and the 120-second timeout.
The focused replay `CTNative/Browser/native-dom-assignment.test` then passes
**1/1 (181.34s): nine sources / 210 Node-VM observations plus 210 Node accessor
traces / eight GCC-Clang binaries / 196 refusals**, both providers/policies/layouts
and lifetime checks. The incremental build before this replay had no work.

The final workflow exits **0**, and all **1,819 input hashes** match locally and
on the devbox before documentation edits. Stable formatting passes **912 C++ /
108 Python / 105 web files**. Required `tools/format.sh --check` still reports
**26 pinned-formatter diagnostics in nine unchanged files**; that is a recorded
baseline, not a pinned-format pass. Black and `git diff --check` pass.

The existing whole-Bootstrap **19/574 native / 0 of 47 globals**, Button **4/86 /
22 lifecycle observations**, and Data session **7/7** are historical measurements,
not rerun results. Full CTest, full compiler lit, broad native/corpus matrices,
WPT and test262 were skipped per the focused validation policy. No browser source
or runtime semantics changed. Independent agents reviewed both proof paths and
extended execution tests; root recovered the escape draft after its agent hit a
service limit.

**Exact next:** one original full-H admission probe (`ctbrowser-dom-v1`, optimization
off) still refuses **DOM property read lacks a proved receiver and supported
member**. The prefix-result execution above admits two functions, including its
filter, in every tested provider/policy mode. No full-Bootstrap gain is claimed.
Full H still needs the browser-owned public UTF-16 indexing/slicing and Unicode
case seam for `charAt(0).toLowerCase() + slice(1)`. No suffix ASCII premise follows
from the filter. Lowercasing can collide ordinary result keys; the existing ordered
helper handles collisions, but any admission must preserve the source proof for
the sole possible prototype-key write. Coordinate through AGENT-SYNC while Claude
owns ctbrowser. Do not replace Unicode with the runtime's current byte/ASCII String
operations. Config/inheritance/defaults, retained callbacks and the application
driver remain open; signed multiplication snapshots are an independent future
escape step. Evidence: `/tmp/ctcompile-native-recovery/` and
`/tmp/ctcompile-prefix-focused/`. Older checkpoints below are historical.

## Ordered native result assignments and negative Sub snapshots, 2026-09-17 UTC

Continued clean **`1bbaff44`** and the **12:40:58 AGENT-SYNC handoff** after
checking both commit histories and unmerged branches. The interrupted guard/helper
thread was complete; this session resumed its documented fresh-result assignment
boundary. Three agents split source preparation, execution tests and escape review;
root recovered the escape proposal after one agent hit its service limit.

**`e8d5aae0`** preserves receiver-only fresh-object writes through structured
branches and loops. **`1576568d`** proves and lowers constant String-key assignments
except `__proto__` on a direct fresh object. Values are owning JSON trees, Strings,
optional Strings, null, Boolean or Number. Every mutation must finish before a
return, join, spread or value assignment can snapshot its target; observations
inside its mutation loop refuse. The shared member-update helper preserves numeric
key order, ordinary insertion order and last-write values for assignments and
spreads. Two former JSON refusals retain their exact source and earlier DOM reads
as execution positives. No browser implementation or Script/VM/GC dependency was
added; the public `ctbrowser::json_value` owns the result.

**`eec9fcb9`** preserves exact negative Number subtraction snapshots. String
coercion supplies no such fact. Existing latch invariance, sign, index and final
update checks remain. The new source oracle measures **20 sites / eight sound /
zero violations / eight of 12 precision (66.7%)**; the three existing signed
stride/unary source oracles pass. CFG/SCF tests add 27 rows and budget cutoffs.

The corrected **371-target build / 3/3 focused CTests (1.31s)** pass. The final
assignment driver passes **1/1 lit (55.69s): six sources / 111 Node-VM observations /
eight GCC-Clang binaries / 88 refusals**, both providers/policies/layouts and
ASan/UBSan lifetime checks. It executes original M values inside repeated fixed-key
writes and checks results after document/session destruction. The other seven
focused lit tests pass: dataset **28 sources / 112 observations / eight binaries /
432 refusals**, JSON **21 / 584 / eight / 248**, Strings **783 observations / eight
binaries / 1,056 source refusals** plus provenance controls, and four escape drivers.

The new driver retains **111 additional Node accessor traces**. The VM currently
invokes enumerable dataset getters during Object.keys, so its comparison uses the
same original entry source and values with plain data properties in both Node and
VM; dataset-getter and DOM-method order remain checked. Native observation first
checks raw result-key order and duplicates, then applies Node JSON enumeration to
nested parsed trees: public Core deliberately retains parse order. Ordinary nested
key order and scalar types still match exactly. The first compile typo and three
fixture failures are retained as evidence; they did not require runtime changes.

The complete frozen gate passes **310/310 CTests (2087.43s) / 260/260 lit
(1822.86s)** with no skips; workflow exit status is **0**. All **1,796 input
hashes** match locally and on the devbox. Fresh full Bootstrap remains **19/574
native / 0 of 47 globals** in both policy reports, with no skipped or pruned
functions. Button remains **4/86 / 22 agreeing lifecycle observations**. The
original DOM Data session passes its seven-function admission and lifetime
assertions; this is separate from the historical unadapted CommonJS census.
Stable formatting passes **890 C++ / 108 Python / 105 web files**. The required
pinned formatter retains exactly **nine unchanged files / 26 diagnostics**.
WPT/test262 corpus measurements were not rerun.

**Exact next:** 44 fresh admission probes cover 11 sources in both providers and
policies. A fixed result write now admits one function; original M inside a fixed-key
dataset result loop admits two. Guarded value/M loops remain admitted. Full original
H advances from its source-local identity refusal to **DOM property read lacks a
proved receiver and supported member**, also seen in the isolated Unicode
`charAt(0).toLowerCase() + slice(1)` expression. Dynamic and `__proto__` result writes
reach the explicit final DOM assignment diagnostic.

Do not generalize `__proto__` skipping: repeated writes after null, a parsed
prototype's own data slot, or a previous spread can create an own property. A
parallel Node-only review found a narrower possible proof for original H: exactly
one `bs__proto__` snapshot key, one traversal/write, original M values and final
own-data-only observations. Ordinary ASCII/Unicode normalized keys can collide.
This proposal is **not implemented or native-validated**. The current runtime still
has byte-indexed charAt and ASCII case conversion; the earlier É/İ VM discrepancy
was not remeasured. Coordinate a public, non-VM Unicode seam with the browser work.

Full H, Config/inheritance/defaults, retained callbacks and the application driver
remain unfinished. Claude's round-six/seven runtime changes are outside this frozen
oracle and need fresh differential validation after integration. Evidence:
`/tmp/ctcompile-assignment/`, including frozen hashes, failed and passing gates,
generated C++, 44 boundary probes and `prototype-next.md` with Node counterexamples.
Older sections below are historical checkpoints.

## Validated element guards and nested helper calls, 2026-09-17 UTC

Resumed seven dirty ctcompile files on **`d9f89a05`**, identified in the
**10:52:21 recovery / 10:55:26 abandoned-loop AGENT-SYNC journal**. Both histories
and unmerged branches were checked; September 7 WIP was already integrated. Two
editing agents hit service limits after reviewing the drafts; root recovered them.
A third agent completed independent proof and corrected-order reviews.

**`0a186321`** proves helper/capture initialization before enclosing SCF branches
and loops, preserving actual arguments and both URI/JSON exception continuations.
Invoke-boundary crossings, late/mutable captures, callable escapes and incomplete
budgets still refuse. Nested clone accounting now visits every operation. The former
branch-call refusal retains its exact original source as a differential positive.

**`fe650e1c`** folds truthiness and Not only from validated nonnullable element
parameters, on the private fingerprinted candidate. Existing helper/cell expansion
runs first, so original source CellGet operations and helper formals resolve through
that proof; the guard itself grants no facts to cells, nullable reads, joins or loop
state. Iterator preparation follows, then complete DOM reproof. Guard budgets fail
without mutation. Original guarded value/count/alias loops and the former `!element`
refusal retain their source bodies and earlier DOM observations. No browser or
runtime semantics changed; emitted C++ remains ordinary owning containers and public
DOM calls without Script/VM/GC symbols.

The first **62-target build / 2/2 CTests** passed, but three lit drivers exposed the
pass-order error and two newly admitted refusal expectations. Those were corrected,
not removed. The corrected **316-target build / 2/2 CTests (0.43s) / 3/3 lit
(147.24s)** pass. Dataset: **26 sources / 97 Node-VM source-double observations /
eight GCC-Clang binaries / 432 refusals**, HTML/SVG and lifetime sanitization.
JSON: **19 sources / 488 observations / eight binaries / 256 refusals** with lifetime
sanitization. Strings: **783 observations / eight binaries / 1,056 source refusals**
plus the existing provenance/depth/budget controls.

The complete frozen gate passes **310/310 CTests (2076.81s) / 258/258 lit
(1815.84s)**, with no skips. All **1,793 input hashes** match locally and on the
devbox. Fresh full Bootstrap remains **19/574 native / 0 of 47 globals** in both
policy reports, with no skipped or pruned functions. Button remains **4/86 / 22
agreeing Node-VM lifecycle observations**. The separate original DOM Data session
regression also passes its seven-function admission and lifetime assertions;
unadapted CommonJS Data still reports **0/7**, a different census.

After that full run, **`ff594bb6`** adds original M inside guarded dataset-value
and nullable-attribute loops to the existing execution driver. All 26 earlier
sources, fixtures, values and refusals are preserved. The updated **1/1 dataset lit
(52.75s)** passes **28 sources / 112 Node-VM source-double observations / eight
GCC-Clang binaries / 432 refusals**, both providers/policies/layouts, HTML/SVG,
repeated calls, invalid handles and ASan/UBSan lifetime checks. Boolean, Number,
JSON, URI-failure and JSON-failure paths run inside the loops. These new observations
check `typeof` results; the existing JSON driver checks full returned values.
The all-build needed no compilation work; all **1,793 updated input hashes** were
verified locally/remotely before documentation edits. The full-suite result above
precedes this test-only extension; the updated driver was then rerun separately.

Stable formatting passes **890 C++ / 107 Python / 105 web files**. The required
pinned formatter reproduces the existing **nine unchanged files / 26 diagnostics**.
WPT/test262 corpus measurements were not rerun.

**Exact next:** 36 fresh admission probes cover nine sources in both providers and
both optimization policies. The isolated element guard admits one function; guarded
values and original M calls inside value, attribute and guarded-value loops each
admit two. The two guarded M-loop consumers now have the permanent execution checks
above. Full original H still refuses **DOM helper branch contains an unproved local
identity**.
Fresh static result assignment refuses object escape/identity; dynamic loop assignment
refuses nonlocal/unordered uses. Extend the fresh-object/source-use proof and complete
DOM SetProperty proof together, preserving assignment order, collisions and inherited
`__proto__` setter behavior; JSON spread semantics are insufficient.

Unicode `charAt(0).toLowerCase() + slice(1)` still refuses the unsupported member.
The previously measured É/İ oracle gap was not remeasured here. Full H,
Config/inheritance/defaults, retained callbacks and the application driver remain
unfinished. Claude's separately measured round-six runtime changes are outside this
frozen tree and require fresh differential validation when integrated. Evidence:
`/tmp/ctcompile-guard-finish/`. Older sections below are historical checkpoints.

## Present dataset values and signed unary snapshots, 2026-09-17 UTC

Resumed **12 dirty ctcompile files on `6e8fb994`**, identified in the **02:27:52
recovery / 02:29:20 abandoned-loop AGENT-SYNC journal**. The integrated-runtime
recovery was already committed; September 7 WIP is already an ancestor. Three agents
split dataset tests, escape recovery and independent review. Root recovered the two
editing agents after service limits, reviewed their drafts and completed the gates.

**`9a367648`** compiles original same-element `t.dataset[n]` reads through public
`ctbrowser::dataset_value`. Immutable snapshots record their exact element and
enumeration epoch; confined filtering preserves provenance, and the existing guarded
vector index proof supplies a present own member. A fresh dataset lookup cannot renew
an old key. Changed/joined/carried Strings, cross-element reads, mutations, unknown
effects and incomplete budgets receive no membership evidence. Native C++ returns
owning Strings; no browser implementation, Script/VM/GC dependency or runtime
semantics changed.

**`8207a051`** preserves held bounded signed Number facts through unary Plus and
Neg, including a second Neg restoring a positive stride or own index. Exact initial
values, invariant backedges and bounded final updates remain required. Coercible
String/BigInt/unknown inputs do not acquire Number facts. CFG/SCF and complete source
oracles cover retained children, zero trips, overshoot, source mutation and budget
exhaustion.

The **393-target build / 3/3 focused CTests (0.83s) / 5/5 lit (42.59s)** pass.
Dataset tests cover **22 sources / 80 Node-VM source-double observations / eight
GCC-Clang binaries / 392 refusals**, HTML/SVG and ASan/UBSan lifetime checks, including
returned Strings after document/session destruction and mutation between calls.
The new signed-unary oracle observes **20 sites / eight sound / zero violations /
eight of 12 precision (66.7%)**. The complete historical fixture remains **895 sites /
40 sound / zero violations / 40 of 172 precision (23.3%)**, with its snapshot unchanged.
Independent static dataset proof/emission review found no actionable defect.

The complete frozen gate passes **310/310 CTests (1950.15s) / 258/258 lit
(1701.18s)**, wrapper **0**, with no skips. All **1,793 input hashes** match locally
and on the devbox before documentation edits. This includes the corrected Annex B
String witness from the preceding recovery. Array suites now cover **567 dense /
234 induction / 162 structured rows**, with **21,935 / 15,806 / 9,424** conservative
budget cutoffs.

Fresh full Bootstrap remains **19/574 native / 0 of 47 globals**, Data **7/7**, and
Button **4/86 / 22 agreeing Node-VM lifecycle observations**. All four corpus reports
are byte-identical to the preceding gate. The original dataset-value loop now admits
**two native functions in all four provider/policy modes**, alongside the filter,
count and prefix loops. Original M retains **24 nine-register blocks / handler ^bb12**.
Full H still refuses **DOM helper branch contains an unproved local identity**.

Stable formatting passes **890 C++ / 107 Python / 105 web files**. The required
pinned formatter reproduces exactly the same **nine unchanged HEAD files / 26
diagnostics**. WPT/test262 corpus measurements were not rerun. Evidence, source
manifests, generated code, static review and boundary probes are preserved under
`/tmp/ctcompile-values-finalize/`.

**Exact next, measured in all four provider/policy modes:** prove the original
`if (!t) return {};` for an already validated, nonnullable element input, and fold
that exact guard on the private source candidate before iterator preparation. The
isolated guard currently refuses the unsupported `ctjs.unary`; the guarded value
loop refuses **DOM iteration requires one top-level source iterator**. An empty
result object alone already admits one function.

An independent composition thread is loop-local original M: both dataset-value and
getAttribute arguments refuse **DOM helper call has unsupported arity or source
order**. Extend the helper-call dominance/source-order proof at the actual nested
call while preserving capture initialization and original exception continuations.
Fresh result writes remain separate: static assignment refuses object escape/identity,
and dynamic assignment refuses nonlocal/unordered object uses. Ordered collisions and
inherited `__proto__` setter behavior need an assignment proof beyond JSON spread.
The 28 isolated boundary probes are admission measurements, not full-H execution.

The unchanged Unicode witness still disagrees for `bsÉtage` and `bsİtem`; ASCII,
empty, emoji and supplementary Deseret agree. The original expression lowercases its
first UTF-16 code unit. Keep this oracle issue coordinated with Claude; the filter
does not establish an ASCII suffix.

Full H, Unicode first-code-unit key normalization, loop-local M composition,
ordered dynamic result assignment/collision semantics, Config/inheritance/defaults,
retained callbacks and the application driver remain unfinished. Older sections below
are historical checkpoints.

## Integrated-runtime recovery complete, 2026-09-17 UTC

Resumed clean `228d80d1` from this handoff and the interrupted **2026-09-16
22:29 AGENT-SYNC** gate. The native source had already landed: `c8b9856d`
(original anchored prefix), `bf872db3` (branch-local confined filter scheduling),
and `c7d1b1a2` (held bounded Neg stride). Claude reworded their old SHAs without
changing the trees. September 7 WIP is already an ancestor; no branch was replayed.

The integrated browser's Annex B fix gives a block function a local binding.
The former duplicate-export fixture therefore has one global publication and now
correctly admits. Its exact original source is preserved as a native/Node/VM positive;
an explicit repeated `var` export retains the refusal, with one-versus-two raw
publication assertions. An isolated before/after probe confirms the scope distinction.
No compiler or browser semantics changed during recovery.

The fresh devbox build passed **817 targets**. The full gate passed **309/310
CTests (1978.86s)**: all **309 non-lit tests**, and **256/257 lit (1720.91s)**;
only that stale expectation failed. After the test-only correction, **3/3 focused
CTests (143.02s)** passed, including the complete Strings driver **1/1 lit (142.57s)**.
Thus every test has passed across the full and corrected focused runs; this is not
a single all-green full invocation. Both sets of **1,792 hashes** match locally and
on the devbox, and only `native_dom_strings.py` differs between them. Logs, failed
and corrected manifests, generated-code probes and reports are preserved under
`/tmp/ctcompile-runtime-resume/`; the corrected workflow exits **0**.

Stable clang-format 23.1.1 passes **890 C++ / 107 Python / 105 web files**. The
required pinned formatter reproduces the same **nine unchanged HEAD files / 26
diagnostics**, matching the predecessor baseline. Independent bounded static reviews
found no actionable prefix/filter or held-Neg proof defect. WPT/test262 corpus
measurements were not rerun.

Fresh full Bootstrap remains **19/574 native / 0 of 47 globals**, without skips or
pruning. Data remains **7/7**, Button **4/86** with **22 agreeing Node/VM lifecycle
observations**; all four reports are byte-identical to the previous frozen gate.
Original M retains **24 nine-register blocks / handler ^bb12**. In all four
provider/policy modes, the original filter, count loop and anchored-prefix loop each
compile **two native functions**. Full H still refuses **DOM helper branch contains
an unproved local identity**; Unicode key normalization and live dataset values each
refuse **DOM property read lacks a proved receiver and supported member**.

**Exact next:** present own `t.dataset[n]` String reads through public
`ctbrowser::dataset_value`, proving the exact element, immutable snapshot member and
unchanged enumeration epoch. Fresh dataset lookup cannot renew a stale key. The
existing iterator completion normalizer already keeps the key and consumer together;
generic String joins must not gain membership. The independent escape continuation
is preserving held signed Number snapshots through unary Plus and a second Neg.
Full H also needs its fresh result-object/valid-element guard, Unicode first-code-unit
normalization, loop-local M calls and dynamic assignment/collision semantics.
Node/VM still disagree for `bsÉtage` and `bsİtem`; this oracle gap is journaled for
Claude. Config/inheritance/defaults, retained callbacks and the application driver
remain open. Older sections below are historical checkpoints.

## Original snapshot iteration and scalar loop completion, 2026-09-16 UTC

Resumed clean **9b02a344**, its HANDOFF and the **19:22:54 AGENT-SYNC** journal:
the unfinished thread was the original filtered `for...of`, with source regressions
preserved at `/tmp/ctcompile-dom-iterator-tests/native_dom_dataset.loop-draft.py`.
Both histories and unmerged branches were checked; September 7 WIP was already landed
and older lens/JSON branches were explicitly superseded. Three agents split source
normalization, regressions and escape review. Root recovered two service-limited
agents' work; the source agent completed independent proof and next-boundary reviews.

**a607afc6** preserves source loop condition/yield tuples and exact register
correspondence through completion dispatch. An inactive slot may disappear only with
a constant predicate, an unused destination and a same-type live state replacement;
all source operations are accounted for. Original Array iterator and open/next/close
identities are explicit host premises. Iterator preparation reuses the complete
dataset/filter prefix proof on a private clone, then reproves the complete entry.
Scalar Number/String/Boolean loop state is supported. Each indexed String read needs
the exact immutable vector, a zero-start/unit-increment index and dominance by that
vector's `index < length` true arm. Vector writes, loop DOM mutations, escaped helpers,
unknown calls and insufficient budgets still refuse without published evidence.

**d9112248** connects those proofs to native String inference and owning `vector.at`
extraction. Original count, ordered-key and saved-snapshot loops execute unchanged;
generated C++ uses ordinary vectors, strings and loops, with no Script/VM/GC symbols.
The driver preserves all previous source witnesses and adds iterator/prototype,
mutation, escape and missing-premise controls. Independent prepared-loop tests cover
incorrect starts, updates, guards, vectors, branch placement, forwarded slots and every
insufficient proof budget. No browser implementation or runtime semantics changed.

Corrected focused build, **3/3 CTests (0.89s) / 2/2 lit (34.66s) PASS**. Dataset:
**15 sources / 45 Node-VM source-double observations / eight GCC-Clang binaries /
228 refusals**, HTML/SVG and lifetime sanitization. The first build's const MLIR
handle API error was fixed; failed logs and hashes are retained. The escape review
found no defect in the landed negative-Sub proof; no escape extension was started.
Historical escape results remain **895 observed sites / 40 sound / zero violations /
40 of 172 precision (23.3%)**, with all **1,123 snapshot rows unchanged**.

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

Claude's **6edb7421** browser work is still separate from this gate: Annex B/catch
bytecode, new globals and platform changes require fresh differential validation when
integrated. Evidence, source hashes, generated C++, reviews and fresh boundary probes:
`/tmp/ctcompile-iteration-resume/`. The previous sections are historical checkpoints.

## Owning snapshot length and negative Number strides, 2026-09-16 UTC

Continued the clean **26ba2f8e** handoff and its exact original dataset iteration
boundary. Both histories and unmerged branches were checked; September 7 WIP was already
an ancestor and old lens branches were explicitly superseded in the journal. Three
agents split source preparation, regressions and escape analysis. Two hit service
limits; root recovered their drafts. A further read-only review found no proof defect;
root removed a duplicate new oracle before the source freeze.

**0f3fbe91** proves `.length` on an owning original dataset-key or filtered String
snapshot. Evidence identifies each exact property read; existing `vec_length` lowering
returns its Number without a browser/VM lookup. Filter identities, callback confinement,
snapshot immutability and complete-budget proof remain required. The original `for...of`
source has not been rewritten into `.length`. The next-loop count/order/saved-snapshot
regression draft is preserved separately at
`/tmp/ctcompile-dom-iterator-tests/native_dom_dataset.loop-draft.py`.

**e1bbd9e4** proves increasing dynamic Sub latches with bounded original negative Number
strides, including the frontend's single literal negation and an unchanged carried
stride. It reuses the existing exact initialization/final-update proof; commuted
subtraction, String/BigInt/unknown and changing strides remain refused. CFG/SCF tests
retain returned children and cover zero trips/overshoot.

Corrected focused build, **3/3 CTests (0.91s)** and **3/3 lit (27.32s)** pass, including
**12 dataset sources / 33 Node-VM source-double observations / eight GCC-Clang binaries
/ 150 refusals**, HTML/SVG and lifetime sanitization. The first array CTest identified
three old UnknownIndex assertions. The unchanged negative-offset sources now prove an
exact absent index/growing length, so still refuse at MissingElement. All three
assertions were corrected and the array CTest rerun successfully.

New escape oracle: **15 observed sites / six sound / zero violations / six of eight
precision (75%)**. Historical oracle remains **895 sites / 40 sound / zero violations /
40 of 172 precision (23.3%)**. The existing **1,123-row** snapshot still matches. Array
suites cover **567 dense / 174 induction / 123 structured rows**, with **21,928 / 12,506
/ 7,047** conservative budget cutoffs.

Complete build and **305/305 CTests (2262.84s) / 256/256 lit (1983.71s) PASS**, wrapper
**0**, with no skips. All **1,772 frozen input hashes** match locally and on the devbox
before these documentation changes. Fresh full Bootstrap remains **19/574 native / 0 of
47 globals**, Data **7/7**, and Button **4/86**, including all **22 Node-VM lifecycle
observations**. Both Bootstrap policy reports, Data, Button and next-boundary reports
are byte-identical to the prior gate. Original M preserves **24 nine-register blocks /
handler ^bb12**. The full WPT/test262 corpus measurement was not rerun. Evidence lives
in `/tmp/ctcompile-dataset-iteration/`. Stable formatting passes **877 C++ / 104 Python
/ 105 web**; the pinned formatter's **nine files / 26 diagnostics** independently match
unchanged HEAD files. No browser implementation or runtime semantics changed.

Exact next (original IR is now local at `/tmp/ctcompile-dataset-filter-next/`): the
count-only filtered `for...of` still refuses **DOM helper completion observes an
inactive value**; full original H refuses **DOM helper completion requires acyclic
structured source**, each in all four provider/policy modes. The loop needs proofs of
iterator-helper identity, original Array iterator behavior, scalar loop state and
inactive completion slots. Its before region contains an index switch; after-region
arguments forward count/index/completion slots. The completion copier currently returns
one function-result value and cannot model that loop terminal tuple merely by accepting
While. Read-only details are in
`/tmp/ctcompile-dataset-iteration/iteration-boundary.md`. Native snapshot length is now
available for its index path; String extraction still needs an exact integral in-bounds
index proof before using existing `vec_at`. Then key normalization, live dataset reads,
M composition and dynamic result writes remain, as do full Config/inheritance, retained
callbacks and the application driver. No full-bundle admission gain is claimed by this
prerequisite.
