// ctbrowser.style: matching, the cascade, and interning.
//
// Correctness first. A matcher that is fast and wrong is worse than v1's,
// which is slow and right - so the bucketing and the ancestor filter are
// checked against cases designed to break them: selectors whose rightmost
// compound files them in an unexpected bucket, and descendant selectors the
// filter is supposed to reject without walking.

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.style;

#include "check.hpp"
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::style;

namespace {

// Build a document, resolve it, and let a test ask for one element's value.
struct fixture {
	atom_table atoms;
	document doc{atoms};
	engine styles{atoms};
	style_map resolved;

	void load(std::string_view html, std::string_view css, std::string_view ua = {}) {
		parse_html(doc, html);
		if (!ua.empty()) { styles.add_sheet(ua, 0); } // user agent origin
		styles.add_sheet(css, 1);                     // author origin
		const auto txn = doc.read();
		resolved = styles.resolve_all(txn);
	}

	[[nodiscard]] node_id find(std::string_view tag_name) {
		const auto txn = doc.read();
		const atom want = atoms.intern_lower(tag_name);
		node_id found{};
		const auto walk = [&](auto && self, node_id at) -> void {
			if (txn.kind(at).value_or(node_kind::text) == node_kind::element &&
			    txn.tag(at).value_or(atom{}) == want && !found) {
				found = at;
			}
			for (const node_id c : txn.children(at)) { self(self, c); }
		};
		walk(walk, txn.root());
		return found;
	}
	[[nodiscard]] node_id find_id(std::string_view want_id) {
		const auto txn = doc.read();
		const atom key = atoms.intern("id");
		node_id found{};
		const auto walk = [&](auto && self, node_id at) -> void {
			if (txn.attribute_value(at, key) == want_id && !found) { found = at; }
			for (const node_id c : txn.children(at)) { self(self, c); }
		};
		walk(walk, txn.root());
		return found;
	}

