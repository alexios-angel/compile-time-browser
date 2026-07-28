module;
#include <string>
#include <string_view>

export module ctbrowser.dom:html;

import ctbrowser.core;
import :document;
import :node;
import :treebuilder;

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

export namespace ctbrowser {

struct parse_result {
	node_id root;
	bool wellformed = true; // the tree builder recovers from everything; kept for callers
};

[[nodiscard]] inline parse_result parse_html(document & doc, std::string_view source) {
	html::tree_builder builder{doc, doc.atoms()};
	parse_result out;
	out.root = builder.parse(source);
	return out;
}

} // namespace ctbrowser
