# Shell Bindings Resources contracts

<a id="contract-1"></a>

`bool frames_dirty_ = true;`

Set by `mutated()` and by the first tick after a parse. Without it the
reconcile walks the whole tree on every frame of an idle page, which is
exactly what "a frame runs only what changed" forbids.

<a id="contract-2"></a>

`[[nodiscard]] static std::string_view mime_for_path(std::string_view path);`

The content type a path implies, since there is no server here to send
one. Empty for a name this engine has no type for.

<a id="contract-3"></a>

`void settle_image(context & cx, const pending_image & waiting);`

Resolve the bytes, set `complete`, and announce it - `onload` and any
`load` listener, or the error pair.

<a id="contract-4"></a>

`void install_image_views(context & cx, script::object_object & obj, node_id id);`

The loading surface an <img> has beyond a plain element: src, the size
that falls back to the decoded pixels, complete, decode().

<a id="contract-5"></a>

`void settle_fetch(context & cx, const pending_fetch & waiting);`

Do the work for one queued fetch and settle its promise. Called from the
event loop, not from fetch().

<a id="contract-6"></a>

`void install_xhr(context & cx);`

`XMLHttpRequest`, XHR Standard §4 - bindings/xhr.cpp. AFTER the event
interfaces (it is an EventTarget) and install_dom_exception.

<a id="contract-7"></a>

`[[nodiscard]] value make_response(context & cx, const std::string & url, int status,`

The Response object.

The BODY methods hand back settled promises: the bytes are already in
hand by the time a Response exists, so there is nothing to wait for. It is
the fetch itself that is asynchronous, which is the part a page can
observe.

<a id="contract-8"></a>

`[[nodiscard]] value make_event_object(context & cx, std::string_view type, bool bubbles,`

The shared Event builder: everything `new Event`, `document.createEvent`
and the engine's own input events have in common. `bubbles` and
`cancelable` are the two flags that change what dispatch does.

<a id="contract-9"></a>

`[[nodiscard]] bool default_passive_value(std::string_view type, const path_step & target);`

WHAT `passive` MEANS WHEN THE PAGE DID NOT SAY -
https://dom.spec.whatwg.org/#default-passive-value. The member has no
default in the IDL: a listener for one of the four SCROLL-BLOCKING types
registered on the window, the document, the document element or the body
is passive unless the page asked for otherwise, and passive everywhere
else means only what was asked for. It is not a hint - the canceled flag
is not set while such a listener runs - so getting it wrong makes
`preventDefault` work where it must not.

<a id="contract-10"></a>

`bool dispatch_error_value(std::string_view message, value error,`

The `error` event a faulting callback produces, carrying the VALUE the
throw left behind beside its text. `dispatch_error` is this with no value,
which is what a fault that was never an exception has to hand a page.

<a id="contract-11"></a>

`[[nodiscard]] value make_mouse_event(context & cx, std::string_view type, node_id target,`

The MouseEvent (or PointerEvent) the engine sends for one input event:
the coordinates, the button and the modifiers, on the right prototype so
dispatch can tell it from a plain `new Event("click")`.

<a id="contract-12"></a>

`[[nodiscard]] bool has_activation_behavior(const read_txn & txn, node_id node) const;`

DOM 2.9 dispatch, the activation half. Which node on the path has
activation behaviour - HTML's list: <a href>, <area href>, <button>,
<input>, <label>, <summary> - and what happens at it after the listeners
ran. See the definitions in events/dispatch.cpp.

<a id="contract-13"></a>

`void install_event_interfaces(context & cx);`

`Event`, `CustomEvent` and `EventTarget` as globals, and the prototype an
event object is linked to so `instanceof` and the phase constants work.

<a id="contract-14"></a>

`bool dispatch_to(value event, path_step at);`

THE DISPATCH ALGORITHM, over a path rather than over a node chain. See the
definition: capture from the window down, then bubble back up, with
`currentTarget` and `eventPhase` set for each step and the propagation
flags checked between them. Returns whether a listener cancelled it.

