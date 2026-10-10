//===- dispatch.cpp - Event dispatch ----------------------------*- C++ -*-===//
//
// DOM event dispatch (DOM 2.9): building the event path with shadow-tree
// retargeting, running capture and bubble listeners, click activation
// behaviour, and the microtask checkpoint after an event. Also the shared
// initialisation every Event object gets.
//
//===----------------------------------------------------------------------===//
#include "internal.hpp"

#include "handler_names.hpp"
#include <ctbrowser/script/compile.hpp>

namespace ctbrowser::shell {

using namespace detail;

namespace detail {

/// A one-line description of a thrown value for error reports: "Name:
/// message" for an error object, else its string conversion.
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

/// The realm's single `isTrusted` getter, fetched from Event.prototype.
///
/// Every event carries it as an own accessor (see initialise_event), and WPT
/// dom/events/Event-isTrusted.any.js checks that two events' getters are the
/// same function, so a fresh native per event would fail. Keeping the one copy
/// in Event.prototype's accessor table needs no private slot and nothing extra
/// for the collector to trace.
[[nodiscard]] value is_trusted_getter_of(value event_prototype) {
    if (!event_prototype.is_object()) { return value::undefined(); }
    auto * proto = static_cast<script::object_object *>(event_prototype.as_heap());
    const script::accessor_entry * entry = proto->find_accessor("isTrusted");
    return entry == nullptr ? value::undefined() : entry->getter;
}

/// Writes onto `event` everything `new Event`, `new MouseEvent`,
/// `document.createEvent` and the engine's own input events have in common.
///
/// Writing onto the receiver, not a new object, is what lets
/// `class X extends Event` work. Methods live on Event.prototype, not here.
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
    // Declared here rather than created by the first passive listener that
    // runs, so the write in fire_at updates a slot instead of growing the
    // table in the middle of a dispatch.
    event.set(std::string{passive_property}, value::boolean(false));
    // Not trusted and not initialised: the page-facing paths start from both,
    // and the callers that know better set them afterwards.
    event.define(trusted_property, value::boolean(false), script::attr_none);
    event.define(initialised_property, value::boolean(false), script::attr_none);
    // `isTrusted` is [LegacyUnforgeable], which WebIDL defines as an own
    // property of every instance sharing one getter per realm rather than an
    // inherited one. Both halves are observable: getOwnPropertyDescriptor on an
    // instance must find it, and the getters of two instances must be the same
    // function.
    //
    // Test is_callable(), not is_object(): is_object() is true only for
    // heap_kind::object, and a native getter is heap_kind::native.
    if (is_trusted_getter.is_callable()) {
        event.define_accessor("isTrusted", is_trusted_getter, value::undefined(),
                              script::attr_enumerable);
    } else {
        event.set("isTrusted", value::boolean(false));
    }
}

} // namespace detail

