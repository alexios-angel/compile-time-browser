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
Only its checked, inert function-declaration wrapper may be omitted. Skipped
source, additional initialization effects, calls to the entry from JavaScript,
captures, borrowed returns, handle retention, prototype or method writes,
unknown receivers, and nested control flow refuse. Current operations are
strict element identity, Boolean negation, Boolean/String/undefined constants and returns,
`classList.toggle(token[, force])`, `toggleAttribute(name[, force])`,
`hasAttribute(name)`, `removeAttribute(name)` and
`setAttribute(name, String-or-Boolean)`. Tokens and names come from source strings;
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

The provider starts with the standard `undefined` binding, independently of
external script state. Complete source discovery rejects replacement (including
a declaration named `undefined`) and script reentry. Preparation replaces only
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
The current tests supply explicit IR entry arguments; adapting an imported
function's inert declaration wrapper to this contract is still separate work.

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
storage and hides that path. Retained native DOM keys remain unimplemented.

Original Button/BaseComponent construction, prototype/static getters, disposal,
retained callbacks and the native application driver follow this ownership step.
