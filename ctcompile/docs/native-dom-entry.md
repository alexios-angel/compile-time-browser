# Native DOM entries

`ctnative-lower-to-emitc` accepts a fingerprinted `ctbrowser-dom-v1` host
contract for one synchronous function. Its explicit parameters are borrowed
`ctbrowser::element_ref` values: a document pointer and generation-tagged
`node_id`. The C++ caller owns the documents and keeps them alive throughout
the call. Identity compares both fields, including across documents; detaching
a node does not destroy it.

Entries using `matches` or `closest` also take the caller's live
`ctbrowser::style::engine &` for each element parameter used as a selector
receiver, appended after the element parameters in source parameter order.
For example, `closest(element, expected)` takes `(element_ref, element_ref,
style::engine &)`, and two queried elements take two engine references. The
caller supplies the engine associated with each document, so hover/focus state
matches the page. Every element and engine atom-table association is checked
before source effects. The engine, document and their atom table must remain
alive throughout the synchronous call.

This is an action entry, not native Bootstrap initialization. For example:

```javascript
function toggle(element) {
  const active = element.classList.toggle('active');
  element.setAttribute('aria-pressed', active);
  return active;
}
```

Import the complete source and structure it with `--ctjs-resolve-globals` and
`--ctjs-lift-to-scf`. Obtain that resulting module's fingerprint with
`--ctnative-host-contract=fingerprint=true`. The driver supplies:

```json
{
  "version": 1,
  "provider": "ctbrowser-dom-v1",
  "module_sha256": "<the reported lowercase SHA-256>",
  "entry": "toggle$1",
  "element_parameters": [0]
}
```

Indices are zero-based explicit JavaScript parameters. Every explicit
parameter must be listed, in order; arbitrary C++ types and effect promises
are not accepted. Pass the JSON path as `host-manifest` to
`--ctnative-lower-to-emitc`, followed by the usual EmitC cleanup and C++
translation. The existing host-contract and prefix analyses for
`closed-source-v1` remain separate. The DOM provider uses the native lowering
entry and a fresh live source proof, never printed proof attributes.

The emitted translation unit exports the selected function without a launcher.
Local helpers with structured `if`/`else` bodies may be expanded at their original
call sites. Their exact closure identities, direct-call targets, unused implicit
arguments, arity and complete source census are checked first. Nested helpers,
String/Boolean/element arguments and local returned element aliases use the same
proof; every explicit entry parameter must still be an element. The call tree is
bounded by `host-max-steps` and 64 frames. Recursion, uncalled helper bodies and
observed callable identities refuse. The expanded private clone must pass the
complete DOM proof before it replaces any source.

Local fresh objects may hold these helpers in unique, constant String slots.
Every slot write, read and call must have an exact local identity and source order;
method receivers must be that same holder, and helpers must not observe `this`.
Extracted local calls may use an undefined receiver. Duplicate writes, missing or
inherited reads, `__proto__`, object/callable escapes and unknown uses refuse.
The holder is compiler-only evidence and disappears after all its calls are proved;
no callable table or dynamic dispatch is emitted.

Local leaf helpers may capture immutable primitive or DOM values. The shared
closure/cell queries prove unique source identities and no binding mutation or
escape; the DOM proof additionally requires each assignment to precede every
read and invocation. Hoisted closure creation may precede the assignment. Captures
are substituted at each call, preserving copied Strings across later DOM writes.
Object-held leaf methods use the same proof. Cells never become runtime storage.
Captured local callable/holder graphs use this same leaf proof. Consumers expand
before their cells and holders are retired, exposing callable identities for the
next checked expansion. Cycles and chains reaching 64 frames refuse; every rescan
and cloned invocation is charged. The original optional-return callable and holder
sources execute unchanged. Nested helpers expand before the shared leaf query.
Forwarded slots have a private DOM proof of the original creator, enclosing slot,
complete local cell uses and immutable target. Symbolic enclosing loads are inserted
at each call, so forwarding preserves invocation-specific values and assignment
order. Mixed local/forwarded slots and multiple enclosing levels use the same proof.
**4ef7649c** also accepts an exported entry capturing immutable block-local setup.
Its wrapper must publish exactly that source closure and otherwise contain only
checked constants, cells and local callable/holder initialization. A private call
runs after all initialization, including assignments after publication, and the
existing expansion must eliminate all setup storage before complete DOM reproof.
Original wrapper arguments cannot supply host element values. **2da43c9a** additionally
accepts one uniquely called, uncaptured local factory returning exactly that exported
entry. Arguments and private initialization stay in source order; the factory's
implicit arguments, creator identity, complete uses and direct-call symbol/new-target
are checked before cloning. The existing capture proof then eliminates every private
cell/holder/callable. **823b71b8** also lets a factory return a fresh table of unique
own callable slots and publish one constant String selection. Multiple slots may hold the same
closure; every callable body still requires a source invocation. Table identities,
missing/inherited/accessor or repeated slots, factory globals, nested/captured
factories and observable initialization remain refused. The table disappears before
emission; generated C++ still calls the public DOM library directly.

