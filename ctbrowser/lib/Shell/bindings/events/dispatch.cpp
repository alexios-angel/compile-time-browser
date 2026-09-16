// dom_bindings - the dispatch algorithm: capture down the path and bubble back
// up, the listener list and its options, the event object itself, and the
// `on<type>` handler properties.

#include "internal.hpp"

#include <ctbrowser/script/compile.hpp>

namespace ctbrowser::shell {

using namespace detail;

namespace {

// WHAT A THROWN VALUE IS CALLED, for the text side of a report.
//
// `window.onerror` takes a STRING first - twenty years of shipped code reads it
// as one, and Event-dispatch-throwing.html asserts `typeof e === "string"` - so
// something has to flatten the value. An Error's `name` and `message` are data
// properties on every Error this engine makes, which is why they are read
// instead of calling `toString`: a page's own thrown object may have a
// `toString` that throws, and a reporter that faults is the one thing worse
// than a fault nobody reports. (`context::describe_thrown` is the same shape
// and is private to the VM.)
[[nodiscard]] std::string describe_thrown(context & cx, value thrown) {
    if (!thrown.is_object()) { return cx.to_string(thrown); }
    const value name = cx.lookup_property(thrown, "name");
    const value message = cx.lookup_property(thrown, "message");
    if (name.is_undefined() && message.is_undefined()) { return "an exception"; }
    std::string text = name.is_undefined() ? std::string{"Error"} : cx.to_string(name);
    const std::string body = message.is_undefined() ? std::string{} : cx.to_string(message);
    if (!body.empty()) { text += ": " + body; }
    return text;
}

} // namespace

namespace detail {

// THE ONE `isTrusted` GETTER OF THIS REALM, fetched back off Event.prototype.
//
// Every event carries it as an OWN accessor (below), and
// dom/events/Event-isTrusted.any.js compares the getters of two different
// events for identity - so a fresh native per event fails a test that a data
// property fails differently. Keeping the single copy in Event.prototype's own
// accessor table means there is no private slot to invent and nothing extra for
// the collector to be told about.
[[nodiscard]] value is_trusted_getter_of(value event_prototype) {
    if (!event_prototype.is_object()) { return value::undefined(); }
    auto * proto = static_cast<script::object_object *>(event_prototype.as_heap());
    const script::accessor_entry * entry = proto->find_accessor("isTrusted");
    return entry == nullptr ? value::undefined() : entry->getter;
}

// Everything `new Event`, `new MouseEvent`, `document.createEvent` and the
// engine's own input events have in common, written ONTO an object rather than
// into a new one.
//
// Writing onto the receiver is what makes `class SubclassedEvent extends Event`
// work: a constructor that allocates its own object leaves `super()`'s caller
// holding an instance nothing was ever written to, so every property the
// subclass inherits reads undefined. That is what
// Event-subclasses-constructors.html found, and because its assertion ran
// outside a test() the whole file reported HARNESS_ERROR rather than a failure.
//
// The METHODS are not here. They live on Event.prototype, one copy per page,
// where the specification puts them - an event used to carry seven freshly
// allocated natives of its own, which is seven allocations per mousemove.
void initialise_event(context & cx, script::object_object & event, std::string_view type,
                      bool bubbles, bool cancelable, double timestamp, value is_trusted_getter) {
    event.set("type", cx.string(std::string{type}));
    event.set("target", value::null());
    // `srcElement` is the same object under the name IE gave it, and plenty of
    // shipped code still reads it.
    event.set("srcElement", value::null());
    event.set("currentTarget", value::null());
    event.set("eventPhase", value::number(0));
    event.set("bubbles", value::boolean(bubbles));
    event.set("cancelable", value::boolean(cancelable));
    event.set("composed", value::boolean(false));
    event.set("defaultPrevented", value::boolean(false));
    event.define(std::string{timestamp_property}, value::number(timestamp), script::attr_none);
    event.set(std::string{cancel_bubble_property}, value::boolean(false));
    event.set(std::string{stop_immediate_property}, value::boolean(false));
    event.set(std::string{dispatch_property}, value::boolean(false));
    // DECLARED HERE rather than created by the first passive listener that
    // runs, so the write in fire_at updates a slot instead of growing the
    // table in the middle of a dispatch.
    event.set(std::string{passive_property}, value::boolean(false));
    // NOT trusted, and NOT initialised: both are what the page-facing paths
    // start from, and each of the two callers that knows better says so.
    event.define(trusted_property, value::boolean(false), script::attr_none);
    event.define(initialised_property, value::boolean(false), script::attr_none);
    // `isTrusted` is [LegacyUnforgeable], which WebIDL defines as an OWN
    // property of every instance sharing one getter per realm rather than an
    // inherited one. Both halves are observable: getOwnPropertyDescriptor on an
    // instance must find it, and the getters of two instances must be the same
    // function.
    //
    // `is_callable()` AND NOT `is_object()`. `value::is_object()` is
    // heap_kind::object EXACTLY, and a getter is a heap_kind::native - so the
    // obvious test is false for every getter there has ever been, and this
    // silently took the fallback below and made `isTrusted` a DATA property on
    // every event in the engine. Nothing about that looks wrong from the page
    // until something asks for the descriptor. It is the same trap that
    // context::make_instance sets for a native constructor's prototype, one
    // function up, and it is worth knowing there are two of them.
    if (is_trusted_getter.is_callable()) {
        event.define_accessor("isTrusted", is_trusted_getter, value::undefined(),
                              script::attr_enumerable);
    } else {
        event.set("isTrusted", value::boolean(false));
    }
}

} // namespace detail

// THE DISPATCH ALGORITHM: capture down the path, then bubble back up.
//
// What changed from the old three-line version, and why each half is
// load-bearing:
//
//   * `currentTarget` and `eventPhase` are set BEFORE each step's listeners run
//     and cleared after the whole dispatch. They are the two properties a
//     delegating page reads, and both read `undefined` before.
//   * the propagation flags are checked BETWEEN steps, so stopPropagation stops
//     the rest of the path, and the immediate flag is checked between listeners
//     at one step.
//   * an event whose `bubbles` is false runs the bubble pass at the TARGET only,
//     which is the whole meaning of the flag.
//   * `preventDefault` is honoured only when `cancelable`, which is the
//     difference between a cancellable click and a `load` a page cannot refuse.
bool dom_bindings::dispatch_to(value event, path_step at) {
    if (cx_ == nullptr || !event.is_object()) { return false; }
    context & cx = *cx_;
    auto * object = static_cast<script::object_object *>(event.as_heap());
    // THE TWO REFUSALS, BEFORE ANYTHING ELSE HAPPENS - concept-event-dispatch
    // is not reached at all when either flag is wrong, and a nested dispatch of
    // an event that is already travelling would otherwise reset the flags the
    // outer one is reading.
    if (flag_of(cx, event, dispatch_property)) {
        throw_dom_exception(cx, "InvalidStateError",
                            "Failed to execute 'dispatchEvent': the event is already being "
                            "dispatched.");
        return false;
    }
    // AN EVENT NOBODY INITIALISED. `document.createEvent` hands back one with no
    // type, and dispatching it is a mistake the specification names rather than
    // a dispatch to a listener called "". An object that is not one of ours has
    // no flag at all and is refused the same way, which is closer to the
    // TypeError a browser raises than the silent success this used to be.
    const value * initialised = object->find(std::string{initialised_property});
    if (initialised == nullptr || !context::truthy(*initialised)) {
        throw_dom_exception(cx, "InvalidStateError",
                            "Failed to execute 'dispatchEvent': the event has not been "
                            "initialised.");
        return false;
    }
    // BEFORE the listeners run. A handler for `input` reads the field's new
    // value, so a wrapper still holding the old one is the whole bug.
    (void)refresh_wrappers();
    const std::string type = cx.to_string(cx.lookup_property(event, "type"));
    const std::vector<path_step> path =
        propagation_path(at, context::truthy(cx.lookup_property(event, "composed")));
    const bool bubbles = context::truthy(cx.lookup_property(event, "bubbles"));

    // THE ACTIVATION TARGET - DOM 2.9 dispatch, steps 5.5 to 5.9.11 - and the
    // legacy-pre-activation behaviour, which runs BEFORE any listener. A
    // `click` that is a MouseEvent - not a plain `new Event("click")` - has one
    // node with activation behaviour: the target itself if it has any, else,
    // only when the event bubbles, the first ancestor on the path that does.
    // A checkbox or radio there is toggled NOW, so a click listener reads the
    // new checkedness; what it toggled from is kept so a preventDefault can
    // put it back. The other half - the activation behaviour proper, or the
    // legacy-canceled one - is at the end of this function.
    node_id activation_target;
    bool legacy_toggled = false;
    bool legacy_was_checked = false;
    node_id legacy_was_checked_radio;
    if (type == "click" && doc_ != nullptr && at.on == listen_on::node && at.node &&
        mouse_event_prototype_.is_object()) {
        bool is_mouse_event = false;
        for (value proto = object->prototype; proto.is_object();
             proto = static_cast<script::object_object *>(proto.as_heap())->prototype) {
            if (proto.bits() == mouse_event_prototype_.bits()) {
                is_mouse_event = true;
                break;
            }
        }
        const auto txn = doc_->read();
        for (std::size_t i = 0; is_mouse_event && i < path.size(); ++i) {
            if (path[i].on != listen_on::node || (i > 0 && !bubbles)) { break; }
            if (has_activation_behavior(txn, path[i].node)) {
                activation_target = path[i].node;
                break;
            }
        }
        if (activation_target) {
            const control_kind kind =
                control_kind_of(atoms_->text(txn.tag(activation_target).value_or(atom{})),
                                txn.attribute_value(activation_target, atoms_->intern("type")));
            // HTML: a checkbox's legacy-pre-activation behaviour inverts its
            // checkedness; a radio's sets it to true, and only if it was false.
            // Disabled does not matter here - the mouse never gets this far at
            // a disabled control and `click()` refuses before dispatching, but
            // `dispatchEvent` reaches it and toggles it, which
            // Event-dispatch-click.html asserts.
            if (kind == control_kind::checkbox || kind == control_kind::radio) {
                const bool checked = forms_->state_of(txn, *atoms_, activation_target).checked;
                if (kind == control_kind::checkbox || !checked) {
                    legacy_was_checked = checked;
                    if (kind == control_kind::radio) {
                        // The radio this one is about to uncheck, so a cancelled
                        // click can check it again - the same group walk
                        // form_store::toggle is about to make.
                        const atom name_attr = atoms_->intern("name");
                        const atom type_attr = atoms_->intern("type");
                        const std::string_view group =
                            txn.attribute_value(activation_target, name_attr);
                        const auto walk = [&](auto && self, node_id node) -> void {
                            if (legacy_was_checked_radio) { return; }
                            if (node != activation_target &&
                                txn.attribute_value(node, name_attr) == group &&
                                txn.attribute_value(node, type_attr) == "radio" &&
                                forms_->state_of(txn, *atoms_, node).checked) {
                                legacy_was_checked_radio = node;
                                return;
                            }
                            for (const node_id child : txn.children(node)) { self(self, child); }
                        };
                        if (!group.empty()) { walk(walk, txn.root()); }
                    }
                    forms_->toggle(txn, *atoms_, activation_target, kind);
                    legacy_toggled = true;
                }
            }
        }
    }

    const value target_object = object_of_step(cx, at);
    object->set("target", target_object);
    object->set("srcElement", target_object);
    // `offsetX`/`offsetY` ARE RELATIVE TO THE TARGET'S BOX - CSSOM View - and
    // both the constructed and the engine's mouse events arrived with a copy of
    // the client coordinates instead. Set here, once per dispatch, because this
    // is the one place both kinds pass through and the target is known.
    // ponytail: the border box, not the padding edge; subtract the border
    // widths when a page notices.
    if (at.on == listen_on::node && at.node) {
        if (const value * client_x = object->find("clientX")) {
            flush_layout(); // mouse-event-retarget.html dispatches before the first frame
            const rect box = box_of(at.node);
            const value * client_y = object->find("clientY");
            object->set("offsetX", value::number(context::to_number(*client_x) - box.x));
            object->set(
                "offsetY",
                value::number((client_y == nullptr ? 0.0 : context::to_number(*client_y)) - box.y));
        }
    }

    // SHADOW TREES: THE TARGET IS RETARGETED AND `window.event` IS HIDDEN.
    //
    // A listener outside a shadow tree sees the HOST as the target of an event
    // from inside it - DOM's "retarget", run for each step against that step's
    // node - and a listener on a node whose root is a shadow root does not get
    // `window.event` at all (concept-event-listener-inner-invoke step 8 sets
    // the current event only when the invocation target is not in a shadow
    // tree). After the dispatch the target is the last shadow-adjusted one, and
    // null if even that is still inside a shadow tree, so a closed tree's
    // internals never leak out on the event object.
    //
    // ALL OF IT IS GATED ON THE TARGET BEING IN A SHADOW TREE AT ALL, which is
    // one root walk, so a page with no shadow DOM pays nothing per step.
    // ponytail: `composedPath()` does not hide a closed tree's nodes.
    const auto in_shadow = [&](node_id node) {
        const auto txn = doc_->read();
        return shadow_tree_of(root_of_tree(txn, node, false)) != nullptr;
    };
    const bool target_in_shadow =
        at.on == listen_on::node && at.node && doc_ != nullptr && in_shadow(at.node);
    // DOM 4.4 "retarget": A climbs out of every shadow tree that does not also
    // hold B - the B here being a step, so the document and the window (no
    // node) pull A all the way into the light tree.
    const auto retarget = [&](node_id a, const path_step & against) {
        const auto txn = doc_->read();
        for (int guard = 0; a && guard < 64; ++guard) {
            const node_id root = root_of_tree(txn, a, false);
            const shadow_tree * tree = shadow_tree_of(root);
            if (tree == nullptr) { return a; }
            if (against.on == listen_on::node) {
                // Is `root` a shadow-including inclusive ancestor of B?
                bool holds = false;
                for (node_id b = against.node; b && !holds;) {
                    holds = b == root;
                    const node_id up = txn.parent(b);
                    const shadow_tree * above = up ? nullptr : shadow_tree_of(b);
                    b = up ? up : above == nullptr ? node_id{} : above->host;
                }
                if (holds) { return a; }
            }
            a = tree->host;
        }
        return a;
    };
    // `relatedTarget` IS RETARGETED TOO - concept-event-dispatch step 4 against
    // the target, then per step (invoke step 4) - so a `focus` leaving a
    // closed shadow tree names the host and not the input inside it. A
    // relatedTarget that retargets to the target itself while not being it
    // (step 6's condition) is a dispatch that does not happen: the targets
    // are cleared and nothing is invoked - relatedTarget.window.js's "Reset
    // targets on early return".
    const value related_value = cx.lookup_property(event, "relatedTarget");
    const node_id related = handle_of(related_value);
    const bool related_in_shadow = related && doc_ != nullptr && in_shadow(related);
    // PER STEP, AND BEFORE ANY LISTENER RUNS: "append to an event path"
    // fixes each item's relatedTarget as the path is built, so a listener
    // that moves the related node (relatedTarget.window.js, "part 2") does
    // not change what the later steps and the final value see.
    std::vector<node_id> related_steps(path.size());
    if (related_in_shadow) {
        for (std::size_t i = 0; i < path.size(); ++i) {
            related_steps[i] = retarget(related, path[i]);
        }
    }
    // AND THE TARGET'S SIDE THE SAME WAY: each step's shadow-adjusted target
    // and whether the step's own node is in a shadow tree are the path
    // struct's fields, fixed as it is appended - so a listener that moves a
    // node out of the shadow tree mid-dispatch (event-global-extra.window.js,
    // "nodes moving post-dispatch") changes neither what the later steps see
    // as `target` nor whether `window.event` is hidden from them.
    std::vector<node_id> shown_steps(path.size());
    std::vector<bool> hidden_steps(path.size());
    if (target_in_shadow) {
        for (std::size_t i = 0; i < path.size(); ++i) {
            shown_steps[i] = retarget(at.node, path[i]);
            hidden_steps[i] = path[i].on == listen_on::node && in_shadow(path[i].node);
        }
    }
    // Steps 6.10-6.11, decided here too: what the event names as `target` and
    // `relatedTarget` once it has stopped travelling - the last node step's
    // shadow-adjusted pair - and whether both are cleared because either is
    // still inside a shadow tree.
    node_id last = at.node;
    node_id last_related = related;
    for (std::size_t i = path.size(); i-- > 0;) {
        if (path[i].on != listen_on::node) { continue; }
        if (target_in_shadow) { last = shown_steps[i]; }
        if (related_in_shadow) { last_related = related_steps[i]; }
        break;
    }
    const bool clear_targets =
        (target_in_shadow || related_in_shadow) &&
        ((last && in_shadow(last)) || (last_related && in_shadow(last_related)));
    if (related_in_shadow && at.on == listen_on::node && retarget(related, at) == at.node &&
        related != at.node) {
        object->set("target", value::null());
        object->set("srcElement", value::null());
        object->set("relatedTarget", value::null());
        object->set(std::string{cancel_bubble_property}, value::boolean(false));
        object->set(std::string{stop_immediate_property}, value::boolean(false));
        return prevented(event);
    }
    object->set(std::string{stop_immediate_property}, value::boolean(false));
    object->set(std::string{dispatch_property}, value::boolean(true));
    ++dispatch_depth_;
    // `composedPath()` is the path AS A LIST, and it is empty outside a
    // dispatch. Built once here rather than recomputed by the method, because
    // the tree may have moved by the time a page asks.
    {
        value listed = cx.make_array();
        auto * items = static_cast<script::array_object *>(listed.as_heap());
        for (const path_step & step : path) { items->items.push_back(object_of_step(cx, step)); }
        object->set(std::string{path_property}, listed);
    }

    // `window.event`, WHICH IS SET FOR THE WHOLE DISPATCH AND RESTORED AFTER IT.
    //
    // It is the oldest event API there is and pages still reach for it - and not
    // only old ones: dom/events/Event-stopPropagation-cancel-bubbling.html
    // calls `event.stopPropagation()` with no receiver at all, which only works
    // because the bare name is a global. So it is written to BOTH the window
    // object and the globals table, which are separate storage here.
    //
    // SAVED AND RESTORED rather than cleared, because a listener may dispatch:
    // the inner dispatch must leave the outer one's event visible when it
    // returns, which is exactly what window-event-restored-after-throwing-
    // onerror.html checks by throwing from the middle of the nested one.
    //
    // THE TWO STORAGES ARE SAVED SEPARATELY, which is not tidiness: a page may
    // have a top-level `var event` of its own, and that is a GLOBAL while
    // `window.event` is an own property of the window object. Restoring one
    // from the other would silently overwrite the page's variable with
    // undefined the first time anything was dispatched.
    const context::rooted keep_event{cx, event};
    script::object_object * window = window_object();
    value outer_window_event = value::undefined();
    if (window != nullptr) {
        if (const value * held = window->find("event")) { outer_window_event = *held; }
        window->set("event", event);
    }
    const value outer_global_event =
        cx.has_global("event") ? cx.global("event") : value::undefined();
    const context::rooted keep_outer_window{cx, outer_window_event};
    const context::rooted keep_outer_global{cx, outer_global_event};
    const auto expose = [&](value shown) {
        if (window != nullptr) { window->set("event", shown); }
        cx.define_global("event", shown);
    };
    expose(event);

    const auto stopped = [&] { return flag_of(cx, event, cancel_bubble_property); };
    // ONE STEP'S BOOKKEEPING: `currentTarget`, `eventPhase`, and inside a shadow
    // tree the retargeted `target` and whether `window.event` shows. Answers
    // whether this step is AT_TARGET - which the HOST of a shadow tree also is,
    // its shadow-adjusted target being itself, so a listener there sees phase 2
    // and hears a non-bubbling event.
    const auto visit = [&](std::size_t index, double otherwise) {
        const path_step & step = path[index];
        node_id shown = at.node;
        if (target_in_shadow) {
            shown = shown_steps[index];
            const value shown_object = wrap(cx, shown);
            object->set("target", shown_object);
            object->set("srcElement", shown_object);
            expose(hidden_steps[index] ? value::undefined() : event);
        }
        const bool is_target = (step.on == at.on && step.node == at.node) ||
                               (step.on == listen_on::node && step.node == shown);
        if (related_in_shadow) { object->set("relatedTarget", wrap(cx, related_steps[index])); }
        object->set("currentTarget", object_of_step(cx, step));
        object->set("eventPhase", value::number(is_target ? 2 : otherwise));
        return is_target;
    };

    // CAPTURE: from the window down to the target. The target's own capturing
    // listeners run here, at phase AT_TARGET rather than CAPTURING_PHASE.
    for (std::size_t i = path.size(); i-- > 0;) {
        if (stopped()) { break; }
        (void)visit(i, 1);
        fire_at(path[i], type, event, true);
    }
    // BUBBLE: back up. A non-bubbling event gets this pass at the target only -
    // `continue` and not `break`, because a shadow host further up is a target
    // too and the steps between are merely skipped.
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (stopped()) { break; }
        if (!visit(i, 3) && !bubbles) { continue; }
        fire_at(path[i], type, event, false);
    }

