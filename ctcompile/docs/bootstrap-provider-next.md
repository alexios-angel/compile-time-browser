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

The user's later folder-organization request is also in progress. A complete
**182-file move map** is prepared at `/tmp/ctcompile-test-organization-moves.json`:
Analysis/Escape, Types and Ownership; CTNative/HostContract, ExceptionRecovery,
Fixtures, Checks and Specialization; Runtime, Packaging, Comparison, Core and
Support. Only CMake/lit configuration files remain at the test root in that plan.
Fixture bytes, generated names and test registrations stay fixed. Apply and gate
the moves in coherent groups next; the historical-case repair is committed as
**0995a758**, and no folder move has been applied at this checkpoint.

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
The generic `object(actual)` lookup follows a sole store but does not establish
initialization dominance; using that lookup alone is insufficient. Carry explicit
live/fingerprint evidence through HostContract, OwnedGlobalMethods/Roots,
NativeMap/ObjectIdentity/TypeInference and ordinary typed global emission.
Reject second stores (including after the last call), early reads, field mutation,
unknown consumers, payload/return escapes and incompatible later arguments.
The true `var` sibling companion **600b8fb6** additionally needs immutable aliases
and two distinct allocation identities. Full native Bootstrap and direct browser
API integration remain unfinished.

Evidence: `/tmp/ctcompile-retained-{focused,execution,probe}.log`,
`/tmp/ctcompile-test-splits-focused.log`,
`/tmp/ctcompile-retained-key-execution-final.log`,
`/tmp/ctcompile-{map-tests,escape,large-test}-split-*.json`.


## Current continuation: retaining object keys, 2026-09-10

Resumed the **15:44–15:45 UTC** object-key/Div thread at **183a10c0**, including
three dirty Div files. The earlier interrupted mutation gate and codex-wip
recovery were already complete. Three agents handled independent escape,
owner-test and execution work; root serialized every build and commit. No
browser or runtime source changed.

**e9802e24** proves a direct empty entry object passed to a captured method as
an independent object actual/formal edge, with no primitive category. Every
actual use and every current invocation is checked; each object formal can only
be borrowed by the captured Map's `has`. Native identity and callable carriers
are still independently derived. Mixed primitive/object formals, field-bearing
or named objects, mutations, returns and storing an object key remain refused.

The exact **20d4806e** source advances **0/4 → 4/4 native in both modes**,
keeping all four calls and Number `trace=0`. **0dfded6a** executes that source,
a local-alias variant, repeated fresh actuals and two object formals: **four
programs, sixteen typed Node/interpreter observations and seven distinguishing
mutations**. Explicit/deduced GCC/Clang output retains actual allocations,
call arguments and `Map.has`, with no Script symbols. A saved getter survives
root/table destruction, **128 future object-key rounds**, reentry and final
Map/key destruction under ASan/UBSan/leak checks. The budget boundary is **1688**,
with **31 cutoffs** checked. Ten ownership/effect controls and a separate
complete-owner mixed-key carrier control retain their refusals.

All five owner/host CTests pass **5/5 in 217.69 seconds**. New source/prepared
owner matrices cover **20/18 rows and all 1348/1295 incomplete budgets**.
A test-only forged schema attribute initially invalidated its own restoration
contract; the corrected test removes it before checking the original contract.
An independent review found no additional production issue.

**c20f43a4** finishes mixed BigInt Div's independent error-retention proof,
without granting native BigInt or successful-completion permission. It checks
**57 rows, 43 live states and 3410 cutoffs**. The strict fixture records **721
claims, 741 observed sites, 21 unclaimed and 100/137 precision**, with zero
violations/partial/pending. Eight escape CTests pass **8/8 in 9.22 seconds**.
Historical fixture bytes and Div sources remain intact; Mod controls stay
conservative. The full build completes **263 steps without warnings**. Stable
clang-format **22.1.8 passes 745 files**; bundled 23 has the same nine baseline
differences. The fresh complete gate at **a129764d** passes **512/517 CTests
in 2227.17 seconds**, including **all 372 compiler tests** and **166/166 lit
cases** (1442.85 seconds internally; CTest 1443.04). The five browser failures
are `selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`;
each diagnostic exactly matches the previous gate. The full-run rebuild reports
no work, separately from the earlier 263-step build. All **14 committed/local/
devbox code/test hashes** match the frozen inputs. The fresh artifact census
verifies **417 native programs, 1668 GCC/Clang executables and 88 sanitized
binaries across 44 lifetime families**. Eight new C++ variants preserve their
allocations, calls and `Map.has`; those programs' sixteen ordinary and two
sanitized executables have no Script/AOT symbols.

**Next: retaining actual object keys through Bootstrap Data.** Exact
**7381e2fb** adds `t.set(e, 1)` before the same `t.has(e)` and remains unowned
**0/4** (typed Node/interpreter `trace=1`). It needs an ordinary Map-to-key owner
and all later set/get/delete uses proved through the existing host seam.
Field-bearing **698def06** and named **7573e89b** actuals remain separate owner
obligations. Numeric-seeded **5eba229d** now has complete ownership but stays
**0/4** at the closed Number/Object key variant's missing native carrier;
that is distinct from object ownership. These two bounded extensions can proceed
independently: `HostContract` proves exact Map-to-key retention and same-family
sibling uses; lowering supplies the already-proved Number/Object key variant.
Keep object-valued payloads, named/field-bearing keys and unknown effects separate.

The fresh postfull Data probe preserves browser/CommonJS/AMD source hashes
**80a6fd87/cc6c3960/821e07a5** and all **19/19/20 typed observations**. They remain
**0/7, 0/7 and 0/8** native in both modes, with **42/42/43 calls** preserved.
Ordinary publication **8359592c** remains **0/7**, with **19 observations and
40 calls**. Every observation matches Node and the interpreter. The owner reason
remains `property receiver lacks a fresh own-data object proof`. Browser,
CommonJS and ordinary prefix analyses finish **24 resolved calls and 23 provider
summaries**; freshly fingerprinted residual contracts still refuse ownership.
Exact Data and full native Bootstrap remain unfinished.

Artifacts: `/tmp/ctcompile-object-resume-{build-all,escape,owners,execution}.log`,
`/tmp/ctcompile-object-resume-{probe,execution}/`, and the fourteen-input
`/tmp/ctcompile-object-resume-frozen.json`. Full gate:
`/tmp/ctcompile-object-resume-full.log` and
`/tmp/ctcompile-object-resume-full-detail.log`. Exact Data:
`/tmp/ctcompile-object-resume-exact-data/`. Artifact audit:
`/tmp/ctcompile-object-resume-audit.json` and
`/tmp/ctcompile-object-resume-full-cpp/`.

### Independent next implementations

For **7381e2fb**, start with the two object-formal `has` restrictions in
`HostContract/CapturedMapBody.cpp`: prove the same exact empty object at the key
operand of captured `Map.set`, retaining scalar payload checks. Existing native
Map storage owns identity keys. Sibling calls additionally need
`Values.cpp::capturedMapParameters` to accept only exact invocations in the same
completely enumerated captured-Map family. Check caller/key release, alias and
distinct identities, overwrite/delete/clear, saved sibling lifetimes, reentry,
unsafe later uses, cycles, stale facts and incomplete budgets.

For **5eba229d**, add a key-only
`std::variant<double, std::shared_ptr<ctnative::identity_object>>` spelling in
`Lowering/LoweringSupport.cpp`, independently typed alternative admission in
`Lowering/Admission/Operations.cpp`, and key wrapping in `Lowering/EmitC/Maps.cpp`.
The existing variant comparator and both Map layouts already handle identity
and Number SameValueZero. Do not broaden generic `mixedMapSpelling`: that would
change established object-valued payload storage. Reuse `native-map-mixed.mlir`
and `check-map-mixed.py` for identity, NaN, signed zero, deletion and lifetime;
preserve the mixed same-formal and independent ownership refusals.

## Previous Data/object-key baseline, 2026-09-10

**58983ccc** finishes the prior mutation-size boundary. The exact insertion
**56679e2d** and absent-deletion **887e65fc** now admit **5/5 in both modes**
with sixteen calls and Number `trace=1` preserved. The proof transports mutable
cardinality only with independent actual-instance membership/absence and a
bounded complete candidate census. **a03cc3fd** executes 23 native programs,
38 typed observations, 26 mutations, six refusal/repair families and a 128-call
saved-leaf lifetime. Focused type/escape checks pass **9/9**, owner/host/provider
checks **5/5**, and presence lit **1/1**. The interrupted complete gate is now
recovered: the remaining **354/354 CTests pass in 2262.99 seconds**, accounting
for **512/517 overall** across the original and resumed runs, with **all 372
compiler tests** and **166/166 lit cases** passing. The five recorded browser
failures are unchanged in the preserved prefix. All fourteen committed inputs
match the devbox; **413 native programs and 43 lifetime families** pass.
See `HANDOFF.md` for the distinction between recovered and fresh evidence.

The continuation returns to Bootstrap's actual Data fragment. Fresh explicit
source contracts, both native modes and a separately fingerprinted contract
after prefix specialization give these measured results:

| Source | SHA-256 prefix | Functions / calls | Typed observations | Native |
|---|---|---|---|---|
| exact browser | `80a6fd87` | 7 / 42 | 19 | 0/7 |
| exact CommonJS | `cc6c3960` | 7 / 42 | 19 | 0/7 |
| exact AMD | `821e07a5` | 8 / 43 | 20 | 0/8 |
| ordinary publication, verbatim Data methods | `8359592c` | 7 / 40 | 19 | 0/7 |

All observations agree with Node and the interpreter. Every source's current
native owner reason is `property receiver lacks a fresh own-data object proof`.
Browser/CommonJS and ordinary publication prefix analyses each finish **24
resolved calls and 23 provider summaries**, with no remaining prefix boundary.
Their freshly contracted residual programs still refuse native ownership.
Completing a startup trace is not proof about future callers or object owners.

The ordinary-publication companion preserves every upstream Data method byte
and every observation, replacing only the UMD wrapper with the existing ordinary
host slot seam. It isolates remaining ownership work from UMD branch discovery;
it does not yet make Data native.

