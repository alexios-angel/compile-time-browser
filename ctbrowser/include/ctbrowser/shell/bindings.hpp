#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/image/images.hpp>
#include <ctbrowser/shell/input.hpp>
#include <ctbrowser/shell/net/net.hpp>
#include <ctbrowser/shell/page/assets.hpp>
#include <ctbrowser/shell/page/canvas.hpp>
#include <ctbrowser/shell/page/forms.hpp>
#include <ctbrowser/shell/page/webgl.hpp>

// The web platform, bound to the engine VM. Two invariants:
//
//   * SCRIPT HOLDS HANDLES, NOT POINTERS. An element wrapper carries a
//     node_id, and every native resolves it against the live document. A stale
//     reference is a failed lookup that returns undefined, not a use-after-free.
//   * MUTATION IS A CALLBACK. A native that changes the document calls
//     on_mutation, and the browser decides what that invalidates. Bindings do
//     not know about layout, and layout does not know script exists.
//
// Gaps are named rather than stubbed: a `getContext` that returns an object
// with no drawing on it is worse than one that is absent, because a page
// checking for canvas support gets the wrong answer.

namespace ctbrowser::shell {

using ctbrowser::script::context;
using ctbrowser::script::value;

// Argument coercion.
[[nodiscard]] inline std::string arg_string(context & cx, std::span<value> args, std::size_t i) {
    return i < args.size() ? cx.to_string(args[i]) : std::string{};
}
[[nodiscard]] inline double arg_number(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}
[[nodiscard]] inline value arg(std::span<value> args, std::size_t i) {
    return i < args.size() ? args[i] : value::undefined();
}

// Installing natives. One function object, and the two ways the bindings hang
// one on an object: as a data property (`attrs` is what `define` takes, and
// `attr_default` means a plain `set`, which keeps an existing property's
// attributes) or as an accessor. An accessor's natives are named the way
// WebIDL names them - `get x` and `set x`.
[[nodiscard]] inline value native(context & cx, std::string name, script::native_fn fn) {
    return value::object(cx.allocate<script::native_object>(std::move(name), std::move(fn)));
}
template <class Obj>
void set_method(context & cx, Obj & obj, const std::string & name, script::native_fn fn,
                std::uint8_t attrs = script::attr_default) {
    obj.set(name, native(cx, name, std::move(fn)));
    if (attrs != script::attr_default) { obj.set_attrs(name, attrs); }
}
template <class Obj>
void define_getter(context & cx, Obj & obj, const std::string & name, script::native_fn get,
                   script::native_fn set = {},
                   std::uint8_t attrs = script::attr_enumerable | script::attr_configurable) {
    obj.define_accessor(name, native(cx, "get " + name, std::move(get)),
                        set ? native(cx, "set " + name, std::move(set)) : value::undefined(),
                        attrs);
}

// Where an element wrapper keeps its handle. A property rather than a side
// table, so a wrapper is self-describing and two wrappers for the same element
// resolve to the same node.
inline constexpr std::string_view handle_property = "__node";

// Where a Path2D keeps the verbs it recorded. A Path2D is a RECORDING, not a
// drawing: it is built once and replayed into a canvas by fill(path) or
// stroke(path), possibly under a different transform than the one in force
// when it was built. Keeping the verbs in an ordinary script array means the
// GC traces them with no new heap kind, and a page can be shown what it built.
inline constexpr std::string_view path_commands_property = "__cmds";

class dom_bindings {
public:
    // `on_mutation` is how the browser learns it has to re-run the pipeline.
    // Taking it as a callback rather than a browser reference keeps this
    // testable on its own and keeps the dependency pointing one way.
    dom_bindings(document & doc, atom_table & atoms, canvas_store & canvases, form_store & forms,
                 std::function<void()> on_mutation, std::function<void(node_id)> on_focus)
        : doc_(&doc), atoms_(&atoms), canvases_(&canvases), forms_(&forms),
          on_mutation_(std::move(on_mutation)), on_focus_(std::move(on_focus)) {}

    // OUT OF LINE, because `secondary_documents_` below is a vector of
    // unique_ptr to THIS class and the deleter has to be instantiated where the
    // class is complete.
    ~dom_bindings();

    // Where loadImage() and fetch() look. Both are owned by the browser, which
    // hands them over before scripts run; without them the page still runs and
    // every load simply fails.
    void observe_resources(asset_registry & assets, image_store & images);
    // Whether fetch() may open a socket when the registry misses. Off makes a
    // run hermetic, which is what a test wants and what CTBROWSER_NETWORK=0
    // selects.
    void allow_network(bool allowed) { network_allowed_ = allowed; }

    // NAVIGATION, as state rather than as an action. `location.reload()` cannot
    // reload the page where it is called: the reload tears down this context and
    // the program still running inside it. So it records the request and the
    // browser drains it between ticks.
    [[nodiscard]] bool reload_requested() const noexcept { return reload_requested_; }
    // What `location` reports. The browser sets it; the page can only read it,
    // because assigning to location.href is a navigation and the engine has none.
    void observe_location(std::string href, std::string hash);

    // Where focus is now, so `document.activeElement` can answer. The focus
    // hook below goes the other way - script setting focus - and left the
    // bindings with no way to READ it.
    void observe_focus(node_id id);
    // Where alert() goes. The browser records them, because a reload replaces
    // these bindings and the alert that caused it must survive that.
    void set_alert_hook(std::function<void(const std::string &)> hook);

    // `element.click()` does two things: it dispatches a click event, and - if
    // nothing called preventDefault - it performs the element's DEFAULT ACTION.
    // The second half belongs to the browser (following a link, toggling a
    // checkbox, submitting a form), so it comes in as a hook for the same reason
    // on_mutation does: the dependency points one way.
    void set_activate_hook(std::function<void(node_id)> hook) { on_activate_ = std::move(hook); }

    // Returns whether a page write reached a control, so the browser knows the
    // paint is stale without having to diff the form store.
    bool refresh_wrappers();

    // Layout results, so offsetWidth and friends can answer. Set by the
    // browser after each layout; null until the first one, and the natives
    // return 0 then rather than pretending.
    void observe_layout(const layout::fragment * fragments) { fragments_ = fragments; }
    // The cascade's output and the box tree, for getComputedStyle. Three sources
    // are needed rather than one because `style::computed_style` is not a
    // computed style: it holds only the declarations that MATCHED, as text, with
    // no inheritance and no initial values. So a keyword comes from the style
    // map, a resolved length from the box tree, and a used size from the
    // fragment - see lib/Shell/bindings/computed_style/.
    void observe_styles(const style::style_map * styles) { styles_ = styles; }
    void observe_boxes(const layout::box_node * boxes) { boxes_ = boxes; }
    void observe_viewport(int width, int height);
    // THE FRAMES THIS DOCUMENT HAS LOADED, each with the bindings over its
    // document, so the browser can run a frame's document through the same
    // stages as the page at the size its <iframe> box got (browser::
    // layout_frames). The bindings deliberately know nothing of that pipeline;
    // this is the list it walks.
    struct loaded_frame {
        node_id element;
        dom_bindings * bindings;
    };
    [[nodiscard]] std::vector<loaded_frame> loaded_frames() const {
        std::vector<loaded_frame> out;
        for (const frame_entry & entry : frames_) {
            if (entry.bindings != nullptr) { out.push_back({unpack(entry.key), entry.bindings}); }
        }
        return out;
    }
    [[nodiscard]] document & owned_document() noexcept { return *doc_; }
    // Milliseconds since the page loaded, for performance.now and the timers.
    void advance_clock(double ms) { now_ms_ += ms; }
    [[nodiscard]] double now_ms() const noexcept { return now_ms_; }

    // Everything this holds that the VM cannot see. Registered with the context
    // so a collection does not free a page's own listeners out from under it.
    void register_roots(context & cx);

    void install(context & cx);

    // --- dispatch, from the browser --------------------------------------

    // Fire `type` at `target` and at every ancestor, like a bubbling DOM event.
    // Returns whether a listener called preventDefault.
    bool dispatch(std::string_view type, node_id target);

    // A KeyboardEvent. `code` is the physical key and `key` is what it means -
    // pages read both, and an event object carrying neither is why a page could
    // register a keydown listener and never learn which key was pressed.
    bool dispatch_key(std::string_view type, node_id target, const input_event & input);

    // AN UNCAUGHT EXCEPTION, ANNOUNCED to `window.onerror` and
    // `addEventListener("error", ...)` - which is how testharness.js tells "this
    // test threw during load" from "this test never finished". The event carries
    // `message`, `filename`, `lineno`, `colno` and `error` because those are the
    // five properties that handler reads; only `message` is real here, and the
    // rest say so by being empty rather than by being absent.
    bool dispatch_error(std::string_view message);

    // A MouseEvent. clientX/clientY are viewport coordinates, which is what
    // MDN's breakout reads to move its paddle.
    bool dispatch_mouse(std::string_view type, node_id target, const input_event & input);
    // A `wheel` event, and whether the page CONSUMED it - a page that calls
    // preventDefault means the document must not scroll as well. See the
    // definition for the sign and the units, both of which are easy to invert.
    bool dispatch_wheel(node_id target, const input_event & input);

    bool dispatch_event(std::string_view type, node_id target, value event);

    // `element.click()`, HTML 3.2.6: nothing for a disabled form control, else
    // a synthetic, untrusted MouseEvent `click` - bubbles, cancelable, composed
    // - sent down the one dispatch path an engine click takes, activation
    // behaviour included. The browser calls it for a <label>, whose activation
    // behaviour IS a click at the control it labels. Returns whether cancelled.
    bool click(node_id target);
    // "Connected", DOM 4.2.1: the shadow-including root is the document. A
    // checkbox fires `input` and `change` only when it is, and a form submits
    // only when it is.
    [[nodiscard]] bool is_connected(node_id target) const;

    // Run the timers that are due, then the animation callbacks. Returns how
    // many ran, so an event loop can tell whether it needs another frame.
    std::size_t run_due_callbacks();

    // --- NESTED BROWSING CONTEXTS (bindings/frames.cpp) --------------------
    //
    // Bring the `<iframe>`s in this document into step with the tree: load the
    // ones that appeared, reload the ones whose `src` changed, forget the ones
    // that went away. Called from the browser's tick BEFORE the window's `load`
    // event, because a page's `load` handler is where WPT reads
    // `frame.contentDocument` and it must already be there.
    //
    // A FRAME IS A SECOND DOCUMENT, which is a model this class already has -
    // see `make_html_document`. What a frame adds to it is the bytes: the src
    // is resolved through the asset registry, so a frame loads from wherever
    // the page's other subresources load from and reaches no socket of its own.
    void reconcile_frames();
    // Paint Timing: `first-paint` and `first-contentful-paint`, at the page
    // clock's current reading, once. The browser calls it from its first frame.
    void record_first_paint();
    // A resource the BROWSER loaded for an element - a `<link rel=stylesheet>`,
    // a `<style>`, a `<script>` - is announced at that element on the next
    // tick, the way an `<iframe>`'s load is: `load`, or `error` when the bytes
    // were not found. Queued rather than fired because the page's own script
    // registers the listener after the element has already been processed.
    void announce_load(node_id id, bool ok) { frame_loads_.push_back(pending_frame{id, ok, true}); }

    [[nodiscard]] std::size_t pending_timers() const noexcept { return timers_.size(); }
    // When the next callback is due, in milliseconds from now. Infinity when
    // there is none - which is what lets an idle application block rather than
    // poll. An animation frame is due IMMEDIATELY: a page that asked for one
    // wants the next frame, not a timer's worth of delay.
    [[nodiscard]] double next_callback_ms() const;

    [[nodiscard]] std::size_t pending_animation_frames() const noexcept;
    // The first fault a timer or animation-frame callback raised. Empty when
    // the page's callbacks are running cleanly.
    [[nodiscard]] const std::string & callback_error() const noexcept { return callback_error_; }
    // --- the WebGL back end, stage 2 of docs/plans/angle.md -------------------
    //
    // BEFORE THE PAGE RUNS. A context is made when a page asks for one, and its
    // backend cannot change underneath programs already compiled into it - so
    // this decides for contexts made from here on and says nothing about any
    // that exist.
    void prefer_angle(bool on) { angle_preferred_ = on; }
    // Every GL call a page made that the ANGLE path does not forward yet,
    // gathered from all its contexts. EMPTY is the claim a test makes; a
    // backend that silently dropped calls would paint something plausible.
    [[nodiscard]] std::vector<std::string> unforwarded_gl_calls() const;

    [[nodiscard]] const std::vector<std::string> & console_output() const noexcept;

private:
    struct timer {
        std::uint32_t id = 0;
        value callback;
        double due_ms = 0;
        double interval_ms = 0;
        bool repeating = false;
        bool cancelled = false;
    };
    // WHICH EVENT TARGET A LISTENER IS ON. The document and the window are not
    // nodes and must not share a bucket: `currentTarget` reports each, and
    // `removeEventListener` on one must not take the other's listener away.
    enum class listen_on : std::uint8_t {
        node,     // an element; `target` names it
        document, // document.addEventListener
        window,   // window.addEventListener, and the bare global spelling
        // `new EventTarget()`, and anything that inherits from one. It has no
        // node and no place in the tree, so it is identified by the OBJECT -
        // `host` below - and its path is itself and nothing else.
        object
    };

