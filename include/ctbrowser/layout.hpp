#ifndef CTBROWSER__LAYOUT__HPP
#define CTBROWSER__LAYOUT__HPP

#include "dom.hpp"
#include <ctjs/cfunction.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#endif

// Style resolution and block layout. The style resolver is a
// constexpr type-erased callable (ctjs::cfunction, not std::function)
// so the engine is not templated on the sheet type - the app glue
// captures `ctcss::query(Page::sheet_type{}, ...)` once - AND the whole
// layout pass folds at compile time (ctcss::query is constexpr; see the
// static_assert in tests/dom.cpp). Inline styles a script set
// (element.setStyle) win over the sheet, like the style attribute would.
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
    ctjs::cfunction<std::string_view(const ctcss::element_ref *, size_t, std::string_view)>;

// measure a text run's width in pixels at a font size; when absent the
// layout assumes the embedded font's square glyphs (width == font_px).
// The SDL shell installs a TTF-backed measure when a real font loads.
using text_measure_fn = ctjs::cfunction<int(std::string_view, int)>;

struct computed_style {
	const node * n;
	const style_fn * resolve;
	std::vector<ctcss::element_ref> chain;

	constexpr std::string_view get(std::string_view prop) const {
		if (n->inline_style.has(prop)) { return n->inline_style.get(prop); }
		return (*resolve)(chain.data(), chain.size(), prop);
	}
	constexpr int px(std::string_view prop, int fallback) const {
		const ctcss::length l = ctcss::parse_length(get(prop));
		if (!l.ok || (l.u != ctcss::unit::px && l.u != ctcss::unit::none)) { return fallback; }
		return static_cast<int>(l.value);
	}
	constexpr ctcss::color color_of(std::string_view prop, ctcss::color fallback) const {
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

constexpr uint32_t pack_argb(ctcss::color c) {
	return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
	       (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

constexpr bool skipped_tag(std::string_view tag) {
	return tag == "head" || tag == "style" || tag == "script" || tag == "title";
}

struct layout_pass {
	const style_fn * resolve;
	const text_measure_fn * measure;
	std::vector<paint_cmd> * out;
	int vw = 0;  // viewport width  (for position:fixed/absolute + text-align)
	int vh = 0;  // viewport height (for top/bottom + vertical placement)

	static constexpr int UNSET = -1000000;

	constexpr int text_width(std::string_view t, int font_px) const {
		if (measure != nullptr && *measure) { return (*measure)(t, font_px); }
		return static_cast<int>(t.size()) * font_px;
	}

	// text-align inherits: walk up until a node sets it ("" = default/left)
	constexpr std::string_view text_align(node & n) const {
		for (node * p = &n; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const std::string_view v = pcs.get("text-align");
			if (!v.empty()) { return v; }
		}
		return {};
	}

	// color inherits (CSS): walk up until a node sets `color`; default black
	constexpr ctcss::color text_color(node & n) const {
		for (node * p = &n; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const ctcss::color c = ctcss::parse_color(pcs.get("color"));
			if (c.ok) { return c; }
		}
		return {true, 0, 0, 0, 255};
	}

	// shift every paint emitted since `start`, plus the node rects of the
	// subtree, by (dx, dy) - used to place an out-of-flow (positioned) box
	constexpr void translate(size_t start, node & n, int dx, int dy) {
		for (size_t i = start; i < out->size(); ++i) { (*out)[i].x += dx; (*out)[i].y += dy; }
		translate_rects(n, dx, dy);
	}
	constexpr void translate_rects(node & n, int dx, int dy) {
		n.x += dx;
		n.y += dy;
		for (const auto & c : n.children) { translate_rects(*c, dx, dy); }
	}

	// lay out `n` with its content starting at (x, y), `width` available
	// for the border box; returns the border-box height CONTRIBUTED TO FLOW
	// (0 for position:fixed/absolute, which are lifted out and positioned
	// against the viewport).
	constexpr int place(node & n, int x, int y, int width) {
		if (skipped_tag(n.tag)) {
			n.x = n.y = n.w = n.h = 0;
			return 0;
		}
		computed_style cs{&n, resolve, n.chain()};
		if (cs.get("display") == std::string_view{"none"}) {
			n.x = n.y = n.w = n.h = 0;
			return 0;
		}
		const std::string_view pos = cs.get("position");
		if (pos == std::string_view{"fixed"} || pos == std::string_view{"absolute"}) {
			// containing block = the viewport (nearest-positioned-ancestor is
			// not tracked; enough to overlay UI on a full-window canvas)
			const int left = cs.px("left", UNSET), right = cs.px("right", UNSET);
			const int top = cs.px("top", UNSET), bottom = cs.px("bottom", UNSET);
			int pw = cs.px("width", -1);
			if (pw < 0) {
				pw = vw - (left != UNSET ? left : 0) - (right != UNSET ? right : 0);
			}
			if (pw < 0) { pw = vw; }
			const size_t start = out->size();
			const int h = block_body(n, 0, 0, pw); // lay out at the origin...
			const int fx = (left != UNSET) ? left : (right != UNSET ? vw - pw - right : 0);
			const int fy = (top != UNSET) ? top : (bottom != UNSET ? vh - h - bottom : 0);
			translate(start, n, fx, fy);         // ...then lift to (fx, fy)
			return 0;                            // out of normal flow
		}
		return block_body(n, x, y, width);
	}

	// the in-flow block layout: text, canvas payload, and stacked children
	constexpr int block_body(node & n, int x, int y, int width) {
		computed_style cs{&n, resolve, n.chain()};
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

		// direct text first, wrapped to the content width (greedy, by
		// measured width - square-glyph estimate without a font)
		std::string_view text = n.text;
		const ctcss::color fg = text_color(n); // CSS color inherits from ancestors
		const std::string_view align = text_align(n);
		if (!trimmed(text).empty()) {
			std::string_view rest = trimmed(text);
			while (!rest.empty()) {
				size_t take = rest.size();
				while (take > 1 && text_width(rest.substr(0, take), font_px) > content_w) {
					--take;
				}
				const int tw = text_width(rest.substr(0, take), font_px);
				int tx = n.x + padding;
				if (align == std::string_view{"center"}) { tx += (content_w - tw) / 2; }
				else if (align == std::string_view{"right"}) { tx += content_w - tw; }
				paint_cmd cmd;
				cmd.what = paint_cmd::kind::text;
				cmd.x = tx;
				cmd.y = cursor;
				cmd.w = tw;
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

		// backgrounds are emitted in a pre-pass by collect_backgrounds below
		return n.h + 2 * margin;
	}

	static constexpr std::string_view trimmed(std::string_view v) {
		constexpr std::string_view ws = " \t\n\r";
		const size_t begin = v.find_first_not_of(ws);
		if (begin == std::string_view::npos) { return {}; }
		return v.substr(begin, v.find_last_not_of(ws) - begin + 1);
	}

	constexpr int inherited_font(node & n, const computed_style &) {
		for (node * p = n.parent; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const int v = pcs.px("font-size", -1);
			if (v > 0) { return v; }
		}
		return 16;
	}
};

// backgrounds, painted back-to-front before content
constexpr void collect_backgrounds(node & n, const style_fn & resolve,
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

// lay the document out for a viewport and produce the paint list.
// viewport_h (when > 0) anchors position:fixed/absolute top/bottom.
constexpr std::vector<paint_cmd> layout(document & doc, int viewport_w,
                                     const style_fn & resolve,
                                     const text_measure_fn & measure = {},
                                     int viewport_h = 0) {
	std::vector<paint_cmd> content;
	detail::layout_pass pass{&resolve, &measure, &content, viewport_w, viewport_h};
	if (doc.root) { (void)pass.place(*doc.root, 0, 0, viewport_w); }
	std::vector<paint_cmd> out;
	if (doc.root) { detail::collect_backgrounds(*doc.root, resolve, out); }
	out.insert(out.end(), content.begin(), content.end());
	return out;
}

} // namespace ctbrowser

#endif
