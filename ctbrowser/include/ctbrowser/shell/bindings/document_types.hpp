//===- document_types.hpp - Document binding records ------------*- C++ -*-===//
//
// Plain data records dom_bindings keeps for its document: performance entries,
// animation frame callbacks, live range boundaries, replaceChildren notes and
// the scripts the parser hands over. The records chain-include each other and
// are all reached through bindings.hpp, which includes this one.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "resources_types.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

/// One `performance.getEntries()` entry. Plain data: the objects a page sees
/// are built when it asks, with PerformancePaintTiming.prototype behind them.
struct performance_entry {
    std::string name;
    std::string type;
    double start_ms;
};

/// One entry of the map of animation frame callbacks (HTML 8.9.2), kept in
/// registration order. `cancelAnimationFrame` removes an entry, and "run the
/// animation frame callbacks" runs the entries of a copy that are still in the
/// map, so a callback cancelled by an earlier one this frame does not run.
struct animation_frame_callback {
    std::uint32_t id = 0;
    value callback;
    const dom_bindings * document = nullptr; // Whose window asked; null when none did.
};

/// One boundary of a live Range whose node is in this document: the range,
/// the names of its node and offset slots, and the boundary itself.
struct live_boundary {
    script::object_object * range = nullptr;
    std::string_view node_slot;
    std::string_view offset_slot;
    node_id node;
    double offset = 0;
};

/// A "replace all" (DOM 4.2.3), which the diff cannot see whole:
/// `replaceChildren(x)` where x was already a child queues one record removing
/// every old child and adding x, but the tree afterwards shows only that the
/// others went. The caller notes it here before the mutated() that follows, and
/// record_mutations emits exactly this record for the parent instead of a diff.
struct replace_all_note {
    node_id parent;
    std::vector<node_id> removed;
    std::vector<node_id> added;
};

// --- The parser-driven document (bindings/document/write.cpp) ----------
//
// HTML 8.4 dynamic markup insertion and 13.2.6 "in text" on `</script>`: the
// page's parse runs in the bindings, interleaved with its scripts, and
// `document.open/write/writeln/close` reach the same parser. The browser
// supplies only how a script is run (its compile cache, module loader and
// packager hooks); the parser, the insertion point, the defer/async sets and
// readyState belong to the document.

/// One script the parser hands over to be run.
struct parser_script {
    node_id element;
    std::string source;
    std::string specifier; // A module's registry key (its src, or synthetic).
    bool module = false;
    /// The src could not be fetched: nothing runs, `error` fires at the
    /// element, and the runner is told so the embedder can report it.
    bool missing = false;
};

} // namespace binding_detail

} // namespace ctbrowser::shell
