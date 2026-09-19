# Shell Bindings Lifecycle contracts

<a id="contract-1"></a>

`~dom_bindings();`

OUT OF LINE, because `secondary_documents_` below is a vector of
unique_ptr to THIS class and the deleter has to be instantiated where the
class is complete.

<a id="contract-2"></a>

`void observe_resources(asset_registry & assets, image_store & images);`

Where loadImage() and fetch() look. Both are owned by the browser, which
hands them over before scripts run; without them the page still runs and
every load simply fails.

<a id="contract-3"></a>

`void observe_location(std::string href, std::string hash);`

What `location` reports. The browser sets it; the page can only read it,
because assigning to location.href is a navigation and the engine has none.

<a id="contract-4"></a>

`void observe_focus(node_id id);`

Where focus is now, so `document.activeElement` can answer. The focus
hook below goes the other way - script setting focus - and left the
bindings with no way to READ it.

<a id="contract-5"></a>

`void set_alert_hook(std::function<void(const std::string &)> hook);`

Where alert() goes. The browser records them, because a reload replaces
these bindings and the alert that caused it must survive that.

<a id="contract-6"></a>

`bool refresh_wrappers();`

Returns whether a page write reached a control, so the browser knows the
paint is stale without having to diff the form store.

<a id="contract-7"></a>

`void register_roots(context & cx);`

Everything this holds that the VM cannot see. Registered with the context
so a collection does not free a page's own listeners out from under it.

<a id="contract-8"></a>

`bool dispatch(std::string_view type, node_id target);`

Fire `type` at `target` and at every ancestor, like a bubbling DOM event.
Returns whether a listener called preventDefault.

<a id="contract-9"></a>

`bool dispatch_key(std::string_view type, node_id target, const input_event & input);`

A KeyboardEvent. `code` is the physical key and `key` is what it means -
pages read both, and an event object carrying neither is why a page could
register a keydown listener and never learn which key was pressed.

<a id="contract-10"></a>

`bool dispatch_error(std::string_view message);`

AN UNCAUGHT EXCEPTION, ANNOUNCED to `window.onerror` and
`addEventListener("error", ...)` - which is how testharness.js tells "this
test threw during load" from "this test never finished". The event carries
`message`, `filename`, `lineno`, `colno` and `error` because those are the
five properties that handler reads; only `message` is real here, and the
rest say so by being empty rather than by being absent.

<a id="contract-11"></a>

`bool dispatch_error(std::string_view message, std::string_view script_src);`

...naming the script that failed: an external script's URL, resolved
against the document's, is the ErrorEvent's `filename`; empty means the
document's own (an inline script).

<a id="contract-12"></a>

`bool dispatch_mouse(std::string_view type, node_id target, const input_event & input);`

A MouseEvent. clientX/clientY are viewport coordinates, which is what
MDN's breakout reads to move its paddle.

<a id="contract-13"></a>

`bool dispatch_wheel(node_id target, const input_event & input);`

A `wheel` event, and whether the page CONSUMED it - a page that calls
preventDefault means the document must not scroll as well. See the
definition for the sign and the units, both of which are easy to invert.

<a id="contract-14"></a>

`bool click(node_id target);`

`element.click()`, HTML 3.2.6: nothing for a disabled form control, else
a synthetic, untrusted MouseEvent `click` - bubbles, cancelable, composed
- sent down the one dispatch path an engine click takes, activation
behaviour included. The browser calls it for a <label>, whose activation
behaviour IS a click at the control it labels. Returns whether cancelled.

<a id="contract-15"></a>

`[[nodiscard]] bool is_connected(node_id target) const;`

"Connected", DOM 4.2.1: the shadow-including root is the document. A
checkbox fires `input` and `change` only when it is, and a form submits
only when it is.

<a id="contract-16"></a>

`std::size_t run_due_callbacks();`

Run the timers that are due, then the animation callbacks. Returns how
many ran, so an event loop can tell whether it needs another frame.

<a id="contract-17"></a>

`void reconcile_frames();`

--- NESTED BROWSING CONTEXTS (bindings/frames.cpp) --------------------

Bring the `<iframe>`s in this document into step with the tree: load the
ones that appeared, reload the ones whose `src` changed, forget the ones
that went away. Called from the browser's tick BEFORE the window's `load`
event, because a page's `load` handler is where WPT reads
`frame.contentDocument` and it must already be there.

