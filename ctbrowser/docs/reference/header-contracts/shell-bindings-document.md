# Shell Bindings Document contracts

<a id="contract-1"></a>

`value listener_fence_;`

--- the listener fence ---

THE FENCE EVERY LISTENER IS CALLED BEHIND, and it is a JavaScript
function rather than a C++ try because the exception it has to stop is
not a C++ one.

`context::throw_error` unwinds to the innermost live `try` ANYWHERE
below it on the stack - `handlers_` is one list for the whole VM - so
without a fence a listener that threw would land in whatever `try` the
page happened to be inside. The DOM says "if this throws an exception,
then report the exception": the dispatch continues and the page is told
through an `error` event.

A `try` INSIDE the callee is the only thing the VM's unwinder stops at,
so the fence is

    function (invoke, callback, receiver, args) {
        try { ...call it... } catch (e) { return [e]; }
        return null;
    }

compiled once per page. It is also where WebIDL's "call a user object's
operation" lives, because both halves of that algorithm can throw and
both have to be INSIDE the fence: the `handleEvent` Get - which a page
may make an accessor - and the TypeError for a listener object whose
`handleEvent` is not callable.

<a id="contract-2"></a>

`value listener_invoke_;`

The native the fence calls back into: `invoke(fn, receiver, args)`, where
`args` is one value or an array of them. Kept because the window's
`onerror` takes five positional arguments rather than the event.

<a id="contract-3"></a>

`void install_listener_fence(context & cx, script::native_object & keeper);`

Build the native at install and the fence at the first dispatch - see
compile_listener_fence for why not sooner. A compile failure leaves the
fence undefined and the unfenced C++ path stands in.

<a id="contract-4"></a>

`[[nodiscard]] bool invoke_listener(context & cx, value callback, value receiver, value args,`

Call one listener - a function, or an object with a `handleEvent` - with
`this` bound to `receiver`. True when it threw, with the thrown value in
`thrown`; a VM fault that was never an exception reports false and leaves
`context::failed()` set, as it always did. `returned` is what the callee
returned when that was a BOOLEAN and undefined otherwise - the one part
of a return value HTML's "processing the return value" reads, and the
one part the fence can hand back without an allocation per call.

<a id="contract-5"></a>

`void install_event_handler_attributes(context & cx);`

--- event handler IDL attributes (HTML 8.1.7.2) -----------------------

`el.onclick`, `document.onclick`, `window.onload`: an ACCESSOR on the
interface prototype, null when unset, and the setter takes only an
object - `el.onclick = ""` stores null, which is what
Body-FrameSet-Event-Handlers.html spends a third of its assertions on.

<a id="contract-6"></a>

`[[nodiscard]] value event_handler_get(context & cx, value self, const std::string & name);`

The handler currently registered for `name` on `self`, compiling the
content attribute if that is where it still is.

<a id="contract-7"></a>

`void activate_event_handler(context & cx, path_step at, std::string_view type);`

The event handler's listener for `on<type>` at a step (HTML 8.1.8.1,
"activate"/"deactivate an event handler"): appended to the listener
list the first time the handler is set to something - by the IDL
attribute, or by the content attribute as `mutated()` sees it written -
so it fires in registration order among addEventListener's, and
removed when the handler goes back to null.

<a id="contract-8"></a>

`void settle_attribute_writes(const std::vector<document::write_note> & writes);`

The content attributes `mutated()` saw written since the last time: an
`on*` attribute (de)activates its handler, an input's `type` its state.

<a id="contract-9"></a>

`[[nodiscard]] value compile_handler_attribute(context & cx, value self,`

`onclick="doThing()"` as a function, compiled once and cached on the
object it belongs to. Undefined when the attribute is absent or will not
compile - HTML says a handler that fails to compile is null.

<a id="contract-10"></a>

`[[nodiscard]] value attach_shadow(context & cx, node_id host, std::span<value> args);`

`element.attachShadow(init)`, DOM 4.8. Answers the ShadowRoot, or
undefined HAVING ALREADY THROWN - a TypeError for a missing or unknown
`mode`, a NotSupportedError for a second attach or for an element that
cannot host one.

<a id="contract-11"></a>