A smaller source-derived witness preserves the vendor's `t.has(e)` operation:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const t = new Map;
    return { get(e) { return t.has(e) ? 1 : 0; } };
});
var trace = host.slot.get({});
```

Exact SHA-256 prefix **20d4806e**; four functions and four calls. Both modes
refuse **0/4** at `property call lacks a current source getter proof`. Changing
only the argument to `({}, 1)` preserves the actual allocation and every Map,
publication and call operation. That **2507446b** candidate passes source getter
analysis but still refuses **0/4** at `owned global method table has another
allocation`. Its seeded counterpart behaves the same. These are measured
separate boundaries, not successful native repairs. A later Object actual also
prevents borrowing an earlier Number argument's proof.

Continue through the existing `HostContract` and native Map ownership model:
prove ordinary object arguments and identity across all supplied calls, and
prove the corresponding ordinary C++ owners. The current
`capturedMapParameters` accepts primitive alternatives only; Data's object
argument cannot be justified by an observed startup value. Nested Map creation
inside Data.set and callback/error effects remain separate obligations. Keep
source evaluation, future argument variation, unknown effects and conservative
work limits. Full native Bootstrap and direct browser API integration remain
unfinished.

The interrupted root/table companion probe is now measured too. Its ten sources
preserve every original allocation/read/call; four evaluated-Number companions
admit **4/4 in both modes** and execute **32 GCC/Clang binaries** without Script.
The six object-key/later-object cases remain **0/4**. The future observer agrees
with Node and the interpreter; three Node mutations distinguish its expected result. These controls
reuse existing root/table allocations; they do not discharge **2507446b**'s extra
allocation or **20d4806e**'s fresh object-argument ownership. Fresh exact Data
measurements retain all counts and observations above. Recovered artifacts:
`/tmp/ctcompile-data-resume-exact/` and `/tmp/ctcompile-data-resume-root-keys/`.

Artifacts: `/tmp/ctcompile-bootstrap-next-boundary/{sources,results,future}.json`,
`/tmp/ctcompile-mutation-exact-probe/`, and
`/tmp/ctcompile-mutation-execution.log`. Later measurements will refine this
boundary; none of these compile counts claim native execution of Data.

## Previous continuation: mutations after equal branch cardinalities, 2026-09-09

**bb8c52d3** finishes the prior **2d1763eb** join boundary: both modes admit
**5/5**, preserving all fifteen calls and Number `trace=1`. Independent source
ownership and native instance presence retain equal cardinalities without
inventing common key membership. **4a624e9e** executes eighteen new native
programs and the saved-join lifetime. Two historical singleton-join programs
also now execute unchanged. Focused type/escape checks pass **9/9** and
Map-presence lit **1/1**. The full warning-free build passes **all 372 compiler
CTests** and **166/166 lit cases**, including **390 native programs and 42
lifetime families**. Overall **512/517 CTests pass in 2302.35 seconds**; the
five established browser failure outputs are byte-identical to the previous
run. All fourteen code/test hashes match committed, local and devbox files.
Native corpus coverage remains Bootstrap **19/574**, p5 **39/4754**, Phaser
**45/7725** in both modes; exact Data remains **0/7, 0/7 and 0/8**.
`HANDOFF.md` records the complete measurements.

The next exact source inserts a key distinct from either possible survivor:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key, flag) { const item = {value: 1}; state.set(key, item); state.clear(); state.set(1, item); state.set(2, item); if (flag) { state.delete(1); } else { state.delete(2); state.has(key); } state.set(3, item); const saved = state.size; state.clear(); state.set(2, item); state.set(4, item); return state.get(saved).value === 1 ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set(7, false);
```

SHA-256: `56679e2d1edd49ce7e69a84633446fe408c31320c2613ed495484080a435ffa2`.
Five functions, **16 raw/prepared calls**, and Number `trace=1` are preserved.
Both native modes refuse **0/5**, with `property call lacks a current source
getter proof`. Each arm leaves one entry; the subsequent write of key three
increases both sizes to two, but currently invalidates the separate cardinality.
The final lookup must retrieve a real object and read its field.

The exact repair replaces only `const saved = state.size;` with
`state.size; const saved = 2;`. SHA-256:
`a11c4179e2b11578f284a41461ea87c867c9a96b5bfdd8d4b8d1154bb52bba9d`.
It preserves all sixteen calls and the evaluated size read, admits **5/5 in
both modes**, and retains Number `trace=1`. Both startup flags give the same
admission results. Deleting definitely absent key three exposes the parallel
size-one preservation boundary (**887e65fc** versus **8e3fa6f1**). Saving the
size before the mutation or clearing and rebuilding the census already admits.
Twelve typed Node/interpreter probes and eight discriminating mutations agree.

Continue the independent mutable cardinality only when membership/absence and
key relations prove the mutation's exact size effect on this runtime instance.
A possible insertion versus overwrite, possible deletion, unknown alias or
unknown effect must remain conservative. Keep every arm and actual read position;
cardinality never proves common key membership. Retain the bounded census and
independent native rederivation. Full native Bootstrap and direct browser API
integration remain unfinished.

Evidence: `/tmp/ctcompile-after-join-size-final/{sources,results,mutations}.json`
and `/tmp/ctcompile-map-join-execution.log`.

## Previous continuation: equal branch cardinalities, 2026-09-09

**e04810c8** completes exact saved sizes after deletion, and **b33125d1** gates
21 native programs, six refusal/repair families and a 128-call saved-size/object
lifetime. All ten historical continuation sources preserve their bytes. The
focused gate passes **14/14 CTests in 231.66 seconds** and **1/1 presence lit in
0.95 seconds**. The full warning-free build passes **all 372 compiler CTests**
and **166/166 lit cases**, including **370 native programs/41 lifetime
families**. Overall **512/517 CTests pass in 2102.19 seconds**; the five browser
failure outputs are unchanged. All fourteen code/test hashes match committed,
local and devbox files. Native corpus coverage remains Bootstrap **19/574**,
p5 **39/4754**, Phaser **45/7725** in both modes; exact Data remains **0/7,
0/7 and 0/8**. `HANDOFF.md` records the detailed measurements.

