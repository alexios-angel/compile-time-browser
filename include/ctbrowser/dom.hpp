#ifndef CTBROWSER__DOM__HPP
#define CTBROWSER__DOM__HPP

#include <cstddef>

#include <cthtml.hpp>
#include <ctcss.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <charconv>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#endif

// The RUNTIME document. The page's HTML instantiates into this mutable
// tree at startup (instantiate_html - cthtml's value parser); scripts
// mutate it (text, classes, inline styles, canvas pixels) and the
// style/layout passes read it.
// Nodes are stable for the document's lifetime - v0.1 scripts mutate
// but do not create or remove elements - so raw node pointers may be
// captured by script bindings.
//
// The tree is built from std::string/std::vector/std::unique_ptr - all
// constexpr on this toolchain - so `node`, `document` and instantiate()
// are constexpr: a page can be parsed (cthtml value parser), built and
// queried entirely at compile time (see tests/dom.cpp). SDL, images and
// audio stay runtime; the DOM does not.

namespace ctbrowser {

// A tiny ordered property map (std::map is not constexpr). Insertion
// order is irrelevant to the cascade; last write wins on a key.
struct style_map {
	std::vector<std::pair<std::string, std::string>> items;

	constexpr bool has(std::string_view key) const noexcept {
		for (const auto & [k, v] : items) {
			if (k == key) { return true; }
		}
		return false;
	}
	constexpr std::string_view get(std::string_view key) const noexcept {
		for (const auto & [k, v] : items) {
			if (k == key) { return v; }
		}
		return {};
	}
	constexpr void set(std::string_view key, std::string_view value) {
		for (auto & [k, v] : items) {
			if (k == key) {
				v = std::string{value};
				return;
			}
		}
		items.emplace_back(std::string{key}, std::string{value});
	}
	constexpr bool empty() const noexcept { return items.empty(); }
};

struct node {
	std::string tag;
	std::string id;
	std::string classes; // space-separated, ctcss::element_ref shape
	std::vector<std::pair<std::string, std::string>> attributes;
	std::string text; // the element's direct text, joined
	std::vector<std::unique_ptr<node>> children;
	node * parent = nullptr;

	// inline styles set from script: highest cascade priority
	style_map inline_style;

	// <canvas> payload: 0xAARRGGBB pixels, drawn by scripts
	std::int32_t canvas_w = 0;
	std::int32_t canvas_h = 0;
	std::vector<uint32_t> pixels;

	// layout output (viewport coordinates), refreshed every frame
	std::int32_t x = 0, y = 0, w = 0, h = 0;

	// <select> widget state (runtime only): chosen <option> + dropdown open
	std::int32_t select_index = -1;    // -1 => not set yet, treated as the first option
	bool select_open = false; // the popup list is expanded

	constexpr bool is_canvas() const { return tag == "canvas"; }
	constexpr bool is_select() const { return tag == "select"; }

	// nth <option> child (only option children count); nullptr if out of range
	constexpr node * nth_option(std::int32_t idx) {
		std::int32_t k = 0;
		for (const auto & c : children) {
			if (c->tag == "option") {
				if (k == idx) { return c.get(); }
				++k;
			}
		}
		return nullptr;
	}
	constexpr std::int32_t option_count() const {
		std::int32_t k = 0;
		for (const auto & c : children) {
			if (c->tag == "option") { ++k; }
		}
		return k;
	}
	// effective selected index (clamped; -1/out-of-range => 0)
	constexpr std::int32_t selected_option() const {
		const std::int32_t n = option_count();
		if (n == 0) { return 0; }
		return (select_index >= 0 && select_index < n) ? select_index : 0;
	}

	constexpr std::string_view attribute(std::string_view name) const {
		for (const auto & [k, v] : attributes) {
			if (k == name) { return v; }
		}
		return {};
	}

	constexpr bool has_class(std::string_view c) const {
		return ctcss::detail::has_class(classes, c);
	}
	constexpr void add_class(std::string_view c) {
		if (has_class(c)) { return; }
		if (!classes.empty()) { classes += ' '; }
		classes += c;
	}
	constexpr void remove_class(std::string_view c) {
		std::string out;
		std::size_t i = 0;
		while (i < classes.size()) {
			while (i < classes.size() && classes[i] == ' ') { ++i; }
			std::size_t j = i;
			while (j < classes.size() && classes[j] != ' ') { ++j; }
			if (j > i && classes.substr(i, j - i) != c) {
				if (!out.empty()) { out += ' '; }
				out += classes.substr(i, j - i);
			}
			i = j;
		}
		classes = out;
	}
	constexpr void toggle_class(std::string_view c) {
		if (has_class(c)) {
			remove_class(c);
		} else {
			add_class(c);
		}
	}