`void install_shadow_root_members(context & cx, script::object_object & obj, node_id root);`

The members a ShadowRoot has that an ordinary DocumentFragment does not.
Installed from wrap(), AFTER the prototype link, so the two it
replaces - querySelector and querySelectorAll, which have to search a
DETACHED subtree - overwrite the general ones rather than race them.

<a id="contract-12"></a>

`void install_xml_serializer(context & cx);`

`XMLSerializer` - bindings/domparsing.cpp, the serialising half of the
DOM Parsing specification (DOMParser itself is in window/window.cpp).

<a id="contract-13"></a>

`[[nodiscard]] std::string serialize_xml(node_id node, std::string_view inherited) const;`

One node as XML, with `inherited` the default namespace its parent put
in scope. Not the HTML fragment serialiser: see the file.

<a id="contract-14"></a>

`void install_shadow_dom(context & cx);`

Slots, `assignedSlot`, `setHTMLUnsafe` and `getHTML` - bindings/shadow_dom.cpp.
On the interface prototypes, so it runs once and AFTER the table exists.

<a id="contract-15"></a>

`[[nodiscard]] std::vector<node_id> assigned_nodes_of(node_id slot) const;`

The slottables one <slot> has been given, in tree order. A function of
the two trees rather than a stored list: see the file.

<a id="contract-16"></a>

`[[nodiscard]] node_id assigned_slot_of(node_id slottable) const;`

The <slot> a slottable is assigned to, or a null id - the FLAT-TREE
parent of a slotted node, which is not its light-DOM parent. Ignores
open/closed mode: an event still routes through a closed slot, so
Slottable.assignedSlot (which hides a closed tree) keeps its own check.

<a id="contract-17"></a>

`void attach_declarative_shadow_roots(document & doc, node_id within);`

Every `<template shadowrootmode>` under `within` turned into the shadow
root it declares, IN `doc` - which is the scratch document a fragment
was parsed into. `setHTMLUnsafe` runs it; `innerHTML` deliberately does
not.

<a id="contract-18"></a>

`[[nodiscard]] node_id root_of_tree(const read_txn & txn, node_id from, bool composed) const;`

"Shadow-including root", DOM 4.4: the top of the tree `from` is in, and
with `composed` the walk continues through each shadow host rather than
stopping at the ShadowRoot.

<a id="contract-19"></a>

`[[nodiscard]] node_id body_or_frameset_of(value self);`

HTML's window-reflecting body element event handler set: `body.onload`
is the window's, and a `<body onload>` content attribute is compiled onto
the window. Lazy - checked on read - because there is no attribute-change
hook; see events/dispatch.cpp. `forwarded_from_` is the element whose
attribute last supplied each window handler.

<a id="contract-20"></a>

`[[nodiscard]] dom_bindings & target_owner(value self);`

Which bindings an EventTarget receiver belongs to - `owner_of` for a
node, the document's own for a Document, else this. The EventTarget
methods route through it so a second document's nodes get a path.

<a id="contract-21"></a>

`void set_current_script(node_id script);`

What the browser tells the document as a load progresses.
`document.currentScript`: the <script> running now, or none.

<a id="contract-22"></a>

`void set_ready_state(std::string_view state);`

`document.readyState`, with `readystatechange` at the document when it
changes.

<a id="contract-23"></a>

`bool dispatch_focus(std::string_view type, node_id target, node_id related);`

A FocusEvent at `target` naming `related` (the element focus came from
or went to): `focus`/`blur` do not bubble, `focusin`/`focusout` do.

<a id="contract-24"></a>

`void install_frame_accessors(context & cx);`

`contentWindow`/`contentDocument` on HTMLIFrameElement.prototype, which
build a not-yet-reconciled frame on demand. See frames.cpp.

<a id="contract-25"></a>

`void install_element_reflection(context & cx);`

`ariaActiveDescendantElement` and the seven `aria*Elements` lists: HTML
2.6.1's Element and FrozenArray<Element> reflection, with the explicitly
set attr-element kept on the wrapper. See element/reflection.cpp.

<a id="contract-26"></a>

`void install_double_reflection(context & cx);`

