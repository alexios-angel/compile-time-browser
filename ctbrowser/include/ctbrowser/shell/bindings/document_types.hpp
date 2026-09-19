#pragma once

#include "resources_types.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

// `performance.getEntries()`. Plain data: the objects a page sees are built
// when it asks, with PerformancePaintTiming.prototype behind them.
struct performance_entry {
    std::string name;
    std::string type;
    double start_ms;
};

// The map of animation frame callbacks (HTML 8.9.2): handle -> callback,
// in registration order. `cancelAnimationFrame` removes an entry, and
// "run the animation frame callbacks" runs the entries of a COPY that are
// still in the map - so a callback cancelled by an earlier one this frame
// does not run.
struct animation_frame_callback {
    std::uint32_t id = 0;
    value callback;
};

// Every boundary of every live range that is a node of THIS document:
// the range, its two slot names and the boundary.
struct live_boundary {
    script::object_object * range = nullptr;
    std::string_view node_slot;
    std::string_view offset_slot;
    node_id node;
    double offset = 0;
};

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

// --- THE PARSER-DRIVEN DOCUMENT - bindings/document/write.cpp -----------
//
// HTML 8.4 dynamic markup insertion and 13.2.6 "in text" on `</script>`:
// the page's parse runs HERE, interleaved with its scripts, and
// `document.open/write/writeln/close` reach the same parser. The browser
// supplies only how a script is RUN - its compile cache, its module loader
// and its packager hooks are the browser's; the parser, the insertion
// point, the defer/async sets and readyState are the document's.
struct parser_script {
    node_id element;
    std::string source;
    std::string specifier; // a module's registry key (its src, or synthetic)
    bool module = false;
    // The src could not be fetched: nothing runs, `error` fires at the
    // element, and the runner is told so the embedder can report it.
    bool missing = false;
};

} // namespace binding_detail

} // namespace ctbrowser::shell
