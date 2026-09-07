# Planned private Map mutation summaries

**Status: design only, based on `031f62a`.** No mutation summary or additional
native admission is implemented by this document. The next proposed option is
`follow-provider-mutations=true`, requiring both `follow-provider-reads=true`
and `follow-publication=true`. The default pipeline and existing options retain
their current boundaries.

This extends the [checked host prefix](native-host-prefix.md), whose retained
factory/cell identities can describe a private Map before an unknown effect.
It does not replace the independent native type, ownership, closed-call or
host-slot proofs. Global exports do not become closed native objects because
one executed entry prefix was observed.

## Measured starting point and exact next operation

Bootstrap 5.3.8's exact Data declaration is in
[bootstrap.bundle.js](../../ctbrowser/vendor/bootstrap/bootstrap.bundle.js):
the outer Map is declared at line 10, `set` starts at line 12, the conflict
diagnostic is on line 15, `get` is on line 17, and `remove` starts at line 18.
[bootstrap-data-probe.py](../../tools/check/bootstrap-data-probe.py) preserves
the 1,101-byte vendor fragment, SHA-256
`3eebf5b59691eff3c32478e06fb44a5c9eb554bc045d2ff2e20901813a9f99c8`.
The remainder of the factory is replaced with `return e`; this is an exact
Data prerequisite probe, not complete Bootstrap initialization.

At `031f62a`, the three provider-read probes resolve four calls: factory
`fn$3`, absent `get` (`fn$5`), absent `remove` (`fn$6`), and the first `set`
(`fn$4`). Only the first two method invocations have completed summaries.
Each contains one empty-Map `has` read. UMD branch counts are 2/5/6 for
CommonJS/browser/realm fallback, and native admission remains 0/7 in each.
The existing three boxed differentials pass 19/19/24 observations with GC
stress; Node independently matches those 62 observations.

The CommonJS baseline was checked against the devbox artifact
`build/ctcompile/test/bootstrap-host-prefix/commonjs_provider_reads/evidence.json`
and its `prepared.mlir`. Its program SHA-256 is
`cc6c3960099f201b3e6c17f556c4cc5e08a7a47190e08cfba5628be40ea0a181`.
The report's current public boundary is the first `Data.set` call. Inside
`fn$4`, the existing reader derives `outer.has(element) == false`, then stops
at `ctjs.get_property outer["set"]`. The subsequent source operations are:

```mlir
%outer = ctjs.load_upvalue %arg2[0]
%set = ctjs.get_property %outer[%key_set]
%Map = ctjs.load_global "Map"
%inner = ctjs.construct %Map(%Map)
%stored = ctjs.call %set(%outer, %arg3, %inner)
```

The actual IR then reads `outer.get(element)`, tests the inner Map's key and
size, and calls `inner.set(key, value)`. These operations must remain in the
original method body, including allocation and failure behavior. Merely
allowing the first `set` call without carrying its post-state would make the
existing empty-Map reader incorrect on later invocations.

## Proposed scope and predicted progress

Support finite, known private Map state through standard zero-argument nested
Map construction and `has`, `get`, `size`, `set` and `delete`. Source keys may
be supported primitives or already proved fresh ordinary object identities.
Values may be supported primitives or private nested Maps. Ordinary object
payloads, arbitrary callbacks, snapshots, errors and escaping resources remain
outside this slice. A summary returns a proved primitive and leaves every
source method and observer branch at runtime.

The following is a **prediction to validate after implementation**, not a new
measurement. The unchanged exact programs should resolve 13 calls in total:
one factory, eleven completed method invocations, and the conflicting `set`
that reaches the next unsupported effect.

| Entry call after the factory | Expected normal result | Predicted provider state/effect |
|---|---|---|
| `get(absent, "bs.alert")` | null | outer remains empty |
| `remove(absent, "bs.alert")` | undefined | outer remains empty |
| `set(element, "bs.alert", 42)` | undefined | allocate inner A; two Map.set calls |
| `set(other, "bs.alert", 21)` | undefined | allocate distinct inner B; two Map.set calls |
| `get(element, "bs.alert")` | 42 | read inner A |
| `get(other, "bs.alert")` | 21 | read inner B |
| `set(element, "bs.alert", 43)` | undefined | replace existing entry in A; one Map.set call |
| `get(element, "bs.alert")` | 43 | read the replacement |
| `get(element, "bs.missing")` | null | missing inner get returns undefined; source fallback returns null |
| `remove(element, "bs.missing")` | undefined | one unsuccessful Map.delete call; A stays nonempty |
| `get(element, "bs.alert")` | 43 | failed deletion preserved A |
| `set(element, "bs.collapse", 99)` | no summary | stop at the conflict arm's `load_global "console"` |

The eleven completed methods predict two nested allocations in addition to
the factory's outer Map, five executed `set` operations and one executed
`delete` attempt. That delete does not remove an entry; reports must distinguish
an operation from a successful mutation. The original factory report still
contains one resource allocation and three capture edges. Nested allocations
belong to their method invocation reports, not to the factory source site.
UMD branch counts should remain 2/5/6, and the seven-function native denominator
and current 0/7 admission are separate measurements to rerun.

