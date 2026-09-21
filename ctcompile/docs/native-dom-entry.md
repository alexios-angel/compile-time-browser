# Native DOM entries

`ctnative-lower-to-emitc` accepts a fingerprinted `ctbrowser-dom-v1` host
contract for one synchronous function. Its explicit parameters are borrowed
`ctbrowser::element_ref` values: a document pointer and generation-tagged
`node_id`. The C++ caller owns the documents and keeps them alive throughout
the call. Identity compares both fields, including across documents; detaching
a node does not destroy it.

Entries using `matches`, `closest`, `querySelector` or `querySelectorAll` also take
the caller's live `ctbrowser::style::engine &` for each element parameter used as
a selector receiver, appended after the element parameters in source parameter order.
For example, `closest(element, expected)` takes `(element_ref, element_ref,
style::engine &)`, and two queried elements take two engine references. The
caller supplies the engine associated with each document, so hover/focus state
matches the page. Every element and engine atom-table association is checked
before source effects. The engine, document and their atom table must remain
alive throughout the synchronous call.

Generated selector calls use typed C++ prototype members. `matches` carries its
proved Style association in a borrowed `js_element_t` from `Runtime/Browser.hpp`:

```cpp
auto receiver = ctnative::js_element_t{element, styles};
auto matched = ctnative::Element.prototype.matches.call(receiver, ctnative::js_string{"button"});
auto button = ctnative::Element.prototype.querySelector.call(element, styles, "button");
auto buttons = ctnative::Element.prototype.querySelectorAll.call(element, styles, "button");
```

`closest` uses the same explicit Style form as the query methods.
`Element` is an `inline constexpr element_constructor` containing an
`element_prototype` with the existing four stateless method objects. Their
`const` members call public Style with explicit borrowed inputs. The flat
`ctnative::querySelector`, `querySelectorAll`, `matches` and `closest` names remain
constant references to those same members for existing C++ callers.

Both direct and original JavaScript prototype selector calls emit this form.
Source identity, receiver, mutation and lifetime proofs still apply; the C++
`Element` spelling supplies no JavaScript proof on its own. This composition
introduces no callable table, allocation or virtual dispatch.

`Runtime/Browser.hpp` also exposes `js_document_t{document, styles}` with
`documentElement()`, `querySelector()` and `querySelectorAll()`. Both views borrow
stable, live resources; they never own or extend their lifetime. Element views
support all four selectors through instance methods or receiver-only prototype
calls. Nullable results use `std::optional<js_element_t>` for DOM null, while
query-all returns an owning `std::vector` snapshot of borrowed elements. Checked
`.value()` extraction returns the underlying public `element_ref`.

The [typed JavaScript interface plan](plans/native-js-types.md) tracks the remaining
emitter migration and Object/Array interfaces. Source `document` access now uses
the explicit host binding below; the C++ view alone supplies no source authority.
Bootstrap's default-root helpers and an application driver remain unfinished.

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

## Explicit source document binding

Both `ctbrowser-dom-v1` and `ctbrowser-dom-session-v1` accept the optional field
`"current_document_parameter": 0`. Its value must name a declared element input.
The source `document` is that input's owning document, even when the input node is
detached. An absent field grants no document binding; other providers reject it.
This contract promises the original document binding, `documentElement` accessor,
`querySelector`/`querySelectorAll` methods and their lookup chains. The complete
source proof rejects binding replacement, accessor/method/prototype mutation,
unknown calls and escapes.
A saved document alias retains the same authority; a saved query method must still
be called with its exact original receiver.

Source `document.documentElement` and `document.querySelector(String)` emit ordinary
`ctnative::document` accessor/method calls over that input's owner and live Style engine.
The anchor's engine reference is appended in parameter order, including for root-only
entries. All input handles and engine atom-table associations are checked before
source effects. The caller owns these stable resources for the call; an owned
session provides its nonmovable document and engine.

The global `ctnative::document` object is declared alongside `Element` in
`Runtime/ctnative.hpp`. `Runtime/Browser.hpp` supplies its methods and a
noncopyable, nonmovable `document_scope` that borrows a `js_document_t` view:

```cpp
const ctnative::document_scope scope{page, styles};
auto button = ctnative::document.querySelector(ctnative::js_string{"button"});
```

Generated entries establish one lexical scope after validating all inputs.
It saves and restores the previous thread-local binding on return or exception;
nested invocations for another document therefore restore their caller's document.
Access without a scope throws `std::logic_error`. Binding validates Style before
changing the active view. Existing explicit document/element views and saved
snapshots keep their own document association when the global binding changes.
The owner and Style engine must outlive the scope and every borrowed view.
Scopes support synchronous calls on one thread; they must not span coroutine
suspension or interleaved asynchronous tasks. Separate threads have independent
bindings. No document singleton, VM context or new source admission is introduced.