    // One stop on the path an event travels. A node, one of the two event
    // targets that have no node, or a standalone EventTarget.
    struct path_step {
        node_id node;                    // empty unless `on` is `node`
        listen_on on = listen_on::node;  // which kind of target this is
        value host = value::undefined(); // set only when `on` is `object`
    };

    struct listener {
        node_id target; // set only when `on` is `node`
        listen_on on = listen_on::node;
        // The standalone EventTarget this listener is on, when `on` is `object`.
        // A GC root: nothing else may be holding it while a listener is.
        value host = value::undefined();
        std::string type;
        value callback;
        // The AbortSignal this listener was registered with, if any. Aborting
        // it removes every listener that carries it - which is how a library
        // takes down a whole sketch's listeners in one call.
        value abort_signal = value::undefined();
        // `{ once: true }` - fire and remove.
        bool once = false;
        // `{ capture: true }` - fired on the way DOWN to the target rather than
        // on the way back up. It is the whole reason to pass it: a capturing
        // listener on an ancestor sees the event BEFORE the target does, which
        // is how a page intercepts one.
        bool capture = false;
        // `{ passive: true }` - a promise that this listener will not call
        // preventDefault, which the DOM ENFORCES rather than trusts: the
        // canceled flag is not set while a passive listener runs.
        // `AddEventListenerOptions-passive.any.js` is three tests about exactly
        // that and `passive-by-default.html` is a hundred more.
        bool passive = false;
        // Set when a `once` listener has fired, so the pass that removes them
        // runs after the dispatch rather than mutating the list being walked.
        bool spent = false;
    };

    // --- element wrappers -------------------------------------------------

    // ONE WRAPPER PER ELEMENT, cached: `getElementById('x') ===
    // getElementById('x')`, and a wrapper a page holds on to keeps answering
    // about the live element rather than about a snapshot.
    [[nodiscard]] value wrap(context & cx, node_id id);

    [[nodiscard]] static std::uint64_t pack(node_id id);
    [[nodiscard]] static node_id unpack(std::uint64_t bits);

    // The element a native was called on. Returns an empty handle when the
    // receiver is not a wrapper - which a native must treat as "do nothing"
    // rather than as "the document root".
    [[nodiscard]] node_id receiver(context & cx);

    // Live-ish properties. Refreshed when a wrapper is made and after layout,
    // which is what `element.offsetWidth` actually needs to be useful.
    void refresh_element(context & cx, script::object_object & obj, node_id id);

    [[nodiscard]] rect box_of(node_id id) const;

    // THE IDL OPERATIONS, ON THE INTERFACE PROTOTYPES - one native per realm,
    // not one per wrapper. `Node.prototype.appendChild.call(x, y)`,
    // `"insertBefore" in Node.prototype` and `.length` on each are what the
    // corpus asks; a wrapper carrying eighty own natives answered none of them.
    //
    // Every operation is recorded in `operations_` by the instance that built
    // it, and the native on the prototype - installed by the PRIMARY bindings
    // only - is a trampoline that asks `owner_of(this)` which instance's copy
    // to run: a second Document shares the realm's prototypes (see
    // adopt_interfaces_of) and its nodes must edit ITS tree, not the primary's.
    // `interfaces` are the interface names whose prototypes get the native -
    // one for a Node operation, three for a ParentNode mixin - and `length` is
    // the WebIDL argument count.
    void define_operation(context & cx, std::initializer_list<const char *> interfaces,
                          const char * name, unsigned length, script::native_fn fn);
    // The three files the operations live in - see lib/Shell/bindings/element/.
    // Called from install_operations only.
    void install_operations(context & cx);
    void install_attribute_methods(context & cx);
    void install_node_methods(context & cx);
    void install_control_methods(context & cx);
    struct operation {
        std::string name;
        script::native_fn fn;
    };
    // Built lazily for a secondary document, on the first call routed to it.
    std::vector<operation> operations_;
    void note_callback_fault(std::string_view source);
    // One reading of addEventListener's third argument, shared by the element,
    // document and window registrations - three copies is three chances for
    // `once` to work on one of them and not the others.
    [[nodiscard]] listener make_listener(context & cx, path_step target, std::span<value> args);
    // Register one, UNLESS AN EQUAL ONE IS ALREADY THERE. The DOM defines a
    // listener's identity as (type, callback, capture) on one target and says a
    // second addEventListener with all three the same does nothing - which is
    // what a page relies on when it registers defensively in a function it calls
    // more than once. Every addEventListener goes through here so the rule holds
    // for elements, the document, the window and a standalone EventTarget alike.
    void add_listener(listener made);
    // Drop the listeners a `once` fired, but ONLY when no dispatch is running:
    // erasing from the vector a dispatch is indexing is how the listener after
    // the removed one gets skipped, and a listener may dispatch another event.
    void reap_spent_listeners();
    // `innerHTML`. Setting one PARSES: the markup becomes real nodes under the
    // element, replacing whatever was there.
    void set_inner_html(node_id target, std::string_view markup);
    [[nodiscard]] std::string inner_html(node_id target) const;
    // One node and its subtree, copied from another document into this one.
    // The scratch document a fragment is parsed into shares this atom table, so
    // a tag or attribute name needs no remapping.
    node_id copy_subtree(const read_txn & from, node_id node, node_id parent);
    // `cloneNode(deep)`: a DETACHED copy, which is what makes it different from
    // copy_subtree above - that one exists to move a parsed fragment into this
    // document and needs somewhere to put it.
    // `owner` is the bindings the source node belongs to when it is not this
    // one - `importNode` reads another document's tree - and null otherwise.
    node_id clone_node(const read_txn & from, node_id source, bool deep,
                       const dom_bindings * owner = nullptr);
    // Insert `child` into `parent`, before `before` or at the end when `before`
    // is empty, FLATTENING a DocumentFragment: inserting one moves its children
    // and leaves the fragment itself empty and parentless. Every insertion
    // method goes through here, because a fragment is legal at every one of them
    // and handling it at four call sites is three chances to forget.
    bool insert_node(node_id parent, node_id child, node_id before);

    // THE "ENSURE PRE-INSERTION VALIDITY" STEPS, DOM 4.2.3, shared by
    // `insertBefore`, `appendChild` and `moveBefore`. Answers false having
    // ALREADY THROWN, so a caller is one `if` rather than an error channel.
    [[nodiscard]] bool pre_insert_valid(context & cx, node_id parent, node_id child, value node_arg,
                                        value ref_arg);
    // One argument of append/prepend/before/after/replaceWith, as a node. A
    // wrapper resolves to its node; ANYTHING ELSE becomes a Text node, which is
    // what makes `el.append("hello")` work and is the whole reason those methods
    // are nicer than appendChild.
    // Another document's node is ADOPTED - cloned into this slab, its wrapper
    // rebound - except a fragment, whose children come and which itself stays
    // where it was, as insertion has it; `adoptNode` asks for the fragment too
    // with `whole_fragment`.
    [[nodiscard]] node_id node_from(context & cx, value v, bool whole_fragment = false);
    // "Convert nodes into a node", DOM 4.2.5: the arguments of one of those
    // methods as ONE node - the node itself for one argument, a fragment holding
    // them all (which MOVES each out of the tree) for several.
    [[nodiscard]] node_id convert_nodes(context & cx, std::span<value> args);
    // "Viable next/previous sibling", DOM 4.2.7: the first sibling of `self` in
    // that direction that is not one of `args`. Empty when there is none.
    [[nodiscard]] node_id viable_sibling(node_id self, std::span<value> args, bool forward);
    // THE EXACT NAMESPACE OF AN ELEMENT, as a string. Derived from `element_ns`
    // for everything the parser built - there are only two answers there - and
    // read from `namespaces_` for an element `createElementNS` put in some other
    // one. Empty means the null namespace, which reports as `null`.
    [[nodiscard]] std::string namespace_of(node_id id) const;
    [[nodiscard]] std::string text_content(node_id target) const;
    void write_location_parts(context & cx, script::object_object & loc);
    // `element.style` and `element.classList` - the two views onto an element
    // that are OBJECTS rather than values, so unlike everything in
    // refresh_element they are built once and keep their identity. A page holds
    // on to `el.style` and writes through it later, which a fresh object every
    // sync would silently discard.
    //
    // They take the id directly because their methods are not called with the
    // element as `this`: `el.classList.add(...)` has the CLASS LIST as the
    // receiver, so `receiver(cx)` finds no handle.
    void install_element_views(context & cx, script::object_object & obj, node_id id);

    // `getComputedStyle`, on `window` and as a bare global.
    void install_computed_style(context & cx);
    [[nodiscard]] value computed_style_object(context & cx, node_id id);
    // ...for `getComputedStyle(el, "::before")`: `pseudo` is the pseudo-element's
    // name (`before`/`after`), resolved through the style engine on every read.
    [[nodiscard]] value computed_style_object(context & cx, node_id id, atom pseudo);
    // ONE ELEMENT'S WHOLE COMPUTED STYLE, as (CSS name, value) pairs: the
    // supported longhands lexicographically, then the shorthands, then whatever
    // the element declared that the property table has never heard of. Empty for
    // an element that is not in the document.
    //
    // SEPARATE FROM THE OBJECT because the object is LIVE: CSSOM says
    // getComputedStyle returns a live CSSStyleDeclaration, so every property on
    // it is an accessor that calls this again rather than a string captured when
    // the object was made. It holds raw pointers into the box and fragment trees
    // for the length of the call and never past it - the next layout frees them,
    // and that now happens inside a script turn.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> computed_style_entries(
        node_id id);
    // ...and of the element's `::before`/`::after`: `pseudo` is the style
    // engine::resolve_pseudo made for it, read in the element's place - no box,
    // percentages against the element's content box, the element as parent.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> computed_style_entries(
        node_id id, const style::computed_style_ptr & pseudo);

    // --- DOMException, and the CSS interface --------------------------------
    //
    // `bindings/exceptions.cpp` owns the exception hierarchy every DOM method
    // throws through, `bindings/css.cpp` the `CSS` namespace object.
    void install_dom_exception(context & cx);
    // A DOMException instance with the right `name`, `code` and `message`, on
    // `DOMException.prototype` - which is what `assert_throws_dom` checks and
    // what `context::throw_error` cannot build, because an engine-raised error
    // is an ECMAScript Error by construction.
    [[nodiscard]] value make_dom_exception(context & cx, std::string_view name,
                                           std::string message);
    // ...and thrown, which is the form a native binding needs.
    void throw_dom_exception(context & cx, std::string_view name, std::string message);

    // The `CSS` namespace object - `CSS.supports` and `CSS.escape`. Its answers
    // come from style/css/properties.hpp, so it can never disagree with
    // `el.style` about whether a value is valid.
    void install_css_interface(context & cx);

    // --- Each BEGIN/END block below belongs to exactly one file in bindings/.

    // BEGIN reflection (bindings/element/reflection.cpp)
    //
    // THE INTERFACE OBJECTS, AND REFLECTION, WHICH ARE ONE THING.
    //
    // `HTMLDivElement.prototype -> HTMLElement.prototype -> Element.prototype ->
    // Node.prototype -> EventTarget.prototype` is a real chain here, built once
    // per page from a static table in element/interfaces.cpp, and every wrapper is linked
    // into it by the tag it has. That is what `el instanceof HTMLBodyElement`
    // and `eventTarget.constructor.name` ask, and it is ALSO where the reflected
    // IDL attributes live: `id`, `href`, `disabled` and the ~270 others are
    // accessors on ONE prototype each rather than on every wrapper, which is
    // both what the specification says and what makes a table affordable. See
    // element/reflection.cpp for the table and for what each type does.
    void install_dom_interfaces(context & cx);
    // Build them if they are not built and the pieces they chain to exist yet.
    // Cheap after the first success; called from wrap() because the bindings'
    // install order puts the first element wrapper BEFORE `EventTarget` exists.
    void ensure_dom_interfaces(context & cx);
    // `HTMLCanvasElement.prototype` for a <canvas>, `Text.prototype` for a text
    // node, `Element.prototype` for something createElementNS put in a
    // namespace that is neither HTML nor SVG.
    [[nodiscard]] value prototype_for_node(const read_txn & txn, node_id id) const;
    // One interface's prototype by name - how a collection built in another
    // translation unit becomes `instanceof HTMLCollection`. Undefined before
    // the interfaces are built, and undefined for a name that is not one.
    [[nodiscard]] value interface_prototype(std::string_view name) const;
    // ONE reflected IDL attribute, read and written. The row is a pointer into
    // the static table in element/reflection.cpp and is `const void *` here for the reason
    // the third-party-header invariant exists: the row type is one file's
    // business, and putting it in this header would make every consumer of the
    // engine parse a 270-row table's declaration to get at `document`.
    [[nodiscard]] value reflected_get(context & cx, const void * row);
    [[nodiscard]] value reflected_set(context & cx, const void * row, std::span<value> args);

    // --- attributes as nodes: Attr, and the NamedNodeMap over them ---------
    //
    // A qualified name as THIS element would have stored it: lowercased for an
    // HTML element and left exactly as written for anything else. It is the
    // only difference between `getAttribute` and `getAttributeNS`, and it is
    // why `svg.setAttribute("viewBox", ...)` must not fold.
    [[nodiscard]] atom attribute_key(const read_txn & txn, node_id id,
                                     std::string_view qualified) const;
    // ONE Attr, LIVE: `value`, `nodeValue` and `textContent` read and write the
    // element's attribute rather than a string captured when it was made.
    [[nodiscard]] value attribute_object(context & cx, node_id owner, const attribute & held);
    // `element.attributes`, refilled in place so the map keeps its identity.
    void refresh_attribute_map(context & cx, script::object_object & map, node_id id);
    // `element.dataset` - a DOMStringMap over the `data-*` attributes. Its own
    // function rather than more of install_element_views because it is
    // CONDITIONAL: only an HTML, SVG or MathML element has one.
    void install_dataset(context & cx, script::object_object & obj, node_id id);
    // DOM 4.9 "validate and extract", shared by setAttributeNS and
    // setNamedItemNS. False HAVING ALREADY THROWN - InvalidCharacterError for a
    // name that is not a QName, NamespaceError for the four prefix rules.
    [[nodiscard]] bool validate_and_extract(context & cx, std::string_view where,
                                            const std::string & ns, const std::string & qualified);

    // Parallel to the static interface table in element/interfaces.cpp: one prototype per
    // row, in the same order, so a tag resolves to a prototype by index.
    std::vector<value> interface_prototypes_;
    // EVERY PROTOTYPE, IN ONE ARRAY THE COLLECTOR CAN SEE. Each interface's
    // prototype is reachable from its constructor, which is a global - and a
    // page may delete a global, after which a prototype this object still
    // points at could be swept. So every constructor `retains` this one array,
    // and all 70-odd of them would have to be deleted before any prototype
    // became unreachable.
    value interface_keeper_;
    // Set once the chain is built AND linked to `event_target_prototype_`,
    // which install_event_interfaces publishes after the first wrapper exists.
    bool interfaces_linked_ = false;

    // --- CharacterData ---
    //
    // `CharacterData.prototype` AND `Text.prototype`, filled in once the
    // interface chain exists. On the PROTOTYPES rather than on every wrapper,
    // for the reason reflection is: `substringData` is one function per page
    // here and was one per text node in the shape install_element_methods uses.
    //
    // Every offset in them is a UTF-16 CODE UNIT and this engine stores UTF-8 -
    // see the helpers above install_character_data in element/character_data.cpp, and the note
    // there on what a surrogate pair costs.
    void install_character_data(context & cx);
    // `new Text("x")`, `new Comment("x")` and `new DocumentFragment()` - the
    // three node interfaces a page may construct. The other eighty-eight throw
    // "Illegal constructor", which is what a browser does too; these three make
    // a node owned by this document and NOT in its tree.
    [[nodiscard]] value construct_node_interface(context & cx, std::string_view which,
                                                 std::span<value> args);
    // `Node.prototype.isEqualNode` - the DOM's structural comparison, in which
    // two elements' attributes are UNORDERED SETS compared by (namespace, local
    // name, value) and the prefix takes no part.
    [[nodiscard]] bool nodes_are_equal(const read_txn & txn, node_id left, node_id right) const;
    // END reflection

    // BEGIN mutation observers (bindings/mutation.cpp)