The next exact source leaves one key on each branch but different keys:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key, flag) { const item = {value: 1}; state.set(key, item); state.clear(); state.set(1, item); state.set(2, item); if (flag) { state.delete(1); } else { state.delete(2); state.has(key); } const saved = state.size; state.clear(); state.set(1, item); state.set(3, item); return state.get(saved).value === 1 ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set(7, false);
```

SHA-256: `2d1763ebbce40e07c852e21e89b45106e767f8a83432af6be650b59f7e3c20fc`.
Five functions, **15 raw/prepared calls**, Number `trace=1`, and both structural
arms are preserved. Both native modes refuse **0/5** with `property call lacks
a current source getter proof`. The final lookup reads a real object's field;
it cannot pass merely by mistaking a present read for absence. The current Map
size is two, independently of the saved size one.

The exact repair replaces only `const saved = state.size;` with
`state.size; const saved = 1;`. SHA-256:
`b0d72c4ef0d1b5017a8d041903d1bf14dde9d93cdf1e998ec5a31f28eb08b249`.
It retains all fifteen calls, the evaluated size read and Number `trace=1`, and
admits **5/5 in both modes**. The true-startup originals/repairs have the same
admission results. Twelve typed Node/interpreter sources, two future observers
and six discriminating mutations pass. Disjoint writes after clear expose the
same join boundary with thirteen calls. Saving size inside each arm already
admits; unequal size-one/size-two arms remain refused and distinguish both
future observations.

Continue a separately justified cardinality fact through the structural join.
Both arms must establish the same exact size for the actual runtime Map; a
union of possible keys or intersection of definite keys cannot substitute for
that proof. Preserve the complete key census for membership queries, aliases,
mutation invalidation, all arms, work limits and independent native rederivation.
A saved scalar must still describe its actual read position. Full native
Bootstrap and direct browser API integration remain unfinished.

Measured artifacts: `/tmp/ctcompile-after-delete-size-final/` and
`/tmp/ctcompile-delete-finish-execution.log`.

## Previous continuation: exact size after deleting a proved key, 2026-09-09

**9781743a** finishes the exact saved-one proof in the historical **544f425b**
source: both modes admit **5/5** with all eight calls and Number `trace=0`
preserved. The complete possible-key upper bound and definite distinct-entry
lower bound must coincide; native presence rederives the fact for the exact
runtime Map. **880ec3ce** executes 23 focused native programs and twelve
refusal/repair families. The full gate passes all **372 compiler CTests** and
**166/166 lit cases**, including **350 integrated native programs/40 lifetime
families**. Only the five established browser failures remain; `HANDOFF.md`
records the complete measured gate.

The next measured source isolates deletion of a known literal key:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {value: 1}; state.set(key, item); state.clear(); state.set(1, item); state.delete(1); const zero = state.size; state.set(1, item); return state.get(zero) === void 0 ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set(7);
```

SHA-256: `d5a66fc6f63b8aeb3976e570a16f9eab2e3aa6302c520b1f9c9b45a9953d3f5f`.
Five functions and **ten raw/prepared calls** remain. Both modes refuse **0/5**
with `property call lacks a current source getter proof`. Node and the
interpreter agree on Number `trace=1`: deletion leaves size zero, and the
subsequent write uses key one.

The exact repair replaces only `const zero = state.size;` with
`state.size; const zero = 0;`. SHA-256:
`315c5f00fd4cee28236efb7779f91865f4549ba6d8398616f5dd366c8f2aa224`.
It preserves every evaluated read/write/delete and all ten calls, proves
complete ownership and admits **5/5** in both modes with the same observation.

All ten fresh Node/interpreter probes agree on typed observations. Deleting
one of two known keys (**aeaca3a0**) also refuses **0/5**; its literal-one repair
(**b1c69bca**) admits **5/5**, with ten calls and Number `trace=1`. A saved size
two read before deletion (**00e098c1**) already admits: a later mutation cannot
change the saved value. Nonidentical both-deleting branch bodies remain unowned
for either startup flag, preserving twelve calls on each source.

The historical formal-key delete-last source **6aa0868a** and its literal-zero
candidate **adeaad91** both remain unowned **0/5**. They have a separate unknown
key/absence obligation; neither is a working repair for the other. The additional
real-clear repair **79587f9c** admits **5/5** with eleven calls. All historical
source bytes are preserved in the integrated execution tests.

Continue the complete possible-key census through deletion. Remove only keys
proved SameValueZero-equal to the deleted key; keep possibly aliasing keys
conservatively, and independently update definite entries and lower bounds.
Preserve current program position, aliases, every structural arm, resource
limits and stale/fresh proof refusals. Native presence must rederive compatible
facts for the actual runtime instance, never a shared schema or an imported
report. Saved immutable sizes must survive later deletes and writes.

A separate own-field observation boundary was measured while finishing the
saved-one tests. Raw **f4c6f000** proves complete ownership but remains **0/5**
because its global result is optional. The exact entry `+ 0` repair **1d39e071**
and field-comparison repair **91e905f8** each admit **5/5**, with all eight calls
and Number `trace=1`; generated code independently reads the present object's
field. Preserve the raw refusal rather than treating it as a key-proof failure.

Evidence: `/tmp/ctcompile-after-one-size-boundary-final-{results,sources}.json`
and `/tmp/ctcompile-map-one-field-candidates-results.json`. Full native
Bootstrap and direct browser API integration remain unfinished.

## Previous continuation: exact saved one after clear/set, 2026-09-09

**5217c13c/a8c77455/f3bbd184** finish the saved-zero boundary: historical
**49663558** admits **5/5** in both modes with all eight calls preserved.
Fourteen focused programs, twenty-three typed references, nine refusal/repair
families and a 128-call saved-object lifetime pass. `HANDOFF.md` records gates;
the full run passes all **372 compiler CTests** and **166/166 lit cases**
(**512/517** total, five established browser failures). Exact-zero proof keeps
the actual Map operations and evaluated size reads in emitted code.

The next unchanged source is:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {value: 1}; state.set(key, item); state.clear(); state.set(1, item); const zero = state.size; return state.get(zero) === void 0 ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set(7);
```

SHA-256: `544f425bc1c5766d6176bbc358947a58ebdd423f4d6a517546c93fa22f67627e`.
Five functions and eight raw/prepared calls remain. Both modes refuse **0/5**
with `property call lacks a current source getter proof`. Node/interpreter
agree on Number `trace=0` because size is one and key one is present.

The exact repair replaces only `const zero = state.size;` with
`state.size; const zero = 1;`. SHA-256:
`d4120093ea2a496cffeacaf09611ce80a23ffbf547700e74ef23d56618c72192`.
It preserves the evaluated read, eight calls and Number observation, proves
complete ownership, and admits **5/5** in both modes.

Eight fresh source probes all agree on typed Node/interpreter observations.
Only that literal-one repair admits. The original, saved-one followed by growth,
and both-one branches remain unowned. Existing identical-arm coalescing reduces
the branch witness from nine raw calls to eight prepared calls; all other
sources preserve their call counts. Startup-only emptiness and clear/set/delete
of the last key each remain unowned even with a literal-zero replacement. Those
are separate whole-invocation/absence obligations, not additional working repairs.

Extend live exact cardinality after clear plus proved writes without confusing
it with a positive lower bound. Unknown invocation contents, possibly equal
keys, partial branch intersections, writes/deletions, and source position must
remain independent. A saved size describes its read-time state after later
mutations; selected values need compatible evidence on every structural arm.
Native presence must derive the same evidence independently for the actual
runtime Map, never its schema family. Preserve fingerprints and work limits.
The source still has an ordinary object payload and current field proof; no
browser or Script dependency belongs in this increment.

Evidence: `/tmp/ctcompile-after-zero-size-boundary-{results,sources}.json` and
`/tmp/ctcompile-after-zero-size-boundary-corrected.log`. Full native Bootstrap
and direct browser API integration remain unfinished.

## Previous continuation: exact saved zero after Map.clear, 2026-09-09

**d83f6b83/c4fa24e3/a680b093** complete owning String field proof, carrier
admission/emission and execution. Historical **88d51f7d** now admits **5/5** in
both modes with its seven calls intact. Twelve native programs, eighteen typed
reference comparisons and a 128-call saved String lifetime pass. `HANDOFF.md`
records the complete gate status.

The next historical source is unchanged:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {value: 1}; state.set(key, item); state.clear(); const zero = state.size; state.set(1, item); return state.get(zero) === void 0 ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set(7);
```

SHA256: `496635583adf728c73d3a48a71a98d7e4733a2bd9a5d14d17d21b1660359b316`.
Node and the interpreter agree on Number `trace=1`. Both modes preserve five
functions/eight raw and prepared calls, but native remains **0/5**, with owner
reason `property call lacks a current source getter proof`.

The exact repair keeps the evaluated read as `state.size; const zero = 0;`.
SHA256: `33aa4c4a24bba6adeec8cc2711e406190f25942f3829ee0c35efae2c9dba36a0`.
It retains the same calls and observation and admits **5/5** in both modes.

A fresh thirteen-source probe agrees on typed Node/interpreter observations in
all cases. Only the literal repair admits; the other twelve remain **0/5** and
unowned, including captured aliases, String leaf fields, repeated clear, saved
zero followed by growth, an equal-zero key, reads before clear/after a write,
and both-clearing/one-clearing branches with both startup flags. These are
reference/admission measurements, not new native executions.

`PrimitiveMapKeyEvidence::sizeLowerBound == 0` means no useful lower bound;
it cannot establish exact zero or SameValueZero equality with literal zero.
`HostContract/CapturedMapBody.cpp` already tracks complete possible keys after
clear. Acquire an immutable exact-empty fact at the actual size-read SSA value,
preserve it through later mutations, and join control-flow facts conservatively.
`Analysis/NativeMap/Presence.cpp` must independently rederive compatible live
facts: its intersected known-entry list being empty does not prove the Map is
empty. Preserve source position, exact Map identity, effects, forged/stale report
refusals and work budgets. A later clear must not rewrite an earlier saved size.

Evidence: `/tmp/ctcompile-after-string-fields-boundary-{results,sources}.json`.
The full Bootstrap Data program and direct browser APIs remain unfinished.

## Previous continuation: owning String leaf fields, 2026-09-09

`0e041bba`, `4b0a1199`, `b4505df6` and `417cd0ac` complete the interrupted
String scalar proof, owning global storage/output and execution tests. The
unchanged **3a99e34c** source and **f0a03c19** literal-copy candidate now admit
**5/5 native** in both modes. The focused gate executes **52 native programs**,
checks **71 typed Node/interpreter observations**, **24 emitted-tag controls**
and nineteen refusal/repair pairs, including empty, escaped UTF-8/NUL and long
String values. Saved String values survive global overwrite, 128 future Map
calls and final owner destruction. Three method-table String sources separately
pass **3/3 native** with actual String tags/bytes and saved owning callables.
Both GCC/Clang and explicit/deduced output pass the no-Script-symbol gate.
These are focused measurements; `HANDOFF.md` records the full session gate.

The exact next historical source is:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {value: 'instance'}; state.set(key, item); return state.size; }
    };
});
host.slot.set('x'); host.slot.set('x'); host.slot.set('y');
var trace = host.slot.size();
```

SHA-256, including its final newline:
`88d51f7dc833c6e17e1c8bf2e54096a504cc53417541f0ccdda4e66569b5f9e6`.
The source keeps **five functions/seven raw and prepared calls**. Node and the
interpreter agree on **Number `trace=2`**. Both optimization modes still refuse
**0/5 native** with owner reason
`property call lacks a current source getter proof`. Owning String globals do
not yet admit owning String fields on the objects stored in the Map.

The exact Number repair changes only `{value: 'instance'}` to `{value: 1}`.
Its SHA-256 is
`80b192281418b92f773f12308bfa3b93ce5160880da6aa3ced1ae29bf1329c3c`.
It preserves all calls and the Number observation, has complete ownership, and
admits **5/5 native** in both modes. Both historical sources remain unchanged in
`native_owned_global_maps/sources.py`.

A fresh thirteen-source probe after the String commits records **all thirteen
typed Node/interpreter comparisons agreeing** and the same outcomes in both
optimization modes. Every source retains five functions and every raw/prepared
call:

| controls | calls each | native | complete owner |
|---|---:|---:|---|
| historical String leaf / Number repair | 7 | 0/5 / 5/5 | no / yes |
| initialized Number field read | 5 | 5/5 | yes |
| String field read, empty, escaped bytes, alias overwrite | 5 | 0/5, four cases | no |
| saved long String field before alias overwrite and Map deletion | 7 | 0/5 | no |
| String/Number write, dynamic field write, prototype effect | 5 | 0/5, three cases | no |
| exact zero after clear / literal-zero repair | 8 | 0/5 / 5/5 | no / yes |

All ten refused sources stop at the same owner reason above. The byte field
reference is exactly
`trace="quoted%20%22field%22%0A%09%25%5C%3D%3B%E9%9B%AA%00tail"`;
the empty field produces `trace=""`. The saved field retains all 64 repetitions
of `saved field contents-` after the object field changes and its Map entry is
deleted. These are String-field reference/refusal measurements; String-field
native execution and lifetime have not yet been established. The mixed-write,
dynamic and prototype controls currently stop at ownership, so they do not
exercise an independent storage admission decision.

The next implementation has three independent obligations:

1. `HostContract/CapturedMapBody.cpp` currently excludes String from the allowed
   leaf-field value mask even though its primitive facts already represent
   String literals. Extend the live source field proof while preserving exact
   allocation/alias identity, reads at their original point, all future method
   paths, ordinary fixed keys, effect invalidation, fingerprints and work limits.
   A later field overwrite must not change the facts for an earlier saved read.
2. `Lowering/Admission/IdentityFields.cpp` admits scalar carriers only. String
   store and read admission must follow actual inferred types independently of
   host ownership. Preserve the separate live initialization query in
   `Analysis/NativeObject/Fields.cpp`: a field schema is not evidence that this
   exact receiver was initialized before this read. Keep absent/optional, mixed,
   stale, exhausted, dynamic and prototype controls independently exercised.
3. `Lowering/ObjectValues/Fields.cpp` currently emits every member, getter and
   setter with `nullable_scalar`. Derive a complete field store census, then use
   the existing owning `nullable_string` consistently in declarations, accesses
   and conversions for String fields. The emitter merges equal property names
   across accepted functions; every emitted member must have one compatible
   carrier across its complete store census. A narrower final read cannot erase
   an earlier String/Number storage conflict. Preserve initial Undefined and
   exact String tag checks; saved reads must own their bytes after mutation and
   owner destruction. Reuse the existing exact output and no-Script gates.

Exact zero after `Map.clear()` remains a separate boundary. Unchanged source
**49663558** reads `const zero = state.size;`, inserts key `1`, then asks whether
`state.get(zero)` is Undefined. Its full SHA-256 is
`496635583adf728c73d3a48a71a98d7e4733a2bd9a5d14d17d21b1660359b316`.
The repair retains the evaluated read as `state.size; const zero = 0;`, hash
`33aa4c4a24bba6adeec8cc2711e406190f25942f3829ee0c35efae2c9dba36a0`.
Both retain five functions/eight calls and Number `trace=1`; the original is
**0/5**, the repair **5/5**, in both modes. `PrimitiveMapKeyEvidence` records a
lower bound, whose zero also means no useful bound. Exact emptiness needs its
own live fact tied to the size read; a zero lower bound cannot establish it.

These isolated results establish no new full-Bootstrap or direct browser API
coverage. Evidence: `/tmp/ctcompile-after-string-boundary-results.json` and
`/tmp/ctcompile-after-string-boundary-sources.json`; the temporary probe driver
is `/tmp/ctcompile-after-string-boundary.py`. Historical measurements below are
preserved with their original source bytes and the boundaries at that time.

## Previous continuation: owning String globals before 0e041bba, 2026-09-09

`df304fdf` and `ab61f9ac` complete the interrupted Boolean proof/execution
continuation. The unchanged **681c8895** source and **d3a90c01** literal-copy
candidate now admit **5/5 native** in both modes. The recovered focused gate
passes **14 CTests in 149.81 seconds**, **34 native programs**, **49 typed source
observations**, **14 emitted-tag mutations**, saved Map lifetimes and all fifteen
refusal controls. Source bytes remain unchanged. `HANDOFF.md` records the full
session gate and the separate BigInt Div/Mod escape increment.

The exact next source is:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); const saved = state.get(key); return state.size; }
    };
});
host.slot.size(); const first = host.slot.set('x'); const second = host.slot.set('y'); host.slot.set('z'); var trace = first * 10 + second;
const fixed = 'owned scalar'; const copy = fixed;
```

