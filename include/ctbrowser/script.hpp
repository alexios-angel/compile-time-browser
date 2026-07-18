#ifndef CTBROWSER__SCRIPT__HPP
#define CTBROWSER__SCRIPT__HPP

#include "dom.hpp"
#include "image.hpp"
#include "font8x8.hpp"
#include <ctjs.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <cmath>
#include <memory>
#include <set>
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
	return ctjs::value::object(std::move(o));
}

inline ctjs::value canvas_context(node * n, image_store * images) {
	auto ctx = std::make_shared<ctjs::object_t>();
	ctx->set("fillStyle", ctjs::value{"#000000"});
	ctx->set("strokeStyle", ctjs::value{"#000000"});
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
	// text in the embedded 8x8 font, scaled by an integer factor
	ctx->set("fillText",
	         ctjs::value::function(
	             [put, style_of](ctjs::context &, const std::vector<ctjs::value> & a) {
		             if (a.size() >= 3) {
			             const std::string text = a[0].to_string();
			             const int x = static_cast<int>(a[1].to_number());
			             const int y = static_cast<int>(a[2].to_number());
			             const int scale =
			                 a.size() >= 4 ? static_cast<int>(a[3].to_number()) : 1;
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

} // namespace detail

// the host bindings a page's script runs with; `title` receives
// document.setTitle updates (the shell mirrors it into the window),
// `images` backs loadImage/drawImage
inline std::vector<ctjs::binding> dom_bindings(document & doc, std::string & title,
                                               image_store & images) {
	std::vector<ctjs::binding> out;
	out.push_back({"getElementById",
	               ctjs::native([&doc](const std::vector<ctjs::value> & a) -> ctjs::value {
		               if (a.empty()) { return {}; }
		               node * n = doc.by_id(a[0].to_string());
		               return n ? detail::element_handle(n) : ctjs::value{};
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
