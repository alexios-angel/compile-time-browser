# Next Bootstrap native boundary

## Retained object keys, mixed key carriers and test splits, 2026-09-10

Continued the exact **7381e2fb / 5eba229d** boundaries left by **637f4e8c**
and the **17:06:31 UTC** synchronization journal. The checkout was clean at
**98b7fde4**. The older codex-wip branch, CallDirectOp repair, interrupted
mutation gate and source-split repairs were already merged and gated. Three
agents handled independent lowering, escape and execution work; root alone
serialized devbox operations and Git writes. No browser/runtime source changed.

**25c4c882** proves empty object keys across the complete captured-Map call
family. Actual uses must belong to that exact invocation census; every sibling
body independently permits object formals only as Map keys. Map storage retains
the key, which has no outgoing ownership edges. Same-family aliases, set/get/has/
delete and scalar payloads reuse existing ownership and native identity proofs.
Field mutation, object payloads, arbitrary calls and cycles remain refused.
Source/prepared owner checks cover live edits, stale fingerprints and incomplete
budgets. A separate review found no additional correctness issue.

**54d50a6b** supplies the separately proved Number/Object Map key carrier using
`std::variant<double, std::shared_ptr<ctnative::identity_object>>`. Key spelling,
admission and wrapping use the existing comparator/storage implementations;
object-valued payload storage is unchanged. Identity aliases, distinct keys,
NaN, signed zero, deletion, clear and owner release pass the native mixed-key
and object-key suites with GCC/Clang and sanitizers (**2/2**, **46.49 seconds**).
This storage carrier does not authorize mixing Number/Object actuals at one
formal across calls; **5eba229d** seeds a numeric key inside an object-key method.

**e59e6ce1** executes **seven native object-key programs**, with **21 typed
Node/interpreter observations across 19 sources** and **13 distinguishing
mutations**. Historical source bytes remain intact. The exact retained-key
**7381e2fb** and mixed-key **5eba229d** now admit **4/4 functions in both modes**;
the fresh-key sibling source **3fbdfc23** admits **7/7** with fifteen calls.
Explicit/deduced GCC/Clang output preserves actual allocations and Map calls,
with no Script symbols. Saved sibling callables retain Map/key ownership after
caller and table release, through **128 future key rounds**, aliasing,
overwrite/delete/clear, reentry and final release under ASan/UBSan/leak checks.
Twelve independent refusals remain. Exact budget boundaries **1516 / 1639**
check **32 / 31 cutoffs**.

**0995a758** promotes historical `parameter_object`, preserving its name,
**7b592b83** source identity, **five functions and five calls**. It passes native
execution in both layouts with GCC/Clang and sanitizers; saved setter/getter
callables retain **128 fresh/aliased keys** after caller release and free them
with the final Map owner. The collection now checks **eight native programs,
20 source rows, 22 typed observations and 14 distinguishing mutations**.
All **171 historical refusal sources** retain their exact bytes; only this
proved case changes routing. Its proof completes at budget **3442**, with
**31 cutoffs** checked. The complete lit rerun passes as recorded below.

**4680d90e** extends independent mixed BigInt error-retention proof to Mod.
It grants neither successful completion nor native BigInt/effect permission.
The matrix checks **57 rows, 43 live states and 3451 cutoffs**. The strict fixture
has **737 claims, 760 observed sites, 24 unclaimed and 101/139 precision**, with
zero soundness violations, partial or pending checks. Initial array failures
were stale test category predicates; their source-preserving correction passes.
Next escape witness: mixed BigInt **Pow**, exact **06f94afe**, still conservative.

