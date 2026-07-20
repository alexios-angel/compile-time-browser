#ifndef CTBROWSER__ENGINE__HPP
#define CTBROWSER__ENGINE__HPP

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
	// the page stylesheet, parsed BY VALUE from the page's <style> text at
	// construction (linear ctcss::parse_value, not the Earley TYPE path);
	// `resolve` closes over it. Declared before resolve so it is live first.
	ctcss::value_sheet css_sheet;
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
	      resolve([this](const ctcss::element_ref * chain, size_t n, std::string_view prop) {
		      return ctcss::query(css_sheet, chain, n, prop);
	      }),
	      extra_(extra),
	      script(ctjs::run_value(Page::script_text(), all_bindings(std::move(extra)))) {
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
	std::vector<paint_cmd> frame(int viewport_w) {
		// advance CSS @keyframes (writes interpolated inline styles) before layout
		detail::apply_animations(doc, css_sheet, ev.now_ms);
		std::vector<paint_cmd> cmds = ctbrowser::layout(doc, viewport_w, resolve, measure, ev.viewport_h);
		ev.viewport_w = viewport_w;
		ev.refresh_tracked();
		return cmds;
	}

	// the page's @font-face rules (family + src), for the shell to load custom
	// fonts; empty when the stylesheet declares none
	const std::vector<ctcss::value_sheet::font_face> & font_faces() const noexcept {
		return css_sheet.font_faces;
	}

	// keep the viewport dimensions current; on a change, refresh
	// window.innerWidth/innerHeight and dispatch a DOM "resize" event so
	// scripts (e.g. BabylonJS's engine.resize()) can react
	void resize_viewport(int w, int h) {
		if (ev.viewport_w == w && ev.viewport_h == h) { return; }
		ev.viewport_w = w;
		ev.viewport_h = h;
		ev.refresh_tracked();
		ev.dispatch("resize", ctjs::value{});
	}

	// --- event delivery (missing handlers are quietly skipped)

	void click_at(int x, int y) {
		if (!doc.root) { return; }
		node * hit = doc.root->hit_test(x, y);
		// fire addEventListener('click', ...) on the hit target and its ancestors
		// (bubbling), as the DOM would - copy each list, handlers may mutate it
		for (node * n = hit; n != nullptr; n = n->parent) {
			const auto it = ev.click_listeners.find(n);
			if (it == ev.click_listeners.end()) { continue; }
			const std::vector<ctjs::value> fns = it->second;
			for (const ctjs::value & fn : fns) {
				ev.invoke(fn, {detail::mouse_event(static_cast<double>(x), static_cast<double>(y))});
			}
		}
		// legacy onClick(id): nearest ancestor with a non-empty id
		node * idn = hit;
		while (idn != nullptr && idn->id.empty()) { idn = idn->parent; }
		if (idn != nullptr) { deliver(script, "onClick", idn->id); }
	}
	void key(std::string_view name, bool down) {
		if (down) {
			keys_down.emplace(name);
		} else if (const auto it = keys_down.find(name); it != keys_down.end()) {
			keys_down.erase(it);
		}
		deliver(script, "onKey", name, down);
		ev.dispatch(down ? "keydown" : "keyup", detail::key_event(name));
	}
	void mouse_move(double x, double y) {
		mouse_x = x;
		mouse_y = y;
		deliver(script, "onMouseMove", x, y);
		ev.dispatch("mousemove", detail::mouse_event(x, y));
	}
	void mouse_button(double x, double y, bool down) {
		mouse_x = x;
		mouse_y = y;
		mouse_down = down;
		if (down) {
			deliver(script, "onMouseDown", x, y);
			ev.dispatch("mousedown", detail::mouse_event(x, y));
			// a <select> widget (open popup, or a collapsed control) eats the click
			if (!handle_select_click(static_cast<int>(x), static_cast<int>(y))) {
				click_at(static_cast<int>(x), static_cast<int>(y));
			}
		} else {
			ev.dispatch("mouseup", detail::mouse_event(x, y));
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
	}

	// document.location.reload(): fresh DOM, fresh script run
	void do_reload() {
		ev.reset();
		open_select_ = nullptr; // node pointers are about to be invalidated
		doc = instantiate_html(Page::html_text());
		script = ctjs::run_value(Page::script_text(), all_bindings(extra_));
	}

private:
	// Route a mouse press through a <select>: if a popup is open, a click on an
	// option row selects it (and fires the element's onchange), a click elsewhere
	// closes it; if none is open, a click on a collapsed control opens it. Returns
	// true when the widget consumed the click (so it does not also become onClick).
	bool handle_select_click(int x, int y) {
		if (open_select_ != nullptr) {
			node * sel = open_select_;
			int idx = 0;
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