public:
    // WHAT `mutated()` HAS TO CALL, and the whole reason this is a diff.
    //
    // `mutated()` is the one funnel every DOM-changing native already goes
    // through - 18 call sites under element/, five in document/ - and it
    // takes no arguments because the funnel does not know WHAT changed. So the
    // records are reconstructed rather than reported: `observe()` takes a
    // snapshot of every observed node's children, attributes and text, and this
    // diffs that snapshot against the document as it is now, queues a record
    // for each difference and re-snapshots. See lib/Shell/bindings/mutation.cpp
    // for the three shapes of mutation a diff genuinely cannot recover.
    //
    // Costs nothing when no page script has ever constructed a
    // MutationObserver, which is the overwhelmingly common case: the first line
    // returns on an empty registration list.
    void record_mutations();

private:
    // ONE `observe()` CALL'S OPTIONS, after the dictionary's own defaulting.
    // `attributeOldValue` or `attributeFilter` PRESENT with `attributes`
    // ABSENT turns `attributes` on - which is why `attributes` is a field here
    // and not just a read of the dictionary.
    struct mutation_options {
        bool child_list = false;
        bool attributes = false;
        bool character_data = false;
        bool attribute_old_value = false;
        bool character_data_old_value = false;
        bool subtree = false;
        bool has_attribute_filter = false;
        std::vector<std::string> attribute_filter;
    };

    // A REGISTERED OBSERVER, DOM §4.3.1: one (observer, target) pair with its
    // options. `observe()` on a target already registered for this observer
    // REPLACES the options rather than adding a second entry.
    struct mutation_registration {
        std::size_t observer = 0; // index into mutation_observers_
        node_id target;
        // `observe(document, ...)`: the document object carries no node handle
        // here - this tree builder has no Document node above `<html>` - so the
        // registration is on the root element and remembers that it stands for
        // the document.
        bool whole_document = false;
        mutation_options options;
    };

    // ONE OBSERVED NODE AS IT WAS, which is what a record is a difference from.
    // All three are captured for every observed node regardless of which the
    // registration asked for: a node may be observed by two registrations that
    // want different things, and one snapshot that answers both is cheaper than
    // keeping the union of their options per node.
    struct mutation_node_state {
        std::vector<node_id> children;
        std::vector<attribute> attributes;
        std::string text; // CharacterData nodes only
    };

    void install_mutation_observer(context & cx);
    // The MutationObserver instance at that index, and its pending record
    // queue. The queue is a JavaScript ARRAY held on the instance rather than a
    // std::vector<value> here, so that rooting the instance roots the records
    // and there is one thing to keep alive instead of two.
    [[nodiscard]] script::object_object * mutation_observer_at(std::size_t index);
    [[nodiscard]] script::array_object * mutation_records_of(std::size_t index);
    // A MutationRecord with every field the IDL names, absent ones null and the
    // two node lists real empty arrays.
    [[nodiscard]] value make_mutation_record(context & cx, std::string_view type, node_id target);
    void queue_mutation_record(std::size_t observer, value record);
    // The microtask side. `queue_microtask` fixes its arguments at QUEUE time
    // and the record list has to stay open until the microtask RUNS, so what is
    // queued is a native trampoline that calls the second of these.
    void queue_mutation_delivery();
    void deliver_mutation_records();
    // Re-read every observed node. Called by observe(), by disconnect() and at
    // the end of every record_mutations().
    void take_mutation_snapshot();
    void collect_observed(const read_txn & txn, node_id root, bool subtree,
                          std::vector<node_id> & into) const;
    // The observer's index, or npos when the value is not one of ours.
    [[nodiscard]] std::size_t mutation_observer_index(value v) const;
    // WHAT KEEPS ALL THIS ALIVE: the `MutationObserver` interface object's
    // `retained` list - see script::native_object::retained. Refilled whenever
    // the set changes, which is rare and tiny.
    void sync_mutation_roots();

    std::vector<value> mutation_observers_;
    std::vector<mutation_registration> mutation_registrations_;
    flat_map<std::uint64_t, mutation_node_state> mutation_snapshot_;
    value mutation_observer_prototype_;
    value mutation_record_prototype_;
    value mutation_trampoline_;
    // The interface object, whose `retained` list is the root set above.
    script::native_object * mutation_interface_ = nullptr;
    // Whether a delivery microtask is already queued. Cleared when it runs,
    // which is what makes several mutations in one script turn arrive as ONE
    // callback holding several records.
    bool mutation_delivery_queued_ = false;
    // END mutation observers

    // BEGIN web animations (bindings/animations.cpp)
    //
    // THE SLICE OF WEB ANIMATIONS `css/css-values` OBSERVES: `element.animate`
    // makes an Animation over a KeyframeEffect, the page seeks it - `pause()`
    // then `currentTime = t` is what interpolation-testcommon.js does - and
    // reads the animated property back through getComputedStyle. So the model
    // is the specification's timing model over ONE clock (`now_ms_`, which is
    // also `document.timeline.currentTime`) and the effect value is an OVERLAY
    // on the cascade's text for that element: computed_style_entries asks
    // `animated_values` before it asks the style map, and every rule downstream
    // - a percentage against its containing block, an inset's used value - runs
    // on the interpolated text exactly as it would on a declared one.
    //
    // Nothing RENDERS an animation: the overlay exists for getComputedStyle and
    // paint never sees it. Lengths, percentages, calc() and numbers
    // interpolate; everything else is discrete.
public:
    // The animated properties of one element as (css name, text) pairs, at the
    // element's animations' CURRENT time. `underlying` answers the cascade's
    // text for a property, which is the missing endpoint of a one-keyframe
    // effect; `font_size` is the basis an `em` in a keyframe resolves against.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> animated_values(
        node_id id, float font_size,
        const std::function<std::string_view(std::string_view)> & underlying) const;
    // Changes whenever ANY animated answer might: an animation was made,
    // seeked, paused or cancelled, or the clock moved while one is live. Zero
    // while no animation exists, so a page without animations never re-derives
    // a cached computed style on its account.
    [[nodiscard]] std::uint64_t animation_stamp() const noexcept;

private:
    struct animation_keyframe {
        double offset = 1;         // the COMPUTED offset, once the missing ones are filled
        bool offset_given = false; // `getKeyframes()` reports `offset: null` for a computed one
        std::string easing = "linear";
        std::string composite = "auto";
        std::vector<std::pair<std::string, std::string>> values; // css name -> text
    };
    struct effect_timing {
        double delay = 0;
        double end_delay = 0;
        double duration = 0;
        double iterations = 1;
        double iteration_start = 0;
        std::string fill = "auto";
        std::string direction = "normal";
        std::string easing = "linear";
    };
    struct keyframe_effect_record {
        value self;
        node_id target;
        std::string composite = "replace";
        effect_timing timing;
        std::vector<animation_keyframe> keyframes;
    };
    struct animation_record {
        value self;
        std::size_t effect = static_cast<std::size_t>(-1);            // into effects_
        double start_time = std::numeric_limits<double>::quiet_NaN(); // NaN: unresolved
        double hold_time = std::numeric_limits<double>::quiet_NaN();
        double playback_rate = 1;
        value finished; // the `finished` promise
        bool finished_settled = false;
    };
    static constexpr std::size_t no_record = static_cast<std::size_t>(-1);

    // `Animation`, `KeyframeEffect`, `DocumentTimeline`, `document.timeline`,
    // `document.getAnimations`, and `animate`/`getAnimations` on
    // Element.prototype. Called from install_dom_interfaces, which is where
    // that prototype exists; a second call is a no-op.
    void install_animations(context & cx);
    [[nodiscard]] std::size_t animation_index(value v) const;
    [[nodiscard]] std::size_t effect_index(value v) const;
    // `new KeyframeEffect(target, keyframes, options)` and `new Animation(effect)`,
    // shared with `element.animate`, which is the two of them plus `play()`.
    [[nodiscard]] value make_keyframe_effect(context & cx, node_id target, value keyframes,
                                             value options);
    [[nodiscard]] value make_animation(context & cx, std::size_t effect);
    // The two dictionaries. Both throw a TypeError HAVING RETURNED false.
    [[nodiscard]] bool read_timing(context & cx, value options, effect_timing & into);
    [[nodiscard]] bool read_keyframes(context & cx, value keyframes,
                                      std::vector<animation_keyframe> & into);
    // The timing model, Web Animations §4.4-4.5. `current_time` is NaN when
    // unresolved; `play_state` is one of idle/running/paused/finished.
    [[nodiscard]] double animation_current_time(const animation_record & a) const noexcept;
    void set_animation_current_time(animation_record & a, double t);
    [[nodiscard]] std::string_view play_state(const animation_record & a) const noexcept;
    [[nodiscard]] double effect_end_time(const keyframe_effect_record & e) const noexcept;
    // Settle the finished promise when the animation is in the finished state,
    // and bump the stamp; every state change ends here.
    void update_finished_state(context & cx, std::size_t index);
    void sync_animation_roots();
    // The animations whose effect targets `id` - or any descendant of it with
    // `subtree` - that are not idle, in creation order.
    [[nodiscard]] std::vector<std::size_t> animations_on(node_id id, bool subtree) const;

    std::vector<keyframe_effect_record> effects_;
    std::vector<animation_record> animations_;
    value animation_prototype_;
    value keyframe_effect_prototype_;
    value timeline_;
    script::native_object * animation_interface_ = nullptr;
    std::uint64_t animation_generation_ = 0;
    // END web animations
    // BEGIN custom elements (bindings/custom_elements.cpp)