<a id="contract-15"></a>

`[[nodiscard]] std::vector<path_step> propagation_path(path_step at,`

Where an event aimed at `at` travels: the node and its ancestors, then the
document, then the window - innermost first. A `composed` event crosses
each shadow boundary to the host; one that is not stops at the shadow
root. A detached tree ends at its own root and reaches neither the
document nor the window.

<a id="contract-16"></a>

`[[nodiscard]] value object_of_step(context & cx, path_step step);`

The JavaScript object for one step, which is what `currentTarget` reports
and what an `on<type>` handler property is looked up on.

<a id="contract-17"></a>

`[[nodiscard]] path_step step_of(value self);`

AND BACK AGAIN: which event target a value IS. `object_of_step` is the
other direction and the two have to agree. It exists because
EventTarget.prototype's three methods are INHERITED by every node
wrapper - the interface chain ends at EventTarget - so `this` inside
`addEventListener` is as often a node as it is a standalone target, and
reading it as standalone gives an element a listener list no dispatch
through the tree ever visits.

<a id="contract-18"></a>

`bool fire_handler_property(value target, std::string_view type, value event,`

`onclick`, `onload` - the handler PROPERTY, run after the listeners. True
when it threw, with the thrown value written through `thrown` if a caller
asked for it; a caller that does not is one with nowhere to report to.

<a id="contract-19"></a>

`[[nodiscard]] std::vector<node_id> all_by_class(node_id root,`

Every element below `root` whose class attribute holds every one of
`tokens`, in document order. An EMPTY `root` means the whole document and
includes the document's own root element - which is `<html>` here, this
tree builder having no Document node above it. A given `root` is an
element and is excluded, a search being over descendants. An empty
`tokens` matches nothing, which is what the ordered set parser leaves
behind for an all-whitespace argument and what the DOM says the answer is.

<a id="contract-20"></a>

`[[nodiscard]] std::vector<node_id> all_by_name(std::string_view name);`

Every HTML element in the document whose `name` attribute is exactly
`name`, in document order. HTML only: `document.getElementsByName` is an
HTML method and an SVG element carrying `name=` is not one of its answers.

<a id="contract-21"></a>

`[[nodiscard]] node_id first_html_element(std::string_view local);`

--- THE HTML TREE ACCESSORS (bindings/document/collections.cpp) ------

`find_by_tag` matches on the TAG ATOM, and that is the wrong question for
anything HTML defines: `<title>` inside `<svg>` interns to the same atom
as the document's own, and the tokenizer keeps foreign content's case so
an SVG `<clipPath>` is a different atom from an HTML one. Everything HTML
names - the title element, the head, `document.images` - is a LOCAL NAME
in the HTML NAMESPACE, so these two ask that instead.

The local name is what follows the first colon, because
`createElementNS(HTML, "blah:title")` really is a title element: DOM
"validate and extract" puts the prefix before the colon and the local
name after it, and HTML's definitions are all in terms of the latter.

<a id="contract-22"></a>

`[[nodiscard]] node_id title_element();`

"THE TITLE ELEMENT", which is not simply the first `<title>`: in a
document whose root is an SVG `<svg>` it is that root's first SVG
`<title>` CHILD, and in every other document it is the first HTML title
element anywhere in tree order. Empty when there is none.

<a id="contract-23"></a>

`[[nodiscard]] node_id body_element();`

"The body element": the first child of the DOCUMENT ELEMENT that is a
`body` or a `frameset`. Not the first `<body>` anywhere.

<a id="contract-24"></a>

`[[nodiscard]] value make_html_document(context & cx, const std::string * title);`

--- A SECOND DOCUMENT (bindings/document/second_document.cpp) ---------

`createHTMLDocument` and `createDocument` return one: a SECOND
dom_bindings over its own tree, in the same realm. Every per-node key -
`wrappers_`, `namespaces_` - is a member, so a second instance has a
second set of them. What it shares with the primary is the atom table,
the script context, and the INTERFACE OBJECTS, so that
`otherDoc.createElement("div") instanceof HTMLDivElement` is true against
the one `HTMLDivElement` a page can see.