A FRAME IS A SECOND DOCUMENT, which is a model this class already has -
see `make_html_document`. What a frame adds to it is the bytes: the src
is resolved through the asset registry, so a frame loads from wherever
the page's other subresources load from and reaches no socket of its own.

<a id="contract-18"></a>

`bool navigate_form_target(node_id form,`

A form submission (GET) aimed at a frame this document names by
`target`: the frame navigates to the action with the entries as its
query. False when the submission is not one of those - see frames.cpp.

<a id="contract-19"></a>

`std::function<void(node_id form, node_id submitter)> submit_form_;`

HTML 4.10.21.3 from the browser's activation: the entry list (with its
`formdata` event), then the navigation above. control_methods.cpp
installs it.

<a id="contract-20"></a>

`void record_first_paint();`

Paint Timing: `first-paint` and `first-contentful-paint`, at the page
clock's current reading, once. The browser calls it from its first frame.

<a id="contract-21"></a>

`[[nodiscard]] double next_callback_ms() const;`

When the next callback is due, in milliseconds from now. Infinity when
there is none - which is what lets an idle application block rather than
poll. An animation frame is due IMMEDIATELY: a page that asked for one
wants the next frame, not a timer's worth of delay.

<a id="contract-22"></a>

`[[nodiscard]] std::vector<std::string> unforwarded_gl_calls() const;`

Every GL call a page made that the ANGLE path does not forward yet,
gathered from all its contexts. EMPTY is the claim a test makes; a
backend that silently dropped calls would paint something plausible.

<a id="contract-23"></a>

`[[nodiscard]] value wrap(context & cx, node_id id);`

ONE WRAPPER PER ELEMENT, cached: `getElementById('x') ===
getElementById('x')`, and a wrapper a page holds on to keeps answering
about the live element rather than about a snapshot.

<a id="contract-24"></a>

`[[nodiscard]] static node_id unpack(std::uint64_t bits);`

The inverse of handle::key(), which is how a wrapper's `__node` number
and every per-node table here spell a node.

<a id="contract-25"></a>

`[[nodiscard]] node_id receiver(context & cx);`

The element a native was called on. Returns an empty handle when the
receiver is not a wrapper - which a native must treat as "do nothing"
rather than as "the document root".

<a id="contract-26"></a>

`void refresh_element(context & cx, script::object_object & obj, node_id id);`

Live-ish properties. Refreshed when a wrapper is made and after layout,
which is what `element.offsetWidth` actually needs to be useful.

<a id="contract-27"></a>

`[[nodiscard]] located locate(node_id id, bool scrolled = false) const;`

`scrolled` subtracts every scroll offset above the box - the viewport's
(unless a fixed box is on the way) and each scrolled container's - which
is what getBoundingClientRect answers in; offsetTop and its kin read
the layout position and leave it false.

<a id="contract-28"></a>

`[[nodiscard]] bool is_viewport_element(node_id id, bool scrolling);`

Whether `id` is the element whose client rectangle and scrolling area
are the VIEWPORT's (CSSOM View §7): the root element in a no-quirks
document, the body in a quirks one - and, for the scrolling area, only
a body that is not potentially scrollable.

<a id="contract-29"></a>

`[[nodiscard]] bool potentially_scrollable(node_id body) const;`

"Potentially scrollable" (§2): the body has a box and neither it nor
its parent is `overflow: visible`/`clip` on the axis.

<a id="contract-30"></a>

`[[nodiscard]] point viewport_scroll() const;`

--- SCROLLING (bindings/element/views.cpp, bindings/window/scrolling.cpp)

THE SCROLL STATE LIVES HERE. A scroll container's offset is a fact about
the element the page reads and writes through this object model, and a
frame's document has no browser behind it at all - so the offsets are
the bindings', keyed by node, and every geometry read subtracts them
(locate). The VIEWPORT's offset is the browser's for the page - the
wheel and `window.scrollTo` must agree - reached through the two hooks
below, and this object's own for a frame, where nothing else scrolls.
A scroll queues a `scroll` event for the next tick, as "run the scroll
steps" does. What is NOT here is paint: a scrolled container's content
is drawn where layout put it (the recorder does not read these offsets).

<a id="contract-31"></a>

`void scroll_viewport_to(double x, double y);`

"Perform a scroll of the viewport" (§3.1): clamped to the viewport's
scrolling area, a `scroll` event at the document when it moved.

<a id="contract-32"></a>

`void scroll_element_to(node_id id, double x, double y);`

