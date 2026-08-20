#pragma once
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/node.hpp>
#include <ctbrowser/dom/treebuilder.hpp>

// HTML into the DOM.
//
// Through stage 7 this WRAPPED cthtml's value parser - a practical subset,
// correctly parsed, and a deliberate staging decision so the architecture was
// not blocked on a tokenizer. Stage 8 replaced it with :tokenizer and
// :treebuilder, which follow the WHATWG algorithms.
//
// The reason that mattered is not conformance for its own sake. Real pages are
// malformed - unclosed <p> and <li>, stray </div>, unquoted attributes,
// `<b><i></b></i>` - and the spec is the written-down record of what every
// browser does with each of them. A subset parser does something
// reasonable-looking instead, and "reasonable-looking" is not what the page was
// written against.
//
// Everything above this file speaks to `document::builder`, which is why the
// swap touched only these three files.

namespace ctbrowser {

struct parse_result {
    node_id root;
    bool wellformed = true; // the tree builder recovers from everything; kept for callers
    // Each <svg> element and the EXACT bytes it was written as. The tree has
    // the element but not its children: an SVG subtree is captured rather than
    // parsed, so that a real SVG parser downstream sees `viewBox` and not the
    // `viewbox` this tokenizer would have made of it. See dom/tokenizer.hpp.
    std::vector<std::pair<node_id, std::string>> svg_sources;
};

[[nodiscard]] inline parse_result parse_html(document & doc, std::string_view source) {
    html::tree_builder builder{doc, doc.atoms()};
    parse_result out;
    out.root = builder.parse(source);
    out.svg_sources = builder.foreign_sources();
    return out;
}

} // namespace ctbrowser