## State and identity schema

Keep provider state separate from the prefix's ordinary object heap. Use these
internal identities, derived from live IR and the executed path:

| State | Required identity and contents |
|---|---|
| Factory invocation | Existing one-based invocation token carried by the actual closure value; bound to the exact factory CallOp |
| Captured root | `(factory invocation, captured ConstructOp)` maps to one global provider MapId through the checked final cell/capture edge |
| Nested allocation | Fresh MapId per executed method invocation and ConstructOp occurrence; retain both the method CallOp and source ConstructOp as provenance |
| Ordinary object key | Existing prefix object token; aliases share a token, separate CreateObject executions do not |
| Primitive key | Tagged undefined/null/boolean/number/string value with SameValueZero equality |
| Map state | Insertion-ordered entries of `(KnownKey, KnownValue)`; known values are primitives or MapIds |
| Builtin member value | `(MapId, member)` obtained by a checked live property lookup, used with the matching receiver at its call |

Neither a ConstructOp nor a function symbol alone identifies an allocation:
the two exact `set` invocations execute the same inner ConstructOp but must
produce A and B. Likewise, copying the first factory's method into a second
table must retain the first factory's capture token. Object and Map IDs need
separate tagged domains, even if both counters start at zero.

Map key comparison must not use coercion or truthiness. Numbers use
SameValueZero: NaNs compare equal, and signed zeros compare equal; boolean,
number and string keys remain different. Canonicalize signed zero when storing
a numeric key. Object keys compare only their proved tokens; reading their
fields or invoking `valueOf`/`toString` is unnecessary and unauthorized. An
unknown key cannot be inserted. A nonempty lookup with unproved equality must
stop; do not turn an unproved match into absence.

Replacement preserves insertion position, deletion removes the entry, and
reinsertion appends a new entry. This state representation does not authorize
`keys`, iterators or Array.from yet. Allow Map values only within the proved
private resource graph, and reject Map-valued cycles in this first slice.
An object key's token describes identity and retention, not an acyclic native
owner for that object's reachable fields.

## Checked operation rules

The existing `closed-source-v1` promise of unmodified initial intrinsic
prototypes and explicit initial Map identity remains required. Keep all source
replacement, reflection, descriptor, unknown-effect and receiver guards.
The source's standard Map binding must reach construction directly with the
same callee and new.target and no arguments. Accepting a provider name in a
report or manifest must never replace this source proof.

| Operation | Required proof and abstract normal-result behavior |
|---|---|
| Captured load | Actual invoked closure's implicit callee argument and checked capture index resolve the correct final cell/MapId |
| `new Map()` | Fresh, empty MapId in the current transaction; runtime allocation/failure retained |
| Member lookup | Proved private Map with no own override and still-standard prototype; only the supported member names |
| `has`/`get` | Matching builtin member and actual Map receiver, one evaluated key; known membership or stop |
| `size` | Standard getter on the proved Map; number of live entries |
| `set` | Matching member/receiver, two evaluated operands, supported key/value; update or insert; return receiver MapId locally |
| `delete` | Matching member/receiver, one evaluated key; remove if present and return boolean |
| Method return | Known primitive only; Map/object/method/closure escape remains refused |

The source argument evaluation has already occurred before a builtin call is
summarized. `set`, `get`, `has` and `delete` do not coerce keys or call user
JavaScript, but unsupported argument expressions still stop when encountered.
Wrong receivers, detached calls, pending new.target, proxies, own method
overrides, unknown providers and aliases to an unproved builtin receive no
special exemption.

The method-path interpreter may use MapIds returned by `get` to reach a nested
Map inside the same checked invocation. It may use a primitive `get` result
after normal return to discover the next source entry call. Returning an
ordinary payload object is a different proof: the compiler would need its
exact live identity, aliases, possible accessors/proxy behavior, mutations
and ownership. The existing source object-key token or a finite set of past
`get` results establishes none of those requirements. In particular, this
slice cannot authorize the later exact `get(...).value` or instance-identity
observation merely because the Map was tracked earlier.

## Transactions and bounded work

1. Resolve the actual entry callee and its immutable captured roots. Snapshot
   the committed provider state and allocation-ID boundary before entering the
   method. The existing live publication table supplies the callee; it is never
   frozen across a source write or replacement.
2. Interpret only the selected path into a transaction-local state. Reads see
   preceding writes in that transaction. Record builtin effects and fresh
   allocations locally. Do not append reusable-body branches or calls to the
   global rewrite plan.
3. Commit only after a supported primitive normal return, complete resource
   retention checks and sufficient remaining work. Atomically publish all
   changed Map states, fresh IDs, result and summary facts. An accepted method
   cannot expose a private Map through a return, property, global, closure or
   unknown call. New Maps may be retained through proved captured Map entries;
   unretained temporaries must not masquerade as retained factory resources.