`progress.max` and `<meter>`'s six: HTML's double reflections, on
their interface prototypes. See element/reflection.cpp.

<a id="contract-27"></a>

`static void install_iterable_declaration(context & cx, script::object_object & proto,`

WebIDL's iterable declaration on a collection prototype: `@@iterator`,
and keys/values/entries/forEach unless `named_only` (a named collection
has the first alone). document/collections.cpp; public because the
file-local prototype builder there calls it.

<a id="contract-28"></a>

`void queue_scroll_event(node_id target);`

A `scroll` event at `target` (the document when empty) on the next
tick, once however many times it is asked for before then (§13.1's
pending scroll event targets). The browser calls it for a scroll the
user made; the bindings call it for their own.

<a id="contract-29"></a>

`void report_media_query_changes();`

"Evaluate media queries and report changes" (§13) for THIS document's
MediaQueryLists, after whoever changed its environment: a `change` at
each list whose answer flipped, one tick later. bindings/media_queries.cpp.

<a id="contract-30"></a>

`std::vector<node_id> unstarted_scripts_;`

SCRIPTS A PAGE MADE AND HAS NOT RUN. HTML's "prepare the script element"
runs when one becomes connected (the post-connection steps) or, once
connected, when its children change; `mutated()` is where both are
noticed. A parser-inserted <script> that was empty is in here too - it
was never started, so text appended later runs it. See document/entry.cpp.

<a id="contract-31"></a>

`void execute_script_element(context & cx, node_id id, const std::string & source);`

"Execute the script element" for a classic script's source text:
currentScript set and restored, an uncaught throw reported.

<a id="contract-32"></a>

`void install_range(context & cx);`

DOM 5, Range - bindings/document/range.cpp. `Range` the global and its
prototype, and the document's `createRange`.

<a id="contract-33"></a>

`void register_live_range(value range);`

THE LIVE RANGES (DOM 5.5): every Range of the realm, on the primary.
`settle_live_ranges` runs the specification's range steps for the tree
and data edits the document logged since the last mutation - the
pre-remove steps, the insertion steps, "replace data" - from
mutated(), before any script can read a boundary. splitText and
normalize carry their own steps in their bindings.
ponytail: a range is held for the life of the page (a Range that the
collector could see go would need a weak list); prune if a page makes
them in a loop.

<a id="contract-34"></a>

`void split_live_ranges(node_id node, node_id made, double offset, node_id parent,`

"Split a Text node" steps 7.2-7.5 (DOM 4.11): a boundary in `node` past
`offset` moves into `made` (at index `made_index` under `parent`), and one
on the parent at exactly made_index moves past it.

<a id="contract-35"></a>

`void absorb_live_ranges(node_id current, node_id parent, double index, node_id node,`

normalize() step 7.5-7.8 (DOM 4.7): a boundary in `current` (the text
sibling about to be absorbed, at `index` under `parent`) moves into
`node` at `length` plus its offset; one on the parent at `index` to
(node, length).

<a id="contract-36"></a>

`void install_selection(context & cx);`

The Selection API - bindings/selection.cpp. `Selection` the global,
`getSelection()` on the window and on Document.prototype, and the one
selection object they both answer with.

<a id="contract-37"></a>

`flat_map<std::uint64_t, script::object_object *> adopted_away_;`

WRAPPERS THAT LEFT WITH THEIR NODE. `node_from` adopts by cloning into
the other document's slab and rebinding the page's wrapper to the copy;
the node here keeps its slot, and anything that finds it again by id -
`template.content` after the contents were adopted - must answer the
same object. Marked as roots; see wrap().

<a id="contract-38"></a>

`flat_map<std::string, value> named_collections_;`

`document.x` for several elements of one name is ONE live collection
per name (HTML 3.1.5), so `document.a === document.a` even as the
members change. Marked as roots.

<a id="contract-39"></a>

`std::vector<node_id> moved_by_mutation_;`

THE NODES A MUTATION MOVED: connected before the insertion that is
being announced, so their subtrees were REMOVED for a moment - which is
when HTML's focus fixup rule runs, and `is_connected` afterwards cannot
see. moveBefore does not go through this, and keeps focus. Cleared
after the hook.

<a id="contract-40"></a>