public:
    // WHAT `mutated()` CALLS AFTER THE OBSERVERS. A definition covers every
    // element with its name wherever the parser or a script put one, and the
    // funnel does not say which node changed - so this walks the tree, upgrades
    // any element a definition now covers, diffs the tracked ones against what
    // they were (connected, parent, observed attributes) and RUNS the reactions
    // before returning, which is what [CEReactions] means. Returns on the first
    // line when nothing was ever defined.
    void react_custom_elements();

private:
    struct custom_element_definition {
        std::string name;
        std::string local_name; // the `extends` name, or `name` itself
        value constructor;
        value prototype;
        // The lifecycle callbacks, captured at define time as the
        // specification says - a prototype edited afterwards changes nothing.
        value connected;
        value disconnected;
        value adopted;
        value attribute_changed;
        value connected_move;
        std::vector<std::string> observed_attributes;
    };
    // ONE UPGRADED OR CONSTRUCTED ELEMENT AS IT WAS, which is what a reaction
    // is a difference from - the same shape record_mutations diffs against.
    struct custom_element_state {
        std::size_t definition = 0;
        bool connected = false;
        bool visited = false; // scratch for one scan
        node_id parent;
        std::vector<std::pair<atom, std::string>> attributes; // observed only
    };
    struct custom_element_reaction {
        enum class kind : std::uint8_t {
            upgrade,
            connected,
            disconnected,
            connected_move,
            attribute_changed
        };
        node_id target;
        kind what = kind::upgrade;
        // Strings rather than `value`s: a reaction waits in this queue while
        // the ones before it run script, and nothing would root a heap string.
        std::string name;
        std::string old_value;
        std::string new_value;
        bool has_old = false;
        bool has_new = false;
    };

    void install_custom_elements(context & cx);
    // `document.createElement(name)`: a defined name is constructed through
    // the author's class, anything else is a plain node wrapped.
    [[nodiscard]] value create_html_element(context & cx, const std::string & name);
    // The definition this element's (local name, `is`) pair belongs to, or
    // npos.
    [[nodiscard]] std::size_t custom_definition_for(const read_txn & txn, node_id id) const;
    // The definition whose prototype is on this object's chain, or npos - how
    // the HTMLElement constructor learns which class `super()` came from.
    [[nodiscard]] std::size_t custom_definition_of(context & cx, value receiver) const;
    // One subtree in tree order: upgrade what is new, diff what is tracked.
    void walk_custom_elements(const read_txn & txn, node_id start, bool connected);
    void scan_custom_elements();
    void flush_custom_element_reactions();
    void sync_custom_element_roots();

    std::vector<custom_element_definition> custom_definitions_;
    flat_map<std::uint64_t, custom_element_state> custom_elements_;
    std::vector<custom_element_reaction> custom_reactions_;
    flat_map<std::string, std::vector<value>> when_defined_;
    // The CustomElementRegistry interface object, whose `retained` list roots
    // every constructor, prototype, callback and pending promise above - the
    // arrangement install_mutation_observer uses, for the same reason.
    script::native_object * custom_elements_interface_ = nullptr;
    // END custom elements

    // BEGIN style sheets (bindings/stylesheets/)
public:
    // THE CSSOM'S OWN COPY OF THE AUTHOR'S SHEETS, and why it is a copy.
    //
    // `style::engine::add_sheet` FLATTENS a stylesheet into (selector,
    // declaration) rules and keeps no `css::stylesheet` at all, so the cascade
    // cannot be asked what rules a sheet has. The front end
    // (`style/css/parser.hpp`) is public and header-only, though, so the CSSOM
    // parses the document's own `<style>` and `<link rel=stylesheet>` text a
    // second time - with the SAME rules browser::load_author_styles uses, so
    // the two cannot disagree about which sheets exist.
    //
    // The parse result is converted to OWNED strings immediately and the
    // `css::stylesheet` is dropped. That is deliberate: a `stylesheet` owns a
    // `pool` that every string_view in it points into, so holding one means
    // holding a container that never moves and never reallocates - and holding
    // it buys nothing here, because everything the CSSOM answers with is a
    // SERIALISATION rather than a slice of the source. See the file for what
    // "serialisation" means and why it is not the author's bytes.
    // The declaration itself, and the block algorithms over it, are
    // style/css/properties.hpp's: `el.style` keeps the same list, and the
    // shorthand expansion both need lives once.
    using css_declaration = style::css::declaration;
    // One rule. A grouping rule (`@media`) carries `children` and no
    // declarations; a style rule carries declarations and no children.
    struct css_rule_record {
        std::uint32_t type = 1; // CSSRule.STYLE_RULE and friends
        std::string selector;   // serialised selector list, style rules only
        std::string prelude;    // an at-rule's condition text
        std::string at_name;    // "media", "font-face", ...; empty for a style rule
        // AN AT-RULE THIS FRONT END DISCARDS THE BLOCK OF - `@keyframes`,
        // `@page`, `@supports` - kept as the author wrote it, because the
        // alternative is serialising an empty block that is not what the sheet
        // says. Empty for everything the CSSOM can reconstruct.
        std::string verbatim;
        // `@media`'s query list, ALREADY SERIALISED, one entry per query - which
        // is what a MediaList is a view of. It is not `prelude` because a
        // MediaList is MUTABLE (`appendMedium`, `deleteMedium`, `mediaText`) and
        // a comma-separated string would have to be re-split on every one.
        std::vector<std::string> media_queries;
        std::vector<css_declaration> declarations;
        std::vector<std::size_t> children; // into css_rule_store_
        std::size_t parent = static_cast<std::size_t>(-1);
        std::size_t sheet = static_cast<std::size_t>(-1);
        // An `@import`'s own sheet - `rule.styleSheet` - into css_sheets_.
        std::size_t imported_sheet = static_cast<std::size_t>(-1);
        // `@font-feature-values`' feature blocks, CSS Fonts 4 §8.9: one entry
        // per `name: <integer>+` under `@styleset`, `@annotation` and the
        // rest, which the rule's seven CSSFontFeatureValuesMaps are views of.
        struct feature_value {
            std::string type; // "styleset", "annotation", ...
            std::string name;
            std::vector<double> numbers;
        };
        std::vector<feature_value> features;
    };
    struct css_sheet_record {
        node_id owner; // the <style>/<link>; unset for a constructed sheet
        // The tree the owner was last found in - the document's root or a
        // shadow root - and whether the last walk of that tree still found it.
        // A `<link disabled>` keeps its record and its identity but answers
        // null for `ownerNode`, which is what HTMLLinkElement-disabled-001
        // asserts.
        node_id tree;
        bool attached = false;
        // The CSSImportRule this sheet belongs to, for `ownerRule` and
        // `parentStyleSheet`; unset for every other sheet.
        std::size_t owner_rule = static_cast<std::size_t>(-1);
        std::string href;
        std::string title;
        std::string media;    // the `media` ATTRIBUTE as last seen on the owner
        std::string source;   // what was last parsed, so a <style> edit re-parses
        std::string children; // the <style>'s child ids when last parsed
        bool disabled = false;
        bool constructed = false;
        // CSSOM 6.3 "origin-clean flag": false for a `<link>` fetched from
        // another origin, and then cssRules/insertRule/deleteRule are a
        // SecurityError - the one place the object model refuses to answer.
        bool origin_clean = true;
        // The same list as a rule's, and the reason it is not re-derived from
        // `media` on every read: a script that has called `appendMedium` must
        // not have it undone by the next walk of the DOM.
        std::vector<std::string> media_queries;
        std::vector<std::size_t> rules; // into css_rule_store_
    };

    // `document.styleSheets`, `document.adoptedStyleSheets` and the interface
    // objects. Called from `install`, AFTER install_document - it hangs the
    // accessor off the document object.
    void install_style_sheets(context & cx);
    // `styleElement.sheet` / `linkElement.sheet` - the LinkStyle interface.
    // Called from install_element_views, which is the only place an element
    // wrapper is built. `HTMLLinkElement.disabled` and a ShadowRoot's
    // `styleSheets` / `adoptedStyleSheets` ride on the same prototypes and are
    // installed here too.
    void install_sheet_property(context & cx, script::object_object & obj, node_id id);

    // WHICH `<link>`s ARE STYLESHEETS - one answer for the cascade and the
    // object model, HTML 4.6.7 and 4.2.4.4. `rel` is a space-separated token
    // list matched ASCII case-insensitively; the `disabled` attribute keeps
    // the sheet from being obtained at all; an `alternate stylesheet` is
    // fetched (its `load` fires) but does not apply and is not in
    // `document.styleSheets` unless the link was EXPLICITLY ENABLED - the
    // flag `link.disabled = false` sets when it removes the attribute.
    enum class link_sheet {
        none,
        alternate,
        active
    };
    [[nodiscard]] static link_sheet link_sheet_state(std::string_view rel, bool disabled_attribute,
                                                     bool explicitly_enabled);
    [[nodiscard]] bool link_explicitly_enabled(node_id id) const;
    // HTML 4.2.6, the preferred style sheet set - STICKY, as every engine has
    // it: the first titled sheet to ARRIVE names the set, and a titled sheet
    // inserted before it later does not take over
    // (preferred-stylesheet-reversed-order.html). Arrival order is node
    // creation order, which is what the smallest owner handle among the
    // titled sheets picks out; the sheets are re-derived to find it when
    // nothing has named the set yet.
    // ponytail: never reset, so a page that removes its preferred sheet keeps
    // the name; clear it on removal if a page ever needs that.
    [[nodiscard]] std::string_view preferred_sheet_title();
    // An `@import`'s URL against the sheet it sits in. A `<style>`'s sheet has
    // no href, so its imports resolve as the document's own paths do; a
    // `<link href="a/b.css">` importing `c.css` names `a/c.css`.
    [[nodiscard]] static std::string resolve_sheet_href(std::string_view base,
                                                        std::string_view reference);

    // WHAT THE CASCADE WOULD HAVE TO BE TOLD. insertRule/deleteRule/replaceSync
    // change the object model; nothing reaches `style::engine` from here,
    // because the browser loads the author sheet exactly once per page
    // (`author_sheet_loaded_`) and re-running the cascade is its business, not
    // the bindings'. So the bindings publish the new text and the browser
    // decides: with no hook installed the object model is still correct and the
    // RENDER simply does not move. See bindings/stylesheets/internal.hpp.
    void set_author_styles_hook(std::function<void(std::string)> hook) {
        on_author_styles_ = std::move(hook);
    }
    // Every enabled document sheet, serialised, in document order - exactly
    // what browser::load_author_styles would have concatenated, plus whatever
    // the CSSOM has since done to it.
    [[nodiscard]] std::string author_style_text();

private:
    // The document's sheets, re-derived from the DOM. Cheap and idempotent: an
    // owner node that already has a record keeps it, which is what makes
    // `document.styleSheets[0] === styleElement.sheet` and what keeps a page's
    // expando on a sheet object alive across a read.
    void sync_style_sheets(context & cx);
    // One tree's sheets - the document's, or a shadow root's - into one list.
    // `order` receives the document-order indices the cascade reads, and is
    // null for a shadow tree, which the cascade does not render.
    void sync_sheet_list(context & cx, node_id from, script::object_object & list,
                         std::vector<std::size_t> * order);
    [[nodiscard]] script::object_object * cssom_internals(context & cx);
    [[nodiscard]] value style_sheet_list(context & cx);
    [[nodiscard]] value sheet_object_of(context & cx, node_id owner);
    // THE object for a record - made once, held on the internals object, so
    // `rule.styleSheet`, `sheet.parentStyleSheet`, `document.styleSheets[i]`
    // and `el.sheet` all answer the same object for the same record.
    [[nodiscard]] value sheet_object_for(context & cx, std::size_t sheet);
    [[nodiscard]] value rule_object_for(context & cx, std::size_t rule);
    [[nodiscard]] value make_sheet_object(context & cx, std::size_t sheet);
    [[nodiscard]] value make_rule_object(context & cx, std::size_t rule);
    // An `@import`'s sheet: a record of its own, fetched through the same
    // registry a `<link>` is, parsed - imports and all - and hung off `rule`.
    [[nodiscard]] std::size_t load_imported_sheet(std::size_t rule, std::string_view href);
    [[nodiscard]] value adopted_sheets_array(context & cx, std::span<value> args);
    // `shadowRoot.styleSheets`: the tree's own list, held on the root's wrapper.
    [[nodiscard]] value shadow_sheet_list(context & cx, node_id root);
    // The re-derivation `StyleSheetList.item()` does first.
    [[nodiscard]] std::vector<style::css::namespace_declaration> sheet_namespaces(
        std::size_t sheet) const;
    void resync_sheet_list(context & cx, script::object_object & list);
    [[nodiscard]] value make_rule_list(context & cx, std::span<const std::size_t> rules);
    void refresh_rule_list(context & cx, value list, std::span<const std::size_t> rules);
    [[nodiscard]] value declaration_object(context & cx, std::size_t rule);
    void refresh_declaration_object(context & cx, value declarations);
    void install_stylesheet_prototypes(context & cx);
    // Text -> records. Replaces whatever the sheet held.
    void parse_sheet_rules(std::size_t sheet, std::string_view css);
    // One rule, for insertRule. Returns the new rule's index, or npos with
    // `error` naming the DOMException the caller must throw.
    [[nodiscard]] std::size_t parse_one_rule(std::size_t sheet, std::string_view text,
                                             std::string & error);
    // The CSSOM changed something the cascade would care about.
    void style_sheets_changed();

