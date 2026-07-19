#ifndef CTBROWSER__SCRIPT__HPP
#define CTBROWSER__SCRIPT__HPP

#include "dom.hpp"
#include "image.hpp"
#include "font8x8.hpp"
#include <ctjs.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
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
//   let ctx = getContext("game");             // or canvas.getContext("2d")
//   ctx.fillStyle = "#ff8800";
//   ctx.fillRect(10, 10, 50, 30);
//   ctx.beginPath(); ctx.arc(60, 60, 10, 0, Math.PI*2); ctx.fill();
//
// Two event styles coexist, both first-class:
//  - ctbrowser conventions: the shell calls the script's globals
//    onClick(id) / onKey(key, down) / onFrame(dt) when they exist;
//  - the web platform: document.getElementById/addEventListener with
//    DOM event objects (e.code "ArrowRight", e.clientX),
//    requestAnimationFrame, alert, document.location.reload - enough
//    that an unmodified MDN-tutorial page (examples/pong.html) runs.
//
// And the network, resolved at BUILD time:
//   const response = await fetch("https://.../config.json");
//   const config = await response.json();   // also text(), bytes()
// fetch() serves from the embedded-asset registry - every fetch("...")
// literal was already fetched at compile time by assets.hpp on
// --fetch-allow builds - so the promises are settled by construction
// (which is the exact subset ctjs implements). Un-baked URLs reject
// with a TypeError, the same shape a real network failure has.

namespace ctbrowser {

// script-registered callbacks (addEventListener / requestAnimationFrame)
// plus the interpreter context needed to call them back; the context is
// captured at registration time and lives in the engine's run_result
struct dom_events {
	ctjs::context * cx = nullptr;
	document * doc = nullptr; // set by make_document; createElement et al
	std::map<std::string, std::vector<ctjs::value>, std::less<>> listeners;
	std::vector<ctjs::value> raf;
	double raf_id = 0;
	// setTimeout/setInterval: fired by the engine's tick() against the
	// same now_ms clock performance.now exposes
	struct timer_entry {
		double id;
		double due_ms;
		double interval_ms;
		bool repeat;
		ctjs::value fn;
	};
	std::vector<timer_entry> timers;
	double timer_seq = 0;
	std::vector<std::string> alerts;
	bool reload = false;
	double now_ms = 0;
	// every element handle carries "__node", an index here - how one
	// handle's native (appendChild) resolves ANOTHER handle's node
	std::vector<node *> handle_nodes;

	double track_node(node * n) {
		handle_nodes.push_back(n);
		return static_cast<double>(handle_nodes.size() - 1);
	}
	node * node_of(const ctjs::value & handle) {
		if (!handle.is_object()) { return nullptr; }
		const ctjs::value * id = handle.as_object()->find("__node");
		if (id == nullptr || !id->is_number()) { return nullptr; }
		const size_t i = static_cast<size_t>(id->as_number());
		return i < handle_nodes.size() ? handle_nodes[i] : nullptr;
	}
	// element handles whose layout-derived properties refresh per frame
	std::vector<std::pair<ctjs::rc<ctjs::object_t>, node *>> tracked;
	// the window object + the viewport the engine last laid out
	ctjs::rc<ctjs::object_t> window_obj;
	int viewport_w = 0;
	int viewport_h = 0;

	void reset() {
		cx = nullptr;
		listeners.clear();
		raf.clear();
		timers.clear();
		alerts.clear();
		reload = false;
		tracked.clear();
	}

	// fire everything due at now_ms. Handlers may add or clear timers
	// mid-flight, so each round re-scans from the start and moves the
	// fired entry out first; the round cap keeps a 0ms-retimer chain
	// from wedging the frame
	void run_timers() {
		for (int rounds = 0; rounds < 64; ++rounds) {
			bool fired = false;
			for (size_t i = 0; i < timers.size(); ++i) {
				if (timers[i].due_ms > now_ms) { continue; }
				timer_entry t = timers[i];
				if (t.repeat) {
					timers[i].due_ms = t.due_ms + (t.interval_ms > 1.0 ? t.interval_ms : 1.0);
				} else {
					timers.erase(timers.begin() + static_cast<ptrdiff_t>(i));
				}
				invoke(t.fn, {});
				fired = true;
				break;
			}
			if (!fired) { return; }
		}
	}

	void clear_timer(double id) {
		for (size_t i = 0; i < timers.size(); ++i) {
			if (timers[i].id == id) {
				timers.erase(timers.begin() + static_cast<ptrdiff_t>(i));
				return;
			}
		}
	}

	void invoke(const ctjs::value & fn, std::vector<ctjs::value> args) {
		if (cx == nullptr || !fn.is_function()) { return; }
		try {
			(void)ctjs::call_value(*cx, fn, std::move(args));
		} catch (const ctjs::js_throw & ex) {
			std::fprintf(stderr, "ctbrowser: uncaught (in listener): %s\n",
			             ctjs::error_to_string(ex.thrown).c_str());
		}
	}

