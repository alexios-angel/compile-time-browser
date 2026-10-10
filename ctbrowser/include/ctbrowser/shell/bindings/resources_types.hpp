//===- resources_types.hpp - Resource records -------------------*- C++ -*-===//
//
// Plain data records dom_bindings keeps for loads that finish on a later turn
// (fetches, images, frames, FileReader reads) and for loaded resources.
// The records chain-include each other and are all reached through
// bindings.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "stylesheets_types.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

/// A fetch that has not happened yet. The work finishes on a later turn, so
/// other timers and listeners run meanwhile and an AbortController has
/// something to abort.
struct pending_fetch {
    value promise;
    std::string url;
    value signal; // The AbortSignal it was given, if any.
};

/// An image load that has not happened yet: `img.src = url` returns
/// immediately and the page hears about it through `onload` on a later turn.
/// Firing synchronously from the setter would work for handlers assigned
/// before src (as p5 writes it) but fire at nothing for
/// `img.src = url; img.onload = f`.
struct pending_image {
    value target; // The <img> wrapper whose src was assigned.
    node_id id;
    std::string url;
    value promise; // decode()'s promise; undefined for a plain src assignment.
};

/// A frame (or sheet or script) whose `load` event has not fired yet. The
/// document is built synchronously, since the bytes are already on disk or in
/// the registry, but the event waits for a later turn for the same reason an
/// image's does: `document.body.appendChild(frame)` is often followed by
/// `frame.onload = f`.
struct pending_frame {
    node_id id;
    bool ok = false; // False when the src resolved to no bytes.
    /// A sheet or script announcing its `load`, as opposed to an <iframe>:
    /// not a callback the page scheduled, so the drain does not count it.
    bool resource = false;
    /// The bindings whose element `id` names, when it is not the queue's
    /// own: a frame document's nested frame lands on the primary's queue.
    dom_bindings * owner = nullptr;
};

/// A loaded frame and where it came from. The `src` is kept as written, not
/// resolved, because that is the string the next reconcile compares against:
/// a page that assigns the same src twice must not reload, and one that
/// assigns a different one must.
struct frame_entry {
    node_id element; // The <iframe>.
    std::string src;
    dom_bindings * bindings; // Over the frame's document.
};

/// What a FileReader read produces. A read finishes on a later turn for the
/// same reason an image load does: a page assigns `onload` after calling
/// readAsText, so a synchronous reader would fire before the handler existed.
enum class read_kind : std::uint8_t {
    text,
    data_url,
    array_buffer,
    binary_string
};

/// A FileReader read that has not finished yet.
struct pending_read {
    value reader;
    value blob;
    read_kind kind;
};

/// A URL's bytes, from the asset registry, a file beside the page, or the
/// network when allowed: one answer for fetch() and XMLHttpRequest. `failure`
/// is set when nothing was fetched at all (a network error); a 404 is a
/// status.
struct loaded_resource {
    int status = 200;
    std::string type;
    std::vector<std::byte> body;
    std::string failure;
};

} // namespace binding_detail

} // namespace ctbrowser::shell