    // AFTER THE DISPATCH the event is not travelling any more, and the two
    // properties that say where it is have to say so - a page keeps the object
    // and reads them later.
    if (target_in_shadow || related_in_shadow) {
        // concept-event-dispatch steps 6.10-6.11 and 11: the target is the
        // last step's shadow-adjusted target and the relatedTarget its
        // retargeted one - and BOTH are null when either is still inside a
        // shadow tree (`clearTargets`), so nothing of a closed tree is left
        // on the object.
        const value final_target = clear_targets ? value::null() : wrap(cx, last);
        object->set("target", final_target);
        object->set("srcElement", final_target);
        object->set("relatedTarget", clear_targets       ? value::null()
                                     : related_in_shadow ? wrap(cx, last_related)
                                                         : related_value);
    }
    object->set("currentTarget", value::null());
    object->set("eventPhase", value::number(0));
    object->set(std::string{dispatch_property}, value::boolean(false));
    // AND THE PROPAGATION FLAGS ARE UNSET - concept-event-dispatch step 14, and
    // not a detail: the same event object is dispatched twice by plenty of code,
    // and an engine that left the flag set made the second dispatch reach
    // nobody. Clearing them HERE rather than on entry is what lets
    // `stopPropagation()` called BEFORE a dispatch stop that dispatch, which is
    // the other half of the same rule.
    object->set(std::string{cancel_bubble_property}, value::boolean(false));
    object->set(std::string{stop_immediate_property}, value::boolean(false));
    object->set(std::string{path_property}, cx.make_array());
    if (window != nullptr) { window->set("event", outer_window_event); }
    cx.define_global("event", outer_global_event);
    // A `once` listener is removed AFTER the dispatch, not during it: erasing
    // from the vector being walked is how a later listener gets skipped. And not
    // after a NESTED dispatch either - a listener may dispatch, and the inner
    // compaction would shift the list the outer loop is indexing.
    --dispatch_depth_;
    reap_spent_listeners();
    // THE ACTIVATION BEHAVIOUR, DOM 2.9 dispatch step 11 - AFTER the event has
    // stopped travelling: `eventPhase` is NONE, `currentTarget` null, the path
    // empty and the dispatch flag down, all of which a `change` listener can
    // read off the click that caused it, and one of which lets that listener
    // dispatch the same event again. Before the microtask drain, because a
    // microtask a click listener queued must find the checkedness a
    // preventDefault put back, not the one it saw.
    if (activation_target) {
        if (!prevented(event)) {
            run_activation_behavior(cx, activation_target);
        } else if (legacy_toggled) {
            // The legacy-canceled-activation behaviour: the checkedness the
            // pre-activation changed goes back, and so does the radio it
            // unchecked.
            const auto txn = doc_->read();
            forms_->state_of(txn, *atoms_, activation_target).checked = legacy_was_checked;
            if (legacy_was_checked_radio) {
                forms_->state_of(txn, *atoms_, legacy_was_checked_radio).checked = true;
            }
        }
    }
    // A LISTENER THAT FAULTS IS REPORTED AND THE FAULT CLEARED, exactly as for
    // a timer or an animation frame. Without this the first listener to fault
    // left the VM's failure flag set for the life of the page: every later
    // callback of any kind was refused, so the page stopped responding to
    // everything, and nothing anywhere said why.
    // ...and after an event, which is the other checkpoint a browser has: a
    // listener that resolves a promise has its handlers run before the next
    // event is dispatched, not at some later frame.
    cx.drain_microtasks();
    note_callback_fault(type);
    return prevented(event);
}