The user requested splitting every compiler test over 1,000 lines using Git
moves. **681bd899**, **cdcdc0c2** and **e37a4343** split the C++ tests, escape
checker/fixture and Map-presence lit cases. Named test headers and ordinary C++
translation units keep each original executable and main order. Fixture chunks
reassemble the exact **95,603 bytes** (SHA-256 **c03c5607**); expanded checker
includes reproduce the original checker. C++ bodies/literals and all **99 JS
cases / 100 RUN commands** are preserved. Two blank inter-case separators were
removed at file ends. The Python split preserves **198 helper/class bodies**,
**134 source/data entries**, **199 import scopes**, and the original main AST
when its extracted observation phase is inlined. All eleven original oversized
files are split; the maximum remaining compiler test file is **987 lines**.
The browser corpus's `p5-api-probe.js` is still **1,416 lines** under Claude's
active claim; its eventual split is recorded in the synchronization journal.

The requested folder organization is complete: **321 tracked test files** moved
using locked `git mv` in six commits. **d75b4a51** groups 49 escape/type files;
**db2ecc05** groups 39 ownership/host/exception files; **00ef1597** groups 37
runtime, packaging, core and support files; **b3475276** groups 57 native fixtures,
checks, specialization and importer files. **a085fb54** groups 66 native pass,
provider, export and ownership files; **72cd44b3** groups 73 lowering files.
The test root retains CMake/lit configuration and its new directory guide;
CTNative's root retains only its recursive `.gitignore`. Native lowering uses
`Maps/`, `Objects/`, `Closures/`, `Scalars/`, `Exceptions/`, `Admission/` and
`Emission/`. See `ctcompile/test/README.md` for the full hierarchy.

Fixture and test bodies remain intact; only include/import/RUN/build paths and
reference comments change. The final inventory retains **523 CTest names in the
same order, 168 lit cases and 1,215 RUN lines**. Python helper CLI/import checks
and all 134 Map source/data entries are preserved. The first three folder gates
pass **59/59, 32/32 and 33/33** affected CTests. The next full monorepo run passes
**522/523 in 2325.55 seconds**, with all **151 browser tests** passing; its only
failure is `ctcompile_lit` timing out at **1500.12 seconds** after **167/168**
lit cases pass. The unchanged Map ownership matrix exceeds that tight deadline.
The complete native-suite rerun after the 66 moves passes **168/168 in 1509.23
seconds** using `ctest --timeout 2400`. That 2,400-second suite limit is now
registered in CMake so the standard CTest gate uses it. The final lowering/importer gate passes
**67/67 in 54.84 seconds**. All **1,214 frozen local/devbox inputs** agree
before and after the full and affected rerun workflows. All 523 CTests are thus covered across the full run
and affected reruns; this is not a single green full run. Stable format22 passes
all **800 files**; bundled23 retains the same nine pre-existing differences.
Claude's **45ff3d01** browser refactor branch remains outside these measurements.
Its journal records the deleted GPU/compositor APIs and other public-header
changes. Re-read that journal and the actual subsystem headers after integration;
the compiler still uses the existing plain C++ platform boundary.
Folder evidence: `/tmp/ctcompile-organized-full-ctest.log`,
`/tmp/ctcompile-native-root66-ctest.log`,
`/tmp/ctcompile-native-lowering73-ctest.log` and
`/tmp/ctcompile-test-folder-final-audit.json`.

The requested **filename cleanup is complete**: **193 files renamed** across
all **414 reviewed test files**, with standard, upstream and already-clear names
retained. **89fc5763** renames 45 analysis/runtime files; **5a0f3cec** renames 19
importer, target, comparison, guard and packaging files; **1c72b01d** renames
129 native tests, helpers and fixtures. All moves used locked `git mv`.
**04de66be** updates eight production comments that reference the moved tests;
production behavior is unchanged. Current documentation links and command
examples now use the final folders and filenames.

The source check preserves **61 fixture programs / 370,700 bytes**, all **134
catalog entries**, **49 Python files / 61 import edges**, **168 lit cases /
1,215 RUN lines**, and the **987-line** maximum. All 523 CTest names/order and
1,214 frozen local/devbox inputs agree. Names pass Windows collision and
reserved-name checks. The standard README documents the layout and conventions;
27 existing Git-ignored Python caches are recorded and untouched.
Evidence: `/tmp/ctcompile-final193-verification.json`.

