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
	node * open_select_ = nullptr; // the <select> whose popup is currently open
	node * hovered_ = nullptr;     // the pointer's current hit target
	node * pressed_ = nullptr;     // mouse-down target (:active chain, click pairing)
	node * focused_ = nullptr;     // the focused control (:focus)
	bool click_suppressed_ = false; // set when the select popup ate the mousedown
	std::int32_t scroll_y_ = 0;     // page scroll offset (px; clamped each frame)
	std::int32_t page_h_ = 0;       // last laid-out document height
	// the anchor default action: how <a href> opens the OS web browser.
	// Headless (tests) leave it empty = no-op; the SDL shell installs
	// SDL_OpenURL. Fragment hrefs (#...) never call it.
	ctc::cfunction<void(std::string_view)> open_url;
	std::int32_t gc_ticks_ = 0;             // frames since the last cycle collection (see tick)
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
		std::vector<paint_cmd> cmds = ctbrowser::layout(doc, viewport_w, resolve, measure, ev.viewport_h);
		ev.viewport_w = viewport_w;
		// page scrolling: clamp to the freshly laid-out document height,
		// then shift the world into view - rects AND paints together, so
		// hit testing, script rect readbacks and rendering all agree.
		// position:fixed subtrees stay viewport-anchored.
		page_h_ = doc.root != nullptr ? doc.root->y + doc.root->h : 0;
		const std::int32_t max_scroll = page_h_ > ev.viewport_h ? page_h_ - ev.viewport_h : 0;
		if (scroll_y_ > max_scroll) { scroll_y_ = max_scroll; }
		if (scroll_y_ < 0) { scroll_y_ = 0; }
		if (scroll_y_ != 0) {
			for (paint_cmd & c : cmds) {
				if (!c.fixed) { c.y -= scroll_y_; }
			}
			if (doc.root) { detail::offset_rects(*doc.root, -scroll_y_); }
		}
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
		node * hit = doc.root ? doc.root->hit_test(static_cast<std::int32_t>(x),
		                                           static_cast<std::int32_t>(y))
		                      : nullptr;
		for (node * n = hit; n != nullptr; n = n->parent) {
			if (n->is_textarea()) {
				n->scroll_top -= static_cast<std::int32_t>(dy * 48.0); // clamped by layout
				return;
			}
		}
		scroll_y_ -= static_cast<std::int32_t>(dy * 48.0); // clamped in frame()
	}
	std::int32_t scroll_y() const { return scroll_y_; }

	// the page's @font-face rules (family + src), for the shell to load custom
	// fonts; empty when the stylesheet declares none
	const std::vector<ctcss::value_sheet::font_face> & font_faces() const noexcept {
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
		if (down && !detail::event_flag(evt, "defaultPrevented")) {
			if (focused_ != nullptr && focused_->is_editable() && !focused_->is_disabled()) {
				edit_key(name);
			} else { // page scrolling (clamped in frame())
				if (name == "PageDown") { scroll_y_ += ev.viewport_h > 48 ? ev.viewport_h - 48 : 48; }
				else if (name == "PageUp") { scroll_y_ -= ev.viewport_h > 48 ? ev.viewport_h - 48 : 48; }
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
		f->value.insert(static_cast<std::size_t>(f->caret), utf8);
		f->caret += static_cast<std::int32_t>(utf8.size());
		f->value_dirty = true;
		f->caret_follow = true;
		fire_input(f);
	}
	void mouse_move(double x, double y) {
		mouse_x = x;
		mouse_y = y;
		update_hover(x, y);
		deliver(script, "onMouseMove", x, y);
		ev.dispatch("mousemove", detail::mouse_event(x, y));
	}
	// hover tracking: re-flag the ancestor chain when the hit target
	// changes; the per-frame style re-query restyles :hover for free.
	// Button events sync it too - the pointer is wherever it clicked.
	void update_hover(double x, double y) {
		node * hit = doc.root ? doc.root->hit_test(static_cast<std::int32_t>(x),
		                                           static_cast<std::int32_t>(y))
		                      : nullptr;
		if (hit != hovered_) {
			set_chain_flag(hovered_, &node::hovered, false);
			set_chain_flag(hit, &node::hovered, true);
			if (hovered_ != nullptr) { ev.dispatch("mouseout", detail::mouse_event(x, y, "mouseout")); }
			if (hit != nullptr) { ev.dispatch("mouseover", detail::mouse_event(x, y, "mouseover")); }
			hovered_ = hit;
		}
	}
	void mouse_button(double x, double y, bool down) {
		mouse_x = x;
		mouse_y = y;
		mouse_down = down;
		update_hover(x, y);
		if (down) {
			deliver(script, "onMouseDown", x, y);
			ev.dispatch("mousedown", detail::mouse_event(x, y, "mousedown"));
			pressed_ = doc.root ? doc.root->hit_test(static_cast<std::int32_t>(x),
			                                          static_cast<std::int32_t>(y))
			                    : nullptr;
			set_chain_flag(pressed_, &node::pressed, true); // :active while held
			// browsers move focus on mousedown
			node * f = pressed_;
			while (f != nullptr && !f->is_focusable()) { f = f->parent; }
			set_focus(f);
			// a <select> widget (open popup, or a collapsed control) eats the
			// press - and the synthetic click that would pair with the release
			click_suppressed_ =
			    handle_select_click(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
		} else {
			ev.dispatch("mouseup", detail::mouse_event(x, y, "mouseup"));
			set_chain_flag(pressed_, &node::pressed, false);
			// the click fires on RELEASE (browser semantics): its target is
			// the nearest common ancestor of the press and release targets
			node * up_hit = doc.root ? doc.root->hit_test(static_cast<std::int32_t>(x),
			                                               static_cast<std::int32_t>(y))
			                         : nullptr;
			if (!click_suppressed_ && pressed_ != nullptr && up_hit != nullptr) {
				if (node * target = common_ancestor(pressed_, up_hit)) {
					click_on(target, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
				}
			}
			pressed_ = nullptr;
			click_suppressed_ = false;
		}
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
		if (++gc_ticks_ >= 60) {
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
		doc = instantiate_html(Page::html_text());
		script = ctjs::run_value(Page::script_text(), all_bindings(extra_));
		ctjs::gc::collect(); // reap the old page's cycles now that its roots are gone
	}

private:
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
	void fire_change(node * sel) {
		const auto it = ev.change_handlers.find(sel);
		if (it != ev.change_handlers.end() && it->second.is_function()) {
			ev.invoke(it->second, {});
		}
		if (const auto lit = ev.change_listeners.find(sel); lit != ev.change_listeners.end()) {
			const std::vector<ctjs::value> fns = lit->second;
			ctjs::value evt = detail::simple_event("change");
			for (const ctjs::value & fn : fns) { ev.invoke(fn, {evt}); }
		}
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
			// leaving an edited control commits it: change fires on blur
			if (focused_->is_editable() && focused_->value_dirty) {
				focused_->value_dirty = false;
				fire_change(focused_);
			}
		}
		if (n != nullptr) { n->focused = true; }
		focused_ = n;
	}
	static node * common_ancestor(node * a, node * b) {
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

	// --- text editing (the keydown default action) ---------------------
	static bool is_utf8_cont(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
	static std::int32_t cp_len_before(std::string_view s, std::int32_t pos) {
		std::int32_t n = 0;
		while (pos - n > 0) {
			++n;
			if (!is_utf8_cont(s[static_cast<std::size_t>(pos - n)])) { break; }
		}
		return n;
	}
	static std::int32_t cp_len_at(std::string_view s, std::int32_t pos) {
		if (pos >= static_cast<std::int32_t>(s.size())) { return 0; }
		std::int32_t n = 1;
		while (pos + n < static_cast<std::int32_t>(s.size()) &&
		       is_utf8_cont(s[static_cast<std::size_t>(pos + n)])) {
			++n;
		}
		return n;
	}
	void edit_key(std::string_view name) {
		node * f = focused_;
		if (f == nullptr || !f->is_editable() || f->is_disabled()) { return; }
		f->caret_follow = true; // layout scrolls the caret into view
		std::string & v = f->value;
		std::int32_t & c = f->caret;
		if (c > static_cast<std::int32_t>(v.size())) { c = static_cast<std::int32_t>(v.size()); }
		const auto line_start = [&v](std::int32_t from) {
			std::int32_t i = from;
			while (i > 0 && v[static_cast<std::size_t>(i - 1)] != '\n') { --i; }
			return i;
		};
		const auto line_end = [&v](std::int32_t from) {
			std::int32_t i = from;
			while (i < static_cast<std::int32_t>(v.size()) && v[static_cast<std::size_t>(i)] != '\n') { ++i; }
			return i;
		};
		if (name == "Backspace") {
			const std::int32_t n = cp_len_before(v, c);
			if (n > 0) {
				v.erase(static_cast<std::size_t>(c - n), static_cast<std::size_t>(n));
				c -= n;
				f->value_dirty = true;
				fire_input(f);
			}
		} else if (name == "Delete") {
			const std::int32_t n = cp_len_at(v, c);
			if (n > 0) {
				v.erase(static_cast<std::size_t>(c), static_cast<std::size_t>(n));
				f->value_dirty = true;
				fire_input(f);
			}
		} else if (name == "Left") {
			c -= cp_len_before(v, c);
		} else if (name == "Right") {
			c += cp_len_at(v, c);
		} else if (name == "Home") {
			c = f->is_textarea() ? line_start(c) : 0;
		} else if (name == "End") {
			c = f->is_textarea() ? line_end(c) : static_cast<std::int32_t>(v.size());
		} else if (name == "Up" && f->is_textarea()) {
			const std::int32_t ls = line_start(c), col = c - ls;
			if (ls > 0) {
				const std::int32_t pls = line_start(ls - 1);
				c = pls + (col < ls - 1 - pls ? col : ls - 1 - pls);
			}
		} else if (name == "Down" && f->is_textarea()) {
			const std::int32_t le = line_end(c), col = c - line_start(c);
			if (le < static_cast<std::int32_t>(v.size())) {
				const std::int32_t nls = le + 1, nle = line_end(nls);
				c = nls + (col < nle - nls ? col : nle - nls);
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
		if (const auto it = ev.input_listeners.find(n); it != ev.input_listeners.end()) {
			const std::vector<ctjs::value> fns = it->second;
			ctjs::value evt = detail::simple_event("input");
			for (const ctjs::value & fn : fns) { ev.invoke(fn, {evt}); }
		}
	}

	// --- form submission / reset ---------------------------------------
	void submit_form(node * form) {
		if (form == nullptr) { return; }
		ctjs::value evt = detail::simple_event("submit");
		if (const auto it = ev.submit_listeners.find(form); it != ev.submit_listeners.end()) {
			const std::vector<ctjs::value> fns = it->second;
			for (const ctjs::value & fn : fns) { ev.invoke(fn, {evt}); }
		}
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
	node * label_target(node * lab) {
		const std::string_view forid = lab->attribute("for");
		if (!forid.empty()) { return doc.root ? doc.root->find_by_id(forid) : nullptr; }
		return first_toggle(lab);
	}
	static node * first_toggle(node * n) {
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