// --- activation behaviour --------------------------------------------------
//
// WHICH ELEMENTS HAVE IT, per HTML: a link with an href, a button, an input, a
// label, and a <summary> whose parent is a <details>. `element.click()`,
// `dispatchEvent(new MouseEvent("click"))` and the engine's own mouse click all
// reach the two functions here through dispatch_to, which is the point: the
// browser's activate hook used to be called by two of the three from OUTSIDE
// the dispatch, with the toggle after the listeners, so a click listener on a
// checkbox read the old checkedness and a constructed click toggled nothing.
bool dom_bindings::has_activation_behavior(const read_txn & txn, node_id node) const {
    const std::string_view tag = atoms_->text(txn.tag(node).value_or(atom{}));
    if (tag == "a" || tag == "area") { return txn.has_attribute(node, atoms_->intern("href")); }
    if (tag == "summary") {
        const node_id parent = txn.parent(node);
        return parent && atoms_->text(txn.tag(parent).value_or(atom{})) == "details";
    }
    // A <select> has none in HTML. It is here because the engine opens its
    // popup from the same hook, and a click that reached no activation target
    // would never open one.
    return tag == "button" || tag == "input" || tag == "label" || tag == "select";
}

void dom_bindings::run_activation_behavior(context & cx, node_id target) {
    control_kind kind = control_kind::none;
    std::string javascript_url;
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(target).value_or(atom{}));
        kind = control_kind_of(tag, txn.attribute_value(target, atoms_->intern("type")));
        if (tag == "a" || tag == "area") {
            const std::string_view href = txn.attribute_value(target, atoms_->intern("href"));
            if (ascii_istarts_with(href, "javascript:")) {
                javascript_url = std::string{href.substr(std::string_view{"javascript:"}.size())};
            }
        }
    }
    // A `javascript:` LINK RUNS ITS SCRIPT, and that is the whole navigation:
    // HTML 7.4.2.1 evaluates the percent-decoded URL body as a classic script
    // in the document's realm, as a TASK queued from the navigate - so it is a
    // zero-delay timer here, which is what puts it after the click's own
    // listeners and microtasks. In the bindings rather than the browser's
    // follow_link because the bindings own the context; the browser would
    // have to re-enter `run` from inside the dispatch that is running now.
    // dom/events/Event-dispatch-click's "pick the first with activation
    // behavior <a href>" is two nested `javascript:` anchors and expects the
    // inner one's script, once.
    if (!javascript_url.empty()) {
        script::program compiled = script::compiler::compile(
            "return (function () {\n" + percent_decode(javascript_url) + "\n});");
        if (compiled.ok) {
            const value body = cx.run_nested(cx.own_program(std::move(compiled)));
            if (body.is_callable()) { (void)add_timer(body, 0, false); }
        }
        return;
    }
    if (kind == control_kind::checkbox || kind == control_kind::radio) {
        // HTML's input activation behaviour for the two: nothing unless the
        // element is connected, else `input` - bubbles, composed - and then
        // `change` - bubbles - neither cancelable. Being disabled does not
        // matter: the specification excepts exactly these two from its
        // mutability check, and Event-dispatch-click.html's "disabling checkbox
        // in onclick listener shouldn't suppress input" is that sentence as a
        // test. The toggle itself happened before the listeners ran.
        if (is_connected(target)) {
            const auto fire = [&](std::string_view name, bool composed) {
                value event = make_event(cx, name, target);
                auto * object = static_cast<script::object_object *>(event.as_heap());
                object->set("cancelable", value::boolean(false));
                object->set("composed", value::boolean(composed));
                (void)dispatch_event(name, target, event);
            };
            fire("input", true);
            fire("change", false);
        }
    }
    // The rest is the browser's: following the link, toggling the <details>,
    // clicking the control the <label> labels, submitting or resetting the
    // form, opening the <select> - and, for the two above, repainting the box.
    if (on_activate_) { on_activate_(target); }
}