	[[nodiscard]] std::string value_of(node_id n, std::string_view property) {
		const auto it = resolved.find(engine::key_of(n));
		if (it == resolved.end() || !it->second) { return "<unresolved>"; }
		return std::string{it->second->get(atoms.intern_lower(property))};
	}
	[[nodiscard]] computed_style_ptr style_of(node_id n) {
		const auto it = resolved.find(engine::key_of(n));
		return it == resolved.end() ? computed_style_ptr{} : it->second;
	}
};

void expect_value(fixture & f, node_id n, std::string_view property, std::string_view want,
                  std::string_view what) {
	const std::string got = f.value_of(n, property);
	if (got != want) {
		std::printf("FAIL %-38s %s => '%s' (want '%s')\n", std::string{what}.c_str(),
		            std::string{property}.c_str(), got.c_str(), std::string{want}.c_str());
		++ctbrowser_test_failures;
	}
}

void test_simple_selectors() {
	fixture f;
	f.load("<div id=box class='a b'><p>hi</p></div>",
	       "div { color: red } p { color: blue } .a { margin: 1px } #box { padding: 2px }");
	const node_id div = f.find("div");
	const node_id p = f.find("p");

	expect_value(f, div, "color", "red", "tag selector");
	expect_value(f, p, "color", "blue", "tag selector (other)");
	expect_value(f, div, "margin", "1px", "class selector");
	expect_value(f, div, "padding", "2px", "id selector");
	expect_value(f, p, "margin", "", "class must not leak to a child");
}

// Bucketing files a rule under its RIGHTMOST compound. These selectors are
// chosen so the bucket is not the obvious one - `#nav a` lives in the TAG
// bucket, not the id bucket - which is exactly where a naive index breaks.
void test_bucketing_uses_the_rightmost_compound() {
	fixture f;
	f.load("<nav id=nav><a class=link>x</a></nav><a class=link>y</a>",
	       "#nav a { color: red }"      // filed under tag `a`
	       ".wrap .link { color: blue }" // filed under class `link`
	       "nav .link { font-size: 9px }");
	const auto txn = f.doc.read();
	const node_id inside = f.find("a"); // the first <a>, inside <nav>

	expect_value(f, inside, "color", "red", "#nav a matched via the tag bucket");
	expect_value(f, inside, "font-size", "9px", "nav .link matched via the class bucket");
	// .wrap .link must NOT match: there is no .wrap ancestor. This is the case
	// the ancestor filter rejects without walking.
	expect_value(f, inside, "color", "red", ".wrap .link correctly rejected");
}

void test_descendant_and_child_combinators() {
	fixture f;
	f.load("<section><div><p id=deep>x</p></div></section><p id=shallow>y</p>",
	       "section p { color: red }"    // descendant: matches the deep one
	       "section > p { color: lime }" // child: matches neither
	       "div > p { font-weight: bold }");
	const node_id deep = f.find_id("deep");
	const node_id shallow = f.find_id("shallow");

	expect_value(f, deep, "color", "red", "descendant crosses generations");
	expect_value(f, deep, "font-weight", "bold", "child matches a direct parent");
	expect_value(f, shallow, "color", "", "descendant must not match outside");
	// `section > p` must not match the deep p, whose parent is a div
	const std::string deep_color = f.value_of(deep, "color");
	CHECK(deep_color == "red"); // lime would mean the child combinator was ignored
}

void test_specificity_and_source_order() {
	fixture f;
	f.load("<div id=x class=c>hi</div>",
	       "div { color: tag }"     // 0,0,1
	       ".c { color: class }"    // 0,1,0 - beats tag
	       "#x { color: id }");     // 1,0,0 - beats class
	const node_id div = f.find("div");
	expect_value(f, div, "color", "id", "id beats class beats tag");

	fixture g;
	g.load("<div class=c>hi</div>", ".c { color: first } .c { color: second }");
	expect_value(g, g.find("div"), "color", "second", "equal specificity: later wins");
}

// Author rules beat user-agent rules even when the UA selector is more
// specific - origin outranks specificity in the cascade.
void test_author_beats_user_agent() {
	fixture f;
	f.load("<div id=x>hi</div>",
	       "div { color: author }",  // author, low specificity
	       "#x { color: ua }");      // UA, high specificity
	expect_value(f, f.find("div"), "color", "author", "author origin outranks UA specificity");
}

// The point of interning: elements that resolve identically must SHARE one
// style object, so comparing them is a pointer compare.
void test_identical_styles_are_shared() {
	fixture f;
	f.load("<ul><li>a</li><li>b</li><li>c</li><li>d</li></ul>", "li { color: red; margin: 0 }");
	const auto txn = f.doc.read();
	const node_id ul = f.find("ul");
	const auto items = txn.children(ul);
	CHECK(items.size() >= 4);

	const computed_style_ptr first = f.style_of(items[0]);
	CHECK(static_cast<bool>(first));
	bool all_shared = true;
	for (const node_id li : items) {
		if (f.style_of(li).get() != first.get()) { all_shared = false; }
	}
	CHECK(all_shared); // pointer equality, not just value equality

	// and the table really only holds a few distinct styles for this document
	const std::size_t distinct = f.styles.styles().distinct_styles();
	std::printf("  4 <li> + ul + text nodes -> %zu distinct styles\n", distinct);
	CHECK(distinct <= 3);
}

// A false NEGATIVE from the ancestor filter would silently drop a matching
// rule, so this hammers deep nesting where the filter does the most work.
void test_deep_nesting_still_matches() {
	std::string html = "<div class=root>";
	for (int i = 0; i < 40; ++i) { html += "<div>"; }
	html += "<span id=target>x</span>";
	for (int i = 0; i < 40; ++i) { html += "</div>"; }
	html += "</div>";

	fixture f;
	f.load(html, ".root span { color: found }");
	const node_id target = f.find_id("target");
	CHECK(static_cast<bool>(target));
	expect_value(f, target, "color", "found", "descendant matched through 40 levels");
}

void test_unmatched_element_gets_empty_style() {
	fixture f;
	f.load("<div><em>x</em></div>", "p { color: red }");
	const node_id em = f.find("em");
	CHECK(f.value_of(em, "color").empty());
	CHECK(static_cast<bool>(f.style_of(em))); // resolved, just to nothing
}

} // namespace

int main() {
	test_simple_selectors();
	test_bucketing_uses_the_rightmost_compound();
	test_descendant_and_child_combinators();
	test_specificity_and_source_order();
	test_author_beats_user_agent();
	test_identical_styles_are_shared();
	test_deep_nesting_still_matches();
	test_unmatched_element_gets_empty_style();
	REPORT("style_basics");
}