`importNode` and `adoptNode` do not cross between two documents; a node
of one passed to the other is REFUSED rather than misread - see
`handle_of`, which checks that the wrapper it was given is one of ours.

<a id="contract-25"></a>

`[[nodiscard]] value make_xml_document(context & cx, std::string_view ns,`

`as_xml_document` picks the interface: `createDocument` returns an
XMLDocument and `new Document()` a plain Document, and the two differ in
nothing else - DOM 4.5.1 and 4.5 respectively.

<a id="contract-26"></a>

`[[nodiscard]] dom_bindings * owner_of(value v);`

WHICH BINDINGS A WRAPPER BELONGS TO: this one, the primary, or one of the
primary's other secondaries. Null for anything that is not a node of any
document in the realm. `handle_of` answers only for this one's own
wrappers, which is the refusal `importNode` has to get past.

<a id="contract-27"></a>

`[[nodiscard]] bool is_a_document(value v) const;`

Is this the `document` of ANY bindings in the realm? `is_the_document`
is identity against this one's; a Document is refused by importNode and
adoptNode whichever document it is.

<a id="contract-28"></a>

`void mark_roots(const script::context::root_visitor & mark) const;`

The realm has ONE external-roots callback - `set_external_roots` replaces
rather than appends - so the primary's walks itself and then every
secondary. A secondary never registers.

<a id="contract-29"></a>

`void adopt_interfaces_of(const dom_bindings & primary);`

Take the primary's interface prototypes rather than building a second set
of globals: `install_dom_interfaces` DEFINES `HTMLDivElement` and its
ninety neighbours, and running it twice would leave two of each and break
every `instanceof` taken across the two documents.

<a id="contract-30"></a>

`void install_tree_accessors(context & cx, script::object_object & doc);`

`document.title`, `document.images` and the seven collections beside it,
all as ACCESSORS - see the definition for why not one of them can be a
property refreshed on the tick.

<a id="contract-31"></a>

`[[nodiscard]] std::vector<node_id> named_document_items(std::string_view name);`

HTML's "named access on the Document object" - the named elements with a
given name, in tree order. `embed`, `form`, `iframe`, `img` and `object`
by their `name`; `object` by its `id`; and `img` by its `id` ONLY when it
also carries a non-empty `name`, which is the asymmetry
`nameditem-01.html` tests by removing one attribute at a time.

<a id="contract-32"></a>

`[[nodiscard]] std::vector<std::string> document_property_names();`

The other direction: every name the rule above answers to, once each, in
tree order - the document's "supported property names".

<a id="contract-33"></a>

`[[nodiscard]] value make_document_proxy(context & cx, value target);`

The Proxy a page sees as `document`. Installs the `get`, `has`, `ownKeys`
and `getOwnPropertyDescriptor` traps over `document_target_` and returns
it.

<a id="contract-34"></a>

`[[nodiscard]] value make_live_collection(context & cx,`

A COLLECTION THAT IS LIVE, which is the whole difficulty. `getElementsBy*`
returns a view of the document rather than a snapshot of it: a page takes
the collection, appends an element, and reads `length` again expecting the
new number. An array cannot answer that, so this is a Proxy whose `get`
and `has` traps re-run `members` on every read - the same mechanism the
`window` proxy already uses, and the reason a second one is cheap.

WHICH INTERFACE it claims to be is a parameter, because two of the DOM's
live collections are the same object with different names on it:
`getElementsByTagName` is an HTMLCollection and `getElementsByName` is a
NodeList, and `document.getElementsByName-liveness.html` asserts
`e instanceof NodeList` before it checks a single length.

<a id="contract-35"></a>

`[[nodiscard]] std::vector<node_id> query(std::string_view selector, node_id within = node_id{},`

`querySelectorAll`, on the real Selectors engine - see the definition.

`invalid` comes back true when the text is not a selector at all, which is
the only case `querySelector` may throw SyntaxError for. It is an out
parameter rather than a throw so that this stays callable from a place that
has no context, and defaulted so the four call sites that predate it do not
have to care.