bool dom_bindings::dispatch_event(std::string_view type, node_id target, value event) {
    (void)type; // the event carries it; initEvent can have changed it since
    if (cx_ == nullptr) { return false; }
    const script::context::rooted keep{*cx_, event}; // see fire_at
    return dispatch_to(event, target ? path_step{target, listen_on::node}
                                     : path_step{node_id{}, listen_on::document});
}

value dom_bindings::make_event_object(context & cx, std::string_view type, bool bubbles,
                                      bool cancelable) {
    auto * event = cx.allocate<script::object_object>();
    const value self = value::object(event);
    // THE PROTOTYPE FIRST, so a collection triggered by the writes below finds
    // an object that is already an Event - and because everything a page calls
    // on one is found through it.
    if (event_prototype_.is_object()) { event->prototype = event_prototype_; }
    initialise_event(cx, *event, type, bubbles, cancelable, now_ms_,
                     is_trusted_getter_of(event_prototype_));
    return self;
}

value dom_bindings::make_event(context & cx, std::string_view type, node_id target) {
    // AN ENGINE EVENT BUBBLES AND CAN BE CANCELLED. Every event the browser
    // generates here is one a page may refuse - a click, a key, a wheel notch -
    // and the flags decide whether the bubble pass runs at all and whether
    // preventDefault does anything, so getting them wrong is not cosmetic.
    value event = make_event_object(cx, type, true, true);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set(std::string{trusted_property}, value::boolean(true));
    // AND IT IS INITIALISED, which dispatch now insists on. `browser::tick`
    // sends `load` and `DOMContentLoaded` through here and a click goes the same
    // way, so an engine event that arrived without the flag would be refused by
    // the very check that exists to catch a page's uninitialised one.
    object->set(std::string{initialised_property}, value::boolean(true));
    object->set("target", wrap(cx, target));
    object->set("srcElement", wrap(cx, target));
    return event;
}

bool dom_bindings::prevented(value event) {
    if (!event.is_object()) { return false; }
    const value * slot =
        static_cast<script::object_object *>(event.as_heap())->find("defaultPrevented");
    return slot != nullptr && context::truthy(*slot);
}

// The third argument is an options object or a bare capture flag -
// `addEventListener(t, f, true)` is the old spelling and pages still use it.
//
// THE MEMBERS ARE READ IN THE ORDER WebIDL CONVERTS THEM, which is alphabetical
// and is observable: EventListenerOptions-capture.html and
// AddEventListenerOptions-passive.html both pass a dictionary whose members are
// GETTERS and assert which of them ran. The same tests assert that a member
// nobody asked about - `dummy` - is never read, which is why this looks up four
// names rather than copying the object.
dom_bindings::listener dom_bindings::make_listener(context & cx, path_step target,
                                                   std::span<value> args) {
    listener made;
    made.target = target.node;
    made.on = target.on;
    made.host = target.host;
    made.type = arg_string(cx, args, 0);
    made.callback = arg(args, 1);
    const value options = arg(args, 2);
    if (options.is_object()) {
        made.capture = context::truthy(cx.lookup_property(options, "capture"));
        made.once = context::truthy(cx.lookup_property(options, "once"));
        // `passive` IS A PROMISE THE ENGINE ENFORCES, not one it takes on
        // trust: preventDefault does nothing while this listener runs. Reading
        // it is also how a page feature-DETECTS the option at all - it hands
        // addEventListener a dictionary whose members are getters and watches
        // which of them run, which is what every passive polyfill does.
        //
        // AN ABSENT MEMBER IS NOT `false`, it is the DEFAULT PASSIVE VALUE. The
        // IDL gives `passive` no default precisely so the DOM can compute one
        // from the type and the target, and `{passive: undefined}` is the same
        // as leaving it out - WebIDL treats a member whose value is undefined
        // as not present, which passive-by-default.html tests separately from
        // omitting it for all 40 of its combinations.
        const value asked = cx.lookup_property(options, "passive");
        made.passive = asked.is_undefined() ? default_passive_value(made.type, target)
                                            : context::truthy(asked);
        const value signal = cx.lookup_property(options, "signal");
        if (!signal.is_undefined()) {
            // `AbortSignal signal` IS NOT NULLABLE IN THE IDL, so `{signal:
            // null}` is a TypeError rather than "no signal". A page that means
            // "no signal" leaves the member out.
            if (!signal.is_object()) {
                cx.throw_error("TypeError",
                               "Failed to execute 'addEventListener' on 'EventTarget': member "
                               "signal is not of type AbortSignal.");
                made.spent = true;
                return made;
            }
            made.abort_signal = signal;
            // AN ALREADY-ABORTED SIGNAL ADDS NOTHING. The signal's abort steps
            // are what remove a listener, and one registered after the abort
            // would never have them run - so it would live forever, which is
            // the opposite of what the page asked for.
            if (context::truthy(cx.lookup_property(signal, "aborted"))) { made.spent = true; }
        }
    } else {
        // The bare capture flag, or no third argument at all. Either way the
        // dictionary's other members are at their defaults - and `passive`'s
        // default is computed, so the commonest spelling on the web,
        // `addEventListener(type, fn)`, is where the rule matters most.
        if (args.size() > 2) { made.capture = context::truthy(options); }
        made.passive = default_passive_value(made.type, target);
    }
    // A NULL OR ABSENT CALLBACK REGISTERS NOTHING. The DOM returns before the
    // listener is built rather than storing one that can never be invoked, and
    // it matters now that a listener object without a callable `handleEvent` is
    // a TypeError: a stored `null` would report one on every dispatch.
    if (made.callback.is_nullish()) { made.spent = true; }
    return made;
}

// https://dom.spec.whatwg.org/#default-passive-value - the four SCROLL-BLOCKING
// types, on the four targets a page scrolls through.
//
// The rule reads as one sentence and is worth stating as one: a `touchstart`,
// `touchmove`, `wheel` or `mousewheel` listener on the WINDOW, the DOCUMENT, the
// DOCUMENT ELEMENT or the BODY is passive unless the page said `{passive:
// false}`. Anywhere else, and for any other type, only what was asked for
// counts. It exists because those four listeners on those four targets are how
// a page blocks a scroll, and a browser cannot start scrolling until it knows
// whether one of them will - so the platform changed the default rather than
// wait.
//
// `find_by_tag` walks the tree, so the type test comes FIRST: four string
// comparisons decide it for every listener a page registers that is not one of
// these, which is nearly all of them.
bool dom_bindings::default_passive_value(std::string_view type, const path_step & target) {
    if (type != "touchstart" && type != "touchmove" && type != "wheel" && type != "mousewheel") {
        return false;
    }
    switch (target.on) {
    case listen_on::window:
    case listen_on::document: return true;
    case listen_on::node:
        // The document element and the body, by tag rather than by position:
        // `documentElement` here is `find_by_tag("html")` for the same reason
        // the document's own property is, so the two answers agree.
        return target.node &&
               (target.node == find_by_tag("html") || target.node == find_by_tag("body"));
    // A standalone EventTarget is not in any document, so no scroll depends on
    // it: generic-events-stay-cancelable.html is that case exactly, and a
    // passive default there would make an event it dispatches uncancellable.
    case listen_on::object: return false;
    }
    return false;
}

void dom_bindings::add_listener(listener made) {
    // A listener the options refused - a null signal, or one already aborted -
    // is not registered at all. `spent` is the flag the reap pass already reads,
    // so nothing else has to learn about the case.
    if (made.spent) { return; }
    // (type, callback, capture) ON ONE TARGET is a listener's identity, and the
    // DOM says a second registration of the same three does nothing at all.
    // Without this a page that registers in a function it calls twice got two
    // calls per event, and a `once` listener registered twice fired twice.
    const bool already = std::ranges::any_of(listeners_, [&](const listener & l) {
        return l.on == made.on && l.target == made.target && l.host.bits() == made.host.bits() &&
               l.type == made.type && l.capture == made.capture && !l.spent &&
               l.callback.bits() == made.callback.bits();
    });
    if (already) { return; }
    listeners_.push_back(std::move(made));
}

void dom_bindings::reap_spent_listeners() {
    if (dispatch_depth_ > 0) { return; }
    std::erase_if(listeners_, [](const listener & l) { return l.spent; });
}

