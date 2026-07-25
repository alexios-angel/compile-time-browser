#ifndef CTBROWSER__LAYOUT__HPP
#define CTBROWSER__LAYOUT__HPP

#include <cstdint>

#include <cstddef>

#include "dom.hpp"
#include "ua.hpp"
#include "utf.hpp"
#include <ctc/cfunction.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <algorithm>
#include <array>
#include <optional>
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

	[[nodiscard]] constexpr std::string_view get(std::string_view prop) const {
		if (n->inline_style.has(prop)) { return n->inline_style.get(prop); }
		return (*resolve)(chain.data(), chain.size(), prop);
	}
	[[nodiscard]] constexpr std::int32_t px(std::string_view prop, std::int32_t fallback) const {
		const ctcss::length l = ctcss::parse_length(get(prop));
		if (!l.ok || (l.u != ctcss::unit::px && l.u != ctcss::unit::none)) { return fallback; }
		return static_cast<std::int32_t>(l.value);
	}
	[[nodiscard]] constexpr ctcss::color color_of(std::string_view prop, ctcss::color fallback) const {
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

// --- paint_cmd factories. A filled rect is by far the most common paint
// (frames, carets, highlights, widget chrome, backgrounds); building one
// field-by-field at every site was the single most repeated shape in the
// engine. `fixed` marks the viewport-anchored ones - the scrollbar, the
// context menu, position:fixed subtrees - which page scrolling skips.
[[nodiscard]] constexpr paint_cmd box_cmd(std::int32_t x, std::int32_t y, std::int32_t w,
                                          std::int32_t h, std::uint32_t argb, bool fixed = false) {
	paint_cmd c;
	c.what = paint_cmd::kind::box;
	c.fixed = fixed;
	c.x = x;
	c.y = y;
	c.w = w;
	c.h = h;
	c.argb = argb;
	return c;
}

// a text run. The caller stamps family/weight/style afterwards - via
// layout_pass::push_text, which also emits the decoration band.
[[nodiscard]] constexpr paint_cmd text_cmd(std::int32_t x, std::int32_t y, std::int32_t w,
                                           std::int32_t h, std::uint32_t argb, std::u32string text,
                                           std::int32_t font_px) {
	paint_cmd c;
	c.what = paint_cmd::kind::text;
	c.x = x;
	c.y = y;
	c.w = w;
	c.h = h;
	c.argb = argb;
	c.text = std::move(text);
	c.font_px = font_px;
	return c;
}

// a <canvas>'s pixel buffer, blitted by the shell at its natural size
[[nodiscard]] constexpr paint_cmd canvas_cmd(std::int32_t x, std::int32_t y, node * n) {
	paint_cmd c;
	c.what = paint_cmd::kind::canvas;
	c.x = x;
	c.y = y;
	c.w = n->canvas_w;
	c.h = n->canvas_h;
	c.canvas_node = n;
	return c;
}

// inline-level elements share rows (Firefox's inline flow); the CSS
// display property overrides the tag default either way
inline constexpr std::array inline_level_tags{
    std::string_view{"a"},      std::string_view{"span"},   std::string_view{"b"},
    std::string_view{"i"},      std::string_view{"u"},      std::string_view{"s"},
    std::string_view{"em"},     std::string_view{"strong"}, std::string_view{"code"},
    std::string_view{"small"},  std::string_view{"big"},    std::string_view{"mark"},
    std::string_view{"label"},  std::string_view{"input"},  std::string_view{"button"},
    std::string_view{"select"}, std::string_view{"textarea"}, std::string_view{"sub"},
    std::string_view{"sup"},    std::string_view{"tt"},     std::string_view{"kbd"},
    std::string_view{"samp"},   std::string_view{"cite"},   std::string_view{"var"},
    std::string_view{"dfn"},    std::string_view{"abbr"},   std::string_view{"ins"},
    std::string_view{"del"},    std::string_view{"strike"}, std::string_view{"img"},
    std::string_view{"q"},      std::string_view{"time"},   std::string_view{"output"}};

[[nodiscard]] constexpr bool inline_level_tag(std::string_view tag) {
	return std::ranges::contains(inline_level_tags, tag);
}
// inline containers with no explicit width shrink to their content
[[nodiscard]] constexpr bool shrink_wrap_tag(std::string_view tag) {
	return inline_level_tag(tag) && tag != "input" && tag != "button" && tag != "select" &&
	       tag != "textarea" && tag != "img";
}

// the resolved text style of one element (family/weight/style/deco)
struct font_spec {
	std::string family;
	bool bold = false;
	bool italic = false;
	paint_cmd::strike deco = paint_cmd::strike::none;
};

// everything a block resolves once about its own text, then hands to the
// pieces that draw it (the wrapped flow, and a label's trailing run)
struct block_text {
	std::u32string text; // n.text decoded to code points, decoded once
	ctcss::color fg;     // inherited `color`
	std::string_view align;
	font_spec fs;
	std::int32_t font_px = 16;
};

// the UA line box: one glyph row plus quarter-em leading. Flow text, the
// widget emitters and the engine's caret math all step by this.
[[nodiscard]] constexpr std::int32_t line_height(std::int32_t font_px) noexcept {
	return font_px + font_px / 4;
}

[[nodiscard]] constexpr uint32_t pack_argb(ctcss::color c) {
	return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
	       (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

[[nodiscard]] constexpr bool skipped_tag(std::string_view tag) {
	return tag == "head" || tag == "style" || tag == "script" || tag == "title";
}

// a containing block: the rect that position:absolute/fixed children and
// percentage lengths resolve against
struct box {
	std::int32_t x = 0, y = 0, w = 0, h = 0;
};

// The styling resolvers, the paint emitters and the native widget
// chrome: everything that decides how a box LOOKS. Nothing in here
// calls back into flow layout - that dependency runs strictly one
// way, which is why layout_pass extends this and not the reverse.
struct widget_painter {
	const style_fn * resolve;
	const text_measure_fn * measure;
	std::vector<paint_cmd> * out;
	std::int32_t vw = 0;  // viewport width  (for position:fixed/absolute + vw/text-align)
	std::int32_t vh = 0;  // viewport height (for top/bottom + vh + vertical placement)
	std::vector<paint_cmd> * overlays = nullptr; // painted last, on top (open <select>)

	static constexpr std::int32_t UNSET = -1000000;

	[[nodiscard]] constexpr std::int32_t text_width(std::u32string_view t, std::int32_t font_px,
	                                  const font_spec & fs = {}) const {
		if (measure != nullptr && *measure) { return (*measure)(t, font_px, fs.family, fs.bold, fs.italic); }
		return static_cast<std::int32_t>(t.size()) * font_px; // one square glyph per code point
	}

	// CSS inheritance, as one walk: the value of `prop` declared by the
	// nearest ancestor (self first), or "" when nobody declares it. Every
	// inherited text-style resolver below is a parse on top of this.
	[[nodiscard]] constexpr std::string_view inherited(node * n, std::string_view prop) const {
		for (node * p = n; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const std::string_view v = pcs.get(prop);
			if (!v.empty()) { return v; }
		}
		return {};
	}

	// inherited text-style resolvers: multiple fonts coexist in one
	// document - every element resolves its own family/weight/style, and
	// every text cmd carries them
	constexpr std::string font_family_of(node * n) const {
		return std::string{inherited(n, "font-family")};
	}
	constexpr bool font_bold_of(node * n) const {
		const std::string_view v = inherited(n, "font-weight");
		if (v.empty()) { return false; }
		if (ctcss::detail::ascii_iequals(v, "bold") || ctcss::detail::ascii_iequals(v, "bolder")) { return true; }
		if (ctcss::detail::ascii_iequals(v, "normal") || ctcss::detail::ascii_iequals(v, "lighter")) { return false; }
		const ctcss::length l = ctcss::parse_length(v); // numeric weights
		return l.ok && l.value >= 600;
	}
	constexpr bool font_italic_of(node * n) const {
		const std::string_view v = inherited(n, "font-style");
		return ctcss::detail::ascii_iequals(v, "italic") || ctcss::detail::ascii_iequals(v, "oblique");
	}
	constexpr paint_cmd::strike text_deco_of(node * n) const {
		// text-decoration is not truly inherited in CSS, but in a block
		// engine treating it as inherited matches how it propagates to
		// the descendants that render the text
		const std::string_view v = inherited(n, "text-decoration");
		if (v.find("underline") != std::string_view::npos) { return paint_cmd::strike::underline; }
		if (v.find("line-through") != std::string_view::npos) { return paint_cmd::strike::line_through; }
		return paint_cmd::strike::none; // an explicit `none` stopped the walk
	}
	constexpr font_spec font_spec_of(node * n) const {
		return font_spec{font_family_of(n), font_bold_of(n), font_italic_of(n), text_deco_of(n)};
	}

	// emit one filled rect
	constexpr void push_box(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h,
	                        std::uint32_t argb) {
		out->push_back(box_cmd(x, y, w, h, argb));
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
		if (dy >= 0 && dw > 0) { push_box(dx, dcy, dw, 1, argb); }
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
	[[nodiscard]] constexpr std::int32_t font_of(node * n) const {
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

	// a 2D pixel displacement (transform: translate)
	struct offset {
		std::int32_t x = 0;
		std::int32_t y = 0;
	};

	// transform: translate/translateX/translateY offsets (px); % of the element's
	// own (w,h). rotate/scale/translateZ and other functions are ignored.
	constexpr offset translate_of(const computed_style & cs, std::int32_t w, std::int32_t h,
	                              std::int32_t font_px) const {
		const std::string_view t = cs.get("transform");
		std::int32_t tx = 0;
		std::int32_t ty = 0;
		std::size_t i = 0;
		while (i < t.size()) {
			const std::size_t p = t.find("translate", i);
			if (p == std::string_view::npos) { break; }
			std::size_t j = p + 9; // past "translate"
			enum class axis_of { both, x_only, y_only };
			axis_of axis = axis_of::both;
			if (j < t.size() && (t[j] == 'X' || t[j] == 'x')) { axis = axis_of::x_only; ++j; }
			else if (j < t.size() && (t[j] == 'Y' || t[j] == 'y')) { axis = axis_of::y_only; ++j; }
			else if (j < t.size() && (t[j] == 'Z' || t[j] == 'z')) { i = j + 1; continue; }
			if (j >= t.size() || t[j] != '(') { i = j; continue; }
			const std::size_t open = j + 1, close = t.find(')', open);
			if (close == std::string_view::npos) { break; }
			const std::string_view args = t.substr(open, close - open);
			const std::size_t comma = args.find(',');
			const std::string_view a0 = trimmed(comma == std::string_view::npos ? args : args.substr(0, comma));
			const std::string_view a1 = comma == std::string_view::npos ? std::string_view{} : trimmed(args.substr(comma + 1));
			if (axis == axis_of::x_only) {
				tx += len_px(a0, w, font_px, 0);
			} else if (axis == axis_of::y_only) {
				ty += len_px(a0, h, font_px, 0);
			} else {
				tx += len_px(a0, w, font_px, 0);
				if (!a1.empty()) { ty += len_px(a1, h, font_px, 0); }
			}
			i = close + 1;
		}
		return {tx, ty};
	}

	// text-align inherits ("" = default/left)
	[[nodiscard]] constexpr std::string_view text_align(node & n) const { return inherited(&n, "text-align"); }

	// color inherits too, but this one canNOT go through inherited(): it
	// walks until a value PARSES, not until one is merely declared, so an
	// unreadable `color: mauve` keeps searching upward instead of ending
	// the walk. Default black.
	[[nodiscard]] constexpr ctcss::color text_color(node & n) const {
		for (node * p = &n; p != nullptr; p = p->parent) {
			computed_style pcs{p, resolve, p->chain()};
			const ctcss::color c = ctcss::parse_color(pcs.get("color"));
			if (c.ok) { return c; }
		}
		return {true, 0, 0, 0, 255};
	}

	// the longest prefix of `rest` that measures within content_w - at
	// least one code point, so an overlong glyph still makes progress.
	// Shared by the flow text and the textarea's soft wrap; the two differ
	// only in where they then put the word break, so that part is theirs.
	[[nodiscard]] constexpr std::size_t fitting_prefix(std::u32string_view rest, std::int32_t content_w,
	                                     std::int32_t font_px, const font_spec & fs) const {
		std::size_t take = rest.size();
		while (take > 1 && text_width(rest.substr(0, take), font_px, fs) > content_w) { --take; }
		return take;
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
		return n.is_disabled() ? detail::ua_widget_frame_disabled : detail::ua_widget_frame;
	}
	constexpr void emit_frame(const node & n, std::uint32_t argb) {
		if (n.w <= 1 || n.h <= 1) { return; }
		push_box(n.x, n.y, n.w, 1, argb);                 // top
		push_box(n.x, n.y + n.h - 1, n.w, 1, argb);       // bottom
		push_box(n.x, n.y, 1, n.h, argb);                 // left
		push_box(n.x + n.w - 1, n.y, 1, n.h, argb);       // right
	}
	// checkbox / radio: a ~14px (at 16px font) box or disc; Firefox's
	// modern theme - #8f8f9d frame, #0060df fill when checked, white mark
	constexpr void emit_toggle(node & n, std::int32_t font_px, std::int32_t padding, bool radio) {
		const std::int32_t side = font_px > 9 ? font_px * 7 / 8 : 8;
		n.w = side + 2 * padding;
		n.h = side + 2 * padding;
		const std::int32_t bx = n.x + padding, by = n.y + padding;
		if (!radio) {
			// field, frame, and - when checked - accent fill + white check mark
			push_box(bx, by, side, side, n.checked ? detail::ua_widget_accent : detail::ua_widget_field);
			push_box(bx, by, side, 1, detail_frame_argb(n));
			push_box(bx, by + side - 1, side, 1, detail_frame_argb(n));
			push_box(bx, by, 1, side, detail_frame_argb(n));
			push_box(bx + side - 1, by, 1, side, detail_frame_argb(n));
			if (n.checked) {
				// a stepped check: short down-stroke + longer up-stroke
				const std::int32_t u = side > 11 ? 2 : 1; // stroke thickness
				const std::int32_t cx0 = bx + side / 5, cy0 = by + side / 2;
				for (std::int32_t i = 0; i < side / 4; ++i) {
					push_box(cx0 + i, cy0 + i, u, u + 1, detail::ua_widget_mark);
				}
				const std::int32_t mx = bx + side / 5 + side / 4, my = by + side / 2 + side / 4;
				for (std::int32_t i = 0; i < side / 2; ++i) {
					push_box(mx + i, my - i, u, u + 1, detail::ua_widget_mark);
				}
			}
		} else {
			// disc drawn as rows (the emit_select triangle technique): width
			// steps out then back in - an octagon-ish circle at glyph sizes
			for (std::int32_t r = 0; r < side; ++r) {
				const std::int32_t d = r < side / 2 ? side / 2 - 1 - r : r - side / 2;
				std::int32_t inset = d > side / 4 ? d - side / 4 : 0;
				push_box(bx + inset, by + r, side - 2 * inset, 1,
				         r == 0 || r == side - 1 ? detail_frame_argb(n) : detail::ua_widget_field);
				if (inset > 0) {
					push_box(bx + inset, by + r, 1, 1, detail_frame_argb(n));
					push_box(bx + side - inset - 1, by + r, 1, 1, detail_frame_argb(n));
				} else {
					push_box(bx, by + r, 1, 1, detail_frame_argb(n));
					push_box(bx + side - 1, by + r, 1, 1, detail_frame_argb(n));
				}
			}
			if (n.checked) {
				// the accent dot, inset a third
				const std::int32_t inset = side / 3;
				push_box(bx + inset, by + inset, side - 2 * inset, side - 2 * inset,
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
		// the PERSISTED horizontal scroll: the view holds still and only
		// moves when the caret would leave it (minimal adjustment, like a
		// real text field)
		std::size_t start = n.scroll_cp < 0 ? 0 : static_cast<std::size_t>(n.scroll_cp);
		if (start > shown.size()) { start = shown.size(); }
		if (caret_cp < start) { start = caret_cp; } // scrolled left of the view
		while (caret_cp > start &&
		       text_width(std::u32string_view{shown}.substr(start, caret_cp - start), font_px, fs) >
		           content_w) {
			++start; // advance just enough to bring the caret back in
		}
		n.scroll_cp = static_cast<std::int32_t>(start);
		std::u32string_view view{shown};
		view = view.substr(start);
		std::size_t take = view.size(); // clip the tail to the field
		while (take > 0 && text_width(view.substr(0, take), font_px, fs) > content_w) { --take; }
		const ctcss::color fg = text_color(n);
		// geometry cache for the engine's caret-from-click math
		n.ui_font_px = font_px;
		n.ui_text_x = n.x + padding;
		n.ui_text_y = top;
		n.ui_line_h = line_height(font_px);
		n.ui_family = fs.family;
		n.ui_bold = fs.bold;
		n.ui_italic = fs.italic;
		// the selection highlight (light blue, black text stays readable)
		if (n.has_selection()) {
			const std::size_t vb = static_cast<std::size_t>(n.sel_begin());
			const std::size_t ve = static_cast<std::size_t>(n.sel_end());
			std::size_t cb2 = utf8_length(std::string_view{n.value}.substr(0, vb));
			std::size_t ce2 = utf8_length(std::string_view{n.value}.substr(0, ve));
			cb2 = cb2 > start ? cb2 - start : 0;
			ce2 = ce2 > start ? ce2 - start : 0;
			if (cb2 > take) { cb2 = take; }
			if (ce2 > take) { ce2 = take; }
			if (ce2 > cb2) {
				push_box(n.x + padding + text_width(view.substr(0, cb2), font_px, fs), top,
				         text_width(view.substr(cb2, ce2 - cb2), font_px, fs), font_px,
				         detail::ua_selection_highlight);
			}
		}
		if (take > 0) {
			push_text(text_cmd(n.x + padding, top, text_width(view.substr(0, take), font_px, fs),
			                   font_px, pack_argb(fg), std::u32string{view.substr(0, take)}, font_px),
			          fs);
		}
		if (n.focused && n.ui_caret_on) { // the caret, blinking on the engine's clock
			push_box(n.x + padding + text_width(std::u32string_view{shown}.substr(start, caret_cp - start),
			                                    font_px, fs),
			         top, 1, font_px, pack_argb(fg));
		}
	}

	// <textarea>: a multi-line editable field with SOFT WRAPPING (long
	// lines wrap at word boundaries like Firefox's wrap=soft default;
	// hard newlines still break). The visual lines publish through
	// ui_lines so the engine's caret navigation and click mapping speak
	// wrapped lines; scrolling is internal, scrollbar-less.
	constexpr void emit_textarea(node & n, std::int32_t font_px, std::int32_t padding, std::int32_t top) {
		const font_spec fs = font_spec_of(&n);
		const std::int32_t line_h = line_height(font_px);
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
		// geometry cache for the engine
		n.ui_font_px = font_px;
		n.ui_text_x = n.x + padding;
		n.ui_text_y = top;
		n.ui_line_h = line_h;
		n.ui_family = fs.family;
		n.ui_bold = fs.bold;
		n.ui_italic = fs.italic;

		// 1) the VISUAL lines: hard-split on newlines, soft-wrap each
		// segment at the content width (word boundaries preferred)
		const std::u32string all = utf8_to_utf32(n.value);
		n.ui_lines.clear();
		{
			std::size_t seg = 0;
			while (seg <= all.size()) {
				std::size_t nl = std::u32string_view{all}.substr(seg).find(U'\n');
				const std::size_t seg_end = nl == std::u32string_view::npos ? all.size() : seg + nl;
				std::size_t pos = seg;
				do {
					std::u32string_view rest{all.data() + pos, seg_end - pos};
					std::size_t take = fitting_prefix(rest, content_w, font_px, fs);
					// a textarea KEEPS the break space on the line it ended
					// (brk + 1, not brk) - the caret has to be able to sit
					// after it, and no character may be skipped in an editable
					if (take < rest.size()) {
						const std::size_t brk = rest.substr(0, take + 1).rfind(U' ');
						if (brk != std::u32string_view::npos && brk > 0) { take = brk + 1; }
					}
					node::text_line l;
					l.cp_start = static_cast<std::int32_t>(pos);
					l.cp_end = static_cast<std::int32_t>(pos + take);
					l.hard = pos + take >= seg_end;
					l.x = n.x + padding;
					l.w = text_width(rest.substr(0, take), font_px, fs);
					n.ui_lines.push_back(l);
					pos += take;
				} while (pos < seg_end);
				if (seg_end == all.size()) { break; }
				seg = seg_end + 1; // past the newline
			}
			if (n.ui_lines.empty()) { n.ui_lines.push_back({0, 0, n.x + padding, 0, 0, true}); }
		}

		// 2) caret position in cp space -> its visual line
		std::size_t caret_cp = utf8_length(std::string_view{n.value}.substr(
		    0, static_cast<std::size_t>(n.caret < 0 ? 0 : n.caret)));
		if (caret_cp > all.size()) { caret_cp = all.size(); }
		std::int32_t caret_line = static_cast<std::int32_t>(n.ui_lines.size()) - 1;
		for (std::size_t i = 0; i < n.ui_lines.size(); ++i) {
			const node::text_line & l = n.ui_lines[i];
			const auto cc = static_cast<std::int32_t>(caret_cp);
			if (cc < l.cp_end || (cc == l.cp_end && l.hard)) {
				caret_line = static_cast<std::int32_t>(i);
				break;
			}
		}

		// 3) inner scrolling: clamp, and pull an edited caret into view
		const std::int32_t total_lines = static_cast<std::int32_t>(n.ui_lines.size());
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

		// 4) stamp on-screen y per line, render the visible ones
		const std::size_t sel_b_cp =
		    n.has_selection() ? utf8_length(std::string_view{n.value}.substr(0, static_cast<std::size_t>(n.sel_begin()))) : 0;
		const std::size_t sel_e_cp =
		    n.has_selection() ? utf8_length(std::string_view{n.value}.substr(0, static_cast<std::size_t>(n.sel_end()))) : 0;
		for (std::size_t i = 0; i < n.ui_lines.size(); ++i) {
			node::text_line & l = n.ui_lines[i];
			l.y = top + static_cast<std::int32_t>(i) * line_h - n.scroll_top;
			if (l.y + font_px > n.y + n.h - padding || l.y < n.y) { continue; } // clipped
			const std::u32string_view line{all.data() + l.cp_start,
			                               static_cast<std::size_t>(l.cp_end - l.cp_start)};
			if (n.has_selection() && sel_e_cp > sel_b_cp) { // the selected sub-span
				const std::size_t hb = sel_b_cp > static_cast<std::size_t>(l.cp_start)
				                           ? sel_b_cp - static_cast<std::size_t>(l.cp_start)
				                           : 0;
				std::size_t he = sel_e_cp > static_cast<std::size_t>(l.cp_start)
				                     ? sel_e_cp - static_cast<std::size_t>(l.cp_start)
				                     : 0;
				if (he > line.size()) { he = line.size(); }
				if (he > hb) {
					push_box(l.x + text_width(line.substr(0, hb), font_px, fs), l.y,
					         text_width(line.substr(hb, he - hb), font_px, fs), font_px,
					         detail::ua_selection_highlight);
				}
			}
			if (!line.empty()) {
				push_text(text_cmd(l.x, l.y, l.w, font_px, pack_argb(fg), std::u32string{line}, font_px),
				          fs);
			}
			if (n.focused && n.ui_caret_on && static_cast<std::int32_t>(i) == caret_line) {
				const std::size_t col = caret_cp - static_cast<std::size_t>(l.cp_start);
				std::int32_t bar_x =
				    l.x + text_width(line.substr(0, col <= line.size() ? col : line.size()), font_px, fs);
				// wrap spaces may exceed the content width by a glyph; the
				// caret still pins inside the box (Firefox behavior)
				if (bar_x > n.x + n.w - padding - 1) { bar_x = n.x + n.w - padding - 1; }
				push_box(bar_x, l.y, 1, font_px, pack_argb(fg));
			}
		}
	}

	constexpr void emit_select(node & n, std::int32_t font_px, std::int32_t padding, std::int32_t top, std::int32_t content_w) {
		const ctcss::color fg = text_color(n);
		const std::int32_t line_h = line_height(font_px);
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
		out->push_back(text_cmd(tx, top, tw, font_px, pack_argb(fg), label, font_px));
		// a down-pointing triangle just to the right of the label
		const std::int32_t ax = tx + tw + font_px / 3, ay = top + font_px / 4;
		for (std::int32_t r = 0; r * 2 < arrow; ++r) {
			if (arrow - 2 * r > 0) { push_box(ax + r, ay + r, arrow - 2 * r, 1, pack_argb(fg)); }
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
			// opaque list background (the UA sheet's option { background:#000 })
			overlays->push_back(box_cmd(ox, oy, ow, row_h * nopt, detail::ua_option_list_bg));
			for (std::int32_t i = 0; i < nopt; ++i) {
				node * opt = n.nth_option(i);
				if (opt == nullptr) { continue; }
				const std::int32_t ry = oy + i * row_h;
				if (i == n.selected_option()) { // highlight the current choice
					overlays->push_back(box_cmd(ox, ry, ow, row_h, detail::ua_option_selected));
				}
				const std::u32string ot = utf8_to_utf32(trimmed(opt->text));
				overlays->push_back(text_cmd(ox + padding + font_px / 4, ry + 2, text_width(ot, font_px),
				                             font_px, detail::ua_option_text, ot, font_px));
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

// Where the boxes GO: in-flow block and inline layout, out-of-flow
// (positioned) placement, and the table grid - the one "widget" that
// lays its cells out with place(), so it lives here with flow rather
// than with the chrome.
struct layout_pass : widget_painter {
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
			const offset t = translate_of(cs, pw, h, font_px);
			translate(start, n, fx + t.x, fy + t.y);
			// position:fixed is viewport-anchored: exempt the subtree (rects
			// via the node flag, paints via the cmd flag) from page scrolling
			n.viewport_fixed = pos == std::string_view{"fixed"};
			if (n.viewport_fixed) {
				for (std::size_t i = start; i < out->size(); ++i) { (*out)[i].fixed = true; }
			}
			// overlays (open <select> popups) emitted by this subtree ride along
			if (overlays != nullptr) {
				for (std::size_t i = start_ov; i < overlays->size(); ++i) {
					(*overlays)[i].x += fx + t.x;
					(*overlays)[i].y += fy + t.y;
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

		const std::int32_t box_w = resolve_box_width(n, cs, width, font_px, m, p);
		const std::int32_t content_w = box_w - p.left - p.right;

		n.x = x + m.left;
		n.y = y + m.top;
		n.w = box_w;

		std::int32_t cursor = n.y + p.top;

		// the form controls draw native chrome instead of flowing content
		if (const auto widget_h = emit_native_widget(n, font_px, padding, cursor, content_w, m, cb)) {
			return *widget_h;
		}

		emit_disclosure_marker(n, font_px, cursor);
		emit_list_marker(n, font_px, cursor);

		const block_text bt{utf8_to_utf32(n.text), text_color(n), text_align(n), font_spec_of(&n),
		                    font_px};
		// a <label> wrapping a control renders the control FIRST, its text
		// after it on the same line ("[x] option one") - the common form
		// idiom; ordering between text and element children is otherwise
		// not preserved by the DOM's concatenated-text model
		const bool children_first = n.tag == "label" && !n.children.empty();
		std::int32_t content_max_x = n.x + p.left; // shrink-wrap extent tracker
		n.ui_lines.clear(); // rebuilt below; the engine's selection geometry
		n.ui_font_px = font_px;
		n.ui_family = bt.fs.family;
		n.ui_bold = bt.fs.bold;
		n.ui_italic = bt.fs.italic;

		if (!children_first) { emit_text_flow(n, bt, n.x + p.left, content_w, cursor, content_max_x); }

		if (n.is_canvas()) {
			out->push_back(canvas_cmd(n.x + p.left, cursor, &n));
			cursor += n.canvas_h;
		}

		layout_children(n, bt, p, content_w, cb, children_first, cursor, content_max_x);

		std::int32_t box_h = prop_px(cs, "height", cb.h, font_px, -1);
		if (box_h < 0) { box_h = (cursor - n.y) + p.bottom; }
		n.h = box_h;
		// inline containers with no explicit width shrink to their content
		if (detail::shrink_wrap_tag(n.tag) && prop_px(cs, "width", width, font_px, -1) < 0) {
			const std::int32_t want = (content_max_x - n.x) + p.right;
			if (want > 0 && want < n.w) { n.w = want; }
		}

		// buttons carry Firefox's 1px widget border (layout has no border
		// property - widget frames belong to the widgets)
		if (n.tag == "button") { emit_frame(n, detail_frame_argb(n)); }

		// backgrounds are emitted in a pre-pass by collect_backgrounds below
		return n.h + m.top + m.bottom;
	}

	// the border-box width: a canvas is its pixel buffer (even against an
	// explicit width), then any CSS width wins, then button/select shrink
	// to their content the way Firefox renders them inline-block, and
	// everything else fills the line.
	constexpr std::int32_t resolve_box_width(node & n, const computed_style & cs, std::int32_t width,
	                                         std::int32_t font_px, const sides & m,
	                                         const sides & p) const {
		if (n.is_canvas()) { return n.canvas_w; }
		const std::int32_t explicit_w = prop_px(cs, "width", width, font_px, -1);
		if (explicit_w >= 0) { return explicit_w; }
		if (n.tag == "button") {
			const std::int32_t tw = text_width(utf8_to_utf32(trimmed(n.text)), font_px, font_spec_of(&n));
			return tw + p.left + p.right + 2;
		}
		if (n.is_select()) {
			const font_spec bfs = font_spec_of(&n);
			std::int32_t widest = 0;
			for (const auto & c : n.children) { // size to the widest option
				if (c->tag != "option") { continue; }
				const std::int32_t w2 = text_width(utf8_to_utf32(trimmed(c->text)), font_px, bfs);
				if (w2 > widest) { widest = w2; }
			}
			return widest + font_px + p.left + p.right + 4; // + the arrow
		}
		return width - m.left - m.right;
	}

	// The controls that render as native widgets rather than flowing their
	// content: <select> collapses to the chosen option plus an arrow (and a
	// popup when open), toggles draw a box or disc, text fields and tables
	// lay themselves out. Returns the flow height when `n` is one of them,
	// nullopt when it is an ordinary block.
	constexpr std::optional<std::int32_t> emit_native_widget(node & n, std::int32_t font_px,
	                                                         std::int32_t padding, std::int32_t top,
	                                                         std::int32_t content_w, const sides & m,
	                                                         const box & cb) {
		const auto flow_h = [&] { return n.h + m.top + m.bottom; };
		if (n.is_select()) {
			emit_select(n, font_px, padding, top, content_w);
			emit_frame(n, detail_frame_argb(n));
			return flow_h();
		}
		if (n.is_checkbox() || n.is_radio()) {
			emit_toggle(n, font_px, padding, n.is_radio());
			return flow_h();
		}
		if (n.is_input()) {
			if (ctcss::detail::ascii_iequals(n.input_type(), "hidden")) {
				zero_rects(n);
				return 0;
			}
			emit_input(n, font_px, padding, top);
			return flow_h();
		}
		if (n.is_textarea()) {
			emit_textarea(n, font_px, padding, top);
			return flow_h();
		}
		if (n.tag == "table") {
			emit_table(n, padding, top, content_w, cb);
			return flow_h();
		}
		return std::nullopt;
	}

	// the details/summary disclosure triangle: right-pointing when closed,
	// down-pointing when open (the summary's UA padding-left leaves the gutter)
	constexpr void emit_disclosure_marker(node & n, std::int32_t font_px, std::int32_t top) {
		if (!n.is_summary() || n.parent == nullptr || !n.parent->is_details()) { return; }
		const std::uint32_t argb = pack_argb(text_color(n));
		const std::int32_t s = font_px / 2 + 2;
		const std::int32_t mx = n.x + 4;
		const std::int32_t my = top + font_px / 2 - s / 2;
		if (n.parent->open) { // down-pointing: rows narrow toward the tip
			for (std::int32_t r = 0; r * 2 < s; ++r) { push_box(mx + r, my + r + s / 4, s - 2 * r, 1, argb); }
		} else { // right-pointing: columns shorten toward the tip
			for (std::int32_t c = 0; c * 2 < s; ++c) { push_box(mx + c + s / 4, my + c, 1, s - 2 * c, argb); }
		}
	}

	// <li> markers, drawn into the 40px gutter the UA ul/ol padding-left leaves
	constexpr void emit_list_marker(node & n, std::int32_t font_px, std::int32_t top) {
		if (n.tag != "li" || n.parent == nullptr) { return; }
		const std::uint32_t argb = pack_argb(text_color(n));
		if (n.parent->tag == "ul") {
			const std::int32_t d = font_px / 3 > 2 ? font_px / 3 : 3; // the disc
			push_box(n.x - d * 3, top + font_px / 2 - d / 2, d, d, argb);
			return;
		}
		if (n.parent->tag != "ol") { return; }
		std::int32_t idx = 1; // this item's ordinal among its <li> siblings
		for (const auto & sib : n.parent->children) {
			if (sib.get() == &n) { break; }
			if (sib->tag == "li") { ++idx; }
		}
		std::u32string num;
		for (const char ch : std::to_string(idx)) { num.push_back(static_cast<char32_t>(ch)); }
		num.push_back(U'.');
		const font_spec fs = font_spec_of(&n);
		const std::int32_t nw = text_width(num, font_px, fs);
		push_text(text_cmd(n.x - nw - font_px / 2, top, nw, font_px, argb, std::move(num), font_px), fs);
	}

	// the block's own text: hard-break on U+000A (from <br>), then greedily
	// wrap each line to the content width, preferring word boundaries. Each
	// visual line publishes its code-point span through n.ui_lines, which is
	// what the engine's selection and caret geometry read.
	constexpr void emit_text_flow(node & n, const block_text & bt, std::int32_t left,
	                              std::int32_t content_w, std::int32_t & cursor,
	                              std::int32_t & content_max_x) {
		if (trimmed(std::u32string_view{bt.text}).empty()) { return; }
		const bool preserve = n.tag == "pre"; // pre keeps leading spaces
		std::u32string_view remain = bt.text;
		while (true) {
			const std::size_t nl = remain.find(U'\n');
			const std::u32string_view raw_line =
			    nl == std::u32string_view::npos ? remain : remain.substr(0, nl);
			const std::u32string_view line = preserve ? raw_line : trimmed(raw_line);
			if (line.empty()) {
				cursor += line_height(bt.font_px); // blank row (e.g. consecutive <br>)
			} else {
				std::u32string_view rest = line;
				while (!rest.empty()) {
					const std::size_t take = wrap_take(rest, content_w, bt.font_px, bt.fs, preserve);
					emit_text_line(n, bt, rest.substr(0, take),
					               static_cast<std::int32_t>(rest.data() - bt.text.data()), left,
					               content_w, cursor, content_max_x);
					cursor += line_height(bt.font_px);
					rest.remove_prefix(take);
					while (!preserve && !rest.empty() && rest.front() == U' ') {
						rest.remove_prefix(1); // eat the break space(s)
					}
				}
			}
			if (nl == std::u32string_view::npos) { break; }
			remain.remove_prefix(nl + 1);
		}
	}

	// flow text breaks AT the space and then eats it, so the next line
	// starts on a word (a single overlong word still breaks mid-word, like
	// a browser). `pre` takes the hard cut.
	[[nodiscard]] constexpr std::size_t wrap_take(std::u32string_view rest, std::int32_t content_w,
	                                std::int32_t font_px, const font_spec & fs,
	                                bool preserve = false) const {
		std::size_t take = fitting_prefix(rest, content_w, font_px, fs);
		if (take < rest.size() && !preserve) {
			const std::size_t brk = rest.substr(0, take + 1).rfind(U' ');
			if (brk != std::u32string_view::npos && brk > 0) { take = brk; }
		}
		return take;
	}

	// one wrapped line: its alignment, its ui_lines entry, its selection
	// highlight and its glyphs
	constexpr void emit_text_line(node & n, const block_text & bt, std::u32string_view line,
	                              std::int32_t cp_start, std::int32_t left, std::int32_t content_w,
	                              std::int32_t cursor, std::int32_t & content_max_x) {
		const std::int32_t tw = text_width(line, bt.font_px, bt.fs);
		std::int32_t tx = left;
		if (bt.align == std::string_view{"center"}) { tx += (content_w - tw) / 2; }
		else if (bt.align == std::string_view{"right"}) { tx += content_w - tw; }
		const std::int32_t cp_end = cp_start + static_cast<std::int32_t>(line.size());
		n.ui_lines.push_back({cp_start, cp_end, tx, cursor, tw, true});
		if (tx + tw > content_max_x) { content_max_x = tx + tw; }
		// the CHARACTER-precise selection highlight: the overlap of
		// [sel_from, sel_to) with this line
		if (n.sel_from >= 0 && n.sel_to > n.sel_from) {
			const std::int32_t hb = n.sel_from > cp_start ? n.sel_from : cp_start;
			const std::int32_t he = n.sel_to < cp_end ? n.sel_to : cp_end;
			if (he > hb) {
				const auto off = static_cast<std::size_t>(hb - cp_start);
				const auto len = static_cast<std::size_t>(he - hb);
				push_box(tx + text_width(line.substr(0, off), bt.font_px, bt.fs), cursor,
				         text_width(line.substr(off, len), bt.font_px, bt.fs), bt.font_px,
				         detail::ua_selection_highlight);
			}
		}
		push_text(text_cmd(tx, cursor, tw, bt.font_px, pack_argb(bt.fg), u32str(line), bt.font_px),
		          bt.fs);
	}

	// children: consecutive INLINE-LEVEL children share rows (wrapping like
	// Firefox's inline flow, items vertically centered on their line); block
	// children stack, passing the containing block straight through. A
	// closed <details> shows only its <summary>.
	constexpr void layout_children(node & n, const block_text & bt, const sides & p,
	                               std::int32_t content_w, const box & cb, bool children_first,
	                               std::int32_t & cursor, std::int32_t & content_max_x) {
		const std::int32_t line_start_x = n.x + p.left;
		const std::int32_t right_edge = line_start_x + content_w;
		const std::int32_t gap = bt.font_px / 3;
		std::int32_t line_x = line_start_x;
		std::int32_t line_top = cursor;
		std::int32_t line_h = 0;
		std::vector<std::pair<std::size_t, node *>> line_items;
		const auto flush_line = [&]() {
			for (auto & [ci, cn] : line_items) { // center each item on the line
				const std::int32_t dy = (line_h - cn->h) / 2;
				if (dy > 0) { translate(ci, *cn, 0, dy); }
			}
			line_items.clear();
			if (line_h > 0) { cursor = line_top + line_h; }
			line_top = cursor;
			line_x = line_start_x;
			line_h = 0;
		};
		for (const auto & c : n.children) {
			if (n.is_details() && !n.open && !c->is_summary()) {
				zero_rects(*c);
				continue;
			}
			computed_style ccs{c.get(), resolve, c->chain()};
			const std::string_view disp = ccs.get("display");
			const bool inl = disp == std::string_view{"inline"} ||
			                 disp == std::string_view{"inline-block"} ||
			                 (disp.empty() && detail::inline_level_tag(c->tag));
			if (!inl) {
				flush_line();
				cursor += place(*c, line_start_x, cursor, content_w, cb);
				line_top = cursor;
				continue;
			}
			const std::size_t ci = out->size();
			const std::int32_t old_x = line_x, old_top = line_top;
			std::int32_t avail = right_edge - line_x;
			if (avail < bt.font_px) { avail = content_w; }
			const std::int32_t h = place(*c, line_x, line_top, avail, cb);
			if (line_x > line_start_x && c->x + c->w > right_edge) {
				// does not fit beside its predecessors: wrap to a new line
				flush_line();
				translate(ci, *c, line_start_x - old_x, line_top - old_top);
			}
			line_items.push_back({ci, c.get()});
			if (h > line_h) { line_h = h; }
			line_x = c->x + c->w + gap;
			if (c->x + c->w > content_max_x) { content_max_x = c->x + c->w; }
		}
		// a wrapping label's text continues the control's line
		if (children_first && !trimmed(std::u32string_view{bt.text}).empty()) {
			const std::u32string_view lt = trimmed(std::u32string_view{bt.text});
			const std::int32_t tw = text_width(lt, bt.font_px, bt.fs);
			const std::int32_t ty = line_top + (line_h > bt.font_px ? (line_h - bt.font_px) / 2 : 0);
			const std::int32_t ls = static_cast<std::int32_t>(lt.data() - bt.text.data());
			n.ui_lines.push_back({ls, ls + static_cast<std::int32_t>(lt.size()), line_x, ty, tw, true});
			push_text(text_cmd(line_x, ty, tw, bt.font_px, pack_argb(bt.fg), u32str(lt), bt.font_px),
			          bt.fs);
			if (line_x + tw > content_max_x) { content_max_x = line_x + tw; }
			if (line_height(bt.font_px) > line_h) { line_h = line_height(bt.font_px); }
			line_x += tw;
		}
		flush_line();
	}

	constexpr void collect_rows(node & n, std::vector<node *> & rows) {
		for (const auto & c : n.children) {
			if (c->tag == "tr") { rows.push_back(c.get()); }
			else if (c->tag == "thead" || c->tag == "tbody" || c->tag == "tfoot") { collect_rows(*c, rows); }
		}
	}
	// the widest unwrapped text run in a subtree, at each node's own
	// resolved font - the "natural" width auto table layout sizes by
	constexpr std::int32_t natural_text_w(node & n) {
		std::int32_t w = 0;
		if (!n.text.empty()) {
			const std::u32string t = utf8_to_utf32(n.text);
			const font_spec fs = font_spec_of(&n);
			const std::int32_t px = font_of(&n);
			std::size_t pos = 0;
			while (pos <= t.size()) {
				const std::size_t nl = std::u32string_view{t}.substr(pos).find(U'\n');
				const std::size_t end = nl == std::u32string_view::npos ? t.size() : pos + nl;
				const std::int32_t lw =
				    text_width(std::u32string_view{t.data() + pos, end - pos}, px, fs);
				if (lw > w) { w = lw; }
				if (end == t.size()) { break; }
				pos = end + 1;
			}
		}
		for (const auto & c : n.children) {
			const std::int32_t cw = natural_text_w(*c);
			if (cw > w) { w = cw; }
		}
		return w;
	}
	constexpr void emit_table(node & n, std::int32_t padding, std::int32_t top, std::int32_t content_w,
	                          const box & cb) {
		const std::int32_t spacing = 2;
		const bool bordered = detail::parse_int_attr(n.attribute("border"), 0) > 0;
		std::int32_t cursor = top;
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
			for (const auto & c : n.children) {
				if (c->tag == "caption") { cursor += place(*c, n.x + padding, cursor, content_w, cb); }
			}
			n.h = (cursor - n.y) + padding;
			return;
		}
		// AUTO table layout (Firefox/Chrome): each column takes its widest
		// cell's unwrapped content, the table SHRINKS to the sum - it only
		// fills the container on an explicit CSS width, and scales the
		// columns down proportionally when the naturals would overflow
		std::vector<std::int32_t> colws(ncols, 0);
		for (node * r : rows) {
			std::size_t k = 0;
			for (const auto & c : r->children) {
				if (c->tag != "td" && c->tag != "th") { continue; }
				computed_style ccs{c.get(), resolve, c->chain()};
				const sides cp = sides_of(ccs, "padding", content_w, font_of(c.get()));
				const std::int32_t nat = natural_text_w(*c) + cp.left + cp.right + 2;
				if (nat > colws[k]) { colws[k] = nat; }
				++k;
			}
		}
		const std::int32_t gaps = spacing * static_cast<std::int32_t>(ncols + 1);
		std::int32_t natsum = 0;
		for (const std::int32_t cw : colws) { natsum += cw; }
		computed_style cs{&n, resolve, n.chain()};
		if (!cs.get("width").empty() || natsum + gaps > content_w) {
			const std::int32_t inner = content_w - gaps > 0 ? content_w - gaps : 1;
			const std::int32_t base = natsum > 0 ? natsum : 1;
			for (std::int32_t & cw : colws) { cw = cw * inner / base; }
			natsum = inner;
		}
		const std::int32_t table_w = natsum + gaps;
		// caption: centered over the TABLE, not the container - and it
		// sits OUTSIDE the table border (the frame wraps the grid only)
		for (const auto & c : n.children) {
			if (c->tag == "caption") { cursor += place(*c, n.x + padding, cursor, table_w, cb); }
		}
		const std::int32_t grid_top = cursor;
		for (node * r : rows) {
			cursor += spacing;
			std::int32_t cx = n.x + padding + spacing;
			std::int32_t row_h = 0;
			std::size_t k = 0;
			for (const auto & c : r->children) {
				if (c->tag != "td" && c->tag != "th") { continue; }
				const std::int32_t h = place(*c, cx, cursor, colws[k], cb);
				if (h > row_h) { row_h = h; }
				cx += colws[k] + spacing;
				++k;
			}
			// row rect for hit tests; cells stretch to the row height so
			// the bordered grid is uniform
			r->x = n.x + padding;
			r->y = cursor;
			r->w = cx - r->x;
			r->h = row_h;
			for (const auto & c : r->children) {
				if (c->tag != "td" && c->tag != "th") { continue; }
				c->h = row_h;
				if (bordered) { emit_frame(*c, detail::ua_table_border); }
			}
			cursor += row_h;
		}
		cursor += spacing;
		n.w = table_w + 2 * padding;
		n.h = (cursor - n.y) + padding;
		if (bordered) {
			node grid;
			grid.x = n.x + padding;
			grid.y = grid_top;
			grid.w = table_w;
			grid.h = cursor - grid_top;
			emit_frame(grid, detail::ua_table_border);
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
	if (c.ok && c.a != 0) { out.push_back(box_cmd(n.x, n.y, n.w, n.h, pack_argb(c))); }
	for (const auto & kid : n.children) { collect_backgrounds(*kid, resolve, out); }
}

} // namespace detail

// lay the document out for a viewport and produce the paint list.
// viewport_h (when > 0) anchors position:fixed/absolute top/bottom.
[[nodiscard]] constexpr std::vector<paint_cmd> layout(document & doc, std::int32_t viewport_w,
                                     const style_fn & resolve,
                                     const text_measure_fn & measure = {},
                                     std::int32_t viewport_h = 0) {
	std::vector<paint_cmd> content, overlays;
	detail::layout_pass pass{{&resolve, &measure, &content, viewport_w, viewport_h, &overlays}};
	if (doc.root) { (void)pass.place(*doc.root, 0, 0, viewport_w, detail::box{0, 0, viewport_w, viewport_h}); }
	std::vector<paint_cmd> out;
	if (doc.root) { detail::collect_backgrounds(*doc.root, resolve, out); }
	out.insert(out.end(), content.begin(), content.end());
	out.insert(out.end(), overlays.begin(), overlays.end()); // popups paint on top
	return out;
}

} // namespace ctbrowser

#endif