SHA-256, including its final newline:
`3a99e34c6ceb6f9a13e85666c3480b7e7c74d52c783ad1420d726d103a655d8a`.
It keeps **five functions/eight raw and prepared calls**, complete ownership,
and the Node/interpreter observations:

```text
copy="owned%20scalar"
first=1
fixed="owned%20scalar"
second=2
trace=12
```

Both optimization modes still refuse **0/5** at
`standard Map identity is unproved with other host/global value reads`.
The byte-preserved candidate changes only `const copy = fixed;` to
`const copy = 'owned scalar';`. Its SHA-256 is
`f0a03c19d5620834869e559ef0aaafede25f6b017c24bae67021d9a460d76d84`.
It retains every call and observation, but is **not a complete repair**:
both modes remain **0/5** at
``store to global `fixed` requires a Number or Boolean global``.
The load proof and owning global storage/output are distinct obligations.

A fresh fifteen-source probe after the Boolean commit records **all fifteen
Node/interpreter comparisons agreeing**, including scalar kind counts, and
identical native outcomes in both modes. Every source has five functions and
eight raw/prepared calls. The unchanged Boolean control is **5/5**; all fourteen
String controls remain **0/5**:

| controls | complete owner | first boundary |
|---|---|---|
| original, empty/byte/long copies, alias chain, duplicate write | yes, six cases | Map identity lacks an exact scalar-read edge |
| original/empty/byte literal candidates, direct String trace | yes, four cases | String global storage/observation |
| read before initialization | no | definite source initialization |
| future method writes the String global | no | current source getter proof |
| String/Boolean and String/Undefined selected values | no, two cases | unconditional straight-line entry ownership |

The byte controls include quote, newline, tab, percent, backslash, equals,
semicolon, UTF-8 and an embedded NUL followed by `tail`; the interpreter prints
`%00tail` without truncation. Empty String prints `""`. These are measured
reference/refusal probes, **not native String execution or lifetime results**.
The last two selected-value controls stop before storage admission and therefore
do not test its full type census; independent raw type/admission controls remain
necessary.

Extend the existing exact scalar edge in `HostContract/Values.cpp`,
`Analysis/OwnedGlobalMethods.cpp` and `Analysis/NativeMap.cpp` for String origins.
Keep one earlier store, original SSA value and scope, every write/effect,
complete future method family, fingerprints and shared work limits.
`TypeInference` already subscribes to the actual stored-value lattice after
removing only independently disproved initial Undefined. Pending, mixed,
optional and boxed types must remain actual lattice outcomes; a host category
or requested observation cannot supply a native String type.

The existing `EmitC/ScalarConversions.cpp::censusScalars` records actual joined
store types in `globalTypes`. Global admission in `Admission/Operations.cpp`
and `Admission/Values.cpp::printable` must agree with that whole store census.
Then select one consistent per-binding owning carrier in
`EmitC/Expressions.cpp::lvalueOfGlobal`, global stores/observations in
`EmitC/Operations.cpp`, and global declarations in `EmitC/Module.cpp`.
`StringValues/RuntimeHelpers.h` already has owning `nullable_string` storage
with distinct Undefined, Null and String tags; reuse it. Its `string_text`
helper coerces absent values to text and **cannot replace an exact String tag
check at the observation boundary**. Missing stores and wrong tags must fail;
empty String remains a present value.

Follow `NativeReference.cpp` and `native-values-fixture.emitc.mlir` for quoted,
per-byte percent output with uppercase hex; preserve embedded NUL and UTF-8/WTF-8
bytes. String literals already use `std::string(literal, byte_count)`. Add
independent String source/prepared query and actual-type propagation tests,
fresh/stale and exhausted proof controls, wrong-tag/missing-store observations,
and saved long-String ownership across mutation/reentry/final destruction.
Use the existing GCC/Clang explicit/deduced and no-Script-symbol gates.

All twenty-four historical source/candidate hashes remain in the checked
fixture. Exact zero after clear, String leaf fields, full native Bootstrap and
direct browser API integration remain separate unfinished boundaries; these
isolated probes establish no new full-bundle coverage.
Evidence: `/tmp/ctcompile-next-string-boundary-results.json`,
`/tmp/ctcompile-next-string-boundary-sources.json` and
`/tmp/ctcompile-next-string-boundary.log`; the temporary probe driver is
`/tmp/ctcompile-next-string-boundary.py`.

## Previous continuation: Boolean globals before df304fdf, 2026-09-09

`06c4a649` and `4d2906d1` complete the exact constant-only Number continuation.
The unchanged **3c1dfd95** source now reaches **5/5 native** in both modes with
eight calls and all five observations preserved. Six proof CTests and seventeen
focused native programs pass; the latter include a 128-future-call sanitizer
lifetime, signed zero/NaN, stale/fresh forgeries and actual scalar dataflow.
The final warning-free **241-step** build finishes **512/517 CTests in
1698.62 seconds**, including all **372 compiler tests**. Lit passes **165/165
in 979.39 seconds**, including **266 native programs/35 lifetime families**.
Only the five established browser failures remain. All fourteen code/test
hashes match HEAD and the devbox. Fresh Bootstrap coverage is **19/574** in both
modes; exact Data remains **0/7 CommonJS/browser, 0/8 AMD**. See `HANDOFF.md` for
complete proof, execution and escape-analysis measurements.

The next original source is:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); const saved = state.get(key); return state.size; }
    };
});
host.slot.size(); const first = host.slot.set('x'); const second = host.slot.set('y'); host.slot.set('z'); var trace = first * 10 + second;
const fixed = false; const copy = fixed;
```

SHA-256, including the final newline:
`681c88958033cc6856eb955d592166118c74d44cc9c5cb4de4bb90aacde2e63d`.
It preserves **five functions/eight calls** and Node/interpreter observations
`copy=false, first=1, fixed=false, second=2, trace=12` (three Numbers, two
Booleans). Both optimization modes have complete ownership but remain **0/5**
at `standard Map identity is unproved with other host/global value reads`.

The exact candidate replaces only `const copy = fixed;` with
`const copy = false;`, preserving calls and observations. Its SHA-256 is
`d3a90c0165fe16ae6f3333d4d084c86e67c70b4ace304d888c4d43440cb46116`.
This is **not a complete repair**: both modes still refuse **0/5** at
`store to global fixed requires a numeric global`. It independently exposes the
second boundary after removing the unproved copy.

Use the existing live scalar-edge seam in `HostContract/Values.cpp`,
`Analysis/OwnedGlobalMethods.cpp` and `Analysis/NativeMap.cpp` for definite
Boolean origins. Preserve complete environment/ownership, SSA scope/order,
all writes, fresh fingerprints, shared work limits and actual stored-value
subscriptions. `TypeInference` already handles the actual Boolean lattice;
no host category or requested observation may manufacture a type.

The distinct admission/output work is in `Lowering/Admission/Operations.cpp`
(global loads/stores), `Lowering/Admission/Values.cpp::printable`, and
`Lowering/EmitC/Operations.cpp` (entry observations). `Emitter.h` currently
records global names only, and every observation calls `global_number`.
The existing `NullableHelpers.h` finite scalar representation already carries
a Boolean tag; do not introduce another value model. Select Boolean output
from the complete actual global-store type census, preserve the Number tag
check, and add an equally exact Boolean check and `true`/`false` output.
Missing writes, a wrong tag, optional or boxed values must not masquerade as
valid Boolean observations.

Extend the original Boolean source and literal-copy candidate in
`native_owned_global_maps/sources.py` and its execution driver, the two existing
scalar query/type functions, and `global-undefined.mlir` controls. Exercise both
false and true, mixed/optional actual types, all writes/effects, stale/exhausted
proofs, source/prepared cloning and wrong-tag/missing-store observation
mutations. Preserve source and call order; do not repair a refusal by changing
the runtime oracle or coercing a Boolean to Number.

The analogous String source **3a99e34c** and candidate **f0a03c19** have the same
two measured barriers, plus owning String storage/escaped output. Undefined,
multiple writes, dynamic globals and future method effects keep their separate
refusals. Exact zero after Map.clear, String leaf fields, full native Bootstrap
and direct browser APIs remain unfinished. All 24 prior source/candidate hashes
are preserved in the checked fixture; `/tmp/ctcompile-constant-first.json`
records current observations and classifications.

## Previous continuation: constant-only Number globals, 2026-09-09

`396b7e46` and `bbedee5b` complete the exact alias/direct-observation boundary:
both original sources now reach **5/5 native** in both modes, preserving eight/
seven calls and trace=12/1. Nineteen focused programs, their refusal/repair and
stale/fresh controls, and both saved-scalar sanitizer families pass. Actual
stored SSA types remain independent of host categories. All 173 historical
refusal classifications and 506 helper rows are unchanged. The warning-free
243-step full gate finishes **512/517 CTests in 1649.36 seconds**: all **372
compiler tests** pass, with only the five established browser failures. Lit
passes **165/165 in 928.06 seconds**, including **250 published programs and
34 lifetime families**. All twelve code/test hashes match HEAD and the devbox.
Fresh Bootstrap coverage stays **19/574** in both modes; exact Data remains
**0/7 CommonJS/browser, 0/8 AMD**. See `HANDOFF.md` for complete measurements.

The next exact source still has complete host ownership but refuses native
output at the standard Map identity guard:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); const saved = state.get(key); return state.size; }
    };
});
host.slot.size(); const first = host.slot.set('x'); const second = host.slot.set('y'); host.slot.set('z'); var trace = first * 10 + second;
const fixed = 7; const copy = fixed;
```

SHA-256, including the final newline:
`3c1dfd95d22834d6ce64c352ab515f628fddd4e03168acf709642d62243c1491`.
It preserves **five functions/eight calls** and Node/interpreter observations
`copy=7, first=1, fixed=7, second=2, trace=12`. Both native modes remain **0/5**,
with `standard Map identity is unproved with other host/global value reads`.
The exact repair replaces only `const copy = fixed;` with `const copy = 7;`.
It reaches **5/5** in both modes with the same calls and observations; SHA-256
`41a33e4066ff78c262289225670eb38736d06e7cbd2c3f55be2176668b084f71`.

