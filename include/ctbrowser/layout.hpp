#ifndef CTBROWSER__LAYOUT__HPP
#define CTBROWSER__LAYOUT__HPP

#include "dom.hpp"
#ifndef CTBROWSER_IN_A_MODULE
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#endif

// Style resolution and block layout. The style resolver is a
// std::function so the engine is not templated on the sheet type -
// the app glue captures `ctcss::query(Page::sheet_type{}, ...)` once.
// Inline styles a script set (element.setStyle) win over the sheet,
// like the style attribute would.
//
// Layout is CSS-flavored block stacking, enough for real UI and for
// hosting <canvas>: every element is a block box; width/height/
// margin/padding/font-size take px lengths; background(-color) and
// color take ctcss colors; display:none prunes. Text renders in the
// embedded 8x8 font scaled to font-size and wraps at the content
// width. The output is a paint list (back-to-front) plus per-node
// rects for hit testing.

namespace ctbrowser {

// resolve one property for one node chain ("" = unset)
using style_fn =
    std::function<std::string_view(const ctcss::element_ref *, size_t, std::string_view)>;

struct computed_style {
	const node * n;
	const style_fn * resolve;
	std::vector<ctcss::element_ref> chain;

	std::string_view get(std::string_view prop) const {
		if (const auto it = n->inline_style.find(prop); it != n->inline_style.end()) {
			return it->second;
		}
		return (*resolve)(chain.data(), chain.size(), prop);
	}
	int px(std::string_view prop, int fallback) const {
		const ctcss::length l = ctcss::parse_length(get(prop));
		if (!l.ok || (l.u != ctcss::unit::px && l.u != ctcss::unit::none)) { return fallback; }
		return static_cast<int>(l.value);
	}
	ctcss::color color_of(std::string_view prop, ctcss::color fallback) const {
		const ctcss::color c = ctcss::parse_color(get(prop));
		return c.ok ? c : fallback;
	}
};

struct paint_cmd {
	enum class kind { box, text, canvas };
	kind what = kind::box;
	int x = 0, y = 0, w = 0, h = 0;
	uint32_t argb = 0;      // box fill / text color
	std::string text;       // kind::text
	int font_px = 16;       // kind::text
	node * canvas_node = nullptr; // kind::canvas
};

namespace detail {

inline uint32_t pack_argb(ctcss::color c) {
	return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
	       (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

inline bool skipped_tag(std::string_view tag) {
	return tag == "head" || tag == "style" || tag == "script" || tag == "title";
}

struct layout_pass {
	const style_fn * resolve;
	std::vector<paint_cmd> * out;

	// lay out `n` with its content starting at (x, y), `width` available
	// for the border box; returns the border-box height
	int place(node & n, int x, int y, int width) {
		if (skipped_tag(n.tag)) {
			n.x = n.y = n.w = n.h = 0;
			return 0;
		}
		computed_style cs{&n, resolve, n.chain()};
		if (cs.get("display") == std::string_view{"none"}) {
			n.x = n.y = n.w = n.h = 0;
			return 0;
		}

		const int margin = cs.px("margin", 0);
		const int padding = cs.px("padding", 0);
		const int font_px = cs.px("font-size", inherited_font(n, cs));

		int box_w = cs.px("width", -1);
		if (box_w < 0) { box_w = width - 2 * margin; }
		if (n.is_canvas()) { box_w = n.canvas_w; }
		const int content_w = box_w - 2 * padding;

		n.x = x + margin;
		n.y = y + margin;
		n.w = box_w;

		int cursor = n.y + padding;

		// direct text first, wrapped to the content width
		std::string_view text = n.text;
		const ctcss::color fg = cs.color_of("color", {true, 0, 0, 0, 255});
		if (!trimmed(text).empty()) {
			// glyphs are square: an 8x8 bitmap scaled to font_px x font_px
			const int chars_per_line = content_w / font_px > 0 ? content_w / font_px : 1;
			std::string line;
			std::string_view rest = trimmed(text);
			while (!rest.empty()) {
				const size_t take = rest.size() > static_cast<size_t>(chars_per_line)
				                        ? static_cast<size_t>(chars_per_line)
				                        : rest.size();
				paint_cmd cmd;
				cmd.what = paint_cmd::kind::text;
				cmd.x = n.x + padding;
				cmd.y = cursor;
				cmd.w = static_cast<int>(take) * font_px;
				cmd.h = font_px;
				cmd.argb = pack_argb(fg);
				cmd.text = std::string{rest.substr(0, take)};
				cmd.font_px = font_px;
				out->push_back(cmd);
				cursor += font_px + font_px / 4;
				rest.remove_prefix(take);
			}
		}

		// canvas payload
		if (n.is_canvas()) {
			paint_cmd cmd;
			cmd.what = paint_cmd::kind::canvas;
			cmd.x = n.x + padding;
			cmd.y = cursor;
			cmd.w = n.canvas_w;
			cmd.h = n.canvas_h;
			cmd.canvas_node = &n;
			out->push_back(cmd);
			cursor += n.canvas_h;
		}

		// children stack vertically
		for (const auto & c : n.children) {
			cursor += place(*c, n.x + padding, cursor, content_w);
		}

		int box_h = cs.px("height", -1);
		if (box_h < 0) { box_h = (cursor - n.y) + padding; }
		n.h = box_h;

		// background paints BELOW text/children: insert before this
		// node's own commands would be wrong, so backgrounds are emitted
		// in a pre-pass by paint() below
		return n.h + 2 * margin;
	}

	static std::string_view trimmed(std::string_view v) {
		while (!v.empty() && (v.front() == ' ' || v.front() == '\n' || v.front() == '\t' ||
		                      v.front() == '\r')) {
			v.remove_prefix(1);
		}
		while (!v.empty() &&
		       (v.back() == ' ' || v.back() == '\n' || v.back() == '\t' || v.back() == '\r')) {
			v.remove_suffix(1);
		}
		return v;
	}

	int inherited_font(node & n, const computed_style &) {
		for (node * p = n.parent; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const int v = pcs.px("font-size", -1);
			if (v > 0) { return v; }
		}
		return 16;
	}
};

// backgrounds, painted back-to-front before content
inline void collect_backgrounds(node & n, const style_fn & resolve,
                                std::vector<paint_cmd> & out) {
	if (detail::skipped_tag(n.tag) || n.w == 0 || n.h == 0) {
		for (const auto & c : n.children) { collect_backgrounds(*c, resolve, out); }
		return;
	}
	computed_style cs{&n, &resolve, n.chain()};
	std::string_view bg = cs.get("background-color");
	if (bg.empty()) { bg = cs.get("background"); }
	const ctcss::color c = ctcss::parse_color(bg);
	if (c.ok && c.a != 0) {
		paint_cmd cmd;
		cmd.what = paint_cmd::kind::box;
		cmd.x = n.x;
		cmd.y = n.y;
		cmd.w = n.w;
		cmd.h = n.h;
		cmd.argb = pack_argb(c);
		out.push_back(cmd);
	}
	for (const auto & kid : n.children) { collect_backgrounds(*kid, resolve, out); }
}

} // namespace detail

// lay the document out for a viewport and produce the paint list
inline std::vector<paint_cmd> layout(document & doc, int viewport_w,
                                     const style_fn & resolve) {
	std::vector<paint_cmd> content;
	detail::layout_pass pass{&resolve, &content};
	if (doc.root) { (void)pass.place(*doc.root, 0, 0, viewport_w); }
	std::vector<paint_cmd> out;
	if (doc.root) { detail::collect_backgrounds(*doc.root, resolve, out); }
	out.insert(out.end(), content.begin(), content.end());
	return out;
}

} // namespace ctbrowser

#endif
