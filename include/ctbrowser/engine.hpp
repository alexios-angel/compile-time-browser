#ifndef CTBROWSER__ENGINE__HPP
#define CTBROWSER__ENGINE__HPP

#include "page.hpp"
#include "dom.hpp"
#include "image.hpp"
#include "assets.hpp"
#include "layout.hpp"
#include "script.hpp"
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
	    : doc(instantiate<typename Page::doc_type>()),
	      title(Page::title()),
	      assets(detail::merge_assets(std::move(embedded), auto_assets<Page>())),
	      images{{}, std::move(image_decoder), &assets},
	      resolve([](const ctcss::element_ref * chain, size_t n, std::string_view prop) {
		      return ctcss::query(typename Page::sheet_type{}, chain, n, prop);
	      }),
	      extra_(extra),
	      script(Page::script_type::run(all_bindings(std::move(extra)))) { }

	engine(const engine &) = delete;
	engine & operator=(const engine &) = delete;

	// one layout pass for a viewport width; updates node rects (and
	// the offsetLeft/width properties on exposed element handles) too
	std::vector<paint_cmd> frame(int viewport_w) {
		std::vector<paint_cmd> cmds = ctbrowser::layout(doc, viewport_w, resolve, measure);
		ev.viewport_w = viewport_w;
		ev.refresh_tracked();
		return cmds;
	}

	// --- event delivery (missing handlers are quietly skipped)

	void click_at(int x, int y) {
		if (!doc.root) { return; }
		node * hit = doc.root->hit_test(x, y);
		while (hit != nullptr && hit->id.empty()) { hit = hit->parent; }
		if (hit != nullptr) { deliver(script, "onClick", hit->id); }
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
			click_at(static_cast<int>(x), static_cast<int>(y));
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
		doc = instantiate<typename Page::doc_type>();
		script = Page::script_type::run(all_bindings(extra_));
	}

private:
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
		for (ctjs::binding & b : extra) { out.push_back(std::move(b)); }
		return out;
	}
};

} // namespace ctbrowser

#endif
