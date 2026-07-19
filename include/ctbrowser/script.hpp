#ifndef CTBROWSER__SCRIPT__HPP
#define CTBROWSER__SCRIPT__HPP

#include "dom.hpp"
#include "image.hpp"
#include "font8x8.hpp"
#include <ctjs.hpp>
#ifndef CTBROWSER_IN_A_MODULE
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

namespace ctbrowser {

// script-registered callbacks (addEventListener / requestAnimationFrame)
// plus the interpreter context needed to call them back; the context is
// captured at registration time and lives in the engine's run_result
struct dom_events {
	ctjs::context * cx = nullptr;
	std::map<std::string, std::vector<ctjs::value>, std::less<>> listeners;
	std::vector<ctjs::value> raf;
	double raf_id = 0;
	std::vector<std::string> alerts;
	bool reload = false;
	double now_ms = 0;
	// element handles whose layout-derived properties refresh per frame
	std::vector<std::pair<std::shared_ptr<ctjs::object_t>, node *>> tracked;

	void reset() {
		cx = nullptr;
		listeners.clear();
		raf.clear();
		alerts.clear();
		reload = false;
		tracked.clear();
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
	o.set("getContext", ctjs::value::function(
	                        [n, images, ev](ctjs::context & cx, const std::vector<ctjs::value> &) {
		                        ev->cx = &cx;
		                        return n->is_canvas() ? canvas_context(n, images)
		                                              : ctjs::value{};
	                        },
	                        "getContext"));
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
	auto ctx = std::make_shared<ctjs::object_t>();
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
	// --- the 2D path API. Shapes accumulate between beginPath and
	// fill; arc() with a full sweep is a disc, and partial arcs
	// DEGRADE to the full disc (documented approximation - games draw
	// balls, not pie charts)
	struct path_shape {
		bool is_rect;
		double x, y, w, h; // rect
		double cx, cy, r;  // circle
	};
	auto path = std::make_shared<std::vector<path_shape>>();
	const auto disc = [put](int cx, int cy, int r, uint32_t c) {
		for (int y = -r; y <= r; ++y) {
			for (int x = -r; x <= r; ++x) {
				if (x * x + y * y <= r * r) { put(cx + x, cy + y, c); }
			}
		}
	};
	ctx->set("beginPath", ctjs::value::function(
	                          [path](ctjs::context &, const std::vector<ctjs::value> &) {
		                          path->clear();
		                          return ctjs::value{};
	                          },
	                          "beginPath"));
	ctx->set("closePath", ctjs::value::function(
	                          [](ctjs::context &, const std::vector<ctjs::value> &) {
		                          return ctjs::value{};
	                          },
	                          "closePath"));
	ctx->set("rect", ctjs::value::function(
	                     [path](ctjs::context &, const std::vector<ctjs::value> & a) {
		                     if (a.size() >= 4) {
			                     path->push_back({true, a[0].to_number(), a[1].to_number(),
			                                      a[2].to_number(), a[3].to_number(), 0, 0,
			                                      0});
		                     }
		                     return ctjs::value{};
	                     },
	                     "rect"));
	ctx->set("arc", ctjs::value::function(
	                    [path](ctjs::context &, const std::vector<ctjs::value> & a) {
		                    if (a.size() >= 3) {
			                    path->push_back({false, 0, 0, 0, 0, a[0].to_number(),
			                                     a[1].to_number(), a[2].to_number()});
		                    }
		                    return ctjs::value{};
	                    },
	                    "arc"));
	ctx->set("fill", ctjs::value::function(
	                     [path, fill, disc, style_of](ctjs::context &,
	                                                  const std::vector<ctjs::value> &) {
		                     const uint32_t c = style_of();
		                     for (const path_shape & s : *path) {
			                     if (s.is_rect) {
				                     fill(static_cast<int>(s.x), static_cast<int>(s.y),
				                          static_cast<int>(s.w), static_cast<int>(s.h),
				                          c);
			                     } else {
				                     disc(static_cast<int>(s.cx),
				                          static_cast<int>(s.cy), static_cast<int>(s.r),
				                          c);
			                     }
		                     }
		                     return ctjs::value{};
	                     },
	                     "fill"));
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
	ctjs::object_t d;
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
	out.push_back(
	    {"requestAnimationFrame",
	     ctjs::value::function(
	         [&ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		         ev.cx = &cx;
		         if (!a.empty() && a[0].is_function()) { ev.raf.push_back(a[0]); }
		         return ctjs::value{++ev.raf_id};
	         },
	         "requestAnimationFrame")});
	out.push_back({"alert",
	               ctjs::value::function(
	                   [&ev](ctjs::context &, const std::vector<ctjs::value> & a) {
		                   const std::string msg = a.empty() ? "" : a[0].to_string();
		                   ev.alerts.push_back(msg);
		                   std::fprintf(stderr, "ctbrowser: alert: %s\n", msg.c_str());
		                   return ctjs::value{};
	                   },
	                   "alert")});
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
