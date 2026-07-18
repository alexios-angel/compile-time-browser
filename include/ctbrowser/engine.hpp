#ifndef CTBROWSER__ENGINE__HPP
#define CTBROWSER__ENGINE__HPP

#include "page.hpp"
#include "dom.hpp"
#include "layout.hpp"
#include "script.hpp"

// The assembled runtime, WITHOUT any windowing: instantiate the DOM,
// bind the style resolver to the page's compile-time stylesheet, run
// the page's script with the DOM API - everything the SDL shell needs,
// and everything the headless tests exercise.

namespace ctbrowser {

template <typename Page> class engine {
public:
	document doc;
	std::string title;
	style_fn resolve;
	ctjs::run_result script;

	engine()
	    : doc(instantiate<typename Page::doc_type>()),
	      title(Page::title()),
	      resolve([](const ctcss::element_ref * chain, size_t n, std::string_view prop) {
		      return ctcss::query(typename Page::sheet_type{}, chain, n, prop);
	      }),
	      script(Page::script_type::run(dom_bindings(doc, title))) { }

	engine(const engine &) = delete;
	engine & operator=(const engine &) = delete;

	// one layout pass for a viewport width; updates node rects too
	std::vector<paint_cmd> frame(int viewport_w) {
		return ctbrowser::layout(doc, viewport_w, resolve);
	}

	// event delivery (missing handlers are quietly skipped)
	void click_at(int x, int y) {
		if (!doc.root) { return; }
		node * hit = doc.root->hit_test(x, y);
		while (hit != nullptr && hit->id.empty()) { hit = hit->parent; }
		if (hit != nullptr) { deliver(script, "onClick", hit->id); }
	}
	void key(std::string_view name, bool down) {
		deliver(script, "onKey", name, down);
	}
	void tick(double dt) {
		deliver(script, "onFrame", dt);
	}
};

} // namespace ctbrowser

#endif
