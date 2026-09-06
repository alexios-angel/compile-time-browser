# Bootstrap Data host and export contract

Design and source audit, 2026-09-05. **The contract below is proposed, not an
implemented compiler API.** The next useful host slice is the exact Data probe
under an explicitly closed application driver. It must connect exported slots
and retained factory values to the existing ownership/call proofs. Recognizing
the names `bootstrap`, `module.exports` or `console.error` is not such a proof.

## Exact source and current evidence

[`tools/check/bootstrap-data-probe.py`](../../tools/check/bootstrap-data-probe.py)
retains the vendor UMD wrapper and Data declaration verbatim, then replaces the
rest of the factory with `return e`.
This is not the full Bootstrap factory. No isolated factory variant is used by
this audit, and an isolated factory's native count must never replace the exact
publication probe's count.

The current vendor is Bootstrap 5.3.8:

| Provenance | Value |
|---|---|
| Vendor SHA-256 | `5b29f1692a632853edc37b45bc1deedd595a777c9b234e8262ccd74ebfcf3d65` |
| Retained fragment bytes | 1,101 |
| Fragment SHA-256 | `3eebf5b59691eff3c32478e06fb44a5c9eb554bc045d2ff2e20901813a9f99c8` |

The devbox audit used the ordinary resolve-globals, lift-to-SCF and native
lowering pipeline on all three existing generated probes. The result remained
**0/7 CommonJS, 0/7 browser, 0/8 AMD**, with no missing source functions. The
19/19/20 reference observations remain a separate test of the bounded harness.
The first refusals show why adding Map carriers alone does not close this graph:

| Source role | Current native refusal or proof fact |
|---|---|
| Script entry | `uses this`: the wrapper receives the script's `%arg0` |
| Console recorder | Its message parameter remains boxed; no proved caller |
| UMD wrapper | Its first source parameter remains boxed; factory argument also has `the callback value or its identity escapes the call-only parameter` |
| Data factory | Method field belongs to an object whose shape is not closed |
| `set`, `get`, `remove` | Each reads the captured registry through its own closure |
| AMD `define` | Factory parameter remains boxed; its value is stored for later invocation |

The browser's prepared IR contains this actual flow, with SSA names simplified:

```mlir
%factory = ctjs.create_closure %callee[3] this %undefined
ctjs.call_direct @fn$2(%undefined, %undefined, %wrapper, %script_this, %factory)
// Inside the selected browser arm:
%data = ctjs.call %factory(%undefined)
ctjs.set_property %host[%bootstrap_key], %data
// Later in the script:
%host = ctjs.load_global "globalThis"
%data = ctjs.get_property %host[%bootstrap_key]
%get = ctjs.get_property %data[%get_key]
%value = ctjs.call %get(%data, %element, %component_key)
```

The factory constructs one Map and three closures capturing the same immutable
binding to that Map. Their state mutates; the captured binding does not. The
arrow getter carries lexical `this` in imported closure construction but never
reads it in its body. These observations permit owning environments after the
publication flow is proved; they do not permit borrowing the factory frame.

Relevant implementation boundaries:

- `Lowering/ClosureLifting/MethodTables.cpp` requires one literal producer,
  dominating single field initialization, closed direct argument/return flow,
  and visible concrete call signatures. A property read must feed a call with
  the same table receiver. Publication into another object, detached methods,
  alias writes and open returns currently refuse.
- `Analysis/ClosedCallable.cpp` checks actual closure-value uses, including
  numeric-index producers. Private symbol visibility alone does not close
  callers or make a retained factory private to the compiler.
- `Lowering/Admission/Functions.cpp` ordinarily requires receiver, `new.target`
  and callee operands to be unused. Its existing receiver carrier is a proved
  closed object, not an arbitrary script-global receiver.
- `Lowering/LowerToEmitC.cpp` propagates refusal in both directions over direct
  calls and retained callable targets. There is currently no native/boxed
  export bridge allowing just the factory to change ABI under a refused UMD
  caller. A first host slice must admit the complete supported call component.