public:
    // Counts style_sheets_changed(): a CSSOM edit changes what an element
    // computes to without touching the document, so a cached computed style
    // compares this beside the document version.
    [[nodiscard]] std::uint64_t style_stamp() const noexcept { return style_generation_; }

private:
    std::uint64_t style_generation_ = 0;
    [[nodiscard]] css_sheet_record * receiver_sheet(context & cx);
    [[nodiscard]] css_rule_record * receiver_rule(context & cx);
    // The media query list `this` is a view of - a sheet's or a media rule's.
    // One function for both because a MediaList carries whichever private slot
    // names its owner and CSSOM gives the two the same interface.
    [[nodiscard]] std::vector<std::string> * receiver_media(context & cx);
    // A MediaList over one of those lists, cached on `owner` under a private
    // slot so that `sheet.media === sheet.media` - which is [SameObject].
    [[nodiscard]] value media_list_object(context & cx, script::object_object & owner);
    void refresh_media_list(context & cx, value list);
    [[nodiscard]] std::string rule_css_text(const css_rule_record & rule) const;

    // unique_ptr rather than a bare vector because a record is addressed by
    // INDEX from script and by REFERENCE from C++, and inserting a rule while a
    // reference to another one is live would otherwise dangle.
    std::vector<std::unique_ptr<css_sheet_record>> css_sheets_;
    std::vector<std::unique_ptr<css_rule_record>> css_rule_store_;
    // Owner node -> sheet index, rebuilt by sync_style_sheets.
    flat_map<std::uint64_t, std::size_t> css_sheet_by_owner_;
    // ...and the same sheets in DOCUMENT ORDER, which is what the cascade needs
    // and what `css_sheets_` is not: a record whose `<style>` has since been
    // removed stays in the store (a rule object the page still holds must keep
    // answering) and must not appear in the author CSS.
    std::vector<std::size_t> css_document_sheets_;
    // The `<link>`s whose `disabled` attribute a script removed - HTML's
    // "explicitly enabled" flag, which is what lets an alternate sheet apply.
    std::vector<std::uint64_t> enabled_links_;
    std::string css_preferred_title_; // see preferred_sheet_title
    // The StyleSheetList, the adopted array and the interface prototypes. Held
    // on the DOCUMENT under a non-configurable private key as well as here, so
    // the collector reaches them through `mark(document_)` and this member
    // needs no line in register_roots.
    value cssom_internals_;
    std::function<void(std::string)> on_author_styles_;
    // END style sheets

    // BEGIN selectors (bindings/document/tree_ops.cpp)
public:
    // THE CASCADE'S OWN ENGINE, so that a selector cannot mean one thing in a
    // stylesheet and another in a script. `query()` runs `style::engine::select`,
    // which is the matcher a rule goes through; handing over the browser's engine
    // rather than making one here is what keeps `:hover` and the interned atoms the
    // same on both sides.
    //
    // Optional: bindings built without a browser - which several unit tests do -
    // fall back to an engine of their own. Matching needs the atom table and the
    // traversal state, not the rules, so an engine with no sheets in it answers a
    // query exactly as well.
    void observe_style_engine(style::engine & engine) { selector_engine_ = &engine; }

    // getElementById's walk, which the browser's fragment scroll and focus
    // navigation share rather than keeping a second one.
    [[nodiscard]] node_id find_by_id(const std::string & want);