The global-object integration passes the two focused document lit cases in
**128.07 s**: root/query **48 native executions, 82 refusals**, query-all
**32 executions, 84 refusals**. The clients verify that an independent outer
document is restored after successful calls, selector exceptions, invalid inputs
and rejected session calls. Exact native-runtime/host-contract CTests pass
**2/2, 0.58 s total**; runtime checks also cover failed binding, saved views and
thread isolation. These measurements use public DOM/Core/Style, with no full-suite
or new Node/VM differential claim. Earlier measurements below retain their scope.

The root is reread at each source access. Document queries use public Style with
document scope, including the root element, and preserve live interactive state.
Both return `std::optional<js_element_t>` internally. The emitted
`ctnative::element_or_null` bridge checks an engaged view and converts it to the
existing nullable `element_ref` carrier; absence is canonical DOM null, never
undefined. A truthiness or `!`/`!!` guard permits dereferencing that exact result
only in the present arm. A guard of the bound `document.documentElement` also
proves subsequent root reads present in that arm: the complete supported effect
set cannot replace the root or reenter script. This fact is restored on branch
exit and does not follow a guard of an unrelated selector result.
Unguarded use, borrowed returns/storage, branch/loop transport and explicit
source null comparisons remain refused. Invalid selector
syntax keeps the existing C++ exception and preceding source effects.

Source `document.querySelectorAll(String)` uses the same explicit binding and
proof of the original method and receiver. It retains `std::vector<ctnative::js_element_t>`
locally: the vector owns snapshot membership, and its views borrow the anchor's
document and live Style. Proved `.length` reads and bounded indexed loops reuse
the element-snapshot proof below. Each indexed use emits `.at(index).value()`
to pass a checked raw element to existing operations; the whole vector is never
converted to a second raw-element vector. Document scope includes a matching root,
preserves tree order and deduplication, and excludes shadow descendants. Empty or
removed roots yield empty snapshots. Attribute/class writes preserve saved
membership, while later queries see the changes. Returns, storage, joins,
callback retention and unproved indices remain refused; dataset-enabled mutation
loops still require a separate backedge alias proof.

The original Bootstrap `R.find` default now works under that source root guard,
with either an omitted receiver or explicit `undefined`. Local callable slots
may be read inside source `if` arms when initialized unconditionally beforehand;
conditional/late writes, replacement and wrong receivers remain refused.
Missing helper arguments become exact `undefined`, while excess arity remains
unsupported. The existing initial `undefined` binding and live entry proof
authorize selecting the original default arm. Spread/concat requires its complete
confined length and indexed-consumer proof; no default expression or source
effect is replaced with a handwritten helper.

This does not establish a root for an unguarded caller. Canonical indexed and
`for…of` loops over `R.find` members are supported as described below. General
borrowed transport, initialization and the application driver remain unfinished.

The focused `CTNative/Browser/native-dom-document-default.test` passes **32 native
executions and 92 refusals, 86.49 s**, with the unchanged vendor-pinned `R.find`
body, both providers, GCC/Clang, printing layouts and optimization policies.
It covers omitted/explicit-undefined calls, both document anchor positions,
empty/replaced roots, matching-root exclusion, detached anchors, live hover,
selector exception order, rejected inputs and outer-document restoration.
The existing spread-length regression passes **32 executions and 52 refusals,
84.42 s**. Exact host-contract CTest passes **1/1, 0.57 s total**, including
guard mutation, missing/excess arguments, callable initialization order and
budget controls. These are focused public DOM/Style measurements.

Admitted element `closest` and `querySelector` calls now use the same typed
`js_element_t` receiver and optional-result bridge as document queries. Guarded
chains retain their live Style association. The selected query/prototype-query
regressions pass **2/2, 168.70 s**: respectively **16 native executions/14 refusals**
and **64/100**. Source admission and browser behavior are unchanged by this
carrier migration.

The focused `CTNative/Browser/native-dom-document.test` passes **48 native
executions and 82 refusals** across borrowed/owned providers, both compilers,
printing layouts and optimization policies. Public DOM/Style clients cover both
anchor positions, empty/replaced roots, detached anchors, root inclusion, live hover,
cross-document identity, invalid input/Style rejection before effects, selector
failure order and absence of Script symbols. The focused host-contract CTest passes
**1/1** with schema and programmatic-provider controls; these are not full-suite or
Node/VM differential results.