// THE FENCE, COMPILED ONCE PER PAGE.
//
// See the note on `listener_fence_` in the header: the VM unwinds a throw to
// the innermost `try` on the whole stack, so the only thing that can stop one
// at the dispatch is a `try` INSIDE the callee. This is that `try`, and it
// carries WebIDL's "call a user object's operation" with it, because every
// step of that algorithm that can throw has to be on this side of the fence:
//
//   * the `handleEvent` GET, which a page may make an accessor -
//     EventListener-handleEvent.html registers one whose getter throws and then
//     asserts on the identity of the object it threw;
//   * the TypeError for a listener object whose `handleEvent` is not callable,
//     which the same file tests with `null` and with `42`.
//
// Both used to be C++ - a `cx.failed()` check after the lookup and a
// `throw_error` after it - and both were wrong for the same reason: the
// lookup's throw had already left for the page's `try`, so `failed()` was
// clear, and the TypeError this file then raised replaced the page's exception
// with one of ours.
//
// `typeof callback === "function"` IS THE WHOLE TEST for which arm to take. A
// function is never asked for a `handleEvent`, even when it has one - that is
// the other half of the same clause and is what the last two tests of that file
// check. `this` is the CURRENT TARGET for a function and the listener object
// for a `handleEvent`, which reads its own state.
void dom_bindings::install_listener_fence(context & cx, script::native_object & keeper) {
    // `invoke(fn, receiver, args)` - the trip back into C++, so that the CALL
    // itself is still ours. `args` is the event, or an ARRAY of arguments for
    // the one handler that is not handed one: `window.onerror` takes (message,
    // filename, lineno, colno, error). One value in the common case rather than
    // an array, because an array per mousemove is an allocation per mousemove.
    //
    // WHAT COMES BACK IS THE CALLEE'S RETURN VALUE IF IT WAS A BOOLEAN, and
    // undefined otherwise. A handler property's `return false` cancels the
    // event and nothing else about a return value is read, so a boolean is all
    // the fence forwards - it is not a heap value, so nothing has to be rooted
    // across the return, and it cannot be mistaken for the `[thrown]` array.
    auto * runner = cx.allocate<script::native_object>(
        "invokeEventListener", [](context & c, std::span<value> a) {
            if (a.size() < 3 || !a[0].is_callable()) { return value::undefined(); }
            value out = value::undefined();
            if (a[2].is_array()) {
                const std::vector<value> spread =
                    static_cast<script::array_object *>(a[2].as_heap())->items;
                out = c.call(a[0], spread, a[1]);
            } else {
                const value one = a[2];
                out = c.call(a[0], std::span<const value>{&one, 1}, a[1]);
            }
            return out.is_boolean() ? out : value::undefined();
        });
    listener_invoke_ = value::object(runner);
    keeper.retained.push_back(listener_invoke_);
    fence_keeper_ = &keeper;
}

// COMPILED AT THE FIRST DISPATCH, NOT AT INSTALL. `run_nested` goes through
// `context::call`, and `call` refuses every closure while no program has run
// yet (`program_` is null until the page's first `execute`) - so a fence built
// during install() came back `undefined`, every listener took the unfenced
// path below, and a throw inside one reached the page's own `try`. By the
// first dispatch a program is always running.
void dom_bindings::compile_listener_fence(context & cx) {
    if (listener_fence_.is_callable() || fence_keeper_ == nullptr) { return; }
    // A `TypeError` AND NOT A STRING, because the page can see the difference:
    // EventListener-handleEvent.html checks the reported value with
    // `promise_rejects_js(t, TypeError, ...)`.
    script::program compiled = script::compiler::compile(
        "return (function (invoke, callback, receiver, args) {\n"
        "    try {\n"
        "        if (typeof callback === \"function\") {\n"
        "            return invoke(callback, receiver, args);\n"
        "        }\n"
        "        var method = callback.handleEvent;\n"
        "        if (typeof method !== \"function\") {\n"
        "            throw new TypeError(\"Failed to invoke an EventListener: the object's \"\n"
        "                + \"'handleEvent' property is not a function.\");\n"
        "        }\n"
        "        return invoke(method, callback, args);\n"
        "    } catch (thrown) {\n"
        "        return [thrown];\n"
        "    }\n"
        "});\n");
    // A COMPILE FAILURE IS NOT FATAL. `invoke_listener` falls back to the
    // unfenced C++ path, which is what this file did before the fence existed:
    // every listener still runs and only the containment is lost.
    if (compiled.ok) { listener_fence_ = cx.run_nested(cx.own_program(std::move(compiled))); }
    fence_keeper_->retained.push_back(listener_fence_);
}

bool dom_bindings::invoke_listener(context & cx, value callback, value receiver, value args,
                                   value & thrown, value & returned) {
    thrown = value::undefined();
    returned = value::undefined();
    // ROOTED ACROSS THE FENCE'S COMPILE. The first listener a fresh context
    // fires compiles the fence through run_nested, which can collect - and
    // the event, the callback and the receiver live only in these C++
    // locals until `call` roots them. settle_read's event object was freed
    // exactly there once install_builtins allocated enough for the heap to
    // cross its first collection threshold inside the page's first handler
    // (image_basics' FileReader test, 2026-09-12).
    const context::rooted keep_callback{cx, callback};
    const context::rooted keep_receiver{cx, receiver};
    const context::rooted keep_args{cx, args};
    compile_listener_fence(cx);
    if (listener_fence_.is_callable() && listener_invoke_.is_callable()) {
        const value passed[4] = {listener_invoke_, callback, receiver, args};
        const value answer = cx.call(listener_fence_, passed);
        if (!answer.is_array()) {
            returned = answer;
            return false;
        }
        // THE THROWN VALUE COMES BACK IN A ONE-ELEMENT ARRAY rather than as
        // itself, because `undefined` and `null` are both values a page can
        // throw and neither can be told from "nothing was thrown" on its own.
        const auto & items = static_cast<script::array_object *>(answer.as_heap())->items;
        thrown = items.empty() ? value::undefined() : items.front();
        return true;
    }
    // WITHOUT THE FENCE: the shape this had before it existed, minus the
    // containment. A throw from here reaches whatever `try` the page was inside.
    if (callback.is_callable()) {
        if (args.is_array()) {
            const std::vector<value> spread =
                static_cast<script::array_object *>(args.as_heap())->items;
            returned = cx.call(callback, spread, receiver);
        } else {
            returned = cx.call(callback, std::span<const value>{&args, 1}, receiver);
        }
        return false;
    }
    if (!callback.is_object_like()) { return false; }
    const value handler = cx.lookup_property(callback, "handleEvent");
    if (cx.failed()) { return false; }
    if (!handler.is_callable()) {
        cx.throw_error("TypeError", "Failed to invoke an EventListener: the object's "
                                    "'handleEvent' property is not a function.");
        return false;
    }
    (void)cx.call(handler, std::span<const value>{&args, 1}, callback);
    return false;
}

void dom_bindings::fire_at(path_step step, std::string_view type, value event, bool capturing) {
    // THE EVENT IS ROOTED FOR THE CALL. An event the ENGINE made - a sheet's
    // load from browser::tick, an image's from the registry - lives only in
    // a C++ local while its listeners run, and a listener that allocates
    // enough runs the collector; the object was freed under the second
    // listener and read back as whatever took its slot (paint_timing saw an
    // element where `bubbles` should have been). A page-made event is held
    // by its register too, so this costs it nothing.
    if (cx_ == nullptr) { return; }
    const script::context::rooted keep{*cx_, event};
    // A LISTENER THAT THREW IS REPORTED TO THE PAGE, and not only to the
    // embedder.
    //
    // `note_callback_fault` at the end of a dispatch put the text in
    // `callback_error()`, which is an EMBEDDER channel: ctbrowse prints it and
    // ctdrive returns it. The page was told nothing at all - no `error` event,
    // no `window.onerror` - so a library with its own error reporting saw a
    // listener that simply stopped, and a test that counts error events counted
    // none. This is the other half of the fix wpt.md records for a throw during
    // LOAD; the same gap was still open one layer down.
    //
    // THE GUARD IS `type == "error"` AND IT IS THE WHOLE GUARD. Reporting is
    // itself a dispatch, so an `error` listener that throws is the recursion,
    // and refusing to report a fault raised while an `error` event is
    // travelling bounds it at one level with no state to keep. The dispatch
    // that swallows it still clears the VM's failure flag through
    // note_callback_fault, so nothing is left refusing to run.
    //
    // THE FAULT IS TAKEN BEFORE THE EVENT IS DISPATCHED, for the reason wpt.md
    // gives at length: every C++ entry into JavaScript declines while `failed_`
    // is set, so a report attempted with the flag still up runs no listener at
    // all and tells the page nothing twice.
    //
    // TWO WAYS TO FAIL AND ONLY ONE OF THEM IS A THROW. The fence hands back the
    // value a listener threw and the VM's failure flag is never raised for it;
    // a VM FAULT - the allocation ceiling, the call stack ceiling - is a failure
    // that was never an exception, unwinds nothing, and is still only visible
    // as `failed()`. Both are reported and both clear what they read, because a
    // flag left up refuses every later callback of any kind.
    const auto report_fault = [this, type](bool threw, value thrown) {
        if (cx_ == nullptr) { return; }
        const bool faulted = cx_->failed();
        if (!threw && !faulted) { return; }
        if (type == "error") {
            // Reporting is itself a dispatch, so this is the recursion; bounded
            // at one level with no state to keep. The flag still has to go.
            if (faulted) { (void)cx_->take_error(); }
            return;
        }
        const context::rooted keep_thrown{*cx_, thrown};
        const std::string fault =
            std::string{type} + " listener: " +
            (faulted ? cx_->take_error() : "uncaught " + describe_thrown(*cx_, thrown));
        // The page gets it first. If nothing handled it - `preventDefault` on
        // an error event is how a page says it did - it goes to the embedder
        // as well, which is what a browser's console is for. The FIRST one is
        // kept there, exactly as note_callback_fault keeps it: a listener that
        // faults on every event has one bug, not a thousand.
        const bool handled = dispatch_error_value(fault, thrown);
        if (!handled && callback_error_.empty()) { callback_error_ = fault; }
    };
    // THE LIST IS COPIED BEFORE ANY OF IT RUNS, which is what the DOM says and
    // is not the same thing as walking it carefully.
    //
    // concept-event-listener-inner-invoke opens with "let listeners be a clone
    // of the object's event listener list", and then two rules fall out of the
    // clone rather than being written anywhere: a listener REGISTERED during
    // this dispatch is not in the copy and does not run, and a listener REMOVED
    // during it is in the copy and is skipped by its `removed` flag.
    //
    // Walking the live vector by index cannot do the second half here, because
    // `removeEventListener` is `std::erase_if` in five places across four files
    // - it MOVES every entry after the one it takes, so the index the loop is
    // holding now names the NEXT listener and that one is silently skipped. A
    // handler that removes itself is not exotic; it is the shape of every
    // "run this once" listener written before `{once: true}` existed, and
    // Event-dispatch-handlers-changed.html removes one at each of eight targets.
    //
    // What is copied is IDENTITY, not the listener: a `value` in a plain vector
    // is invisible to the collector, and a callback dropped by the removal that
    // happened mid-dispatch could be swept while this loop still held it. The
    // re-find below reads the callback back out of `listeners_`, which IS
    // traced, and finding nothing is exactly the `removed` check.
    std::vector<std::uint64_t> queued;
    const auto belongs = [&](const listener & l) {
        if (l.on != step.on || l.type != type || l.capture != capturing) { return false; }
        if (l.on == listen_on::node && l.target != step.node) { return false; }
        if (l.on == listen_on::object && l.host.bits() != step.host.bits()) { return false; }
        return true;
    };
    for (const listener & l : listeners_) {
        if (!l.spent && belongs(l)) { queued.push_back(l.callback.bits()); }
    }
    for (const std::uint64_t identity : queued) {
        // RE-READ THE FLAG EACH TIME. stopImmediatePropagation is defined by
        // stopping the listeners that would have run next at this very step, so
        // a check hoisted out of the loop implements the other method.
        if (flag_of(*cx_, event, stop_immediate_property)) { return; }
        const auto found = std::ranges::find_if(listeners_, [&](const listener & l) {
            return !l.spent && l.callback.bits() == identity && belongs(l);
        });
        // Gone since the copy was taken - removed by a listener that ran
        // earlier at this step, or by its AbortSignal.
        if (found == listeners_.end()) { continue; }
        if (found->once) { found->spent = true; }
        // COPIED OUT BEFORE THE CALL. `found` is an iterator into a vector a
        // listener can grow (addEventListener) or shrink (an AbortSignal), so
        // nothing may touch it once script is running.
        const value callback = found->callback;
        const bool passive = found->passive;
        // SET AND CLEARED rather than saved and restored: an event that is
        // already being dispatched is refused, so one can never be inside two
        // of these at once. It stays set across a NESTED dispatch on purpose -
        // the flag belongs to the event, so a listener of some other event that
        // reaches back for this one and cancels it is refused too, which is
        // what concept-event-listener-inner-invoke steps 10 and 15 say.
        auto * carrier = static_cast<script::object_object *>(event.as_heap());
        if (passive) { carrier->set(std::string{passive_property}, value::boolean(true)); }
        value thrown = value::undefined();
        value returned = value::undefined();
        const bool threw =
            invoke_listener(*cx_, callback, object_of_step(*cx_, step), event, thrown, returned);
        if (passive) { carrier->set(std::string{passive_property}, value::boolean(false)); }
        // PER LISTENER, not per dispatch. A throw from the first of three must
        // not stop the other two - which it did twice over, first because every
        // later `call` declines while the VM's failure flag is up and then
        // because the throw left the dispatch entirely - and each one that
        // throws is its own report.
        report_fault(threw, thrown);
    }
    if (!capturing && !flag_of(*cx_, event, stop_immediate_property)) {
        value thrown = value::undefined();
        const bool threw = fire_handler_property(object_of_step(*cx_, step), type, event, &thrown);
        report_fault(threw, thrown);
    }
}