Only a checked inert declaration or the proved initialization above may be omitted.
Skipped source, additional initialization effects, calls to the entry from JavaScript,
mutable entry bindings, borrowed returns, handle retention, prototype or method writes,
unknown receivers, loops and unstructured control flow refuse. Current operations are
strict element identity, Boolean/Number negation, and Number/Boolean/String/undefined
constants and returns,
`classList.toggle(token[, force])`, `toggleAttribute(name[, force])`,
`getAttribute(name)`, `hasAttribute(name)`, `removeAttribute(name)` and
`setAttribute(name, String-or-Boolean)`. Tokens and names come from definite source strings, including String + String expressions;
force is a proved Boolean (including an earlier DOM result) or explicit undefined.
`contains(otherElement)`, `matches(selector)` and `closest(selector)` also
use proved parameter receivers. Selectors are source strings, parsed by the
existing Style parser at the source call; invalid syntax throws
`std::invalid_argument` after any preceding source effects. `contains` preserves
document identity before calling `read_txn::is_ancestor_of`. Selector calls use
`engine::element_matches` and `engine::closest`, the same cores as the bindings.

A `closest` result is a local borrowed identity, with a canonical empty
`element_ref{}` for no match. Its only supported observation is strict equality
with another result or an element parameter, so misses compare equal even across
documents. Dereferencing, retaining or returning that result, passing it to
`contains`, and comparing it with an explicit source `null` still refuse.
Unsupported coercions and observed set/remove results refuse.

A `getAttribute` result is an owning `std::optional<std::string>`: absent is
`std::nullopt`, while an empty attribute remains an engaged empty string. Reads
call the public `get_element_attribute` core shared with the VM binding, preserving
HTML name folding, SVG/XML casing, first qualified-name matching and embedded
NUL bytes. Read names are not validated as mutation names. The copied result may
be returned or left unused and survives later mutations or document destruction.
Strict equality/inequality compares these copied results with null, definite Strings
or another optional String. `!` and `!!` preserve the difference between presence
and truthiness: both a missing attribute and an empty attribute are false. These
Boolean observations can drive existing DOM force arguments. Concatenation accepts
two definite Strings and uses ordinary `std::string` addition; optional Strings
are not implicitly coerced into names or values. Loose equality between two independently proved Strings is supported;
coercive loose equality, global `Boolean` calls, implicit numeric conversion and
property storage still refuse. No generic
nullable carrier is emitted.

DOM manifests may additionally supply `"initial_intrinsics": ["Number"]`.
This explicitly promises the standard initial Number binding and its unmodified
`Number.prototype.toString` lookup chain. It authorizes the complete source proof
for the numeric prefix used by Bootstrap's M helper:

```javascript
function canonicalAttribute(element) {
  const saved = element.getAttribute('data-bs-config');
  return saved === Number(saved).toString();
}
```

Number accepts one proved String, null or optional String with an undefined call
receiver; standard Number `toString` accepts its exact Number receiver and no
arguments. Number results may be returned, tested for truthiness or joined with
other Numbers. Truthiness preserves NaN and signed-zero behavior. The owning JSON
joins below also accept Numbers. The compiler checks the whole entry and both
branch arms before erasing builtin reads.
Replacement, prototype writes, callable escapes, unknown coercions, script reentry,
radix arguments and stale source fingerprints refuse. Number, decodeURIComponent
and JSON are the optional initial intrinsic names supported by these DOM providers;
each must be declared independently, without duplicates.

Emission calls public Core `string_to_number` and `number_to_string` with ordinary
`double` and owning `std::string` values. Missing attributes convert to positive
zero; saved attribute strings remain independent of later DOM changes. The shared
Core implementation preserves the VM's current numeric behavior, including its
known numeric-text limitations; this is not a claim of arbitrary-string ECMAScript
equivalence. Original M's complete prefix uses the separately authorized URI/JSON
chain and mixed-result ownership described below.

DOM manifests may also supply `"initial_intrinsics": ["decodeURIComponent"]`.
The supplied binding must be the standard own global data binding. Number authority
is independent; declaring URI decoding never authorizes a Number call.