The focused `CTNative/Browser/native-dom-document-all.test` passes **32 native
executions and 84 refusals** in **87.38s**, across borrowed/owned providers,
GCC/Clang, both printing layouts and optimization policies. It covers both anchor
positions, typed snapshot storage, current-root selection, ordered writes,
duplicate matches, live hover, invalid inputs/Style and selector failure order.
The selected document and Element query-all regressions also pass **2/2** in
**122.39s**: **48/82** and **16/44** native executions/refusals respectively.
The exact host-contract CTest passes **1/1** in **0.56s** total, including live IR
mutations, forged printed evidence and insufficient budgets for this query-all
path. These are focused public DOM/Style checks; no new Node/VM differential or
full-suite pass is claimed.

## Dataset key snapshots

`Object.keys(element.dataset)` returns an owning `std::vector<std::string>`.
The manifest must declare `"initial_intrinsics": ["Object"]` and an ordered,
distinct `"dataset_parameters": [0]` subset of `element_parameters`. These are
HTML/SVG inputs; every declared input is checked before source effects. MathML
and other namespace URIs remain in Shell and are not covered by this public
node-handle contract.

The generated helper projects keys from public `ctbrowser::dataset_entries`.
Keys preserve attribute order, including numeric names. Current public DOM behavior
excludes namespaced attributes and unsupported uppercase names. An empty dataset
returns an empty vector. The result owns its bytes after attribute mutation and document destruction.
No DOMStringMap object, VM value or collector is created.

The complete source proof requires the original Object/keys identity and receiver.
A saved dataset alias is accepted only when no DOM mutation intervenes before
its enumeration. Returning a saved key vector after a mutation and rereading
`element.dataset` for a fresh snapshot are supported. Dataset/value writes,
vector mutation or identity and unproved value reads remain refused. Missing
dataset values need an Undefined/prototype proof separate from getAttribute's
String-or-null result.

The original Bootstrap filter is also supported:

```javascript
Object.keys(element.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"))
```

Its manifest supplies `"initial_intrinsics": ["Object", "Array", "String"]`.
Array fixes the original filter method and default constructor/species chain, as
well as `Array.prototype[Symbol.iterator]`/`values` and its iterator prototype chain
without custom next/return hooks. String fixes the original startsWith method.
The complete source proof requires a capture-free,
confined callback with one String parameter and a Boolean result. A branch-local
creation retains the same complete callback/use proof. Implicit arguments,
callback identity, side effects and prefixes not proved as ASCII String constants
remain refused.
The callback becomes an ordinary C++ function; `std::copy_if` creates a second owning
String vector. Empty/all-rejected results, source order, saved snapshots, and lifetime
after document destruction are covered. Missing intrinsics, replacement methods,
forged proof attributes and every insufficient work budget withdraw all evidence.

An exact `.length` read on either owning snapshot returns a JavaScript Number
through the existing `ctnative::vec_length` helper. The complete proof identifies
each read and its owning String-vector receiver, and rejects vector writes.
Original `for...of` over either owning snapshot is supported for one top-level
source iterator with scalar Number/String/Boolean loop state. For the Bootstrap
filter, its manifest supplies:

```json
{
  "initial_intrinsics": [
    "Object", "Array", "String",
    "__ctbrowser_for_of_open", "__ctbrowser_iter_next", "__ctbrowser_iter_close"
  ]
}
```

The three helper identities and original Array iteration permit private specialization
of the dense snapshot path. Existing helper/cell expansion runs first, followed by
exact element-guard normalization on the private fingerprinted candidate. For a
validated nonnullable element parameter, the original `if (!t) return {};` and
truthy/alias variants select only their live continuation. Guard facts transfer
through Not/Truthy, never through unproved cells, nullable reads, joins or loop
state; insufficient budgets leave the candidate unchanged. Preparation then reuses
the complete dataset/filter proof for the original prefix and reproves the entry.
Source completion dispatch preserves exact loop condition/yield tuples and register correspondence. A poison slot
can be removed only for a constant-selected, unused destination with a same-type live
state replacement; every source operation is accounted for.

Each indexed read requires an index starting at Number zero, a Number-one increment
on every continuing backedge, and dominance by the true arm of `index < length` for
that exact immutable vector. The emitted `vector.at` copies an owning String. Original
count, ordered-key and saved-snapshot loops retain their source order and lifetime.
Wrong starts/updates/guards/forwarded slots, vector mutation, loop DOM mutation,
escaped helper records, unknown calls and insufficient budgets refuse. Nested or
multiple source iterators and dynamic key normalization remain outside this proof.