`bool moving_ = false;`

Set while `moveBefore` moves: DOM's "move" runs neither the removing
nor the insertion side effects an ordinary insertion has - no focus
fixup, and no script "children changed" steps (script-move-before.html).

<a id="contract-41"></a>

`void install_form_owner(context & cx);`

`form` on the form-associated elements - the form owner, HTML 4.10.17.3.
element/reflection.cpp.

<a id="contract-42"></a>

`[[nodiscard]] unsigned foreign_document_position(value given);`

`compareDocumentPosition` against a node or Document of ANOTHER document
in the realm: DISCONNECTED and IMPLEMENTATION_SPECIFIC, with the
direction the specification only asks to be consistent taken from the
order of the two bindings. Zero when `given` is not one of those.

<a id="contract-43"></a>

`void bind_attr_object(context & cx, script::object_object & attr, node_id owner,`

An Attr's value accessors and ownerElement, (re)bound to `owner` - or to
nowhere. See element/attributes.cpp.

<a id="contract-44"></a>

`[[nodiscard]] attribute attribute_of_object(context & cx, value given);`

The four parts of an Attr read off the object; an empty name when it is
not one. And a detached copy of one, for cloneNode and importNode.

<a id="contract-45"></a>

`[[nodiscard]] std::string outer_html(node_id target) const;`

`outerHTML`, HTML 13.2 / DOM Parsing: the element serialised WITH its own
tag, and the setter that parses in the parent's context and puts the
result in the element's place. See document/tree_ops.cpp.

<a id="contract-46"></a>

`[[nodiscard]] std::string serialize_html(node_id target, bool outer,`

The optional two parameters are the "serializable shadow roots" set of
HTML fragment serialisation (`getHTML({serializableShadowRoots, shadowRoots})`):
an element's shadow root is written out as a `<template shadowrootmode>`
before the element's own children when the root's `serializable` is set
and `serializable_shadow_roots` is true, or when the root is in
`shadow_roots`. Both default off, so `innerHTML`/`outerHTML` are byte
identical to before.

<a id="contract-47"></a>

`[[nodiscard]] bool validate_and_extract_element(context & cx, std::string_view where,`

"Validate and extract" for an ELEMENT name, DOM 4.9, shared by
createElementNS and createDocument: false having thrown the
InvalidCharacterError or NamespaceError the pair earns.

<a id="contract-48"></a>

`flat_map<std::uint64_t, std::vector<std::pair<std::string, script::object_object *>>>`

ONE Attr OBJECT PER (element, namespace, local name), so that
`el.getAttributeNode("x") === el.attributes[0]` - an Attr is a node and
a node has an identity. Keyed by element.key(), then by the pair; rooted
by mark_roots like wrappers_; an entry goes when the attribute does.

<a id="contract-49"></a>

`[[nodiscard]] value parse_from_string(context & cx, std::string_view markup,`

`new DOMParser().parseFromString(markup, type)`, HTML 8.6.2: a SECOND
document - this document's HTML parser over `markup` for text/html, the
XML parser for the four XML types - as a real Document or XMLDocument in
the realm, so `createElement`, `documentElement.tagName` and the rest
answer as the type says. See document/second_document.cpp.

<a id="contract-50"></a>

`dom_bindings & adopt_second_document(context & cx, document & fresh);`

The bindings for a document this one made, linked and installed - the
half of make_html_document and make_xml_document they share.

<a id="contract-51"></a>

`[[nodiscard]] parse_result parse_document(std::string_view html);`

Parse `html` as this document's page. Every parser-inserted script has
run, the deferred ones after the tree, and readyState is "interactive"
when it returns; DOMContentLoaded and load are the caller's task. The
<svg> sources are for the rasteriser, as parse_html's are.

<a id="contract-52"></a>

`std::vector<parser_script> deferred_scripts_;`

The scripts that "will execute when the document has finished parsing"
(defer, and modules) and "as soon as possible" (async), 4.12.1.1.

<a id="contract-53"></a>

`void parser_finished(bool initial);`

"The end", 13.2.7, from the readiness change on: `initial` is the page
load, whose DOMContentLoaded/load task the browser's tick already is;
a script-created parser queues its own.
