#ifndef CTBROWSER__LAYOUT__HPP
#define CTBROWSER__LAYOUT__HPP

#include <cstdint>

#include <cstddef>

#include "dom.hpp"
#include "ua.hpp"
#include "utf.hpp"
#include <ctc/cfunction.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#endif

// Style resolution and block layout. The style resolver is a
// constexpr type-erased callable (ctc::cfunction, not std::function)
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
    ctc::cfunction<std::string_view(const ctcss::element_ref *, std::size_t, std::string_view)>;

// measure a UTF-32 text run's width in pixels at a font size; when absent the
// layout assumes the embedded font's square glyphs (width == font_px per code
// point). The SDL shell installs a TTF-backed measure when a real font loads.
using text_measure_fn =
    ctc::cfunction<std::int32_t(std::u32string_view, std::int32_t, std::string_view, bool, bool)>;

struct computed_style {
	const node * n;
	const style_fn * resolve;
	std::vector<ctcss::element_ref> chain;

	constexpr std::string_view get(std::string_view prop) const {
		if (n->inline_style.has(prop)) { return n->inline_style.get(prop); }
		return (*resolve)(chain.data(), chain.size(), prop);
	}
	constexpr std::int32_t px(std::string_view prop, std::int32_t fallback) const {
		const ctcss::length l = ctcss::parse_length(get(prop));
		if (!l.ok || (l.u != ctcss::unit::px && l.u != ctcss::unit::none)) { return fallback; }
		return static_cast<std::int32_t>(l.value);
	}
	constexpr ctcss::color color_of(std::string_view prop, ctcss::color fallback) const {
		const ctcss::color c = ctcss::parse_color(get(prop));
		return c.ok ? c : fallback;
	}
};

struct paint_cmd {
	enum class kind { box, text, canvas };
	enum class strike : std::uint8_t { none, underline, line_through };
	kind what = kind::box;
	bool fixed = false; // position:fixed - exempt from page scrolling
	std::int32_t x = 0, y = 0, w = 0, h = 0;
	uint32_t argb = 0;      // box fill / text color
	std::u32string text;    // kind::text (UTF-32 code points)
	std::int32_t font_px = 16;       // kind::text
	std::string font_family;         // kind::text (resolved font-family list)
	bool bold = false;               // kind::text (font-weight >= bold)
	bool italic = false;             // kind::text (font-style italic/oblique)
	strike deco = strike::none;      // kind::text (text-decoration)
	node * canvas_node = nullptr; // kind::canvas
};