"Scroll an element to x, y" (§6): nothing for a box that is not a scroll
container, else clamped to its scrolling area, with a `scroll` event at
the element when it moved.

<a id="contract-33"></a>

`void set_scroll_position(node_id id, char axis, double v);`

The scrollTop/scrollLeft setters' whole algorithm, root and quirks-body
delegation to the window included; `axis` is 'x' or 'y'.

<a id="contract-34"></a>

`void scroll_into_view(node_id id, std::string_view block, std::string_view inline_,`

"Scroll a target into view" (§6.1) over every scroll container above
the element and then the viewport. `block` and `inline_` are "start",
"center", "end" or "nearest"; `nearest_container` stops at the first
scrolling box (the `container` option).

<a id="contract-35"></a>

`[[nodiscard]] node_id scrolling_element();`

§5's scrollingElement: the body in quirks mode when it is not
potentially scrollable, the root otherwise, empty for null.

<a id="contract-36"></a>

`void install_style_accessor(context & cx);`

`style` on the HTMLElement/SVGElement/MathMLElement prototypes, the
declaration proxy built on first read; make_style_view is one
element's. element/views.cpp.

<a id="contract-37"></a>

`[[nodiscard]] std::vector<node_id> elements_from_point(double x, double y, bool all);`

§5's hit test over the fragment tree, topmost first: every element
whose border box is under the viewport point, painted-last first, the
root last. `all` false stops at the first.

<a id="contract-38"></a>

`[[nodiscard]] std::optional<box_geometry> box_geometry_of(value node, std::string_view box);`

GeometryUtils (§10) and the geometry interfaces it answers in. A box of
the node in viewport coordinates - "margin", "border", "padding" or
"content" - or nothing when it has none; a Document names the viewport.

<a id="contract-39"></a>

`void define_operation(context & cx, std::initializer_list<const char *> interfaces,`

THE IDL OPERATIONS, ON THE INTERFACE PROTOTYPES - one native per realm,
not one per wrapper. `Node.prototype.appendChild.call(x, y)`,
`"insertBefore" in Node.prototype` and `.length` on each are what the
corpus asks; a wrapper carrying eighty own natives answered none of them.

Every operation is recorded in `operations_` by the instance that built
it, and the native on the prototype - installed by the PRIMARY bindings
only - is a trampoline that asks `owner_of(this)` which instance's copy
to run: a second Document shares the realm's prototypes (see
adopt_interfaces_of) and its nodes must edit ITS tree, not the primary's.
`interfaces` are the interface names whose prototypes get the native -
one for a Node operation, three for a ParentNode mixin - and `length` is
the WebIDL argument count.

<a id="contract-40"></a>

`void install_operations(context & cx);`

The three files the operations live in - see lib/Shell/bindings/element/.
Called from install_operations only.

<a id="contract-41"></a>

`[[nodiscard]] listener make_listener(context & cx, path_step target, std::span<value> args);`

One reading of addEventListener's third argument, shared by the element,
document and window registrations - three copies is three chances for
`once` to work on one of them and not the others.

<a id="contract-42"></a>

`void add_listener(listener made);`

Register one, UNLESS AN EQUAL ONE IS ALREADY THERE. The DOM defines a
listener's identity as (type, callback, capture) on one target and says a
second addEventListener with all three the same does nothing - which is
what a page relies on when it registers defensively in a function it calls
more than once. Every addEventListener goes through here so the rule holds
for elements, the document, the window and a standalone EventTarget alike.

<a id="contract-43"></a>

`void reap_spent_listeners();`

Drop the listeners a `once` fired, but ONLY when no dispatch is running:
erasing from the vector a dispatch is indexing is how the listener after
the removed one gets skipped, and a listener may dispatch another event.

<a id="contract-44"></a>

`void set_inner_html(node_id target, std::string_view markup);`

`innerHTML`. Setting one PARSES: the markup becomes real nodes under the
element, replacing whatever was there.

<a id="contract-45"></a>

`node_id copy_subtree(const read_txn & from, node_id node, node_id parent);`

One node and its subtree, copied from another document into this one.
The scratch document a fragment is parsed into shares this atom table, so
a tag or attribute name needs no remapping.

<a id="contract-46"></a>

`node_id clone_node(const read_txn & from, node_id source, bool deep,`

`cloneNode(deep)`: a DETACHED copy, which is what makes it different from
copy_subtree above - that one exists to move a parsed fragment into this
document and needs somewhere to put it.
`owner` is the bindings the source node belongs to when it is not this
one - `importNode` reads another document's tree - and null otherwise.