<a id="contract-36"></a>

`flat_map<std::uint64_t, std::string> namespaces_;`

WHERE AN ARBITRARY NAMESPACE URI LIVES. `node` carries a three-valued
`node_ns` and not a URI, for the size reason written down beside the
enumerator; the handful of elements a page creates with createElementNS in
a namespace that is neither HTML nor SVG keep their URI here, keyed the
same way a wrapper is.

<a id="contract-37"></a>

`flat_map<std::uint64_t, std::vector<node_id>> manual_slots_;`

`slot.assign(...nodes)`, keyed by the SLOT: a shadow tree whose
slotAssignment is "manual" assigns nothing by name, so the only
assignment it has is the one a page made. What is stored is what the
page passed; whether a node still qualifies - a child of the host, an
element or a text node - is decided when the list is read, so moving a
node out of the host un-assigns it without a hook.

<a id="contract-38"></a>

`flat_map<std::uint64_t, std::vector<node_id>> slot_assignments_;`

THE SLOTCHANGE SIGNALS, DOM 4.2.2.4. An assignment is computed, not
stored (shadow_dom.cpp), so a change is found by diffing: `mutated()`
recomputes every slot's assigned nodes and a slot whose list moved is
"signalled" - queued for a `slotchange` at the next mutation observer
microtask, after the observers' callbacks (DOM 4.3.3 step 5), even if
it has left its tree since. ponytail: every slot of every shadow tree
is recomputed per mutation; a per-host dirty bit if a page carries
thousands of slots.

<a id="contract-39"></a>

`flat_map<std::uint64_t, std::pair<std::string, std::string>> nonce_slots_;`

[[CryptographicNonce]], HTML 2.6.1: what `el.nonce = x` wrote, paired with
the `nonce` attribute's text at the time - see reflection.cpp's
`cryptographic_nonce` for why the pair. Empty until a page assigns one.

<a id="contract-40"></a>

`std::size_t dispatch_depth_ = 0;`

How many dispatches are on the stack. A listener may dispatch, and the
inner dispatch must not compact the listener list the outer one is walking.

<a id="contract-41"></a>

`value dom_exception_prototype_;`

DOMException.prototype, held here as well as on the global for the reason
blob_prototype_ is: a page can delete a global, and an exception whose
prototype was collected stops being a DOMException.

<a id="contract-42"></a>

`value css_interface_;`

The `CSS` namespace object, held for the reason above: a page can delete
the global and `CSS.supports` must still be the same function afterwards.

<a id="contract-43"></a>

`value document_;`

THE DOCUMENT IS TWO VALUES, and which one a caller wants is not a detail.

`document_` is what a PAGE holds: a Proxy, because HTML's named access
(`document.someImgName`) has to answer for a name nobody ever defined as
a property and has to STOP answering the moment the attribute behind it
is removed. It is therefore what `ownerDocument`, `getRootNode` and every
identity comparison must use, or a page's `document` and the engine's
are two different objects.

`document_target_` is the object BEHIND it, which is where every property
the bindings install actually lives. `document_object()` returns it, so
everything that writes a property on the document keeps working
unchanged; the proxy falls through to it for every name that is not a
named element.

<a id="contract-44"></a>

`std::vector<std::unique_ptr<document>> owned_documents_;`

A DOCUMENT THIS ONE MADE, and the bindings over it. Only ever non-empty
on the primary; a secondary makes no further documents because
`document.implementation` is not installed on one.

<a id="contract-45"></a>

`bool secondary_ = false;`

Is this the bindings for a document a page MADE? It changes three things
and nothing else: no `document` global, no `location`/`defaultView`, and
no `document.implementation` - a document from createHTMLDocument has a
null browsing context, so all three are what the DOM already says.

<a id="contract-46"></a>

`dom_bindings * primary_ = nullptr;`

The bindings that made this one, for a secondary; null on the primary.
What `owner_of` walks up through to find the other documents.