namespace detail {

// the resolved text style of one element (family/weight/style/deco)
struct font_spec {
	std::string family;
	bool bold = false;
	bool italic = false;
	paint_cmd::strike deco = paint_cmd::strike::none;
};

constexpr uint32_t pack_argb(ctcss::color c) {
	return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
	       (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

constexpr bool skipped_tag(std::string_view tag) {
	return tag == "head" || tag == "style" || tag == "script" || tag == "title";
}

// a containing block: the rect that position:absolute/fixed children and
// percentage lengths resolve against
struct box {
	std::int32_t x = 0, y = 0, w = 0, h = 0;
};

struct layout_pass {
	const style_fn * resolve;
	const text_measure_fn * measure;
	std::vector<paint_cmd> * out;
	std::int32_t vw = 0;  // viewport width  (for position:fixed/absolute + vw/text-align)
	std::int32_t vh = 0;  // viewport height (for top/bottom + vh + vertical placement)
	std::vector<paint_cmd> * overlays = nullptr; // painted last, on top (open <select>)

	static constexpr std::int32_t UNSET = -1000000;

	constexpr std::int32_t text_width(std::u32string_view t, std::int32_t font_px,
	                                  const font_spec & fs = {}) const {
		if (measure != nullptr && *measure) { return (*measure)(t, font_px, fs.family, fs.bold, fs.italic); }
		return static_cast<std::int32_t>(t.size()) * font_px; // one square glyph per code point
	}

	// inherited text-style resolvers (the font_of/text_color pattern):
	// multiple fonts coexist in one document - every element resolves its
	// own family/weight/style, every text cmd carries them
	constexpr std::string font_family_of(node * n) const {
		for (node * p = n; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const std::string_view v = pcs.get("font-family");
			if (!v.empty()) { return std::string{v}; }
		}
		return {};
	}
	constexpr bool font_bold_of(node * n) const {
		for (node * p = n; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const std::string_view v = pcs.get("font-weight");
			if (v.empty()) { continue; }
			if (ctcss::detail::ascii_iequals(v, "bold") || ctcss::detail::ascii_iequals(v, "bolder")) { return true; }
			if (ctcss::detail::ascii_iequals(v, "normal") || ctcss::detail::ascii_iequals(v, "lighter")) { return false; }
			const ctcss::length l = ctcss::parse_length(v); // numeric weights
			return l.ok && l.value >= 600;
		}
		return false;
	}
	constexpr bool font_italic_of(node * n) const {
		for (node * p = n; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const std::string_view v = pcs.get("font-style");
			if (v.empty()) { continue; }
			return ctcss::detail::ascii_iequals(v, "italic") || ctcss::detail::ascii_iequals(v, "oblique");
		}
		return false;
	}
	constexpr paint_cmd::strike text_deco_of(node * n) const {
		// text-decoration is not truly inherited in CSS, but in a block
		// engine treating it as inherited matches how it propagates to
		// the descendants that render the text
		for (node * p = n; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const std::string_view v = pcs.get("text-decoration");
			if (v.empty()) { continue; }
			if (v.find("underline") != std::string_view::npos) { return paint_cmd::strike::underline; }
			if (v.find("line-through") != std::string_view::npos) { return paint_cmd::strike::line_through; }
			return paint_cmd::strike::none; // explicit none stops the walk
		}
		return paint_cmd::strike::none;
	}
	constexpr font_spec font_spec_of(node * n) const {
		return font_spec{font_family_of(n), font_bold_of(n), font_italic_of(n), text_deco_of(n)};
	}

	// stamp a text cmd with the spec and emit its decoration band
	constexpr void push_text(paint_cmd cmd, const font_spec & fs) {
		cmd.font_family = fs.family;
		cmd.bold = fs.bold;
		cmd.italic = fs.italic;
		cmd.deco = fs.deco;
		const std::int32_t dy = fs.deco == paint_cmd::strike::underline ? cmd.h + 1
		                        : fs.deco == paint_cmd::strike::line_through ? cmd.h / 2
		                                                                     : -1;
		const std::int32_t dx = cmd.x, dw = cmd.w, dcy = cmd.y + dy;
		const std::uint32_t argb = cmd.argb;
		out->push_back(std::move(cmd));
		if (dy >= 0 && dw > 0) {
			paint_cmd band;
			band.what = paint_cmd::kind::box;
			band.x = dx;
			band.y = dcy;
			band.w = dw;
			band.h = 1;
			band.argb = argb;
			out->push_back(band);
		}
	}

	// resolve a CSS length to px: px/unitless absolute; % of `basis`; vw/vh of the
	// viewport; em of `font_px`; rem of the 16px root. calc() is not handled.
	constexpr std::int32_t len_px(std::string_view s, std::int32_t basis, std::int32_t font_px, std::int32_t fallback) const {
		const ctcss::length l = ctcss::parse_length(s);
		if (!l.ok) { return fallback; }
		switch (l.u) {
		case ctcss::unit::px:
		case ctcss::unit::none: return static_cast<std::int32_t>(l.value);
		case ctcss::unit::pct: return static_cast<std::int32_t>(l.value / 100.0 * basis);
		case ctcss::unit::vw: return static_cast<std::int32_t>(l.value / 100.0 * vw);
		case ctcss::unit::vh: return static_cast<std::int32_t>(l.value / 100.0 * vh);
		case ctcss::unit::em: return static_cast<std::int32_t>(l.value * font_px);
		case ctcss::unit::rem: return static_cast<std::int32_t>(l.value * 16.0);
		}
		return fallback;
	}
	constexpr std::int32_t prop_px(const computed_style & cs, std::string_view prop, std::int32_t basis,
	                      std::int32_t font_px, std::int32_t fallback) const {
		return len_px(cs.get(prop), basis, font_px, fallback);
	}

	// the CSS box sides: the 1/2/3/4-value shorthand ("margin: 16px 0")
	// expanded per spec, then margin-left-style per-side overrides
	struct sides {
		std::int32_t top = 0, right = 0, bottom = 0, left = 0;
	};
	constexpr sides sides_of(const computed_style & cs, std::string_view base, std::int32_t basis,
	                         std::int32_t font_px) const {
		sides s;
		const std::string_view sh = cs.get(base);
		if (!sh.empty()) {
			std::int32_t v[4] = {0, 0, 0, 0};
			std::int32_t k = 0;
			std::size_t i = 0;
			while (i < sh.size() && k < 4) {
				while (i < sh.size() && ctcss::detail::is_css_blank(sh[i])) { ++i; }
				const std::size_t st = i;
				while (i < sh.size() && !ctcss::detail::is_css_blank(sh[i])) { ++i; }
				if (i > st) { v[k++] = len_px(sh.substr(st, i - st), basis, font_px, 0); }
			}
			if (k == 1) { s = {v[0], v[0], v[0], v[0]}; }
			else if (k == 2) { s = {v[0], v[1], v[0], v[1]}; }
			else if (k == 3) { s = {v[0], v[1], v[2], v[1]}; }
			else if (k == 4) { s = {v[0], v[1], v[2], v[3]}; }
		}
		const auto side = [&](std::string_view suffix, std::int32_t & slot) {
			const std::string prop = std::string{base} + std::string{suffix};
			const std::string_view v = cs.get(prop);
			if (!v.empty()) { slot = len_px(v, basis, font_px, slot); }
		};
		side("-top", s.top);
		side("-right", s.right);
		side("-bottom", s.bottom);
		side("-left", s.left);
		return s;
	}

	// computed font-size (px): em/% relative to the parent's font, vw/vh to the
	// viewport, rem to the root; inherits when unset (root default 16px)
	constexpr std::int32_t font_of(node * n) const {
		if (n == nullptr) { return 16; }
		computed_style cs{n, resolve, n->chain()};
		const ctcss::length l = ctcss::parse_length(cs.get("font-size"));
		if (!l.ok) { return font_of(n->parent); }
		switch (l.u) {
		case ctcss::unit::px:
		case ctcss::unit::none: return static_cast<std::int32_t>(l.value);
		case ctcss::unit::em: return static_cast<std::int32_t>(l.value * font_of(n->parent));
		case ctcss::unit::pct: return static_cast<std::int32_t>(l.value / 100.0 * font_of(n->parent));
		case ctcss::unit::rem: return static_cast<std::int32_t>(l.value * 16.0);
		case ctcss::unit::vw: return static_cast<std::int32_t>(l.value / 100.0 * vw);
		case ctcss::unit::vh: return static_cast<std::int32_t>(l.value / 100.0 * vh);
		}
		return font_of(n->parent);
	}

	// transform: translate/translateX/translateY offsets (px); % of the element's
	// own (w,h). rotate/scale/translateZ and other functions are ignored.
	constexpr void translate_of(const computed_style & cs, std::int32_t w, std::int32_t h, std::int32_t font_px,
	                            std::int32_t & tx, std::int32_t & ty) const {
		const std::string_view t = cs.get("transform");
		tx = 0;
		ty = 0;
		std::size_t i = 0;
		while (i < t.size()) {
			const std::size_t p = t.find("translate", i);
			if (p == std::string_view::npos) { break; }
			std::size_t j = p + 9; // past "translate"
			std::int32_t axis = 2;     // 0 = X, 1 = Y, 2 = both
			if (j < t.size() && (t[j] == 'X' || t[j] == 'x')) { axis = 0; ++j; }
			else if (j < t.size() && (t[j] == 'Y' || t[j] == 'y')) { axis = 1; ++j; }
			else if (j < t.size() && (t[j] == 'Z' || t[j] == 'z')) { i = j + 1; continue; }
			if (j >= t.size() || t[j] != '(') { i = j; continue; }
			const std::size_t open = j + 1, close = t.find(')', open);
			if (close == std::string_view::npos) { break; }
			const std::string_view args = t.substr(open, close - open);
			const std::size_t comma = args.find(',');
			const std::string_view a0 = trimmed(comma == std::string_view::npos ? args : args.substr(0, comma));
			const std::string_view a1 = comma == std::string_view::npos ? std::string_view{} : trimmed(args.substr(comma + 1));
			if (axis == 0) {
				tx += len_px(a0, w, font_px, 0);
			} else if (axis == 1) {
				ty += len_px(a0, h, font_px, 0);
			} else {
				tx += len_px(a0, w, font_px, 0);
				if (!a1.empty()) { ty += len_px(a1, h, font_px, 0); }
			}
			i = close + 1;
		}
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
	constexpr void translate(std::size_t start, node & n, std::int32_t dx, std::int32_t dy) {
		for (std::size_t i = start; i < out->size(); ++i) { (*out)[i].x += dx; (*out)[i].y += dy; }
		translate_rects(n, dx, dy);
	}
	constexpr void translate_rects(node & n, std::int32_t dx, std::int32_t dy) {
		n.x += dx;
		n.y += dy;
		for (const auto & c : n.children) { translate_rects(*c, dx, dy); }
	}
	// a hidden subtree must lose its WHOLE rect tree: layout rects persist
	// between frames, and hit_test walks children first - a stale child
	// rect would keep a display:none/closed-details subtree clickable
	static constexpr void zero_rects(node & n) {
		n.x = n.y = n.w = n.h = 0;
		for (const auto & c : n.children) { zero_rects(*c); }
	}

	// lay out `n` with its content starting at (x, y), `width` available for the
	// border box, `cb` the containing block (nearest positioned ancestor, or the
	// viewport). Returns the border-box height CONTRIBUTED TO FLOW - 0 for
	// position:fixed/absolute, which are lifted out and positioned against `cb`
	// (fixed) / the viewport (fixed), then offset by any transform:translate.
	constexpr std::int32_t place(node & n, std::int32_t x, std::int32_t y, std::int32_t width, const box & cb) {
		if (skipped_tag(n.tag)) {
			zero_rects(n);
			return 0;
		}
		computed_style cs{&n, resolve, n.chain()};
		if (cs.get("display") == std::string_view{"none"}) {
			zero_rects(n);
			return 0;
		}
		n.viewport_fixed = false; // re-derived below for position:fixed
		const std::int32_t font_px = font_of(&n);
		const std::string_view pos = cs.get("position");
		if (pos == std::string_view{"fixed"} || pos == std::string_view{"absolute"}) {
			const box vp{0, 0, vw, vh};
			const box & c = (pos == std::string_view{"fixed"}) ? vp : cb;
			const std::int32_t left = prop_px(cs, "left", c.w, font_px, UNSET);
			const std::int32_t right = prop_px(cs, "right", c.w, font_px, UNSET);
			const std::int32_t top = prop_px(cs, "top", c.h, font_px, UNSET);
			const std::int32_t bottom = prop_px(cs, "bottom", c.h, font_px, UNSET);
			std::int32_t pw = prop_px(cs, "width", c.w, font_px, -1);
			if (pw < 0) { pw = c.w - (left != UNSET ? left : 0) - (right != UNSET ? right : 0); }
			if (pw < 0) { pw = c.w; }
			const std::int32_t maxw = prop_px(cs, "max-width", c.w, font_px, -1);
			if (maxw >= 0 && pw > maxw) { pw = maxw; }
			const std::int32_t ph = prop_px(cs, "height", c.h, font_px, -1); // definite? else content
			const std::size_t start = out->size();
			const std::size_t start_ov = overlays != nullptr ? overlays->size() : 0;
			// children resolve against THIS box, laid out at the origin then lifted
			const box child_cb{0, 0, pw, ph >= 0 ? ph : c.h};
			const std::int32_t laid = block_body(n, 0, 0, pw, child_cb);
			const std::int32_t h = ph >= 0 ? ph : laid;
			std::int32_t fx = c.x + (left != UNSET ? left : (right != UNSET ? c.w - pw - right : 0));
			std::int32_t fy = c.y + (top != UNSET ? top : (bottom != UNSET ? c.h - h - bottom : 0));
			std::int32_t tx = 0, ty = 0;
			translate_of(cs, pw, h, font_px, tx, ty);
			translate(start, n, fx + tx, fy + ty);
			// position:fixed is viewport-anchored: exempt the subtree (rects
			// via the node flag, paints via the cmd flag) from page scrolling
			n.viewport_fixed = pos == std::string_view{"fixed"};
			if (n.viewport_fixed) {
				for (std::size_t i = start; i < out->size(); ++i) { (*out)[i].fixed = true; }
			}
			// overlays (open <select> popups) emitted by this subtree ride along
			if (overlays != nullptr) {
				for (std::size_t i = start_ov; i < overlays->size(); ++i) {
					(*overlays)[i].x += fx + tx;
					(*overlays)[i].y += fy + ty;
				}
			}
			return 0; // out of normal flow
		}
		return block_body(n, x, y, width, cb);
	}

	// the in-flow block layout: text, canvas payload, and stacked children. `cb`
	// is passed through to descendants (a static box does not establish one).
	constexpr std::int32_t block_body(node & n, std::int32_t x, std::int32_t y, std::int32_t width, const box & cb) {
		computed_style cs{&n, resolve, n.chain()};
		const std::int32_t font_px = font_of(&n);
		const sides m = sides_of(cs, "margin", width, font_px);
		const sides p = sides_of(cs, "padding", width, font_px);
		const std::int32_t padding = p.left; // widget emitters use the inline inset

		std::int32_t box_w = prop_px(cs, "width", width, font_px, -1);
		if (box_w < 0) { box_w = width - m.left - m.right; }
		if (n.is_canvas()) { box_w = n.canvas_w; }
		// buttons and selects shrink to their content (Firefox renders
		// them inline-block) unless the page sets an explicit width
		if (prop_px(cs, "width", width, font_px, -1) < 0) {
			if (n.tag == "button") {
				const font_spec bfs = font_spec_of(&n);
				const std::int32_t tw = text_width(utf8_to_utf32(trimmed(n.text)), font_px, bfs);
				box_w = tw + p.left + p.right + 2;
			} else if (n.is_select()) {
				const font_spec bfs = font_spec_of(&n);
				node * sel = n.nth_option(n.selected_option());
				std::int32_t widest = 0;
				for (const auto & c : n.children) { // size to the widest option
					if (c->tag != "option") { continue; }
					const std::int32_t w2 = text_width(utf8_to_utf32(trimmed(c->text)), font_px, bfs);
					if (w2 > widest) { widest = w2; }
				}
				(void)sel;
				box_w = widest + font_px + p.left + p.right + 4; // + the arrow
			}
		}
		const std::int32_t content_w = box_w - p.left - p.right;

		n.x = x + m.left;
		n.y = y + m.top;
		n.w = box_w;

		std::int32_t cursor = n.y + p.top;

		// <select> renders as a native widget: the selected option collapsed with
		// a down-arrow (plus a popup list on top when open), not stacked options
		if (n.is_select()) {
			emit_select(n, font_px, padding, cursor, content_w);
			emit_frame(n, detail_frame_argb(n));
			return n.h + m.top + m.bottom;
		}
		// form widgets render native chrome (Firefox-style):
		if (n.is_checkbox() || n.is_radio()) {
			emit_toggle(n, font_px, padding, n.is_radio());
			return n.h + m.top + m.bottom;
		}
		if (n.is_input()) {
			if (ctcss::detail::ascii_iequals(n.input_type(), "hidden")) {
				zero_rects(n);
				return 0;
			}
			emit_input(n, font_px, padding, cursor);
			return n.h + m.top + m.bottom;
		}
		if (n.is_textarea()) {
			emit_textarea(n, font_px, padding, cursor);
			return n.h + m.top + m.bottom;
		}
		if (n.tag == "table") {
			emit_table(n, padding, cursor, content_w, cb);
			return n.h + m.top + m.bottom;
		}
		// list markers: the UA ul/ol padding-left leaves a 40px gutter
		if (n.tag == "li" && n.parent != nullptr) {
			const ctcss::color fg = text_color(n);
			if (n.parent->tag == "ul") {
				const std::int32_t d = font_px / 3 > 2 ? font_px / 3 : 3; // the disc
				paint_cmd b;
				b.what = paint_cmd::kind::box;
				b.x = n.x - d * 3;
				b.y = cursor + font_px / 2 - d / 2;
				b.w = d;
				b.h = d;
				b.argb = pack_argb(fg);
				out->push_back(b);
			} else if (n.parent->tag == "ol") {
				std::int32_t idx = 1;
				for (const auto & sib : n.parent->children) {
					if (sib.get() == &n) { break; }
					if (sib->tag == "li") { ++idx; }
				}
				std::u32string num;
				for (const char ch : std::to_string(idx)) { num.push_back(static_cast<char32_t>(ch)); }
				num.push_back(U'.');
				const font_spec fs2 = font_spec_of(&n);
				const std::int32_t nw = text_width(num, font_px, fs2);
				paint_cmd c;
				c.what = paint_cmd::kind::text;
				c.x = n.x - nw - font_px / 2;
				c.y = cursor;
				c.w = nw;
				c.h = font_px;
				c.argb = pack_argb(fg);
				c.text = std::move(num);
				c.font_px = font_px;
				push_text(std::move(c), fs2);
			}
		}

		// direct text, decoded to UTF-32 code points once, then wrapped to the
		// content width (measured width, or one square glyph per code point)
		const std::u32string text = utf8_to_utf32(n.text);
		const ctcss::color fg = text_color(n); // CSS color inherits from ancestors
		const std::string_view align = text_align(n);
		const font_spec fs = font_spec_of(&n); // family/weight/style/decoration
		if (!trimmed(std::u32string_view{text}).empty()) {
			// hard-break on U+000A (from <br>) into lines, then greedily wrap each
			std::u32string_view remain = text;
			bool more = true;
			const bool preserve = n.tag == "pre"; // pre keeps leading spaces
			while (more) {
				const std::size_t nl = remain.find(U'\n');
				const std::u32string_view raw_line =
				    nl == std::u32string_view::npos ? remain : remain.substr(0, nl);
				const std::u32string_view line = preserve ? raw_line : trimmed(raw_line);
				if (line.empty()) {
					cursor += font_px + font_px / 4; // blank row (e.g. consecutive <br>)
				} else {
					std::u32string_view rest = line;
					while (!rest.empty()) {
						std::size_t take = rest.size();
						while (take > 1 && text_width(rest.substr(0, take), font_px, fs) > content_w) {
							--take;
						}
						// break at a WORD boundary when splitting (a single
						// overlong word still breaks mid-word, like a browser)
						if (take < rest.size() && !preserve) {
							const std::size_t brk = rest.substr(0, take + 1).rfind(U' ');
							if (brk != std::u32string_view::npos && brk > 0) { take = brk; }
						}
						const std::int32_t tw = text_width(rest.substr(0, take), font_px, fs);
						std::int32_t tx = n.x + p.left;
						if (align == std::string_view{"center"}) { tx += (content_w - tw) / 2; }
						else if (align == std::string_view{"right"}) { tx += content_w - tw; }
						paint_cmd cmd;
						cmd.what = paint_cmd::kind::text;
						cmd.x = tx;
						cmd.y = cursor;
						cmd.w = tw;
						cmd.h = font_px;
						cmd.argb = pack_argb(fg);
						cmd.text = u32str(rest.substr(0, take));
						cmd.font_px = font_px;
						push_text(std::move(cmd), fs);
						cursor += font_px + font_px / 4;
						rest.remove_prefix(take);
						while (!preserve && !rest.empty() && rest.front() == U' ') {
							rest.remove_prefix(1); // eat the break space(s)
						}
					}
				}
				if (nl == std::u32string_view::npos) { more = false; }
				else { remain.remove_prefix(nl + 1); }
			}
		}

		// canvas payload
		if (n.is_canvas()) {
			paint_cmd cmd;
			cmd.what = paint_cmd::kind::canvas;
			cmd.x = n.x + p.left;
			cmd.y = cursor;
			cmd.w = n.canvas_w;
			cmd.h = n.canvas_h;
			cmd.canvas_node = &n;
			out->push_back(cmd);
			cursor += n.canvas_h;
		}

		// children stack vertically; a static box passes its own containing block
		// straight through (only positioned boxes establish a new one, in place()).
		// A closed <details> shows only its <summary>.
		for (const auto & c : n.children) {
			if (n.is_details() && !n.open && !c->is_summary()) {
				zero_rects(*c);
				continue;
			}
			cursor += place(*c, n.x + p.left, cursor, content_w, cb);
		}

		std::int32_t box_h = prop_px(cs, "height", cb.h, font_px, -1);
		if (box_h < 0) { box_h = (cursor - n.y) + p.bottom; }
		n.h = box_h;

		// buttons carry Firefox's 1px widget border (layout has no border
		// property - widget frames belong to the widgets)
		if (n.tag == "button") { emit_frame(n, detail_frame_argb(n)); }

		// backgrounds are emitted in a pre-pass by collect_backgrounds below
		return n.h + m.top + m.bottom;
	}

	static constexpr std::string_view trimmed(std::string_view v) {
		constexpr std::string_view ws = " \t\n\r";
		const std::size_t begin = v.find_first_not_of(ws);
		if (begin == std::string_view::npos) { return {}; }
		return v.substr(begin, v.find_last_not_of(ws) - begin + 1);
	}
	// materialize a u32string from a view (the string_view-taking basic_string ctor
	// is not usable in constexpr on this libstdc++, so build it by hand)
	static constexpr std::u32string u32str(std::u32string_view v) {
		std::u32string s;
		for (const char32_t c : v) { s.push_back(c); }
		return s;
	}
	// trim leading/trailing ASCII whitespace from a UTF-32 run
	static constexpr std::u32string_view trimmed(std::u32string_view v) {
		static constexpr char32_t ws[] = {U' ', U'\t', U'\n', U'\r', 0};
		const std::size_t begin = v.find_first_not_of(ws);
		if (begin == std::u32string_view::npos) { return {}; }
		return v.substr(begin, v.find_last_not_of(ws) - begin + 1);
	}

	// render a <select>: the collapsed control (selected option + down-arrow) into
	// `out`, and, when open, the popup option list into `overlays` (painted last,
	// on top). Sets each <option>'s hit rect (its overlay row, or empty when
	// closed) so the engine can route clicks. Sets n.h to the control height.
	// --- Firefox-style widget chrome -----------------------------------
	// a 1px frame around the node's border box (layout has no `border`
	// property; the widgets draw their own, like Firefox's form theme)
	static constexpr std::uint32_t detail_frame_argb(const node & n) {
		return n.is_disabled() ? 0xFFC8C8CEu : detail::ua_widget_frame;
	}
	constexpr void emit_frame(const node & n, std::uint32_t argb) {
		if (n.w <= 1 || n.h <= 1) { return; }
		const auto edge = [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
			paint_cmd b;
			b.what = paint_cmd::kind::box;
			b.x = x;
			b.y = y;
			b.w = w;
			b.h = h;
			b.argb = argb;
			out->push_back(b);
		};
		edge(n.x, n.y, n.w, 1);
		edge(n.x, n.y + n.h - 1, n.w, 1);
		edge(n.x, n.y, 1, n.h);
		edge(n.x + n.w - 1, n.y, 1, n.h);
	}
	// checkbox / radio: a ~14px (at 16px font) box or disc; Firefox's
	// modern theme - #8f8f9d frame, #0060df fill when checked, white mark
	constexpr void emit_toggle(node & n, std::int32_t font_px, std::int32_t padding, bool radio) {
		const std::int32_t side = font_px > 9 ? font_px * 7 / 8 : 8;
		n.w = side + 2 * padding;
		n.h = side + 2 * padding;
		const std::int32_t bx = n.x + padding, by = n.y + padding;
		const auto box_cmd = [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, std::uint32_t argb) {
			paint_cmd b;
			b.what = paint_cmd::kind::box;
			b.x = x;
			b.y = y;
			b.w = w;
			b.h = h;
			b.argb = argb;
			out->push_back(b);
		};
		if (!radio) {
			// field, frame, and - when checked - accent fill + white check mark
			box_cmd(bx, by, side, side, n.checked ? detail::ua_widget_accent : 0xFFFFFFFFu);
			box_cmd(bx, by, side, 1, detail_frame_argb(n));
			box_cmd(bx, by + side - 1, side, 1, detail_frame_argb(n));
			box_cmd(bx, by, 1, side, detail_frame_argb(n));
			box_cmd(bx + side - 1, by, 1, side, detail_frame_argb(n));
			if (n.checked) {
				// a stepped check: short down-stroke + longer up-stroke
				const std::int32_t u = side > 11 ? 2 : 1; // stroke thickness
				const std::int32_t cx0 = bx + side / 5, cy0 = by + side / 2;
				for (std::int32_t i = 0; i < side / 4; ++i) {
					box_cmd(cx0 + i, cy0 + i, u, u + 1, detail::ua_widget_mark);
				}
				const std::int32_t mx = bx + side / 5 + side / 4, my = by + side / 2 + side / 4;
				for (std::int32_t i = 0; i < side / 2; ++i) {
					box_cmd(mx + i, my - i, u, u + 1, detail::ua_widget_mark);
				}
			}
		} else {
			// disc drawn as rows (the emit_select triangle technique): width
			// steps out then back in - an octagon-ish circle at glyph sizes
			for (std::int32_t r = 0; r < side; ++r) {
				const std::int32_t d = r < side / 2 ? side / 2 - 1 - r : r - side / 2;
				std::int32_t inset = d > side / 4 ? d - side / 4 : 0;
				box_cmd(bx + inset, by + r, side - 2 * inset, 1,
				        r == 0 || r == side - 1 ? detail_frame_argb(n) : 0xFFFFFFFFu);
				if (inset > 0) {
					box_cmd(bx + inset, by + r, 1, 1, detail_frame_argb(n));
					box_cmd(bx + side - inset - 1, by + r, 1, 1, detail_frame_argb(n));
				} else {
					box_cmd(bx, by + r, 1, 1, detail_frame_argb(n));
					box_cmd(bx + side - 1, by + r, 1, 1, detail_frame_argb(n));
				}
			}
			if (n.checked) {
				// the accent dot, inset a third
				const std::int32_t inset = side / 3;
				box_cmd(bx + inset, by + inset, side - 2 * inset, side - 2 * inset,
				        detail::ua_widget_accent);
			}
		}
	}
	// a text-ish <input>: white field (background via the UA sheet's
	// pre-pass), 1px frame, and the value attribute's text (password
	// masks). No editing - the field is presentational.
	constexpr void emit_input(node & n, std::int32_t font_px, std::int32_t padding, std::int32_t top) {
		n.h = font_px + 2 * padding;
		emit_frame(n, detail_frame_argb(n));
		const font_spec fs = font_spec_of(&n);
		const std::int32_t content_w = n.w - 2 * padding;
		std::u32string shown = utf8_to_utf32(n.value); // the LIVE editable value
		if (ctcss::detail::ascii_iequals(n.input_type(), "password")) {
			shown.assign(shown.size(), U'*');
		}
		// caret position in code points (caret is a byte offset)
		std::size_t caret_cp = utf8_length(std::string_view{n.value}.substr(
		    0, static_cast<std::size_t>(n.caret < 0 ? 0 : n.caret)));
		if (caret_cp > shown.size()) { caret_cp = shown.size(); }
		// suffix-scroll: drop leading code points until the caret fits
		std::size_t start = 0;
		while (caret_cp > start &&
		       text_width(std::u32string_view{shown}.substr(start, caret_cp - start), font_px, fs) >
		           content_w) {
			++start;
		}
		std::u32string_view view{shown};
		view = view.substr(start);
		std::size_t take = view.size(); // clip the tail to the field
		while (take > 0 && text_width(view.substr(0, take), font_px, fs) > content_w) { --take; }
		const ctcss::color fg = text_color(n);
		if (take > 0) {
			paint_cmd c;
			c.what = paint_cmd::kind::text;
			c.x = n.x + padding;
			c.y = top;
			c.w = text_width(view.substr(0, take), font_px, fs);
			c.h = font_px;
			c.argb = pack_argb(fg);
			c.text = std::u32string{view.substr(0, take)};
			c.font_px = font_px;
			push_text(std::move(c), fs);
		}
		if (n.focused) { // the caret: a 1px bar at the caret's measured x
			paint_cmd bar;
			bar.what = paint_cmd::kind::box;
			bar.x = n.x + padding +
			        text_width(std::u32string_view{shown}.substr(start, caret_cp - start), font_px, fs);
			bar.y = top;
			bar.w = 1;
			bar.h = font_px;
			bar.argb = pack_argb(fg);
			out->push_back(bar);
		}
	}

	// <textarea>: a multi-line editable field. Hard newlines only (no
	// soft wrap - long lines clip; documented); rows/cols size the box
	// when the page sets no width/height.
	constexpr void emit_textarea(node & n, std::int32_t font_px, std::int32_t padding, std::int32_t top) {
		const font_spec fs = font_spec_of(&n);
		const std::int32_t line_h = font_px + font_px / 4;
		const std::int32_t rows = detail::parse_int_attr(n.attribute("rows"), 2);
		const std::int32_t cols = detail::parse_int_attr(n.attribute("cols"), 20);
		computed_style cs{&n, resolve, n.chain()};
		const std::int32_t cw0 = text_width(U"0", font_px, fs);
		if (cs.get("width").empty()) { n.w = cols * cw0 + 2 * padding; }
		if (cs.get("height").empty()) { n.h = rows * line_h + 2 * padding; }
		else {
			const ctcss::length hl = ctcss::parse_length(cs.get("height"));
			if (hl.ok) { n.h = static_cast<std::int32_t>(hl.value); }
		}
		emit_frame(n, detail_frame_argb(n));
		const std::int32_t content_w = n.w - 2 * padding;
		const ctcss::color fg = text_color(n);
		// caret line/column (byte offsets -> per-line code points)
		const std::string & v = n.value;
		const std::size_t cb = static_cast<std::size_t>(n.caret < 0 ? 0 : n.caret) > v.size()
		                           ? v.size()
		                           : static_cast<std::size_t>(n.caret < 0 ? 0 : n.caret);
		std::int32_t line_i = 0, caret_line = 0;
		std::size_t ls = 0, caret_ls = 0;
		for (std::size_t i = 0; i <= v.size(); ++i) {
			if (i == cb) {
				caret_line = line_i;
				caret_ls = ls;
			}
			if (i == v.size()) { break; }
			if (v[i] == '\n') {
				++line_i;
				ls = i + 1;
			}
		}
		// inner scrolling (no scrollbar, like Firefox's overlay style):
		// clamp scroll_top to the content, and when an edit moved the
		// caret, bring it into view first
		std::int32_t total_lines = 1;
		for (const char ch : v) {
			if (ch == '\n') { ++total_lines; }
		}
		const std::int32_t view_h = n.h - 2 * padding;
		const std::int32_t max_scroll =
		    total_lines * line_h > view_h ? total_lines * line_h - view_h : 0;
		if (n.caret_follow) {
			n.caret_follow = false;
			const std::int32_t caret_top = caret_line * line_h;
			if (caret_top < n.scroll_top) { n.scroll_top = caret_top; }
			if (caret_top + line_h > n.scroll_top + view_h) { n.scroll_top = caret_top + line_h - view_h; }
		}
		if (n.scroll_top > max_scroll) { n.scroll_top = max_scroll; }
		if (n.scroll_top < 0) { n.scroll_top = 0; }
		const std::int32_t skip_lines = n.scroll_top / line_h;

		// render each hard line, clipped to the box
		std::int32_t y = top;
		std::size_t pos = 0;
		std::int32_t li2 = 0;
		while (li2 < skip_lines && pos <= v.size()) { // scrolled-off lines
			const std::size_t nl = v.find('\n', pos);
			if (nl == std::string::npos) { break; }
			pos = nl + 1;
			++li2;
		}
		while (pos <= v.size() && y + font_px <= n.y + n.h - padding) {
			std::size_t nl = v.find('\n', pos);
			if (nl == std::string::npos) { nl = v.size(); }
			std::u32string line = utf8_to_utf32(std::string_view{v}.substr(pos, nl - pos));
			std::size_t take = line.size();
			while (take > 0 && text_width(std::u32string_view{line}.substr(0, take), font_px, fs) > content_w) {
				--take;
			}
			if (take > 0) {
				paint_cmd c;
				c.what = paint_cmd::kind::text;
				c.x = n.x + padding;
				c.y = y;
				c.w = text_width(std::u32string_view{line}.substr(0, take), font_px, fs);
				c.h = font_px;
				c.argb = pack_argb(fg);
				c.text = std::u32string{std::u32string_view{line}.substr(0, take)};
				c.font_px = font_px;
				push_text(std::move(c), fs);
			}
			if (n.focused && li2 == caret_line) {
				const std::u32string upto = utf8_to_utf32(std::string_view{v}.substr(caret_ls, cb - caret_ls));
				paint_cmd bar;
				bar.what = paint_cmd::kind::box;
				bar.x = n.x + padding + text_width(upto, font_px, fs);
				bar.y = y;
				bar.w = 1;
				bar.h = font_px;
				bar.argb = pack_argb(fg);
				out->push_back(bar);
			}
			y += line_h;
			++li2;
			if (nl == v.size()) { break; }
			pos = nl + 1;
		}
	}

	// <table>: rows through thead/tbody/tfoot, td/th cells laid out as
	// ordinary blocks in EQUAL-WIDTH columns (no colspan/rowspan/auto
	// sizing - documented); border-spacing fixed at 2px; the `border`
	// attribute >= 1 draws Firefox's classic 1px frames.
	constexpr void collect_rows(node & n, std::vector<node *> & rows) {
		for (const auto & c : n.children) {
			if (c->tag == "tr") { rows.push_back(c.get()); }
			else if (c->tag == "thead" || c->tag == "tbody" || c->tag == "tfoot") { collect_rows(*c, rows); }
		}
	}
	constexpr void emit_table(node & n, std::int32_t padding, std::int32_t top, std::int32_t content_w,
	                          const box & cb) {
		const std::int32_t spacing = 2;
		const bool bordered = detail::parse_int_attr(n.attribute("border"), 0) > 0;
		std::int32_t cursor = top;
		// caption first, as a plain block above the grid
		for (const auto & c : n.children) {
			if (c->tag == "caption") { cursor += place(*c, n.x + padding, cursor, content_w, cb); }
		}
		std::vector<node *> rows;
		collect_rows(n, rows);
		std::size_t ncols = 0;
		for (node * r : rows) {
			std::size_t k = 0;
			for (const auto & c : r->children) {
				if (c->tag == "td" || c->tag == "th") { ++k; }
			}
			if (k > ncols) { ncols = k; }
		}
		if (ncols == 0) {
			n.h = (cursor - n.y) + padding;
			return;
		}
		const std::int32_t colw =
		    (content_w - spacing * static_cast<std::int32_t>(ncols + 1)) / static_cast<std::int32_t>(ncols);
		std::int32_t table_right = n.x + padding;
		for (node * r : rows) {
			cursor += spacing;
			std::int32_t cx = n.x + padding + spacing;
			std::int32_t row_h = 0;
			for (const auto & c : r->children) {
				if (c->tag != "td" && c->tag != "th") { continue; }
				const std::int32_t h = place(*c, cx, cursor, colw, cb);
				if (h > row_h) { row_h = h; }
				cx += colw + spacing;
			}
			if (cx > table_right) { table_right = cx; }
			// row rect for hit tests
			r->x = n.x + padding;
			r->y = cursor;
			r->w = cx - r->x;
			r->h = row_h;
			if (bordered) {
				for (const auto & c : r->children) {
					if (c->tag == "td" || c->tag == "th") { emit_frame(*c, 0xFF808080u); }
				}
			}
			cursor += row_h;
		}
		cursor += spacing;
		n.h = (cursor - n.y) + padding;
		if (bordered) { emit_frame(n, 0xFF808080u); }
	}

	constexpr void emit_select(node & n, std::int32_t font_px, std::int32_t padding, std::int32_t top, std::int32_t content_w) {
		const ctcss::color fg = text_color(n);
		const std::int32_t line_h = font_px + font_px / 4;
		const std::int32_t nopt = n.option_count();
		const std::string_view align = text_align(n);
		node * sel = n.nth_option(n.selected_option());
		const std::u32string label = sel != nullptr ? utf8_to_utf32(trimmed(sel->text)) : std::u32string{};
		const std::int32_t arrow = font_px * 2 / 3;
		const std::int32_t tw = text_width(label, font_px);
		std::int32_t tx = n.x + padding;
		if (align == std::string_view{"center"}) { tx += (content_w - tw - arrow - font_px / 3) / 2; }
		else if (align == std::string_view{"right"}) { tx += content_w - tw - arrow - font_px / 3; }
		if (tx < n.x + padding) { tx = n.x + padding; }
		{
			paint_cmd c;
			c.what = paint_cmd::kind::text;
			c.x = tx;
			c.y = top;
			c.w = tw;
			c.h = font_px;
			c.argb = pack_argb(fg);
			c.text = label;
			c.font_px = font_px;
			out->push_back(c);
		}
		// a down-pointing triangle just to the right of the label
		const std::int32_t ax = tx + tw + font_px / 3, ay = top + font_px / 4;
		for (std::int32_t r = 0; r * 2 < arrow; ++r) {
			paint_cmd b;
			b.what = paint_cmd::kind::box;
			b.x = ax + r;
			b.y = ay + r;
			b.w = arrow - 2 * r;
			b.h = 1;
			b.argb = pack_argb(fg);
			if (b.w > 0) { out->push_back(b); }
		}
		n.h = line_h + 2 * padding;

		if (n.select_open && overlays != nullptr && nopt > 0) {
			// content-width popup, centered under the control, painted on top
			std::int32_t ow = 0;
			for (std::int32_t i = 0; i < nopt; ++i) {
				if (node * o = n.nth_option(i)) {
					const std::int32_t w2 = text_width(utf8_to_utf32(trimmed(o->text)), font_px);
					if (w2 > ow) { ow = w2; }
				}
			}
			ow += 2 * padding + font_px;
			std::int32_t ox = n.x + padding + (content_w - ow) / 2;
			if (ox < n.x) { ox = n.x; }
			const std::int32_t oy = n.y + n.h, row_h = line_h + 4;
			paint_cmd bg;
			bg.what = paint_cmd::kind::box;
			bg.x = ox;
			bg.y = oy;
			bg.w = ow;
			bg.h = row_h * nopt;
			bg.argb = 0xFF000000u; // opaque list background (option { background:#000 })
			overlays->push_back(bg);
			for (std::int32_t i = 0; i < nopt; ++i) {
				node * opt = n.nth_option(i);
				if (opt == nullptr) { continue; }
				const std::int32_t ry = oy + i * row_h;
				if (i == n.selected_option()) { // highlight the current choice
					paint_cmd hl;
					hl.what = paint_cmd::kind::box;
					hl.x = ox;
					hl.y = ry;
					hl.w = ow;
					hl.h = row_h;
					hl.argb = 0xFF2A4A8Au;
					overlays->push_back(hl);
				}
				const std::u32string ot = utf8_to_utf32(trimmed(opt->text));
				paint_cmd t;
				t.what = paint_cmd::kind::text;
				t.x = ox + padding + font_px / 4;
				t.y = ry + 2;
				t.w = text_width(ot, font_px);
				t.h = font_px;
				t.argb = 0xFFFFFFFFu;
				t.text = ot;
				t.font_px = font_px;
				overlays->push_back(t);
				opt->x = ox;
				opt->y = ry;
				opt->w = ow;
				opt->h = row_h;
			}
		} else { // closed: options are not hit targets
			for (std::int32_t i = 0; i < nopt; ++i) {
				if (node * o = n.nth_option(i)) { o->x = o->y = o->w = o->h = 0; }
			}
		}
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
constexpr std::vector<paint_cmd> layout(document & doc, std::int32_t viewport_w,
                                     const style_fn & resolve,
                                     const text_measure_fn & measure = {},
                                     std::int32_t viewport_h = 0) {
	std::vector<paint_cmd> content, overlays;
	detail::layout_pass pass{&resolve, &measure, &content, viewport_w, viewport_h, &overlays};
	if (doc.root) { (void)pass.place(*doc.root, 0, 0, viewport_w, detail::box{0, 0, viewport_w, viewport_h}); }
	std::vector<paint_cmd> out;
	if (doc.root) { detail::collect_backgrounds(*doc.root, resolve, out); }
	out.insert(out.end(), content.begin(), content.end());
	out.insert(out.end(), overlays.begin(), overlays.end()); // popups paint on top
	return out;
}

} // namespace ctbrowser

#endif