The analysis/runtime filename wave passes **44/44 CTests in 222.40 seconds**;
the importer/target wave passes **10/10 in 10.03 seconds**, including **45/45
lit cases**. The final **347-step devbox rebuild passes**. Its full CTest run
completes **169 passing tests**, then the user explicitly waives further tests
for this filename-only round. The ongoing native lit process and its children
were stopped under the existing devbox lock; the full 523-test run is therefore
**incomplete by request, not a reported full pass**. No further test run is
required for this cleanup. Stable format22 passes **800 files**; bundled23
retains the same nine pre-existing differences. Logs, hashes and the waiver
record are under `/tmp/ctcompile-names-final193-*`.

The **read-only ponytail audit** recommends three remaining cuts: unused
`tools/mingw/build-shaderc-mingw.sh` (**121 lines**), obsolete phase/configure
comments in `MLIRContextSetup.cpp` and `test/cmake/Lit.cmake` (**33 lines**), and
the two unread `CTCOMPILE_STANDALONE` assignments (**2 lines**). Total:
**156 lines, zero dependencies**. None was applied. The requested test cleanup
is separate; Claude's browser cuts are on his incoming branch. Full evidence:
`/tmp/ctcompile-ponytail-audit-final.json`.

**Incoming runtime dependency:** Claude's 637e4a40 branch includes 552a4ba0,
which invokes getters and setters on the implicit Object.prototype chain.
The current generic escape classifier still marks property receivers NEITHER
using the old data-only fallback assumption (`CTJS/IR/Ops/Properties.td`).
A **220-byte retained-this witness** now measures the gap on both matching
devbox binary sets: source SHA-256 **441a8930**, program **a99e2d04fc92995e**,
function **1 / readInherited**, PC **1 / obj**. Both compilers claim Confined.
Current main records one confined object and zero escaped; incoming 637e4a40
records zero confined and one escaped through globals. The incoming checker
exits **1 with one soundness violation**; main exits **0**. Both collections
have zero unresolved or unchecked target observations. Raw commands, binary
hashes and observations are in `/tmp/ctcompile-nd3-probe-results/summary.json`
and its `main/` and `incoming/` directories. This diagnostic probe is separate
from the green filename gate. An intercepted assignment also cannot prove an
own slot; that setter follow-up remains a static witness.
Repair the receiver effect/proof and add getter/setter/iteration retention
regressions before treating the runtime integration as gated. Property sinks
alone are insufficient: Iterable must keep Carry while sinking, load provenance
must record both edges, and contents analysis must refuse ordinary-object writes
without an own-data/prototype proof. `/tmp/ctcompile-nd3-repair-scope.md` records
the exact production and test paths. Native field admission has separate guards;
no end-to-end native miscompile has been measured here.
Current main still has the old runtime and passes its ND-3 pin. Once the proof
repair and runtime are integrated, change the existing `typeof` probe in
`test/Analysis/Escape/Cycle.cpp` from `undefined` to `number`, retain the explicit
chain's numeric 42 check, update divergence status and rerun the affected
escape/oracle and integration gates. The source-preserving pin patch under
`/tmp/ctcompile-nd3-after-runtime-integration/` is only one part of that work;
`proof-safety-audit.md` records the missing proof. Do not apply the pin alone.
Keep the runtime's corrected semantics; native getter admission is unchanged.
The automatic integration watcher was stopped using Claude's documented stop
option after the false Confined claim was measured. A clean compiler checkout
allows Claude to commit; integrate the runtime after the compiler proof repair
and its measured gate.

