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
unique ordinary methods. Methods may capture their own fixed local constructor
solely for proved static-getter reads. Other identity and lexical-home observations
refuse; receivers read/write ordinary fields or call another proved method on
that same receiver. Method reads must be used only as direct callees;
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
reflection, other captured values and unchecked nested regions fail closed.

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
preparation supplies neither ownership nor DOM effect authority. DOM composition
uses the separate transaction described below.

`HostContract/prepareDOMEntry` owns the DOM preparation transaction: class
initialization, URI completion, source helper expansion, element guards, iteration, wrapper and
optional-force normalization, then complete source reproof. It publishes the
normalized module and refreshed contract together; refusal preserves both.
`LowerToEmitC` calls that shared seam. It grants no ownership authority.
DOM helper expansion also accepts already normalized direct calls to exact private
capture-free targets with no live closure creation, exact arity and undefined
callee/new.target. It binds the actual receiver separately at each call. Receiver
observations on this route survive into complete DOM reproof; callee/new.target
observations, nested closures/captures, recursion and unvisited source functions
still refuse. Existing live closure and lexical receiver rules remain unchanged.
This normalization supplies no constructor/prototype or class-provider proof.

After direct receiver expansion, fresh confined objects with initialized ordinary
constant-key fields forward each read's source-position value. Every object use
must be an ordered root/read/write. Roots and writes stay in the original block;
reads may occur inside a later structured branch or loop. Those regions cannot
write the object, so reads retain the enclosing operation's field snapshot.
Aliases that escape, identity observations, self-stores, missing writes,
reserved/dynamic keys and conditional field writes remain outside this rule.
Field operations and the allocation disappear while
all value producers remain for complete DOM reproof. This also carries element
fields as ordinary borrowed DOM operands. Empty String keys remain valid.
Objects passed to live closures still reach holder classification before their
calls expand; JSON aggregate behavior is unchanged.

Class/DOM composition now reuses the complete original class proof in that private
transaction. The public class pass remains closed-source-only with unused entry
parameters. A DOM request may supply `__ctbrowser_class_defined` and optional
unique `Error` alongside existing DOM intrinsic declarations. The class helper
and Error bindings and complete original getter bodies are proved before only
those declarations are consumed. DOM identities survive every method probe and
final typed reproof. The original source census rejects declared intrinsic
replacement before any method can disappear. Unused literal-message Error
getters may then disappear; referenced throws still require final typed DOM
proof and currently refuse.

Declared DOM loads and `toString` reads in entry/method bodies defer to that
complete typed proof. Exact capture-free helpers created and called in the entry
also defer, whether their original call is ordinary or already resolved. Their
calls survive class rewriting; all global-holder targets retain the stricter
census because unused slots can be removed earlier. Original helper exception
CFGs stay intact for the existing URI/JSON normalization and final typed proof.
This composes unchanged Bootstrap M with `M(shape.read())` in the entry; a method
capturing M remains outside the constructor-only capture proof.

`toString` requires an actual Number receiver; arbitrary
coercion hooks remain unsupported. Even a method with only an intrinsic load
requires a probe; deferred helper operations also force the complete method
probes. An ignored pure intrinsic result is inert, while observable
identity uses, unknown effects and unsupported inputs still refuse. Constructors,
getters and other helpers retain their existing stricter census. Lifted helpers
discard their direct-call callee operand only when it names the exact lifted
target and that target does not observe its callee argument. All premises are
checked before deleting any closure, so child deletion order cannot authorize a
parent. Nested callee dependencies remain refused.
Selected-entry parameter uses, unknown entry calls and short-circuit `if`/`yield`
results defer to final typed DOM proof, which checks both source branches.
Unknown calls in zero-parameter class methods instead require independent
private probes at every actual entry-local construction. Every such method is
probed, including unused and transitive callers that can replace a receiver field. The
constructor/getter and other helper effect censuses remain unchanged. Probe
eligibility is checked before normalization can replace method bodies.

Parameterized methods require reachability from an original direct entry call
on each actual instance. The bounded census follows exact `this.method(...)`
calls through that instance's own method definitions; synthetic unused-body
probes cannot establish reachability. One private proof retains all original
calls, arguments and field state in source order. Unused parameterized methods,
missing host authority, bad later calls, uncalled second instances, recursion and
invalid calls in dead branches still refuse; no formal receives invented host
authority. The existing lifter supplies JavaScript undefined for omitted arguments.
Complete typed DOM proof establishes exact strict-undefined comparisons and
their truth conversions, while still checking both default branches and all
argument/default producers. Only then may the private candidate fold a decided
branch, preserving the selected operations in source order. Null does not trigger
a default; unknown effects in a skipped default still refuse. Literal and proved
static-getter defaults, including fresh empty `DefaultType` objects, now compose
through transitive calls. Complete original W/r/H effects remain the next Config
boundary.

DOM attribute writes have the proved JavaScript undefined result; consumers
receive the same typed proof as other values. An entry proved to return only
undefined emits a C++ `void` signature and return, preserving its write effects
without a nullable scalar carrier. `setAttribute` also accepts the
optional String returned by `getAttribute`: a small native conversion passes
either its String or `"null"` to the existing ctbrowser DOM API. This uses ordinary
`std::optional<std::string>` ownership and adds no VM or browser implementation.

The existing constructor/method lifter runs under a quadratic IR-size ceiling;
only freshly proved lifted closures with inert root uses are removed. Input native
reports cannot authorize erasure. Independent direct-receiver, confined-field and
typed DOM proofs must all succeed before the source and contract are published.
The original `class_key` and `class_order` now execute with both DOM providers.
The captured getter-key case also executes with both providers. The preserved
`class_element` now executes its `classList.toggle` and `getAttribute` calls
through constructor-stored `this.element` and captured `Button.NAME`. Unread
method definitions may be omitted from other private probes, then from the
emitted candidate only after all their original bodies pass typed DOM proof.
A module-wide key-read census and symbol-use check keep that omission conservative;
proof-only invocations never enter emitted code. Uncalled parameterized methods
and construction outside the entry remain unsupported by this composition.
The original `unused_key_dom_method` control retains its parameter-provenance refusal.

Local cells in class setup functions may carry a fixed value when every write
stores the same SSA value. Writes and captures remain ordered in the original
block; reads may also occur in a later `scf.if` arm. Reads and captures must
follow initialization; repeated identical constructor writes are inert.
Chained aliases retain the full constructor and receiver-use census.
Each method capture must resolve to its own exact constructor, with local slot
metadata and only ordinary static-getter reads. The existing getter dependency
proof checks those reads and unused bodies. After all source checks, expansion
removes the proved capture loads, closure slots and cell plumbing. Shared
immutable-capture rules are unchanged.

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
