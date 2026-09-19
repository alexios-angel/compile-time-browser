#include "internal.hpp"

#include "handler_names.hpp"
#include <ctbrowser/script/compile.hpp>

namespace ctbrowser::shell {

using namespace detail;

namespace detail {

std::string describe_thrown(context & cx, value thrown) {
    if (!thrown.is_object()) { return cx.to_string(thrown); }
    const value name = cx.lookup_property(thrown, "name");
    const value message = cx.lookup_property(thrown, "message");
    if (name.is_undefined() && message.is_undefined()) { return "an exception"; }
    std::string text = name.is_undefined() ? std::string{"Error"} : cx.to_string(name);
    const std::string body = message.is_undefined() ? std::string{} : cx.to_string(message);
    if (!body.empty()) { text += ": " + body; }
    return text;
}

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
    // `composedPath()` IS PER LISTENER, NOT PER DISPATCH. DOM 2.9 builds it
    // from the INVOCATION TARGET, and a node inside a closed shadow tree that
    // the listener is not itself in is left out: a page that put a listener on
    // the document must not learn what is inside a closed tree from the path.
    // An OPEN tree hides nothing - being reachable through `shadowRoot` is
    // what open means.
    const auto closed_to = [&](const path_step & item, const path_step & from) {
        if (item.on != listen_on::node || !item.node) { return false; }
        const auto txn = doc_->read();
        node_id a = item.node;
        for (int guard = 0; a && guard < 64; ++guard) {
            const node_id root = root_of_tree(txn, a, false);
            const shadow_tree * tree = shadow_tree_of(root);
            if (tree == nullptr) { return false; }
            if (!tree->open) {
                // Is the closed root a shadow-including inclusive ancestor of
                // the listener's own node? The walk `retarget` uses, and a
                // listener that is not on a node at all - the document, the
                // window - is never inside one.
                bool holds = false;
                for (node_id b = from.on == listen_on::node ? from.node : node_id{}; b && !holds;) {
                    holds = b == root;
                    const node_id up = txn.parent(b);
                    const shadow_tree * above = up ? nullptr : shadow_tree_of(b);
                    b = up ? up : above == nullptr ? node_id{} : above->host;
                }
                if (!holds) { return true; }
            }
            a = tree->host;
        }
        return false;
    };
    const auto visit = [&](std::size_t index, double otherwise) {
        const path_step & step = path[index];
        node_id shown = at.node;
        if (target_in_shadow) {
            shown = shown_steps[index];
            const value shown_object = wrap(cx, shown);
            object->set("target", shown_object);
            object->set("srcElement", shown_object);
            expose(hidden_steps[index] ? value::undefined() : event);
            // The path AS THIS STEP MAY SEE IT. Rebuilt per step rather than
            // filtered by the method, for the reason the whole list is built
            // here: the tree may have moved by the time a page asks.
            value listed = cx.make_array();
            auto * items = static_cast<script::array_object *>(listed.as_heap());
            for (const path_step & one : path) {
                if (!closed_to(one, step)) { items->items.push_back(object_of_step(cx, one)); }
            }
            object->set(std::string{path_property}, listed);
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
    // event is dispatched, not at some later frame. NOT from inside the
    // mutation observer microtask, though: a checkpoint does not nest
    // (HTML's "performing a microtask checkpoint" flag).
    if (!primary().delivering_mutations_) { cx.drain_microtasks(); }
    note_callback_fault(type);
    return prevented(event);
}

} // namespace ctbrowser::shell