Measured gates so far: the initial merged-tree build passes **751 steps**; the
feature rebuild passes **302 steps** without warnings. After the C++ splits,
all **15 focused CTests pass in 224.65 seconds**, and the three regrouped
Map-presence lit tests pass in **0.64 seconds**. Stable clang-format **22.1.8**
passes **800 files**; bundled 23 retains the same nine baseline differences.
**2172add2** commits the Python split after its seven-program native/lifetime
check passed again. The full monorepo gate passes **522/523 CTests in 2228.37
seconds**: **151/151 browser tests**, and **371/372 compiler tests**. Its lit
CTest passes **167/168** cases in **1407.83 seconds**; the sole failure is the
historical `parameter_object` expectation, now admitting all five functions.
Its exact fresh-key setter/size-getter source now passes the focused execution
and lifetime check above. The corrected **ctcompile_lit CTest passes 1/1**, with
**168/168 lit cases in 1499.87 seconds** (1499.80 internally; 1499.88 total).
Thus all **523 CTests** are covered as passing across the original run and the
corrected rerun; the initial failure remains recorded. All **1,214 source hashes**
match the devbox before and after the corrected run, with only the six intended
test files differing from the initial full run. Independent evidence:
`/tmp/ctcompile-retained-composite-audit.json`.

Fresh exact Bootstrap Data measurements preserve these source identities:

| Program | SHA-256 prefix | Functions | Raw/prepared calls | Native, both optimization modes |
| --- | --- | ---: | ---: | ---: |
| Browser | `80a6fd87` | 7 | 42 | 0/7 |
| CommonJS | `cc6c3960` | 7 | 42 | 0/7 |
| AMD | `821e07a5` | 8 | 43 | 0/8 |
| Ordinary publication | `8359592c` | 7 | 40 | 0/7 |

All **77 typed Number observations** agree between Node and the interpreter.
Fresh prepared contracts still refuse with `property receiver lacks a fresh
own-data object proof`; both native modes preserve the refused operations and
source calls. Browser/CommonJS/ordinary prefix probes each resolve **24 calls**
and summarize **23 provider calls**, keeping **one outer allocation, three
nested allocations, one callback and two global writes** at runtime. Browser
and CommonJS residuals have **40 calls**, ordinary remains at **40**; fresh
residual contracts still admit **0/7**. Prefix work does not claim complete
ownership, and no native execution of these wholly refused Data programs is
claimed. Evidence: `/tmp/ctcompile-retained-exact-data-final-results.json`.

**Next: ordinary named object owners for actual Bootstrap Data.** The exact
field-bearing/nested-Map Data methods still need independent ownership and
effect proofs. Block-const sibling **b6d341ad** currently refuses **0/7**, but
that particular source imports globals because of the VM/compiler block-scope
bug fixed by Claude's **d99ddf7b**, which is not merged here. Remeasure it after
integration; do not treat that bug as a permanent native limitation. Ordinary
`var` key **7573e89b** remains an independent global-owner refusal and is the
smallest next slice. Prove one empty allocation, one unconditional initialization
dominating every load, and key-only arguments across the complete captured-Map
family. Keep each actual `LoadGlobal` separate from its allocation identity.
The existing `environmentProblem()` checks initialization order, so the
sole-store lookup alone is not evidence of a current unsound admission. The
missing piece is a checked allocation/store/load edge that downstream ownership
and type consumers can revalidate. Follow `scalarGlobalRead()`'s live-edge pattern
without primitive categories, preserving the actual `LoadGlobal` in
`HostMethodArgument`. Publish only after the complete environment, fingerprint
and budget proof succeeds; revalidate in `OwnedGlobalMethods`, then consume that
edge in NativeMap, ObjectIdentity, TypeInference and typed global emission.
Reject second stores (including after the last call), early reads, field mutation,
unknown consumers, payload/return escapes and incompatible later arguments.
The true `var` sibling companion **600b8fb6** additionally needs immutable aliases
and two distinct allocation identities. Full native Bootstrap and direct browser
API integration remain unfinished.

