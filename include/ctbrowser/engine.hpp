#ifndef CTBROWSER__ENGINE__HPP
#define CTBROWSER__ENGINE__HPP

#include <cstdint>

#include <cstddef>

#include "page.hpp"
#include "dom.hpp"
#include "image.hpp"
#include "assets.hpp"
#include "layout.hpp"
#include "anim.hpp"
#include "script.hpp"
#include "babylon.hpp"
#ifndef CTBROWSER_IN_A_MODULE
#include <limits>
#include <optional>
#include <set>
#endif

// The assembled runtime, WITHOUT any windowing: instantiate the DOM,
// bind the style resolver to the page's compile-time stylesheet, run
// the page's script with the DOM API - everything the SDL shell needs,
// and everything the headless tests exercise.
//
// The engine also owns the INPUT STATE games poll (keys down, mouse
// position/buttons - fed by the shell, exposed to scripts as
// isKeyDown/mouseX/mouseY/isMouseDown), the image registry behind
// loadImage/drawImage, and an extension seam: the shell passes extra
// host bindings (audio, screenshots, fullscreen) into the constructor.

namespace ctbrowser {

namespace detail {

// --- engine timing and input metrics (the browsers' cadences) --------
inline constexpr std::int64_t caret_blink_period_ms = 1000; // solid for the first half
inline constexpr std::int64_t caret_blink_half_ms = 500;    // Chrome's 500/500 blink
inline constexpr double wheel_step_px = 48.0;               // one notch of the wheel
inline constexpr std::int32_t page_scroll_overlap_px = 48;  // PageUp/Down keep this much
inline constexpr std::int32_t gc_interval_frames = 60;      // cycle collection, ~1 Hz
// position_from_point ranks candidate lines vertical-distance-first; the
// weight only has to outrank any plausible horizontal distance in px
inline constexpr std::int64_t line_rank_vertical_weight = 100000;

// --- caret arithmetic over a UTF-8 value -----------------------------
// The caret is a BYTE offset into node::value, but every user-facing move
// is by code point, so these convert between the two. They were static
// members of engine and never touched an engine: they are functions of a
// string (or of one node), and being free makes them reusable and
// separately testable.
[[nodiscard]] inline bool is_utf8_cont(char c) noexcept {
	return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}
// bytes in the code point ENDING at `pos` (0 when pos is at the start)
[[nodiscard]] inline std::int32_t cp_len_before(std::string_view s, std::int32_t pos) {
	std::int32_t n = 0;
	while (pos - n > 0) {
		++n;
		if (!is_utf8_cont(s[static_cast<std::size_t>(pos - n)])) { break; }
	}
	return n;
}
// bytes in the code point STARTING at `pos` (0 at the end)
[[nodiscard]] inline std::int32_t cp_len_at(std::string_view s, std::int32_t pos) {
	if (pos >= static_cast<std::int32_t>(s.size())) { return 0; }
	std::int32_t n = 1;
	while (pos + n < static_cast<std::int32_t>(s.size()) &&
	       is_utf8_cont(s[static_cast<std::size_t>(pos + n)])) {
		++n;
	}
	return n;
}
[[nodiscard]] inline std::int32_t byte_of_cp(std::string_view v, std::int32_t cp) {
	std::size_t i = 0;
	for (std::int32_t k = 0; k < cp && i < v.size(); ++k) { (void)utf8_next(v, i); }
	return static_cast<std::int32_t>(i);
}
[[nodiscard]] inline std::int32_t cp_of_byte(std::string_view v, std::int32_t b) {
	return static_cast<std::int32_t>(
	    utf8_length(v.substr(0, static_cast<std::size_t>(b < 0 ? 0 : b))));
}

// The textarea's visual lines: layout's soft-wrapped ui_lines when they
// are FRESH (i.e. they still describe the current value), else hard-line
// spans computed here, so edits between frames stay navigable.
[[nodiscard]] inline std::vector<node::text_line> visual_lines(const node & f) {
	const std::int32_t total = static_cast<std::int32_t>(utf8_length(f.value));
	if (!f.ui_lines.empty() && f.ui_lines.back().cp_end == total) { return f.ui_lines; }
	std::vector<node::text_line> out;
	const std::u32string all = utf8_to_utf32(f.value);
	std::size_t seg = 0;
	while (seg <= all.size()) {
		const std::size_t nl = std::u32string_view{all}.substr(seg).find(U'\n');
		const std::size_t seg_end = nl == std::u32string_view::npos ? all.size() : seg + nl;
		out.push_back({static_cast<std::int32_t>(seg), static_cast<std::int32_t>(seg_end), 0, 0, 0, true});
		if (seg_end == all.size()) { break; }
		seg = seg_end + 1;
	}
	if (out.empty()) { out.push_back({0, 0, 0, 0, 0, true}); }
	return out;
}
[[nodiscard]] inline std::int32_t caret_visual_line(const std::vector<node::text_line> & lines,
                                      std::int32_t caret_cp) {
	for (std::size_t i = 0; i < lines.size(); ++i) {
		const node::text_line & l = lines[i];
		if (caret_cp < l.cp_end || (caret_cp == l.cp_end && l.hard)) {
			return static_cast<std::int32_t>(i);
		}
	}
	return static_cast<std::int32_t>(lines.size()) - 1;
}

// The page scrollbar as pure geometry: given the bar's width, the page
// and viewport sizes and the current offset, where is the thumb, and what
// does dragging it mean? It holds no engine state and calls nothing, so
// the arithmetic can be read (and got wrong, and fixed) on its own.
struct scrollbar_model {
	std::int32_t width = 0; // 0 = hidden (CSS scrollbar-width: none)
	std::int32_t page_h = 0;
	std::int32_t viewport_w = 0;
	std::int32_t viewport_h = 0;
	std::int32_t scroll_y = 0;

	struct rect {
		std::int32_t x = 0, y = 0, w = 0, h = 0;
	};

