//===- html.hpp - HTML parsing entry points ------------------- -*- C++ -*-===//
//
// Parses HTML into the DOM through dom/tokenizer.hpp and dom/treebuilder.hpp,
// which follow the WHATWG algorithms. Real pages are malformed (unclosed <p>
// and <li>, stray </div>, `<b><i></b></i>`), and the specification records what
// every browser does with each case.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/node.hpp>
#include <ctbrowser/dom/treebuilder.hpp>

namespace ctbrowser {

/// What parse_html produced.
struct parse_result {
    node_id root;
    /// Each <svg> element and the exact bytes it was written as, for the
    /// rasteriser. The tree also holds the parsed, namespaced subtree (see
    /// dom/tokenizer.hpp).
    std::vector<std::pair<node_id, std::string>> svg_sources;
};

/// Parses a whole HTML document into `doc`. `scripting` is the document's
/// scripting flag (see tree_builder::set_scripting): a page parses with it
/// on; a document no script will run in (DOMParser's, createHTMLDocument's)
/// parses with it off, which makes `<noscript>` hold elements rather than
/// text. The same flag also sets "allow declarative shadow roots"
/// (document.hpp): HTML enables it for documents created by navigation, not
/// for DOMParser's.
[[nodiscard]] inline parse_result parse_html(document & doc, std::string_view source,
                                             bool scripting = true) {
    html::tree_builder builder{doc, doc.atoms()};
    doc.set_scripting(scripting);
    doc.set_allow_declarative_shadow_roots(scripting);
    builder.set_scripting(scripting);
    parse_result out;
    out.root = builder.parse(source);
    out.svg_sources = builder.foreign_sources();
    return out;
}

/// The fragment parsing algorithm (HTML 13.2.9), into a scratch document
/// `doc`. `context` is the local name of the element the markup is being set
/// on and `context_ns` its namespace, which is what makes `<td>` inside a
/// `<tr>` a cell, `<b>` inside a `<title>` or `<script>` text, and `<circle>`
/// inside an SVG `<g>` an SVG element. The parsed nodes are the children of
/// the returned node, to be moved under the real element; the context element
/// itself is not in the scratch tree.
///
/// `scripting` is the flag of the document the fragment is for, not the
/// scratch document's: a `<noscript>` in a DOMParser document's innerHTML
/// holds elements, in the page's it is raw text. `declarative_shadow_roots` is
/// 13.2.9's allowDeclarativeShadowRoots: true for `setHTMLUnsafe`, false for
/// `innerHTML`.
[[nodiscard]] inline node_id parse_html_fragment(document & doc, std::string_view source,
                                                 std::string_view context,
                                                 node_ns context_ns = node_ns::html,
                                                 bool scripting = true,
                                                 bool declarative_shadow_roots = false) {
    html::tree_builder builder{doc, doc.atoms()};
    doc.set_allow_declarative_shadow_roots(declarative_shadow_roots);
    builder.set_scripting(scripting);
    return builder.parse_fragment(source, context, context_ns);
}

/// parse_html_fragment with a <body> context, as innerHTML on an ordinary
/// element uses.
[[nodiscard]] inline node_id parse_html_body_fragment(document & doc, std::string_view source) {
    return parse_html_fragment(doc, source, "body");
}

} // namespace ctbrowser
