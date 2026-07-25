module;
#include <string>
#include <string_view>

#include <cthtml.hpp>

export module ctbrowser.dom:html;

import ctbrowser.core;
import :document;
import :node;

// HTML into the v2 DOM.
//
// This WRAPS cthtml's existing value parser rather than replacing it, and
// that is a staging decision, not the end state. A spec-compliant HTML
// tokenizer and tree builder - with the error recovery real pages depend on,
// the insertion modes, the adoption agency algorithm - is its own multi-week
// project, and blocking the architecture on it would be the wrong order to
// build in. cthtml parses a practical subset correctly today; swapping it for
// a conformant parser later changes only this file, because everything above
// speaks to `document::builder` rather than to a parser.
//
// The translation itself is straightforward: cthtml hands back an immutable
// value tree of indices, and this walks it once, allocating v2 nodes through
// the builder. Element and attribute names intern on the way through, so by
// the time the document is live, every name comparison downstream is an
// integer compare.

export namespace ctbrowser {

struct parse_result {
	node_id root;
	bool wellformed = false; // cthtml's own verdict; a bad document still builds
};

namespace detail {

inline void build_subtree(document::builder & out, atom_table & atoms, cthtml::node source,
                          node_id parent) {
	if (source.is_text()) {
		const std::string text = source.text();
		if (text.empty()) { return; }
		out.append(parent, out.create_text(text));
		return;
	}
	if (!source.is_element()) { return; }

	// Tags and attribute names fold to lowercase on the way in; cthtml already
	// canonicalises tags, but interning through intern_lower keeps the
	// guarantee in one place rather than depending on the parser's contract.
	const node_id element = out.create_element(atoms.intern_lower(source.name()));
	for (const cthtml::dom_attribute & attr : source.attributes()) {
		out.set_attribute(element, atoms.intern_lower(attr.name), attr.value);
	}
	out.append(parent, element);
	for (cthtml::node child : source) { build_subtree(out, atoms, child, element); }
}

} // namespace detail

// Parse `html` into `doc`. The document must be empty and unobserved - this
// runs on the builder path, which is not safe against concurrent readers.
inline parse_result parse_html(document & doc, std::string_view html) {
	const cthtml::document source = cthtml::parse(html);
	auto out = doc.build();
	const node_id root = doc.root();
	detail::build_subtree(out, doc.atoms(), source.root(), root);
	return parse_result{root, source.ok()};
}

} // namespace ctbrowser
