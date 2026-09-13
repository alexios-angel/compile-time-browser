# Native DOM entries

`ctnative-lower-to-emitc` accepts a fingerprinted `ctbrowser-dom-v1` host
contract for one synchronous function. Its explicit parameters are borrowed
`ctbrowser::element_ref` values: a document pointer and generation-tagged
`node_id`. The C++ caller owns the documents and keeps them alive throughout
the call. Identity compares both fields, including across documents; detaching
a node does not destroy it.

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
`classList.toggle(token)` and `setAttribute(name, String-or-Boolean)`. Tokens
and names come from source strings; unsupported coercions refuse.

Generated calls use the public `dom/element.hpp` and `dom/token_list.hpp` APIs.
`validate_element` checks every incoming handle before effects. Token validation
and DOM write failures propagate as C++ `std::bad_expected_access` exceptions. The
VM adapters use the same platform implementation and retain their own value
conversion, JavaScript exceptions and Shell notifications. This entry does not
deliver Shell mutation observers, custom-element callbacks, events or rendering.

The registered `ctcompile_native_dom_entry` CTest compiles and executes real
DOM clients with GCC and the configured C++23 Clang, both printing layouts and
optimization policies. It checks document domains, detached subtree lifetime,
ordered mutations, String/Boolean conversion, validation failures, refusal
controls and absence of Script symbols. Boolean actions also exclude scalar
value-model helpers from the emitted C++.

Next is retained DOM-backed Data ownership: the document must outlive every
stored key, not merely one call. Then admit the original Button receiver/action
and its BaseComponent/Config construction, prototype/static getters and disposal.
This standalone probe does not change the untouched Bootstrap bundle's admission
denominator or complete the native application driver.