namespace {

// THE EVENT HANDLER IDL ATTRIBUTES, HTML 8.1.7.2, as one list per mixin.
//
// A REVERSAL, and it is worth naming because the opposite rule is written down
// in element/methods.cpp: that file installs a handler property only for
// "exactly the events this engine can dispatch, and no more", on the grounds
// that a detection answering yes for an event that never fires is worse than
// one answering no. That reasoning holds for a page choosing between two
// spellings of the same event - which is the case it was written for,
// `onwheel` against `onmousewheel` - and does not hold for the attribute's mere
// existence: HTML requires all of these on every one of these objects,
// `'onanimationend' in el` is true in every browser, and a page that branches
// on it is depending on animations working rather than on the property. What
// the engine does not dispatch is a gap in the DISPATCH, and hiding the
// attribute does not close it.
//
// Those 19 names stay OWN null data properties on the wrapper, which shadow
// the accessor here - so for them `el.onclick = ""` still reads "" until that
// list moves. `onscroll` is what the unit tests use for that reason.
//
// Four files in dom/events ask for the ones no engine event uses:
// Body-FrameSet-Event-Handlers.html wants `onscroll` and `onresize`, and the
// four `webkit-*-event.html` files want the animation and transition pairs
// prefixed and unprefixed at once. `ontouchstart` is deliberately NOT here: it
// is the name a touch capability check asks about, and this engine has none.
constexpr std::string_view global_event_handlers[] = {"onabort",
                                                      "onanimationcancel",
                                                      "onanimationend",
                                                      "onanimationiteration",
                                                      "onanimationstart",
                                                      "onauxclick",
                                                      "onbeforeinput",
                                                      "onbeforematch",
                                                      "onbeforetoggle",
                                                      "onblur",
                                                      "oncancel",
                                                      "oncanplay",
                                                      "oncanplaythrough",
                                                      "onchange",
                                                      "onclick",
                                                      "onclose",
                                                      "oncontextlost",
                                                      "oncontextmenu",
                                                      "oncontextrestored",
                                                      "oncopy",
                                                      "oncuechange",
                                                      "oncut",
                                                      "ondblclick",
                                                      "ondrag",
                                                      "ondragend",
                                                      "ondragenter",
                                                      "ondragleave",
                                                      "ondragover",
                                                      "ondragstart",
                                                      "ondrop",
                                                      "ondurationchange",
                                                      "onemptied",
                                                      "onended",
                                                      "onerror",
                                                      "onfocus",
                                                      "onformdata",
                                                      "ongotpointercapture",
                                                      "oninput",
                                                      "oninvalid",
                                                      "onkeydown",
                                                      "onkeypress",
                                                      "onkeyup",
                                                      "onload",
                                                      "onloadeddata",
                                                      "onloadedmetadata",
                                                      "onloadstart",
                                                      "onlostpointercapture",
                                                      "onmousedown",
                                                      "onmouseenter",
                                                      "onmouseleave",
                                                      "onmousemove",
                                                      "onmouseout",
                                                      "onmouseover",
                                                      "onmouseup",
                                                      "onpaste",
                                                      "onpause",
                                                      "onplay",
                                                      "onplaying",
                                                      "onpointercancel",
                                                      "onpointerdown",
                                                      "onpointerenter",
                                                      "onpointerleave",
                                                      "onpointermove",
                                                      "onpointerout",
                                                      "onpointerover",
                                                      "onpointerup",
                                                      "onprogress",
                                                      "onratechange",
                                                      "onreset",
                                                      "onresize",
                                                      "onscroll",
                                                      "onscrollend",
                                                      "onsecuritypolicyviolation",
                                                      "onseeked",
                                                      "onseeking",
                                                      "onselect",
                                                      "onslotchange",
                                                      "onstalled",
                                                      "onsubmit",
                                                      "onsuspend",
                                                      "ontimeupdate",
                                                      "ontoggle",
                                                      "ontransitioncancel",
                                                      "ontransitionend",
                                                      "ontransitionrun",
                                                      "ontransitionstart",
                                                      "onvolumechange",
                                                      "onwaiting",
                                                      "onwebkitanimationend",
                                                      "onwebkitanimationiteration",
                                                      "onwebkitanimationstart",
                                                      "onwebkittransitionend",
                                                      "onwheel"};

// WindowEventHandlers - the ones that are the WINDOW's business rather than an
// element's, and the reason `Body-FrameSet-Event-Handlers.html` exists at all:
// HTML also puts these on `<body>` and `<frameset>`, where they FORWARD. See
// install_event_handler_attributes for what is and is not wired here.
constexpr std::string_view window_event_handlers[] = {
    "onafterprint",       "onbeforeprint", "onbeforeunload",       "onhashchange",
    "onlanguagechange",   "onmessage",     "onmessageerror",       "onoffline",
    "ononline",           "onpagehide",    "onpageshow",           "onpopstate",
    "onrejectionhandled", "onstorage",     "onunhandledrejection", "onunload"};

// WHERE A HANDLER IS ACTUALLY KEPT. Three slots per name and each answers a
// different question, which is why one would not do:
//
//   * `__on<name>` - what the IDL attribute was ASSIGNED. Its PRESENCE is the
//     state, not its value: `el.onclick = null` is a null that has been set,
//     and HTML says an assignment deactivates the content attribute's handler,
//     so a present null must not fall back to the markup.
//   * `__onsrc<name>` - the content attribute's text as it was when it was last
//     compiled, so a changed attribute recompiles and an unchanged one does not.
//   * `__onfn<name>`  - the function that text compiled to. `el.onclick ===
//     el.onclick` has to hold, and it would not if each read compiled again.
//
// All three are `attr_none`, so a page enumerating the object never sees them.
[[nodiscard]] std::string assigned_slot(const std::string & name) {
    return "__on" + name;
}
[[nodiscard]] std::string source_slot(const std::string & name) {
    return "__onsrc" + name;
}
[[nodiscard]] std::string compiled_slot(const std::string & name) {
    return "__onfn" + name;
}

} // namespace

