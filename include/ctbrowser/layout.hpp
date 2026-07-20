#ifndef CTBROWSER__LAYOUT__HPP
#define CTBROWSER__LAYOUT__HPP

#include "dom.hpp"
#include "utf.hpp"
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

// measure a UTF-32 text run's width in pixels at a font size; when absent the
// layout assumes the embedded font's square glyphs (width == font_px per code
// point). The SDL shell installs a TTF-backed measure when a real font loads.
using text_measure_fn = ctjs::cfunction<int(std::u32string_view, int)>;

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
	std::u32string text;    // kind::text (UTF-32 code points)
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

// a containing block: the rect that position:absolute/fixed children and
// percentage lengths resolve against
struct box {
	int x = 0, y = 0, w = 0, h = 0;
};

struct layout_pass {
	const style_fn * resolve;
	const text_measure_fn * measure;
	std::vector<paint_cmd> * out;
	int vw = 0;  // viewport width  (for position:fixed/absolute + vw/text-align)
	int vh = 0;  // viewport height (for top/bottom + vh + vertical placement)
	std::vector<paint_cmd> * overlays = nullptr; // painted last, on top (open <select>)

	static constexpr int UNSET = -1000000;

	constexpr int text_width(std::u32string_view t, int font_px) const {
		if (measure != nullptr && *measure) { return (*measure)(t, font_px); }
		return static_cast<int>(t.size()) * font_px; // one square glyph per code point
	}

	// resolve a CSS length to px: px/unitless absolute; % of `basis`; vw/vh of the
	// viewport; em of `font_px`; rem of the 16px root. calc() is not handled.
	constexpr int len_px(std::string_view s, int basis, int font_px, int fallback) const {
		const ctcss::length l = ctcss::parse_length(s);
		if (!l.ok) { return fallback; }
		switch (l.u) {
		case ctcss::unit::px:
		case ctcss::unit::none: return static_cast<int>(l.value);
		case ctcss::unit::pct: return static_cast<int>(l.value / 100.0 * basis);
		case ctcss::unit::vw: return static_cast<int>(l.value / 100.0 * vw);
		case ctcss::unit::vh: return static_cast<int>(l.value / 100.0 * vh);
		case ctcss::unit::em: return static_cast<int>(l.value * font_px);
		case ctcss::unit::rem: return static_cast<int>(l.value * 16.0);
		}
		return fallback;
	}
	constexpr int prop_px(const computed_style & cs, std::string_view prop, int basis,
	                      int font_px, int fallback) const {
		return len_px(cs.get(prop), basis, font_px, fallback);
	}

	// computed font-size (px): em/% relative to the parent's font, vw/vh to the
	// viewport, rem to the root; inherits when unset (root default 16px)
	constexpr int font_of(node * n) const {
		if (n == nullptr) { return 16; }
		computed_style cs{n, resolve, n->chain()};
		const ctcss::length l = ctcss::parse_length(cs.get("font-size"));
		if (!l.ok) { return font_of(n->parent); }
		switch (l.u) {
		case ctcss::unit::px:
		case ctcss::unit::none: return static_cast<int>(l.value);
		case ctcss::unit::em: return static_cast<int>(l.value * font_of(n->parent));
		case ctcss::unit::pct: return static_cast<int>(l.value / 100.0 * font_of(n->parent));
		case ctcss::unit::rem: return static_cast<int>(l.value * 16.0);
		case ctcss::unit::vw: return static_cast<int>(l.value / 100.0 * vw);
		case ctcss::unit::vh: return static_cast<int>(l.value / 100.0 * vh);
		}
		return font_of(n->parent);
	}

