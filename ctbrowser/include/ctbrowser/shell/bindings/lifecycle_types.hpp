#pragma once

#include "helpers.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

// THE FRAMES THIS DOCUMENT HAS LOADED, each with the bindings over its
// document, so the browser can run a frame's document through the same
// stages as the page at the size its <iframe> box got (browser::
// layout_frames). The bindings deliberately know nothing of that pipeline;
// this is the list it walks.
struct loaded_frame {
    node_id element;
    dom_bindings * bindings;
};

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
    // THE EVENT HANDLER'S LISTENER (HTML 8.1.8.1): appended when the
    // handler - the IDL attribute or the content attribute - was first
    // set to something, removed when it is deactivated, and at its
    // place in the list it runs the CURRENT handler property. `callback`
    // is a placeholder object that gives it an identity.
    bool handler = false;
};

// --- CSSOM VIEW GEOMETRY (bindings/element/views.cpp) -------------------
// The first fragment for a node in tree order, with its absolute border
// box - what box_of answers, plus the fragment itself for the edges and
// the scrolling area. `f` is null when the node has no box.
struct located {
    const layout::fragment * f = nullptr;
    rect abs;
    // What `transform: translate()` on the box and its ancestors moved it
    // by - the part offsetTop and an image's `x` "ignore".
    point translation;
};

struct box_geometry {
    rect bounds;
    transform to_viewport;
    [[nodiscard]] rect bounding_rect() const;
};

struct operation {
    std::string name;
    script::native_fn fn;
};

} // namespace binding_detail

} // namespace ctbrowser::shell