	// the ctcss chain for this node, root-first
	constexpr std::vector<ctcss::element_ref> chain() const {
		std::vector<ctcss::element_ref> out;
		for (const node * n = this; n != nullptr; n = n->parent) {
			out.insert(out.begin(), ctcss::element_ref{n->tag, n->id, n->classes});
		}
		return out;
	}

	constexpr node * find_by_id(std::string_view want) {
		if (id == want) { return this; }
		for (const auto & c : children) {
			if (node * hit = c->find_by_id(want)) { return hit; }
		}
		return nullptr;
	}

	constexpr node * find_first(std::string_view want_tag) {
		if (tag == want_tag) { return this; }
		for (const auto & c : children) {
			if (node * hit = c->find_first(want_tag)) { return hit; }
		}
		return nullptr;
	}

	// --- querySelector: a practical subset of CSS selectors. A compound
	// like "div#id.cls" tests tag ('*'/empty = any) then every #id/.class
	// token; whitespace is the descendant combinator ("#panel .value").
	// No combinators beyond descendant, no pseudo/attribute selectors -
	// enough for real scripts' getElementById-style lookups.
	constexpr bool matches_compound(std::string_view sel) const {
		std::size_t t = 0;
		while (t < sel.size() && sel[t] != '#' && sel[t] != '.') { ++t; }
		if (t > 0) {
			const std::string_view type = sel.substr(0, t);
			if (type != "*" && type != tag) { return false; }
		}
		std::size_t i = t;
		while (i < sel.size()) {
			const char kind = sel[i++];
			std::size_t j = i;
			while (j < sel.size() && sel[j] != '#' && sel[j] != '.') { ++j; }
			const std::string_view name = sel.substr(i, j - i);
			if (kind == '#') {
				if (id != name) { return false; }
			} else if (kind == '.') {
				if (!has_class(name)) { return false; }
			}
			i = j;
		}
		return true;
	}
	// this matches parts[k], and (if more parts) some descendant chain matches the rest
	constexpr node * qs_from(const std::vector<std::string_view> & parts, std::size_t k) {
		if (!matches_compound(parts[k])) { return nullptr; }
		if (k + 1 == parts.size()) { return this; }
		for (const auto & c : children) {
			if (node * r = c->qs_find(parts, k + 1)) { return r; }
		}
		return nullptr;
	}
	// first node in this subtree (self first) that begins a match of parts[k..]
	constexpr node * qs_find(const std::vector<std::string_view> & parts, std::size_t k) {
		if (node * r = qs_from(parts, k)) { return r; }
		for (const auto & c : children) {
			if (node * r = c->qs_find(parts, k)) { return r; }
		}
		return nullptr;
	}
	constexpr node * query_selector(std::string_view sel) {
		std::vector<std::string_view> parts;
		std::size_t i = 0;
		while (i < sel.size()) {
			while (i < sel.size() && (sel[i] == ' ' || sel[i] == '\t' || sel[i] == '>')) { ++i; }
			std::size_t j = i;
			while (j < sel.size() && sel[j] != ' ' && sel[j] != '\t' && sel[j] != '>') { ++j; }
			if (j > i) { parts.push_back(sel.substr(i, j - i)); }
			i = j;
		}
		return parts.empty() ? nullptr : qs_find(parts, 0);
	}