	// transform: translate/translateX/translateY offsets (px); % of the element's
	// own (w,h). rotate/scale/translateZ and other functions are ignored.
	constexpr void translate_of(const computed_style & cs, int w, int h, int font_px,
	                            int & tx, int & ty) const {
		const std::string_view t = cs.get("transform");
		tx = 0;
		ty = 0;
		size_t i = 0;
		while (i < t.size()) {
			const size_t p = t.find("translate", i);
			if (p == std::string_view::npos) { break; }
			size_t j = p + 9; // past "translate"
			int axis = 2;     // 0 = X, 1 = Y, 2 = both
			if (j < t.size() && (t[j] == 'X' || t[j] == 'x')) { axis = 0; ++j; }
			else if (j < t.size() && (t[j] == 'Y' || t[j] == 'y')) { axis = 1; ++j; }
			else if (j < t.size() && (t[j] == 'Z' || t[j] == 'z')) { i = j + 1; continue; }
			if (j >= t.size() || t[j] != '(') { i = j; continue; }
			const size_t open = j + 1, close = t.find(')', open);
			if (close == std::string_view::npos) { break; }
			const std::string_view args = t.substr(open, close - open);
			const size_t comma = args.find(',');
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
	constexpr void translate(size_t start, node & n, int dx, int dy) {
		for (size_t i = start; i < out->size(); ++i) { (*out)[i].x += dx; (*out)[i].y += dy; }
		translate_rects(n, dx, dy);
	}
	constexpr void translate_rects(node & n, int dx, int dy) {
		n.x += dx;
		n.y += dy;
		for (const auto & c : n.children) { translate_rects(*c, dx, dy); }
	}

	// lay out `n` with its content starting at (x, y), `width` available for the
	// border box, `cb` the containing block (nearest positioned ancestor, or the
	// viewport). Returns the border-box height CONTRIBUTED TO FLOW - 0 for
	// position:fixed/absolute, which are lifted out and positioned against `cb`
	// (fixed) / the viewport (fixed), then offset by any transform:translate.
	constexpr int place(node & n, int x, int y, int width, const box & cb) {
		if (skipped_tag(n.tag)) {
			n.x = n.y = n.w = n.h = 0;
			return 0;
		}
		computed_style cs{&n, resolve, n.chain()};
		if (cs.get("display") == std::string_view{"none"}) {
			n.x = n.y = n.w = n.h = 0;
			return 0;
		}
		const int font_px = font_of(&n);
		const std::string_view pos = cs.get("position");
		if (pos == std::string_view{"fixed"} || pos == std::string_view{"absolute"}) {
			const box vp{0, 0, vw, vh};
			const box & c = (pos == std::string_view{"fixed"}) ? vp : cb;
			const int left = prop_px(cs, "left", c.w, font_px, UNSET);
			const int right = prop_px(cs, "right", c.w, font_px, UNSET);
			const int top = prop_px(cs, "top", c.h, font_px, UNSET);
			const int bottom = prop_px(cs, "bottom", c.h, font_px, UNSET);
			int pw = prop_px(cs, "width", c.w, font_px, -1);
			if (pw < 0) { pw = c.w - (left != UNSET ? left : 0) - (right != UNSET ? right : 0); }
			if (pw < 0) { pw = c.w; }
			const int maxw = prop_px(cs, "max-width", c.w, font_px, -1);
			if (maxw >= 0 && pw > maxw) { pw = maxw; }
			const int ph = prop_px(cs, "height", c.h, font_px, -1); // definite? else content
			const size_t start = out->size();
			const size_t start_ov = overlays != nullptr ? overlays->size() : 0;
			// children resolve against THIS box, laid out at the origin then lifted
			const box child_cb{0, 0, pw, ph >= 0 ? ph : c.h};
			const int laid = block_body(n, 0, 0, pw, child_cb);
			const int h = ph >= 0 ? ph : laid;
			int fx = c.x + (left != UNSET ? left : (right != UNSET ? c.w - pw - right : 0));
			int fy = c.y + (top != UNSET ? top : (bottom != UNSET ? c.h - h - bottom : 0));
			int tx = 0, ty = 0;
			translate_of(cs, pw, h, font_px, tx, ty);
			translate(start, n, fx + tx, fy + ty);
			// overlays (open <select> popups) emitted by this subtree ride along
			if (overlays != nullptr) {
				for (size_t i = start_ov; i < overlays->size(); ++i) {
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
	constexpr int block_body(node & n, int x, int y, int width, const box & cb) {
		computed_style cs{&n, resolve, n.chain()};
		const int font_px = font_of(&n);
		const int margin = prop_px(cs, "margin", width, font_px, 0);
		const int padding = prop_px(cs, "padding", width, font_px, 0);

		int box_w = prop_px(cs, "width", width, font_px, -1);
		if (box_w < 0) { box_w = width - 2 * margin; }
		if (n.is_canvas()) { box_w = n.canvas_w; }
		const int content_w = box_w - 2 * padding;

		n.x = x + margin;
		n.y = y + margin;
		n.w = box_w;

		int cursor = n.y + padding;

		// <select> renders as a native widget: the selected option collapsed with
		// a down-arrow (plus a popup list on top when open), not stacked options
		if (n.is_select()) {
			emit_select(n, font_px, padding, cursor, content_w);
			return n.h + 2 * margin;
		}

		// direct text, decoded to UTF-32 code points once, then wrapped to the
		// content width (measured width, or one square glyph per code point)
		const std::u32string text = utf8_to_utf32(n.text);
		const ctcss::color fg = text_color(n); // CSS color inherits from ancestors
		const std::string_view align = text_align(n);
		if (!trimmed(std::u32string_view{text}).empty()) {
			// hard-break on U+000A (from <br>) into lines, then greedily wrap each
			std::u32string_view remain = text;
			bool more = true;
			while (more) {
				const size_t nl = remain.find(U'\n');
				const std::u32string_view line =
				    trimmed(nl == std::u32string_view::npos ? remain : remain.substr(0, nl));
				if (line.empty()) {
					cursor += font_px + font_px / 4; // blank row (e.g. consecutive <br>)
				} else {
					std::u32string_view rest = line;
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
						cmd.text = u32str(rest.substr(0, take));
						cmd.font_px = font_px;
						out->push_back(cmd);
						cursor += font_px + font_px / 4;
						rest.remove_prefix(take);
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
			cmd.x = n.x + padding;
			cmd.y = cursor;
			cmd.w = n.canvas_w;
			cmd.h = n.canvas_h;
			cmd.canvas_node = &n;
			out->push_back(cmd);
			cursor += n.canvas_h;
		}

		// children stack vertically; a static box passes its own containing block
		// straight through (only positioned boxes establish a new one, in place())
		for (const auto & c : n.children) {
			cursor += place(*c, n.x + padding, cursor, content_w, cb);
		}

		int box_h = prop_px(cs, "height", cb.h, font_px, -1);
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
		const size_t begin = v.find_first_not_of(ws);
		if (begin == std::u32string_view::npos) { return {}; }
		return v.substr(begin, v.find_last_not_of(ws) - begin + 1);
	}

	// render a <select>: the collapsed control (selected option + down-arrow) into
	// `out`, and, when open, the popup option list into `overlays` (painted last,
	// on top). Sets each <option>'s hit rect (its overlay row, or empty when
	// closed) so the engine can route clicks. Sets n.h to the control height.
	constexpr void emit_select(node & n, int font_px, int padding, int top, int content_w) {
		const ctcss::color fg = text_color(n);
		const int line_h = font_px + font_px / 4;
		const int nopt = n.option_count();
		const std::string_view align = text_align(n);
		node * sel = n.nth_option(n.selected_option());
		const std::u32string label = sel != nullptr ? utf8_to_utf32(trimmed(sel->text)) : std::u32string{};
		const int arrow = font_px * 2 / 3;
		const int tw = text_width(label, font_px);
		int tx = n.x + padding;
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
		const int ax = tx + tw + font_px / 3, ay = top + font_px / 4;
		for (int r = 0; r * 2 < arrow; ++r) {
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
			int ow = 0;
			for (int i = 0; i < nopt; ++i) {
				if (node * o = n.nth_option(i)) {
					const int w2 = text_width(utf8_to_utf32(trimmed(o->text)), font_px);
					if (w2 > ow) { ow = w2; }
				}
			}
			ow += 2 * padding + font_px;
			int ox = n.x + padding + (content_w - ow) / 2;
			if (ox < n.x) { ox = n.x; }
			const int oy = n.y + n.h, row_h = line_h + 4;
			paint_cmd bg;
			bg.what = paint_cmd::kind::box;
			bg.x = ox;
			bg.y = oy;
			bg.w = ow;
			bg.h = row_h * nopt;
			bg.argb = 0xFF000000u; // opaque list background (option { background:#000 })
			overlays->push_back(bg);
			for (int i = 0; i < nopt; ++i) {
				node * opt = n.nth_option(i);
				if (opt == nullptr) { continue; }
				const int ry = oy + i * row_h;
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
			for (int i = 0; i < nopt; ++i) {
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
constexpr std::vector<paint_cmd> layout(document & doc, int viewport_w,
                                     const style_fn & resolve,
                                     const text_measure_fn & measure = {},
                                     int viewport_h = 0) {
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