Inside the proved loop, `element.dataset[key]` returns an owning `std::string`
through public `ctbrowser::dataset_value`. The key must be the exact indexed member
of that element's original or filtered immutable snapshot, with no intervening DOM
mutation. A fresh dataset lookup cannot renew a stale snapshot key. Saved aliases
and separate calls reading updated attributes are supported; results survive document
or session destruction. Joined, transformed, carried, literal and cross-element keys
do not inherit membership. Every incomplete proof withholds all member evidence.
This establishes a present own String, with no missing-property prototype lookup.

The original Bootstrap `n.replace(/^bs/, "")` is supported on a proved String.
Its manifest also supplies `"RegExp"` and `"__ctbrowser_regexp"`, together with
`"String"`. The exact anchored literal must have empty String flags, one confined
replacement use and an empty String replacement; the original String receiver and
intrinsic identities are required. The emitted C++ uses `starts_with("bs")` and
`substr`, preserving all suffix bytes in an owning String without a regex engine.
Unanchored/flagged/general patterns, escaped or reused literals, changed methods,
reentry and incomplete proof budgets remain refused. The following Unicode first-code-unit
case conversion is a separate boundary.

The source tests compare Node and the VM using a DOMStringMap-shaped `ownKeys`
Proxy. Chromium independently confirms attribute order and live saved-dataset
keys. Shell's current binding instead refills an ordinary proxy target on dataset
lookup; its numeric sorting and stale saved enumeration are a separate runtime
boundary, not evidence from those source-double comparisons. A further Chromium
witness includes namespaced `data-hidden` and `p:data-other` attributes; the shared
DOM core skips them. Native retains that platform limitation. The fixture
expectations must change when the core gains namespace support.

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
unknown receivers, unproved loops and unstructured control flow refuse.
Current operations are
strict element identity, Boolean/Number negation, and Number/Boolean/String/undefined
constants and returns,
`classList.toggle(token[, force])`, `classList.contains(token)`,
`classList.add(...tokens)`, `classList.remove(...tokens)`, `toggleAttribute(name[, force])`,
`getAttribute(name)`, `hasAttribute(name)`, `removeAttribute(name)` and
`setAttribute(name, String-or-Boolean)`. Tokens and names come from definite source strings, including String + String expressions;
force is a proved Boolean (including an earlier DOM result) or explicit undefined.
Class-list add/remove accept zero or more definite String arguments and return
undefined (C++ `void`). Saved local class lists read the element's current
attribute. All mutation arguments are validated before any token is changed;
the first invalid argument throws `std::bad_expected_access<ctbrowser::token_argument_error>`
with its index and token error. Empty calls normalize an existing attribute,
including same-value writes, but leave an absent attribute absent. Membership
does not write and preserves the current browser behavior for empty or
whitespace-containing tokens: false. Add/remove invalidate saved dataset
presence proofs, including calls with no arguments; contains preserves them.
`contains(otherElement)`, `matches(selector)`, `closest(selector)`,
`querySelector(selector)` and `querySelectorAll(selector)` also use proved element
receivers. Selectors are source strings, parsed by the
existing Style parser at the source call; invalid syntax throws
`std::invalid_argument` after any preceding source effects. `contains` preserves
document identity before calling `read_txn::is_ancestor_of`. Selector calls use
`engine::element_matches`, `engine::closest` and `engine::select`, the same cores
as the bindings. Element `querySelector` returns the first descendant in current
tree order, excludes the receiver itself, binds `:scope` to that receiver and
preserves the platform's shadow boundaries. It also works in detached subtrees.
Document `querySelector` uses the explicit binding above and includes the root.

Bootstrap's original prototype selector spelling is supported for an explicit
element and a proved String:

```javascript
const select = Element.prototype.querySelector;
const button = select.call(element, 'button');
const buttons = Element.prototype.querySelectorAll.call(element, '.selected');
```

The manifest must supply `"initial_intrinsics": ["Element", "Function"]`.
Element promises its original global binding, own `prototype` and original
`querySelector`/`querySelectorAll` methods. Function promises those methods'
original prototype chains and `Function.prototype.call`, without own `call`
shadows or accessors. These are embedding guarantees, not runtime checks.
The complete source proof rejects replacement, mutation, callable/prototype
escape and reentry, and requires `.call` to retain the exact selector function
as its receiver. Its two arguments must be a proved nonnull element and String;
guarded query results and checked snapshot members can supply that element.
Saved constructor, prototype and method aliases preserve the same identities.