4. On a selected unsupported write, call, property read, throw, escape or
   control shape, discard the entire attempted summary and stop the prefix at
   that method invocation. Keep previously completed prefix facts, but supply
   no post-state, partial effects or subsequent-call proof for this invocation.
   Do not resume from the old pre-call state after rejecting a mutating path.
5. Allocation/Map.set failure paths remain runtime. The post-state is
   conditional on normal return; it must not be applied in a catch/exception
   continuation. Exception handling and reentry across this boundary are
   unsupported. Budget exhaustion or an invalid/stale contract discards the
   entire analysis result, including earlier candidate rewrites.

Charge the existing `max-steps` budget for every visited operation, capture
edge, key comparison, copied state entry, insertion/removal, allocation and
resource-graph edge. A transaction copy is proportional to the copied state,
not one nominal step. A copy-on-write implementation may charge only copied
nodes, but must retain the same refusal behavior when work is exhausted.
Bound structured nesting explicitly; loops, recursive calls and repeated
unproved contexts stay unsupported. Check ID/count overflow before allocation.
Do not grow a speculative heap or report vector after the budget is exhausted.

## Integration sequence

Add a private `HostContract/ProviderState.h/.cpp` containing the identity,
SameValueZero and transactional Map-state helpers. `PrefixFactories.cpp`
registers its successful captured roots in that state. Extend `PrefixReads.cpp`
into a shared provider-path interpreter, with mutation operations enabled only
by the new option. Crucially, the mutation mode must route subsequent reads
through committed state rather than calling the old hard-coded empty reader.
Existing read-only mode keeps its present behavior and tests.

Wire the option through `HostPrefix.h`, `Prefix.h`, `PrefixAnalysis.cpp`,
`PrefixValues.cpp`, `PrefixPass.cpp` and `Passes.td`; add the helper TU to
`Analysis/CMakeLists.txt`. Summaries should bind exact entry/method/factory
calls and MapIds, distinguish factory from nested allocations, and record
executed mutation operations and their results. Reports remain diagnostics,
not admission authority; `full_host_contract_claimed` remains false.
Keep this work out of native carriers, ClosureLifting, Admission and EmitC.

The only emitted change remains naming further actual entry call targets.
Preserve actual callee values, raw receivers, argument evaluation and call
order. Keep reusable methods, observer branches, source stores, construction
and provider operations unchanged. Unknown later calls or accessors still
stop discovery, and the existing in-place shared-body continuation guard is
unchanged. An ordinary call's effective `this` stays unknown.

## Acceptance, refusal and lifetime gates

- Add focused source/IR tests for set/get/has/size/delete, replacement, failed
  deletion, deletion/reinsertion, NaN and signed-zero keys, differently tagged
  primitive keys, aliased versus distinct object keys, nested Map identity and
  two factory invocations. Repeated execution of one nested ConstructOp must
  produce distinct MapIds. Method/table replacement and copied methods must
  use the current callee and original capture owner respectively.
- Refuse unknown keys/values, object payloads and object/resource returns,
  detached or mismatched receivers, provider/prototype/own-member replacement,
  captured binding writes, resource cycles/escape, unproved branches and
  arbitrary callbacks. Test a successful write followed by an unknown call,
  throw or escape: no partial post-state or summary may survive, and a later
  method must not be resolved from stale pre-call state.
- Exhaust work during lookup, state copying, nested allocation and after a
  tentative mutation. Verify all usable proofs are withheld. Stale manifests,
  forged reports, omitted prerequisites and reruns after source mutation must
  not preserve state or invocation IDs.
- Extend the exact prefix driver with three new mutation modes. Preserve
  vendor/program hashes, source-function counts and native refusal accounting;
  assert the predicted 13 targets, eleven completed methods and new error
  boundary only after measuring them. Compare reusable method IR byte-for-byte
  and observer branch counts before/after.
- Compile boxed script/wrapper entries and compare all 19/19/24 observations
  with the interpreter and independent Node realms. Keep method/provider calls
  runtime. GC-stress fixtures must retain keys and nested Maps after factory
  return and publication replacement, preserve aliases, distinguish factories,
  and exercise allocation churn. These are runtime semantic/lifetime checks,
  not a standalone native or RAII claim.
- Retain exact error-order, accessor and reentrant-error oracle controls and
  an executed wrong-observation negative control. Run existing publication,
  provider-read and native admission regressions before the full devbox gate.

The newly reachable conflict arm begins with console lookup, followed in the
actual IR by error member lookup, Array.from member lookup, Map.keys lookup
and call, Array.from call, indexed extraction, concatenation, then error call.
The initial Array identity alone does not authorize the snapshot or console
effects. A source-created console object does not prove its callback harmless:
the callback can mutate publication, reenter Data, throw or change provider
state. No Map state or current export target may be carried across it without
a separate checked effect/reentry proof. The predicted 4-to-13 target advance
therefore leaves all error-path effects and full native Bootstrap admission
explicitly unfinished.
