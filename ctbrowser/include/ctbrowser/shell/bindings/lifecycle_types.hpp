//===- lifecycle_types.hpp - Lifecycle records ------------------*- C++ -*-===//
//
// Plain data records dom_bindings keeps for loaded frames, timers, the event
// path and its listeners, and CSSOM View geometry. The records chain-include
// each other and are all reached through bindings.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "helpers.hpp"
#include <ctbrowser/shell/listener_registry.hpp>

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

/// A frame this document has loaded, with the bindings over its document, so
/// the browser can run a frame's document through the same stages as the page
/// at the size its <iframe> box got (browser::layout_frames). The bindings
/// deliberately know nothing of that pipeline; this is the list it walks.
struct loaded_frame {
    node_id element;
    dom_bindings * bindings;
};

/// One setTimeout or setInterval registration.
struct timer {
    std::uint32_t id = 0;
    value callback;
    std::vector<value> arguments; // setTimeout's third and later arguments.
    /// The frame window it was set on, or undefined for the page's own: a timer
    /// of a frame that has since been removed never fires (its document is not
    /// fully active).
    value window = value::undefined();
};

using listen_on = event_target_kind;

/// One stop on the path an event travels: a node, one of the two event
/// targets that have no node (document and window), or a standalone
/// EventTarget.
struct path_step {
    node_id node;                    // Empty unless `on` is `node`.
    listen_on on = listen_on::node;  // Which kind of target this is.
    value host = value::undefined(); // Set only when `on` is `object`.
};

/// The listener registry key for \p at. Every path constructor goes through
/// this, so each target has one canonical key; document and window stay
/// distinct although neither has a node.
[[nodiscard]] inline listener_target listener_target_of(const path_step & at) {
    if (at.on == listen_on::node) { return {at.on, at.node.key()}; }
    if (at.on == listen_on::object) { return {at.on, at.host.bits()}; }
    return {at.on, 0};
}

/// What dom_bindings stores per event listener.
struct listener_payload {
    value callback;
    /// Aborting this signal removes every listener that carries it.
    value abort_signal = value::undefined();
    /// The standalone EventTarget this listener is registered on, if any.
    /// A GC root: nothing else may be holding it while a listener is.
    value host = value::undefined();
};

using listener = listener_registry<listener_payload>::entry;

// --- CSSOM View geometry (bindings/element/views.cpp) -------------------

/// The first fragment for a node in tree order, with its absolute border box:
/// what box_of answers, plus the fragment itself for the edges and the
/// scrolling area. `f` is null when the node has no box.
struct located {
    const layout::fragment * f = nullptr;
    rect abs;
    /// How far `transform: translate()` on the box and its ancestors moved it:
    /// the part offsetTop and an image's `x` ignore.
    point translation;
};

/// A named native method to install on a prototype.
struct operation {
    std::string name;
    script::native_fn fn;
};

} // namespace binding_detail

} // namespace ctbrowser::shell