	void dispatch(std::string_view type, const ctjs::value & event) {
		const auto it = listeners.find(type);
		if (it == listeners.end()) { return; }
		// copy: a handler may add/remove listeners while we iterate
		const std::vector<ctjs::value> fns = it->second;
		for (const ctjs::value & fn : fns) { invoke(fn, {event}); }
	}

	// push the live layout rects back into the exposed handles
	void refresh_tracked() {
		for (auto & [o, n] : tracked) {
			const bool canvas = n->is_canvas();
			o->set("width",
			       ctjs::value{static_cast<double>(canvas ? n->canvas_w : n->w)});
			o->set("height",
			       ctjs::value{static_cast<double>(canvas ? n->canvas_h : n->h)});
			o->set("offsetLeft", ctjs::value{static_cast<double>(n->x)});
			o->set("offsetTop", ctjs::value{static_cast<double>(n->y)});
		}
		if (window_obj) {
			window_obj->set("innerWidth", ctjs::value{viewport_w});
			window_obj->set("innerHeight", ctjs::value{viewport_h});
		}
	}
};

namespace detail {

inline uint32_t css_to_argb(std::string_view spec, uint32_t fallback) {
	const ctcss::color c = ctcss::parse_color(spec);
	if (!c.ok) { return fallback; }
	return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
	       (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

// SDL key names (what the shell feeds engine::key) -> DOM KeyboardEvent.code
inline std::string dom_key_code(std::string_view sdl) {
	if (sdl == "Left") { return "ArrowLeft"; }
	if (sdl == "Right") { return "ArrowRight"; }
	if (sdl == "Up") { return "ArrowUp"; }
	if (sdl == "Down") { return "ArrowDown"; }
	if (sdl == "Space") { return "Space"; }
	if (sdl == "Return") { return "Enter"; }
	if (sdl == "Escape") { return "Escape"; }
	if (sdl == "Backspace") { return "Backspace"; }
	if (sdl == "Tab") { return "Tab"; }
	if (sdl.size() == 1 && sdl[0] >= 'A' && sdl[0] <= 'Z') {
		return std::string{"Key"} + std::string{sdl};
	}
	if (sdl.size() == 1 && sdl[0] >= 'a' && sdl[0] <= 'z') {
		return std::string{"Key"} + static_cast<char>(sdl[0] - 'a' + 'A');
	}
	if (sdl.size() == 1 && sdl[0] >= '0' && sdl[0] <= '9') {
		return std::string{"Digit"} + std::string{sdl};
	}
	return std::string{sdl};
}

// DOM KeyboardEvent.key for the codes tutorial pages compare against
inline std::string dom_key_key(std::string_view code) {
	if (code == "Space") { return " "; }
	if (code.size() == 4 && code.starts_with("Key")) {
		return std::string{static_cast<char>(code[3] - 'A' + 'a')};
	}
	if (code.size() == 6 && code.starts_with("Digit")) {
		return std::string{code.substr(5)};
	}
	return std::string{code};
}

inline ctjs::value key_event(std::string_view sdl_name) {
	ctjs::object_t ev;
	const std::string code = dom_key_code(sdl_name);
	ev.set("key", ctjs::value{dom_key_key(code)});
	ev.set("code", ctjs::value{code});
	return ctjs::value::object(std::move(ev));
}

inline ctjs::value mouse_event(double x, double y) {
	ctjs::object_t ev;
	ev.set("clientX", ctjs::value{x});
	ev.set("clientY", ctjs::value{y});
	return ctjs::value::object(std::move(ev));
}

inline ctjs::value canvas_context(node * n, image_store * images);

inline ctjs::value element_handle(node * n, image_store * images, dom_events * ev) {
	ctjs::object_t o;
	o.set("id", ctjs::value{n->id});
	o.set("tag", ctjs::value{n->tag});
	// the live web-facing properties (refreshed after every layout)
	o.set("width", ctjs::value{static_cast<double>(n->is_canvas() ? n->canvas_w : n->w)});
	o.set("height", ctjs::value{static_cast<double>(n->is_canvas() ? n->canvas_h : n->h)});
	o.set("offsetLeft", ctjs::value{static_cast<double>(n->x)});
	o.set("offsetTop", ctjs::value{static_cast<double>(n->y)});
	o.set("__node", ctjs::value{ev->track_node(n)});
	o.set("getContext", ctjs::value::function(
	                        [n, images, ev](ctjs::context & cx, const std::vector<ctjs::value> &) {
		                        ev->cx = &cx;
		                        return n->is_canvas() ? canvas_context(n, images)
		                                              : ctjs::value{};
	                        },
	                        "getContext"));
	o.set("appendChild",
	      ctjs::value::function(
	          [n, ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		          ev->cx = &cx;
		          if (!a.empty() && ev->doc != nullptr) {
			          if (node * child = ev->node_of(a[0])) {
				          ev->doc->append_child(n, child);
			          }
		          }
		          return a.empty() ? ctjs::value{} : a[0]; // returns the child
	          },
	          "appendChild"));
	o.set("removeChild",
	      ctjs::value::function(
	          [n, ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		          ev->cx = &cx;
		          if (!a.empty() && ev->doc != nullptr) {
			          if (node * child = ev->node_of(a[0])) {
				          ev->doc->remove_child(n, child);
			          }
		          }
		          return a.empty() ? ctjs::value{} : a[0];
	          },
	          "removeChild"));
	o.set("setAttribute",
	      ctjs::value::function(
	          [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		          if (a.size() >= 2) {
			          const std::string key = a[0].to_string();
			          const std::string val = a[1].to_string();
			          bool found = false;
			          for (auto & [k, v] : n->attributes) {
				          if (k == key) {
					          v = val;
					          found = true;
				          }
			          }
			          if (!found) { n->attributes.emplace_back(key, val); }
			          if (key == "id") { n->id = val; }
			          if (key == "class") { n->classes = val; }
			          if (n->is_canvas() && (key == "width" || key == "height")) {
				          const int d = parse_int_attr(val, 0);
				          if (key == "width") { n->canvas_w = d; }
				          else { n->canvas_h = d; }
				          n->pixels.assign(static_cast<size_t>(n->canvas_w) *
				                               static_cast<size_t>(n->canvas_h),
				                           0x00000000u);
			          }
		          }
		          return ctjs::value{};
	          },
	          "setAttribute"));
	o.set("addEventListener",
	      ctjs::value::function(
	          [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		          ev->cx = &cx;
		          if (a.size() >= 2 && a[1].is_function()) {
			          ev->listeners[a[0].to_string()].push_back(a[1]);
		          }
		          return ctjs::value{};
	          },
	          "addEventListener"));
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
	// the element's live layout rect (viewport coordinates) - lets a
	// script convert mouse positions into canvas-local ones
	o.set("rect", ctjs::value::function(
	                  [n](ctjs::context &, const std::vector<ctjs::value> &) {
		                  ctjs::object_t r;
		                  r.set("x", ctjs::value{static_cast<double>(n->x)});
		                  r.set("y", ctjs::value{static_cast<double>(n->y)});
		                  r.set("w", ctjs::value{static_cast<double>(n->w)});
		                  r.set("h", ctjs::value{static_cast<double>(n->h)});
		                  return ctjs::value::object(std::move(r));
	                  },
	                  "rect"));
	ctjs::value v = ctjs::value::object(std::move(o));
	ev->tracked.emplace_back(v.as_object(), n);
	return v;
}

inline ctjs::value canvas_context(node * n, image_store * images) {
	auto ctx = ctjs::rc<ctjs::object_t>::make();
	ctx->set("fillStyle", ctjs::value{"#000000"});
	ctx->set("strokeStyle", ctjs::value{"#000000"});
	ctx->set("font", ctjs::value{"10px sans-serif"});
	ctx->set("width", ctjs::value{n->canvas_w});
	ctx->set("height", ctjs::value{n->canvas_h});
	const auto style_of = [ctx]() -> uint32_t {
		if (const ctjs::value * v = ctx->find("fillStyle")) {
			return css_to_argb(v->to_string(), 0xFF000000u);
		}
		return 0xFF000000u;
	};
	const auto stroke_of = [ctx]() -> uint32_t {
		if (const ctjs::value * v = ctx->find("strokeStyle")) {
			return css_to_argb(v->to_string(), 0xFF000000u);
		}
		return 0xFF000000u;
	};
	const auto put = [n](int x, int y, uint32_t argb) {
		if (x >= 0 && y >= 0 && x < n->canvas_w && y < n->canvas_h) {
			n->pixels[static_cast<size_t>(y) * static_cast<size_t>(n->canvas_w) +
			          static_cast<size_t>(x)] = argb;
		}
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
	// nearest-neighbour blit with alpha test (a == 0 skips the pixel)
	const auto blit = [n, images, put](int handle, int sx, int sy, int sw, int sh, int dx,
	                                   int dy, int dw, int dh) {
		const image * im = images->get(handle);
		if (im == nullptr || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) { return; }
		for (int py = 0; py < dh; ++py) {
			const int src_y = sy + py * sh / dh;
			if (src_y < 0 || src_y >= im->h) { continue; }
			for (int px = 0; px < dw; ++px) {
				const int src_x = sx + px * sw / dw;
				if (src_x < 0 || src_x >= im->w) { continue; }
				const uint32_t argb = im->pixels[static_cast<size_t>(src_y) *
				                                     static_cast<size_t>(im->w) +
				                                 static_cast<size_t>(src_x)];
				if ((argb >> 24) == 0) { continue; }
				put(dx + px, dy + py, argb);
			}
		}
		(void)n;
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
	                         [put, style_of](ctjs::context &, const std::vector<ctjs::value> & a) {
		                         if (a.size() >= 2) {
			                         put(static_cast<int>(a[0].to_number()),
			                             static_cast<int>(a[1].to_number()), style_of());
		                         }
		                         return ctjs::value{};
	                         },
	                         "putPixel"));
	// clearRect clears to TRANSPARENT (the page shows through), per spec
	ctx->set("clearRect", ctjs::value::function(
	                          [fill](ctjs::context &, const std::vector<ctjs::value> & a) {
		                          if (a.size() >= 4) {
			                          fill(static_cast<int>(a[0].to_number()),
			                               static_cast<int>(a[1].to_number()),
			                               static_cast<int>(a[2].to_number()),
			                               static_cast<int>(a[3].to_number()), 0x00000000u);
		                          }
		                          return ctjs::value{};
	                          },
	                          "clearRect"));
	ctx->set("strokeRect",
	         ctjs::value::function(
	             [fill, stroke_of](ctjs::context &, const std::vector<ctjs::value> & a) {
		             if (a.size() >= 4) {
			             const int x = static_cast<int>(a[0].to_number());
			             const int y = static_cast<int>(a[1].to_number());
			             const int w = static_cast<int>(a[2].to_number());
			             const int h = static_cast<int>(a[3].to_number());
			             const uint32_t c = stroke_of();
			             fill(x, y, w, 1, c);
			             fill(x, y + h - 1, w, 1, c);
			             fill(x, y, 1, h, c);
			             fill(x + w - 1, y, 1, h, c);
		             }
		             return ctjs::value{};
	             },
	             "strokeRect"));
	// --- the 2D path API with a REAL transform stack. Per spec, path
	// verbs transform their points by the CURRENT matrix as they are
	// appended (so rasterization is matrix-free): subpaths are device-
	// space polylines, fill() scanline-fills them (even-odd), stroke()
	// draws lineWidth-thick segments. arc() honors its angles by
	// sampling; rect() appends a closed 4-point subpath.
	struct ctx2d {
		double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; // CTM [a c e; b d f]
		std::vector<std::array<double, 6>> stack;
		struct sub {
			std::vector<std::pair<double, double>> pts;
			bool closed = false;
		};
		std::vector<sub> subs;

		std::pair<double, double> tx(double x, double y) const {
			return {a * x + c * y + e, b * x + d * y + f};
		}
		void mul(double a2, double b2, double c2, double d2, double e2, double f2) {
			const double na = a * a2 + c * b2;
			const double nb = b * a2 + d * b2;
			const double nc = a * c2 + c * d2;
			const double nd = b * c2 + d * d2;
			const double ne = a * e2 + c * f2 + e;
			const double nf = b * e2 + d * f2 + f;
			a = na; b = nb; c = nc; d = nd; e = ne; f = nf;
		}
		sub & cur() {
			if (subs.empty()) { subs.emplace_back(); }
			return subs.back();
		}
	};
	auto st = std::make_shared<ctx2d>();
	// even-odd scanline fill of every subpath (closing them implicitly)
	const auto fill_subs = [st, put](uint32_t col) {
		double miny = 1e300, maxy = -1e300;
		for (const auto & s : st->subs) {
			for (const auto & [px, py] : s.pts) {
				miny = std::min(miny, py);
				maxy = std::max(maxy, py);
			}
		}
		if (miny > maxy) { return; }
		for (int y = static_cast<int>(std::floor(miny)); y <= static_cast<int>(std::ceil(maxy));
		     ++y) {
			const double sy = y + 0.5;
			std::vector<double> xs;
			for (const auto & s : st->subs) {
				const size_t n = s.pts.size();
				if (n < 2) { continue; }
				for (size_t i = 0; i < n; ++i) {
					const auto [x1, y1] = s.pts[i];
					const auto [x2, y2] = s.pts[(i + 1) % n];
					if ((y1 <= sy) == (y2 <= sy)) { continue; }
					xs.push_back(x1 + (sy - y1) / (y2 - y1) * (x2 - x1));
				}
			}
			std::sort(xs.begin(), xs.end());
			for (size_t i = 0; i + 1 < xs.size(); i += 2) {
				for (int x = static_cast<int>(std::ceil(xs[i] - 0.5));
				     x < static_cast<int>(std::ceil(xs[i + 1] - 0.5)) + 1; ++x) {
					put(x, y, col);
				}
			}
		}
	};
	// a lineWidth-thick segment as a filled quad
	const auto thick_seg = [put](double x1, double y1, double x2, double y2, double w,
	                             uint32_t col) {
		const double dx = x2 - x1;
		const double dy = y2 - y1;
		const double len = std::hypot(dx, dy);
		if (len == 0) { return; }
		const double nx = -dy / len * (w / 2);
		const double ny = dx / len * (w / 2);
		const int minx = static_cast<int>(std::floor(std::min({x1 - std::fabs(nx), x2 - std::fabs(nx)})));
		const int maxx = static_cast<int>(std::ceil(std::max({x1 + std::fabs(nx), x2 + std::fabs(nx)})));
		const int miny = static_cast<int>(std::floor(std::min({y1 - std::fabs(ny) - w, y2 - std::fabs(ny) - w})));
		const int maxy = static_cast<int>(std::ceil(std::max({y1 + std::fabs(ny) + w, y2 + std::fabs(ny) + w})));
		for (int y = miny; y <= maxy; ++y) {
			for (int x = minx; x <= maxx; ++x) {
				// distance from pixel center to the segment
				const double px = x + 0.5 - x1;
				const double py = y + 0.5 - y1;
				const double t = std::clamp((px * dx + py * dy) / (len * len), 0.0, 1.0);
				const double ddx = px - t * dx;
				const double ddy = py - t * dy;
				if (ddx * ddx + ddy * ddy <= (w / 2) * (w / 2)) { put(x, y, col); }
			}
		}
	};
	ctx->set("lineWidth", ctjs::value{1.0});
	ctx->set("globalAlpha", ctjs::value{1.0});
	const auto num_prop = [ctx](const char * name, double fallback) {
		const ctjs::value * v = ctx->find(name);
		return v != nullptr && v->is_number() ? v->as_number() : fallback;
	};
	ctx->set("save", ctjs::value::function(
	                     [st](ctjs::context &, const std::vector<ctjs::value> &) {
		                     st->stack.push_back({st->a, st->b, st->c, st->d, st->e, st->f});
		                     return ctjs::value{};
	                     },
	                     "save"));
	ctx->set("restore", ctjs::value::function(
	                        [st](ctjs::context &, const std::vector<ctjs::value> &) {
		                        if (!st->stack.empty()) {
			                        const auto m = st->stack.back();
			                        st->stack.pop_back();
			                        st->a = m[0]; st->b = m[1]; st->c = m[2];
			                        st->d = m[3]; st->e = m[4]; st->f = m[5];
		                        }
		                        return ctjs::value{};
	                        },
	                        "restore"));
	ctx->set("translate", ctjs::value::function(
	                          [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		                          if (a.size() >= 2) {
			                          st->mul(1, 0, 0, 1, a[0].to_number(), a[1].to_number());
		                          }
		                          return ctjs::value{};
	                          },
	                          "translate"));
	ctx->set("rotate", ctjs::value::function(
	                       [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		                       if (!a.empty()) {
			                       const double t = a[0].to_number();
			                       st->mul(std::cos(t), std::sin(t), -std::sin(t),
			                               std::cos(t), 0, 0);
		                       }
		                       return ctjs::value{};
	                       },
	                       "rotate"));
	ctx->set("scale", ctjs::value::function(
	                      [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		                      if (a.size() >= 2) {
			                      st->mul(a[0].to_number(), 0, 0, a[1].to_number(), 0, 0);
		                      }
		                      return ctjs::value{};
	                      },
	                      "scale"));
	ctx->set("resetTransform", ctjs::value::function(
	                               [st](ctjs::context &, const std::vector<ctjs::value> &) {
		                               st->a = st->d = 1;
		                               st->b = st->c = st->e = st->f = 0;
		                               return ctjs::value{};
	                               },
	                               "resetTransform"));
	ctx->set("beginPath", ctjs::value::function(
	                          [st](ctjs::context &, const std::vector<ctjs::value> &) {
		                          st->subs.clear();
		                          return ctjs::value{};
	                          },
	                          "beginPath"));
	ctx->set("closePath", ctjs::value::function(
	                          [st](ctjs::context &, const std::vector<ctjs::value> &) {
		                          if (!st->subs.empty()) { st->subs.back().closed = true; }
		                          return ctjs::value{};
	                          },
	                          "closePath"));
	ctx->set("moveTo", ctjs::value::function(
	                       [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		                       if (a.size() >= 2) {
			                       st->subs.emplace_back();
			                       st->subs.back().pts.push_back(
			                           st->tx(a[0].to_number(), a[1].to_number()));
		                       }
		                       return ctjs::value{};
	                       },
	                       "moveTo"));
	ctx->set("lineTo", ctjs::value::function(
	                       [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		                       if (a.size() >= 2) {
			                       st->cur().pts.push_back(
			                           st->tx(a[0].to_number(), a[1].to_number()));
		                       }
		                       return ctjs::value{};
	                       },
	                       "lineTo"));
	ctx->set("rect", ctjs::value::function(
	                     [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		                     if (a.size() >= 4) {
			                     const double x = a[0].to_number();
			                     const double y = a[1].to_number();
			                     const double w = a[2].to_number();
			                     const double h = a[3].to_number();
			                     ctx2d::sub s;
			                     s.pts = {st->tx(x, y), st->tx(x + w, y),
			                              st->tx(x + w, y + h), st->tx(x, y + h)};
			                     s.closed = true;
			                     st->subs.push_back(std::move(s));
		                     }
		                     return ctjs::value{};
	                     },
	                     "rect"));
	ctx->set("arc", ctjs::value::function(
	                    [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		                    if (a.size() >= 5) {
			                    const double cx = a[0].to_number();
			                    const double cy = a[1].to_number();
			                    const double r = a[2].to_number();
			                    double t0 = a[3].to_number();
			                    double t1 = a[4].to_number();
			                    const bool ccw = a.size() >= 6 && a[5].truthy();
			                    if (!ccw && t1 < t0) { t1 += 6.283185307179586; }
			                    if (ccw && t0 < t1) { t0 += 6.283185307179586; }
			                    const int steps =
			                        std::max(8, static_cast<int>(std::fabs(t1 - t0) * r / 2));
			                    auto & s = st->cur();
			                    for (int i = 0; i <= steps; ++i) {
				                    const double t = t0 + (t1 - t0) * i / steps;
				                    s.pts.push_back(st->tx(cx + r * std::cos(t),
				                                           cy + r * std::sin(t)));
			                    }
		                    }
		                    return ctjs::value{};
	                    },
	                    "arc"));
	ctx->set("fill", ctjs::value::function(
	                     [st, fill_subs, style_of](ctjs::context &,
	                                               const std::vector<ctjs::value> &) {
		                     fill_subs(style_of());
		                     return ctjs::value{};
	                     },
	                     "fill"));
	ctx->set("stroke",
	         ctjs::value::function(
	             [st, thick_seg, stroke_of, num_prop](ctjs::context &,
	                                                  const std::vector<ctjs::value> &) {
		             const uint32_t col = stroke_of();
		             const double w = num_prop("lineWidth", 1.0);
		             for (const auto & s : st->subs) {
			             const size_t n = s.pts.size();
			             for (size_t i = 0; i + 1 < n; ++i) {
				             thick_seg(s.pts[i].first, s.pts[i].second,
				                       s.pts[i + 1].first, s.pts[i + 1].second, w, col);
			             }
			             if (s.closed && n > 2) {
				             thick_seg(s.pts[n - 1].first, s.pts[n - 1].second,
				                       s.pts[0].first, s.pts[0].second, w, col);
			             }
		             }
		             return ctjs::value{};
	             },
	             "stroke"));
	ctx->set("measureText",
	         ctjs::value::function(
	             [ctx](ctjs::context &, const std::vector<ctjs::value> & a) {
		             const std::string text = a.empty() ? "" : a[0].to_string();
		             int px = 10;
		             if (const ctjs::value * fv = ctx->find("font")) {
			             const std::string spec = fv->to_string();
			             int v = 0;
			             for (size_t i = 0; i < spec.size() && spec[i] >= '0' && spec[i] <= '9';
			                  ++i) {
				             v = v * 10 + (spec[i] - '0');
			             }
			             if (v > 0) { px = v; }
		             }
		             const int scale = px >= 8 ? px / 8 : 1;
		             ctjs::object_t m;
		             m.set("width", ctjs::value{static_cast<double>(text.size()) * 8.0 *
		                                        static_cast<double>(scale)});
		             return ctjs::value::object(std::move(m));
	             },
	             "measureText"));
	// a filled circle - not in the 2D spec, but games want one (documented
	// extension, like drawImageRegion below)
	ctx->set("fillCircle",
	         ctjs::value::function(
	             [put, style_of](ctjs::context &, const std::vector<ctjs::value> & a) {
		             if (a.size() >= 3) {
			             const int cx = static_cast<int>(a[0].to_number());
			             const int cy = static_cast<int>(a[1].to_number());
			             const int r = static_cast<int>(a[2].to_number());
			             const uint32_t c = style_of();
			             for (int y = -r; y <= r; ++y) {
				             for (int x = -r; x <= r; ++x) {
					             if (x * x + y * y <= r * r) { put(cx + x, cy + y, c); }
				             }
			             }
		             }
		             return ctjs::value{};
	             },
	             "fillCircle"));
	// DOM fillText: y is the BASELINE, size comes from ctx.font
	// ("16px Arial" -> 16, default 10px), glyphs are the embedded 8x8
	// font scaled by the integer factor px/8 (min 1)
	ctx->set("fillText",
	         ctjs::value::function(
	             [ctx, put, style_of](ctjs::context &, const std::vector<ctjs::value> & a) {
		             if (a.size() >= 3) {
			             const std::string text = a[0].to_string();
			             int px = 0;
			             if (const ctjs::value * f = ctx->find("font")) {
				             const std::string spec = f->to_string();
				             for (size_t i = 0;
				                  i < spec.size() && spec[i] >= '0' && spec[i] <= '9';
				                  ++i) {
					             px = px * 10 + (spec[i] - '0');
				             }
			             }
			             const int scale = px >= 8 ? px / 8 : 1;
			             const int x = static_cast<int>(a[1].to_number());
			             const int y = static_cast<int>(a[2].to_number()) - 8 * scale;
			             const uint32_t c = style_of();
			             int pen = x;
			             for (const char ch : text) {
				             for (int row = 0; row < 8; ++row) {
					             for (int col = 0; col < 8; ++col) {
						             if (!glyph_pixel(ch, row, col)) { continue; }
						             for (int sy = 0; sy < scale; ++sy) {
							             for (int sx = 0; sx < scale; ++sx) {
								             put(pen + col * scale + sx,
								                 y + row * scale + sy, c);
							             }
						             }
					             }
				             }
				             pen += 8 * scale;
			             }
		             }
		             return ctjs::value{};
	             },
	             "fillText"));
	ctx->set("drawImage",
	         ctjs::value::function(
	             [images, blit](ctjs::context &, const std::vector<ctjs::value> & a) {
		             if (a.size() >= 3) {
			             const int handle = static_cast<int>(a[0].to_number());
			             const image * im = images->get(handle);
			             if (im != nullptr) {
				             const int dw = a.size() >= 5 ? static_cast<int>(a[3].to_number())
				                                          : im->w;
				             const int dh = a.size() >= 5 ? static_cast<int>(a[4].to_number())
				                                          : im->h;
				             blit(handle, 0, 0, im->w, im->h,
				                  static_cast<int>(a[1].to_number()),
				                  static_cast<int>(a[2].to_number()), dw, dh);
			             }
		             }
		             return ctjs::value{};
	             },
	             "drawImage"));
	// sprite sheets: source region -> destination region
	ctx->set("drawImageRegion",
	         ctjs::value::function(
	             [blit](ctjs::context &, const std::vector<ctjs::value> & a) {
		             if (a.size() >= 9) {
			             blit(static_cast<int>(a[0].to_number()),
			                  static_cast<int>(a[1].to_number()),
			                  static_cast<int>(a[2].to_number()),
			                  static_cast<int>(a[3].to_number()),
			                  static_cast<int>(a[4].to_number()),
			                  static_cast<int>(a[5].to_number()),
			                  static_cast<int>(a[6].to_number()),
			                  static_cast<int>(a[7].to_number()),
			                  static_cast<int>(a[8].to_number()));
		             }
		             return ctjs::value{};
	             },
	             "drawImageRegion"));
	return ctjs::value{ctx};
}

// the document global: getElementById/addEventListener plus the
// location.reload() escape hatch (the engine re-instantiates the DOM
// and re-runs the script - navigation, compile-time style)
inline ctjs::value make_document(document & doc, image_store & images, dom_events & ev) {
	ev.doc = &doc;
	ctjs::object_t d;
	d.set("createElement",
	      ctjs::value::function(
	          [&doc, &images, &ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		          ev.cx = &cx;
		          if (a.empty()) { return ctjs::value{}; }
		          node * n = doc.create_element(a[0].to_string());
		          return element_handle(n, &images, &ev);
	          },
	          "createElement"));
	d.set("body", ctjs::value{}); // filled below once the tree exists
	if (node * b = doc.body()) { d.set("body", element_handle(b, &images, &ev)); }
	d.set("getElementById",
	      ctjs::value::function(
	          [&doc, &images, &ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		          ev.cx = &cx;
		          if (a.empty()) { return ctjs::value{}; }
		          node * n = doc.by_id(a[0].to_string());
		          return n != nullptr ? detail::element_handle(n, &images, &ev)
		                              : ctjs::value{};
	          },
	          "getElementById"));
	d.set("addEventListener",
	      ctjs::value::function(
	          [&ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		          ev.cx = &cx;
		          if (a.size() >= 2 && a[1].is_function()) {
			          ev.listeners[a[0].to_string()].push_back(a[1]);
		          }
		          return ctjs::value{};
	          },
	          "addEventListener"));
	ctjs::object_t loc;
	loc.set("reload", ctjs::value::function(
	                      [&ev](ctjs::context &, const std::vector<ctjs::value> &) {
		                      ev.reload = true;
		                      return ctjs::value{};
	                      },
	                      "reload"));
	d.set("location", ctjs::value::object(std::move(loc)));
	return ctjs::value::object(std::move(d));
}

} // namespace detail

// the host bindings a page's script runs with; `title` receives
// document.setTitle updates (the shell mirrors it into the window),
// `images` backs loadImage/drawImage, `ev` collects the script's
// addEventListener/requestAnimationFrame registrations
inline std::vector<ctjs::binding> dom_bindings(document & doc, std::string & title,
                                               image_store & images, dom_events & ev) {
	std::vector<ctjs::binding> out;
	out.push_back({"getElementById",
	               ctjs::native([&doc, &images, &ev](const std::vector<ctjs::value> & a)
	                                -> ctjs::value {
		               if (a.empty()) { return {}; }
		               node * n = doc.by_id(a[0].to_string());
		               return n ? detail::element_handle(n, &images, &ev) : ctjs::value{};
	               },
	               "getElementById")});
	out.push_back({"getContext",
	               ctjs::native([&doc, &images](const std::vector<ctjs::value> & a) -> ctjs::value {
		               if (a.empty()) { return {}; }
		               node * n = doc.by_id(a[0].to_string());
		               return n != nullptr && n->is_canvas()
		                          ? detail::canvas_context(n, &images)
		                          : ctjs::value{};
	               },
	               "getContext")});
	out.push_back({"document", detail::make_document(doc, images, ev)});
	{
		// the window object: the environment surface libraries probe.
		// addEventListener shares the document's listener registry;
		// performance.now is the engine's clock (now_ms)
		auto w = ctjs::rc<ctjs::object_t>::make();
		w->set("innerWidth", ctjs::value{0});
		w->set("innerHeight", ctjs::value{0});
		w->set("devicePixelRatio", ctjs::value{1.0});
		w->set("addEventListener",
		       ctjs::value::function(
		           [&ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
			           ev.cx = &cx;
			           if (a.size() >= 2 && a[1].is_function()) {
				           ev.listeners[a[0].to_string()].push_back(a[1]);
			           }
			           return ctjs::value{};
		           },
		           "addEventListener"));
		ctjs::object_t perf;
		perf.set("now", ctjs::value::function(
		                    [&ev](ctjs::context &, const std::vector<ctjs::value> &) {
			                    return ctjs::value{ev.now_ms};
		                    },
		                    "now"));
		w->set("performance", ctjs::value::object(std::move(perf)));
		ev.window_obj = w;
		out.push_back({"window", ctjs::value{w}});
	}
	out.push_back(
	    {"requestAnimationFrame",
	     ctjs::value::function(
	         [&ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		         ev.cx = &cx;
		         if (!a.empty() && a[0].is_function()) { ev.raf.push_back(a[0]); }
		         return ctjs::value{++ev.raf_id};
	         },
	         "requestAnimationFrame")});
	{
		// setTimeout/setInterval share the engine clock; fired by tick()
		const auto arm = [&ev](bool repeat, const char * name) {
			return ctjs::value::function(
			    [&ev, repeat](ctjs::context & cx, const std::vector<ctjs::value> & a)
			        -> ctjs::value {
				    ev.cx = &cx;
				    if (a.empty() || !a[0].is_function()) { return ctjs::value{0.0}; }
				    const double ms = a.size() > 1 ? a[1].to_number() : 0.0;
				    ev.timers.push_back({++ev.timer_seq, ev.now_ms + (ms > 0 ? ms : 0),
				                         ms > 0 ? ms : 0, repeat, a[0]});
				    return ctjs::value{ev.timer_seq};
			    },
			    name);
		};
		out.push_back({"setTimeout", arm(false, "setTimeout")});
		out.push_back({"setInterval", arm(true, "setInterval")});
		const auto disarm = [&ev](const char * name) {
			return ctjs::value::function(
			    [&ev](ctjs::context &, const std::vector<ctjs::value> & a) -> ctjs::value {
				    if (!a.empty() && a[0].is_number()) { ev.clear_timer(a[0].as_number()); }
				    return ctjs::value{};
			    },
			    name);
		};
		out.push_back({"clearTimeout", disarm("clearTimeout")});
		out.push_back({"clearInterval", disarm("clearInterval")});
	}
	out.push_back({"alert",
	               ctjs::value::function(
	                   [&ev](ctjs::context &, const std::vector<ctjs::value> & a) {
		                   const std::string msg = a.empty() ? "" : a[0].to_string();
		                   ev.alerts.push_back(msg);
		                   std::fprintf(stderr, "ctbrowser: alert: %s\n", msg.c_str());
		                   return ctjs::value{};
	                   },
	                   "alert")});
	out.push_back(
	    {"fetch",
	     ctjs::native(
	         [&images](const std::vector<ctjs::value> & a) -> ctjs::value {
		         // the web fetch(), backed by COMPILE-TIME fetching: every
		         // fetch("https://...") literal in the page's script was
		         // fetched during the build (assets.hpp, --fetch-allow
		         // gated) into the embedded-asset registry, so the promise
		         // this returns is already settled - which is exactly the
		         // subset ctjs promises implement. A URL that was not (or
		         // could not be) baked in rejects like a network failure.
		         const std::string url = a.empty() ? "" : a[0].to_string();
		         const embedded_asset * hit = find_asset(images.embedded, url);
		         if (hit == nullptr) {
			         return ctjs::make_promise(
			             ctjs::make_error("TypeError", "Failed to fetch: " + url), true);
		         }
		         std::string body{reinterpret_cast<const char *>(hit->data), hit->size};
		         ctjs::object_t r;
		         r.set("ok", ctjs::value{true});
		         r.set("status", ctjs::value{200});
		         r.set("url", ctjs::value{url});
		         r.set("text", ctjs::value::function(
		                           [body](ctjs::context &, const std::vector<ctjs::value> &) {
			                           return ctjs::make_promise(ctjs::value{body}, false);
		                           },
		                           "text"));
		         r.set("json",
		               ctjs::value::function(
		                   [body](ctjs::context &, const std::vector<ctjs::value> &)
		                       -> ctjs::value {
			                   try {
				                   size_t i = 0;
				                   ctjs::value parsed = ctjs::detail::json_value(body, i, 0);
				                   ctjs::detail::json_ws(body, i);
				                   if (i != body.size()) { ctjs::detail::json_fail(i); }
				                   return ctjs::make_promise(std::move(parsed), false);
			                   } catch (const ctjs::js_throw & ex) {
				                   return ctjs::make_promise(ex.thrown, true);
			                   }
		                   },
		                   "json"));
		         r.set("bytes", ctjs::value::function(
		                            [body](ctjs::context &, const std::vector<ctjs::value> &) {
			                            ctjs::array_t bytes;
			                            bytes.reserve(body.size());
			                            for (const char c : body) {
				                            bytes.push_back(ctjs::value{static_cast<double>(
				                                static_cast<unsigned char>(c))});
			                            }
			                            return ctjs::make_promise(
			                                ctjs::value::array(std::move(bytes)), false);
		                            },
		                            "bytes"));
		         return ctjs::make_promise(ctjs::value::object(std::move(r)), false);
	         },
	         "fetch")});
	out.push_back({"loadImage",
	               ctjs::native([&images](const std::vector<ctjs::value> & a) -> ctjs::value {
		               if (a.empty()) { return ctjs::value{-1.0}; }
		               return ctjs::value{
		                   static_cast<double>(images.load(a[0].to_string()))};
	               },
	               "loadImage")});
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