```javascript
function decodeAttributeText(element) {
  const text = element.hasAttribute('good') ? 'A%20B' : '%';
  let saved = 'before';
  try { saved = decodeURIComponent(text); }
  catch (ignored) { return saved; }
  return saved;
}
```

With URI authority alone, the complete original acyclic entry must contain one
checked ordinary URI call, with an undefined receiver and one proved definite
String input. Both success and
failure continuations return owning Strings. The catch payload must be unused;
reading, returning, storing or rethrowing it refuses. Local String assignments
before the call are preserved, and a failed assignment retains the previous value.
Protected preparation and both tails currently admit only inert bookkeeping,
constants and the proved global loads. Other protected calls and DOM writes
refuse; the explicitly authorized JSON chain below adds a second fallible call.
Prefix branches and early returns retain their source order. Supported local
helpers are normalized before expansion, and the complete DOM proof checks the
inlined result again.

Normalization inspects the original handler and every pre-call register, proves
every discarded status edge, and constructs continuations in a private module.
The complete DOM identity/type proof runs again before publication. Every incomplete
work budget or failed proof preserves the source. Emission calls the existing
public `ctbrowser::decode_uri_component`, tests `std::optional<std::string>::has_value()`
and moves its String only on success. Empty String is successful decoding;
malformed URI selects the original catch continuation. C++ allocation exceptions
propagate normally and cannot select the JavaScript catch. No error object, Script
value, VM context or new decoder implementation appears in native output.

The saved nullable attribute guard is also supported:

```javascript
function decodeSavedAttribute(element) {
  const t = element.getAttribute('data-bs-config');
  if ('string' != typeof t) return t;
  try { return decodeURIComponent(t); }
  catch (ignored) { return t; }
}
```

`typeof` returns `"object"` for null and `"string"` for a present empty String.
Equality with those literals, either operand order, strict or String-only loose
equality, and Boolean negation retain the exact saved-value predicate. Only the
selected branch gains String uses; no fact narrows the producer, a sibling read,
or a use after the branches rejoin. The emitter copies the optional's String
inside that branch, retaining independent ownership for catch and later return.
Both arms must pass the complete source proof. Refused or incomplete proofs
publish no partial refinements; supplied attributes never grant authority.

DOM manifests may supply `"initial_intrinsics": ["decodeURIComponent", "JSON"]`
to compile the protected body from original M:

```javascript
function parsedConfig(element) {
  const text = element.hasAttribute('good') ? '%7B%22active%22%3Atrue%7D' : '%';
  try { return JSON.parse(decodeURIComponent(text)); }
  catch (ignored) { return text; }
}
```

JSON authority binds the initial global JSON object and its original own-data
`parse` property. The proof preserves the member lookup before URI argument
evaluation, the exact JSON receiver, one definite String argument and two checked
calls on one success path. Either failure retains its original catch snapshot;
the payload remains unobserved. Supported local helpers may contain this chain.

Emission calls public Core `ctbrowser::parse_json`, tests its
`std::expected<ctbrowser::json_value, std::size_t>`, and moves the tree only on
success. The result owns its strings, arrays and object members and may outlive
the document. String failure arms become owning `ctbrowser::json_value` strings.
No Script value, VM context, collector or second JSON parser is involved. Core's
existing numeric, Unicode and nesting behavior remains the same as the VM parser.

Replacement or shadowing of JSON, another method, a changed receiver, a reviver,
reversed or additional calls, observed catch payloads and missing or duplicate
intrinsic declarations refuse. A JSON result may join with a definite String,
Boolean, Number, null or optional String. Each alternative becomes an owning
`ctbrowser::json_value`; optional Strings copy their bytes only in the selected
present arm, while absence becomes JSON null. Undefined, borrowed and callable
alternatives still refuse. Under the complete DOM proof, `typeof` observes the
owning variant and returns an owning String: Boolean, Number and String alternatives
report their scalar tags; null, arrays and objects all report `"object"`. The tag
supports existing String equality but never narrows JSON to object-only members.
JSON truthiness, equality, coercion and property observations remain refused.

With all three initial intrinsics (`Number`, `decodeURIComponent`, `JSON`), the
complete original M helper and `H.getDataAttribute(element, "config")` compile.
M retains its Boolean/Number/null prefix, saved nullable guard, original lookup
order and failure snapshots. H reuses the existing proof for F's original
regexp/callback replacement when the constant key has no match, then calls the
public DOM attribute core. Matching or live F keys remain refused. Config's
`"object" == typeof parsed` observation compiles; the following branch-local empty
object and JSON object spread remain outside the current proof.

