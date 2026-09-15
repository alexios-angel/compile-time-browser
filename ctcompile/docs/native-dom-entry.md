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
Local capture-free, straight-line helpers may be expanded at their original call
sites. Their exact closure identities, direct-call targets, unused implicit
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
Original wrapper arguments cannot supply host element values. Top-level globals,
factory calls and observable outer initialization remain refused.

Only a checked inert declaration or the proved initialization above may be omitted.
Skipped source, additional initialization effects, calls to the entry from JavaScript,
mutable entry bindings, borrowed returns, handle retention, prototype or method writes,
unknown receivers, and nested control flow refuse. Current operations are
strict element identity, Boolean negation, Boolean/String/undefined constants and returns,
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
are not implicitly coerced into names or values. Loose equality, global `Boolean`
calls, numeric conversion, branches and property storage still refuse. No generic
nullable carrier is emitted.

The provider starts with the standard `undefined` binding and initially unmodified
`Object.prototype`, independently of external script state, as in the isolated
closed-source provider. Complete source discovery rejects replacement (including
a declaration named `undefined`), prototype mutation and script reentry. This
object premise proves that unique local callable writes create own data slots;
`__proto__` remains excluded because its standard inherited setter is observable. Preparation replaces only
those proved global reads with constants in a private clone and reproves it
before publication. This does not claim that the VM makes its globals immutable.

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

The `ctcompile_native_dom_strings` CTest compares **249** copied-value and Boolean
observations with Node and the ctbrowser VM, then executes eight GCC/Clang clients
across both providers,
optimization policies and printing layouts. It checks copied optional strings,
invalid handles before effects, document domains, name bytes and casing, and
**336** source refusals for unsupported coercion, control flow, handles and retention,
**44** provenance/depth refusals, **24** method provenance checks, **74** capture
provenance/storage/budget checks and four
work-budget/fingerprint controls. Helper
cases preserve argument evaluation order, saved String values, repeated calls and
nested name construction. These clients link DOM/Core only and reject Script symbols
or generic nullable value helpers in the generated code.

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