- `CTJS/IR/Ops/Calls.td` keeps callee value and receiver distinct. The original
  callee value remains necessary for boxed dispatch even after symbol resolution.
  `Ops/Properties.td` requires a shape/accessor proof before generic property
  operations can become slots.

Paths in this list are relative to `ctcompile/lib/CTNative/`, except the last
bullet, which names `ctcompile/include/ctcompile/` paths.

## The host behavior to preserve

| Branch | Conditions and observable action |
|---|---|
| CommonJS | `typeof exports === "object"` and `typeof module !== "undefined"` take priority over AMD. Call the factory, then assign its result to the evaluated `module.exports` reference. Rebinding that property does not rebind the old `exports` alias. |
| AMD | Otherwise, test `typeof define === "function"`, then `define.amd`. Invoke the loaded `define` as a bare call with the factory value; ignore its result. The wrapper does not itself invoke or publish the factory's result. |
| Browser | Otherwise, select defined `globalThis`, or `t || self`, where `t` is the script receiver passed to the wrapper. Call the factory, then assign its result to that selected object's `bootstrap` property. |

These predicates do not establish ordinary objects: `typeof null` is `object`,
and a defined `module` need not be a usable exports object. A host manifest must
prove the actual values and properties, not silently strengthen the predicates.
Repeated global reads can also observe mutation when accessors are possible.

All three Data fields are ordinary writable, enumerable, configurable own
properties. Replacing a field affects subsequent reads through every table
alias. Replacing the publication slot affects future slot reads; retained table
and function aliases still point at their previous values and own their state.
Do not replace a mutable export with an unconditional C++ function symbol.

The original Data methods do not observe their invocation receiver, so detached
original `set`, `get` and `remove` calls still work. A replacement method may use
`this`. A direct-call proof must separately establish the loaded callee identity
and any receiver non-observation; table shape alone establishes neither.
`new.target`, construction, callable identity inspection and unknown argument
windows stay outside the first contract. AMD's `typeof pending === "function"`
is a specific required observation, not permission for arbitrary inspection.

`console.error` is loaded when the rejection branch executes. It receives the
current console object as receiver and one fully constructed message argument.
Its return value is discarded: `Data.set` returns `undefined`. A replaced sink
must be observed. A throwing sink propagates its thrown value; the original
entry remains, and the rejected entry has not been inserted. A reentrant sink
can remove the old entry and insert another value; the outer rejection must
not roll back that mutation or commit its rejected value afterward.

Publication setters and AMD registration may throw after retaining a value.
That retained table or factory must remain valid. A factory throw prevents the
publication write. No wrapper or Data method catches these exceptions.

## A bounded contract API

Introduce a driver-owned `HostContract` input and a recomputed
`HostContractAnalysis`; the following are proposed query names:

```text
binding(load/store) -> absent | owned_slot(schema, initialization) | unknown
property(read/write) -> own_data_slot(owner, key, schema, write_set) | unknown
call(callee_value, receiver, arguments) -> target, signature, effects | unknown
roots() -> script_entry, observed_slots, retained_callables, output_events
```

The driver supplies supported host providers and the set of externally
observable roots. The compiler derives source writes, aliases, dominance,
callers and effects from IR. A manifest identifies providers and schemas; it
cannot assert that an arbitrary source operation is pure, closed or native.
Clear and rederive proof annotations before each use, as the existing Map,
method-table and binding-time analyses do. Bind any supplied manifest to the
compiled source and generated driver so an unrelated module cannot inherit it.

The first provider is a closed test/application driver, with all script execution
and host callbacks enumerated. Its concrete contract is:

| Resource | Initial bounded representation and obligation |
|---|---|
| Host globals | Explicit presence/absence for `exports`, `module`, `define`, `globalThis`, `self`, `console` and `undefined`; prove own data slots and exact initial values. Absence is a driver fact, never inferred from zero source stores. |
| Publication slot | An owning slot for the Data table. Follow every source store and load across the wrapper call; prove the write reaches each read. Preserve the slot's identity separately from its current contents. |
| Data handle | Existing owning method-table representation, with all three fields initialized before publication. Initially accept the existing single-target, single-initialization method proof; unknown host field writes refuse. |
| AMD registration | A driver-owned slot initially `undefined`, then retaining an owning factory callable. Model the registration write, the later `typeof` and invocation, including definite initialization. It is not a call-only callback. |
| Factory invocation | A fresh registry and table on every invocation. Retaining a factory is not running it. Repeated invocation must not share one precomputed runtime Map. |
| Data arguments/results | Existing proved owning object identities and scalar-field schemas for elements/instances, owning strings for component keys, and the supported object/scalar union. Infer signatures from real calls. `get` retains the source's falsy-to-null behavior; `set`/`remove` return undefined. DOM objects and unknown host identities are not covered by this carrier. |
| Error recorder | The actual proved recorder or a generated provider with one string argument, explicit receiver behavior, trace/output effects, no JavaScript throw, no reentry, and no access to the registry. The global name `console.error` does not grant these facts. |
| Intrinsics | Proved `Map`, its used prototype methods/size access, `Array.from` and the relevant iterator behavior. Unknown replacement, accessors, prototype mutation or user conversion refuse the intrinsic proof. |

`owned_slot` means ownership and tracked loads/stores, not an immutable
JavaScript property. In this first driver, externally observed handles cannot
receive unmodeled mutations. If a consumer can mutate a published JavaScript
object outside the compiled source, that driver does not satisfy the contract.
Known source mutation must either be represented by the existing proof's
supported flow or cause a diagnostic. A future open JavaScript adapter would
need mutable property/function cells and checked dispatch or boxed fallback;
that is not supplied by declaring exports read-only in the compiler.

The CommonJS/browser slot flow extends `closedValueFlow` with proved store/load
edges. The AMD slot additionally needs retained-callable flow and a supported
`typeof` observation. These are root edges for lifetime and reachability, not
an assertion that distinct runtime factory calls alias the same allocation.
Once those edges close the factory, reuse returned-method-table/capture proofs
and infer the method signatures from the post-publication calls.

For the exact CommonJS/AMD and defined-`globalThis` browser driver, the wrapper's
fallback parameter is unused after branch proof. Dead argument-flow elimination
may remove its script-`this` use only after proving every use dead. It must not
globally substitute undefined for the script receiver. If `this` remains
observable, supply a real host-global receiver carrier or refuse that entry.

The roots are explicit: script entry; selected publication slot and reachable
table/callable owners; AMD's retained factory and scheduled invocations; the
error sink's ordered events; and the probe's named `trace*` scalar outputs.
Retained factories/methods remain reachable even when initialization never calls
them. Closed helper visibility does not remove any of these roots. The test
driver must declare the observation names rather than make `trace*` a compiler
heuristic. Saved instance handles and object equality observations retain their
owners after Map deletion and factory return.

## Effects, exceptions and partial evaluation

Keep the following distinctions in the call/property proof result:

| Operation | Required effects or limits |
|---|---|
| Host binding/property read | Runtime unless provider and source flow prove its value; possible getter/proxy execution is an unknown call. |
| Export/AMD slot store | Publishes an owning value and changes an observable root. It is not removable initialization bookkeeping. |
| Factory allocation | Produces fresh invocation-local identities; residualize fresh allocations if static preparation is used. |
| `set`/`remove` | Mutate registry state. Their result being undefined does not make the calls dead. |
| Error recorder | Ordered runtime output and scalar trace writes; known disjoint effects may preserve other heap facts. |
| Unknown or reentrant host call | May change reachable slots, method cells and registry state; retain broad invalidation/refusal. |