private:
    [[nodiscard]] style::engine & selector_engine();
    style::engine * selector_engine_ = nullptr;
    std::unique_ptr<style::engine> own_selector_engine_;

    // --- THE DOCUMENT AS A NODE (bindings/document/as_node.cpp) -----------
    //
    // There is NO Document node in this tree: `txn.root()` is the `<html>`
    // element and `document` is a plain script object carrying no handle at
    // all. Every Node and ParentNode member below therefore answers as if
    // there were a Document whose one child is `documentElement`. The whole of
    // that decision, and what it makes impossible, is written down above
    // `install_document_as_node` in bindings/document/as_node.cpp - read it before
    // adding to any of these.
    void install_document_as_node(context & cx, script::object_object & doc);
    // DOM 6: `createTreeWalker`, `createNodeIterator` and the `NodeFilter`
    // constants - bindings/document/traversal.cpp.
    void install_traversal(context & cx, script::object_object & doc);

    // Is this value the `document` object itself? By IDENTITY, because shape
    // cannot tell: the Document is the one node-like object with no handle
    // property, so `handle_of` reports the same empty handle for it as for a
    // number or a plain object.
    [[nodiscard]] bool is_the_document(value v) const;

    // DOM 4.4, "locate a namespace", run at an ELEMENT. `prefix` is the null
    // prefix when the pointer is null, which is what `lookupNamespaceURI(null)`
    // and `isDefaultNamespace` both ask for. An empty answer IS the null
    // namespace and reports as `null`.
    [[nodiscard]] std::string locate_namespace(node_id element, const std::string * prefix);
    // DOM 4.4, "locate a namespace prefix". Empty means no prefix was found.
    [[nodiscard]] std::string locate_namespace_prefix(node_id element, const std::string & ns);

    // `normalize()`: merge adjacent Text children and drop empty ones, over a
    // whole subtree. Reads the shape out first and mutates afterwards - a
    // structural write inside a live read_txn is a shape nothing else in these
    // bindings has.
    void normalize_subtree(node_id root);
    // END selectors

    // The 2D context. Its methods close over the canvas node, so the object can
    // be stored and reused - which is what every canvas page does.
    // What a page can pass to drawImage: a loadImage() handle (a number) or an
    // <img> element wrapper. Anything else is nothing to draw.
    [[nodiscard]] std::shared_ptr<const paint::bitmap> image_argument(value v);

    // A DOMMatrix over a transform: the six numbers plus the methods a page
    // composes them with. See the getTransform binding for why six bare numbers
    // was not enough.
    [[nodiscard]] value matrix_object(context & cx, const transform & t);

    [[nodiscard]] value canvas_context_object(context & cx, node_id id);

    // `canvas.getContext('webgl')`. The JavaScript surface is in its own file -
    // seventy-nine methods and a constant table would bury the DOM in this one -
    // and the state machine it drives is in shell/page/webgl.hpp.
    [[nodiscard]] value webgl_context_object(context & cx, node_id id, int version);
    // Its three halves, called in this order: the constant table, then every
    // method. See lib/Shell/bindings/webgl/.
    void install_webgl_constants(script::object_object * obj, bool webgl2);
    void install_webgl_methods(context & cx, script::object_object * obj, webgl_context * gl,
                               canvas_context * surface);
    void install_webgl_draw_methods(context & cx, script::object_object * obj, webgl_context * gl,
                                    canvas_context * surface, int width, int height, bool webgl2);

    // SETTING canvas.width RESIZES THE DRAWING BUFFER, and for a WebGL canvas
    // that is not cosmetic: canvas_context::resize REALLOCATES the bitmap, so a
    // context still holding the old pointer is drawing into freed memory.
    void resize_webgl_context(node_id id, int width, int height);
    // Copy every live WebGL context's surface into its canvas bitmap. Called
    // once at the end of a frame, never per draw.
    void present_webgl_contexts();

    // ONE CONTEXT PER CANVAS, kept for the document's life. getContext is
    // idempotent in the spec: a page that calls it twice gets the same object
    // with the same buffers and programs still bound, and a fresh one each time
    // would quietly lose everything it had uploaded.
    flat_map<std::uint64_t, std::unique_ptr<webgl_context>> webgl_contexts_;
    // Whether a NEW WebGL context should go on the ANGLE backend. Stage 2 of
    // docs/plans/angle.md keeps both paths alive, and this is the switch.
    bool angle_preferred_ = false;
    // The JS wrapper for each, so getContext hands back the SAME object - and a
    // GC root, because the page may drop its reference and ask again.
    flat_map<std::uint64_t, script::object_object *> webgl_objects_;

    [[nodiscard]] static float number(std::span<value> args, std::size_t i);

    // "bold 16px sans-serif" -> 16.
    [[nodiscard]] static float font_size_from(std::string_view font);

    // ...and -> family "sans-serif", bold, not italic.
    //
    // An honest subset of the CSS `font` shorthand: tokens before the <n>px one
    // supply bold/italic, and the first entry of the family list after it is
    // the family. Not handled, and not pretended to be: `font-weight: 700` as a
    // number, `<size>/<line-height>`, and keyword sizes like `medium`.
    static void font_face_from(std::string_view font, std::string & family, bool & bold,
                               bool & italic);

    [[nodiscard]] node_id handle_of(value v);

    [[nodiscard]] std::string text_of(node_id id) const;

    void set_text(node_id id, std::string text);

    [[nodiscard]] static std::vector<std::string_view> split(std::string_view text);

    void mutated();

    // --- globals ----------------------------------------------------------

    void install_console(context & cx);

    void install_document(context & cx);

    // `alert` and `location`.
    void install_navigation(context & cx);

    value make_location(context & cx);

    [[nodiscard]] script::object_object * document_object();
    [[nodiscard]] script::object_object * window_object();

    void install_window(context & cx);

    // --- images and fetch --------------------------------------------------

    void install_resources(context & cx);

    // `performance`: `now`, the entry list and `getEntries*` over it, plus the
    // `PerformanceEntry` and `PerformancePaintTiming` globals. Returns the
    // object so install_window can hang it on the window and the global scope.
    // Its own file: lib/Shell/bindings/performance.cpp.
    [[nodiscard]] script::object_object * install_performance(context & cx);

    // A FETCH THAT HAS NOT HAPPENED YET: the work finishes on a later turn, so
    // other timers and listeners run meanwhile and an AbortController has
    // something to abort.
    struct pending_fetch {
        value promise;
        std::string url;
        value signal; // the AbortSignal it was given, if any
    };
    std::vector<pending_fetch> fetches_;

    // AN IMAGE LOAD THAT HAS NOT HAPPENED YET, for the same reason a fetch is
    // one: `img.src = url` returns immediately and the page hears about it
    // through `onload` on a later turn. Firing synchronously from the setter
    // would work for the way p5 writes it - handlers assigned before src - and
    // break `img.src = url; img.onload = f`, which fires nothing at all.
    struct pending_image {
        value target; // the <img> wrapper whose src was assigned
        node_id id;
        std::string url;
        value promise; // decode()'s promise; undefined for a plain src assignment
    };
    std::vector<pending_image> image_loads_;

    // A FRAME WHOSE `load` HAS NOT BEEN ANNOUNCED YET. The document is built
    // synchronously - the bytes are already on disk or in the registry - but
    // the EVENT is not, for the same reason an image's is not: `document.body
    // .appendChild(frame)` is followed by `frame.onload = f` often enough that
    // firing from the insertion would fire at nothing.
    struct pending_frame {
        node_id id;
        bool ok = false; // false when the src resolved to no bytes
        // A sheet or script announcing its `load`, as opposed to an <iframe>:
        // not a callback the page scheduled, so the drain does not count it.
        bool resource = false;
        // The bindings whose element `id` names when it is not the queue's
        // own: a frame document's nested frame lands on the primary's queue.
        dom_bindings * owner = nullptr;
    };
    std::vector<pending_frame> frame_loads_;
    // Which frames are loaded, and from what. The `src` is kept as WRITTEN
    // rather than resolved, because that is the string the next reconcile
    // compares against - a page that assigns the same src twice must not
    // reload, and one that assigns a different one must.
    struct frame_entry {
        std::uint64_t key; // pack(id) of the <iframe>
        std::string src;
        dom_bindings * bindings; // over the frame's document
    };
    std::vector<frame_entry> frames_;
    // Set by `mutated()` and by the first tick after a parse. Without it the
    // reconcile walks the whole tree on every frame of an idle page, which is
    // exactly what "a frame runs only what changed" forbids.
    bool frames_dirty_ = true;
    // Returns the bindings over the frame's document (null when no wrapper).
    dom_bindings * load_frame(context & cx, node_id id, const std::string & src);
    void settle_frame(context & cx, const pending_frame & waiting);
    // The content type a path implies, since there is no server here to send
    // one. Empty for a name this engine has no type for.
    [[nodiscard]] static std::string_view mime_for_path(std::string_view path);

    // A FileReader's read, which finishes on a LATER TURN for the same reason an
    // image load does: a page assigns `onload` after calling readAsText, so a
    // reader that delivered synchronously would fire before the handler existed.
    enum class read_kind : std::uint8_t {
        text,
        data_url,
        array_buffer,
        binary_string
    };
    struct pending_read {
        value reader;
        value blob;
        read_kind kind;
    };
    std::vector<pending_read> reads_;
    void settle_read(context & cx, const pending_read & waiting);

    // Resolve the bytes, set `complete`, and announce it - `onload` and any
    // `load` listener, or the error pair.
    void settle_image(context & cx, const pending_image & waiting);

    // Queue one. `promise` is undefined unless decode() asked for it.
    void begin_image_load(value target, node_id id, std::string url, value promise);

    // The loading surface an <img> has beyond a plain element: src, the size
    // that falls back to the decoded pixels, complete, decode().
    void install_image_views(context & cx, script::object_object & obj, node_id id);

    // Do the work for one queued fetch and settle its promise. Called from the
    // event loop, not from fetch().
    void settle_fetch(context & cx, const pending_fetch & waiting);

    [[nodiscard]] value fetch_now(context & cx, const std::string & url);

    [[nodiscard]] static value make_rejection(context & cx, const std::string & message);

    // The Response object.
    //
    // The BODY methods hand back settled promises: the bytes are already in
    // hand by the time a Response exists, so there is nothing to wait for. It is
    // the fetch itself that is asynchronous, which is the part a page can
    // observe.
    [[nodiscard]] value make_response(context & cx, const std::string & url, int status,
                                      const std::string & content_type,
                                      std::vector<std::byte> body) {
        auto * response = cx.allocate<script::object_object>();
        response->set("url", cx.string(url));
        response->set("status", value::number(status));
        response->set("ok", value::boolean(status >= 200 && status < 300));
        response->set("statusText", cx.string(status == 200   ? "OK"
                                              : status == 404 ? "Not Found"
                                                              : ""));
        response->set("type", cx.string("basic"));
        // `headers` IS AN OBJECT with get() and has(), not a string. A page does
        // `res.headers.get('content-type')`, and the only header this engine
        // knows is the content type - so it answers that one and reports every
        // other as absent rather than pretending.
        {
            auto * headers = cx.allocate<script::object_object>();
            headers->set("__contentType", cx.string(content_type));
            const auto is_content_type = [](std::string_view wanted) {
                return ascii_iequals(wanted, "content-type");
            };
            set_method(cx, *headers, "get",
                       [content_type, is_content_type](context & c, std::span<value> a) {
                           if (!is_content_type(arg_string(c, a, 0)) || content_type.empty()) {
                               return value::null();
                           }
                           return c.string(content_type);
                       });
            set_method(cx, *headers, "has",
                       [content_type, is_content_type](context & c, std::span<value> a) {
                           return value::boolean(is_content_type(arg_string(c, a, 0)) &&
                                                 !content_type.empty());
                       });
            response->set("headers", value::object(headers));
        }

        const std::string text{reinterpret_cast<const char *>(body.data()), body.size()};
        set_method(cx, *response, "text", [text](context & c, std::span<value>) {
            return c.make_promise(c.string(text), false);
        });
        set_method(cx, *response, "json", [text](context & c, std::span<value>) {
            // Through the standard library's JSON.parse, so one parser decides
            // what JSON means here.
            const value parser = c.global("JSON");
            if (parser.is_object()) {
                if (value * parse =
                        static_cast<script::object_object *>(parser.as_heap())->find("parse")) {
                    const value text_value = c.string(text);
                    const value args[1] = {text_value};
                    return c.make_promise(c.call(*parse, args), false);
                }
            }
            return c.make_promise(value::undefined(), false);
        });
        // The bytes, three ways a caller may ask for them. `bytes()` is the
        // newest and p5 prefers it when present; `arrayBuffer()` is what
        // everything else uses, and `blob()` is what an object URL is made from.
        const auto byte_array = [](context & c, const std::vector<std::byte> & bytes) {
            const value out = c.make_array();
            auto * items = static_cast<script::array_object *>(out.as_heap());
            items->elements = script::element_kind::u8;
            items->items.reserve(bytes.size());
            for (const std::byte b : bytes) {
                items->items.push_back(value::number(static_cast<double>(std::to_integer<int>(b))));
            }
            return out;
        };
        set_method(cx, *response, "bytes", [body, byte_array](context & c, std::span<value>) {
            return c.make_promise(byte_array(c, body), false);
        });
        set_method(cx, *response, "arrayBuffer", [body, byte_array](context & c, std::span<value>) {
            // The shape install_typed_arrays recognises: an object carrying
            // `__bytes`, so `new Uint8Array(buffer)` is a view over THIS
            // storage rather than a copy of it.
            auto * buffer = c.allocate<script::object_object>();
            buffer->set("byteLength", value::number(static_cast<double>(body.size())));
            buffer->set("length", value::number(static_cast<double>(body.size())));
            buffer->set("__bytes", byte_array(c, body));
            return c.make_promise(value::object(buffer), false);
        });
        set_method(cx, *response, "blob",
                   [this, body, content_type, byte_array](context & c, std::span<value>) {
                       // A minimal Blob: its size, its type and its bytes. Enough for a
                       // page that hands one to URL.createObjectURL, which is the only
                       // thing anything here does with one.
                       auto * blob = c.allocate<script::object_object>();
                       // A REAL Blob - `instanceof Blob` was false, and p5's loadBlob
                       // probe only ever read as passing because the throw in its
                       // `.then` was lost rather than delivered as a rejection.
                       if (blob_prototype_.is_object()) { blob->prototype = blob_prototype_; }
                       blob->set("size", value::number(static_cast<double>(body.size())));
                       blob->set("type", c.string(content_type));
                       blob->set("__bytes", byte_array(c, body));
                       return c.make_promise(value::object(blob), false);
                   });
        return cx.make_promise(value::object(response), false);
    }

    void install_timers(context & cx);

    [[nodiscard]] std::uint32_t add_timer(value callback, double delay_ms, bool repeating);

    // --- events -----------------------------------------------------------

    [[nodiscard]] value make_event(context & cx, std::string_view type, node_id target);
    // The shared Event builder: everything `new Event`, `document.createEvent`
    // and the engine's own input events have in common. `bubbles` and
    // `cancelable` are the two flags that change what dispatch does.
    [[nodiscard]] value make_event_object(context & cx, std::string_view type, bool bubbles,
                                          bool cancelable);
    // WHAT `passive` MEANS WHEN THE PAGE DID NOT SAY -
    // https://dom.spec.whatwg.org/#default-passive-value. The member has no
    // default in the IDL: a listener for one of the four SCROLL-BLOCKING types
    // registered on the window, the document, the document element or the body
    // is passive unless the page asked for otherwise, and passive everywhere
    // else means only what was asked for. It is not a hint - the canceled flag
    // is not set while such a listener runs - so getting it wrong makes
    // `preventDefault` work where it must not.
    [[nodiscard]] bool default_passive_value(std::string_view type, const path_step & target);
    // The `error` event a faulting callback produces, carrying the VALUE the
    // throw left behind beside its text. `dispatch_error` is this with no value,
    // which is what a fault that was never an exception has to hand a page.
    bool dispatch_error_value(std::string_view message, value error);
    // The MouseEvent (or PointerEvent) the engine sends for one input event:
    // the coordinates, the button and the modifiers, on the right prototype so
    // dispatch can tell it from a plain `new Event("click")`.
    [[nodiscard]] value make_mouse_event(context & cx, std::string_view type, node_id target,
                                         const input_event & input, bool pointer);
    // DOM 2.9 dispatch, the activation half. Which node on the path has
    // activation behaviour - HTML's list: <a href>, <area href>, <button>,
    // <input>, <label>, <summary> - and what happens at it after the listeners
    // ran. See the definitions in events/dispatch.cpp.
    [[nodiscard]] bool has_activation_behavior(const read_txn & txn, node_id node) const;
    void run_activation_behavior(context & cx, node_id target);
    // `Event`, `CustomEvent` and `EventTarget` as globals, and the prototype an
    // event object is linked to so `instanceof` and the phase constants work.
    void install_event_interfaces(context & cx);
    // THE DISPATCH ALGORITHM, over a path rather than over a node chain. See the
    // definition: capture from the window down, then bubble back up, with
    // `currentTarget` and `eventPhase` set for each step and the propagation
    // flags checked between them. Returns whether a listener cancelled it.
    bool dispatch_to(value event, path_step at);
    // Where an event aimed at `at` travels: the node and its ancestors, then the
    // document, then the window - innermost first. A `composed` event crosses
    // each shadow boundary to the host; one that is not stops at the shadow
    // root. A detached tree ends at its own root and reaches neither the
    // document nor the window.
    [[nodiscard]] std::vector<path_step> propagation_path(path_step at,
                                                          bool composed = false) const;
    // The JavaScript object for one step, which is what `currentTarget` reports
    // and what an `on<type>` handler property is looked up on.
    [[nodiscard]] value object_of_step(context & cx, path_step step);
    // AND BACK AGAIN: which event target a value IS. `object_of_step` is the
    // other direction and the two have to agree. It exists because
    // EventTarget.prototype's three methods are INHERITED by every node
    // wrapper - the interface chain ends at EventTarget - so `this` inside
    // `addEventListener` is as often a node as it is a standalone target, and
    // reading it as standalone gives an element a listener list no dispatch
    // through the tree ever visits.
    [[nodiscard]] path_step step_of(value self);

    [[nodiscard]] static bool prevented(value event);

    void fire_at(path_step step, std::string_view type, value event, bool capturing);

    // `onclick`, `onload` - the handler PROPERTY, run after the listeners. True
    // when it threw, with the thrown value written through `thrown` if a caller
    // asked for it; a caller that does not is one with nowhere to report to.
    bool fire_handler_property(value target, std::string_view type, value event,
                               value * thrown = nullptr);
    [[nodiscard]] value value_of_wrapper(node_id id) const;

    // --- lookups ----------------------------------------------------------

    [[nodiscard]] node_id find_by_tag(std::string_view tag);
    // Every element with this tag, in document order; "*" means all of them.
    [[nodiscard]] std::vector<node_id> all_by_tag(std::string_view tag);
    // Every element below `root` whose class attribute holds every one of
    // `tokens`, in document order. An EMPTY `root` means the whole document and
    // includes the document's own root element - which is `<html>` here, this
    // tree builder having no Document node above it. A given `root` is an
    // element and is excluded, a search being over descendants. An empty
    // `tokens` matches nothing, which is what the ordered set parser leaves
    // behind for an all-whitespace argument and what the DOM says the answer is.
    [[nodiscard]] std::vector<node_id> all_by_class(node_id root,
                                                    const std::vector<std::string> & tokens);
    // Every HTML element in the document whose `name` attribute is exactly
    // `name`, in document order. HTML only: `document.getElementsByName` is an
    // HTML method and an SVG element carrying `name=` is not one of its answers.
    [[nodiscard]] std::vector<node_id> all_by_name(std::string_view name);

    // --- THE HTML TREE ACCESSORS (bindings/document/collections.cpp) ------
    //
    // `find_by_tag` matches on the TAG ATOM, and that is the wrong question for
    // anything HTML defines: `<title>` inside `<svg>` interns to the same atom
    // as the document's own, and the tokenizer keeps foreign content's case so
    // an SVG `<clipPath>` is a different atom from an HTML one. Everything HTML
    // names - the title element, the head, `document.images` - is a LOCAL NAME
    // in the HTML NAMESPACE, so these two ask that instead.
    //
    // The local name is what follows the first colon, because
    // `createElementNS(HTML, "blah:title")` really is a title element: DOM
    // "validate and extract" puts the prefix before the colon and the local
    // name after it, and HTML's definitions are all in terms of the latter.
    [[nodiscard]] node_id first_html_element(std::string_view local);
    [[nodiscard]] std::vector<node_id> all_html_elements(std::string_view local);
    // "THE TITLE ELEMENT", which is not simply the first `<title>`: in a
    // document whose root is an SVG `<svg>` it is that root's first SVG
    // `<title>` CHILD, and in every other document it is the first HTML title
    // element anywhere in tree order. Empty when there is none.
    [[nodiscard]] node_id title_element();
    // "The body element": the first child of the DOCUMENT ELEMENT that is a
    // `body` or a `frameset`. Not the first `<body>` anywhere.
    [[nodiscard]] node_id body_element();

    // --- A SECOND DOCUMENT (bindings/document/second_document.cpp) ---------
    //
    // `createHTMLDocument` and `createDocument` return one: a SECOND
    // dom_bindings over its own tree, in the same realm. Every per-node key -
    // `wrappers_`, `namespaces_` - is a member, so a second instance has a
    // second set of them. What it shares with the primary is the atom table,
    // the script context, and the INTERFACE OBJECTS, so that
    // `otherDoc.createElement("div") instanceof HTMLDivElement` is true against
    // the one `HTMLDivElement` a page can see.
    //
    // `importNode` and `adoptNode` do not cross between two documents; a node
    // of one passed to the other is REFUSED rather than misread - see
    // `handle_of`, which checks that the wrapper it was given is one of ours.
    [[nodiscard]] value make_html_document(context & cx, const std::string * title);
    // `as_xml_document` picks the interface: `createDocument` returns an
    // XMLDocument and `new Document()` a plain Document, and the two differ in
    // nothing else - DOM 4.5.1 and 4.5 respectively.
    [[nodiscard]] value make_xml_document(context & cx, std::string_view ns,
                                          std::string_view qualified_name,
                                          bool as_xml_document = true);
    // WHICH BINDINGS A WRAPPER BELONGS TO: this one, the primary, or one of the
    // primary's other secondaries. Null for anything that is not a node of any
    // document in the realm. `handle_of` answers only for this one's own
    // wrappers, which is the refusal `importNode` has to get past.
    [[nodiscard]] dom_bindings * owner_of(value v);
    // Is this the `document` of ANY bindings in the realm? `is_the_document`
    // is identity against this one's; a Document is refused by importNode and
    // adoptNode whichever document it is.
    [[nodiscard]] bool is_a_document(value v) const;
    // The realm has ONE external-roots callback - `set_external_roots` replaces
    // rather than appends - so the primary's walks itself and then every
    // secondary. A secondary never registers.
    void mark_roots(const script::context::root_visitor & mark) const;
    // Take the primary's interface prototypes rather than building a second set
    // of globals: `install_dom_interfaces` DEFINES `HTMLDivElement` and its
    // ninety neighbours, and running it twice would leave two of each and break
    // every `instanceof` taken across the two documents.
    void adopt_interfaces_of(const dom_bindings & primary);
    // `document.title`, `document.images` and the seven collections beside it,
    // all as ACCESSORS - see the definition for why not one of them can be a
    // property refreshed on the tick.
    void install_tree_accessors(context & cx, script::object_object & doc);
    // HTML's "named access on the Document object" - the named elements with a
    // given name, in tree order. `embed`, `form`, `iframe`, `img` and `object`
    // by their `name`; `object` by its `id`; and `img` by its `id` ONLY when it
    // also carries a non-empty `name`, which is the asymmetry
    // `nameditem-01.html` tests by removing one attribute at a time.
    [[nodiscard]] std::vector<node_id> named_document_items(std::string_view name);
    // The other direction: every name the rule above answers to, once each, in
    // tree order - the document's "supported property names".
    [[nodiscard]] std::vector<std::string> document_property_names();
    // The Proxy a page sees as `document`. Installs the `get`, `has`, `ownKeys`
    // and `getOwnPropertyDescriptor` traps over `document_target_` and returns
    // it.
    [[nodiscard]] value make_document_proxy(context & cx, value target);
    // The DOM's ORDERED SET PARSER: split on ASCII whitespace - space, tab, LF,
    // FF and CR, all five - and drop duplicates. `split` above splits on spaces
    // alone, which is right for nothing in particular and wrong for a class
    // attribute written across two lines.
    [[nodiscard]] static std::vector<std::string> ordered_set(std::string_view text);
    // A COLLECTION THAT IS LIVE, which is the whole difficulty. `getElementsBy*`
    // returns a view of the document rather than a snapshot of it: a page takes
    // the collection, appends an element, and reads `length` again expecting the
    // new number. An array cannot answer that, so this is a Proxy whose `get`
    // and `has` traps re-run `members` on every read - the same mechanism the
    // `window` proxy already uses, and the reason a second one is cheap.
    //
    // WHICH INTERFACE it claims to be is a parameter, because two of the DOM's
    // live collections are the same object with different names on it:
    // `getElementsByTagName` is an HTMLCollection and `getElementsByName` is a
    // NodeList, and `document.getElementsByName-liveness.html` asserts
    // `e instanceof NodeList` before it checks a single length.
    [[nodiscard]] value make_live_collection(context & cx,
                                             std::function<std::vector<node_id>()> members,
                                             std::string_view interface_name = "HTMLCollection");
    // `querySelectorAll`, on the real Selectors engine - see the definition.
    //
    // `invalid` comes back true when the text is not a selector at all, which is
    // the only case `querySelector` may throw SyntaxError for. It is an out
    // parameter rather than a throw so that this stays callable from a place that
    // has no context, and defaulted so the four call sites that predate it do not
    // have to care.
    [[nodiscard]] std::vector<node_id> query(std::string_view selector, node_id within = node_id{},
                                             bool * invalid = nullptr, bool first_only = false);
    // The document's own live properties - title and activeElement.
    void refresh_document();

    document * doc_;
    atom_table * atoms_;
    canvas_store * canvases_;
    form_store * forms_;
    std::function<void()> on_mutation_;
    std::function<void(node_id)> on_focus_;
    std::function<void(const std::string &)> on_alert_;
    std::function<void(node_id)> on_activate_;
    // WHERE AN ARBITRARY NAMESPACE URI LIVES. `node` carries a three-valued
    // `node_ns` and not a URI, for the size reason written down beside the
    // enumerator; the handful of elements a page creates with createElementNS in
    // a namespace that is neither HTML nor SVG keep their URI here, keyed the
    // same way a wrapper is.
    flat_map<std::uint64_t, std::string> namespaces_;
    flat_map<std::uint64_t, script::object_object *> wrappers_;
    // [[CryptographicNonce]], HTML 2.6.1: what `el.nonce = x` wrote, paired with
    // the `nonce` attribute's text at the time - see reflection.cpp's
    // `cryptographic_nonce` for why the pair. Empty until a page assigns one.
    flat_map<std::uint64_t, std::pair<std::string, std::string>> nonce_slots_;
    bool wrote_to_control_ = false;
    // How many dispatches are on the stack. A listener may dispatch, and the
    // inner dispatch must not compact the listener list the outer one is walking.
    std::size_t dispatch_depth_ = 0;
    // What the browser last told us has focus, for document.activeElement.
    node_id focused_;
    std::string location_href_;
    std::string location_hash_;
    // The element the fragment names - `:target` - see observe_location.
    node_id target_element_;
    // DOMException.prototype, held here as well as on the global for the reason
    // blob_prototype_ is: a page can delete a global, and an exception whose
    // prototype was collected stops being a DOMException.
    value dom_exception_prototype_;
    // The `CSS` namespace object, held for the reason above: a page can delete
    // the global and `CSS.supports` must still be the same function afterwards.
    value css_interface_;
    value location_;
    // THE DOCUMENT IS TWO VALUES, and which one a caller wants is not a detail.
    //
    // `document_` is what a PAGE holds: a Proxy, because HTML's named access
    // (`document.someImgName`) has to answer for a name nobody ever defined as
    // a property and has to STOP answering the moment the attribute behind it
    // is removed. It is therefore what `ownerDocument`, `getRootNode` and every
    // identity comparison must use, or a page's `document` and the engine's
    // are two different objects.
    //
    // `document_target_` is the object BEHIND it, which is where every property
    // the bindings install actually lives. `document_object()` returns it, so
    // everything that writes a property on the document keeps working
    // unchanged; the proxy falls through to it for every name that is not a
    // named element.
    value document_;
    value document_target_;
    value window_;
    // A DOCUMENT THIS ONE MADE, and the bindings over it. Only ever non-empty
    // on the primary; a secondary makes no further documents because
    // `document.implementation` is not installed on one.
    std::vector<std::unique_ptr<document>> owned_documents_;
    std::vector<std::unique_ptr<dom_bindings>> secondary_documents_;
    // Is this the bindings for a document a page MADE? It changes three things
    // and nothing else: no `document` global, no `location`/`defaultView`, and
    // no `document.implementation` - a document from createHTMLDocument has a
    // null browsing context, so all three are what the DOM already says.
    bool secondary_ = false;
    // The bindings that made this one, for a secondary; null on the primary.
    // What `owner_of` walks up through to find the other documents.
    dom_bindings * primary_ = nullptr;
    // --- XML documents ---
    // `document.contentType`, WHEN IT IS NOT DERIVABLE. A parsed document
    // answers from `document::xml()` and needs nothing here; `createDocument`
    // does not, because DOM 4.5.1 makes the string depend on the NAMESPACE it
    // was given - "application/xml", "application/xhtml+xml" or
    // "image/svg+xml" - and the namespace is an argument that is gone by the
    // time the property is installed. Empty means "derive it".
    std::string content_type_;
    // `document.cookie`, in insertion order so reading it back is stable.
    std::vector<std::pair<std::string, std::string>> cookies_;
    // Counts the object URLs handed out, so each is distinct. Counted rather
    // than random for the same reason Math.random is seeded: a page that prints
    // one could not otherwise have a golden.
    std::uint32_t next_object_url_ = 0;
    // Blob.prototype, kept so canvas.toBlob's Blob is one too - `x instanceof
    // Blob` has to be true whoever made it.
    value blob_prototype_;

    // THE CONTEXT INTERFACE OBJECTS - `window.CanvasRenderingContext2D` and
    // friends; the element interfaces live in the table (interface_prototypes_).
    //
    // A browser exposes one per interface, and libraries use them two ways that
    // both have to work: feature detection (`!!window.CanvasRenderingContext2D`)
    // and identity (`ctx instanceof CanvasRenderingContext2D`). A bare marker
    // object satisfies the first and makes the second silently FALSE.
    //
    // So each carries a real `prototype`, and the objects that are instances get
    // that prototype linked. See interface_prototype().
    // `Event.prototype` and `CustomEvent.prototype`. Every event object this
    // engine makes is linked to one, which is what carries `e.constructor`,
    // `e instanceof Event` and the four phase constants a page reads as
    // `e.AT_TARGET` rather than as `Event.AT_TARGET`.
    value event_prototype_;
    value custom_event_prototype_;
    // `MouseEvent.prototype` and `PointerEvent.prototype`: what an engine mouse
    // event is linked to, and what dispatch checks a `click` against before
    // running activation behaviour - a `new Event("click")` toggles nothing.
    value mouse_event_prototype_;
    value pointer_event_prototype_;
    // `EventTarget.prototype`, where the three methods a standalone target
    // inherits live.
    value event_target_prototype_;
    value canvas2d_prototype_;
    value webgl_prototype_;
    // A SEPARATE INTERFACE, not a subclass. `WebGL2RenderingContext` does not
    // inherit from `WebGLRenderingContext` in the specification, so a real
    // WebGL 2 context is NOT `instanceof WebGLRenderingContext` - and a page
    // that tests for one to decide which path to take (Phaser does) must get
    // the same answer here as it would in a browser.
    value webgl2_prototype_;
    std::string callback_error_;
    bool reload_requested_ = false;
    asset_registry * assets_ = nullptr;
    image_store * images_ = nullptr;
    bool network_allowed_ = true;
    context * cx_ = nullptr;
    const layout::fragment * fragments_ = nullptr;
    const style::style_map * styles_ = nullptr;
    const layout::box_node * boxes_ = nullptr;
    int viewport_width_ = 0;
    int viewport_height_ = 0;
    // A POSITIVE TIME ORIGIN, not zero. `performance.now()` and every event's
    // `timeStamp` read this, and `dom/events/Event-constructors.any.js` asserts
    // `timeStamp > 0` twice - which is the only thing between that file and a
    // pass. Zero is also not what a browser reports: the origin is when the
    // document began loading and script runs after that, so a page reading
    // `performance.now()` on its first line sees a small positive number
    // everywhere else.
    //
    // A FIXED number rather than a real one, for the reason `Math.random` is
    // seeded: three example pages byte-compare their render against a golden,
    // and a clock that differs run to run cannot have one.
    double now_ms_ = 1;
    // `performance.getEntries()`. Plain data: the objects a page sees are built
    // when it asks, with PerformancePaintTiming.prototype behind them.
    struct performance_entry {
        std::string name;
        std::string type;
        double start_ms;
    };
    std::vector<performance_entry> performance_entries_;

    std::vector<listener> listeners_;
    std::vector<timer> timers_;
    std::vector<value> animation_callbacks_;
    std::vector<std::string> console_;
    std::uint32_t next_timer_id_ = 0;

    // --- the listener fence ---
    //
    // THE FENCE EVERY LISTENER IS CALLED BEHIND, and it is a JavaScript
    // function rather than a C++ try because the exception it has to stop is
    // not a C++ one.
    //
    // `context::throw_error` unwinds to the innermost live `try` ANYWHERE
    // below it on the stack - `handlers_` is one list for the whole VM - so
    // without a fence a listener that threw would land in whatever `try` the
    // page happened to be inside. The DOM says "if this throws an exception,
    // then report the exception": the dispatch continues and the page is told
    // through an `error` event.
    //
    // A `try` INSIDE the callee is the only thing the VM's unwinder stops at,
    // so the fence is
    //
    //     function (invoke, callback, receiver, args) {
    //         try { ...call it... } catch (e) { return [e]; }
    //         return null;
    //     }
    //
    // compiled once per page. It is also where WebIDL's "call a user object's
    // operation" lives, because both halves of that algorithm can throw and
    // both have to be INSIDE the fence: the `handleEvent` Get - which a page
    // may make an accessor - and the TypeError for a listener object whose
    // `handleEvent` is not callable.
    value listener_fence_;
    // The native the fence calls back into: `invoke(fn, receiver, args)`, where
    // `args` is one value or an array of them. Kept because the window's
    // `onerror` takes five positional arguments rather than the event.
    value listener_invoke_;
    // Build the native at install and the fence at the first dispatch - see
    // compile_listener_fence for why not sooner. A compile failure leaves the
    // fence undefined and the unfenced C++ path stands in.
    void install_listener_fence(context & cx, script::native_object & keeper);
    void compile_listener_fence(context & cx);
    // The native that keeps both alive: the EventTarget constructor.
    script::native_object * fence_keeper_ = nullptr;
    // Call one listener - a function, or an object with a `handleEvent` - with
    // `this` bound to `receiver`. True when it threw, with the thrown value in
    // `thrown`; a VM fault that was never an exception reports false and leaves
    // `context::failed()` set, as it always did. `returned` is what the callee
    // returned when that was a BOOLEAN and undefined otherwise - the one part
    // of a return value HTML's "processing the return value" reads, and the
    // one part the fence can hand back without an allocation per call.
    [[nodiscard]] bool invoke_listener(context & cx, value callback, value receiver, value args,
                                       value & thrown, value & returned);

    // --- event handler IDL attributes (HTML 8.1.7.2) -----------------------
    //
    // `el.onclick`, `document.onclick`, `window.onload`: an ACCESSOR on the
    // interface prototype, null when unset, and the setter takes only an
    // object - `el.onclick = ""` stores null, which is what
    // Body-FrameSet-Event-Handlers.html spends a third of its assertions on.
    void install_event_handler_attributes(context & cx);
    // The handler currently registered for `name` on `self`, compiling the
    // content attribute if that is where it still is.
    [[nodiscard]] value event_handler_get(context & cx, value self, const std::string & name);
    void event_handler_set(context & cx, value self, const std::string & name, value given);
    // `onclick="doThing()"` as a function, compiled once and cached on the
    // object it belongs to. Undefined when the attribute is absent or will not
    // compile - HTML says a handler that fails to compile is null.
    [[nodiscard]] value compile_handler_attribute(context & cx, value self,
                                                  const std::string & name);

    // --- shadow DOM adapters over the owning document ---
    using shadow_tree = document::shadow_tree;

    [[nodiscard]] node_id shadow_root_of(node_id host) const;
    [[nodiscard]] const shadow_tree * shadow_tree_of(node_id root) const;
    // `element.attachShadow(init)`, DOM 4.8. Answers the ShadowRoot, or
    // undefined HAVING ALREADY THROWN - a TypeError for a missing or unknown
    // `mode`, a NotSupportedError for a second attach or for an element that
    // cannot host one.
    [[nodiscard]] value attach_shadow(context & cx, node_id host, std::span<value> args);
    // The members a ShadowRoot has that an ordinary DocumentFragment does not.
    // Installed from wrap(), AFTER the prototype link, so the two it
    // replaces - querySelector and querySelectorAll, which have to search a
    // DETACHED subtree - overwrite the general ones rather than race them.
    void install_shadow_root_members(context & cx, script::object_object & obj, node_id root);
    // "Shadow-including root", DOM 4.4: the top of the tree `from` is in, and
    // with `composed` the walk continues through each shadow host rather than
    // stopping at the ShadowRoot.
    [[nodiscard]] node_id root_of_tree(const read_txn & txn, node_id from, bool composed) const;

    // HTML's window-reflecting body element event handler set: `body.onload`
    // is the window's, and a `<body onload>` content attribute is compiled onto
    // the window. Lazy - checked on read - because there is no attribute-change
    // hook; see events/dispatch.cpp. `forwarded_from_` is the element whose
    // attribute last supplied each window handler.
    [[nodiscard]] node_id body_or_frameset_of(value self);
    void refresh_forwarded_handler(context & cx, node_id element, const std::string & name);
    flat_map<std::string, node_id> forwarded_from_;
    std::vector<node_id> bodies_; // every wrapped body/frameset, rebuilt when a wrapper is made
    std::size_t bodies_scanned_at_ = static_cast<std::size_t>(-1);
    // Which bindings an EventTarget receiver belongs to - `owner_of` for a
    // node, the document's own for a Document, else this. The EventTarget
    // methods route through it so a second document's nodes get a path.
    [[nodiscard]] dom_bindings & target_owner(value self);

