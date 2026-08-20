// dom_bindings - the 2D canvas context object and its matrix.
//
// One of six files carved out of a 3,926-line bindings.cpp on 2026-08-09.
// These are all member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp, so they split across translation
// units with nothing to declare and no linkage to arrange.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <numbers>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

// A DOMMatrix: the six numbers a page reads, and the methods it composes with.
//
// Every method is IMMUTABLE - `inverse()`, `multiply()`, `translate()` and
// `scale()` all return a new matrix - which is what the spec says for these
// names, the mutating ones being `invertSelf`, `multiplySelf` and so on. Getting
// that backwards would leave a page's base matrix quietly modified.
value dom_bindings::matrix_object(context & cx, const transform & t) {
    auto * out = static_cast<script::object_object *>(cx.make_object().as_heap());
    out->set("a", value::number(t.a));
    out->set("b", value::number(t.b));
    out->set("c", value::number(t.c));
    out->set("d", value::number(t.d));
    out->set("e", value::number(t.e));
    out->set("f", value::number(t.f));
    // The aliases the CSS-facing half of the API uses for the same six numbers.
    out->set("m11", value::number(t.a));
    out->set("m12", value::number(t.b));
    out->set("m21", value::number(t.c));
    out->set("m22", value::number(t.d));
    out->set("m41", value::number(t.e));
    out->set("m42", value::number(t.f));
    out->set("is2D", value::boolean(true));
    const auto method = [&](std::string name, script::native_fn fn) {
        out->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // Reads its operand back out of whatever object it was given, so a page can
    // pass a DOMMatrix, a plain {a,b,c,d,e,f}, or the result of getTransform.
    const auto read = [](context & c, value v) {
        transform got;
        if (!v.is_object()) { return got; }
        const auto part = [&](const char * name, float fallback) {
            const value held = c.lookup_property(v, name);
            return held.is_undefined() ? fallback : static_cast<float>(context::to_number(held));
        };
        got.a = part("a", 1);
        got.b = part("b", 0);
        got.c = part("c", 0);
        got.d = part("d", 1);
        got.e = part("e", 0);
        got.f = part("f", 0);
        return got;
    };
    method("inverse",
           [this, t](context & c, std::span<value>) { return matrix_object(c, t.inverse()); });
    method("multiply", [this, t, read](context & c, std::span<value> a) {
        // `A.multiply(B)` is B applied FIRST and then A, which is the order the
        // spec's matrix product gives - and `then` here already composes that
        // way round, so this is A.then(B) with the arguments as written.
        return matrix_object(c, t.then(read(c, arg(a, 0))));
    });
    method("translate", [this, t](context & c, std::span<value> a) {
        return matrix_object(c, transform::translation(number(a, 0), number(a, 1)).then(t));
    });
    method("scale", [this, t](context & c, std::span<value> a) {
        const float sx = a.empty() ? 1.0f : number(a, 0);
        const float sy = a.size() > 1 ? number(a, 1) : sx;
        return matrix_object(c, transform::scaling(sx, sy).then(t));
    });
    method("rotate", [this, t](context & c, std::span<value> a) {
        // DEGREES, which is the one place this API does not use radians.
        const auto radians =
            static_cast<float>(static_cast<double>(number(a, 0)) * std::numbers::pi / 180.0);
        return matrix_object(c, transform::rotation(radians).then(t));
    });
    method("toString", [t](context & c, std::span<value>) {
        const auto text = [](float v) {
            std::string out = std::to_string(v);
            // Trailing zeros make `matrix(1.000000, ...)` - correct and unreadable.
            while (out.size() > 1 && out.back() == '0') { out.pop_back(); }
            if (!out.empty() && out.back() == '.') { out.pop_back(); }
            return out;
        };
        return c.string("matrix(" + text(t.a) + ", " + text(t.b) + ", " + text(t.c) + ", " +
                        text(t.d) + ", " + text(t.e) + ", " + text(t.f) + ")");
    });
    return value::object(out);
}

value dom_bindings::canvas_context_object(context & cx, node_id id) {
    const auto txn = doc_->read();
    const auto attribute = [&](std::string_view name, int fallback) {
        const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
        if (text.empty()) { return fallback; }
        int value = 0;
        for (const char c : text) {
            if (c < '0' || c > '9') { break; }
            value = value * 10 + (c - '0');
        }
        return value == 0 ? fallback : value;
    };
    canvas_context * canvas =
        canvases_->context_for(id, attribute("width", 300), attribute("height", 150));
    if (canvas == nullptr) { return value::null(); }

    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    if (canvas2d_prototype_.is_object()) { obj->prototype = canvas2d_prototype_; }
    const value self = value::object(obj);
    obj->set("canvas", wrap(cx, id));
    const auto method = [&](std::string name, script::native_fn fn) {
        obj->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // fillStyle and strokeStyle are PROPERTIES that the drawing calls read
    // back, which is the real canvas idiom - `ctx.fillStyle = 'red'` then
    // `ctx.fillRect(...)`. Reading them at draw time rather than at
    // assignment is what makes that work.
    obj->set("fillStyle", cx.string("#000000"));
    obj->set("strokeStyle", cx.string("#000000"));
    obj->set("lineWidth", value::number(1));
    obj->set("globalAlpha", value::number(1));
    obj->set("font", cx.string("10px sans-serif"));
    obj->set("textAlign", cx.string("start"));
    obj->set("textBaseline", cx.string("alphabetic"));
    obj->set("globalCompositeOperation", cx.string("source-over"));

    const auto sync = [canvas](context & c) {
        const value self_value = c.current_this();
        if (!self_value.is_object()) { return; }
        auto * o = static_cast<script::object_object *>(self_value.as_heap());
        // The spec strings are remembered alongside the parsed values, and only
        // when the parse SUCCEEDED - an unreadable colour leaves both halves
        // alone, so what restore() puts back always matches what is drawn.
        if (const value * v = o->find("fillStyle")) {
            std::string spec = c.to_string(*v);
            if (const auto parsed = paint::parse_color(spec)) {
                canvas->fill_style = *parsed;
                canvas->fill_spec = std::move(spec);
            }
        }
        if (const value * v = o->find("strokeStyle")) {
            std::string spec = c.to_string(*v);
            if (const auto parsed = paint::parse_color(spec)) {
                canvas->stroke_style = *parsed;
                canvas->stroke_spec = std::move(spec);
            }
        }
        if (const value * v = o->find("lineWidth")) {
            canvas->line_width = static_cast<float>(context::to_number(*v));
        }
        if (const value * v = o->find("globalAlpha")) {
            canvas->global_alpha = static_cast<float>(context::to_number(*v));
        }
        if (const value * v = o->find("font")) {
            std::string font = c.to_string(*v);
            canvas->font_size = font_size_from(font);
            font_face_from(font, canvas->font_family, canvas->font_bold, canvas->font_italic);
            canvas->font_spec = std::move(font);
        }
        // Kept as the spec's strings rather than parsed here: an unknown value
        // has to behave as the default, and the drawing code is the one place
        // that knows what the default means for each of them.
        if (const value * v = o->find("textAlign")) { canvas->text_align = c.to_string(*v); }
        if (const value * v = o->find("textBaseline")) { canvas->text_baseline = c.to_string(*v); }
        // `globalCompositeOperation` is PARSED here rather than kept as a string,
        // because the drawing code needs the operator on every pixel and a string
        // comparison per pixel is not a thing to do. The spelling is kept
        // alongside it so save/restore and the property read back what was set,
        // and an unknown name behaves as source-over per spec.
        if (const value * v = o->find("globalCompositeOperation")) {
            canvas->composite_spec = c.to_string(*v);
            canvas->composite_mode = composite_from_name(canvas->composite_spec);
        }
    };

    // A drawing call does NOT report a document mutation. The canvas's
    // pixels are shared with the display list, so nothing needs re-recording
    // - the browser notices the canvas's revision moved and re-rasters. An
    // animation that marked the document dirty would re-run style and layout
    // sixty times a second for a page that did not change.
    const auto draws = [sync](auto body) {
        return [sync, body](context & c, std::span<value> args) {
            sync(c);
            body(c, args);
            return value::undefined();
        };
    };

    method("fillRect", draws([canvas](context &, std::span<value> a) {
               canvas->fill_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("clearRect", draws([canvas](context &, std::span<value> a) {
               canvas->clear_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("strokeRect", draws([canvas](context &, std::span<value> a) {
               canvas->stroke_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("beginPath", draws([canvas](context &, std::span<value>) { canvas->begin_path(); }));
    method("closePath", draws([canvas](context &, std::span<value>) { canvas->close_path(); }));
    method("moveTo", draws([canvas](context &, std::span<value> a) {
               canvas->move_to(number(a, 0), number(a, 1));
           }));
    method("lineTo", draws([canvas](context &, std::span<value> a) {
               canvas->line_to(number(a, 0), number(a, 1));
           }));
    method("rect", draws([canvas](context &, std::span<value> a) {
               canvas->rect_path(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("arc", draws([canvas](context & c, std::span<value> a) {
               canvas->arc(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 4),
                           a.size() > 5 && context::truthy(a[5]));
               (void)c;
           }));
    // drawImage(image, dx, dy [, dw, dh]) and the source-rect form. `image`
    // is either a loadImage() handle or an <img> element wrapper - the same
    // two things a page can hold, and both have to work.
    method("drawImage", draws([this, canvas](context &, std::span<value> a) {
               const std::shared_ptr<const paint::bitmap> source = image_argument(arg(a, 0));
               if (!source) { return; }
               const auto natural_w = static_cast<float>(source->width);
               const auto natural_h = static_cast<float>(source->height);
               if (a.size() >= 9) {
                   // The nine-argument form takes a rectangle OUT of the source.
                   canvas->draw_image_region(*source, number(a, 1), number(a, 2), number(a, 3),
                                             number(a, 4), number(a, 5), number(a, 6), number(a, 7),
                                             number(a, 8));
                   return;
               }
               const float w = a.size() >= 4 ? number(a, 3) : natural_w;
               const float h = a.size() >= 5 ? number(a, 4) : natural_h;
               canvas->draw_image(*source, number(a, 1), number(a, 2), w, h);
           }));
    // `fill(path)` and `stroke(path)` REPLAY a Path2D rather than using the
    // context's own current path. p5.js draws every shape that way: it builds
    // one Path2D per shape with a visitor and hands it to the context, so a
    // fill() that ignored its argument drew whatever happened to be left in the
    // context - usually nothing.
    //
    // Replaying replaces the current path, which is what the spec's "these
    // methods do not affect the current default path" amounts to here: nothing
    // in this engine reads the path back afterwards.
    const auto replay = [canvas](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return; }
        const value commands = c.lookup_property(a[0], std::string{path_commands_property});
        if (!commands.is_array()) { return; }
        canvas->begin_path();
        for (const value & step : static_cast<script::array_object *>(commands.as_heap())->items) {
            if (!step.is_array()) { continue; }
            const auto & parts = static_cast<script::array_object *>(step.as_heap())->items;
            if (parts.empty()) { continue; }
            const std::string verb = c.to_string(parts[0]);
            const auto n = [&](std::size_t i) {
                return i < parts.size() ? static_cast<float>(context::to_number(parts[i])) : 0.0f;
            };
            if (verb == "M") {
                canvas->move_to(n(1), n(2));
            } else if (verb == "L") {
                canvas->line_to(n(1), n(2));
            } else if (verb == "Q") {
                canvas->quadratic_curve_to(n(1), n(2), n(3), n(4));
            } else if (verb == "C") {
                canvas->bezier_curve_to(n(1), n(2), n(3), n(4), n(5), n(6));
            } else if (verb == "R") {
                canvas->rect_path(n(1), n(2), n(3), n(4));
            } else if (verb == "A") {
                canvas->arc(n(1), n(2), n(3), n(4), n(5), n(6) != 0);
            } else if (verb == "E") {
                canvas->ellipse(n(1), n(2), n(3), n(4), n(5), n(6), n(7), n(8) != 0);
            } else if (verb == "Z") {
                canvas->close_path();
            }
        }
    };
    // `clip()` takes the same two forms as fill: a Path2D and/or a rule.
    method("clip", draws([canvas, replay](context & c, std::span<value> a) {
               replay(c, a);
               auto rule = canvas_context::fill_rule::nonzero;
               for (const value & v : a) {
                   if (v.is_string() && c.to_string(v) == "evenodd") {
                       rule = canvas_context::fill_rule::even_odd;
                   }
               }
               canvas->clip(rule);
           }));
    method("fill", draws([canvas, replay](context & c, std::span<value> a) {
               replay(c, a);
               // The rule is the last argument in both forms - `fill(rule)` and
               // `fill(path, rule)` - so it is looked for rather than counted.
               auto rule = canvas_context::fill_rule::nonzero;
               for (const value & v : a) {
                   if (v.is_string() && c.to_string(v) == "evenodd") {
                       rule = canvas_context::fill_rule::even_odd;
                   }
               }
               canvas->fill(rule);
           }));
    method("stroke", draws([canvas, replay](context & c, std::span<value> a) {
               replay(c, a);
               canvas->stroke();
           }));
    method("save", draws([canvas](context &, std::span<value>) { canvas->save(); }));
    // restore() has to write the state BACK TO THE JAVASCRIPT OBJECT, not just
    // pop the C++ stack. Everything on that stack except the transform is also
    // a property script can assign - fillStyle, strokeStyle, lineWidth,
    // globalAlpha, font - and sync() copies those onto the context before every
    // call. A restore that only popped was therefore undone by the very next
    // draw, and the transform appeared to be the only thing save() protected
    // for exactly that reason: it is the one piece of state with no property
    // behind it.
    //
    // Ordering is what makes this correct: draws() runs sync() FIRST, so any
    // assignment made since the last call is folded in before restore() pops
    // over it - which is what the spec means by restoring the state as of the
    // matching save().
    method("restore", draws([canvas](context & c, std::span<value>) {
               canvas->restore();
               const value self_value = c.current_this();
               if (!self_value.is_object()) { return; }
               auto * o = static_cast<script::object_object *>(self_value.as_heap());
               o->set("fillStyle", c.string(canvas->fill_spec));
               o->set("strokeStyle", c.string(canvas->stroke_spec));
               o->set("font", c.string(canvas->font_spec));
               o->set("textAlign", c.string(canvas->text_align));
               o->set("textBaseline", c.string(canvas->text_baseline));
               o->set("lineWidth", value::number(canvas->line_width));
               o->set("globalAlpha", value::number(canvas->global_alpha));
               o->set("globalCompositeOperation", c.string(canvas->composite_spec));
           }));
    method("translate", draws([canvas](context &, std::span<value> a) {
               canvas->translate(number(a, 0), number(a, 1));
           }));
    method("scale", draws([canvas](context &, std::span<value> a) {
               canvas->scale(number(a, 0), number(a, 1));
           }));
    method("rotate",
           draws([canvas](context &, std::span<value> a) { canvas->rotate(number(a, 0)); }));
    method("ellipse", draws([canvas](context &, std::span<value> a) {
               canvas->ellipse(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 4),
                               number(a, 5), number(a, 6), a.size() > 7 && context::truthy(a[7]));
           }));
    method("resetTransform",
           draws([canvas](context &, std::span<value>) { canvas->reset_transform(); }));
    // `setTransform` REPLACES the matrix and `transform` composes with it. The
    // six numbers are the 2x3 in the spec's order - a, b, c, d, e, f - and a
    // single argument is a matrix-like object, which is the form
    // `ctx.setTransform(ctx.getTransform())` takes.
    const auto matrix_argument = [](context & c, std::span<value> a) {
        if (!a.empty() && a[0].is_object()) {
            const auto component = [&](const char * name, float fallback) {
                const value v = c.lookup_property(a[0], name);
                return v.is_undefined() ? fallback : static_cast<float>(context::to_number(v));
            };
            return transform{component("a", 1), component("b", 0), component("c", 0),
                             component("d", 1), component("e", 0), component("f", 0)};
        }
        return transform{number(a, 0), number(a, 1), number(a, 2),
                         number(a, 3), number(a, 4), number(a, 5)};
    };
    method("setTransform", draws([canvas, matrix_argument](context & c, std::span<value> a) {
               // No arguments is the identity, which is what resetTransform is.
               canvas->set_transform(a.empty() ? transform{} : matrix_argument(c, a));
           }));
    method("transform", draws([canvas, matrix_argument](context & c, std::span<value> a) {
               canvas->multiply_transform(matrix_argument(c, a));
           }));
    // Not wrapped in draws(): reading the matrix changes no pixels. It does
    // still have to be the LIVE one, so a library that reads it back after its
    // own translate() sees the translate.
    method("getTransform", [this, canvas](context & c, std::span<value>) {
        return matrix_object(c, canvas->current_transform());
    });
    // `new DOMMatrix()` - and getTransform hands one back.
    //
    // It used to return six bare numbers, which is the arithmetic and none of the
    // API. p5's beginClip does `getTransform().inverse().multiply(other)` to
    // express a clip path relative to where the clip began, so `inverse is
    // undefined` was the whole of clip() not working - and a page composing
    // transforms has no other way to do it.
    cx.define_native("DOMMatrix", [this](context & c, std::span<value> a) {
        transform t;
        // `new DOMMatrix([a, b, c, d, e, f])`, which is the form a page that
        // serialises one writes back.
        if (!a.empty() && a[0].is_array()) {
            const auto & items = static_cast<script::array_object *>(a[0].as_heap())->items;
            const auto at = [&](std::size_t i, float fallback) {
                return i < items.size() ? static_cast<float>(context::to_number(items[i]))
                                        : fallback;
            };
            t.a = at(0, 1);
            t.b = at(1, 0);
            t.c = at(2, 0);
            t.d = at(3, 1);
            t.e = at(4, 0);
            t.f = at(5, 0);
        }
        return matrix_object(c, t);
    });
    method("fillText", draws([canvas](context & c, std::span<value> a) {
               canvas->fill_text(a.empty() ? std::string{} : c.to_string(a[0]), number(a, 1),
                                 number(a, 2));
           }));
    // NOT wrapped in draws() - it changes no pixels - but it must still SYNC,
    // and it did not. It is the one method that read the canvas's font state
    // without refreshing it first, so `ctx.font = '20px X'; ctx.measureText(s)`
    // measured in whatever font the last DRAWING call happened to leave behind.
    // --- the pixels, as bytes
    //
    // getImageData/putImageData are how a page reads what it drew and writes
    // back something it computed - every filter, every colour pick, every
    // `pixels[]` loop in p5.js. The buffer is RGBA bytes in that order, which
    // is NOT the engine's packed ARGB, so both directions unpack rather than
    // memcpy: getting that wrong swaps red and blue and looks almost right.
    const auto image_data = [](context & c, int width, int height) {
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        out->set("width", value::number(width));
        out->set("height", value::number(height));
        value bytes = c.make_array();
        auto * store = static_cast<script::array_object *>(bytes.as_heap());
        // A REAL Uint8ClampedArray, so a page's `data[i] = 300` clamps to 255
        // the way it does in a browser rather than storing 300.
        store->elements = script::element_kind::u8_clamped;
        store->items.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4,
                            value::number(0));
        out->set("data", bytes);
        return std::pair{out, store};
    };
    method("createImageData", [image_data](context & c, std::span<value> a) {
        // `createImageData(other)` takes its SIZE from the other one and is
        // still blank, which is what makes it the way to get a scratch buffer.
        int width = static_cast<int>(arg_number(a, 0));
        int height = static_cast<int>(arg_number(a, 1));
        if (!a.empty() && a[0].is_object()) {
            width = static_cast<int>(context::to_number(c.lookup_property(a[0], "width")));
            height = static_cast<int>(context::to_number(c.lookup_property(a[0], "height")));
        }
        return value::object(image_data(c, std::max(0, width), std::max(0, height)).first);
    });
    method("getImageData", [canvas, image_data](context & c, std::span<value> a) {
        const int x = static_cast<int>(arg_number(a, 0));
        const int y = static_cast<int>(arg_number(a, 1));
        const int width = std::max(0, static_cast<int>(arg_number(a, 2)));
        const int height = std::max(0, static_cast<int>(arg_number(a, 3)));
        auto [out, store] = image_data(c, width, height);
        const auto & surface = canvas->surface();
        if (!surface) { return value::object(out); }
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                const color pixel{surface->at(x + column, y + row)};
                const auto at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(column)) *
                                4;
                store->items[at] = value::number(pixel.red());
                store->items[at + 1] = value::number(pixel.green());
                store->items[at + 2] = value::number(pixel.blue());
                store->items[at + 3] = value::number(pixel.alpha());
            }
        }
        return value::object(out);
    });
    method("putImageData", draws([canvas](context & c, std::span<value> a) {
               if (a.empty() || !a[0].is_object()) { return; }
               const value source = c.lookup_property(a[0], "data");
               if (!source.is_array()) { return; }
               const auto & bytes = static_cast<script::array_object *>(source.as_heap())->items;
               const int width =
                   static_cast<int>(context::to_number(c.lookup_property(a[0], "width")));
               const int height =
                   static_cast<int>(context::to_number(c.lookup_property(a[0], "height")));
               const int dx = static_cast<int>(arg_number(a, 1));
               const int dy = static_cast<int>(arg_number(a, 2));
               // Written straight into the surface: putImageData is NOT
               // affected by the transform, the clip or globalAlpha. That is
               // the spec and it is the reason it exists - a page computing
               // pixels wants those pixels, not those pixels composited.
               const auto & surface = canvas->surface();
               if (!surface) { return; }
               for (int row = 0; row < height; ++row) {
                   for (int column = 0; column < width; ++column) {
                       const auto at =
                           (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(column)) *
                           4;
                       if (at + 3 >= bytes.size()) { continue; }
                       const auto channel = [&](std::size_t offset) {
                           return static_cast<std::uint8_t>(
                               std::clamp(context::to_number(bytes[at + offset]), 0.0, 255.0));
                       };
                       const int px = dx + column;
                       const int py = dy + row;
                       if (px < 0 || py < 0 || px >= surface->width || py >= surface->height) {
                           continue;
                       }
                       surface->pixels[static_cast<std::size_t>(py) *
                                           static_cast<std::size_t>(surface->width) +
                                       static_cast<std::size_t>(px)] =
                           color::rgba(channel(0), channel(1), channel(2), channel(3)).argb;
                   }
               }
           }));
    method("measureText", [canvas, sync](context & c, std::span<value> a) {
        sync(c);
        auto * metrics = static_cast<script::object_object *>(c.make_object().as_heap());
        const std::string text = a.empty() ? std::string{} : c.to_string(a[0]);
        // Through the canvas, so the object that measures is the object that
        // draws - see docs/raster.md. Measuring with font8x8 while fillText
        // drew with a real face is exactly how text ends up where it was not.
        const double width = static_cast<double>(canvas->measure_text(text));
        metrics->set("width", value::number(width));
        // THE BOUNDING BOX, not just the advance. A `width` on its own is not
        // enough for a library that positions text itself: p5.js measures a
        // line as `actualBoundingBoxLeft + actualBoundingBoxRight`, which was
        // undefined + undefined = NaN, and every width it derived from that -
        // textWidth, line wrapping, text bounds - was NaN too.
        //
        // The box is measured from the ALIGNMENT POINT, so which side of it the
        // run falls on depends on textAlign. That is what keeps left+right
        // equal to the width whatever the alignment is.
        double left = 0;
        if (canvas->text_align == "center") {
            left = width / 2;
        } else if (canvas->text_align == "right" || canvas->text_align == "end") {
            left = width;
        }
        metrics->set("actualBoundingBoxLeft", value::number(left));
        metrics->set("actualBoundingBoxRight", value::number(width - left));
        // The font's metrics stand in for the glyphs' own extents. They are an
        // over-estimate for a run with no ascenders or descenders, which is the
        // safe direction: text laid out to these bounds never overlaps.
        const auto & backend = canvas->fonts();
        const double ascent = static_cast<double>(backend.ascent(
            canvas->font_size, canvas->font_family, canvas->font_bold, canvas->font_italic));
        const double descent = static_cast<double>(backend.descent(
            canvas->font_size, canvas->font_family, canvas->font_bold, canvas->font_italic));
        metrics->set("actualBoundingBoxAscent", value::number(ascent));
        metrics->set("actualBoundingBoxDescent", value::number(descent));
        metrics->set("fontBoundingBoxAscent", value::number(ascent));
        metrics->set("fontBoundingBoxDescent", value::number(descent));
        metrics->set("emHeightAscent", value::number(ascent));
        metrics->set("emHeightDescent", value::number(descent));
        metrics->set("alphabeticBaseline", value::number(0));
        return value::object(metrics);
    });
    return self;
}

float dom_bindings::number(std::span<value> args, std::size_t i) {
    return i < args.size() ? static_cast<float>(context::to_number(args[i])) : 0.0f;
}

float dom_bindings::font_size_from(std::string_view font) {
    const std::size_t px = font.find("px");
    if (px == std::string_view::npos) { return 10; }
    std::size_t start = px;
    while (start > 0 && font[start - 1] >= '0' && font[start - 1] <= '9') { --start; }
    float size = 0;
    for (std::size_t i = start; i < px; ++i) {
        size = size * 10 + static_cast<float>(font[i] - '0');
    }
    return size > 0 ? size : 10;
}

void dom_bindings::font_face_from(std::string_view font, std::string & family, bool & bold,
                                  bool & italic) {
    family.clear();
    bold = false;
    italic = false;
    const std::size_t px = font.find("px");
    if (px == std::string_view::npos) { return; }

    // Before the size: the style and weight keywords.
    const std::string_view before = font.substr(0, px);
    bold = before.find("bold") != std::string_view::npos;
    italic = before.find("italic") != std::string_view::npos ||
             before.find("oblique") != std::string_view::npos;

    // After it: the family list. The FIRST entry, which is what the rest of the
    // engine resolves too (layout takes the first name of the list as well).
    std::string_view rest = font.substr(px + 2);
    if (const std::size_t comma = rest.find(','); comma != std::string_view::npos) {
        rest = rest.substr(0, comma);
    }
    const std::size_t first = rest.find_first_not_of(" \t'\"");
    if (first == std::string_view::npos) { return; }
    const std::size_t last = rest.find_last_not_of(" \t'\"");
    family = rest.substr(first, last - first + 1);
}

} // namespace ctbrowser::shell
