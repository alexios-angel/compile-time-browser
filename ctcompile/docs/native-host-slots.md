# Checked host roots and publication-slot flow

`ctnative-host-contract` implements the first proof boundary proposed by
[the Bootstrap host contract](native-bootstrap-host-contract.md). It is an
opt-in analysis pass. It does not change runtime operations, native admission,
the boxed ABI, or the existing exact Data census.

A driver supplies a `closed-source-v1` JSON manifest. The manifest identifies
the script entry, publication roots/properties and observed global names.
Absent bindings and present bindings whose value is undefined are separate:
absence can answer `typeof`, but cannot justify a bare identifier read.

```json
{
  "version": 1,
  "provider": "closed-source-v1",
  "module_sha256": "<canonical module fingerprint>",
  "entry": "_script_$0",
  "roots": [{"binding": "host", "properties": ["slot"]}],
  "observations": ["trace"],
  "absent_bindings": [],
  "undefined_bindings": ["undefined"]
}
```

The provider promises ordinary own global data bindings, the declared initial
environment, and initially unmodified intrinsic prototypes. All execution and
mutation must come from the supplied source. It does not promise that source
operations are pure or nonthrowing. Unknown fields/provider names, supplied
effect claims, conflicting binding declarations and source writes to fixed
absent/undefined bindings are rejected.

The optional `initial_intrinsics` and `realm_global_this` fields identify
specific initial embedding resources for
[entry-prefix specialization](native-host-prefix.md). Intrinsic replacement or
escape is rechecked from source. These fields do not supply an effect proof or
authorize the complete slot query when another obligation fails.

`initial_intrinsics` also accepts `__ctbrowser_class_defined`, the embedding's
class-descriptor initialization helper. This is an explicit initial identity,
not an exemption based on a global's spelling. Source replacement and escaping
uses still fail validation. The other host analyses do not interpret its effects.
It also accepts `Error` for construction with one literal string and the exact
same constructor/new-target identity. This declares no general exception or
payload representation support.

`ctnative-specialize-class-initialization` consumes that identity separately.
Prepare source with `ctjs-resolve-globals` and `ctjs-lift-to-scf`, fingerprint
the resulting module, and bind a `closed-source-v1` manifest with
`initial_intrinsics: ["__ctbrowser_class_defined"]`, empty absent/undefined
bindings, and no realm receiver declaration. Existing roots/observations remain
declarations; a global callable holder requested by either cannot be removed.
This pass grants no publication or ownership proof.
The manifest may additionally declare `Error` for a local throwing static getter.
It may select an explicit imported function when the script is its proved inert
declaration wrapper. Every explicit parameter must be unused; the receiver,
new.target and callee identity remain unobserved except for creating local
closures. The wrapper and publication remain intact. This permits class
preparation for a library entry, but adds no DOM provider or parameter authority.

```sh
ctjs-opt prepared.mlir \
  --ctnative-specialize-class-initialization='manifest=class.json' \
  --ctnative-lower-to-emitc -o native.mlir
```

The bounded census admits local base constructors and fresh prototypes with
unique ordinary methods. Methods have no captures, identity or lexical-home
observations; their receivers read/write ordinary fields or call another proved
method on that same receiver. Method reads must be used only as direct callees;
passing or returning the receiver and observing a method identity still refuse.
Every instance method read must feed its own receiver call. Constructors may call
those immutable methods on the same receiver and must return a primitive constant
when methods exist. Method writes and replacement return objects remain excluded.
Methods may contain structured `if`, `for` and `while` regions, including lifted
break/continue/return dispatch and static numeric increment/decrement counters.
Native admission independently requires Number counter operands. Ordinary methods
may also retain outer CFG exits and literal primitive throws; object and parameter
throws refuse. Every arm and body retains the complete effect and receiver-use
census. After those checks, a budgeted private method clone normalizes structured
switches and unused/all-poison results using the existing exception recovery
machinery. Failure leaves the source untouched; this grants no throwing-call,
iterator or native completion authority. Outer switch/throw exits remain for
native lowering to prove. Mutual method calls use the same proof. Constructors,
class setup and static getter expansion remain linear with one outer block.
Every call stays within the supplied source; unresolved bindings, dynamic keys,
reflection, captured functions and other nested regions fail closed.

Parameterized local helpers use the same complete effect census. Every direct
call rechecks its source closure and target, with unused receiver and new.target.
Cross-function global loads additionally require the existing closed hoisting
declaration proof, charged once per binding against the work budget. Structured
helper bodies keep their branches and argument evaluation order; literal Number
keys are accepted without granting native field/index representation. An arrow may
capture an unused lexical `this`; observing it still refuses. This supplies no
authority for unresolved browser calls, helper replacement or ambient effects
inside an uncalled helper.

