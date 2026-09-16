#pragma once
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/node.hpp>
#include <ctbrowser/dom/treebuilder.hpp>

// HTML into the DOM, through :tokenizer and :treebuilder, which follow the
// WHATWG algorithms. Real pages are malformed - unclosed <p> and <li>, stray
// </div>, `<b><i></b></i>` - and the spec is the written-down record of what
// every browser does with each of them.

namespace ctbrowser {

struct parse_result {
    node_id root;
    // Each <svg> element and the EXACT bytes it was written as, for the
    // rasteriser. The tree holds the parsed, namespaced subtree as well - see
    // dom/tokenizer.hpp.
    std::vector<std::pair<node_id, std::string>> svg_sources;
};

[[nodiscard]] inline parse_result parse_html(document & doc, std::string_view source) {
    html::tree_builder builder{doc, doc.atoms()};
    parse_result out;
    out.root = builder.parse(source);
    out.svg_sources = builder.foreign_sources();
    return out;
}

// THE FRAGMENT PARSING ALGORITHM (HTML 13.2.9) into a fresh scratch document:
// `context` is the tag name of the element the markup is being set on - which
// is what makes `<td>` inside a `<tr>` a cell, and `<b>` inside a `<title>` or
// `<script>` text - and the parsed nodes are the children of the returned
// node, to be moved under the real element. The context element itself is
// not in the scratch tree.
[[nodiscard]] inline node_id parse_html_fragment(document & doc, std::string_view source,
                                                 std::string_view context) {
    html::tree_builder builder{doc, doc.atoms()};
    return builder.parse_fragment(source, context);
}

// The fragment above with a <body> context: what every innerHTML on an
// ordinary element gets.
[[nodiscard]] inline node_id parse_html_body_fragment(document & doc, std::string_view source) {
    return parse_html_fragment(doc, source, "body");
}

} // namespace ctbrowser
