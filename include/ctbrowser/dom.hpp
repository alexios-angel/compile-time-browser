#ifndef CTBROWSER__DOM__HPP
#define CTBROWSER__DOM__HPP

#include <cthtml.hpp>
#include <ctcss.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#endif

// The RUNTIME document. The compile-time DOM type instantiates into
// this mutable tree at startup; scripts mutate it (text, classes,
// inline styles, canvas pixels) and the style/layout passes read it.
// Nodes are stable for the document's lifetime - v0.1 scripts mutate
// but do not create or remove elements - so raw node pointers may be
// captured by script bindings.

namespace ctbrowser {

struct node {
	std::string tag;
	std::string id;
	std::string classes; // space-separated, ctcss::element_ref shape
	std::vector<std::pair<std::string, std::string>> attributes;
	std::string text; // the element's direct text, joined
	std::vector<std::unique_ptr<node>> children;
	node * parent = nullptr;

	// inline styles set from script: highest cascade priority
	std::map<std::string, std::string, std::less<>> inline_style;

	// <canvas> payload: 0xAARRGGBB pixels, drawn by scripts
	int canvas_w = 0;
	int canvas_h = 0;
	std::vector<uint32_t> pixels;

	// layout output (viewport coordinates), refreshed every frame
	int x = 0, y = 0, w = 0, h = 0;

	bool is_canvas() const { return tag == "canvas"; }

	std::string_view attribute(std::string_view name) const {
		for (const auto & [k, v] : attributes) {
			if (k == name) { return v; }
		}
		return {};
	}

	bool has_class(std::string_view c) const {
		return ctcss::detail::has_class(classes, c);
	}
	void add_class(std::string_view c) {
		if (has_class(c)) { return; }
		if (!classes.empty()) { classes += ' '; }
		classes += c;
	}
	void remove_class(std::string_view c) {
		std::string out;
		size_t i = 0;
		while (i < classes.size()) {
			while (i < classes.size() && classes[i] == ' ') { ++i; }
			size_t j = i;
			while (j < classes.size() && classes[j] != ' ') { ++j; }
			if (j > i && classes.substr(i, j - i) != c) {
				if (!out.empty()) { out += ' '; }
				out += classes.substr(i, j - i);
			}
			i = j;
		}
		classes = out;
	}
	void toggle_class(std::string_view c) {
		if (has_class(c)) {
			remove_class(c);
		} else {
			add_class(c);
		}
	}

	// the ctcss chain for this node, root-first
	std::vector<ctcss::element_ref> chain() const {
		std::vector<ctcss::element_ref> out;
		for (const node * n = this; n != nullptr; n = n->parent) {
			out.insert(out.begin(), ctcss::element_ref{n->tag, n->id, n->classes});
		}
		return out;
	}

	node * find_by_id(std::string_view want) {
		if (id == want) { return this; }
		for (const auto & c : children) {
			if (node * hit = c->find_by_id(want)) { return hit; }
		}
		return nullptr;
	}

	node * find_first(std::string_view want_tag) {
		if (tag == want_tag) { return this; }
		for (const auto & c : children) {
			if (node * hit = c->find_first(want_tag)) { return hit; }
		}
		return nullptr;
	}

	// deepest node whose layout rect contains (px, py); prefers children
	node * hit_test(int px, int py) {
		for (auto it = children.rbegin(); it != children.rend(); ++it) {
			if (node * hit = (*it)->hit_test(px, py)) { return hit; }
		}
		if (px >= x && px < x + w && py >= y && py < y + h) { return this; }
		return nullptr;
	}
};

namespace detail {

inline int parse_int_attr(std::string_view v, int fallback) {
	int out = 0;
	bool any = false;
	for (const char c : v) {
		if (c < '0' || c > '9') { break; }
		out = out * 10 + (c - '0');
		any = true;
	}
	return any ? out : fallback;
}

// instantiate the runtime tree from the compile-time DOM type
template <typename Elem> void instantiate_into(node & out, node * parent) {
	out.parent = parent;
	out.tag = Elem::name();
	cthtml::for_each_attribute(Elem{}, [&](auto name, auto value) {
		out.attributes.emplace_back(std::string{name.view()}, std::string{value.view()});
	});
	out.id = out.attribute("id");
	out.classes = out.attribute("class");
	out.text = Elem::text();
	if (out.is_canvas()) {
		out.canvas_w = parse_int_attr(out.attribute("width"), 300);
		out.canvas_h = parse_int_attr(out.attribute("height"), 150);
		out.pixels.assign(static_cast<size_t>(out.canvas_w) * static_cast<size_t>(out.canvas_h),
		                  0xFF000000u);
	}
	cthtml::for_each_child(Elem{}, [&](auto child) {
		using child_t = decltype(child);
		if constexpr (child_t::type == cthtml::kind::element) {
			out.children.push_back(std::make_unique<node>());
			instantiate_into<child_t>(*out.children.back(), &out);
		}
	});
}

} // namespace detail

// the live document: owns the tree, remembers the html root
struct document {
	std::unique_ptr<node> root; // the <html> element

	node * body() { return root ? root->find_first("body") : nullptr; }
	node * by_id(std::string_view id) { return root ? root->find_by_id(id) : nullptr; }
};

// build the runtime document from a compile-time DOM type
template <typename Doc> document instantiate() {
	document d;
	d.root = std::make_unique<node>();
	detail::instantiate_into<Doc>(*d.root, nullptr);
	return d;
}

} // namespace ctbrowser

#endif