The compiler erases the proved global/prototype/method reads and emits the
existing typed selector call, using argument zero's document and live Style
engine. Missing guarantees, detached `.call`, `.apply`/`.bind`, coercible
selectors and unguarded nullable receivers refuse. An explicitly bound, guarded
`document.documentElement` can supply an element. The original `R.find` default
uses that guard and its complete helper proof.

Element `querySelectorAll(String)` calls public `engine::select` with
`first_only=false` and returns a local `std::vector<ctnative::js_element_t>`. The vector owns its
slots; each typed view borrows the caller/session document and live Style engine.
Indexed uses extract checked elements through `.at(index).value()` for the existing
DOM operations. The shared selector engine
provides tree order, deduplication, root exclusion, `:scope`, detached-subtree
queries and shadow boundaries. Attribute/class changes leave saved membership
unchanged; a later query sees current membership.

Snapshot `.length` and exact indexed loops are supported:

```javascript
const buttons = element.querySelectorAll('.selected');
for (let i = 0; i < buttons.length; i++) {
  buttons[i].classList.remove('selected');
}
return buttons.length;
```

The index must start at zero, advance by one on every backedge, and be guarded
by `index < snapshot.length` for that same snapshot. Both structured loop forms
are proved: a guarded body before `scf.condition`, or an after body entered on
its true edge. Indexed elements retain their document and live Style engine,
so existing DOM/selector operations are ordinary public C++ calls. Attribute
and class writes are allowed when the contract has no dataset parameters;
dataset-enabled loops still need a separate backedge alias proof. No supported
operation can reclaim a selected node or reenter script.

Snapshot writes, out-of-bounds or unproved indices, identity observation,
borrowed returns, retained callbacks, NodeList `forEach` and custom iterator
protocols remain refused.

`for…of` over proved element snapshots and confined `R.find` copies uses the
existing native loop lowering over `std::vector<js_element_t>`:

```javascript
for (const button of element.querySelectorAll('.selected')) {
  button.classList.remove('selected');
}
```

The manifest must additionally promise original `Array`, `Element`,
`__ctbrowser_for_of_open`, `__ctbrowser_iter_next` and `__ctbrowser_iter_close`
identities. The compiler proves each iterator input using a private prefix,
retaining enclosing conditions so a guarded Bootstrap default retains its root
authority. That proof observes length rather than returning a borrowed snapshot.
It then removes the importer’s protocol alternative and reproves the complete
live entry, including all effects and element uses. Emission uses ordinary C++
indexed loops and checked vector access; it does not yet print range-for syntax.
There is no VM iterator or generic runtime value.

Direct NodeList iteration and spread/concat both preserve the VM’s 2^24 proxy
materialization cap. Separate observations of the original NodeList remain
uncapped. The vector owns membership; each member borrows its document and live
Style. Supported DOM writes do not change saved membership. Sequential and
conditional iterator opens are supported; opens nested inside another loop need
an additional prefix/state proof. Custom hooks, snapshot mutation or escape,
missing intrinsic guarantees and unguarded default roots remain diagnostics.

A confined spread into an empty concat receiver supports `.length` and canonical
indexed element loops:

```javascript
const R = {
  find: (t, e = document.documentElement) => [].concat(...Element.prototype.querySelectorAll.call(e, t))
};
const nodes = R.find('.selected', element);
for (let i = 0; i < nodes.length; i++) {
  nodes[i].classList.remove('selected');
}
return nodes.length;
```

This preserves the original Bootstrap helper. Its holder, property read and call
must satisfy the local callable-holder proof above, including unconditional
initialization before branch-local reads. The supplied element makes
its default arm unreachable: exact declared element identities compare false to
`undefined` under strict equality in either operand order. This fact does not
propagate through unproved cells, joins or nullable query results. Omitted or
explicit-undefined receivers instead select the unchanged default arm under a
source guard of the bound document root.

The manifest supplies `"initial_intrinsics": ["Array", "Element", "Function"]`.
Array additionally promises original `Array.prototype.concat`, default
constructor/species and no spreadability hooks on the fresh receiver. Element
promises no `Symbol.isConcatSpreadable` hooks on result wrappers or their complete
prototype chains. The complete source proof rejects operations that invalidate
these guarantees, including unknown calls, mutation and escaping identities.