`HostContract/Values.cpp::scalarGlobalRead` and
`Analysis/OwnedGlobalMethods.cpp` each reject empty published-call dependency
lists. The existing entry category walk already proves literal/arithmetic
Numbers and single-store aliases, but exposes no scalar edge for this copy.
Extend that existing bounded edge proof for constant-only Numbers, preserving
live entry scope/order, all writes, the complete environment/method family,
current fingerprints and shared work limits. NativeMap must still independently
check harmless loads; TypeInference must still subscribe to actual stored SSA
lattices and retain pending/optional/boxed values. A Number category or a requested
observation must never manufacture a value or native type.

Extend `OwnedGlobalSharedMap.cpp::checkSavedScalarReads` and
`TypeInference.cpp::checkSavedScalarGlobalTypes` alongside the existing native
execution fixture. Add direct empty-dependency controls for stale/forged and
exhausted proofs, a Number value in another function or inaccessible SCF arm,
an unrelated unknown call invalidating the complete environment, and
BigInt/Object initializers that cannot acquire Number authority. Preserve the
nonempty dependency checks and atomic publication after the shared budget
completes. The current twelve source probes do not directly exercise these
four proof hazards. No new carrier or parallel global proof is needed.

All **12 original probes** and **12 candidate edits** retain five functions,
eight calls and Node/interpreter agreement, including explicit scalar type
counts. String observations use the reference's percent-quoted encoding;
Number NaN can print `-nan`. Original cases are nine complete-owner and three
unowned refusals. Only the historical and builtin-spelling literal substitutions
admit; the other ten candidate edits remain 0/5 and are not complete repairs.
In particular, the trace-fed copy still loads a constant-only global after its
first edit. Duplicate writes, reads before initialization, dynamic globals and
future method writes retain their separate controls. Bool/String/Undefined
origins, exact zero after clear, String leaf fields, full Bootstrap and direct
browser APIs remain separate unfinished work.

Evidence: `/tmp/ctcompile-constant-global-next.json` and
`/tmp/ctcompile-alias-next-corrected.json`; all 24 source hashes independently
match the original probes. No source/runtime semantics were changed to obtain
these observations.

## Previous continuation: definite scalar-global initialization, 2026-09-09

`801794d8` closes the saved Number Map-identity boundary. The unchanged
**d74ae2ee** and **867378b1** eight-call sources now reach **5/5 native** in
both modes, trace=3/12. Both execute under explicit/deduced GCC/Clang with exact
requested scalar observations and no Script/VM/AOT symbols. All five host/owner
CTests pass. `1bd5b5ac` also passes fourteen focused native programs, eight
refusal/repair families and the new 128-call sanitizer lifetime; the final
173-case census preserves all 169 historical refusal classifications. The full
warning-free 242-step build finishes **512/517 CTests in 1652.00 seconds**:
all **372 compiler tests** pass, with only five established browser failures.
Lit passes **165/165 in 935.40 seconds**, including **245 published programs
and 33 lifetime families**. Eighteen final code/test hashes match HEAD, frozen
inputs and the devbox. Fresh Bootstrap coverage stays **19/574** in both modes;
exact Data remains **0/7 browser/CommonJS, 0/8 AMD**. See
[HANDOFF.md](HANDOFF.md) for final measurements.

The next exact source passes the complete host, owner and Map identity proofs:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); const saved = state.get(key); return state.size; }
    };
});
host.slot.size(); const first = host.slot.set('x'); const second = host.slot.set('y'); host.slot.set('z'); const alias = first; var trace = alias * 10 + second;
```

Source SHA-256, including its final newline:
`8003b4bc3a35bc936752067dc66c97ab02b36294db685d776d10a53f1a42b388`.
Node/interpreter agree: `alias=1`, `first=1`, `second=2`, **trace=12**. The
source retains five functions, eight calls and its alias store/read, but both
modes remain **0/5 native**, diagnosed as
`store to global alias may be null or undefined; native global observations require a definite number`.
The exact repair changes only `const alias = first;` to
`const alias = first + 0;`; every call, alias store/read, final multiplication
and addition remains. It reaches **5/5**, preserving all observations.

`Analysis/TypeInference.cpp` currently starts each closed-world global-load
join with `absentType`. It cannot yet consume the fresh `OwnedGlobalRoots`
per-load initialization edge. Extend that existing join only when the actual
load, single indexed store and saved value agree with the live edge. Drop the
implicit Undefined seed for that load, then subscribe to and join the real
store operand's lattice. An uninitialized producer must remain pending and
revisit when its type arrives or widens. A boxed producer remains boxed even
when HostContract separately knows a Number category. Neither the category,
an observation name nor a report may manufacture a native NumType.

Keep dynamic globals, multiple writes, read-before-initialization and
stale/exhausted proofs conservative. Do not relax `admission::printable()` or
replace tagged global storage as a shortcut. The raw indirect-call case, whose
host Number evidence coexists with an unproved native type, is a required
independent negative control. The seven-call direct `trace = first` source
hits the same final observation boundary; its `first + 0` repair admits.

All **18** new probes agree on Node/VM and both classifications: ten native,
four unowned, four complete-owner refusals. All original sources remain intact.
Exact zero-size after clear, String leaf fields, full Bootstrap and direct
browser APIs remain separate unfinished boundaries.

## Previous continuation: saved scalar globals, 2026-09-09

`0468fed4` proves entry Number arithmetic and `83c32f0c` gates it. The original
379ccc eight-call sum now reaches **5/5 native**, trace=3, in both modes.
All eighteen focused programs pass Node/interpreter, explicit/deduced GCC/Clang,
no-VM checks and two sanitizer lifetime families. Nine unowned and two
owner-complete refusal families retain exact repairs and live operands. All
169 historical refusal cases were audited without another classification change.
The warning-free full 250-step build passes **512/517 CTests**, including all
**372 compiler tests**; only the five established browser failures remain.
Lit passes **165/165 in 874.79 seconds**, including **233 published Map
programs and 32 sanitizer lifetime families**. Full Bootstrap coverage remains
**19/574** in both modes; see [HANDOFF.md](HANDOFF.md) for measured gates.

The next exact boundary is the unchanged saved-results source:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); const saved = state.get(key); return saved === item ? 1 : 0; }
    };
});
host.slot.size(); const first = host.slot.set('x'); const second = host.slot.set('x'); const third = host.slot.set('y'); var trace = first + second + third;
```

Logical source SHA-256, including its final newline:
`d74ae2ee15979027a09d9ab8a786f3d2f2640617acf9979ef00ce32a226f2065`.
It has **five functions, eight calls, three result stores/loads and two
additions**, trace=3. Both modes now prove complete host ownership, but native
admission remains **0/5** with
`standard Map identity is unproved with other host/global value reads`.
Replacing only the final saved declarations with the original inline expression
keeps all calls and arithmetic and restores the admitted 379ccc source.

`Analysis/NativeMap.cpp` currently exempts only global loads identified by
`OwnedGlobalRoots::lookup()` as the owned ordinary root. The complete host
proof knows the saved scalar categories, but that evidence is not exposed to
this consumer. Extend this existing seam with a bounded live proof of the
actual scalar definitions and uses. Check source ordering, all writes,
current callable/result origins, unknown effects and stale/fresh reports;
never infer a harmless global from its spelling or observed startup value.
Type inference and final native ownership remain independent obligations.
Start with a per-load Number edge recording its single earlier StoreGlobal,
saved SSA value and completed result dependency. Publish it only after the
complete host family and owner checks succeed, sharing their bounded work and
current fingerprint. Rebuild that proof after source transformations; preserve
the constructor, prototype, reflection and unknown-call guards in NativeMap.

The separate eight-call saved size-snapshot source, SHA-256
`867378b10e4c6d18a1902135efc101813e3daadad13b33091c8af25fb89f9a42`,
returns trace=12 and hits the same boundary. Its exact inline repair retains
every call, multiplication and addition and reaches 5/5. Both saved-global
refusals preserve prepared receiver/callee/capture, store/load and binary edges
under fresh/stale forgeries and reruns. All 31 measured cases agree on Node/VM
and both admission modes: **20 native, nine unowned, two owner-complete**.
The repairs preserve call order and arithmetic dependencies but move pure
Number arithmetic earlier. The snapshot repair also publishes `trace` before
the final setter, which neither reads it nor reenters.

Host-only SCF category proofs also require a valid condition and independently
scoped yield operands; entry SCF still has the existing native owner refusal.
Exact zero-size after clear and String leaf-field support remain separate
boundaries. Full native Bootstrap and direct browser APIs remain unfinished.
Evidence: `/tmp/ctcompile-numeric-census.json`, `-saved.mlir`, `-execution.log`
and `-refusal-census.log`. All twelve previous continuation source hashes and
414 historical helper rows remain unchanged.

## Previous continuation: entry numeric results, 2026-09-08

`1c352a82` and `c2b49f99` complete captured standard clear and arbitrary-key
absence, with both original seven-call sources now **5/5 native**, trace=1.
The 21-program focused execution gate passes Node/interpreter, explicit/deduced
GCC/Clang, five refusal/repair controls and three sanitizer lifetime families.
All twenty focused CTests pass across the initial 19/20 run and a corrected
escape expectation rerun. The warning-free full 240-step build completes with
**511/517 CTests**, including the same five browser failures and one old Map
classification (lit **164/165**). **`7011c79e`** retains the historical nine-call
clear source and checks its exact unsupported nullable Number Map-key carrier;
its nine-call repair admits. All twelve focused carrier families and HostContract
CTest pass.
The complete corrected lit rerun passes **165/165 in 776.42 seconds**, including
**215 published Map programs and 30 lifetime families**. All **372 compiler
tests have passing results across the full run and corrected rerun**.