public:
    // What the browser tells the document as a load progresses.
    // `document.currentScript`: the <script> running now, or none.
    void set_current_script(node_id script);
    // `document.readyState`, with `readystatechange` at the document when it
    // changes.
    void set_ready_state(std::string_view state);
    // A FocusEvent at `target` naming `related` (the element focus came from
    // or went to): `focus`/`blur` do not bubble, `focusin`/`focusout` do.
    bool dispatch_focus(std::string_view type, node_id target, node_id related);
    // `hashchange` at the window, a HashChangeEvent with both addresses.
    bool dispatch_hash_change(const std::string & old_url, const std::string & new_url);
    // TIME AS A SCRIPT OBSERVES IT: `performance.now()` and an event's
    // timeStamp. The engine's one clock moves only between ticks, so within a
    // script two readings were equal forever and `while (performance.now() <
    // t)` never ended (Event-timestamp-safe-resolution.html spins until two
    // events differ). Each observation advances it by the 5 us a browser
    // coarsens to, counted from the tick's start - so it is still a function
    // of the page's own behaviour and a golden stays a golden. The first
    // reading of a tick is the clock itself; an event the ENGINE makes reads
    // the clock, not this.
    [[nodiscard]] double observed_now() {
        return now_ms_ + 0.005 * static_cast<double>(time_reads_++);
    }
    std::uint64_t time_reads_ = 0;
    // `contentWindow`/`contentDocument` on HTMLIFrameElement.prototype, which
    // build a not-yet-reconciled frame on demand. See frames.cpp.
    void install_frame_accessors(context & cx);
    // `ariaActiveDescendantElement` and the seven `aria*Elements` lists: HTML
    // 2.6.1's Element and FrozenArray<Element> reflection, with the explicitly
    // set attr-element kept on the wrapper. See element/reflection.cpp.
    void install_element_reflection(context & cx);
    // `progress.max` and `<meter>`'s six: HTML's double reflections, on
    // their interface prototypes. See element/reflection.cpp.
    void install_double_reflection(context & cx);
    // `option.label` and `option.value`, which fall back to the option's text.
    void install_option_reflection(context & cx);
    [[nodiscard]] value element_reference_get(context & cx, std::string_view idl,
                                              std::string_view content, bool list);
    void element_reference_set(context & cx, std::string_view idl, std::string_view content,
                               bool list, value given);
    [[nodiscard]] bool element_reference_in_scope(const read_txn & txn, node_id element,
                                                  node_id candidate) const;
    [[nodiscard]] node_id element_reference_by_id(const read_txn & txn, node_id element,
                                                  std::string_view id) const;
    // THE LAYOUT FLUSH. A box read from script - offsetX of a dispatched
    // click, getBoundingClientRect - is read from the layout AS THE SCRIPT
    // LEFT IT, which before the first frame is no layout at all. The browser
    // installs the same flush its getComputedStyle wrapper does; anything
    // reading `box_of` calls this first. Only what is stale runs.
    void set_layout_hook(std::function<void()> hook) { flush_layout_ = std::move(hook); }
    void flush_layout() {
        if (flush_layout_) { flush_layout_(); }
    }
    std::function<void()> flush_layout_;
    // SCRIPTS A PAGE MADE AND HAS NOT RUN. HTML's "prepare the script element"
    // runs when one becomes connected (the post-connection steps) or, once
    // connected, when its children change; `mutated()` is where both are
    // noticed. A parser-inserted <script> that was empty is in here too - it
    // was never started, so text appended later runs it. See document/entry.cpp.
    std::vector<node_id> unstarted_scripts_;
    void run_inserted_scripts();