<a id="contract-47"></a>

`std::string content_type_;`

--- XML documents ---
`document.contentType`, WHEN IT IS NOT DERIVABLE. A parsed document
answers from `document::xml()` and needs nothing here; `createDocument`
does not, because DOM 4.5.1 makes the string depend on the NAMESPACE it
was given - "application/xml", "application/xhtml+xml" or
"image/svg+xml" - and the namespace is an argument that is gone by the
time the property is installed. Empty means "derive it".

<a id="contract-48"></a>

`std::uint32_t next_object_url_ = 0;`

Counts the object URLs handed out, so each is distinct. Counted rather
than random for the same reason Math.random is seeded: a page that prints
one could not otherwise have a golden.

<a id="contract-49"></a>

`std::vector<std::pair<std::string, std::string>> object_url_types_;`

The media type each object URL was made with (File API §10.3: a
`blob:` response carries the Blob's `type`), because the asset
registry stores bytes only and `mime_for_path` has no extension to go
on. Both live on the PRIMARY: a frame's `URL.createObjectURL` hands out
a name from the same series, and the frame loader asks the same table.

<a id="contract-50"></a>

`value blob_prototype_;`

Blob.prototype, kept so canvas.toBlob's Blob is one too - `x instanceof
Blob` has to be true whoever made it.

<a id="contract-51"></a>

`[[nodiscard]] value make_blob(context & cx, value bytes, std::string_view type);`

A Blob: its size, its type and its bytes (a u8 array, see
make_u8_array), on that prototype. Enough for a page that hands one to
URL.createObjectURL or a FileReader, which is everything done with one.

<a id="contract-52"></a>

`value event_prototype_;`

THE CONTEXT INTERFACE OBJECTS - `window.CanvasRenderingContext2D` and
friends; the element interfaces live in the table (interface_prototypes_).

A browser exposes one per interface, and libraries use them two ways that
both have to work: feature detection (`!!window.CanvasRenderingContext2D`)
and identity (`ctx instanceof CanvasRenderingContext2D`). A bare marker
object satisfies the first and makes the second silently FALSE.

So each carries a real `prototype`, and the objects that are instances get
that prototype linked. See interface_prototype().
`Event.prototype` and `CustomEvent.prototype`. Every event object this
engine makes is linked to one, which is what carries `e.constructor`,
`e instanceof Event` and the four phase constants a page reads as
`e.AT_TARGET` rather than as `Event.AT_TARGET`.

<a id="contract-53"></a>

`value mouse_event_prototype_;`

`MouseEvent.prototype` and `PointerEvent.prototype`: what an engine mouse
event is linked to, and what dispatch checks a `click` against before
running activation behaviour - a `new Event("click")` toggles nothing.

<a id="contract-54"></a>

`value event_target_prototype_;`

`EventTarget.prototype`, where the three methods a standalone target
inherits live.

<a id="contract-55"></a>

`value webgl2_prototype_;`

A SEPARATE INTERFACE, not a subclass. `WebGL2RenderingContext` does not
inherit from `WebGLRenderingContext` in the specification, so a real
WebGL 2 context is NOT `instanceof WebGLRenderingContext` - and a page
that tests for one to decide which path to take (Phaser does) must get
the same answer here as it would in a browser.

<a id="contract-56"></a>

`std::vector<value> pending_media_changes_;`

The lists whose `matches` flipped since the last report, GC roots
until their `change` events go out.

<a id="contract-57"></a>

`double now_ms_ = 1;`

A POSITIVE TIME ORIGIN, not zero. `performance.now()` and every event's
`timeStamp` read this, and `dom/events/Event-constructors.any.js` asserts
`timeStamp > 0` twice - which is the only thing between that file and a
pass. Zero is also not what a browser reports: the origin is when the
document began loading and script runs after that, so a page reading
`performance.now()` on its first line sees a small positive number
everywhere else.

A FIXED number rather than a real one, for the reason `Math.random` is
seeded: three example pages byte-compare their render against a golden,
and a clock that differs run to run cannot have one.