Direct entries with inert declaration wrappers may use structured `if`/`else`
branches through the existing SCF lowering. Each condition must be a proved Boolean or a supported scalar truthiness
observation. Every operation in both arms is checked, including nested arms;
values must dominate their uses and frame bookkeeping stays in the entry block.
Joins carry Numbers, Booleans, definite Strings, owning `std::optional<std::string>`
for String/null alternatives, or owning `ctbrowser::json_value` for JSON and the
supported primitive alternatives above. Incompatible alternatives and borrowed or
callable joins refuse.
Strings widen to optionals at the existing region boundary, while
source effects remain inside their selected arm. Work uses the existing host
budget, and nesting reaching 64 branches refuses. **755af20b** applies complete
arm/operand/yield proof to local helpers before expansion. Simple early returns
that lift to `scf.if` are accepted when both arms have matching frame state;
checked frame bookkeeping disappears while cloning, and branch-local immutable
capture loads bind at each invocation. Callable/cell/object identities and local
helper calls inside branch arms remain refused. **83da7d42** additionally proves
acyclic completion dispatch: a bounded private rewrite carries exact yields into
each branch continuation and selects only constant completion tags. Every original
operation must be visited, and observing an inactive poison value refuses. Source
effects and frame exits remain in their original paths; the unchanged proof checks
the resulting branches. Three/four-return helpers and captured String snapshots
compile; unknown selectors, unvisited arms, invalid frame exits, loops and
general exceptions remain refused. The bounded URI/JSON cases above have their own
complete source and ownership proofs.

The provider starts with the standard `undefined` and reserved `__ctbrowser_regexp`
bindings and initially unmodified
`Object.prototype`, `String.prototype` and `RegExp.prototype` chains, independently
of external script state, as in the isolated
closed-source provider. Complete source discovery rejects replacement (including
a declaration named `undefined`), prototype mutation and script reentry. This
object premise proves that unique local callable writes create own data slots;
`__proto__` remains excluded because its standard inherited setter is observable. Preparation replaces only
those proved global reads with constants in a private clone and reproves it
before publication. This does not claim that the VM makes its globals immutable.

Constant String replacement by a fresh literal ASCII character range (`/[A-Z]/g`,
for example) can disappear when the range has no match. This requires the original
String `replace`, RegExp `@@replace`, `exec` and all flag accessors from those initial
prototype chains. The literal, method and uncaptured callback must have only that
one use; callback identity and the complete source body are checked before proving
it is never invoked. No RegExp or callback runtime reaches generated C++.
One unique local helper may supply the String when all its exact ordinary calls
pass a bounded set of constant Strings. The proof follows these sets through local
helper parameters and checks every possible input; the result keeps its original
receiver value at each invocation. A live input invalidates the whole parameter.
An uncaptured helper held by immutable captured cells waits until every consumer
has expanded and every invocation is visible before binding these inputs. This
admits captured F with distinct names and forwarded call chains; F with captures
of its own remains outside this specialization. Matching strings, other patterns/
flags and prototype mutations still refuse.

Template concatenation also reuses ordinary owning String addition when both
operands are proved Strings. Original Bootstrap H `setDataAttribute` and
`removeDataAttribute` compose with captured F, including a factory-exported entry
table. Nullable values, objects, Numbers and Booleans do not gain implicit template
conversion. H's `getDataAttribute` composes with original M for a proved constant
no-match key such as `"config"`. Matching/live keys and dataset enumeration remain
unproved.

Explicit undefined force preserves the current platform adapters: classList
treats it as omitted, while Element.toggleAttribute treats it as false. Private
preparation omits the former argument and replaces the latter with a Boolean
constant before reproof and C++ type selection. Forced no-ops preserve attribute
bytes and mutation behavior; invalid toggle names still throw before a no-op.

Generated calls use the public `dom/element.hpp` and `dom/token_list.hpp` APIs.
Attribute toggling shares `toggle_element_attribute` with the VM adapter;
presence and removal call the existing document API and shared name folding.
`validate_element` checks every incoming handle before effects. Token validation
and DOM write failures propagate as C++ `std::bad_expected_access` exceptions. The
VM adapters use the same platform implementation and retain their own value
conversion, JavaScript exceptions and Shell notifications. This entry does not
deliver Shell mutation observers, custom-element callbacks, events or rendering.

The registered `ctcompile_native_dom_entry` CTest compiles and executes real
DOM clients with GCC and the configured C++23 Clang, both printing layouts and
optimization policies. It checks document domains, detached subtree lifetime,
ordered mutations, String/Boolean conversion, validation failures, refusal
controls and absence of Script symbols. Selector clients additionally link Style;
other actions keep DOM/Core-only linkage. Query checks cover live interactive
state, atom-table mismatches, shadow boundaries and cross-document misses.
Boolean actions also exclude scalar
value-model helpers from the emitted C++.

