[Back to bootstrap-provider-next.md](../bootstrap-provider-next.md)

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
`test/CTNative/Ownership/native_owned_global_maps/sources.py`.

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
`test/CTNative/Ownership/native_owned_global_maps/sources.py` and its execution driver, the two existing
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