The next exact boundary is **entry arithmetic over published method results**:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); const saved = state.get(key); return saved === item ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set('x') + host.slot.set('x') + host.slot.set('y');
```

Logical source SHA-256 (including its final newline):
`379ccc667b2d463c5fbdc531c53a90ec01c7ba7d8ab578ef1493c3e62f4e281e`.
This unchanged source has **five functions, eight raw/prepared calls, two binary
operations and trace=3**, but stays unowned **0/5** in both modes, diagnosed as
`` unsupported provider behavior through `ctjs.binary` ``. Replacing only its
final declaration with
`host.slot.set('x'); host.slot.set('x'); var trace = host.slot.set('y');`
retains **all eight calls**, reaches **5/5** and returns trace=1. The older
one-call repair also stays admitted, with six total calls.

`HostContract/Analysis.cpp` rejects binary operations in the live environment.
`HostContract/Values.cpp` already proves invocation-result dependencies before
the complete method census. Extend those live proofs only with independently
proved operand/result categories; source order, complete future-call argument
joins and effect checks remain mandatory. Reports or observed startup numbers
cannot supply type authority. Keep the evaluated operands and every call.
A later method key computed from earlier results needs the same bounded
result-dependency proof, without allowing a circular method summary.

Concretely, `capturedMapParameters()` currently accepts a primitive constant or
an exact key in the invocation worklist's local `completedResults`; it cannot
trace a binary expression. Publish no provisional result category until the
complete family recheck succeeds. Keep this category proof separate from
`primitive()`/`truth()`, whose attributes denote actual values and select UMD
branches: an unknown Number result must never become a fabricated Number
constant. Saved global aliases also need their current source order checked.

All **twelve probes** agree across Node/interpreter and both modes: four admitted
5/5 and eight unowned 0/5. Saved results, Number-plus-literal and result-fed keys
remain refused, with original binary and call edges intact. Number/String
concatenation and object coercion controls also refuse. The zero-size witness
after clear is separate: existing lower bounds do not prove that snapshot is
exactly zero. Its eight-call literal-zero repair retains the actual size read
and reaches 5/5. The original seven-call String leaf-field source remains 0/5;
its numeric repair is 5/5. Full Bootstrap and browser API integration are open.

Evidence: `/tmp/ctcompile-map-clear-next.{py,json,log}` and
[HANDOFF.md](HANDOFF.md). Additional numeric globals in the saved-result probe
are included in the interpreter comparison; all original JavaScript is intact.

## Previous continuation: captured Map.clear, 2026-09-08

`f05c3e0b` closes exact-key object-valued Map absence; `3825f3cf` gates
21 programs, ten exact refusal/repair controls and two sanitizer lifetime
families. The original seven-call fresh Undefined source reaches **5/5 native**,
trace=1; historical seven/nine-call post-delete identity sources reach **5/5**,
trace=0. The source hashes below are unchanged. Both affected CTests pass after
preserving when-present payloads through deletion. The full warning-free
250-step build completes at **511/517 CTests**: the same five browser failures
and one old Map refusal classification. Initial lit passes **164/165**.
`4eb2390d` preserves all historical sources while asserting eleven refined type
refusals and gating the original ten-call empty-String deletion program at
**6/6 native**, including future Undefined/empty/distinct-key observations under
explicit/deduced GCC/Clang. The complete corrected lit rerun passes **165/165 in
709.94 seconds**; the published Map gate passes **194 programs and 27 lifetime
families**. All **372 compiler tests have passing results across the first full
run and corrected rerun**. No second full 517-test run is claimed.

The next isolated boundary is **captured `Map.clear()`**. A saved-object identity
read across clear separates method admission from result typing; a fresh read
also needs whole-Map absence. `HostContract/CapturedMapBody.cpp` currently only
admits size/set/get/has/delete and fixed one/two-argument calls. NativeMap,
NativeObject field analysis and EmitC Maps already understand the standard
zero-argument clear method. Reuse those implementations; do not duplicate Map
behavior. Validate every future invocation, live spelling/receiver/arity and
source effects before admitting clear. Whole-Map absence must account for later
writes, possible key aliases, saved values and surviving branch joins.

All **29 measured continuation sources** agree across Node/the interpreter and
both admission modes: five controls reach 5/5, while 24 remain unowned 0/5 with
prepared calls intact. The 22 clear sources include saved identity/fields,
repeated clearing, unseen keys, aliases, later writes and surviving branches.
The original saved-identity case is **five functions, seven calls, trace=1**:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {value: 1}; state.set(key, item); const saved = state.get(key); state.clear(); return saved === item ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set('x');
```

Logical source SHA-256, including its final newline:
`5aefbb04e557a199248b20305a10953764ee1c14b977eff5b7ce2c4a55fdb024`.
Replacing only `state.clear();` with `state.delete(key);` preserves seven calls
and trace=1 and reaches **5/5 native**. The fresh Undefined read variant also
has seven calls/trace=1 but needs the separate whole-Map absence proof; its hash
is `a041e8248d43dac780775c97916939a7e9d88034ce153a24d4576ebbc2f25a16`.

The original entry-addition control remains unowned, **eight calls/trace=3**;
the String-field control remains unowned, **seven calls/trace=2**. Their exact
one-call/numeric-field repairs stay admitted. Full native corpus coverage stays
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725** in both modes, zero pruned;
exact Data remains **0/7 browser/CommonJS, 0/8 AMD**. Direct browser API integration
also remains unfinished. Evidence: `/tmp/ctcompile-absence-recovery-next.json`
and `/tmp/ctcompile-map-absence-next.py`.

## Previous continuation: object-valued Map absence, 2026-09-08

`2593acd7` and `316818b2` close the comparison-only fresh identity boundary from
`bf2fd02e`. The exact local six-call and historical eight-call sources advance
**0/5 -> 5/5 native**, preserving trace=0; both saved-identity repairs remain **5/5**,
trace=1. Four programs agree across Node, the interpreter and standalone
explicit/deduced GCC/Clang, with fresh allocations, field writes and runtime
comparisons intact and no Script/VM symbols. Four lifetime families pass future
calls, reentry, Map/owner release and distinct retained-leaf ASan/UBSan/leak checks.
The focused CTest gate is **15/15**. First full CTest is **511/517** after a
warning-free 245-step build: the same five browser failures plus an existing
specific-diagnostic assertion. `8e603ce5` restores that diagnostic; rebuilt type
and exact diagnostic tests pass. The complete corrected lit rerun passes
**165/165 in 658.71 seconds**; all **372 compiler tests have passing results across
the full run and rerun**. The published Map gate passes **172 programs/25 lifetimes**.
Native Bootstrap stays **19/574**;
exact Data stays **0/7 browser/CommonJS and 0/8 AMD**.