	[[nodiscard]] constexpr bool scrollable() const noexcept {
		return width > 0 && page_h > viewport_h && viewport_h > 0;
	}
	[[nodiscard]] constexpr std::int32_t max_scroll() const noexcept {
		return page_h > viewport_h ? page_h - viewport_h : 0;
	}
	// the thumb in viewport coordinates; nullopt when the page fits
	[[nodiscard]] constexpr std::optional<rect> thumb() const noexcept {
		if (!scrollable()) { return std::nullopt; }
		std::int32_t h = viewport_h * viewport_h / page_h; // proportional to what is shown
		if (h < ua_scrollbar_min_thumb_h) { h = ua_scrollbar_min_thumb_h; }
		if (h > viewport_h) { h = viewport_h; }
		const std::int32_t travel = viewport_h - h;
		const std::int32_t limit = max_scroll();
		const std::int32_t y =
		    limit > 0 ? static_cast<std::int32_t>(static_cast<std::int64_t>(scroll_y) * travel / limit)
		              : 0;
		return rect{viewport_w - width, y, width, h};
	}
	// dragging the thumb so its top lands at `ty` means this offset
	[[nodiscard]] constexpr std::int32_t scroll_for_thumb_top(std::int32_t ty,
	                                            std::int32_t thumb_h) const noexcept {
		const std::int32_t travel = viewport_h - thumb_h;
		return travel > 0 ? static_cast<std::int32_t>(static_cast<std::int64_t>(ty) * max_scroll() /
		                                              travel)
		                  : 0;
	}
};

} // namespace detail

template <typename Page> class engine {
public:
	document doc;
	std::string title;
	// compile-time-embedded assets: every loadImage/playSound literal
	// in the page's script, baked in AUTOMATICALLY on std::embed-capable
	// builds (see assets.hpp; other compilers fall back to runtime file
	// loads), plus anything the caller passes explicitly (which wins).
	// Declared before images: the store keeps a pointer into it.
	std::vector<embedded_asset> assets;
	image_store images;
	std::set<std::string, std::less<>> keys_down; // transparent: string_view lookups
	double mouse_x = 0;
	double mouse_y = 0;
	bool mouse_down = false;
	// the system clipboard, as hooks (the engine is SDL-free; the shell
	// installs SDL_Get/SetClipboardText, tests install their own)
	ctc::cfunction<std::string()> clipboard_get;
	ctc::cfunction<void(std::string_view)> clipboard_set;
	// the anchor default action: how <a href> opens the OS web browser.
	// Headless (tests) leave it empty = no-op; the SDL shell installs
	// SDL_OpenURL. Fragment hrefs (#...) never call it.
	ctc::cfunction<void(std::string_view)> open_url;
	// the page stylesheet, parsed from the page's <style> text at
	// construction (ctcss::parse_value); `resolve` closes over it.
	// Declared before resolve so it is live first.
	ctcss::value_sheet css_sheet;
	// the Firefox-derived user-agent defaults (ua.hpp); consulted when
	// the page's sheet has no declaration for a property
	ctcss::value_sheet ua_sheet;
	style_fn resolve;
	text_measure_fn measure; // shell-installed when a real font loads
	dom_events ev;           // MUST precede script: bindings capture it

private:
	// --- interaction state. These are the engine's own bookkeeping: the
	// pointer's targets, the scroll offset, the caret's blink phase, the
	// open menu, the selection anchor. Nothing outside the engine reads or
	// writes them, and their trailing underscores always said so - they
	// were simply sitting in the public section. (None appears in the
	// constructor's init list, so grouping them here cannot disturb
	// initialization order, which follows declaration order.)
	node * open_select_ = nullptr;  // the <select> whose popup is currently open
	node * hovered_ = nullptr;      // the pointer's current hit target
	node * pressed_ = nullptr;      // mouse-down target (:active chain, click pairing)
	node * focused_ = nullptr;      // the focused control (:focus)
	bool click_suppressed_ = false; // set when the select popup ate the mousedown
	std::int32_t scroll_y_ = 0;     // page scroll offset (px; clamped each frame)
	std::int32_t page_h_ = 0;       // last laid-out document height
	bool sb_dragging_ = false;      // the scrollbar thumb is being dragged
	std::int32_t sb_grab_ = 0;      // pointer offset inside the thumb at grab
	double caret_base_ms_ = 0;      // the blink phase restarts on caret activity
	// context-menu state (right click; Chrome-style Copy/Cut/Paste menu)
	bool menu_open_ = false;
	std::int32_t menu_x_ = 0;
	std::int32_t menu_y_ = 0;
	std::int32_t menu_hover_ = -1;
	node * menu_target_ = nullptr;
	// page text selection: a character-precise anchor the drag extends
	bool selecting_ = false;
	node * psel_node_ = nullptr; // the anchor position (node + cp index)
	std::int32_t psel_cp_ = 0;
	std::int32_t gc_ticks_ = 0; // frames since the last cycle collection (see tick)
	// saved for do_reload; declared before script so it copies BEFORE
	// the constructor moves `extra` into all_bindings
	std::vector<ctjs::binding> extra_;

public:
	ctjs::run_result script;

	// the image decoder must arrive HERE, not be assigned afterwards:
	// the page's script runs inside this constructor, and loadImage
	// calls at script startup already need it
	explicit engine(std::vector<ctjs::binding> extra = {},
	                std::function<image(const std::string &)> image_decoder = {},
	                std::vector<embedded_asset> embedded = {})
	    : doc(instantiate_html(Page::html_text())),
	      title(Page::title()),
	      assets(detail::merge_assets(std::move(embedded), auto_assets<Page>())),
	      images{{}, std::move(image_decoder), &assets},
	      css_sheet(ctcss::parse_value(Page::style_text())),
	      ua_sheet(ctcss::parse_value(detail::ua_css)),
	      resolve([this](const ctcss::element_ref * chain, std::size_t n, std::string_view prop) {
		      // author styles first, UA defaults as the fallback - the CSS
		      // cascade-origin rule (author beats user agent)
		      const std::string_view v = ctcss::query(css_sheet, chain, n, prop);
		      if (!v.empty()) { return v; }
		      return ctcss::query(ua_sheet, chain, n, prop);
	      }),
	      extra_(extra),
	      script(ctjs::run_value(Page::script_text(), all_bindings(std::move(extra)))) {
		ev.request_submit = [this](node * f) { submit_form(f); };
		ev.request_reset = [this](node * f) { reset_form(f); };
		// CTBROWSER_DEBUG: surface a page script that threw during its top-level
		// run (construction/init), with the captured call-stack trace
		if (detail::debug_on() && !script.ok()) {
			std::fprintf(stderr, "ctbrowser: page script did not run cleanly:\n%s\n",
			             script.exception_stack().c_str());
		}
	}

	engine(const engine &) = delete;
	engine & operator=(const engine &) = delete;

	// one layout pass for a viewport width; updates node rects (and
	// the offsetLeft/width properties on exposed element handles) too
	std::vector<paint_cmd> frame(std::int32_t viewport_w) {
		// advance CSS @keyframes (writes interpolated inline styles) before layout
		detail::apply_animations(doc, css_sheet, ev.now_ms);
		update_caret_blink(); // BEFORE layout: this frame's paints carry the phase
		std::vector<paint_cmd> cmds = layout_reserving_scrollbar(viewport_w);
		apply_scroll(cmds);
		paint_scrollbar(cmds);   // the overlay bar, on top
		paint_context_menu(cmds); // and the menu above even that
		ev.refresh_tracked();
		return cmds;
	}

	// mouse-wheel scrolling: a textarea under the pointer scrolls itself
	// (Firefox-style, no scrollbar); otherwise the page scrolls. SDL
	// wheel y > 0 means "scroll up".
	void wheel(double x, double y, double dy) {
		mouse_x = x;
		mouse_y = y;
		update_hover(x, y);
		ev.dispatch("wheel", detail::wheel_event(x, y, -dy * 100.0));
		node * hit = hit_test_at(x, y);
		for (node * n = hit; n != nullptr; n = n->parent) {
			if (n->is_textarea()) {
				n->scroll_top -= static_cast<std::int32_t>(dy * detail::wheel_step_px); // layout clamps
				return;
			}
		}
		scroll_y_ -= static_cast<std::int32_t>(dy * detail::wheel_step_px); // clamped in frame()
	}
	[[nodiscard]] std::int32_t scroll_y() const { return scroll_y_; }

	// what the pointer should look like right now - the shell maps this
	// to a system cursor. CSS `cursor` (resolved through the ordinary
	// cascade, so pages override it) wins; otherwise Firefox behavior:
	// I-beam over selectable text, arrow elsewhere.
	enum class cursor_kind : std::uint8_t { arrow, pointer, text };
	// one element's resolved property, inline style first (the same
	// precedence layout's computed_style uses)
	[[nodiscard]] std::string_view styled(const node * n, std::string_view prop) const {
		if (n->inline_style.has(prop)) { return n->inline_style.get(prop); }
		const auto chain = n->chain();
		return resolve(chain.data(), chain.size(), prop);
	}
	[[nodiscard]] cursor_kind cursor() const {
		for (node * n = hovered_; n != nullptr; n = n->parent) {
			const std::string_view c = styled(n, "cursor");
			if (c.empty()) { continue; }
			if (c == "pointer") { return cursor_kind::pointer; }
			if (c == "text") { return cursor_kind::text; }
			if (c == "default" || c == "auto") { break; }
			break; // an unsupported cursor keyword falls back to the arrow
		}
		if (hovered_ != nullptr && hovered_->is_editable()) { return cursor_kind::text; }
		for (node * n = hovered_; n != nullptr; n = n->parent) {
			if (n->is_link()) { return cursor_kind::pointer; }
		}
		if (hovered_ != nullptr && !hovered_->text.empty()) { return cursor_kind::text; }
		return cursor_kind::arrow;
	}

	// --- the page scrollbar (Firefox-style overlay on the right edge).
	// Hidden via the CSS `scrollbar-width: none` (thin = 6px) on html/body.
	[[nodiscard]] std::int32_t scrollbar_width() const {
		if (doc.root == nullptr) { return detail::ua_scrollbar_width; }
		const std::string_view v = styled(doc.root.get(), "scrollbar-width");
		if (v == "none") { return 0; }
		if (v == "thin") { return detail::ua_scrollbar_width_thin; }
		return detail::ua_scrollbar_width;
	}
	// thumb geometry in viewport coordinates; false when not scrollable.
	// The out-params are the long-standing shape callers (and tests) use;
	// the arithmetic lives in detail::scrollbar_model.
	// NOT [[nodiscard]]: the geometry comes back through the out-params, and
	// a caller that already knows the bar is visible legitimately calls this
	// just to refresh them (tests/browserui.cpp does, after a thumb drag).
	bool scrollbar_thumb(std::int32_t & x, std::int32_t & y, std::int32_t & w, std::int32_t & h) const {
		const auto t = scrollbar().thumb();
		if (!t) { return false; }
		x = t->x;
		y = t->y;
		w = t->w;
		h = t->h;
		return true;
	}

	// the page's @font-face rules (family + src), for the shell to load custom
	// fonts; empty when the stylesheet declares none
	[[nodiscard]] const std::vector<ctcss::value_sheet::font_face> & font_faces() const noexcept {
		return css_sheet.font_faces;
	}

	// keep the viewport dimensions current; on a change, refresh
	// window.innerWidth/innerHeight and dispatch a DOM "resize" event so
	// scripts (e.g. BabylonJS's engine.resize()) can react
	void resize_viewport(std::int32_t w, std::int32_t h) {
		if (ev.viewport_w == w && ev.viewport_h == h) { return; }
		ev.viewport_w = w;
		ev.viewport_h = h;
		ev.refresh_tracked();
		ev.dispatch("resize", ctjs::value{});
	}

	// --- event delivery (missing handlers are quietly skipped)

	void click_at(std::int32_t x, std::int32_t y) {
		if (!doc.root) { return; }
		if (node * hit = doc.root->hit_test(x, y)) { click_on(hit, x, y); }
	}

	// The full click pipeline on a resolved target: listeners bubble first
	// (one SHARED event object - preventDefault/stopPropagation are real),
	// then the legacy onClick(id) delivery, then the element's DEFAULT
	// ACTION unless a listener prevented it. Browsers gate dispatch on
	// disabled controls, so we do too.
	void click_on(node * hit, std::int32_t x, std::int32_t y) {
		for (node * n = hit; n != nullptr; n = n->parent) {
			if (n->is_disabled()) { return; }
			if (n->is_form_control()) { break; } // only the control itself gates
		}
		ctjs::value evt = detail::mouse_event(static_cast<double>(x), static_cast<double>(y), "click");
		for (node * n = hit; n != nullptr; n = n->parent) {
			if (const auto it = ev.click_listeners.find(n); it != ev.click_listeners.end()) {
				const std::vector<ctjs::value> fns = it->second; // copy: handlers may mutate
				for (const ctjs::value & fn : fns) {
					ev.invoke(fn, {evt});
					if (detail::event_flag(evt, "__stopped_now")) { break; }
				}
			}
			if (detail::event_flag(evt, "__stopped_now")) { break; }
			if (const auto it = ev.onclick_handlers.find(n); it != ev.onclick_handlers.end()) {
				ev.invoke(it->second, {evt});
			}
			if (detail::event_flag(evt, "__stopped")) { break; }
		}
		// legacy onClick(id): nearest ancestor with a non-empty id
		node * idn = hit;
		while (idn != nullptr && idn->id.empty()) { idn = idn->parent; }
		if (idn != nullptr) { deliver(script, "onClick", idn->id); }
		if (!detail::event_flag(evt, "defaultPrevented")) { default_action(hit); }
	}
	void key(std::string_view name, bool down) {
		if (down) {
			keys_down.emplace(name);
		} else if (const auto it = keys_down.find(name); it != keys_down.end()) {
			keys_down.erase(it);
		}
		deliver(script, "onKey", name, down);
		ctjs::value evt = detail::key_event(name, down ? "keydown" : "keyup");
		ev.dispatch(down ? "keydown" : "keyup", evt);
		// the editing default action: keystrokes into the focused editable
		// (a listener's preventDefault() suppresses it, like a browser)
		if (down && name == "Escape" && menu_open_) {
			menu_open_ = false;
			return;
		}
		if (down && !detail::event_flag(evt, "defaultPrevented") && ctrl_down()) {
			if (name == "C") { do_copy(); return; }
			if (name == "X") { do_cut(); return; }
			if (name == "V") { do_paste(); return; }
			if (name == "A") { do_select_all(); return; }
		}
		if (down && !detail::event_flag(evt, "defaultPrevented")) {
			if (focused_ != nullptr && focused_->is_editable() && !focused_->is_disabled()) {
				edit_key(name);
			} else { // page scrolling (clamped in frame())
				if (name == "PageDown") { scroll_y_ += page_scroll_step(); }
				else if (name == "PageUp") { scroll_y_ -= page_scroll_step(); }
				else if (name == "Home") { scroll_y_ = 0; }
				else if (name == "End") { scroll_y_ = page_h_; }
			}
		}
	}

	// printable text lands here (the shell forwards SDL_EVENT_TEXT_INPUT;
	// tests call it directly): inserted at the caret of the focused editable
	void text_input(std::string_view utf8) {
		node * f = focused_;
		if (f == nullptr || !f->is_editable() || f->is_disabled()) { return; }
		erase_selection(f);
		f->value.insert(static_cast<std::size_t>(f->caret), utf8);
		f->caret += static_cast<std::int32_t>(utf8.size());
		f->value_dirty = true;
		f->caret_follow = true;
		caret_base_ms_ = ev.now_ms; // typing keeps the caret solid
		fire_input(f);
	}
	void mouse_move(double x, double y) {
		mouse_x = x;
		mouse_y = y;
		if (mouse_down && focused_ != nullptr && focused_->is_editable() &&
		    pressed_ == focused_) { // drag-select inside the control
			focused_->caret = caret_from_click(focused_, x, y);
			focused_->caret_follow = true;
		} else if (mouse_down && selecting_ && psel_node_ != nullptr) {
			// extend the page selection to the character under the pointer
			std::int32_t cp = 0;
			if (node * tn = position_from_point(x, y, cp)) {
				apply_page_range(psel_node_, psel_cp_, tn, cp);
			}
		}
		if (menu_open_) { // the menu tracks its own hover row
			menu_hover_ = (x >= menu_x_ && x < menu_x_ + menu_w)
			                  ? (static_cast<std::int32_t>(y) - menu_y_) / menu_item_h
			                  : -1;
			return;
		}
		if (sb_dragging_) { // dragging the scrollbar thumb
			const auto sb = scrollbar();
			if (const auto t = sb.thumb()) {
				// clamped in frame()
				scroll_y_ = sb.scroll_for_thumb_top(static_cast<std::int32_t>(y) - sb_grab_, t->h);
			}
			return;
		}
		update_hover(x, y);
		deliver(script, "onMouseMove", x, y);
		ev.dispatch("mousemove", detail::mouse_event(x, y));
	}
	// hover tracking: re-flag the ancestor chain when the hit target
	// changes; the per-frame style re-query restyles :hover for free.
	// Button events sync it too - the pointer is wherever it clicked.
	void update_hover(double x, double y) {
		node * hit = hit_test_at(x, y);
		if (hit != hovered_) {
			set_chain_flag(hovered_, &node::hovered, false);
			set_chain_flag(hit, &node::hovered, true);
			if (hovered_ != nullptr) { ev.dispatch("mouseout", detail::mouse_event(x, y, "mouseout")); }
			if (hit != nullptr) { ev.dispatch("mouseover", detail::mouse_event(x, y, "mouseover")); }
			hovered_ = hit;
		}
	}
	// button: DOM numbering (0 = left, 1 = middle, 2 = right)
	void mouse_button(double x, double y, bool down, std::int32_t button = 0) {
		mouse_x = x;
		mouse_y = y;
		mouse_down = down;
		if (!down && sb_dragging_) { // release ends a scrollbar drag
			sb_dragging_ = false;
			return;
		}
		update_hover(x, y);
		if (button == 2) { // the context-menu button
			if (down) { context_click(x, y); }
			return;
		}
		if (down && menu_open_) { // a press with the menu up: item or dismiss
			menu_click(x, y);
			return;
		}
		if (down && scrollbar_hit(x, y)) { return; } // the bar ate the press
		if (down) { press(x, y); }
		else { release(x, y); }
	}
	void tick(double dt) {
		deliver(script, "onFrame", dt);
		ev.now_ms += dt * 1000.0;
		ev.run_timers(); // setTimeout/setInterval due by the new clock
		// requestAnimationFrame: swap the list out first - callbacks
		// re-register themselves for the NEXT frame
		std::vector<ctjs::value> due;
		due.swap(ev.raf);
		for (const ctjs::value & fn : due) { ev.invoke(fn, {ctjs::value{ev.now_ms}}); }
		if (ev.reload) { do_reload(); }
		// reclaim reference cycles the page created this second (bullets/observers/
		// closures the game disposed but that still point at each other - pure
		// refcounting can't free them). Amortized: once a second, not every frame.
		if (++gc_ticks_ >= detail::gc_interval_frames) {
			gc_ticks_ = 0;
			ctjs::gc::collect();
		}
	}

	// document.location.reload(): fresh DOM, fresh script run
	void do_reload() {
		ev.reset();
		open_select_ = nullptr; // node pointers are about to be invalidated
		hovered_ = nullptr;
		pressed_ = nullptr;
		focused_ = nullptr;
		click_suppressed_ = false;
		menu_open_ = false;
		menu_target_ = nullptr;
		sb_dragging_ = false;
		selecting_ = false;
		doc = instantiate_html(Page::html_text());
		script = ctjs::run_value(Page::script_text(), all_bindings(extra_));
		ctjs::gc::collect(); // reap the old page's cycles now that its roots are gone
	}

private:
	// --- the press/release halves of a click ----------------------------

	// mousedown: the :active chain, focus, and the selection anchor
	void press(double x, double y) {
		deliver(script, "onMouseDown", x, y);
		ev.dispatch("mousedown", detail::mouse_event(x, y, "mousedown"));
		pressed_ = hit_test_at(x, y);
		set_chain_flag(pressed_, &node::pressed, true); // :active while held
		// browsers move focus on mousedown
		node * f = pressed_;
		while (f != nullptr && !f->is_focusable()) { f = f->parent; }
		set_focus(f);
		// selection: a fresh press clears the page selection; inside an
		// editable it places the caret (and anchors a drag selection),
		// elsewhere it starts the drag band
		clear_page_selection();
		selecting_ = false;
		psel_node_ = nullptr;
		if (pressed_ != nullptr && pressed_->is_editable() && pressed_ == focused_) {
			pressed_->caret = caret_from_click(pressed_, x, y);
			pressed_->sel_anchor = pressed_->caret;
			pressed_->caret_follow = true;
			caret_base_ms_ = ev.now_ms;
		} else if (!page_select_none(pressed_)) {
			// anchor the drag at the CHARACTER under the pointer
			std::int32_t cp = 0;
			if (node * tn = position_from_point(x, y, cp)) {
				selecting_ = true;
				psel_node_ = tn;
				psel_cp_ = cp;
			}
		}
		// a <select> widget (open popup, or a collapsed control) eats the
		// press - and the synthetic click that would pair with the release
		click_suppressed_ =
		    handle_select_click(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
	}

	// mouseup: the click fires HERE (browser semantics), on the nearest
	// common ancestor of the press and release targets
	void release(double x, double y) {
		ev.dispatch("mouseup", detail::mouse_event(x, y, "mouseup"));
		set_chain_flag(pressed_, &node::pressed, false);
		selecting_ = false;
		if (focused_ != nullptr && focused_->sel_anchor == focused_->caret) {
			focused_->sel_anchor = -1; // a click without a drag: no selection
		}
		node * up_hit = hit_test_at(x, y);
		if (!click_suppressed_ && pressed_ != nullptr && up_hit != nullptr) {
			if (node * target = common_ancestor(pressed_, up_hit)) {
				click_on(target, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
			}
		}
		pressed_ = nullptr;
		click_suppressed_ = false;
	}

	// --- the frame pipeline ---------------------------------------------

	// the caret blinks on Chrome's cadence: 500 ms on, 500 ms off,
	// restarted by any caret activity (so typing keeps it solid)
	void update_caret_blink() {
		if (focused_ == nullptr || !focused_->is_editable()) { return; }
		const double phase = ev.now_ms - caret_base_ms_;
		focused_->ui_caret_on =
		    phase < 0 || (static_cast<std::int64_t>(phase) % detail::caret_blink_period_ms) <
		                     detail::caret_blink_half_ms;
	}

	[[nodiscard]] std::int32_t document_height() const {
		return doc.root != nullptr ? doc.root->y + doc.root->h : 0;
	}

	// A visible scrollbar RESERVES layout space (classic Firefox/Chrome
	// bars), so an overflowing page is laid out a second time at the
	// narrowed width and content never slides under the bar. One retry is
	// enough: narrowing only ever makes a page taller, so the overflow
	// decision cannot flip back.
	std::vector<paint_cmd> layout_reserving_scrollbar(std::int32_t viewport_w) {
		std::vector<paint_cmd> cmds =
		    ctbrowser::layout(doc, viewport_w, resolve, measure, ev.viewport_h);
		ev.viewport_w = viewport_w;
		page_h_ = document_height();
		const std::int32_t sbw = scrollbar_width();
		if (sbw > 0 && page_h_ > ev.viewport_h && viewport_w > sbw) {
			cmds = ctbrowser::layout(doc, viewport_w - sbw, resolve, measure, ev.viewport_h);
			page_h_ = document_height();
		}
		return cmds;
	}

	// clamp the scroll offset to the freshly laid-out page, then shift the
	// world into view - rects AND paints together, so hit testing, script
	// rect readbacks and rendering all agree. position:fixed stays put.
	void apply_scroll(std::vector<paint_cmd> & cmds) {
		const std::int32_t max_scroll = page_h_ > ev.viewport_h ? page_h_ - ev.viewport_h : 0;
		if (scroll_y_ > max_scroll) { scroll_y_ = max_scroll; }
		if (scroll_y_ < 0) { scroll_y_ = 0; }
		if (scroll_y_ == 0) { return; }
		for (paint_cmd & c : cmds) {
			if (!c.fixed) { c.y -= scroll_y_; }
		}
		if (doc.root) { detail::offset_rects(*doc.root, -scroll_y_); }
	}

	void paint_scrollbar(std::vector<paint_cmd> & cmds) const {
		std::int32_t sx = 0, sy = 0, sw = 0, sh = 0;
		if (!scrollbar_thumb(sx, sy, sw, sh)) { return; }
		cmds.push_back(
		    detail::box_cmd(sx, 0, sw, ev.viewport_h, detail::ua_scrollbar_track, /*fixed=*/true));
		cmds.push_back(detail::box_cmd(sx, sy, sw, sh,
		                               sb_dragging_ ? detail::ua_scrollbar_thumb_active
		                                            : detail::ua_scrollbar_thumb,
		                               /*fixed=*/true));
	}

	void paint_context_menu(std::vector<paint_cmd> & cmds) const {
		if (!menu_open_) { return; }
		const auto items = menu_items();
		const std::int32_t mh = menu_item_h * static_cast<std::int32_t>(items.size());
		const auto boxc = [&cmds](std::int32_t bx, std::int32_t by, std::int32_t bw, std::int32_t bh,
		                          std::uint32_t argb) {
			cmds.push_back(detail::box_cmd(bx, by, bw, bh, argb, /*fixed=*/true));
		};
		boxc(menu_x_, menu_y_, menu_w, mh, detail::ua_menu_bg);
		if (menu_hover_ >= 0 && menu_hover_ < static_cast<std::int32_t>(items.size()) &&
		    items[static_cast<std::size_t>(menu_hover_)].enabled) {
			boxc(menu_x_, menu_y_ + menu_hover_ * menu_item_h, menu_w, menu_item_h,
			     detail::ua_menu_hover);
		}
		boxc(menu_x_, menu_y_, menu_w, 1, detail::ua_menu_border);              // top
		boxc(menu_x_, menu_y_ + mh - 1, menu_w, 1, detail::ua_menu_border);     // bottom
		boxc(menu_x_, menu_y_, 1, mh, detail::ua_menu_border);                  // left
		boxc(menu_x_ + menu_w - 1, menu_y_, 1, mh, detail::ua_menu_border);     // right
		std::int32_t iy = menu_y_;
		for (const auto & it : items) {
			std::u32string label = utf8_to_utf32(std::string{it.label});
			const std::int32_t lw = measure_text(label, menu_font_px, menu_font_family, false, false);
			paint_cmd txt = detail::text_cmd(
			    menu_x_ + menu_pad_x, iy + menu_pad_y, lw, menu_font_px,
			    it.enabled ? detail::ua_menu_text : detail::ua_menu_text_disabled, std::move(label),
			    menu_font_px);
			txt.fixed = true;
			txt.font_family = menu_font_family;
			cmds.push_back(std::move(txt));
			iy += menu_item_h;
		}
	}

	// the scrollbar's geometry for the current frame
	[[nodiscard]] detail::scrollbar_model scrollbar() const {
		return {scrollbar_width(), page_h_, ev.viewport_w, ev.viewport_h, scroll_y_};
	}

	// the deepest node under a point, in viewport coordinates
	[[nodiscard]] node * hit_test_at(double x, double y) {
		return doc.root ? doc.root->hit_test(static_cast<std::int32_t>(x),
		                                     static_cast<std::int32_t>(y))
		                : nullptr;
	}

	// Route a mouse press through a <select>: if a popup is open, a click on an
	// option row selects it (and fires the element's onchange), a click elsewhere
	// closes it; if none is open, a click on a collapsed control opens it. Returns
	// true when the widget consumed the click (so it does not also become onClick).
	bool handle_select_click(std::int32_t x, std::int32_t y) {
		if (open_select_ != nullptr) {
			node * sel = open_select_;
			std::int32_t idx = 0;
			for (const auto & c : sel->children) {
				if (c->tag != "option") { continue; }
				node * opt = c.get();
				if (opt->w > 0 && x >= opt->x && x < opt->x + opt->w && y >= opt->y && y < opt->y + opt->h) {
					sel->select_index = idx;
					sel->select_open = false;
					open_select_ = nullptr;
					fire_change(sel);
					return true;
				}
				++idx;
			}
			sel->select_open = false; // clicked off the list
			open_select_ = nullptr;
			return true;
		}
		node * hit = doc.root ? doc.root->hit_test(x, y) : nullptr;
		while (hit != nullptr && !hit->is_select()) { hit = hit->parent; }
		if (hit != nullptr) {
			hit->select_open = true;
			open_select_ = hit;
			return true;
		}
		return false;
	}
	// Fire a node-targeted listener list. The vector is COPIED first: a
	// handler is free to add or remove listeners while we are iterating.
	template <typename Map> void dispatch_to(Map & listeners, node * n, const ctjs::value & evt) {
		const auto it = listeners.find(n);
		if (it == listeners.end()) { return; }
		const std::vector<ctjs::value> fns = it->second;
		for (const ctjs::value & fn : fns) { ev.invoke(fn, {evt}); }
	}

	void fire_change(node * sel) {
		const auto it = ev.change_handlers.find(sel);
		if (it != ev.change_handlers.end() && it->second.is_function()) {
			ev.invoke(it->second, {});
		}
		dispatch_to(ev.change_listeners, sel, detail::simple_event("change"));
	}

	// a press on the scrollbar: grab the thumb, or page-jump on the track
	bool scrollbar_hit(double x, double y) {
		std::int32_t sx = 0, sy = 0, sw = 0, sh = 0;
		if (!scrollbar_thumb(sx, sy, sw, sh)) { return false; }
		const std::int32_t ix = static_cast<std::int32_t>(x), iy = static_cast<std::int32_t>(y);
		if (ix < sx || ix >= sx + sw) { return false; }
		if (iy >= sy && iy < sy + sh) {
			sb_dragging_ = true;
			sb_grab_ = iy - sy;
		} else if (iy < sy) {
			scroll_y_ -= page_scroll_step(); // page toward the click
		} else {
			scroll_y_ += page_scroll_step();
		}
		return true;
	}

	// one PageUp/PageDown (or scrollbar track-click) step: a viewport
	// height less an overlap band, so a line of context carries over
	[[nodiscard]] std::int32_t page_scroll_step() const {
		return ev.viewport_h > detail::page_scroll_overlap_px
		           ? ev.viewport_h - detail::page_scroll_overlap_px
		           : detail::page_scroll_overlap_px;
	}

	// --- interaction helpers -------------------------------------------

	// hover/active apply to the whole ancestor chain of the target
	static void set_chain_flag(node * n, bool node::* flag, bool v) {
		for (; n != nullptr; n = n->parent) { n->*flag = v; }
	}
	void set_focus(node * n) {
		if (n == focused_) { return; }
		if (focused_ != nullptr) {
			focused_->focused = false;
			focused_->sel_anchor = -1; // blur drops the field's selection
			// leaving an edited control commits it: change fires on blur
			if (focused_->is_editable() && focused_->value_dirty) {
				focused_->value_dirty = false;
				fire_change(focused_);
			}
		}
		if (n != nullptr) { n->focused = true; }
		focused_ = n;
	}
	[[nodiscard]] static node * common_ancestor(node * a, node * b) {
		for (node * x = a; x != nullptr; x = x->parent) {
			for (node * y = b; y != nullptr; y = y->parent) {
				if (x == y) { return x; }
			}
		}
		return nullptr;
	}

	// The per-element DEFAULT ACTIONS - what a browser does after the
	// click listeners ran without preventDefault(). Walks target->root
	// and performs the first applicable activation (the select popup is
	// handled earlier, on mousedown).
	void default_action(node * hit) {
		for (node * n = hit; n != nullptr; n = n->parent) {
			if (n->is_checkbox()) {
				n->checked = !n->checked;
				fire_change(n);
				return;
			}
			if (n->is_radio()) {
				if (!n->checked) {
					check_radio(n);
					fire_change(n);
				}
				return;
			}
			if (n->is_summary() && n->parent != nullptr && n->parent->is_details()) {
				n->parent->open = !n->parent->open;
				return;
			}
			if (n->tag == "label") {
				if (node * target = label_target(n); target != nullptr && !target->is_disabled()) {
					if (target->is_checkbox()) {
						target->checked = !target->checked;
						fire_change(target);
					} else if (target->is_radio()) {
						if (!target->checked) {
							check_radio(target);
							fire_change(target);
						}
					}
					if (target->is_focusable()) { set_focus(target); }
				}
				return;
			}
			if (n->is_submit_button()) {
				if (node * form = n->form_ancestor()) { submit_form(form); }
				return;
			}
			if (n->is_reset_button()) {
				if (node * form = n->form_ancestor()) { reset_form(form); }
				return;
			}
			if (n->is_link()) {
				const std::string href{n->attribute("href")};
				if (!href.empty() && href[0] == '#') {
					ev.location_hash = href; // fragment: no browser launch
				} else if (!href.empty()) {
					ev.location_href = href;
					if (open_url) { open_url(href); } // the OS web browser
				}
				return;
			}
			if (n->is_select()) { return; } // activated on mousedown
		}
	}
	void check_radio(node * r) { detail::check_radio(doc.root.get(), *r); }

	// --- selection + clipboard -----------------------------------------
	std::int32_t measure_text(std::u32string_view t, std::int32_t px, std::string_view fam,
	                          bool bold, bool italic) const {
		if (measure) { return measure(t, px, fam, bold, italic); }
		return static_cast<std::int32_t>(t.size()) * px; // the layout fallback
	}
	// map a click inside an editable to a caret byte offset, using the
	// geometry the widget emitters cached on the node
	std::int32_t caret_from_click(node * f, double mx, double my) {
		const std::string & v = f->value;
		if (f->is_textarea() && !f->ui_lines.empty()) {
			// the clicked VISUAL line (soft wrap; lines carry screen y)
			const std::int32_t line_h =
			    f->ui_line_h > 0 ? f->ui_line_h : detail::line_height(f->ui_font_px);
			std::int32_t want = (static_cast<std::int32_t>(my) - f->ui_text_y + f->scroll_top) /
			                    (line_h > 0 ? line_h : 1);
			if (want < 0) { want = 0; }
			if (want >= static_cast<std::int32_t>(f->ui_lines.size())) {
				want = static_cast<std::int32_t>(f->ui_lines.size()) - 1;
			}
			const node::text_line & l = f->ui_lines[static_cast<std::size_t>(want)];
			const std::u32string all = utf8_to_utf32(v);
			const std::u32string_view line{all.data() + l.cp_start,
			                               static_cast<std::size_t>(l.cp_end - l.cp_start)};
			const std::int32_t rel = static_cast<std::int32_t>(mx) - f->ui_text_x;
			std::int32_t cp = l.cp_start;
			std::int32_t prev_w = 0;
			for (std::size_t i = 1; i <= line.size(); ++i) {
				const std::int32_t w =
				    measure_text(line.substr(0, i), f->ui_font_px, f->ui_family, f->ui_bold, f->ui_italic);
				if (rel < (prev_w + w) / 2) { break; }
				prev_w = w;
				cp = l.cp_start + static_cast<std::int32_t>(i);
			}
			return detail::byte_of_cp(v, cp);
		}
		// text input: the view starts at the persisted scroll_cp - clicks
		// map into the VISIBLE window
		std::size_t ls = static_cast<std::size_t>(detail::byte_of_cp(v, f->scroll_cp)), le = v.size();
		// walk the line's code points; the caret snaps to the NEAREST glyph
		// boundary (click in a glyph's left half lands before it)
		const std::int32_t rel = static_cast<std::int32_t>(mx) - f->ui_text_x;
		std::size_t best = ls;
		std::u32string prefix;
		std::size_t i = ls;
		std::int32_t prev_w = 0;
		while (i < le) {
			std::size_t j = i;
			const char32_t cp = utf8_next(v, j);
			prefix.push_back(cp);
			const std::int32_t w = measure_text(prefix, f->ui_font_px, f->ui_family, f->ui_bold, f->ui_italic);
			if (rel < (prev_w + w) / 2) { break; }
			prev_w = w;
			i = j;
			best = i;
		}
		return static_cast<std::int32_t>(best);
	}
	void erase_selection(node * f) {
		if (!f->has_selection()) { return; }
		const std::int32_t b = f->sel_begin(), e2 = f->sel_end();
		f->value.erase(static_cast<std::size_t>(b), static_cast<std::size_t>(e2 - b));
		f->caret = b;
		f->sel_anchor = -1;
		f->value_dirty = true;
	}
	[[nodiscard]] bool page_select_none(node * n) const { // CSS user-select: none (overridable seam)
		for (node * p = n; p != nullptr; p = p->parent) {
			const std::string_view v = styled(p, "user-select");
			if (v == "none") { return true; }
			if (!v.empty()) { return false; }
		}
		return false;
	}
	void clear_page_selection() {
		if (doc.root) { clear_selected(*doc.root); }
	}
	static void clear_selected(node & n) {
		n.selected = false;
		n.sel_from = -1;
		n.sel_to = -1;
		for (const auto & c : n.children) { clear_selected(*c); }
	}
	// can this node's text join the page selection?
	[[nodiscard]] bool selectable_text(node * n) const {
		return n != nullptr && !n->ui_lines.empty() && !n->is_editable() && !page_select_none(n);
	}
	// the CHARACTER under (or nearest to) a point: the closest rendered
	// line vertically, then the nearest glyph boundary along it
	node * position_from_point(double mx, double my, std::int32_t & cp_out) {
		node * best = nullptr;
		const node::text_line * best_line = nullptr;
		std::int64_t best_d = std::numeric_limits<std::int64_t>::max();
		const std::int32_t ix = static_cast<std::int32_t>(mx);
		const std::int32_t iy = static_cast<std::int32_t>(my);
		if (doc.root) { nearest_line(*doc.root, ix, iy, best, best_line, best_d); }
		if (best == nullptr || best_line == nullptr) { return nullptr; }
		// a pointer ABOVE the line selects from its start, BELOW it to its
		// end (that is what makes a downward drag take the whole line);
		// within the band, walk to the nearest glyph boundary
		if (iy < best_line->y) {
			cp_out = best_line->cp_start;
			return best;
		}
		if (iy >= best_line->y + best->ui_font_px) {
			cp_out = best_line->cp_end;
			return best;
		}
		const std::u32string all = utf8_to_utf32(best->text);
		const std::u32string_view line{all.data() + best_line->cp_start,
		                               static_cast<std::size_t>(best_line->cp_end - best_line->cp_start)};
		const std::int32_t rel = ix - best_line->x;
		std::int32_t cp = best_line->cp_start;
		std::int32_t prev_w = 0;
		for (std::size_t i = 1; i <= line.size(); ++i) {
			const std::int32_t w = measure_text(line.substr(0, i), best->ui_font_px, best->ui_family,
			                                    best->ui_bold, best->ui_italic);
			if (rel < (prev_w + w) / 2) { break; }
			prev_w = w;
			cp = best_line->cp_start + static_cast<std::int32_t>(i);
		}
		cp_out = cp;
		return best;
	}
	void nearest_line(node & n, std::int32_t ix, std::int32_t iy, node *& best,
	                  const node::text_line *& best_line, std::int64_t & best_d) {
		if (selectable_text(&n)) {
			for (const node::text_line & l : n.ui_lines) {
				// vertical distance to the line band, then horizontal to its span
				const std::int32_t vd = iy < l.y            ? l.y - iy
				                        : iy >= l.y + n.ui_font_px ? iy - (l.y + n.ui_font_px) + 1
				                                                   : 0;
				const std::int32_t hd = ix < l.x         ? l.x - ix
				                        : ix > l.x + l.w ? ix - (l.x + l.w)
				                                         : 0;
				const std::int64_t d =
				    static_cast<std::int64_t>(vd) * detail::line_rank_vertical_weight + hd;
				if (d < best_d) {
					best_d = d;
					best = &n;
					best_line = &l;
				}
			}
		}
		for (const auto & c : n.children) { nearest_line(*c, ix, iy, best, best_line, best_d); }
	}
	// mark the character range between two positions (document order)
	void apply_page_range(node * a, std::int32_t a_cp, node * b, std::int32_t b_cp) {
		clear_page_selection();
		if (a == nullptr || b == nullptr) { return; }
		if (a == b) {
			a->sel_from = a_cp < b_cp ? a_cp : b_cp;
			a->sel_to = a_cp < b_cp ? b_cp : a_cp;
			a->selected = a->sel_to > a->sel_from;
			return;
		}
		// document order decides which endpoint comes first
		bool in_range = false, a_first = false;
		order_probe(*doc.root, a, b, in_range, a_first);
		node * first = a_first ? a : b;
		node * last = a_first ? b : a;
		const std::int32_t first_cp = a_first ? a_cp : b_cp;
		const std::int32_t last_cp = a_first ? b_cp : a_cp;
		bool marking = false;
		mark_range(*doc.root, first, last, first_cp, last_cp, marking);
	}
	// pre-order walk: which of a/b appears first?
	static bool order_probe(node & n, node * a, node * b, bool & done, bool & a_first) {
		if (done) { return true; }
		if (&n == a) {
			a_first = true;
			done = true;
			return true;
		}
		if (&n == b) {
			a_first = false;
			done = true;
			return true;
		}
		for (const auto & c : n.children) {
			if (order_probe(*c, a, b, done, a_first)) { return true; }
		}
		return false;
	}
	void mark_range(node & n, node * first, node * last, std::int32_t first_cp,
	                std::int32_t last_cp, bool & marking) {
		const bool is_first = &n == first, is_last = &n == last;
		if (is_first) { marking = true; }
		if (marking && selectable_text(&n)) {
			const std::int32_t len = n.ui_lines.empty() ? 0 : n.ui_lines.back().cp_end;
			n.sel_from = is_first ? first_cp : 0;
			n.sel_to = is_last ? last_cp : len;
			n.selected = n.sel_to > n.sel_from;
		}
		if (is_last) {
			marking = false;
			return;
		}
		for (const auto & c : n.children) { mark_range(*c, first, last, first_cp, last_cp, marking); }
		if (is_last) { marking = false; }
	}
	[[nodiscard]] std::string page_selection_text() const {
		std::string out;
		if (doc.root) { collect_selected(*doc.root, out); }
		while (!out.empty() && out.back() == '\n') { out.pop_back(); }
		return out;
	}
	static void collect_selected(const node & n, std::string & out) {
		if (n.selected && n.sel_to > n.sel_from) {
			const std::u32string all = utf8_to_utf32(n.text);
			const std::size_t b = static_cast<std::size_t>(n.sel_from);
			const std::size_t e2 = static_cast<std::size_t>(n.sel_to) < all.size()
			                           ? static_cast<std::size_t>(n.sel_to)
			                           : all.size();
			if (e2 > b) {
				out += utf32_to_utf8(std::u32string_view{all}.substr(b, e2 - b));
				out += '\n';
			}
		}
		for (const auto & c : n.children) { collect_selected(*c, out); }
	}

	// the clipboard command defaults (Ctrl+C/X/V/A and the context menu);
	// each dispatches its cancelable DOM event first - overridable
	[[nodiscard]] bool can_copy() const {
		return (focused_ != nullptr && focused_->has_selection()) || !page_selection_text().empty();
	}
	void do_copy() {
		ctjs::value evt = detail::simple_event("copy");
		ev.dispatch("copy", evt);
		if (detail::event_flag(evt, "defaultPrevented")) { return; }
		std::string text;
		if (focused_ != nullptr && focused_->has_selection()) {
			text = focused_->value.substr(static_cast<std::size_t>(focused_->sel_begin()),
			                              static_cast<std::size_t>(focused_->sel_end() - focused_->sel_begin()));
		} else {
			text = page_selection_text();
		}
		if (!text.empty() && clipboard_set) { clipboard_set(text); }
	}
	void do_cut() {
		if (focused_ == nullptr || !focused_->is_editable() || !focused_->has_selection()) { return; }
		ctjs::value evt = detail::simple_event("cut");
		ev.dispatch("cut", evt);
		if (detail::event_flag(evt, "defaultPrevented")) { return; }
		if (clipboard_set) {
			clipboard_set(focused_->value.substr(
			    static_cast<std::size_t>(focused_->sel_begin()),
			    static_cast<std::size_t>(focused_->sel_end() - focused_->sel_begin())));
		}
		erase_selection(focused_);
		focused_->caret_follow = true;
		fire_input(focused_);
	}
	void do_paste() {
		if (focused_ == nullptr || !focused_->is_editable() || focused_->is_disabled()) { return; }
		ctjs::value evt = detail::simple_event("paste");
		ev.dispatch("paste", evt);
		if (detail::event_flag(evt, "defaultPrevented")) { return; }
		if (!clipboard_get) { return; }
		const std::string text = clipboard_get();
		if (text.empty()) { return; }
		erase_selection(focused_);
		focused_->value.insert(static_cast<std::size_t>(focused_->caret), text);
		focused_->caret += static_cast<std::int32_t>(text.size());
		focused_->value_dirty = true;
		focused_->caret_follow = true;
		fire_input(focused_);
	}
	void do_select_all() {
		if (focused_ != nullptr && focused_->is_editable()) {
			focused_->sel_anchor = 0;
			focused_->caret = static_cast<std::int32_t>(focused_->value.size());
			focused_->caret_follow = true;
			return;
		}
		if (doc.root) { select_all_text(*doc.root); }
	}
	void select_all_text(node & n) {
		if (selectable_text(&n)) {
			n.sel_from = 0;
			n.sel_to = n.ui_lines.back().cp_end;
			n.selected = n.sel_to > 0;
		}
		for (const auto & c : n.children) { select_all_text(*c); }
	}

	// --- the right-click context menu (Chrome-style; the cancelable
	// "contextmenu" event suppresses it, like a real browser) -----------
	// A row carries what choosing it DOES, so the paint order, the hit test
	// and the action can never disagree - they used to be tied together by
	// a switch on the row index.
	struct menu_item {
		std::string_view label;
		bool enabled;
		void (engine::*action)();
	};
	[[nodiscard]] std::vector<menu_item> menu_items() const {
		const bool editable = focused_ != nullptr && focused_->is_editable() && !focused_->is_disabled();
		return {
		    {"Copy", can_copy(), &engine::do_copy},
		    {"Cut", editable && focused_->has_selection(), &engine::do_cut},
		    {"Paste", editable, &engine::do_paste},
		    {"Select All", true, &engine::do_select_all},
		};
	}
	static constexpr std::int32_t menu_w = 160;
	static constexpr std::int32_t menu_item_h = 24;
	static constexpr std::int32_t menu_pad_x = 10;   // label inset from the left edge
	static constexpr std::int32_t menu_pad_y = 5;    // label inset within its row
	static constexpr std::int32_t menu_font_px = 13; // menu chrome is its own size
	static constexpr std::string_view menu_font_family = "sans-serif";
	void context_click(double x, double y) {
		ctjs::value evt = detail::mouse_event(x, y, "contextmenu");
		ev.dispatch("contextmenu", evt);
		if (detail::event_flag(evt, "defaultPrevented")) { return; } // page took over
		menu_target_ = hit_test_at(x, y);
		// right-clicking an editable focuses it (so Paste knows its target)
		for (node * n = menu_target_; n != nullptr; n = n->parent) {
			if (n->is_editable() && !n->is_disabled()) {
				set_focus(n);
				break;
			}
		}
		menu_open_ = true;
		menu_x_ = static_cast<std::int32_t>(x);
		menu_y_ = static_cast<std::int32_t>(y);
		menu_hover_ = -1;
	}
	void menu_click(double x, double y) {
		const std::int32_t ix = static_cast<std::int32_t>(x), iy = static_cast<std::int32_t>(y);
		menu_open_ = false;
		const auto items = menu_items();
		if (ix < menu_x_ || ix >= menu_x_ + menu_w) { return; } // dismissed
		const std::int32_t idx = (iy - menu_y_) / menu_item_h;
		if (idx < 0 || idx >= static_cast<std::int32_t>(items.size())) { return; }
		const menu_item & item = items[static_cast<std::size_t>(idx)];
		if (!item.enabled) { return; }
		(this->*item.action)();
	}

	// --- text editing (the keydown default action) ---------------------
	bool ctrl_down() const {
		return keys_down.contains("Left Ctrl") || keys_down.contains("Right Ctrl");
	}
	bool shift_down() const {
		return keys_down.contains("Left Shift") || keys_down.contains("Right Shift");
	}
	void edit_key(std::string_view name) {
		node * f = focused_;
		if (f == nullptr || !f->is_editable() || f->is_disabled()) { return; }
		f->caret_follow = true; // layout scrolls the caret into view
		caret_base_ms_ = ev.now_ms; // caret activity restarts the blink
		// caret motion: Shift extends the selection, plain motion drops it;
		// an edit replaces the selection first
		const bool motion = name == "Left" || name == "Right" || name == "Home" ||
		                    name == "End" || name == "Up" || name == "Down";
		if (motion) {
			if (shift_down()) {
				if (f->sel_anchor < 0) { f->sel_anchor = f->caret; }
			} else {
				f->sel_anchor = -1;
			}
		} else if (f->has_selection() &&
		           (name == "Backspace" || name == "Delete" || name == "Return")) {
			erase_selection(f);
			fire_input(f);
			if (name != "Return") { return; } // the erase WAS the edit
		}
		std::string & v = f->value;
		std::int32_t & c = f->caret;
		if (c > static_cast<std::int32_t>(v.size())) { c = static_cast<std::int32_t>(v.size()); }
		if (name == "Backspace") {
			const std::int32_t n = detail::cp_len_before(v, c);
			if (n > 0) {
				v.erase(static_cast<std::size_t>(c - n), static_cast<std::size_t>(n));
				c -= n;
				f->value_dirty = true;
				fire_input(f);
			}
		} else if (name == "Delete") {
			const std::int32_t n = detail::cp_len_at(v, c);
			if (n > 0) {
				v.erase(static_cast<std::size_t>(c), static_cast<std::size_t>(n));
				f->value_dirty = true;
				fire_input(f);
			}
		} else if (name == "Left") {
			c -= detail::cp_len_before(v, c);
		} else if (name == "Right") {
			c += detail::cp_len_at(v, c);
		} else if (name == "Home") {
			if (f->is_textarea()) {
				const auto lines = detail::visual_lines(*f);
				const std::int32_t li = detail::caret_visual_line(lines, detail::cp_of_byte(v, c));
				c = detail::byte_of_cp(v, lines[static_cast<std::size_t>(li)].cp_start);
			} else {
				c = 0;
			}
		} else if (name == "End") {
			if (f->is_textarea()) {
				const auto lines = detail::visual_lines(*f);
				const std::int32_t li = detail::caret_visual_line(lines, detail::cp_of_byte(v, c));
				c = detail::byte_of_cp(v, lines[static_cast<std::size_t>(li)].cp_end);
			} else {
				c = static_cast<std::int32_t>(v.size());
			}
		} else if ((name == "Up" || name == "Down") && f->is_textarea()) {
			// move between VISUAL lines, preserving the column
			const auto lines = detail::visual_lines(*f);
			const std::int32_t cc = detail::cp_of_byte(v, c);
			const std::int32_t li = detail::caret_visual_line(lines, cc);
			const std::int32_t ti = name == "Up" ? li - 1 : li + 1;
			if (ti >= 0 && ti < static_cast<std::int32_t>(lines.size())) {
				const node::text_line & cur = lines[static_cast<std::size_t>(li)];
				const node::text_line & tgt = lines[static_cast<std::size_t>(ti)];
				const std::int32_t col = cc - cur.cp_start;
				const std::int32_t tlen = tgt.cp_end - tgt.cp_start;
				c = detail::byte_of_cp(v, tgt.cp_start + (col < tlen ? col : tlen));
			}
		} else if (name == "Return") {
			if (f->is_textarea()) {
				v.insert(static_cast<std::size_t>(c), 1, '\n');
				++c;
				f->value_dirty = true;
				fire_input(f);
			} else if (node * form = f->form_ancestor()) {
				// implicit form submission (Enter in a text input)
				submit_form(form);
			}
		}
	}
	void fire_input(node * n) {
		dispatch_to(ev.input_listeners, n, detail::simple_event("input"));
	}

	// --- form submission / reset ---------------------------------------
	void submit_form(node * form) {
		if (form == nullptr) { return; }
		ctjs::value evt = detail::simple_event("submit");
		dispatch_to(ev.submit_listeners, form, evt);
		if (const auto it = ev.onsubmit_handlers.find(form); it != ev.onsubmit_handlers.end()) {
			ev.invoke(it->second, {evt});
		}
		if (!form->id.empty()) { deliver(script, "onSubmit", form->id); }
		// no default beyond the events - a single-page engine does not
		// navigate on submit
	}
	void reset_form(node * form) {
		if (form != nullptr) { detail::reset_controls(*form); }
	}
	// <label for=id> targets that control; a wrapping label targets its
	// first checkbox/radio descendant
	[[nodiscard]] node * label_target(node * lab) {
		const std::string_view forid = lab->attribute("for");
		if (!forid.empty()) { return doc.root ? doc.root->find_by_id(forid) : nullptr; }
		return first_toggle(lab);
	}
	[[nodiscard]] static node * first_toggle(node * n) {
		for (const auto & c : n->children) {
			if (c->is_checkbox() || c->is_radio()) { return c.get(); }
			if (node * hit = first_toggle(c.get())) { return hit; }
		}
		return nullptr;
	}

	std::vector<ctjs::binding> all_bindings(std::vector<ctjs::binding> extra) {
		std::vector<ctjs::binding> out = dom_bindings(doc, title, images, ev);
		out.push_back({"isKeyDown",
		               ctjs::native([this](const std::vector<ctjs::value> & a) -> ctjs::value {
			               return ctjs::value{!a.empty() &&
			                                  keys_down.contains(a[0].to_string())};
		               },
		               "isKeyDown")});
		out.push_back({"mouseX", ctjs::native([this](const std::vector<ctjs::value> &) {
			               return ctjs::value{mouse_x};
		               },
		               "mouseX")});
		out.push_back({"mouseY", ctjs::native([this](const std::vector<ctjs::value> &) {
			               return ctjs::value{mouse_y};
		               },
		               "mouseY")});
		out.push_back({"isMouseDown", ctjs::native([this](const std::vector<ctjs::value> &) {
			               return ctjs::value{mouse_down};
		               },
		               "isMouseDown")});
		// core BabylonJS API, backed by a software 3D rasterizer into <canvas>
		babylon::install(out, ev, images);
		for (ctjs::binding & b : extra) { out.push_back(std::move(b)); }
		return out;
	}
};

} // namespace ctbrowser

#endif
