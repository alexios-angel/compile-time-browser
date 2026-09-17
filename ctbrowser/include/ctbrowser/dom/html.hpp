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

// `scripting` is the DOCUMENT's scripting flag - see tree_builder::set_scripting.
// A page parses with it on; a document no script will ever run in (DOMParser's,
// createHTMLDocument's) parses with it off, which is what makes `<noscript>`
// hold elements rather than text.
[[nodiscard]] inline parse_result parse_html(document & doc, std::string_view source,
                                             bool scripting = true) {
    html::tree_builder builder{doc, doc.atoms()};
    builder.set_scripting(scripting);
    parse_result out;
    out.root = builder.parse(source);
    out.svg_sources = builder.foreign_sources();
    return out;
}

// THE FRAGMENT PARSING ALGORITHM (HTML 13.2.9) into a fresh scratch document:
// `context` is the local name of the element the markup is being set on and
// `context_ns` its namespace - which is what makes `<td>` inside a `<tr>` a
// cell, `<b>` inside a `<title>` or `<script>` text, and `<circle>` inside an
// SVG `<g>` an SVG element - and the parsed nodes are the children of the
// returned node, to be moved under the real element. The context element
// itself is not in the scratch tree.
[[nodiscard]] inline node_id parse_html_fragment(document & doc, std::string_view source,
                                                 std::string_view context,
                                                 node_ns context_ns = node_ns::html) {
    html::tree_builder builder{doc, doc.atoms()};
    return builder.parse_fragment(source, context, context_ns);
}

// The fragment above with a <body> context: what every innerHTML on an
// ordinary element gets.
[[nodiscard]] inline node_id parse_html_body_fragment(document & doc, std::string_view source) {
    return parse_html_fragment(doc, source, "body");
}

} // namespace ctbrowser