// `onclick="doThing()"` AS A FUNCTION, which is the half of the event handler
// attributes that is not a property at all.
//
// HTML calls what the markup leaves behind an "internal raw uncompiled
// handler": the attribute's value is a function BODY, and it becomes a function
// the first time anything asks for the handler. The parameter is named `event`,
// which is what makes `onclick="alert(event.type)"` work.
//
// NOT CACHED PAST A CHANGE. The specification compiles once, at the moment the
// attribute is set, because the attribute-change steps are a hook it has and
// this does not - so the source text is kept beside the function and a
// different text compiles again. That is one string compare on a read whose
// slot is unset, and it is the difference between `setAttribute` taking effect
// and not.
//
// A HANDLER THAT WILL NOT COMPILE IS NULL, which HTML says in as many words:
// the uncompiled handler is discarded and the attribute reads null, rather than
// the page taking a SyntaxError from a read.
value dom_bindings::compile_handler_attribute(context & cx, value self, const std::string & name) {
    const node_id id = handle_of(self);
    if (!id || doc_ == nullptr || atoms_ == nullptr) { return value::undefined(); }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    std::string source;
    {
        const auto txn = doc_->read();
        const atom key = atoms_->intern_lower(name);
        if (!txn.has_attribute(id, key)) {
            // The attribute has GONE, and so must anything compiled from it -
            // `Body-FrameSet-Event-Handlers.html` calls removeAttribute at the
            // end of every reflection case and the next case reads null.
            (void)object->erase(source_slot(name));
            (void)object->erase(compiled_slot(name));
            return value::undefined();
        }
        source = std::string{txn.attribute_value(id, key)};
    }
    if (const value * had = object->find(source_slot(name));
        had != nullptr && had->is_string() && cx.to_string(*had) == source) {
        const value * made = object->find(compiled_slot(name));
        return made == nullptr ? value::undefined() : *made;
    }
    // THE SCOPE CHAIN, HTML 8.1.8.1 "getting the current value of the event
    // handler" step 10: the realm's global, then the element's node document,
    // then its form owner when it has one, then the element itself - each an
    // object environment, which is what `with` makes. That is why
    // `onclick="remove()"` reaches `window.remove` and not the element's
    // method (Element.prototype[@@unscopables] vetoes it, install_node_methods)
    // while `onclick="title"` reads the document's. The two outer objects are
    // PARAMETERS rather than free names so a frame document's element gets its
    // own document, not the page's global one.
    //
    // ponytail: every free identifier in the body now costs a `has` trap on
    // the document proxy, which walks the tree for named items; an id/name
    // index on the document is the upgrade if a handler-heavy page shows it.
    value form = value::null();
    {
        const auto txn = doc_->read();
        const std::string_view local = txn.local_name(id);
        if (txn.element_ns(id) == node_ns::html &&
            (local == "button" || local == "fieldset" || local == "input" || local == "object" ||
             local == "output" || local == "select" || local == "textarea")) {
            form = cx.lookup_property(self, "form");
        }
    }
    const bool with_form = form.is_object_like();
    script::program compiled = script::compiler::compile(
        std::string{"return (function (document, form) { return function (event) {\n"
                    "with (document) { "} +
        (with_form ? "with (form) { " : "") + "with (this) {\n" + source + "\n}" +
        (with_form ? " }" : "") + " } }; });");
    value made = value::undefined();
    if (compiled.ok) {
        const value outer = cx.run_nested(cx.own_program(std::move(compiled)));
        const value scope[] = {document_, form};
        if (outer.is_callable()) { made = cx.call(outer, scope); }
    }
    object->define(source_slot(name), cx.string(source), script::attr_none);
    object->define(compiled_slot(name), made, script::attr_none);
    return made;
}

// THE WINDOW-REFLECTING BODY ELEMENT EVENT HANDLER SET, HTML 8.1.8.2: six
// names that on a `<body>` or `<frameset>` ARE the Window's handler - `<body
// onload="init()">` and `document.body.onresize = f` both address the window.
[[nodiscard]] bool forwards_to_window(std::string_view name) {
    for (const std::string_view each :
         {"onblur", "onerror", "onfocus", "onload", "onresize", "onscroll"}) {
        if (name == each) { return true; }
    }
    return false;
}

// The body or frameset element `self` wraps, or none.
node_id dom_bindings::body_or_frameset_of(value self) {
    const node_id id = handle_of(self);
    if (!id || doc_ == nullptr) { return {}; }
    const auto txn = doc_->read();
    const atom tag = txn.tag(id).value_or(atom{});
    const bool is =
        txn.element_ns(id) == node_ns::html &&
        (tag == atoms_->intern_lower("body") || tag == atoms_->intern_lower("frameset"));
    return is ? id : node_id{};
}

// A forwarded CONTENT attribute reaches the window from here, lazily: there is
// no attribute-change hook, so the element's `on<name>` text is compared with
// what was last compiled from it on every read of the handler - through the
// element or through the window - and a changed text is compiled onto the
// window's slot, a removed one clears it. The element that last supplied the
// window's handler is remembered so its removal is seen from the window side
// too, and every body or frameset a script has a wrapper for - a detached
// `createElement("body")` included - is checked when the window's handler is
// read, since that is where its content attribute lands. A real hook in
// setAttribute would replace all of this.
void dom_bindings::refresh_forwarded_handler(context & cx, node_id element,
                                             const std::string & name) {
    auto * window = window_object();
    if (window == nullptr || !element) { return; }
    const value self = wrap(cx, element);
    if (!self.is_object()) { return; }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    const value * before = object->find(source_slot(name));
    const bool had = before != nullptr;
    const std::string was = had && before->is_string() ? cx.to_string(*before) : "";
    const value made = compile_handler_attribute(cx, self, name);
    const value * after = object->find(source_slot(name));
    const auto supplier = forwarded_from_.find(name);
    if (after == nullptr) {
        // Gone. If it was this element's text the window was running, the
        // window's handler goes with it ("deactivate an event handler").
        if (had && supplier != forwarded_from_.end() && supplier->second == element) {
            window->define(assigned_slot(name), value::null(), script::attr_none);
            cx.define_global(name, value::null());
            forwarded_from_.erase(supplier);
        }
        return;
    }
    if (had && cx.to_string(*after) == was) { return; } // unchanged since last seen
    const value handler = made.is_callable() ? made : value::null();
    window->define(assigned_slot(name), handler, script::attr_none);
    cx.define_global(name, handler);
    forwarded_from_[name] = element;
}

// THE GETTER. Null when unset, and "unset" is the ABSENCE of the assigned slot
// rather than a null in it - see assigned_slot.
value dom_bindings::event_handler_get(context & cx, value self, const std::string & name) {
    if (!self.is_object()) { return value::null(); }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    if (forwards_to_window(name) && window_object() != nullptr) {
        if (object == window_object()) {
            // The bodies a script can have written to: the document's own,
            // and every wrapped one. The wrapped list is rebuilt only when a
            // wrapper has been made since it was last built.
            if (wrappers_.size() != bodies_scanned_at_) {
                bodies_scanned_at_ = wrappers_.size();
                bodies_.clear();
                const auto txn = doc_->read();
                const atom body = atoms_->intern_lower("body");
                const atom frameset = atoms_->intern_lower("frameset");
                for (const auto & [packed, wrapper] : wrappers_) {
                    const node_id node = unpack(packed);
                    if (!txn.contains(node)) { continue; }
                    const atom tag = txn.tag(node).value_or(atom{});
                    if ((tag == body || tag == frameset) && txn.element_ns(node) == node_ns::html) {
                        bodies_.push_back(node);
                    }
                }
            }
            if (const node_id body = find_by_tag("body");
                body && std::ranges::find(bodies_, body) == bodies_.end()) {
                refresh_forwarded_handler(cx, body, name);
            }
            for (const node_id body : bodies_) { refresh_forwarded_handler(cx, body, name); }
        } else if (const node_id element = body_or_frameset_of(self)) {
            refresh_forwarded_handler(cx, element, name);
            object = window_object();
        }
    }
    if (const value * held = object->find(assigned_slot(name))) { return *held; }
    if (const value made = compile_handler_attribute(cx, self, name); made.is_callable()) {
        return made;
    }
    // THE BARE GLOBAL SPELLING, and only on the window. `window` IS the global
    // object in HTML, so `onerror = report` and `window.onerror = report` are
    // the same write - but the globals table and the window object are separate
    // storage here, and defining these accessors made the window's own property
    // win a lookup that used to fall through to the globals. A page that writes
    // the bare form is not doing anything unusual and would silently stop being
    // heard.
    if (object == window_object() && cx.has_global(name)) {
        if (const value held = cx.global(name); held.is_callable()) { return held; }
    }
    return value::null();
}

// THE SETTER, and the whole of it is one WebIDL annotation.
//
// `EventHandler` is `[LegacyTreatNonObjectAsNull] callback EventHandlerNonNull?`,
// which says: anything that is not an Object becomes null. So `el.onclick = ""`
// and `el.onclick = 42` both STORE NULL and both read back null - not the empty
// string, which is what a plain data property answered and what twelve of
// Body-FrameSet-Event-Handlers.html's assertions are about.
//
// An object that is not callable is stored as it is, which is the same
// annotation read the other way: only a non-Object is nulled. It throws when it
// is eventually invoked, which is where the specification puts the failure.
void dom_bindings::event_handler_set(context & cx, value self, const std::string & name,
                                     value given) {
    if (!self.is_object()) { return; }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    // `body.onload = f` IS `window.onload = f` - see forwards_to_window.
    if (forwards_to_window(name) && window_object() != nullptr && body_or_frameset_of(self)) {
        object = window_object();
    }
    const value stored = given.is_object_like() ? given : value::null();
    object->define(assigned_slot(name), stored, script::attr_none);
    // AND THE ASSIGNMENT DISCARDS THE CONTENT ATTRIBUTE'S HANDLER - HTML's
    // "deactivate an event handler". Dropping the compiled copy is what makes a
    // later read see the assignment rather than the markup.
    (void)object->erase(source_slot(name));
    (void)object->erase(compiled_slot(name));
    if (object == window_object()) { cx.define_global(name, stored); }
}