The compiler proves the imported zero-based spread loop, its sole append, fresh
empty receiver and arguments array, and confined result observations. It keeps
the public selector call and replaces concat length by
`min(snapshot.length, 16777216)`, using ordinary scalar C++ control flow. That
limit preserves the current VM's proxy materialization cap; direct NodeList length
is uncapped. Indexed copies must use their own `index < copied.length` loop
condition, with zero-based unit progression. This is checked before rebinding
the copied slots to the original snapshot; the uncapped original NodeList length
cannot authorize a copied index. Complete live DOM proof then checks the exact
minimum, member bounds, effects and borrowing. Saved membership and ordinary
element identity survive supported attribute/class writes. No JavaScript array,
iterator, callable table or runtime value is emitted. Work-budget exhaustion or a
stale fingerprint leaves the original source unchanged, and the resulting private
candidate must pass the complete DOM proof before publication.

Nonempty receivers, additional arguments/spreads, detached concat, replaced methods,
unproved indices, array identity, escape or mutation remain refused. Unguarded
default document roots also refuse. The source proof tests
check the exact cap and selected arms;
standalone native tests cover empty/scoped/detached results, saved counts across DOM
writes, invalid-selector effect order and borrowed/owned document validation.

Shell's collection numeric reads now check canonical index parsing, overflow and
actual membership, matching `length` and `item`; the former 1,000,000 numeric read cap
is removed from `bindings/document/collections.cpp`. Reading an existing vector
member does not allocate by index, and the old guard ran after collection refresh.
The focused `dom_nodes_wpt` check passes with huge absent, overflowing and
noncanonical index reads; no collection with over one million members was executed.
The separate `ownKeys` enumeration limit of 1,000,000 and proxy-spread limit of
2^24 are unchanged. The confined indexed concat proof above does not admit general
JavaScript Array behavior or custom NodeList iterator protocols.

The focused `native-dom-find-elements.test` passes **48 native executions and
114 refusals** with the unchanged vendor-pinned helper, explicit receiver and
guarded omitted/undefined defaults. Both providers, GCC/Clang, printing layouts
and optimization policies cover ordered element writes, identity, live hover,
saved membership after class mutation, document-root replacement, selector failure
order and outer-document restoration. Existing element query-all **16/44** and
spread-length **32/52** pass alongside it: **3/3 selected lit cases, 141.75 s**.
Exact host-contract CTest passes **1/1, 0.66 s total**. Source controls preserve
the uncapped original NodeList alias, reject copied indices guarded by that alias,
and withhold evidence after cap/arm mutation or incomplete budgets. The cap is
checked structurally; no above-cap collection or new Node/VM differential run is
claimed. Full suites were skipped.

A `closest`, `querySelector` or document-root result is a local borrowed identity,
with a canonical empty `element_ref{}` for no match. Strict equality compares it
with another result or an element parameter, so misses compare equal even across documents. A truthiness
or `!`/`!!` guard permits existing DOM operations on that exact result inside the
present branch:

```javascript
const button = element.closest('[data-bs-toggle="button"]');
if (button) {
  const active = button.classList.toggle('active');
  button.setAttribute('aria-pressed', active);
}
```

Guarded results may also be passed to `contains` or call `matches`, `closest`,
`querySelector` and `querySelectorAll`. Selector chains retain the original input's
live Style engine; each new `closest` or `querySelector` result requires its
own guard.
The absent branch, another result and uses after the guard acquire no
dereference permission. Borrowed returns, property storage,
branch/loop transport, dataset access on derived elements and explicit source
`null` comparisons still refuse. The caller's document remains the sole owner;
the admitted synchronous operations neither destroy nodes nor reenter script.
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
two definite Strings and returns an owning `std::string` with public Core
surrogate normalization; optional Strings are not implicitly coerced into names or
values. Loose equality between two independently proved Strings is supported;
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
`"object" == typeof parsed` observation and the original following spread compile:

```javascript
const i = H.getDataAttribute(element, "config");
return {..."object" == typeof i ? i : {}};
```

The object tag proves only null/array/object alternatives. Null contributes no
properties; arrays contribute indices without `length`; objects preserve their
own data keys. Fresh `{}` constructs the explicit object alternative of
`ctbrowser::json_value`. Ordered spreads overwrite values without moving ordinary
keys, sort array-index keys first, and retain `__proto__` as an own data key.
This is distinct from assignment through its inherited setter.

Constant String-key assignments on the same fresh object also compile, including
writes inside proved branches and dataset loops. Values must be owning JSON trees,
Strings, optional Strings, null, Boolean or Number. Empty keys, embedded NULs,
`constructor` and numeric-looking names retain their String identity. A shared
member-update helper preserves first position/last value for ordinary keys and
numeric index order for assignment and spread. A constant `__proto__` assignment
refuses; spread's own-data definition does not prove the inherited setter's behavior.

