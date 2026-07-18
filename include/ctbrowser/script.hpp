#ifndef CTBROWSER__SCRIPT__HPP
#define CTBROWSER__SCRIPT__HPP

#include "dom.hpp"
#include <ctjs.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <memory>
#include <string>
#include <vector>
#endif

// The DOM API scripts see, delivered through ctjs host bindings.
// Element handles are JS objects whose methods are native closures
// capturing the node pointer; a canvas context is a JS object whose
// fillStyle property the script assigns and whose fillRect/putPixel
// natives read it back - the real canvas idiom, no special cases in
// the interpreter.
//
//   let el = getElementById("status");
//   el.setText("ready");  el.toggleClass("ok");  el.style("color", "red");
//
//   let ctx = getContext("game");
//   ctx.fillStyle = "#ff8800";
//   ctx.fillRect(10, 10, 50, 30);
//
// Events arrive by convention: the shell calls the script's global
// functions onClick(id), onKey(key, down) and onFrame(dt) when they
// exist.

namespace ctbrowser {

namespace detail {

inline uint32_t css_to_argb(std::string_view spec, uint32_t fallback) {
	const ctcss::color c = ctcss::parse_color(spec);
	if (!c.ok) { return fallback; }
	return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
	       (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

inline ctjs::value element_handle(node * n) {
	ctjs::object_t o;
	o.set("id", ctjs::value{n->id});
	o.set("tag", ctjs::value{n->tag});
	o.set("text", ctjs::value::function(
	                  [n](ctjs::context &, const std::vector<ctjs::value> &) {
		                  return ctjs::value{n->text};
	                  },
	                  "text"));
	o.set("setText", ctjs::value::function(
	                     [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		                     n->text = a.empty() ? std::string{} : a[0].to_string();
		                     return ctjs::value{};
	                     },
	                     "setText"));
	o.set("addClass", ctjs::value::function(
	                      [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		                      if (!a.empty()) { n->add_class(a[0].to_string()); }
		                      return ctjs::value{};
	                      },
	                      "addClass"));
	o.set("removeClass", ctjs::value::function(
	                         [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		                         if (!a.empty()) { n->remove_class(a[0].to_string()); }
		                         return ctjs::value{};
	                         },
	                         "removeClass"));
	o.set("toggleClass", ctjs::value::function(
	                         [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		                         if (!a.empty()) { n->toggle_class(a[0].to_string()); }
		                         return ctjs::value{};
	                         },
	                         "toggleClass"));
	o.set("hasClass", ctjs::value::function(
	                      [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		                      return ctjs::value{!a.empty() && n->has_class(a[0].to_string())};
	                      },
	                      "hasClass"));
	o.set("style", ctjs::value::function(
	                   [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		                   if (a.size() >= 2) {
			                   n->inline_style[a[0].to_string()] = a[1].to_string();
		                   }
		                   return ctjs::value{};
	                   },
	                   "style"));
	o.set("attr", ctjs::value::function(
	                  [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		                  if (a.empty()) { return ctjs::value{}; }
		                  return ctjs::value{std::string{n->attribute(a[0].to_string())}};
	                  },
	                  "attr"));
	return ctjs::value::object(std::move(o));
}

inline ctjs::value canvas_context(node * n) {
	auto ctx = std::make_shared<ctjs::object_t>();
	ctx->set("fillStyle", ctjs::value{"#000000"});
	ctx->set("width", ctjs::value{n->canvas_w});
	ctx->set("height", ctjs::value{n->canvas_h});
	const auto style_of = [ctx]() -> uint32_t {
		if (const ctjs::value * v = ctx->find("fillStyle")) {
			return css_to_argb(v->to_string(), 0xFF000000u);
		}
		return 0xFF000000u;
	};
	const auto fill = [n](int x, int y, int w, int h, uint32_t argb) {
		if (n->pixels.empty()) { return; }
		const int x0 = x < 0 ? 0 : x;
		const int y0 = y < 0 ? 0 : y;
		const int x1 = x + w > n->canvas_w ? n->canvas_w : x + w;
		const int y1 = y + h > n->canvas_h ? n->canvas_h : y + h;
		for (int py = y0; py < y1; ++py) {
			for (int px = x0; px < x1; ++px) {
				n->pixels[static_cast<size_t>(py) * static_cast<size_t>(n->canvas_w) +
				          static_cast<size_t>(px)] = argb;
			}
		}
	};
	ctx->set("fillRect", ctjs::value::function(
	                         [style_of, fill](ctjs::context &, const std::vector<ctjs::value> & a) {
		                         if (a.size() >= 4) {
			                         fill(static_cast<int>(a[0].to_number()),
			                              static_cast<int>(a[1].to_number()),
			                              static_cast<int>(a[2].to_number()),
			                              static_cast<int>(a[3].to_number()), style_of());
		                         }
		                         return ctjs::value{};
	                         },
	                         "fillRect"));
	ctx->set("clear", ctjs::value::function(
	                      [n, style_of](ctjs::context &, const std::vector<ctjs::value> &) {
		                      for (uint32_t & p : n->pixels) { p = style_of(); }
		                      return ctjs::value{};
	                      },
	                      "clear"));
	ctx->set("putPixel", ctjs::value::function(
	                         [n, style_of](ctjs::context &, const std::vector<ctjs::value> & a) {
		                         if (a.size() >= 2) {
			                         const int x = static_cast<int>(a[0].to_number());
			                         const int y = static_cast<int>(a[1].to_number());
			                         if (x >= 0 && y >= 0 && x < n->canvas_w && y < n->canvas_h) {
				                         n->pixels[static_cast<size_t>(y) *
				                                       static_cast<size_t>(n->canvas_w) +
				                                   static_cast<size_t>(x)] = style_of();
			                         }
		                         }
		                         return ctjs::value{};
	                         },
	                         "putPixel"));
	return ctjs::value{ctx};
}

} // namespace detail

// the host bindings a page's script runs with; `title` receives
// document.setTitle updates (the shell mirrors it into the window)
inline std::vector<ctjs::binding> dom_bindings(document & doc, std::string & title) {
	std::vector<ctjs::binding> out;
	out.push_back({"getElementById",
	               ctjs::native([&doc](const std::vector<ctjs::value> & a) -> ctjs::value {
		               if (a.empty()) { return {}; }
		               node * n = doc.by_id(a[0].to_string());
		               return n ? detail::element_handle(n) : ctjs::value{};
	               },
	               "getElementById")});
	out.push_back({"getContext",
	               ctjs::native([&doc](const std::vector<ctjs::value> & a) -> ctjs::value {
		               if (a.empty()) { return {}; }
		               node * n = doc.by_id(a[0].to_string());
		               return n != nullptr && n->is_canvas() ? detail::canvas_context(n)
		                                                     : ctjs::value{};
	               },
	               "getContext")});
	out.push_back({"setTitle",
	               ctjs::native([&title](const std::vector<ctjs::value> & a) -> ctjs::value {
		               if (!a.empty()) { title = a[0].to_string(); }
		               return {};
	               },
	               "setTitle")});
	return out;
}

// call a script-defined handler if it exists (missing = quietly skipped)
template <typename... Args>
inline void deliver(ctjs::run_result & script, std::string_view fn, Args &&... args) {
	if (!script[fn].is_function()) { return; }
	(void)script.call(fn, std::forward<Args>(args)...);
}

} // namespace ctbrowser

#endif