// EVERY ONE OF THEM, ON EVERY OBJECT HTML PUTS THEM ON.
//
// ONE PAIR OF NATIVES PER NAME, shared by all four hosts: the accessors find
// their object through `this`, so there is no reason for HTMLElement.prototype
// and Document.prototype to hold different functions for `onclick`.
//
// ON THE WINDOW OBJECT ITSELF rather than on Window.prototype, and this one is
// load-bearing: the window is reached through a Proxy whose `set` trap writes a
// GLOBAL for any name the target does not already own. An accessor one link up
// the prototype chain is not owned, so `window.onload = fn` would define a
// global and never run the setter.
//
// WHAT IS NOT HERE, said plainly. HTML forwards six of these from `<body>` and
// `<frameset>` to the window - `onblur`, `onerror`, `onfocus`, `onload`,
// `onresize`, `onscroll` are the WINDOW's when read or written through a body
// element - and that is not wired, because forwarding without the other half
// is worse than neither. The other half is the attribute-change hook: setting
// the CONTENT attribute on a body element has to compile the handler onto the
// window there and then, and the only place that can happen is `setAttribute`,
// which is another file. With forwarding and no hook, `body.onblur` reads the
// window's slot, which the previous case left explicitly null, and the
// reflection case that passes today would fail instead.
void dom_bindings::install_event_handler_attributes(context & cx) {
    // The interface prototypes are built on the first `wrap()`, which is after
    // this - so ask for them now. It is a no-op if they already exist and it is
    // what makes `interface_prototype` answer at all this early.
    ensure_dom_interfaces(cx);
    install_frame_accessors(cx); // the prototypes exist now, and this is the first to need one
    install_element_reflection(cx);
    install_double_reflection(cx);
    install_option_reflection(cx);
    install_form_owner(cx);
    std::vector<script::object_object *> hosts;
    for (const std::string_view interface : {"HTMLElement", "SVGElement", "Document"}) {
        if (const value proto = interface_prototype(interface); proto.is_object()) {
            hosts.push_back(static_cast<script::object_object *>(proto.as_heap()));
        }
    }
    if (auto * window = window_object()) { hosts.push_back(window); }
    const auto install = [&](std::string_view attribute, std::span<script::object_object *> where) {
        const std::string name{attribute};
        const value getter = value::object(
            cx.allocate<script::native_object>(name, [this, name](context & c, std::span<value>) {
                return event_handler_get(c, c.current_this(), name);
            }));
        const value setter = value::object(
            cx.allocate<script::native_object>(name, [this, name](context & c, std::span<value> a) {
                event_handler_set(c, c.current_this(), name, a.empty() ? value::undefined() : a[0]);
                return value::undefined();
            }));
        for (script::object_object * host : where) { host->define_accessor(name, getter, setter); }
    };
    for (const std::string_view attribute : global_event_handlers) { install(attribute, hosts); }
    // The Document's own (HTML 3.1.3 and the pointer lock, fullscreen and
    // selection specifications): null on the prototype until assigned.
    if (const value proto = interface_prototype("Document"); proto.is_object()) {
        auto * document_proto = static_cast<script::object_object *>(proto.as_heap());
        for (const std::string_view attribute :
             {"onreadystatechange", "onvisibilitychange", "onselectionchange", "onfullscreenchange",
              "onfullscreenerror", "onpointerlockchange", "onpointerlockerror"}) {
            install(attribute, std::span<script::object_object *>{&document_proto, 1});
        }
    }
    // WindowEventHandlers is the window's alone here - see the note above on
    // what forwarding would need.
    if (auto * window = window_object()) {
        for (const std::string_view attribute : window_event_handlers) {
            install(attribute, std::span<script::object_object *>{&window, 1});
        }
    }
}

// `el.onclick = fn` - an EVENT HANDLER PROPERTY, which is the other half of the
// event API and was entirely absent. addEventListener worked; assigning a
// handler stored a function nothing ever called, so a page written the older way
// simply did nothing and said nothing about it.
//
// p5.js needs it on its own load path: `loadImage` sets `img.onload` and
// `img.onerror` and awaits a promise those two settle, and `loadBytes` does the
// same with a FileReader. It is not a legacy corner here, it is the only way
// those callbacks arrive.
//
// The DEVIATION, said plainly: the spec registers an on-handler as a listener at
// the moment it is ASSIGNED, so it runs interleaved with addEventListener ones
// in registration order. Here it runs after them, in the bubble phase, once.
// Observable only by a page that mixes both for one type and depends on the
// order - and cheap, versus a listener list that must be rewritten whenever a
// property is assigned.
bool dom_bindings::fire_handler_property(value target, std::string_view type, value event,
                                         value * thrown) {
    value caught = value::undefined();
    const auto answer = [&](bool threw) {
        if (thrown != nullptr) { *thrown = caught; }
        return threw;
    };
    // `is_object_like()` AND NOT `is_object()`, and this one was load-bearing:
    // `value::is_object()` is heap_kind::object EXACTLY, and THE WINDOW IS A
    // PROXY. So this returned at the door for every window step of every
    // dispatch there has ever been, and `window.onerror`, `window.onload`,
    // `window.onclick` - every handler PROPERTY on the window - has never once
    // fired. An element's worked, which is why nothing noticed: the wrapper is
    // an ordinary object and the only test covering handler properties used
    // one. Found by asserting on window.onerror rather than by a page
    // complaining, because a handler that is never called says nothing.
    if (cx_ == nullptr || !target.is_object_like()) { return answer(false); }
    // THE NAME IS ALL-LOWERCASE AND THE TYPE IS NOT.
    //
    // Every event handler IDL attribute HTML defines is lowercase, and the type
    // it listens for is whatever the specification that named the event chose -
    // which for the prefixed CSS ones is camelCase. `onwebkitanimationend` has
    // an "event handler event type" of `webkitAnimationEnd`, so building the
    // property name by concatenation found nothing for the four legacy families
    // and would have gone on finding nothing. Folding also takes away an
    // invention: `el.onMyEvent` was a handler here for a `MyEvent` dispatch and
    // is not one in any browser, because only the attributes the IDL declares
    // are handlers at all.
    const value handler = cx_->lookup_property(target, "on" + ascii_lower_copy(type));
    if (!handler.is_callable()) { return answer(false); }
    // WHAT THE HANDLER RETURNS IS PART OF WHAT IT DID - HTML, "processing the
    // return value". A handler that returns exactly `false` CANCELS the event,
    // which is `<a onclick="return false">` and twenty years of code written
    // against it; the window's `onerror` is the one inversion, where `true`
    // means "I reported it" and cancels instead.
    //
    // EXACTLY `false`, not merely falsy. `return 0` and `return ""` do not
    // cancel in any browser - the return type is `any` and the rule names the
    // boolean, so truthiness is the wrong question to ask here.
    value returned = value::undefined();
    const auto process_return = [this, event, &returned](bool cancel_on_true) {
        if (!returned.is_boolean() || returned.as_boolean() != cancel_on_true) { return; }
        // "Cancel the event" is the DOM's set-the-canceled-flag, so it obeys
        // the same two refusals `preventDefault` does: an uncancellable event
        // cannot be cancelled and a passive listener may not try.
        if (!context::truthy(cx_->lookup_property(event, "cancelable")) ||
            flag_of(*cx_, event, passive_property)) {
            return;
        }
        static_cast<script::object_object *>(event.as_heap())
            ->set("defaultPrevented", value::boolean(true));
    };
    // `window.onerror` IS THE ONE HANDLER THAT IS NOT HANDED ITS EVENT.
    //
    // HTML's OnErrorEventHandler takes (message, filename, lineno, colno,
    // error) - five positional arguments, the first a STRING - and it is the
    // signature twenty years of shipped code is written against:
    // `window.onerror = function (msg, url, line) { report(msg) }`. Handing it
    // the event instead gives every one of them an object where a string was
    // expected, and `msg.indexOf(...)` on it throws inside the very handler
    // that exists to report a throw. dom/events/Event-dispatch-throwing.html
    // asserts `typeof e === 'string'` for exactly this reason.
    //
    // ONLY AT THE WINDOW and only for `error`: an `onerror` on an <img> is an
    // ordinary event handler and takes the event, which is the distinction the
    // specification draws with the ErrorEvent type rather than the name.
    if (type == "error" &&
        target.bits() == object_of_step(*cx_, path_step{node_id{}, listen_on::window}).bits()) {
        // AN ARRAY, because that is what the fence spreads. The five arguments
        // are built into one so that the handler still runs behind the same
        // `try` every listener does - `window.onerror` is exactly the handler a
        // page is most likely to throw out of, and
        // window-event-restored-after-throwing-onerror.html throws out of it on
        // purpose.
        const value arguments = cx_->make_array();
        auto * items = static_cast<script::array_object *>(arguments.as_heap());
        items->items.push_back(cx_->lookup_property(event, "message"));
        items->items.push_back(cx_->lookup_property(event, "filename"));
        items->items.push_back(cx_->lookup_property(event, "lineno"));
        items->items.push_back(cx_->lookup_property(event, "colno"));
        items->items.push_back(cx_->lookup_property(event, "error"));
        const bool threw = invoke_listener(*cx_, handler, target, arguments, caught, returned);
        process_return(/*cancel_on_true*/ true);
        return answer(threw);
    }
    const bool threw = invoke_listener(*cx_, handler, target, event, caught, returned);
    process_return(/*cancel_on_true*/ false);
    return answer(threw);
}

} // namespace ctbrowser::shell