<a id="contract-47"></a>

`bool insert_node(node_id parent, node_id child, node_id before);`

Insert `child` into `parent`, before `before` or at the end when `before`
is empty, FLATTENING a DocumentFragment: inserting one moves its children
and leaves the fragment itself empty and parentless. Every insertion
method goes through here, because a fragment is legal at every one of them
and handling it at four call sites is three chances to forget.

<a id="contract-48"></a>

`[[nodiscard]] bool pre_insert_valid(context & cx, node_id parent, node_id child, value node_arg,`

THE "ENSURE PRE-INSERTION VALIDITY" STEPS, DOM 4.2.3, shared by
`insertBefore`, `appendChild` and `moveBefore`. Answers false having
ALREADY THROWN, so a caller is one `if` rather than an error channel.

<a id="contract-49"></a>

`[[nodiscard]] node_id node_from(context & cx, value v, bool whole_fragment = false);`

One argument of append/prepend/before/after/replaceWith, as a node. A
wrapper resolves to its node; ANYTHING ELSE becomes a Text node, which is
what makes `el.append("hello")` work and is the whole reason those methods
are nicer than appendChild.
Another document's node is ADOPTED - cloned into this slab, its wrapper
rebound - except a fragment, whose children come and which itself stays
where it was, as insertion has it; `adoptNode` asks for the fragment too
with `whole_fragment`.

<a id="contract-50"></a>

`[[nodiscard]] node_id convert_nodes(context & cx, std::span<value> args);`

"Convert nodes into a node", DOM 4.2.5: the arguments of one of those
methods as ONE node - the node itself for one argument, a fragment holding
them all (which MOVES each out of the tree) for several.

<a id="contract-51"></a>

`[[nodiscard]] node_id viable_sibling(node_id self, std::span<value> args, bool forward);`

"Viable next/previous sibling", DOM 4.2.7: the first sibling of `self` in
that direction that is not one of `args`. Empty when there is none.

<a id="contract-52"></a>

`[[nodiscard]] std::string namespace_of(node_id id) const;`

THE EXACT NAMESPACE OF AN ELEMENT, as a string. Derived from `element_ns`
for everything the parser built - there are only two answers there - and
read from `namespaces_` for an element `createElementNS` put in some other
one. Empty means the null namespace, which reports as `null`.

<a id="contract-53"></a>

`void install_element_views(context & cx, script::object_object & obj, node_id id);`

`element.style` and `element.classList` - the two views onto an element
that are OBJECTS rather than values, so unlike everything in
refresh_element they are built once and keep their identity. A page holds
on to `el.style` and writes through it later, which a fresh object every
sync would silently discard.

They take the id directly because their methods are not called with the
element as `this`: `el.classList.add(...)` has the CLASS LIST as the
receiver, so `receiver(cx)` finds no handle.

<a id="contract-54"></a>

`[[nodiscard]] value computed_style_object(context & cx, node_id id, atom pseudo);`

...for `getComputedStyle(el, "::before")`: `pseudo` is the pseudo-element's
name (`before`/`after`), resolved through the style engine on every read.

<a id="contract-55"></a>

`[[nodiscard]] std::vector<std::pair<std::string, std::string>> computed_style_entries(`

ONE ELEMENT'S WHOLE COMPUTED STYLE, as (CSS name, value) pairs: the
supported longhands lexicographically, then the shorthands, then whatever
the element declared that the property table has never heard of. Empty for
an element that is not in the document.

SEPARATE FROM THE OBJECT because the object is LIVE: CSSOM says
getComputedStyle returns a live CSSStyleDeclaration, so every property on
it is an accessor that calls this again rather than a string captured when
the object was made. It holds raw pointers into the box and fragment trees
for the length of the call and never past it - the next layout frees them,
and that now happens inside a script turn.

<a id="contract-56"></a>

`[[nodiscard]] std::vector<std::pair<std::string, std::string>> computed_style_entries(`

...and of the element's `::before`/`::after`: `pseudo` is the style
engine::resolve_pseudo made for it, read in the element's place - no box,
percentages against the element's content box, the element as parent.

<a id="contract-57"></a>

`void install_dom_exception(context & cx);`

--- DOMException, and the CSS interface --------------------------------

`bindings/exceptions.cpp` owns the exception hierarchy every DOM method
throws through, `bindings/css.cpp` the `CSS` namespace object.