Fresh local callable holders use the same identity/order proof as DOM source
preparation. Each slot has one preceding closure store; reads may only call that
slot on its original holder. Class preparation additionally requires ordinary
keys, one closure storage use and unobserved receiver, new.target and callee.
Every slot body, including unused slots, keeps the complete effect census.
Holder calls are proved before their enclosing direct callers, whose lexical
receiver may be saved by an unused-this arrow. Holder operations stay intact for
normal closure lowering.

Global callable holders additionally require a unique publication from the closed
script entry, after every fixed callable slot is initialized. Before publication,
only constants, closure/object creation, ordinary stores to fresh objects, global
stores and inert frame/root operations are permitted. The provider makes global
stores ordinary own-data writes; this prefix cannot invoke source code, so reads
through subsequent calls also follow publication. All binding writes and uses of
every global load are checked. Earlier calls, even unrelated ones, remain outside
this bounded proof; widening requires a complete caller-order analysis. The global
holder must also be absent from the manifest's roots and observations. After
all checks, capture-free helpers with unused receiver, new.target and callee
become direct calls at their original positions. Missing arguments are padded
with undefined; surplus arguments remain outside this bounded rule. Ordinary
argument evaluation and helper effects remain in order. Recorded loads and
property reads follow private method clones through dispatch normalization.

Only then are the holder's slots, global publication, loads and closures removed.
Unused helper definitions are removed after all holders have been expanded and
both module and body symbol scans find no references. No global object carrier is
needed. Normal native admission must still prove each direct function; this
preparation supplies neither ownership nor DOM effect authority. Class/DOM
provider composition remains separate work.

`HostContract/prepareDOMEntry` now owns the existing DOM preparation transaction:
URI completion, source helper expansion, element guards, iteration, wrapper and
optional-force normalization, then complete source reproof. It publishes the
normalized module and refreshed contract together; refusal preserves both.
`LowerToEmitC` calls that shared seam. It grants no class or ownership authority.
Class/DOM composition still needs a proved constructor/prototype normalization
inside an explicit element entry: the class pass currently requires script entry
index 0, while DOM entry proof cannot carry ordinary constructed class instances.
Host manifests also bypass the ordinary closure/constructor lifter. Those body,
receiver and representation obligations must be joined before invoking the DOM
transaction; adding class intrinsics to a DOM manifest does not establish them.

An exact local instance or method/constructor receiver may select these getters
through its `constructor` property. That intermediate identity may feed only
constant proved getter reads and inert roots; writes and identity escape still
refuse. Instance reads require a primitive constructor return, since an object
return replaces the instance. The getter proof runs after the complete receiver
census, and recorded reads follow private method clones during dispatch
normalization. Expansion removes the intermediate constructor reads and roots.

Local static getters may return closed scalar expressions or fresh empty objects,
and read other proved getters on the same constructor. The dependency graph must
be acyclic; the work budget charges every transitive clone before any mutation.
Expansion happens at each original read, preserving evaluation order and fresh
object identity. Getter closures must have one creation site, no captures, no
setter and no observed callable identity. After expansion, the pass removes the getter definitions only
when the closure census and a charged symbol-use census leave no references.
Remaining getter references in module/body attributes and unresolved targets
refuse before mutation. Inherited classes, foreign receivers, getter stores and nonempty object literals
still refuse. Closure metadata keys `name`, `length`, `__home`, `caller` and `arguments`
remain excluded. The first three have independently measured Node/interpreter
disagreements; the last two retain the conservative metadata boundary.

A linear static getter may throw a literal primitive or a declared `Error`
constructed from one literal string, used only by its throw and inert roots.
Reads of a throwing getter and its dependent getters become direct calls to
capture-free source functions, preserving abrupt completion and closure scope.
Unused getter chains are removed in reverse dependency order; direct symbol
uses keep live callees after the complete closure census.
Ambient calls, escaping Error payloads, coercible messages and Error replacement
still refuse. Native completion and Error representation remain separate proofs;
preparation does not make these throwing calls executable natively.

After every check succeeds, the pass removes the unused helper and unobservable
home/backedge setup while preserving prototype method definitions. It also discards
supplied native reports. Native lowering checks constructor identity, construction
sites, return safety and captures before seeding the receiver fixpoint from the exact
prototype.
Later instance stores cannot seed that constructor receiver. Full constructor and
method admission then verify each dependency; only methods proved during this
invocation count as already lifted. Whole-module method-key mutation and read
censuses exclude replacement and callable identity observations. The prototype
fields are removed only after the proof: methods become direct calls with no runtime
field, and scalar defaults initialize the local struct before constructor execution.
Type and ownership admission remain independent. Native construction owns its local
struct by value; free functions borrow receiver pointers.

