#pragma once

#include "stylesheets_types.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

// A FETCH THAT HAS NOT HAPPENED YET: the work finishes on a later turn, so
// other timers and listeners run meanwhile and an AbortController has
// something to abort.
struct pending_fetch {
    value promise;
    std::string url;
    value signal; // the AbortSignal it was given, if any
};

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

// Which frames are loaded, and from what. The `src` is kept as WRITTEN
// rather than resolved, because that is the string the next reconcile
// compares against - a page that assigns the same src twice must not
// reload, and one that assigns a different one must.
struct frame_entry {
    node_id element; // the <iframe>
    std::string src;
    dom_bindings * bindings; // over the frame's document
};

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

// WHERE A URL'S BYTES COME FROM - the asset registry, a file beside the
// page, the network when allowed - one answer for fetch() and
// XMLHttpRequest. `failure` is set when nothing was fetched at all (a
// network error); a 404 is a status.
struct loaded_resource {
    int status = 200;
    std::string type;
    std::vector<std::byte> body;
    std::string failure;
};

} // namespace binding_detail

} // namespace ctbrowser::shell