Direct keys from one immutable dataset snapshot may also index assignments to a
fresh result. The snapshot and target must be outside the traversal, the original
zero/+1 index visits each source key at most once, and this is the target's sole
writer. Under final-own-data observations, the one possible inherited `__proto__`
setter changes no own member. Its value is still evaluated, including original M's
URI/JSON failure paths, before the owning assignment helper omits that update.
Other keys retain ordinary ordered last-write behavior.

The exact `n.replace(/^bs/, '')` key is supported when a proved pure filter
admits only keys beginning with `bs`, including the original Bootstrap filter.
The callback's Boolean/branch proof establishes that implication; later filters
preserve it. Removing this guaranteed prefix is injective, so only `bs__proto__`
can invoke the prototype setter. The stripped key acquires assignment authority
only: it does not prove `element.dataset[stripped]` exists. Unfiltered replacement,
weaker filters admitting both `__proto__` and `bs__proto__`, repeat stripping,
additional writers, nested traversals and intermediate observations refuse.
Unicode first-code-unit lowercase/slicing and its collision proof remain separate.

Every mutable target must be a direct fresh local allocation. A spread stays in
its allocation block. Every write must finish before any branch yield, source
spread, value assignment or return can snapshot the target. Conditional and loop
writes are accepted only when their entire containing region precedes each such
observation; an observation inside the target's mutation loop refuses. No descendant
property access, identity observation, capture or later mutation is admitted. These
complete-use restrictions make the owning result equivalent to JavaScript's shallow
aliases under the supported observations, without introducing a shared object graph.
Parsed nested trees retain the public Core representation, including parse-order
keys; a JSON observation applies JavaScript enumeration to those keys.

Immutable saved cells may be read inside structured branches only when their
initialization precedes the whole branch; conditional and late cell assignments
refuse. Generic JSON/String spreads, parsed/branch-result targets, Undefined and
borrowed assignment values, and mutation of browser dataset/config objects remain
unsupported.

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
capture loads bind at each invocation. Callable/cell identities and local helper
calls inside branch arms remain refused; fresh data objects have the separate
complete JSON-spread proof above. **83da7d42** additionally proves
acyclic completion dispatch: a bounded private rewrite carries exact yields into
each branch continuation and selects only constant completion tags. Every original
operation must be visited, and observing an inactive poison value refuses. Source
effects and frame exits remain in their original paths; the unchanged proof checks
the resulting branches. Three/four-return helpers and captured String snapshots
compile; unknown selectors, unvisited arms, invalid frame exits and
general exceptions remain refused. Original snapshot loops use the separate exact
terminal-tuple and scalar-state proof above. The bounded URI/JSON cases above have
their own complete source and ownership proofs.

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

Template concatenation also uses the owning surrogate-normalizing Core helper
when both operands are proved Strings. Original Bootstrap H `setDataAttribute` and
`removeDataAttribute` compose with captured F, including a factory-exported entry
table. Nullable values, objects, Numbers and Booleans do not gain implicit template
conversion. H's `getDataAttribute` composes with original M for a proved constant
no-match key such as `"config"`. Matching/live keys and the complete
`getDataAttributes` remain unproved. Dataset key snapshots, the original filter and
its count/ordered-key/saved-snapshot loops have the separate complete proof above;
live dataset values and guarded M-loop `typeof` observations now compose. Helper
and capture initialization must dominate each actual call through its enclosing
structured branches/loops;
Invoke-boundary crossings, late/mutable captures and escaping callables still
refuse. Actual arguments and both URI/JSON failure continuations are preserved.
The dataset driver executes these paths with Node/VM comparisons, both native
providers/policies/layouts, HTML/SVG, repeated calls and lifetime sanitization.
Its `typeof` observations check result categories; the JSON driver checks full
returned values. Unicode key normalization and ordered dynamic result writes
remain next boundaries.

Explicit undefined force preserves the current platform adapters: classList
treats it as omitted, while Element.toggleAttribute treats it as false. Private
preparation omits the former argument and replaces the latter with a Boolean
constant before reproof and C++ type selection. Forced no-ops preserve attribute
bytes and mutation behavior; invalid toggle names still throw before a no-op.