The completed **31-probe** run preserves every source hash and agrees across
Node and the interpreter. Both modes give **13 admitted 5/5 and 18 unowned 0/5**.
The next exact source, `local_absence_delete_undefined`, has **five functions,
seven raw/prepared calls, trace=1**, and remains **0/5 native** with the diagnostic
`property call lacks a current source getter proof`:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {value: 1}; state.set(key, item); state.delete(key); return state.get(key) === void 0 ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set('x');
```

Source SHA-256 (including its final newline):
`f3350b8928408e7ca35dfd7a66da79a26d0c917e3d15ff70b4fb4c8901ea4fb5`.
The exact saved-before-delete repair remains **5/5**, seven calls, trace=1.
Unseeded six-call and numeric-payload seven-call absence controls **already reach
5/5**; the new work concerns object-valued Maps. The unchanged local/historical
fresh post-delete object comparisons (**seven/nine calls, trace=0**) remain
unowned **0/5**, preserving every prepared call.

The host method proof in `HostContract/CapturedMapBody.cpp` needs exact-key
**definite absence**. Its current `present=false` also covers maybe absent, so it
cannot prove Undefined. Preserve absence across exact deletes and known disjoint
writes, invalidate it after potentially aliasing writes, and intersect it at
branch joins. Saved reads retain their earlier values independently of later
Map mutations. Repeated deletion, saving Undefined before reseeding, disjoint
overwrites and possible formal-key aliases all remain refused. Exact same-key
reseeding already reaches **5/5**. Keep the original refused sources and their
exact repairs as independent controls.

`clear()` is a separate unsupported host method. Both fresh-read and saved-object
identity variants remain **0/5**, seven calls, trace=1; replacing `clear()` with
exact `delete()` in the saved-object variant reaches **5/5**. Do not attribute
that refusal solely to an absence result.

Four identical-arm sources have **eight raw calls but seven prepared calls**:
LiftToSCF merges their identical blocks. The deleting versions refuse; their
nondestructive repairs reach 5/5. These probes do not validate a surviving
two-arm join; add a nonidentical safe positive witness for that proof. One-arm
deletion (eight calls) and conditional reseeding (nine calls) remain refused
with both Boolean startup values. The corrected temporary runner records raw
and prepared counts separately without changing any JavaScript. Evidence:
`/tmp/ctcompile-comparison-identity-next.json` and `-next-final.log`.

Entry numeric addition, String/object field carriers, exact Data, full native
Bootstrap initialization and direct browser API integration remain open.
No full-bundle coverage gain is claimed. See [HANDOFF.md](HANDOFF.md).

The [diagnostic and callback increment](native-provider-diagnostics.md) and
[ordinary object payloads](native-provider-objects.md) are implemented behind
explicit host-prefix options. The object increment completes the exact Data
method sequence, including the conflict recorder and object reinsertion. All
runtime effects and the seven-function source denominator remain. This is
prefix discovery; native export admission is still unfinished.

CommonJS and browser entry following reach the end. The realm-fallback probe
completes every Data call and stops at the appended `scriptThis === this`
observer, whose realm comparison is outside the ordinary-object proof. The
remaining observer reads also need own-property/presence evidence before
following them; runtime differential execution still checks all 24 observations.

## Completed provider slice: ordinary object payloads

`set(element, "bs.collapse", instance)` now carries the actual initialized
entry-object identity through private Map storage. The subsequent getters
return that identity and observe its current scalar own fields. The transaction
preserves aliases, distinguishes equal-field objects and discards tentative Map,
object and scalar-global effects together on failure.

This slice requires scalar own contents and a source allocation in the entry
invocation. It refuses missing own-field reads, unknown effects, dynamic fields,
accessors, prototype changes, publication and object/Map cycles. Field writes,
deletion and reinsertion remain runtime. Provider-local object allocations and
general heap graphs still require further work.

## Completed prerequisite: one ordinary global owner

Prefix discovery describes one executed startup path. Bootstrap publishes
callable objects for future callers, so native admission still needs live
callee/type proofs and ownership across the export boundary. The existing
confined method-table-field proof cannot be applied to an arbitrary realm or
global owner. Keep the seven-function denominator and named native refusals
while connecting proved value flow to those consumers.

The [ordinary global owner](native-owned-globals.md) now admits
`var host = {}; host.slot = 42; var trace = host.slot;` at **1/1 native** with
explicit `host-manifest` input. Its bounded live proof carries the allocation
through nominal type inference, final admission and shared owning storage.
`StoredGlobal` and external alias semantics remain; only driver-selected numeric
observations are printed. Eight programs pass Node/interpreter and GCC/Clang in
explicit/deduced forms, including owning lifetime sanitizers. Missing/conditional
initialization, replacement, mutation, stale contracts and incomplete budgets
withhold the owner plan. Without the option, the baseline remains **0/1**.

## Completed: owning method fields and current callees

The live host and [owning source graph](native-owned-global-methods.md) queries
now feed closed value flow, returned-table preparation, owning global fields
and final call-component admission. The constant-getter specimen advances
**1/3 -> 3/3 native** with an explicit manifest; the default stays **1/3**.
Preparation validates the original fingerprint before rewriting a clone and
requires a complete new proof before using that clone. It reconstructs native
facts, preserving real stores despite stale annotations. Standalone GCC/Clang
and post-entry owner/table/callable lifetime checks pass.

## Completed: captured Map environments across publication

The [Map-backed publication specimen](native-owned-global-maps.md) advances
**0/4 -> 4/4 native** with an explicit host manifest and standard Map identity.
Its default remains **0/4**. The complete live callable/source-owner proof
follows the immutable capture, and existing Map/capture/table carriers preserve
allocation identity and shared ownership. Six variants pass Node/interpreter,
GCC/Clang and post-entry Map/table/callable lifetime checks. Completed provider
summaries still supply no authority for future callers, mutable slots or a typed
export ABI; see [the export design](native-export-boundary.md).

The exact specimen has four functions and publishes inside its wrapper.
The importer leaves `factory()` indirect. The host query now proves that actual
callback through the entry's sole wrapper invocation before any rewriting.
Native preparation validates the original fingerprint, works on a disposable
clone and requires complete live proofs after callback/capture preparation and
Map annotation. `prepareNativeMaps()` consumes only proved ordinary-root reads
while checking standard Map identity. Mutable captures, replaced Map bindings,
separate publication, additional factory invocations, reentry, throws, cycles
and incomplete proofs remain refusals.

## Completed proof: Map effects in one published method

The complete live callable proof now permits `size`, `set`, `get`, `has` and
`delete` over primitive contents. It checks every actual method receiver,
argument, capture read and Map alias, including `set`'s return. All effects
execute at runtime; a prior invocation cannot supply a later result. Native
Map type/carrier checks remain independent of this ownership proof.
The mutating publication specimen advances **0/4 -> 4/4 native**, with
Node/interpreter `trace=1`. Fourteen complete native programs and post-entry
mutation/lifetime checks pass; see [the measured gate](native-owned-global-maps.md).

## Completed: one captured Map shared by published methods

With the same explicit manifest and Map identity, the fixed setter/getter
specimen advances **0/5 -> 5/5 native**, retaining Node/interpreter `trace=1`.
A three-method variant admits **6/6**. The complete
live capture census now checks every sibling, its fixed field, primitive body
and current calls. The owner query retains the exact source function chain and
requires identical Map identity/family evidence across all calls. Preparation
checks every plan before lifting and unboxes the shared cell after all members.
The existing returned-table and Map carriers preserve ordinary owning calls.

Nineteen complete programs pass Node/interpreter and explicit/deduced GCC/Clang
execution. The shared lifetime gate retains setter/getter after root/table
release, mutates and reads through them for 1024 calls, distinguishes a fresh
entry's Map, and observes destruction after the last callable releases it.
Both forms pass ASan/UBSan and leak checks. Uncalled/unsafe siblings, replaced
fields, capture-stage mixtures, stale proofs and incomplete budgets refuse.

## Completed: primitive arguments to each published method

The retained `set(key) { state.set(key, 1); return state.size; }` specimen,
called with `"x"`, advances **0/5 -> 5/5 native** with Node/interpreter `trace=1`.
`HostContract/Values.cpp` discovers every current method call before checking
the parameterized family bodies, independently classifies each actual and
retains its live SSA operand. Per-call formal/actual evidence and per-method
primitive tags seed the body proof without recursive property-call authority.
Prepared setter arguments are `(this, new.target, callee, MapEnv, key)`;
the getter remains zero-argument and both capture one Map owner. Existing
capture lifting and typed owning callables need no replacement carrier.

The [Map gate](native-owned-global-maps.md) now passes **25 complete native
programs**, including six new string/number/boolean, repeated, alias and
two-parameter variants. The fifth sanitizer lifetime case keeps typed string
setters/getters after root/table release, mutates caller buffers and exercises
1024 changing keys against independent saved/fresh Maps. Ten argument refusals
preserve all actual operands; source/prepared proof units check both formal
positions, global initialization, environment offsets and every incomplete
budget. These current-call proofs do not establish a future-call ABI contract.

## Completed: independent call-result actuals

`host.slot.set(host.slot.get())` is the retained `parameter_call_result`
specimen, now **5/5 native** with Node/interpreter `trace=1`. A bounded
dependency worklist waits for a completed producer body/effect proof before
using its definite primitive result tag for the consuming formal. It handles
consumer-before-producer declaration order without optimistic recursive
authority. All calls, publication and mutations remain runtime operations.
The gate now passes **34 complete programs**, including nine result variants,
and all five existing lifetime variants; see [the Map gate](native-owned-global-maps.md).

## Completed: locally seeded Map.get results

The retained `result_seeded_map_get` uses
`get() { state.set(0, 1); return state.get(0); }` with the same `set(get())`
entry. Commit `b2466a0` advances it **0/5 -> 5/5 native**, retaining
Node/interpreter `trace=1` and every runtime call. A bounded local last-write
fact supplies an independent result tag only after the complete method proof;
native Map preparation separately proves instance/key presence and the schema.
The seeded lookup used as a later key in one method also advances **0/4 -> 4/4**.
The gate passes forty complete programs and six sanitizer lifetime variants.

## Completed: separate live Map entries

`seeded_earlier_key` inserts `state.set(1, 2)` before `return state.get(0)`.
It now advances **0/5 -> 5/5 native**, retaining Node/interpreter `trace=1`.
Bounded per-key contents and SameValueZero key comparison preserve the earlier
payload across independently disjoint writes/deletes. Seven new programs pass;
the complete [published Map gate](native-owned-global-maps.md) passes 47 programs
and the existing six lifetime variants. No lookup or call is evaluated away.

## Completed: payload types across possibly aliasing writes

Commit `0054611` retains a definite payload tag when every possible overwrite
has that same independently proved tag. `seeded_dynamic_write` advances
**0/5 -> 5/5 native** with `trace=1`. Exact-key writes replace their tag;
possible incompatible writes lose it. Presence and type evidence remain
independent, and no call is evaluated away.

## Completed: nonempty Map.size snapshots

Commit `e8d5cdb` independently proves that a size read from a Map with a
definite entry is at least one. A delete using that saved number cannot erase
key zero. `seeded_dynamic_delete` advances **0/5 -> 5/5 native**, preserving
Node/interpreter `trace=1`. Host result analysis and native presence each derive
their own evidence from live IR; later mutation cannot change the saved number.
The gate passes **58 complete programs** and all six existing lifetime variants
under GCC/Clang and Node/interpreter comparisons. Initial zero sizes, equal
positive keys and equal snapshots remain explicit refusal controls. See the
[Map checkpoint](native-owned-global-maps.md#nonempty-size-snapshots-2026-09-08).

## Completed: distinct-key size bounds

Commit `c900f84` advances `seeded_size_two_entries` from **0/5 -> 5/5 native**,
with Node/interpreter **`trace=2`**. The getter seeds keys 0 and 1, deletes
`state.size`, then returns `state.get(1)`. Both analyses derive a lower bound
from a pairwise-distinct subset of definite keys; different SSA keys alone
never count twice. Each size read examines at most 64 candidates and retains
its immutable bound across later mutation. The native gate passes **63 complete
programs**, eight size programs and ten size-key refusals, plus six existing
sanitizer lifetime variants. Source/prepared host proof budgets are **2278/2372**;
the independent presence lit passes seven positives and fourteen refusals.
See [the Map checkpoint](native-owned-global-maps.md#distinct-key-size-bounds-2026-09-08).

## Completed: homogeneous boolean and owning-string Map payloads

The existing `result_seeded_bool` and `result_seeded_string` specimens now
admit **6/6 native**, preserving Node/interpreter **`trace=2`**. Their existing
host proofs now feed supported native storage, construction and reads.
The published gate passes **69 complete programs** with all seven lifetime
variants, including saved strings after overwrite, deletion and Map destruction.
Ordinary missing reads keep false/empty-string distinctions through existing
nullable carriers; string value snapshots own their elements. Boolean snapshots
remain refused.

## Completed: closed mixed key/payload storage and exact read types

`result_seeded_mixed_contents`, `result_seeded_join_reseed` and
`result_seeded_bool_string_contents` now admit **6/6 native**, with
Node/interpreter traces **2/3/2** and all **9/10/9** calls retained. Exact
Bool/Number or Bool/String schemas use finite `std::variant` alternatives in
the existing owning storage. Separate literal-write evidence proves each read's
presence and scalar result without narrowing the full Map schema. Key comparison
preserves SameValueZero and false versus zero. The published gate passes **76
programs** and all **eight lifetime variants**; the local mixed gate tests both
associative and ordered storage. See [the Map checkpoint](native-owned-global-maps.md#closed-mixed-map-storage-2026-09-08).

## Completed: saved scalar Map reads through writes

Commit `d9a4b04` advances `saved_read_write` from **0/6 to 6/6 native** in both
modes, retaining Node/interpreter **`trace=1`** and all **twelve calls**. Saved
Boolean, Number and String tags survive known source-entry mutations separately
from current contents. A later write consumes that independent scalar fact;
missing reads, possible aliases and differing branch facts remain conservative.
The gate passes **83 complete programs**, including seven saved-chain programs,
three missing/deleted refusals and all nine lifetime sanitizer variants.
The local mixed gate passes 21 observations and ten refusals across both
storage implementations. See [the Map checkpoint](native-owned-global-maps.md#saved-scalar-reads-through-writes-2026-09-08).

## Completed: a saved scalar selected across control flow

Commit `53b44b9` advances `saved_join` from **0/6 to 6/6 native** in both modes,
with Node/interpreter **`trace=3`** and all **sixteen calls preserved**. The live
host proof checks both structured conditional arms and intersects their mutable
contents. A selected scalar gets a type only when both yielded values prove
the same tag. Constant predicates cannot discard an arm. The gate passes
**89 complete programs**, six new conditional programs, four new refusals and
all **ten lifetime sanitizer variants**. Host units cover 25 conditional rows
each in source/prepared form and all 2501/2626 incomplete budgets. The local
mixed gate passes 27 observations and twelve refusals. See
[the Map checkpoint](native-owned-global-maps.md#saved-scalar-values-across-conditionals-2026-09-08).

## Completed: a guarded saved read after conditional deletion

Commit `677714b` admits **6/6 native** in both modes for the saved-value ternary
`state.has('other') ? state.get('other') : state.get('')` after conditional
deletion. All eighteen calls and Node/interpreter `trace=2` remain. Host/native
proofs keep membership separate from the payload tag valid whenever present;
joins intersect both independently. Live same-key guards can restore membership,
while stale observations and unknown/conflicting payloads still refuse.

The gate passes 95 published programs, eleven lifetime sanitizer variants,
35 local mixed observations, seventeen local refusals and 44 host rows each in
source/prepared form. All guarded budgets, forged edits, both runtime flags and
saved owning Strings after final Map release pass. See
[the Map checkpoint](native-owned-global-maps.md#guarded-saved-reads-after-conditional-deletion-2026-09-08).

## Completed: scalar short-circuit result refinement

Commit `58decfe` admits **6/6 native** in both modes for
`(state.has('other') && state.get('other')) || state.get('')`, preserving all
**eighteen calls**, three getter conditionals and Node/interpreter **`trace=2`**.
The live proofs retain finite primitive alternatives by truthiness and refine
only the tested SSA value in each arm. Every arm's effects remain checked.
A local Bool/String temporary owns its String; independently rederived exact
write tags bridge wider SCF inference without changing the Map storage schema.

The gate passes **103 complete programs**, **twelve lifetime sanitizer
families**, **49 local observations and 21 refusals**, and **82 host rows** each
in source/prepared form. Nine new published refusals, all short-circuit budgets,
forged edits and reruns pass. Empty String, false and zero still select the
fallback, and saved future Strings survive final Map release. See
[the Map checkpoint](native-owned-global-maps.md#scalar-short-circuit-results-2026-09-08).

## Completed proof: a finite nullable result contract

Commit `fa29d49` preserves String/Null/Undefined alternatives through the complete
host body dependency worklist and actual/formal census. Startup truthiness is
widened within every category; unseeded cycles and unknown producers still
refuse. The **82 earlier plus 32 nullable host rows** pass in source/prepared
form, with exhaustive budget cutoffs and live mutation controls.

The normalized nullable setter `state.set(key || 'missing', true)` now admits
**6/6 native** in both modes, retaining all **eighteen source calls** and
Node/interpreter **trace=3**. Stored callable signatures use owning
`nullable_string`. A current per-operation key proof keeps the storage String
without narrowing the nullable SSA value elsewhere. Commit `8d80629` passes **110 complete published programs**,
including seven nullable cases and thirteen lifetime sanitizer families. Both
C++ compilers, identity observations, forgeries, reruns and budgets pass; all
**165 lit cases** pass. Final CTest is **512/517**, all **372 compiler tests**,
with only the five recorded browser failures. See
[the nullable checkpoint](native-owned-global-maps.md#finite-nullable-published-results-2026-09-08).

## Completed: owning nullable String Map keys

Commit `5e63d993` admits the original **eighteen-call** nullable-key program
at **6/6 native** in both modes with Node/interpreter **trace=3**. The smaller
**eleven-call** `Opt<Str>` case also admits **6/6**, trace=2; the **thirteen-call**
String/Null/Undefined/empty String identity witness gives trace=4, versus trace=2
for its normalized control. Both layouts use owning tag-aware keys, including
Boolean composition as `std::variant<bool, ctnative::nullable_string>`.
The per-use key proof remains separate from storage; nullable payloads and
unsupported snapshots still refuse. Local native/VM/compiler and sanitizer
tests pass, as do all 118 published positives and fourteen lifetime families;
final whole-suite results are recorded in [HANDOFF.md](HANDOFF.md).

## Completed: owning nullable Map payload storage

Commit `6e150949` admits the eleven-call nullable write and twelve-call
readback at **6/6 native** in both modes, preserving Node/interpreter trace=2.
Owning `nullable_string` storage preserves Null, Undefined and empty String;
closed Boolean composition reuses `std::variant<bool, nullable_string>`.
Mixed reads still require independent payload facts. The original eighteen-call
mixed write admits **6/6**, trace=3. A deleted read correctly returns Undefined;
saved results survive later writes, deletion and final Map destruction.

The first driver passed all 123 positives and fifteen lifetime families.
After promoting the deleted-read case, all six new payload programs and the
remaining refusal controls pass focused checks. Seven targeted lit cases,
four host/owner CTests and the 63-observation/34-refusal local gate pass.
The full 124-program driver and all 165 lit cases pass. Final CTest is
512/517, all 372 compiler tests, with only the five recorded browser failures;
see [the payload checkpoint](native-owned-global-maps.md#owning-nullable-payloads-2026-09-08).

## Completed: finite nullable read facts in mixed storage

Commit **`c4d6bf07`** advances the exact fourteen/nineteen-call readbacks to
**6/6 native** in both modes, retaining trace=2/3. Per-instance/key payload
alternatives seed `Opt<Str>` independently of the Boolean/nullable storage
schema. All four new published programs pass explicit/deduced GCC/Clang,
identity and saved-payload lifetime checks. Three complete-owner refusals and
their exact admitted repairs, fresh/stale forgeries, four budget families and
the 69-observation/39-refusal local gate pass. The complete 128-program driver,
sixteen lifetime families and all 165 lit cases pass. Final CTest is 512/517,
all 372 compiler tests, with only the five recorded browser failures; native
corpus and exact Bootstrap Data counts remain unchanged.

## Completed: finite nullable host-result alternatives

Commit **`ed1a833c`** advances the exact fifteen-call `get -> set -> size(key)`
source **0/6 -> 6/6 native** in both modes, preserving trace=1. The independent
host body proof keeps finite payload alternatives through live writes, joins
and reads, then the complete actual/formal census supplies the next method's
nullable input. Saved read evidence survives later mutation. No native schema
or Presence annotation supplies host proof authority.

Five programs pass both modes, explicit/deduced GCC/Clang, exact identities and
saved-result lifetimes in **`fe867e6f`**. Four independent host refusals and their
exact repairs pass, together with fresh/stale reports, reruns and budget
cutoffs. The full 253-step build passes. Initial CTest is 511/517; after the
test-only `077328ae` ownership/carrier correction, the complete lit rerun passes
165/165, including all 133 programs and seventeen lifetime families. All 372
compiler tests pass across both runs; five established browser failures remain.
[HANDOFF.md](HANDOFF.md) records the separate measurements and logs.

## Completed: same-method invocation result dependencies

Commit **`d7148fcf`** advances the exact fifteen-call `set(set(get(false)))`
source to complete ownership and **6/6 native** in both modes, trace=2.
Independent invocation body proofs supply only source-ordered result facts;
a second mandatory all-call/all-sibling census authorizes the published family.
Unknown results, forward edges, cycles and unsafe later actuals still refuse.
Four source programs, nested saved-result lifetime, five new refusal/repair
families and three budget sweeps pass in **`a1b11e80`**. The combined focused
gate is **12/12 CTests in 61.47 seconds**. The full 137-program/eighteen-lifetime
driver and all 165 lit cases pass. The warning-free 243-step build completes
with CTest **512/517**, all 372 compiler tests and 140/145 browser tests, leaving
only the five established browser failures; [HANDOFF.md](HANDOFF.md) records
the measurements.

## Completed: published method-local leaf object ownership

Commit **`e9f8e33c`** advances the exact seven-call `{}` and `{value: 1}`
programs **0/5 -> 5/5 native** in both modes, retaining trace=2. The body proof
checks every future local leaf allocation and fixed scalar write; exact source
operations reach the host and owner censuses. An object-writing sibling removes
unknown Map reads' primitive guarantee before any invocation result is proved.
The existing native object identity pass now runs in host Map preparation too,
followed by the final live owner proof. No carrier or emitter code changed.
The old four-function empty-object refusal advances to 4/4, trace=1.

All four host/owner CTests pass. Commit **`2efcbbe2`** passes eight new and one
historical program in both modes, explicit/deduced GCC/Clang, independent
identity/field observers, saved-callable sanitizer lifetime, thirteen
refusal/repair families and three budget sweeps. The complete 146-program/
nineteen-lifetime driver and all 165 lit cases pass. The warning-free 244-step
build finishes with CTest 512/517 in 1199.12 seconds: all 372 compiler tests and
140/145 browser tests, leaving the five established browser failures. All
nineteen code/test paths match HEAD, frozen input and final devbox sources;
[HANDOFF.md](HANDOFF.md) records the measurements and preserved test failures.

## Next: method-local object readback and identity

The eight-call saved-object source stores `{value: 1}`, saves a Map.get,
overwrites/deletes the entry and compares exact identities. It gives
Node/interpreter trace=1 and remains unowned **0/5 native** in both modes.
The distinct equal-field eight-call control and fresh-read-after-delete nine-call
control give trace=0 and retain the same refusal. Prove the local object origin,
presence and identity without treating a public object result as already owned.
Final probes isolate a direct fixed own-field read at five calls, trace=1,
and saved same-key identity, Map.get field-read and guarded-read controls at
six calls, trace=1. All remain unowned 0/5 in both modes. The two-stored-object
distinct control is seven calls, trace=0; the comparison-only fresh-object
control is six calls, trace=0 and adds a separate native family limitation.
Saved references must retain their original object after replacement/deletion
and observe later writes through aliases. All sixteen measured sources,
including the unchanged historical 8/8/9-call cases, are in
`/tmp/ctcompile-leaf-object-next.json`. The source and proof obligations are in
[the Map boundary](native-owned-global-maps.md#next-boundary).

String fields and Object/String Map payload carriers remain separate. The
historical nested leaf-writing sibling now has complete ownership but stays
0/6 at that mixed carrier; the original eleven-call `{value: 'instance'}` source
remains unowned0/6, trace=2. Provider entry tokens cannot authorize future local
objects, and ordinary ownership supplies no throwing-call or general export ABI.

The exact Bootstrap getter at vendor line 17 also needs nested/object payloads.
Exact Bootstrap Data, general realm owners and future-call contracts remain
unfinished; the measured corpus counts and full gate are in [HANDOFF.md](HANDOFF.md).

Exceptions do not make a callback or allocation inert. Native try/catch covers
one acyclic handler with homogeneous number, boolean or owning string throws
and proved nonthrowing primitive helpers in its try and catch. Checked callee
resolution now follows preserved status/register vectors. Target verification
also follows homogeneous primitive payloads through defined EmitC helpers.
Explicit `ctjs.invoke` regions now separate the normal result from an implicit
thrown payload and pre-call state; bounded type flow now covers both payloads
and the invoked helper's normal returns. Ordinary call inference remains
conservative on throw exits. An internal `CheckedInvocations` recovery mode now
constructs those regions and connects the two completions to the enclosing try
through a value-only tuple, retaining pre-call state on unwind. The default
mode and all native throwing-call refusals remain. An additional
`EffectCheckedInvocations` mode validates other status and continuation effects
before adopting the recovered clone. Commit `e4b2d5f` proves source global
callee lookups from the complete live declaration/store/use and effect census,
with separate bounded primitive completion facts. The three direct-call source
specimens now recover in this internal mode, preserving assignment and argument
state. The focused gate passes all 3521 source-binding budget cutoffs and 937
effect cutoffs, nineteen live binding mutations and uncalled-throw controls.
Native admission still needs the complete call component; target emission must
consume the accepted invokes. Native throwing callees, general
finally, reentry and object payloads require
further work. The [source invocation gate](native-source-invocations.md) now
retains four source programs, sixteen functions and eleven observations for
assignment snapshots, prior normal calls, argument mutation and receiver/key/
getter/argument order. All source throwing calls still refuse native lowering;
the document identifies the remaining admission and emission obligations.
Normal-return provider facts cannot authorize an exceptional continuation.

Retain the eleven scenarios in
[the exact host audit](../../tools/check/bootstrap-host-contract-audit.py),
including callee-order observation `1234` and throwing/reentrant sinks. Preserve
vendor/program hashes, runtime methods and observer branches. Plan parts 24 and
25 remain the native type and ownership requirements.