public:
    void note_unstarted_script(node_id id) { unstarted_scripts_.push_back(id); }

private:
    // DOM 5, Range - bindings/document/range.cpp. `Range` the global and its
    // prototype, and the document's `createRange`.
    void install_range(context & cx);
    [[nodiscard]] value create_range(context & cx);
    // WRAPPERS THAT LEFT WITH THEIR NODE. `node_from` adopts by cloning into
    // the other document's slab and rebinding the page's wrapper to the copy;
    // the node here keeps its slot, and anything that finds it again by id -
    // `template.content` after the contents were adopted - must answer the
    // same object. Marked as roots; see wrap().
    flat_map<std::uint64_t, script::object_object *> adopted_away_;
    // `document.x` for several elements of one name is ONE live collection
    // per name (HTML 3.1.5), so `document.a === document.a` even as the
    // members change. Marked as roots.
    flat_map<std::string, value> named_collections_;
    // THE NODES A MUTATION MOVED: connected before the insertion that is
    // being announced, so their subtrees were REMOVED for a moment - which is
    // when HTML's focus fixup rule runs, and `is_connected` afterwards cannot
    // see. moveBefore does not go through this, and keeps focus. Cleared
    // after the hook.
    std::vector<node_id> moved_by_mutation_;
    // Set while `moveBefore` moves: DOM's "move" runs neither the removing
    // nor the insertion side effects an ordinary insertion has - no focus
    // fixup, and no script "children changed" steps (script-move-before.html).
    bool moving_ = false;

public:
    [[nodiscard]] std::span<const node_id> moved_by_mutation() const { return moved_by_mutation_; }

private:
    // `form` on the form-associated elements - the form owner, HTML 4.10.17.3.
    // element/reflection.cpp.
    void install_form_owner(context & cx);
    // `compareDocumentPosition` against a node or Document of ANOTHER document
    // in the realm: DISCONNECTED and IMPLEMENTATION_SPECIFIC, with the
    // direction the specification only asks to be consistent taken from the
    // order of the two bindings. Zero when `given` is not one of those.
    [[nodiscard]] unsigned foreign_document_position(value given);
    // An Attr's value accessors and ownerElement, (re)bound to `owner` - or to
    // nowhere. See element/attributes.cpp.
    void bind_attr_object(context & cx, script::object_object & attr, node_id owner,
                          const attribute & held);
    // The four parts of an Attr read off the object; an empty name when it is
    // not one. And a detached copy of one, for cloneNode and importNode.
    [[nodiscard]] attribute attribute_of_object(context & cx, value given);
    [[nodiscard]] value clone_attr_object(context & cx, value given);
    // `outerHTML`, HTML 13.2 / DOM Parsing: the element serialised WITH its own
    // tag, and the setter that parses in the parent's context and puts the
    // result in the element's place. See document/tree_ops.cpp.
    [[nodiscard]] std::string outer_html(node_id target) const;
    void set_outer_html(context & cx, node_id target, std::string_view markup);
    [[nodiscard]] std::string serialize_html(node_id target, bool outer) const;
    // "Validate and extract" for an ELEMENT name, DOM 4.9, shared by
    // createElementNS and createDocument: false having thrown the
    // InvalidCharacterError or NamespaceError the pair earns.
    [[nodiscard]] bool validate_and_extract_element(context & cx, std::string_view where,
                                                    const std::string & ns,
                                                    const std::string & qualified);
    // ONE Attr OBJECT PER (element, namespace, local name), so that
    // `el.getAttributeNode("x") === el.attributes[0]` - an Attr is a node and
    // a node has an identity. Keyed by pack(element), then by the pair; rooted
    // by mark_roots like wrappers_; an entry goes when the attribute does.
    flat_map<std::uint64_t, std::vector<std::pair<std::string, script::object_object *>>>
        attr_objects_;
    void forget_attr_object(node_id owner, std::string_view ns, std::string_view local);
    // `new DOMParser().parseFromString(markup, type)`, HTML 8.6.2: a SECOND
    // document - this document's HTML parser over `markup` for text/html, the
    // XML parser for the four XML types - as a real Document or XMLDocument in
    // the realm, so `createElement`, `documentElement.tagName` and the rest
    // answer as the type says. See document/second_document.cpp.
    [[nodiscard]] value parse_from_string(context & cx, std::string_view markup,
                                          std::string_view type);
    // The bindings for a document this one made, linked and installed - the
    // half of make_html_document and make_xml_document they share.
    dom_bindings & adopt_second_document(context & cx, document & fresh);
    // NamedNodeMap's members, on its prototype - see element/attributes.cpp.
    void install_named_node_map(context & cx);
    // "REPLACE ALL" (DOM 4.2.3), which the diff cannot see whole: `replaceChildren(x)`
    // where x was already a child queues ONE record removing every old child
    // and adding x, and the tree afterwards says only that the others went.
    // The caller notes it here before the mutated() that follows, and
    // record_mutations emits exactly this record for the parent instead of a diff.
    struct replace_all_note {
        node_id parent;
        std::vector<node_id> removed;
        std::vector<node_id> added;
    };
    std::optional<replace_all_note> replace_all_;
};

} // namespace ctbrowser::shell