	// deepest node whose layout rect contains (px, py); prefers children
	constexpr node * hit_test(std::int32_t px, std::int32_t py) {
		for (auto it = children.rbegin(); it != children.rend(); ++it) {
			if (node * hit = (*it)->hit_test(px, py)) { return hit; }
		}
		if (px >= x && px < x + w && py >= y && py < y + h) { return this; }
		return nullptr;
	}
};

namespace detail {

constexpr std::int32_t parse_int_attr(std::string_view v, std::int32_t fallback) {
	std::int32_t out = 0;
	const auto r = std::from_chars(v.data(), v.data() + v.size(), out);
	return r.ec == std::errc{} ? out : fallback;
}

constexpr void init_canvas(node & out) {
	if (out.is_canvas()) {
		out.canvas_w = parse_int_attr(out.attribute("width"), 300);
		out.canvas_h = parse_int_attr(out.attribute("height"), 150);
		out.pixels.assign(static_cast<std::size_t>(out.canvas_w) * static_cast<std::size_t>(out.canvas_h),
		                  0xFF000000u);
	}
}

// instantiate the runtime tree from a cthtml VALUE document
constexpr void instantiate_into(node & out, cthtml::node vn, node * parent) {
	out.parent = parent;
	out.tag = std::string{vn.name()};
	for (const cthtml::dom_attribute & a : vn.attributes()) {
		out.attributes.emplace_back(a.name, a.value);
	}
	out.id = std::string{out.attribute("id")};
	out.classes = std::string{out.attribute("class")};
	// Build the direct text in document order, turning <br> into a hard newline
	// (layout breaks lines on it) and collapsing source newlines to spaces (HTML
	// whitespace). Non-<br> element children recurse as before.
	std::string txt;
	for (cthtml::node child : vn) {
		if (child.is_text()) {
			const std::string t = child.text();
			for (const char ch : t) { txt.push_back(ch == '\n' ? ' ' : ch); }
		} else if (child.is_element()) {
			if (child.name() == std::string_view{"br"}) {
				txt.push_back('\n');
			} else {
				out.children.push_back(std::make_unique<node>());
				instantiate_into(*out.children.back(), child, &out);
			}
		}
	}
	out.text = std::move(txt);
	init_canvas(out);
}

} // namespace detail

// the live document: owns the tree, remembers the html root
struct document {
	std::unique_ptr<node> root; // the <html> element
	// document.createElement products not yet appendChild'd anywhere:
	// they live here so their node* stays valid either way
	std::vector<std::unique_ptr<node>> detached;

	constexpr node * body() { return root ? root->find_first("body") : nullptr; }
	constexpr node * by_id(std::string_view id) { return root ? root->find_by_id(id) : nullptr; }

	// the web's node factory: scripts MAY create elements (this
	// deliberately relaxes the original never-create rule - p5 and
	// friends boot via document.createElement("canvas")). Pointers stay
	// stable: children are unique_ptr, appends never move nodes.
	constexpr node * create_element(std::string_view tag) {
		auto n = std::make_unique<node>();
		n->tag = tag;
		if (n->is_canvas()) {
			n->canvas_w = 300; // spec defaults
			n->canvas_h = 150;
			n->pixels.assign(300 * 150, 0xFF000000u);
		}
		node * raw = n.get();
		detached.push_back(std::move(n));
		return raw;
	}

	// move a detached (or reparent an attached) node under parent
	constexpr node * append_child(node * parent, node * child) {
		if (parent == nullptr || child == nullptr) { return child; }
		std::unique_ptr<node> owned;
		for (auto it = detached.begin(); it != detached.end(); ++it) {
			if (it->get() == child) {
				owned = std::move(*it);
				detached.erase(it);
				break;
			}
		}
		if (!owned && child->parent != nullptr) {
			auto & sibs = child->parent->children;
			for (auto it = sibs.begin(); it != sibs.end(); ++it) {
				if (it->get() == child) {
					owned = std::move(*it);
					sibs.erase(it);
					break;
				}
			}
		}
		if (!owned) { return child; } // not ours to move (e.g. root)
		child->parent = parent;
		parent->children.push_back(std::move(owned));
		return child;
	}

	// detach from the tree (kept alive in `detached`: script handles
	// still hold node*)
	constexpr node * remove_child(node * parent, node * child) {
		if (parent == nullptr || child == nullptr) { return child; }
		auto & sibs = parent->children;
		for (auto it = sibs.begin(); it != sibs.end(); ++it) {
			if (it->get() == child) {
				child->parent = nullptr;
				detached.push_back(std::move(*it));
				sibs.erase(it);
				break;
			}
		}
		return child;
	}
};

// build the runtime document from a cthtml VALUE document.
constexpr document instantiate(const cthtml::document & vdoc) {
	document d;
	d.root = std::make_unique<node>();
	detail::instantiate_into(*d.root, vdoc.root(), nullptr);
	return d;
}

// parse a runtime HTML string and instantiate it in one step
constexpr document instantiate_html(std::string_view html) {
	return instantiate(cthtml::parse(html));
}

} // namespace ctbrowser

#endif