<a id="contract-58"></a>

`void install_abort(context & cx);`

`AbortController` and `AbortSignal`, DOM §3.2 - bindings/abort.cpp. A
signal is an EventTarget; aborting one removes every listener added
with it, fires `abort`, and reaches the signals `AbortSignal.any` made
from it. AFTER install_event_interfaces and install_dom_exception.

<a id="contract-59"></a>

`void install_autocomplete(context & cx);`

The `autocomplete` IDL attribute of input/select/textarea, HTML
4.10.18.7.1 - element/autocomplete.cpp. AFTER the interface table.

<a id="contract-60"></a>

`void install_media_queries(context & cx);`

`matchMedia` and `MediaQueryList`, CSSOM View §4.2 - bindings/
media_queries.cpp. A list is an EventTarget whose `matches` is live
against ITS document's environment (a frame's list reads the frame's);
`report_media_query_changes` is §13's "evaluate media queries and
report changes", run by whoever changed the environment - the browser
on a resize, the frame layout when a frame's box moved - and it fires
`change` one tick later, as the scroll steps do. AFTER install_abort.

<a id="contract-61"></a>

`void install_promise_rejections(context & cx);`

HTML 8.1.7.x "unhandled promise rejections" - bindings/promise_rejections
.cpp. The VM's HostPromiseRejectionTracker (context::set_rejection_
tracker) feeds the about-to-be-notified list; a task fires
`unhandledrejection` (cancelable) at the window for each promise still
unhandled, and `rejectionhandled` for one handled after that. AFTER
install_event_interfaces (PromiseRejectionEvent).

<a id="contract-62"></a>

`[[nodiscard]] dom_bindings & owner_of_media_query_list(value list);`

Which bindings' document a list names - the page's, or a frame's - and
the list's media text evaluated against that document's environment.

<a id="contract-63"></a>

`[[nodiscard]] value make_dom_exception(context & cx, std::string_view name,`

A DOMException instance with the right `name`, `code` and `message`, on
`DOMException.prototype` - which is what `assert_throws_dom` checks and
what `context::throw_error` cannot build, because an engine-raised error
is an ECMAScript Error by construction.

<a id="contract-64"></a>

`void install_css_interface(context & cx);`

The `CSS` namespace object - `CSS.supports` and `CSS.escape`. Its answers
come from style/css/properties.hpp, so it can never disagree with
`el.style` about whether a value is valid.

<a id="contract-65"></a>

`void install_dom_interfaces(context & cx);`

BEGIN reflection (bindings/element/reflection.cpp)

THE INTERFACE OBJECTS, AND REFLECTION, WHICH ARE ONE THING.

`HTMLDivElement.prototype -> HTMLElement.prototype -> Element.prototype ->
Node.prototype -> EventTarget.prototype` is a real chain here, built once
per page from a static table in element/interfaces.cpp, and every wrapper is linked
into it by the tag it has. That is what `el instanceof HTMLBodyElement`
and `eventTarget.constructor.name` ask, and it is ALSO where the reflected
IDL attributes live: `id`, `href`, `disabled` and the ~270 others are
accessors on ONE prototype each rather than on every wrapper, which is
both what the specification says and what makes a table affordable. See
element/reflection.cpp for the table and for what each type does.

<a id="contract-66"></a>

`void ensure_dom_interfaces(context & cx);`

Build them if they are not built and the pieces they chain to exist yet.
Cheap after the first success; called from wrap() because the bindings'
install order puts the first element wrapper BEFORE `EventTarget` exists.

<a id="contract-67"></a>

`[[nodiscard]] value prototype_for_node(const read_txn & txn, node_id id) const;`

`HTMLCanvasElement.prototype` for a <canvas>, `Text.prototype` for a text
node, `Element.prototype` for something createElementNS put in a
namespace that is neither HTML nor SVG.

<a id="contract-68"></a>

`[[nodiscard]] value interface_prototype(std::string_view name) const;`

One interface's prototype by name - how a collection built in another
translation unit becomes `instanceof HTMLCollection`. Undefined before
the interfaces are built, and undefined for a name that is not one.

<a id="contract-69"></a>

`[[nodiscard]] value reflected_get(context & cx, const void * row);`

ONE reflected IDL attribute, read and written. The row is a pointer into
the static table in element/reflection.cpp and is `const void *` here for the reason
the third-party-header invariant exists: the row type is one file's
business, and putting it in this header would make every consumer of the
engine parse a 270-row table's declaration to get at `document`.