The first provider's own data slots and proved nonthrowing recorder avoid
JavaScript exception edges; that scope must be checked before admission. Do
not assume a call cannot throw because its caller lacks `try`. Supporting a
throwing provider later needs an explicit completion/status ABI preserving the
thrown value and committed effects at script, factory, registration and method
boundaries. A failure after an observable write cannot restart the operation in
the interpreter. The native/boxed call-component constraint still applies.

Binding-time facts are not authority to execute a host provider during
compilation. Host reads, publication, registration and output remain runtime
until their separate effect and environment proof permits a particular rewrite.
An error sink being disjoint from a Map does not make the error output static.
Constructor replacement, iterator customization, string conversion and external
mutation must continue to invalidate/refuse instead of inheriting builtin facts.

## Source-derived host audit and known differences

[`bootstrap-host-contract-audit.py`](../../tools/check/bootstrap-host-contract-audit.py)
adds 11 bounded host scenarios around the same checked fragment. It writes every
generated source, its hash, the vendor provenance, explicit observation roots
and a JSON report. Node is the independent JavaScript oracle. Optional
`--reference` results are reported separately; a mismatch does not count as
native conformance. `--require-reference-match` makes any such mismatch fail.
`--negative-control` changes an executed observation and accepts only its exact
intended diagnostic. This standalone audit is not a native CTest coverage floor.

```sh
python3 tools/check/bootstrap-host-contract-audit.py \
  --bootstrap ctbrowser/vendor/bootstrap/bootstrap.bundle.js \
  --work /tmp/bootstrap-host-contract-audit
```

Node 26.8.1 agrees with all 11 scenarios. Running the same source hashes through
the devbox ctbrowser reference agrees with nine. The two differences are actual
boundaries, not native regressions introduced by this design:

| Scenario | Node | Current ctbrowser reference |
|---|---|---|
| Browser fallback with `globalThis` undefined | Wrapper selects top-level script `this`; `self` stays unchanged | Reference script receiver is undefined; wrapper selects `self` |
| Rejection callee-read order | `console.error` getter, `Array.from` getter, `keys()` call, error call: `1234` | `keys()` call, `console.error` getter, error call: `314` |

The second witness saves the original `Array.from` and `Map.prototype.keys`,
records their use, and rejects a second component through the exact Data method.
The prepared IR places `get_property "from"` after the `keys()` call and
`get_property "error"` after message construction. The reference additionally
does not execute the installed `Array.from` getter. Consequently a general host
adapter cannot claim correct accessor/callee behavior merely by following that
IR. Fix the upstream ordering and runtime accessor behavior before admitting
those hosts; the initial own-data-slot/inert-intrinsic contract refuses them.

The nine matching scenarios cover CommonJS precedence/aliasing, defined browser
target, delayed repeated AMD invocation, mutable exports and detached original
methods, replacement/throwing/reentrant error sinks, publication-setter failure,
factory failure before publication, and retained AMD factory after registration
throws. These are oracle checks of semantics to preserve, including behavior
outside the first native contract. They do not authorize those effects in PE.

## Acceptance for the first implementation

1. Keep all three existing exact sources and hashes, 19/19/20 observations,
   source-function accounting, and refusal diagnostics. Any isolated factory
   experiment gets a different artifact name and census.
2. Implement validated host roots/slots and their closed value flow before
   relaxing method-table or callable escape checks. Admit the complete supported
   native call component; keep boxed definitions if a future bridge requires them.
3. Compare the exact supported driver against the reference in standalone native
   C++, with the repository's existing GCC/Clang, no-VM and lifetime checks.
   Native export/AMD owners must survive the factory and preserve independent
   repeated invocations, retained aliases and scalar-field instance identity.
4. Reject forged contract annotations, unknown host/external writes, accessor
   publication, unproved `define`, throwing/reentrant error providers and
   intrinsic mutation with explicit reasons. Do not accept the audit's known
   interpreter mismatches as positive native baselines.

The host-contract proof is the concrete remaining work described here. General
DOM identities, mutable JavaScript export adapters and a full Bootstrap factory
remain outside this bounded implementation.