Generated calls use the public `dom/element.hpp` and `dom/token_list.hpp` APIs.
Class-list membership and mutations share `contains_token`, `add_tokens` and
`remove_tokens` with the Shell adapters; the native wrappers only assemble typed
arguments and propagate the shared API's errors.
Attribute toggling shares `toggle_element_attribute` with the VM adapter;
presence and removal call the existing document API and shared name folding.
`validate_element` checks every incoming handle before effects. Token validation
and DOM write failures propagate as C++ `std::bad_expected_access` exceptions. The
VM adapters use the same platform implementation and retain their own value
conversion, JavaScript exceptions and Shell notifications. This entry does not
deliver Shell mutation observers, custom-element callbacks, events or rendering.

The focused `CTNative/Browser/native-dom.test` lit case compiles and executes real
DOM clients with GCC and the configured C++23 Clang, both printing layouts and
optimization policies. It checks document domains, detached subtree lifetime,
ordered mutations, String/Boolean conversion, validation failures, refusal
controls and absence of Script symbols. Selector clients additionally link Style;
other actions keep DOM/Core-only linkage. Query checks cover live interactive
state, atom-table mismatches, shadow boundaries and cross-document misses.
Boolean actions also exclude scalar
value-model helpers from the emitted C++.
`native-dom-class-list.test` additionally checks variadic and zero-argument
mutations, validation atomicity, saved aliases, HTML/SVG, guarded selector
receivers, void results, and mutation-epoch refusal controls under both DOM
providers. The browser's `dom_token_list`, `dom_mutation` and `element_attrs`
CTests cover the shared core and Shell adapter behavior.

The `ctcompile_native_dom_strings` CTest compares **783** copied-value, Boolean
and numeric-prefix observations with Node and the ctbrowser VM, then executes
eight GCC/Clang clients across both providers, optimization policies and printing
layouts. It checks copied optional Strings, saved numeric/text results after DOM
mutation and destruction, invalid handles before effects, document domains, name
bytes and casing. **1,056** source refusals cover unsupported coercion, control flow,
handles, retention and builtin misuse, alongside identity, capture, branch,
completion, fingerprint and work-budget controls. The **132 numeric observations**
cover 22 input strings/null across six source shapes, including canonical comparison
and repeated Number calls. These clients link DOM/Core only and reject Script/AOT
symbols or generic nullable value helpers in generated code.

## Owned synchronous sessions

Select `ctbrowser-dom-session-v1` with the same fingerprint, entry and
`element_parameters` fields to emit an additional `<entry_symbol>_session` class.
It owns an atom table, then its document, then a selector engine when the source
uses selectors or the explicit document binding. The class cannot be copied or
moved. `document()` provides the document for building the page; `selectors()`
exposes the owned engine's live interactive state when present. `invoke(element_ref, ...)` calls the same proved
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

## Owned Data sessions

`ctbrowser-dom-data-session-v1` emits a nonmovable session owning its document
and private Data storage. It uses the closed-source contract fields (`roots`,
`observations`, initial bindings and intrinsics) plus the same ordered, nonempty
`element_parameters` declaration.
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
`OwnedGlobalRoots` revalidates the complete source/input family before native
typing and emission. Preparation uses a fingerprinted private clone and removes
only the proved inert declaration. Source-prefix specialization still refuses
this provider.

Source functions, roots, tables, Maps and observations become private session
members. Tables and roots use private borrowed pointers; no callable or
DOM-bearing table escapes. Member order destroys Data before the document and
the document before its atom table. `document()` exposes the owned document,
`invoke(element_ref, ...)` checks every document domain before validating any
node or performing source effects, and `observe_*()` returns scalar copies.
Map keys preserve document plus complete node identity without dereferencing an
owner. Detached nodes retain that identity.

Source allocation timing is preserved: the `dataEntry` fixture creates a new
root, Map and method table on every invocation, so the generated code resets
their storage at those source operations. A persistent initialization/action
split still requires its own source proof. The focused
`native-dom-data-session.test` retains both `has` and `get` cases, including
associative storage without snapshots; its separate snapshot source still
refuses for lack of a complete getter proof.

Historical validation on 2026-09-14 (`523e631d`) compiled the pinned **3,218-byte
Bootstrap Data program** with only its three synthetic probe keys adapted to
DOM inputs: **7/7 functions, 23 direct Data calls and 19 observations** agreed
with Node and the interpreter across **five alias partitions**. The
`native-bootstrap-dom-data-session.test` gate covers both optimization policies,
printing layouts and GCC/Clang, plus isolation, repeated invocation, detached
keys, invalid/foreign handles, private-access and missing-reset controls, and
generated-client ASan/UBSan. These are prior measurements, not a fresh replay.

Original Button/BaseComponent constructor publication through Data, retained
DOM/config payloads, disposal, retained callbacks and the native application
driver remain separate work; the Data session does not establish full native
Bootstrap initialization.