<a id="contract-70"></a>

`[[nodiscard]] atom attribute_key(const read_txn & txn, node_id id,`

--- attributes as nodes: Attr, and the NamedNodeMap over them ---------

A qualified name as THIS element would have stored it: lowercased for an
HTML element and left exactly as written for anything else. It is the
only difference between `getAttribute` and `getAttributeNS`, and it is
why `svg.setAttribute("viewBox", ...)` must not fold.

<a id="contract-71"></a>

`[[nodiscard]] value attribute_object(context & cx, node_id owner, const attribute & held);`

ONE Attr, LIVE: `value`, `nodeValue` and `textContent` read and write the
element's attribute rather than a string captured when it was made.

<a id="contract-72"></a>

`void install_dataset(context & cx, script::object_object & obj, node_id id);`

`element.dataset` - a DOMStringMap over the `data-*` attributes. Its own
function rather than more of install_element_views because it is
CONDITIONAL: only an HTML, SVG or MathML element has one.

<a id="contract-73"></a>

`[[nodiscard]] bool validate_and_extract(context & cx, std::string_view where,`

DOM 4.9 "validate and extract", shared by setAttributeNS and
setNamedItemNS. False HAVING ALREADY THROWN - InvalidCharacterError for a
name that is not a QName, NamespaceError for the four prefix rules.

<a id="contract-74"></a>

`std::vector<value> interface_prototypes_;`

Parallel to the static interface table in element/interfaces.cpp: one prototype per
row, in the same order, so a tag resolves to a prototype by index.

<a id="contract-75"></a>

`value interface_keeper_;`

EVERY PROTOTYPE, IN ONE ARRAY THE COLLECTOR CAN SEE. Each interface's
prototype is reachable from its constructor, which is a global - and a
page may delete a global, after which a prototype this object still
points at could be swept. So every constructor `retains` this one array,
and all 70-odd of them would have to be deleted before any prototype
became unreachable.

<a id="contract-76"></a>

`bool interfaces_linked_ = false;`

Set once the chain is built AND linked to `event_target_prototype_`,
which install_event_interfaces publishes after the first wrapper exists.

<a id="contract-77"></a>

`void install_character_data(context & cx);`

--- CharacterData ---

`CharacterData.prototype` AND `Text.prototype`, filled in once the
interface chain exists. On the PROTOTYPES rather than on every wrapper,
for the reason reflection is: `substringData` is one function per page
here and was one per text node in the shape install_element_methods uses.

Every offset in them is a UTF-16 CODE UNIT and this engine stores UTF-8 -
see the helpers above install_character_data in element/character_data.cpp, and the note
there on what a surrogate pair costs.

<a id="contract-78"></a>

`void install_hyperlink_utils(context & cx);`

HTMLHyperlinkElementUtils (HTML 4.6.3) on HTMLAnchorElement and
HTMLAreaElement - element/hyperlink.cpp.

<a id="contract-79"></a>

`[[nodiscard]] value construct_node_interface(context & cx, std::string_view which,`

`new Text("x")`, `new Comment("x")` and `new DocumentFragment()` - the
three node interfaces a page may construct. The other eighty-eight throw
"Illegal constructor", which is what a browser does too; these three make
a node owned by this document and NOT in its tree.

<a id="contract-80"></a>

`[[nodiscard]] bool nodes_are_equal(const read_txn & txn, node_id left, node_id right) const;`

`Node.prototype.isEqualNode` - the DOM's structural comparison, in which
two elements' attributes are UNORDERED SETS compared by (namespace, local
name, value) and the prefix takes no part.

<a id="contract-81"></a>

`void record_mutations(const std::vector<document::write_note> & writes);`

WHAT `mutated()` HAS TO CALL, and the whole reason this is a diff.

`mutated()` is the one funnel every DOM-changing native already goes
through - 18 call sites under element/, five in document/ - and it
takes no arguments because the funnel does not know WHAT changed. So the
records are reconstructed rather than reported: `observe()` takes a
snapshot of every observed node's children, attributes and text, and this
diffs that snapshot against the document as it is now, queues a record
for each difference and re-snapshots. See lib/Shell/bindings/mutation.cpp
for the three shapes of mutation a diff genuinely cannot recover.

Costs nothing when no page script has ever constructed a
MutationObserver, which is the overwhelmingly common case: the first line
returns on an empty registration list.