/// DOM 2.9 dispatch: capture down the path, then bubble back up. Returns
/// whether the default was prevented.
///   * `currentTarget` and `eventPhase` are set before each step's listeners
///     and cleared after the dispatch.
///   * Propagation flags are checked between steps; the immediate flag between
///     listeners at one step.
///   * A non-bubbling event runs the bubble pass at target steps only.
///   * `preventDefault` counts only when `cancelable`.
bool dom_bindings::dispatch_to(value event, path_step at) {
    if (cx_ == nullptr || !event.is_object()) { return false; }
    context & cx = *cx_;
    // In this document's realm: `window.event` and a window's `on<type>` are
    // its realm's globals (frames.cpp).
    const auto in_realm = enter_realm(cx);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    // The two refusals come before anything else: concept-event-dispatch is
    // not reached at all when either flag is wrong, and a nested dispatch of
    // an event that is already travelling would otherwise reset the flags the
    // outer one is reading.
    if (flag_of(cx, event, dispatch_property)) {
        throw_dom_exception(cx, "InvalidStateError",
                            "Failed to execute 'dispatchEvent': the event is already being "
                            "dispatched.");
        return false;
    }
    // An event nobody initialised: `document.createEvent` hands back one with
    // no type, and dispatching it is an error the specification names rather
    // than a dispatch to a listener called "". An object that is not one of
    // ours has no flag at all and is refused the same way.
    const value * initialised = object->find(std::string{initialised_property});
    if (initialised == nullptr || !context::truthy(*initialised)) {
        throw_dom_exception(cx, "InvalidStateError",
                            "Failed to execute 'dispatchEvent': the event has not been "
                            "initialised.");
        return false;
    }
    // Refresh wrappers before the listeners run: a handler for `input` reads
    // the field's new value, so a wrapper must not still hold the old one.
    (void)refresh_wrappers();
    const std::string type = cx.to_string(cx.lookup_property(event, "type"));
    std::vector<path_step> path =
        propagation_path(at, context::truthy(cx.lookup_property(event, "composed")));
    const bool bubbles = context::truthy(cx.lookup_property(event, "bubbles"));

    // The event path's structs (DOM 2.9 dispatch step 5): what each step knows
    // besides its invocation target. That is its shadow-adjusted target
    // (non-null at the target and at every host the target was retargeted to;
    // those are the AT_TARGET steps), the relatedTarget retargeted against it, whether
    // it is inside a shadow tree (then `window.event` is not set for it), and
    // the two closed-tree marks composedPath() filters by. Fixed as the path
    // is built, before any listener runs, so a listener that moves a node
    // (relatedTarget.window.js "part 2", event-global-extra.window.js) changes
    // nothing later steps see.
    //
    // A document with no shadow root has the trivial answer (the target is
    // the only adjusted step and nothing is retargeted) and pays nothing.
    const value related_value = cx.lookup_property(event, "relatedTarget");
    const node_id related = handle_of(related_value);
    const bool shadows = doc_ != nullptr && doc_->has_shadow_roots();
    struct hop {
        bool adjusted = false; // has a shadow-adjusted target...
        node_id target;        // ...which is this node, or `at` itself when empty
        node_id related;       // relatedTarget retargeted against this step
        bool in_shadow = false;
        bool closed_root = false;
        bool closed_slot = false;
    };
    std::vector<hop> hops(path.size());
    if (!hops.empty()) {
        hops.front().adjusted = true;
        hops.front().target = at.node;
    }
    // DOM 4.4 "retarget": A climbs out of every shadow tree whose root is not
    // a shadow-including inclusive ancestor of B; the document and the window
    // (no node) pull A all the way into the light tree.
    const auto holds = [&](const read_txn & txn, node_id root, node_id b) {
        while (b) {
            if (b == root) { return true; }
            const node_id up = txn.parent(b);
            const shadow_tree * above = up ? nullptr : shadow_tree_of(b);
            b = up ? up : above == nullptr ? node_id{} : above->host;
        }
        return false;
    };
    const auto retarget = [&](node_id a, const path_step & against) {
        const auto txn = doc_->read();
        for (int guard = 0; a && guard < 64; ++guard) {
            const node_id root = root_of_tree(txn, a, false);
            const shadow_tree * tree = shadow_tree_of(root);
            if (tree == nullptr) { return a; }
            if (against.on == listen_on::node && holds(txn, root, against.node)) { return a; }
            a = tree->host;
        }
        return a;
    };
    const auto in_shadow = [&](node_id node) {
        const auto txn = doc_->read();
        return node && shadow_tree_of(root_of_tree(txn, node, false)) != nullptr;
    };
    if (shadows) {
        node_id target = at.node;
        bool slotted = at.on == listen_on::node && at.node && assigned_slot_of(at.node);
        for (std::size_t i = 0; i < path.size(); ++i) {
            const path_step & step = path[i];
            hop & h = hops[i];
            if (related) { h.related = retarget(related, step); }
            const auto txn = doc_->read();
            const shadow_tree * tree = step.on == listen_on::node
                                           ? shadow_tree_of(root_of_tree(txn, step.node, false))
                                           : nullptr;
            h.in_shadow = tree != nullptr;
            h.closed_root = tree != nullptr && !tree->open && shadow_tree_of(step.node) != nullptr;
            if (i == 0) { continue; }
            // 5.9.1: the slot a slotted node was handed to, in a closed tree.
            h.closed_slot = slotted && tree != nullptr && !tree->open;
            slotted = step.on == listen_on::node && assigned_slot_of(step.node);
            // 5.9.5: a step inside the target's tree (or the document, or the
            // window) is an ordinary one.
            if (step.on != listen_on::node ||
                (target && holds(txn, root_of_tree(txn, target, false), step.node))) {
                continue;
            }
            // 5.9.6: the path ends where the retargeted relatedTarget is the
            // step; a focus moving within a host's tree is not news to it.
            if (related && h.related == step.node) {
                path.resize(i);
                hops.resize(i);
                break;
            }
            // 5.9.7: a host, which the target is retargeted to.
            target = step.node;
            h.adjusted = true;
            h.target = target;
        }
    }
    // A relatedTarget that retargets to the target itself while not being it
    // (step 5's condition) is a dispatch that does not happen: the targets are
    // cleared and nothing is invoked (WPT relatedTarget.window.js, "Reset
    // targets on early return").
    if (shadows && related && at.on == listen_on::node && !hops.empty() &&
        hops.front().related == at.node && related != at.node) {
        object->set("target", value::null());
        object->set("srcElement", value::null());
        object->set("relatedTarget", value::null());
        object->set(std::string{cancel_bubble_property}, value::boolean(false));
        object->set(std::string{stop_immediate_property}, value::boolean(false));
        return prevented(event);
    }
    // Steps 5.10-5.11, decided here too: what the event names as `target` and
    // `relatedTarget` once it has stopped travelling (the last adjusted
    // step's pair), and whether both are cleared because either is still
    // inside a shadow tree (`clearTargets`).
    std::size_t last_adjusted = 0;
    for (std::size_t i = 0; i < hops.size(); ++i) {
        if (hops[i].adjusted) { last_adjusted = i; }
    }
    const bool clear_targets =
        shadows && !hops.empty() &&
        (in_shadow(hops[last_adjusted].target) || in_shadow(hops[last_adjusted].related));
    const bool closed_anywhere =
        std::ranges::any_of(hops, [](const hop & h) { return h.closed_root || h.closed_slot; });

    // The activation target (DOM 2.9 dispatch, steps 5.5 to 5.9.11) and the
    // legacy-pre-activation behaviour, which runs before any listener. A
    // `click` that is a MouseEvent (not a plain `new Event("click")`) has one
    // node with activation behaviour: the target itself if it has any, else,
    // only when the event bubbles, the first ancestor on the path that does.
    // A checkbox or radio there is toggled now, so a click listener reads the
    // new checkedness; what it toggled from is kept so a preventDefault can
    // put it back. The other half (the activation behaviour proper, or the
    // legacy-canceled one) is at the end of this function.
    node_id activation_target;
    bool legacy_toggled = false;
    bool legacy_was_checked = false;
    std::optional<bool> legacy_was_indeterminate; // a checkbox's only
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
            // Disabled does not matter here: the mouse never gets this far at
            // a disabled control and `click()` refuses before dispatching, but
            // `dispatchEvent` reaches it and toggles it (WPT
            // Event-dispatch-click.html).
            if (kind == control_kind::checkbox || kind == control_kind::radio) {
                const bool checked = forms_->state_of(txn, *atoms_, activation_target).checked;
                if (kind == control_kind::checkbox || !checked) {
                    legacy_was_checked = checked;
                    if (kind == control_kind::radio) {
                        // The radio this one is about to uncheck, so a cancelled
                        // click can check it again; the same group walk
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
                    // ...and a checkbox's sets `indeterminate` to false, which
                    // the cancelled activation puts back with the checkedness
                    // (WPT checkbox.html's pre-activation subtests).
                    if (kind == control_kind::checkbox) {
                        auto & state = forms_->state_of(txn, *atoms_, activation_target);
                        legacy_was_indeterminate = state.indeterminate;
                        state.indeterminate = false;
                    }
                    forms_->toggle(txn, *atoms_, activation_target, kind);
                    legacy_toggled = true;
                }
            }
        }
    }

    // With the legacy target override flag the target is the Document, though
    // the path is the window's alone (dispatch step 2).
    const value target_object =
        at.on == listen_on::window && object->find(legacy_target_override_property) != nullptr
            ? document_
            : object_of_step(cx, at);
    object->set("target", target_object);
    object->set("srcElement", target_object);
    // `offsetX`/`offsetY` are relative to the target's box (CSSOM View). Set
    // here, once per dispatch, because this is the one place both constructed
    // and engine mouse events pass through with the target known.
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

    object->set(std::string{stop_immediate_property}, value::boolean(false));
    object->set(std::string{dispatch_property}, value::boolean(true));
    ++dispatch_depth_;
    // `composedPath()` is the path as a list, and it is empty outside a
    // dispatch. Built here rather than recomputed by the method, because the
    // tree may have moved by the time a page asks, and per listener when a
    // closed tree is on the path: DOM 2.10 hides a closed tree's nodes from a
    // listener that is not itself inside it, by counting the closed roots and
    // closed-tree slots between that listener's step and each other step.
    const auto composed_path_for = [&](std::size_t current) {
        value listed = cx.make_array();
        auto & items = static_cast<script::array_object *>(listed.as_heap())->items;
        int level = 0;
        for (std::size_t k = path.size(); k-- > 0;) {
            if (hops[k].closed_root) { ++level; }
            if (k == current) { break; }
            if (hops[k].closed_slot) { --level; }
        }
        std::vector<value> before;
        int now = level;
        int most = level;
        for (std::size_t k = current; k-- > 0;) {
            if (hops[k].closed_root) { ++now; }
            if (now <= most) { before.push_back(object_of_step(cx, path[k])); }
            if (hops[k].closed_slot && --now < most) { most = now; }
        }
        items.assign(before.rbegin(), before.rend());
        items.push_back(object_of_step(cx, path[current]));
        now = most = level;
        for (std::size_t k = current + 1; k < path.size(); ++k) {
            if (hops[k].closed_slot) { ++now; }
            if (now <= most) { items.push_back(object_of_step(cx, path[k])); }
            if (hops[k].closed_root && --now < most) { most = now; }
        }
        return listed;
    };
    if (!path.empty()) { object->set(std::string{path_property}, composed_path_for(0)); }

    // `window.event` belongs to the listener's global, set around each call by
    // fire_at (DOM 2.10 "inner invoke"); what a step contributes is whether it
    // is inside a shadow tree, fixed with the path. Saved for the dispatch this
    // one may be nested in.
    const context::rooted keep_event{cx, event};
    const bool outer_in_shadow = step_in_shadow_;

    const auto stopped = [&] { return flag_of(cx, event, cancel_bubble_property); };
    // One step's bookkeeping, DOM 2.9 "invoke" steps 1-5: `target` is the
    // shadow-adjusted target of the nearest adjusted step at or before this
    // one, `relatedTarget` this step's retargeted one, `window.event` hidden
    // from a listener inside a shadow tree, then `currentTarget` and
    // `eventPhase`. Answers whether this step is AT_TARGET, which a host the
    // target was retargeted to also is, so a listener there sees phase 2 and
    // hears a non-bubbling event.
    const auto visit = [&](std::size_t index, double otherwise) {
        const path_step & step = path[index];
        step_in_shadow_ = shadows && hops[index].in_shadow;
        if (shadows) {
            std::size_t shown = index;
            while (shown > 0 && !hops[shown].adjusted) { --shown; }
            const value shown_object =
                hops[shown].target ? wrap(cx, hops[shown].target) : target_object;
            object->set("target", shown_object);
            object->set("srcElement", shown_object);
            if (related) { object->set("relatedTarget", wrap(cx, hops[index].related)); }
            if (closed_anywhere) {
                object->set(std::string{path_property}, composed_path_for(index));
            }
        }
        object->set("currentTarget", object_of_step(cx, step));
        object->set("eventPhase", value::number(hops[index].adjusted ? 2 : otherwise));
        return hops[index].adjusted;
    };

    // Capture: from the window down to the target. The target's own capturing
    // listeners run here, at phase AT_TARGET rather than CAPTURING_PHASE.
    for (std::size_t i = path.size(); i-- > 0;) {
        if (stopped()) { break; }
        (void)visit(i, 1);
        fire_at(path[i], type, event, true);
    }
    // Bubble: back up. A non-bubbling event gets this pass at the target only;
    // `continue` and not `break`, because a shadow host further up is a target
    // too and the steps between are merely skipped.
    //
    // A `submit` or `reset` goes no further than a form that is not its
    // target: that form neither hears it nor passes it on. HTML does not write
    // this down; Blink (HTMLFormElement::HandleLocalEvents) and Gecko
    // (HTMLFormElement::WillHandleEvent) both do it, because a form nested in
    // another (which only a script can build) submits itself and not its
    // ancestor (WPT Event-dispatch-single-activation-behavior.html counts one
    // `onsubmit` per click).
    const bool form_event = type == "submit" || type == "reset";
    const auto is_html_form = [&](const path_step & step) {
        if (step.on != listen_on::node) { return false; }
        const auto txn = doc_->read();
        return txn.element_ns(step.node) == node_ns::html && txn.local_name(step.node) == "form";
    };
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (stopped()) { break; }
        const bool at_target = visit(i, 3);
        if (form_event && !at_target && is_html_form(path[i])) { break; }
        if (!at_target && !bubbles) { continue; }
        fire_at(path[i], type, event, false);
    }

    // After the dispatch the event is not travelling any more, and the two
    // properties that say where it is must say so: a page keeps the object
    // and reads them later.
    if (shadows && !hops.empty()) {
        // concept-event-dispatch steps 5.10-5.11 and 11: the target is the
        // last adjusted step's target and the relatedTarget its retargeted
        // one, and both are null when either is still inside a shadow tree
        // (`clearTargets`), so nothing of a closed tree is left on the object.
        const hop & last = hops[last_adjusted];
        const value final_target = clear_targets ? value::null()
                                   : last.target ? wrap(cx, last.target)
                                                 : target_object;
        object->set("target", final_target);
        object->set("srcElement", final_target);
        object->set("relatedTarget", clear_targets ? value::null()
                                     : related     ? wrap(cx, last.related)
                                                   : related_value);
    }
    object->set("currentTarget", value::null());
    object->set("eventPhase", value::number(0));
    object->set(std::string{dispatch_property}, value::boolean(false));
    // The propagation flags are unset (concept-event-dispatch step 14), so an
    // event object dispatched a second time reaches its listeners. Clearing
    // them here rather than on entry is what lets `stopPropagation()` called
    // before a dispatch stop that dispatch.
    object->set(std::string{cancel_bubble_property}, value::boolean(false));
    object->set(std::string{stop_immediate_property}, value::boolean(false));
    object->set(std::string{path_property}, cx.make_array());
    step_in_shadow_ = outer_in_shadow;
    // A `once` listener is removed after the dispatch, not during it: erasing
    // from the vector being walked would skip a later listener. Not after a
    // nested dispatch either: a listener may dispatch, and the inner
    // compaction would shift the list the outer loop is indexing.
    --dispatch_depth_;
    reap_spent_listeners();
    // The activation behaviour, DOM 2.9 dispatch step 11, after the event has
    // stopped travelling: `eventPhase` is NONE, `currentTarget` null, the path
    // empty and the dispatch flag down, all of which a `change` listener can
    // read off the click that caused it, and one of which lets that listener
    // dispatch the same event again. Before the microtask drain, because a
    // microtask a click listener queued must find the checkedness a
    // preventDefault put back, not the one it saw.
    if (activation_target) {
        if (!prevented(event)) {
            activation_event_target = at.node;
            run_activation_behavior(cx, activation_target);
        } else if (legacy_toggled) {
            // The legacy-canceled-activation behaviour: the checkedness the
            // pre-activation changed goes back (a checkbox's indeterminate
            // with it), and so does the radio it unchecked.
            const auto txn = doc_->read();
            auto & state = forms_->state_of(txn, *atoms_, activation_target);
            state.checked = legacy_was_checked;
            if (legacy_was_indeterminate) { state.indeterminate = *legacy_was_indeterminate; }
            if (legacy_was_checked_radio) {
                forms_->state_of(txn, *atoms_, legacy_was_checked_radio).checked = true;
            }
        }
    }
    // Microtask checkpoint after an event, as HTML's "clean up after running
    // script": only with an empty script stack (so `el.click()` from script
    // drains after that script) and never while mutation observers are being
    // delivered (checkpoints do not nest).
    if (!primary().delivering_mutations_ && cx.current_stack().empty()) { cx.drain_microtasks(); }
    // Report a listener fault and clear it; otherwise the VM's failure flag stays
    // set and every later callback is refused.
    note_callback_fault(type);
    return prevented(event);
}

} // namespace ctbrowser::shell