Evidence: `/tmp/ctcompile-retained-{focused,execution,probe}.log`,
`/tmp/ctcompile-test-splits-focused.log`,
`/tmp/ctcompile-retained-key-execution-final.log`,
`/tmp/ctcompile-{map-tests,escape,large-test}-split-*.json`.

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="current-continuation-retaining-object-keys-2026-09-10"></a>
- [Current continuation: retaining object keys, 2026-09-10](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#current-continuation-retaining-object-keys-2026-09-10)
<a id="independent-next-implementations"></a>
- [Independent next implementations](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#independent-next-implementations)
<a id="previous-dataobject-key-baseline-2026-09-10"></a>
- [Previous Data/object-key baseline, 2026-09-10](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#previous-dataobject-key-baseline-2026-09-10)
<a id="previous-continuation-mutations-after-equal-branch-cardinalities-2026-09-09"></a>
- [Previous continuation: mutations after equal branch cardinalities, 2026-09-09](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#previous-continuation-mutations-after-equal-branch-cardinalities-2026-09-09)
<a id="previous-continuation-equal-branch-cardinalities-2026-09-09"></a>
- [Previous continuation: equal branch cardinalities, 2026-09-09](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#previous-continuation-equal-branch-cardinalities-2026-09-09)
<a id="previous-continuation-exact-size-after-deleting-a-proved-key-2026-09-09"></a>
- [Previous continuation: exact size after deleting a proved key, 2026-09-09](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#previous-continuation-exact-size-after-deleting-a-proved-key-2026-09-09)
<a id="previous-continuation-exact-saved-one-after-clearset-2026-09-09"></a>
- [Previous continuation: exact saved one after clear/set, 2026-09-09](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#previous-continuation-exact-saved-one-after-clearset-2026-09-09)
<a id="previous-continuation-exact-saved-zero-after-mapclear-2026-09-09"></a>
- [Previous continuation: exact saved zero after Map.clear, 2026-09-09](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#previous-continuation-exact-saved-zero-after-mapclear-2026-09-09)
<a id="previous-continuation-owning-string-leaf-fields-2026-09-09"></a>
- [Previous continuation: owning String leaf fields, 2026-09-09](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#previous-continuation-owning-string-leaf-fields-2026-09-09)
<a id="previous-continuation-owning-string-globals-before-0e041bba-2026-09-09"></a>
- [Previous continuation: owning String globals before 0e041bba, 2026-09-09](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#previous-continuation-owning-string-globals-before-0e041bba-2026-09-09)
<a id="previous-continuation-boolean-globals-before-df304fdf-2026-09-09"></a>
- [Previous continuation: Boolean globals before df304fdf, 2026-09-09](bootstrap-provider-next/01-current-continuation-retaining-object-keys-2026-09-10.md#previous-continuation-boolean-globals-before-df304fdf-2026-09-09)
<a id="previous-continuation-constant-only-number-globals-2026-09-09"></a>
- [Previous continuation: constant-only Number globals, 2026-09-09](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#previous-continuation-constant-only-number-globals-2026-09-09)
<a id="previous-continuation-definite-scalar-global-initialization-2026-09-09"></a>
- [Previous continuation: definite scalar-global initialization, 2026-09-09](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#previous-continuation-definite-scalar-global-initialization-2026-09-09)
<a id="previous-continuation-saved-scalar-globals-2026-09-09"></a>
- [Previous continuation: saved scalar globals, 2026-09-09](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#previous-continuation-saved-scalar-globals-2026-09-09)
<a id="previous-continuation-entry-numeric-results-2026-09-08"></a>
- [Previous continuation: entry numeric results, 2026-09-08](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#previous-continuation-entry-numeric-results-2026-09-08)
<a id="previous-continuation-captured-mapclear-2026-09-08"></a>
- [Previous continuation: captured Map.clear, 2026-09-08](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#previous-continuation-captured-mapclear-2026-09-08)
<a id="previous-continuation-object-valued-map-absence-2026-09-08"></a>
- [Previous continuation: object-valued Map absence, 2026-09-08](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#previous-continuation-object-valued-map-absence-2026-09-08)
<a id="completed-provider-slice-ordinary-object-payloads"></a>
- [Completed provider slice: ordinary object payloads](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-provider-slice-ordinary-object-payloads)
<a id="completed-prerequisite-one-ordinary-global-owner"></a>
- [Completed prerequisite: one ordinary global owner](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-prerequisite-one-ordinary-global-owner)
<a id="completed-owning-method-fields-and-current-callees"></a>
- [Completed: owning method fields and current callees](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-owning-method-fields-and-current-callees)
<a id="completed-captured-map-environments-across-publication"></a>
- [Completed: captured Map environments across publication](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-captured-map-environments-across-publication)
<a id="completed-proof-map-effects-in-one-published-method"></a>
- [Completed proof: Map effects in one published method](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-proof-map-effects-in-one-published-method)
<a id="completed-one-captured-map-shared-by-published-methods"></a>
- [Completed: one captured Map shared by published methods](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-one-captured-map-shared-by-published-methods)
<a id="completed-primitive-arguments-to-each-published-method"></a>
- [Completed: primitive arguments to each published method](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-primitive-arguments-to-each-published-method)
<a id="completed-independent-call-result-actuals"></a>
- [Completed: independent call-result actuals](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-independent-call-result-actuals)
<a id="completed-locally-seeded-mapget-results"></a>
- [Completed: locally seeded Map.get results](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-locally-seeded-mapget-results)
<a id="completed-separate-live-map-entries"></a>
- [Completed: separate live Map entries](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-separate-live-map-entries)
<a id="completed-payload-types-across-possibly-aliasing-writes"></a>
- [Completed: payload types across possibly aliasing writes](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-payload-types-across-possibly-aliasing-writes)
<a id="completed-nonempty-mapsize-snapshots"></a>
- [Completed: nonempty Map.size snapshots](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-nonempty-mapsize-snapshots)
<a id="completed-distinct-key-size-bounds"></a>
- [Completed: distinct-key size bounds](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-distinct-key-size-bounds)
<a id="completed-homogeneous-boolean-and-owning-string-map-payloads"></a>
- [Completed: homogeneous boolean and owning-string Map payloads](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-homogeneous-boolean-and-owning-string-map-payloads)
<a id="completed-closed-mixed-keypayload-storage-and-exact-read-types"></a>
- [Completed: closed mixed key/payload storage and exact read types](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-closed-mixed-keypayload-storage-and-exact-read-types)
<a id="completed-saved-scalar-map-reads-through-writes"></a>
- [Completed: saved scalar Map reads through writes](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-saved-scalar-map-reads-through-writes)
<a id="completed-a-saved-scalar-selected-across-control-flow"></a>
- [Completed: a saved scalar selected across control flow](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-a-saved-scalar-selected-across-control-flow)
<a id="completed-a-guarded-saved-read-after-conditional-deletion"></a>
- [Completed: a guarded saved read after conditional deletion](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-a-guarded-saved-read-after-conditional-deletion)
<a id="completed-scalar-short-circuit-result-refinement"></a>
- [Completed: scalar short-circuit result refinement](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-scalar-short-circuit-result-refinement)
<a id="completed-proof-a-finite-nullable-result-contract"></a>
- [Completed proof: a finite nullable result contract](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-proof-a-finite-nullable-result-contract)
<a id="completed-owning-nullable-string-map-keys"></a>
- [Completed: owning nullable String Map keys](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-owning-nullable-string-map-keys)
<a id="completed-owning-nullable-map-payload-storage"></a>
- [Completed: owning nullable Map payload storage](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-owning-nullable-map-payload-storage)
<a id="completed-finite-nullable-read-facts-in-mixed-storage"></a>
- [Completed: finite nullable read facts in mixed storage](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-finite-nullable-read-facts-in-mixed-storage)
<a id="completed-finite-nullable-host-result-alternatives"></a>
- [Completed: finite nullable host-result alternatives](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-finite-nullable-host-result-alternatives)
<a id="completed-same-method-invocation-result-dependencies"></a>
- [Completed: same-method invocation result dependencies](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-same-method-invocation-result-dependencies)
<a id="completed-published-method-local-leaf-object-ownership"></a>
- [Completed: published method-local leaf object ownership](bootstrap-provider-next/02-previous-continuation-constant-only-number-globals-2026-09-09.md#completed-published-method-local-leaf-object-ownership)
<a id="next-method-local-object-readback-and-identity"></a>
- [Next: method-local object readback and identity](bootstrap-provider-next/03-next-method-local-object-readback-and-identity.md#next-method-local-object-readback-and-identity)