Inheritance, executable field initializer closures, observable
constructor identity, prototype mutation and retained receivers still refuse.
The pass does not yet compose with the DOM Data session or prepare the original
Bootstrap Button. The original mutable-helper and inherited-getter runtime
discrepancies remain separately measured in the class and Button gates.

The fingerprint covers the canonical generic IR of the complete program and
driver. Locations and previous `ctnative.host_*` reports are excluded. Changing
semantic IR invalidates the manifest; comments or printing locations do not.
The separate exact-probe artifact records the original JavaScript and vendor
hashes as well.

```sh
ctjs-opt prepared.mlir --ctnative-host-contract=fingerprint=true -o /dev/null
ctjs-opt prepared.mlir \
  --ctnative-host-contract='manifest=host.json output=host-report.json report=true'
```

`require-proof=true` makes an incomplete contract a pass failure. Without it,
the pass writes the partial report and its refusal reason. `max-steps` bounds
discovery and defaults to 100,000; exhaustion withholds every usable proof.

## What is proved

Each requested root must have one source binding initialization to a fresh
object in the script entry. The initialization must be unconditional. The
analysis follows global aliases, exact direct-call arguments/returns and
structured branch values. UMD `typeof` string comparisons can select a branch
from the actual source initializers and declared absent/undefined bindings.
This branch interpretation changes no source operations.

An ordinary constant property key and a checked environment establish an own
data slot. Source alias writes are tracked, including ordered replacement.
Every read needs a preceding initialization, and all writes must have a
supported order relative to the read. Cross-function ordering currently
requires one visible invocation path per helper. Multiple invocations and
recursion need context-specific slot state; source schema equality does not
equate different runtime allocations. Every allocation used as an object
identity must have one exact invocation path back to the script entry,
including callers of wrappers around the allocation site.

Direct-call identity is checked against the actual retained closure value and
numeric function identity. A symbol annotation or private visibility alone
does not establish it. Unknown calls, unknown property receivers, dynamic or
prototype keys, accessors, deletion, throwing operations, unknown conversions
and unsupported control flow prevent a complete proof. Intrinsic and error
recorder providers are not implemented by this pass yet.

The public `HostContractAnalysis` object exposes the source write/read edges.
Its `property(read)` query returns an edge only when the **whole requested
contract** passes. The analysis borrows the current module and must be discarded
after a semantic mutation. Printed `candidate_edges` are partial evidence;
`proved_edges` stays zero after any refusal. Old or forged report attributes
never authorize the query, including on repeated pass invocation. No native
pass currently consumes these diagnostic attributes.

## Exact Bootstrap evidence and remaining work

`tools/check/bootstrap-host-slots.py` calls the existing exact probe generator;
the retained vendor wrapper and Data declaration are unchanged. Its separate
artifact contains the generated source, source/vendor hashes, canonical
manifest and slot report. It requests `module.exports` for CommonJS and
`globalThis.bootstrap` for the defined browser host. AMD retains an explicit
unsupported callable-owner frontier: `define` is a function, not a fresh
ordinary object root accepted by this first provider.

These reports preserve the seven/seven/eight source-function denominators and
explicitly list the 19/19/20 observation roots. They do not replace the existing
reference observations, native admission census or lifetime tests. A report of
source publication flow is not a native Bootstrap executable.

The measured CommonJS report finds one fresh root, two source writes and 24
reads with candidate write/read edges. The browser report finds one fresh
root, one source write and 23 candidate edges. AMD finds no accepted fresh
root or candidate edge. All three contracts remain refused with zero usable
edges; the current complete-contract refusal is `property receiver lacks a
fresh own-data object proof`.

The next consumer must use a live complete proof to extend the owning method
table and retained callable flows. The exact Data component still needs the
supported intrinsic/error provider behavior, publication ownership, call
component closure and unchecked mixed-result field access described in the
host contract. Delayed/repeated AMD factory invocation needs a distinct owning
callable slot and per-invocation identities. Full Bootstrap initialization
remains outside this increment.

Focused coverage is registered as `ctcompile_host_contract`, the
`CTNative/HostContract/contract.test` lit test, and
`ctcompile_bootstrap_host_slots_{commonjs,browser,amd}`. It covers live proof
queries, ordered aliases/direct helper publication, missing initialization,
source mutation/accessors/throws, absent-binding misuse, missing roots,
stale manifests, forged reports, unknown provider/effect claims and exhausted
work. Single-invocation factories remain supported; repeated factories and
wrappers cannot merge their allocations or property histories. The existing
exact Data tests remain registered independently.