The `ctcompile_native_dom_strings` CTest compares **625** copied-value, Boolean
and numeric-prefix observations with Node and the ctbrowser VM, then executes
eight GCC/Clang clients across both providers, optimization policies and printing
layouts. It checks copied optional Strings, saved numeric/text results after DOM
mutation and destruction, invalid handles before effects, document domains, name
bytes and casing. **908** source refusals cover unsupported coercion, control flow,
handles, retention and builtin misuse, alongside identity, capture, branch,
completion, fingerprint and work-budget controls. The **132 numeric observations**
cover 22 input strings/null across six source shapes, including canonical comparison
and repeated Number calls. These clients link DOM/Core only and reject Script/AOT
symbols or generic nullable value helpers in generated code.

## Owned synchronous sessions

Select `ctbrowser-dom-session-v1` with the same fingerprint, entry and
`element_parameters` fields to emit an additional `<entry_symbol>_session` class.
It owns an atom table, then its document, then a selector engine when the source
uses selectors. The class cannot be copied or moved. `document()` provides the
document for building the page; `selectors()` exposes the owned engine's live
interactive state when present. `invoke(element_ref, ...)` calls the same proved
source function with the session's selector engine.

Every supplied element must belong to this session. All document-pointer
comparisons precede every handle validation and source effect, so even a foreign
pointer whose document has been destroyed is rejected without dereferencing it.
Invalid IDs within the session still fail the public DOM validation. Detaching
a node preserves its identity and does not invalidate its handle. Callers must
stop using the session's borrowed handles and references before destroying it.
The ordinary synchronous free function remains available under its existing
caller-owned-document contract.

The `ctcompile_native_dom_session` CTest checks actions and selectors, both
optimization policies, both printing layouts and both compilers, with sanitizer
checks for teardown and dangling foreign pointers. This provider still refuses
handle returns, retention in Data or properties, and browser callbacks. It does
not reinterpret source-created ordinary objects as DOM elements.

## Data input provenance

`ctbrowser-dom-data-session-v1` is currently an **analysis-only** contract. It uses
the closed-source contract fields (`roots`, `observations`, initial bindings and
intrinsics) plus the same ordered, nonempty `element_parameters` declaration.
`--ctnative-host-contract` checks the actual input arguments across the complete
Data method family. The entry must be unreferenced by source code, uncaptured,
and declare every explicit parameter as an original JavaScript value.
The imported function may now have one exact inert source-declaration wrapper.
It shares the synchronous DOM provider's bounded declaration proof; the complete
Data census excludes source calls, reads, rewrites and observations of that binding.
Only a fully successful query exposes `HostContractAnalysis::wrapper()`; no source
operation is removed by the analysis. Every table read must feed a completed family
call, so a second uncalled method read or a whole-table global alias also refuses.

Each checked call records `HostMethodArgument.element`, separate from an owning
source allocation. `HostCapturedMap.outerKeyInputs` lists these inputs in parameter
order. Every non-root use must pass the exact input to a proved outer-key formal.
Payloads, child keys, outer snapshots (including fluent receivers), returns, global
aliases, arbitrary properties and extracted callables refuse. Reports expose
`outer_key_inputs`; their counts never authorize a later query.

Entry-order Map facts preserve these exact input origins without inventing fresh
objects. A set followed by a get with the same input retains its scalar value;
using a different input retains the possibility of either a match or a miss.
This is provenance evidence, not proof of a document domain or a retained lifetime.
Native lowering and `OwnedGlobalRoots` still refuse with
`DOM Data requires storage confined to its document owner`; source-prefix
specialization also refuses this provider.

Next, attach private Data storage to the nonmovable DOM owner. It must destroy
Data before the document, then the borrowed atom table. The current shared
table/global carriers and `capture_map()` accessor do not establish that lifetime.
Revalidate the complete input family before typing/emission, preserve document plus
node identity, and check every document domain before dereferencing any input or
performing effects. Gate a source without Map snapshots too: its associative Map
needs an element-key comparator, whereas Data's child snapshot selects ordered
storage and hides that path. Preserve source allocation timing: the imported
`dataEntry` fixture constructs a new root, Map and method table on every invocation.
Reusing that Map across invocations would change its observable behavior; a separate
initialization/action split needs its own source proof. Retained native DOM keys
remain unimplemented.

Original Button/BaseComponent construction, prototype/static getters, disposal,
retained callbacks and the native application driver follow this ownership step.
