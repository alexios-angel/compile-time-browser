//===- canvas.cpp - The 2D canvas context bindings --------------*- C++ -*-===//
//
// The CanvasRenderingContext2D object, its gradients and patterns, and the
// DOMMatrix that getTransform returns. Following WebIDL (HTML 4.12.5.1), every
// method and attribute lives on CanvasRenderingContext2D.prototype, and a
// context object is an empty object linked to it that names its canvas under
// a private key. The drawing state lives only in C++ (canvas_context::state).
//
// Attributes are accessors, not data properties, because an attribute must
// reject invalid values ("ctx.lineWidth = -1" is ignored) and read back its
// own serialisation ("#fa0" reads as "#ffaa00").
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>

#include "canvas_objects.hpp"
#include "image_data.hpp"

#include <charconv>
#include <numbers>
#include <tuple>
#include <unordered_map>

namespace ctbrowser::shell {

using namespace canvas_detail;

namespace {

/// On a context object: the key of its canvas's node.
constexpr std::string_view canvas_key = CTBROWSER_PRIVATE_KEY("ctbrowser:canvas2d");
/// On a canvas element's wrapper: its one 2D context, since calling
/// getContext('2d') twice returns the same object (HTML 4.12.5, step "return
/// the same object").
constexpr std::string_view context_cache_key = CTBROWSER_PRIVATE_KEY("ctbrowser:context2d");
/// On a context object: the CanvasGradient or CanvasPattern script objects
/// held by fillStyle and strokeStyle at every level of the state stack, two
/// slots per level, so the getter returns the very object that was assigned,
/// even after a restore() (HTML 4.12.5.1.10, "the respective object"). They
/// are kept in a script array because the state that names them is C++, which
/// the collector cannot see.
constexpr std::string_view styles_key = CTBROWSER_PRIVATE_KEY("ctbrowser:styles");
/// On CanvasRenderingContext2D.prototype: the other interfaces' prototypes,
/// which must outlive a page deleting the globals that also hold them.
constexpr std::string_view gradient_proto_key = CTBROWSER_PRIVATE_KEY("ctbrowser:gradient-proto");
constexpr std::string_view pattern_proto_key = CTBROWSER_PRIVATE_KEY("ctbrowser:pattern-proto");
constexpr std::string_view metrics_proto_key = CTBROWSER_PRIVATE_KEY("ctbrowser:metrics-proto");
/// On CanvasRenderingContext2D.prototype: marks that install_canvas_2d has
/// already populated it.
constexpr std::string_view installed_key = CTBROWSER_PRIVATE_KEY("ctbrowser:canvas2d-installed");

/// Returns the style slots of the context `self`, grown to hold `levels`
/// levels; with `grow` false, whatever exists (nullptr when none yet).
[[nodiscard]] std::vector<value> * style_slots(context & c, value self, std::size_t levels,
                                               bool grow) {
    if (!self.is_object()) { return nullptr; }
    auto & holder = *static_cast<script::object_object *>(self.as_heap());
    const value * found = holder.find(styles_key);
    if (found == nullptr || !found->is_array()) {
        if (!grow) { return nullptr; }
        holder.set(styles_key, c.make_array());
        found = holder.find(styles_key);
    }
    std::vector<value> & slots = static_cast<script::array_object *>(found->as_heap())->items;
    if (grow && slots.size() < 2 * levels) { slots.resize(2 * levels, value::undefined()); }
    return &slots;
}

/// arg_number as the float the canvas works in.
[[nodiscard]] float number(std::span<value> args, std::size_t i) {
    return static_cast<float>(arg_number(args, i));
}

/// Maps a 0..1 channel to a byte, clamping out-of-range values.
[[nodiscard]] std::uint8_t unit_byte(float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

/// Formats a number as JavaScript would print it, for the attributes that read
/// back as CSS text (letterSpacing's "12px").
[[nodiscard]] std::string number_text(double v) {
    if (v == std::trunc(v) && std::fabs(v) < 1e15) {
        return std::to_string(static_cast<long long>(v));
    }
    char buffer[64];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, v);
    return ec == std::errc{} ? std::string{buffer, end} : std::string{"0"};
}

/// A CSS colour parsed for the canvas: the colour to draw with, and the text
/// its attribute reads back as.
struct parsed_color {
    color value;
    std::string text;
};
/// Parses a CSS colour for the canvas. The text follows HTML's "serialization
/// of a color": a legacy sRGB colour is `#rrggbb` when opaque and
/// `rgba(r, g, b, a)` when not, and a colour in any other space keeps its CSS
/// computed form, `color(srgb 0.5 0 0.5)`. `current` is what `currentcolor`
/// means here.
[[nodiscard]] std::optional<parsed_color> parse_canvas_color(std::string_view text,
                                                             std::string_view current) {
    style::css::color_context ctx;
    ctx.current_color = current;
    const std::string computed = style::css::computed_color(text, ctx);
    if (computed.empty()) { return std::nullopt; }
    const std::optional<style::css::srgb_color> rgb = style::css::resolve_color(text, ctx);
    if (!rgb) { return std::nullopt; }
    parsed_color out{
        color::rgba(unit_byte(rgb->r), unit_byte(rgb->g), unit_byte(rgb->b), unit_byte(rgb->a)),
        computed};
    if (computed.starts_with("rgb(")) {
        static constexpr char digits[] = "0123456789abcdef";
        out.text = "#";
        for (const std::uint8_t channel : {out.value.red(), out.value.green(), out.value.blue()}) {
            out.text += digits[channel >> 4];
            out.text += digits[channel & 0xF];
        }
    }
    return out;
}

/// Makes a canvas interface whose constructor refuses to be called, and
/// returns its prototype.
[[nodiscard]] script::object_object * canvas_interface(context & cx, const char * name) {
    const auto refuse = [name](context & c, std::span<value>) {
        c.throw_error("TypeError", std::string{"Illegal constructor: "} + name);
        return value::undefined();
    };
    return make_interface(cx, name, refuse, {}, std::nullopt).proto;
}

/// Installs a WebIDL operation with `required` as its `length`.
void canvas_operation(context & cx, script::object_object & proto, const std::string & name,
                      unsigned required, script::native_fn fn) {
    set_operation(cx, proto, name, required, std::move(fn));
}

/// Reads `holder[key]`, or undefined when `holder` is not an object.
[[nodiscard]] value prototype_slot(value holder, std::string_view key) {
    if (!holder.is_object()) { return value::undefined(); }
    const value * slot = static_cast<script::object_object *>(holder.as_heap())->find(key);
    return slot == nullptr ? value::undefined() : *slot;
}

/// Lowercases the generic keywords of a font-family list (`SERIF` is the
/// keyword serif and serialises as one) and keeps every other name as written.
[[nodiscard]] std::string lower_generic_families(std::string_view list) {
    std::string out;
    std::size_t at = 0;
    while (true) {
        std::size_t end = at;
        char quote = 0;
        for (; end < list.size(); ++end) {
            const char c = list[end];
            if (quote != 0) {
                if (c == '\\') {
                    ++end;
                } else if (c == quote) {
                    quote = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == ',') {
                break;
            }
        }
        const std::string_view entry = list.substr(at, std::min(end, list.size()) - at);
        const bool generic = ascii_iequals_any(
            trim(entry, html_whitespace),
            {"serif", "sans-serif", "cursive", "fantasy", "monospace", "system-ui", "math", "emoji",
             "fangsong", "ui-serif", "ui-sans-serif", "ui-monospace", "ui-rounded"});
        out += generic ? ascii_lower_copy(entry) : std::string{entry};
        if (end >= list.size()) { break; }
        out += ',';
        at = end + 1;
    }
    return out;
}

/// Applies a canvas `font` value to `s` (HTML 4.12.5.1.4). The value is parsed
/// as the CSS shorthand, except that a CSS-wide keyword or a var() is refused;
/// its size is converted to pixels (a relative one against `base`) and the
/// line height is dropped. Returns the serialisation the attribute reads back,
/// in CSSOM's shorthand order with initial values left out: "italic
/// small-caps 12px serif", never "italic normal 400 12px/2 serif". Returns
/// nullopt, leaving `s` untouched, for a refused font. `relative`, when given,
/// reports whether the size depended on `base`.
[[nodiscard]] std::optional<std::string> apply_font(canvas_context::state & s,
                                                    std::string_view font,
                                                    const std::function<float()> & base,
                                                    bool * relative = nullptr) {
    const std::string_view text = trim(font, html_whitespace);
    if (text.empty() ||
        ascii_iequals_any(text, {"inherit", "initial", "unset", "revert", "revert-layer"}) ||
        ascii_lower_copy(text).find("var(") != std::string::npos) {
        return std::nullopt;
    }
    // Parse it as a stylesheet's `font:` is parsed: set_declaration splits the
    // shorthand into its longhands and refuses an invalid value.
    style::css::declaration_block longhands;
    if (!style::css::set_declaration(longhands, "font", text, false)) { return std::nullopt; }
    std::string style = "normal";
    std::string caps = "normal";
    std::string weight = "normal";
    std::string stretch = "normal";
    std::string size_text;
    std::string family;
    for (const auto & [name, value_text, important] : longhands) {
        const std::string lower = ascii_lower_copy(trim(value_text, html_whitespace));
        if (name == "font-style") {
            style = lower;
        } else if (name == "font-variant-caps" || name == "font-variant") {
            caps = lower;
        } else if (name == "font-weight") {
            weight = lower;
        } else if (name == "font-stretch" || name == "font-width") {
            stretch = lower;
        } else if (name == "font-size") {
            size_text = lower;
        } else if (name == "font-family") {
            family = lower_generic_families(trim(value_text, html_whitespace));
        }
    }
    if (size_text.empty() || family.empty()) { return std::nullopt; }

    // The size, in pixels. ponytail: the absolute keywords use a fixed table
    // with medium = 16px, and rem is 16px rather than the root element's
    // font size; read the root's computed size if a page needs it.
    static constexpr std::string_view keywords[] = {"xx-small", "x-small", "small",    "medium",
                                                    "large",    "x-large", "xx-large", "xxx-large"};
    static constexpr float keyword_px[] = {9, 10, 13, 16, 18, 24, 32, 48};
    float px = -1;
    for (std::size_t i = 0; i < std::size(keywords); ++i) {
        if (size_text == keywords[i]) { px = keyword_px[i]; }
    }
    // Call `base` only for a relative size: it is the element's computed font
    // size, and computing that flushes style, a cost a page setting
    // `ctx.font = "16px Arial"` before every label should not pay.
    const bool is_relative = size_text == "larger" || size_text == "smaller" ||
                             size_text.ends_with('%') ||
                             (size_text.ends_with("em") && !size_text.ends_with("rem")) ||
                             size_text.ends_with("ex") || size_text.ends_with("ch");
    if (relative != nullptr) { *relative = is_relative; }
    if (size_text == "larger") { px = base() * 1.2f; }
    if (size_text == "smaller") { px = base() / 1.2f; }
    if (px < 0) {
        const layout::length size = layout::parse_length(size_text);
        if (size.is_auto()) { return std::nullopt; }
        const float basis = is_relative ? base() : 10.0f;
        px = size.resolve(basis, basis);
    }
    if (!std::isfinite(px) || px < 0) { return std::nullopt; }

    s.font_size = px;
    // The first family, unquoted, is the face, as layout also resolves a list.
    // `list` views `family` itself, not a temporary.
    const std::string_view list = family;
    std::string_view first = trim(list.substr(0, list.find(',')), " \t");
    if (first.size() >= 2 && (first.front() == '"' || first.front() == '\'') &&
        first.back() == first.front()) {
        first = first.substr(1, first.size() - 2);
    }
    s.font_family = std::string{first};
    int numeric = 0;
    const auto parsed = std::from_chars(weight.data(), weight.data() + weight.size(), numeric);
    // 600 and up is bold, as layout reads it.
    s.font_bold =
        parsed.ec == std::errc{} ? numeric >= 600 : weight == "bold" || weight == "bolder";
    s.font_italic = style == "italic" || style.starts_with("oblique");
    s.font_stretch = stretch == "normal" || stretch == "100%" ? "normal" : stretch;
    s.font_variant_caps = caps;

    std::string out;
    const auto part = [&out](std::string_view piece) {
        out += piece;
        out += ' ';
    };
    if (style != "normal") { part(style); }
    if (caps != "normal") { part(caps); }
    if (weight != "normal" && weight != "400") { part(weight); }
    if (stretch != "normal" && stretch != "100%") { part(stretch); }
    out += number_text(static_cast<double>(px)) + "px ";
    const std::string families = style::css::serialize_font_family(family);
    out += families.empty() ? family : families;
    return out;
}

/// The font fields of a canvas state: all that a parsed `font` sets, and all
/// the font parse cache keeps. Caching a whole state would keep its clip mask
/// and its patterns' bitmaps alive as long as the cache entry.
struct canvas_font {
    float size = 10;
    std::string family;
    bool bold = false;
    bool italic = false;
    std::string stretch;
    std::string variant_caps;
    std::string spec;

    [[nodiscard]] static canvas_font of(const canvas_context::state & s) {
        return canvas_font{s.font_size,    s.font_family,       s.font_bold, s.font_italic,
                           s.font_stretch, s.font_variant_caps, s.font_spec};
    }
    void apply(canvas_context::state & s) const {
        s.font_size = size;
        s.font_family = family;
        s.font_bold = bold;
        s.font_italic = italic;
        s.font_stretch = stretch;
        s.font_variant_caps = variant_caps;
        s.font_spec = spec;
    }
};

} // namespace

/// Makes a DOMMatrix: the six numbers a page reads, and the methods it
/// composes with. Every method returns a new matrix (`inverse()`,
/// `multiply()`, `translate()`, `scale()`), as the spec says for these names;
/// the mutating ones are `invertSelf`, `multiplySelf` and so on, which this
/// does not provide.
value dom_bindings::matrix_object(context & cx, const transform & t) {
    auto * out = cx.allocate<script::object_object>();
    out->set("a", value::number(t.a));
    out->set("b", value::number(t.b));
    out->set("c", value::number(t.c));
    out->set("d", value::number(t.d));
    out->set("e", value::number(t.e));
    out->set("f", value::number(t.f));
    // The m-name aliases of the same six numbers, and the rest of the 4x4
    // matrix, which a 2D matrix leaves at the identity.
    out->set("m11", value::number(t.a));
    out->set("m12", value::number(t.b));
    out->set("m13", value::number(0));
    out->set("m14", value::number(0));
    out->set("m21", value::number(t.c));
    out->set("m22", value::number(t.d));
    out->set("m23", value::number(0));
    out->set("m24", value::number(0));
    out->set("m31", value::number(0));
    out->set("m32", value::number(0));
    out->set("m33", value::number(1));
    out->set("m34", value::number(0));
    out->set("m41", value::number(t.e));
    out->set("m42", value::number(t.f));
    out->set("m43", value::number(0));
    out->set("m44", value::number(1));
    out->set("is2D", value::boolean(true));
    out->set("isIdentity",
             value::boolean(t.a == 1 && t.b == 0 && t.c == 0 && t.d == 1 && t.e == 0 && t.f == 0));
    // Reads an operand from whatever object it was given, so a page can pass a
    // DOMMatrix, a plain {a,b,c,d,e,f}, or the result of getTransform.
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
    set_method(cx, *out, "inverse",
               [this, t](context & c, std::span<value>) { return matrix_object(c, t.inverse()); });
    set_method(cx, *out, "multiply", [this, t, read](context & c, std::span<value> a) {
        // The spec's `A.multiply(B)` is the product A x B, which applies B first
        // and then A, i.e. `read(B).then(t)`, since `x.then(y)` applies x
        // first. This computes `t.then(B)`, which applies A first; the two
        // differ when A and B do not commute (translate and scale below use
        // the spec order).
        return matrix_object(c, t.then(read(c, arg(a, 0))));
    });
    set_method(cx, *out, "translate", [this, t](context & c, std::span<value> a) {
        return matrix_object(c, transform::translation(number(a, 0), number(a, 1)).then(t));
    });
    set_method(cx, *out, "scale", [this, t](context & c, std::span<value> a) {
        const float sx = a.empty() ? 1.0f : number(a, 0);
        const float sy = a.size() > 1 ? number(a, 1) : sx;
        return matrix_object(c, transform::scaling(sx, sy).then(t));
    });
    set_method(cx, *out, "rotate", [this, t](context & c, std::span<value> a) {
        // The angle is in degrees, the one place this API does not use radians.
        const auto radians =
            static_cast<float>(static_cast<double>(number(a, 0)) * std::numbers::pi / 180.0);
        return matrix_object(c, transform::rotation(radians).then(t));
    });
    // The sixteen numbers in column-major order, as a typed array.
    const auto elements = [t](context & c, script::element_kind kind) {
        const value out_array = c.make_array();
        auto * items = static_cast<script::array_object *>(out_array.as_heap());
        items->elements = kind;
        for (const float v : {t.a, t.b, 0.0f, 0.0f, t.c, t.d, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                              t.e, t.f, 0.0f, 1.0f}) {
            items->items.push_back(value::number(v));
        }
        return out_array;
    };
    set_method(cx, *out, "toFloat32Array", [elements](context & c, std::span<value>) {
        return elements(c, script::element_kind::f32);
    });
    set_method(cx, *out, "toFloat64Array", [elements](context & c, std::span<value>) {
        return elements(c, script::element_kind::f64);
    });
    set_method(cx, *out, "toString", [t](context & c, std::span<value>) {
        const auto text = [](float v) { return number_text(static_cast<double>(v)); };
        return c.string("matrix(" + text(t.a) + ", " + text(t.b) + ", " + text(t.c) + ", " +
                        text(t.d) + ", " + text(t.e) + ", " + text(t.f) + ")");
    });
    return value::object(out);
}

value dom_bindings::canvas_context_object(context & cx, node_id id) {
    // One context per canvas: the object is cached on the element's wrapper.
    const value wrapper = wrap(cx, id);
    auto * holder =
        wrapper.is_object() ? static_cast<script::object_object *>(wrapper.as_heap()) : nullptr;
    if (holder != nullptr) {
        if (const value * cached = holder->find(context_cache_key);
            cached != nullptr && cached->is_object()) {
            return *cached;
        }
    }
    const auto txn = doc_->read();
    canvas_context * canvas =
        canvases_->context_for(id, static_cast<int>(size_attribute(txn, id, "width", 300)),
                               static_cast<int>(size_attribute(txn, id, "height", 150)));
    if (canvas == nullptr) { return value::null(); }
    auto * obj = cx.allocate<script::object_object>();
    if (canvas2d_prototype_.is_object()) { obj->prototype = canvas2d_prototype_; }
    obj->set(canvas_key, value::number(static_cast<double>(id.key())));
    if (holder != nullptr) { holder->set(context_cache_key, value::object(obj)); }
    return value::object(obj);
}

void dom_bindings::install_canvas_2d(context & cx) {
    const dom_thrower throw_dom = [this](context & c, std::string_view name, std::string message) {
        throw_dom_exception(c, name, std::move(message));
    };
    install_path2d(cx, throw_dom);

    // `new DOMMatrix()` and `new DOMMatrix([a, b, c, d, e, f])`. getTransform
    // returns the same object shape; p5's beginClip relies on its inverse() and
    // multiply().
    cx.define_native("DOMMatrix", [this](context & c, std::span<value> a) {
        transform t;
        // `new DOMMatrix([a, b, c, d, e, f])`, the form a page that serialises a
        // matrix writes back.
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

    if (!canvas2d_prototype_.is_object()) { return; }
    auto & proto = *static_cast<script::object_object *>(canvas2d_prototype_.as_heap());
    if (proto.find(installed_key) != nullptr) { return; }
    proto.set(installed_key, value::boolean(true));
    // An interface object's `prototype` is { writable: false, enumerable:
    // false, configurable: false } (WebIDL 3.7.1): assigning or deleting it
    // does nothing (WPT test 2d.canvas.context.type.prototype).
    if (const value ctor = cx.global("CanvasRenderingContext2D");
        ctor.is_kind(script::heap_kind::native)) {
        static_cast<script::native_object *>(ctor.as_heap())
            ->define("prototype", canvas2d_prototype_, script::attr_none);
    }

    script::object_object * gradient_proto = canvas_interface(cx, "CanvasGradient");
    script::object_object * pattern_proto = canvas_interface(cx, "CanvasPattern");
    script::object_object * metrics_proto = canvas_interface(cx, "TextMetrics");
    proto.set(gradient_proto_key, value::object(gradient_proto));
    proto.set(pattern_proto_key, value::object(pattern_proto));
    proto.set(metrics_proto_key, value::object(metrics_proto));

    // --- this -------------------------------------------------------------
    //
    // The context a method was called on, or a TypeError: calling one on
    // anything but a context is "Illegal invocation" in every browser.
    const auto target = [this](context & c) -> canvas_context * {
        const value self = c.current_this();
        const value * slot =
            self.is_object()
                ? static_cast<script::object_object *>(self.as_heap())->find(canvas_key)
                : nullptr;
        if (slot == nullptr) {
            c.throw_error("TypeError", "Illegal invocation: not a CanvasRenderingContext2D");
            return nullptr;
        }
        return canvases_->find(unpack(static_cast<std::uint64_t>(context::to_number(*slot))));
    };
    const auto canvas_of = [](context & c) {
        const value self = c.current_this();
        const value * slot =
            self.is_object()
                ? static_cast<script::object_object *>(self.as_heap())->find(canvas_key)
                : nullptr;
        return slot == nullptr ? node_id{}
                               : unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
    };

    // The canvas element's computed `property`, or `fallback` when it is not
    // in a document. `currentcolor` and a relative font size resolve against
    // it (HTML 4.12.5.1.10, 4.12.5.1.4).
    const auto computed_of = [this](node_id id, std::string_view property,
                                    std::string fallback) -> std::string {
        bool connected = false;
        {
            const auto txn = doc_->read();
            node_id up = id;
            while (up) {
                const node_id next = txn.parent(up);
                if (!next || next == up) { break; }
                up = next;
            }
            connected = up && up == txn.root();
        }
        if (!connected) { return fallback; }
        flush_layout();
        for (const auto & [name, text] : computed_style_entries(id)) {
            if (name == property && !text.empty()) { return text; }
        }
        return fallback;
    };
    const auto current_color = [computed_of](node_id id) {
        return computed_of(id, "color", "rgb(0, 0, 0)");
    };
    // Parse each colour text once, since pages set the same colours repeatedly
    // (p5 assigns fillStyle before every shape). Colours that depend on the
    // element (currentcolor, light-dark(), contrast-color()) are not cached.
    auto cache = std::make_shared<std::unordered_map<std::string, parsed_color>>();
    const auto parse_color_for =
        [cache, current_color](node_id id,
                               const std::string & text) -> std::optional<parsed_color> {
        if (const auto hit = cache->find(text); hit != cache->end()) { return hit->second; }
        const std::string folded = ascii_lower_copy(text);
        const bool contextual = folded.find("current") != std::string::npos ||
                                folded.find("light-dark") != std::string::npos ||
                                folded.find("contrast") != std::string::npos;
        std::optional<parsed_color> parsed =
            parse_canvas_color(text, contextual ? current_color(id) : "rgb(0, 0, 0)");
        if (parsed && !contextual) {
            if (cache->size() > 1024) { cache->clear(); }
            cache->emplace(text, *parsed);
        }
        return parsed;
    };

    // --- attributes -------------------------------------------------------

    const auto attribute = [&](const std::string & name, script::native_fn get,
                               script::native_fn set) {
        define_getter(cx, proto, name, std::move(get), std::move(set));
    };
    attribute("canvas",
              [this, canvas_of, target](context & c, std::span<value>) {
                  if (target(c) == nullptr && c.throw_pending()) { return value::undefined(); }
                  return wrap(c, canvas_of(c));
              },
              {});

    // A number attribute that ignores invalid values: `accept` says which
    // values are stored; any other value leaves the state unchanged.
    const auto number_attribute = [&](const std::string & name,
                                      double canvas_context::state::* field,
                                      bool (*accept)(double)) {
        attribute(
            name,
            [target, field](context & c, std::span<value>) {
                canvas_context * canvas = target(c);
                return canvas == nullptr ? value::undefined()
                                         : value::number(canvas->current().*field);
            },
            [target, field, accept](context & c, std::span<value> a) {
                canvas_context * canvas = target(c);
                if (canvas == nullptr) { return value::undefined(); }
                const double v = c.to_number_value(arg(a, 0));
                if (c.throw_pending() || !accept(v)) { return value::undefined(); }
                canvas->current().*field = v;
                return value::undefined();
            });
    };
    number_attribute("globalAlpha", &canvas_context::state::alpha,
                     [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; });
    number_attribute("lineWidth", &canvas_context::state::line_width,
                     [](double v) { return std::isfinite(v) && v > 0; });
    number_attribute("miterLimit", &canvas_context::state::miter_limit,
                     [](double v) { return std::isfinite(v) && v > 0; });
    number_attribute("lineDashOffset", &canvas_context::state::dash_offset,
                     [](double v) { return std::isfinite(v); });
    number_attribute("shadowOffsetX", &canvas_context::state::shadow_x,
                     [](double v) { return std::isfinite(v); });
    number_attribute("shadowOffsetY", &canvas_context::state::shadow_y,
                     [](double v) { return std::isfinite(v); });
    number_attribute("shadowBlur", &canvas_context::state::shadow_blur,
                     [](double v) { return std::isfinite(v) && v >= 0; });

    // An enumeration attribute: a string outside the list is ignored, which is
    // WebIDL's rule for an enum-typed attribute and the spec's rule for the
    // DOMString ones with a fixed vocabulary.
    const auto keyword_attribute = [&](const std::string & name,
                                       std::string canvas_context::state::* field,
                                       std::vector<std::string_view> allowed) {
        attribute(
            name,
            [target, field](context & c, std::span<value>) {
                canvas_context * canvas = target(c);
                return canvas == nullptr ? value::undefined() : c.string(canvas->current().*field);
            },
            [target, field, allowed](context & c, std::span<value> a) {
                canvas_context * canvas = target(c);
                if (canvas == nullptr) { return value::undefined(); }
                std::string v = c.to_string(arg(a, 0));
                if (c.throw_pending()) { return value::undefined(); }
                if (std::ranges::find(allowed, std::string_view{v}) == allowed.end()) {
                    return value::undefined();
                }
                canvas->current().*field = std::move(v);
                return value::undefined();
            });
    };
    keyword_attribute("textAlign", &canvas_context::state::text_align,
                      {"start", "end", "left", "right", "center"});
    keyword_attribute("textBaseline", &canvas_context::state::text_baseline,
                      {"top", "hanging", "middle", "alphabetic", "ideographic", "bottom"});
    keyword_attribute("direction", &canvas_context::state::direction, {"ltr", "rtl", "inherit"});
    keyword_attribute("fontKerning", &canvas_context::state::font_kerning,
                      {"auto", "normal", "none"});
    keyword_attribute("fontStretch", &canvas_context::state::font_stretch,
                      {"ultra-condensed", "extra-condensed", "condensed", "semi-condensed",
                       "normal", "semi-expanded", "expanded", "extra-expanded", "ultra-expanded"});
    keyword_attribute("fontVariantCaps", &canvas_context::state::font_variant_caps,
                      {"normal", "small-caps", "all-small-caps", "petite-caps", "all-petite-caps",
                       "unicase", "titling-caps"});
    keyword_attribute("textRendering", &canvas_context::state::text_rendering,
                      {"auto", "optimizeSpeed", "optimizeLegibility", "geometricPrecision"});
    keyword_attribute("imageSmoothingQuality", &canvas_context::state::smoothing_quality,
                      {"low", "medium", "high"});
    attribute(
        "lang",
        [target](context & c, std::span<value>) {
            canvas_context * canvas = target(c);
            return canvas == nullptr ? value::undefined() : c.string(canvas->current().lang);
        },
        [target](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            std::string v = c.to_string(arg(a, 0));
            if (!c.throw_pending()) { canvas->current().lang = std::move(v); }
            return value::undefined();
        });

    // lineCap and lineJoin are enums in the drawing code, text to script.
    attribute(
        "lineCap",
        [target](context & c, std::span<value>) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            switch (canvas->current().cap) {
            case canvas_context::line_cap::round: return c.string("round");
            case canvas_context::line_cap::square: return c.string("square");
            default: return c.string("butt");
            }
        },
        [target](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            const std::string v = c.to_string(arg(a, 0));
            if (v == "butt") { canvas->current().cap = canvas_context::line_cap::butt; }
            if (v == "round") { canvas->current().cap = canvas_context::line_cap::round; }
            if (v == "square") { canvas->current().cap = canvas_context::line_cap::square; }
            return value::undefined();
        });
    attribute(
        "lineJoin",
        [target](context & c, std::span<value>) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            switch (canvas->current().join) {
            case canvas_context::line_join::round: return c.string("round");
            case canvas_context::line_join::bevel: return c.string("bevel");
            default: return c.string("miter");
            }
        },
        [target](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            const std::string v = c.to_string(arg(a, 0));
            if (v == "round") { canvas->current().join = canvas_context::line_join::round; }
            if (v == "bevel") { canvas->current().join = canvas_context::line_join::bevel; }
            if (v == "miter") { canvas->current().join = canvas_context::line_join::miter; }
            return value::undefined();
        });
    attribute(
        "imageSmoothingEnabled",
        [target](context & c, std::span<value>) {
            canvas_context * canvas = target(c);
            return canvas == nullptr ? value::undefined()
                                     : value::boolean(canvas->current().image_smoothing);
        },
        [target](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas != nullptr) {
                canvas->current().image_smoothing = context::truthy(arg(a, 0));
            }
            return value::undefined();
        });
    // globalCompositeOperation accepts only the spec's exact names
    // (case-sensitive, no trailing NUL, no legacy "darker"); anything else is
    // ignored.
    attribute(
        "globalCompositeOperation",
        [target](context & c, std::span<value>) {
            canvas_context * canvas = target(c);
            return canvas == nullptr ? value::undefined()
                                     : c.string(canvas->current().composite_spec);
        },
        [target](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            std::string v = c.to_string(arg(a, 0));
            if (c.throw_pending()) { return value::undefined(); }
            // The <composite-mode> keywords as well as the blend modes:
            // `clear`, and `plus-lighter`, which is `lighter` by its CSS name.
            const bool clear = v == "clear";
            composite mode =
                clear ? composite::copy : composite_from_name(v == "plus-lighter" ? "lighter" : v);
            if (mode == composite::source_over && v != "source-over") { return value::undefined(); }
            canvas->current().composite_mode = mode;
            canvas->current().clear_operator = clear;
            canvas->current().composite_spec = std::move(v);
            return value::undefined();
        });
    // filter: "none" or a <filter-value-list>; the text is kept as given.
    attribute(
        "filter",
        [target](context & c, std::span<value>) {
            canvas_context * canvas = target(c);
            return canvas == nullptr ? value::undefined() : c.string(canvas->current().filter);
        },
        [target](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr || !arg(a, 0).is_string()) { return value::undefined(); }
            std::string v = c.to_string(arg(a, 0));
            // A CSS-wide keyword is not a filter here (WPT 2d.filter.value).
            if (ascii_iequals_any(trim(v, html_whitespace),
                                  {"inherit", "initial", "unset", "revert", "revert-layer"})) {
                return value::undefined();
            }
            if (v == "none" || !style::css::computed_filter(v, {}).empty()) {
                canvas->current().filter = std::move(v);
            }
            return value::undefined();
        });
    attribute(
        "font",
        [target](context & c, std::span<value>) {
            canvas_context * canvas = target(c);
            return canvas == nullptr ? value::undefined() : c.string(canvas->current().font_spec);
        },
        [target, canvas_of, computed_of,
         fonts = std::make_shared<std::unordered_map<std::string, canvas_font>>()](
            context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            const std::string v = c.to_string(arg(a, 0));
            if (c.throw_pending()) { return value::undefined(); }
            canvas_context::state & s = canvas->current();
            // Pages set the same few fonts repeatedly (p5 before every
            // text()), so a font with an absolute size is parsed only once.
            if (const auto hit = fonts->find(v); hit != fonts->end()) {
                hit->second.apply(s);
                return value::undefined();
            }
            // A relative size - `2em`, `50%`, `larger` - is relative to the
            // canvas element's own computed font size, and to the default
            // 10px when the canvas is not in a document.
            const auto base = [&]() {
                const std::string text = computed_of(canvas_of(c), "font-size", "10px");
                const layout::length length = layout::parse_length(text);
                return length.is_auto() ? 10.0f : length.resolve(10, 10);
            };
            canvas_context::state candidate;
            bool relative = false;
            if (const std::optional<std::string> serialized =
                    apply_font(candidate, v, base, &relative)) {
                candidate.font_spec = *serialized;
                const canvas_font font = canvas_font::of(candidate);
                font.apply(s);
                if (!relative) {
                    if (fonts->size() > 256) { fonts->clear(); }
                    fonts->emplace(v, font);
                }
            }
            return value::undefined();
        });
    // letterSpacing and wordSpacing: a CSS <length>, read back as written
    // with the number normalised - "12px", "1.5em".
    const auto spacing_attribute = [&](const std::string & name,
                                       std::string canvas_context::state::* field) {
        attribute(
            name,
            [target, field](context & c, std::span<value>) {
                canvas_context * canvas = target(c);
                return canvas == nullptr ? value::undefined() : c.string(canvas->current().*field);
            },
            [target, field](context & c, std::span<value> a) {
                canvas_context * canvas = target(c);
                if (canvas == nullptr) { return value::undefined(); }
                const std::string v = c.to_string(arg(a, 0));
                if (c.throw_pending()) { return value::undefined(); }
                const std::string_view text = trim(v, html_whitespace);
                double amount = 0;
                const auto [end, ec] =
                    std::from_chars(text.data(), text.data() + text.size(), amount);
                if (ec != std::errc{} || !std::isfinite(amount)) { return value::undefined(); }
                std::string unit =
                    ascii_lower_copy(text.substr(static_cast<std::size_t>(end - text.data())));
                static constexpr std::string_view units[] = {
                    "px",   "em",   "rem", "ex",  "rex", "ch", "rch", "ic", "ric",
                    "cap",  "rcap", "lh",  "rlh", "vw",  "vh", "vi",  "vb", "vmin",
                    "vmax", "cm",   "mm",  "q",   "in",  "pt", "pc"};
                if (std::ranges::find(units, std::string_view{unit}) == std::end(units)) {
                    if (!(unit.empty() && amount == 0)) { return value::undefined(); }
                    unit = "px";
                }
                canvas->current().*field = number_text(amount) + unit;
                return value::undefined();
            });
    };
    spacing_attribute("letterSpacing", &canvas_context::state::letter_spacing);
    spacing_attribute("wordSpacing", &canvas_context::state::word_spacing);

    // fillStyle and strokeStyle: a colour, a CanvasGradient or a CanvasPattern.
    // Anything else is converted to a string (which may throw) and parsed as a
    // CSS colour against the canvas; a value that does not parse is ignored.
    const auto style_attribute = [&](const std::string & name, bool fill) {
        const std::size_t which = fill ? 0 : 1;
        attribute(
            name,
            [target, fill, which, gradient_proto, pattern_proto](context & c, std::span<value>) {
                canvas_context * canvas = target(c);
                if (canvas == nullptr) { return value::undefined(); }
                const canvas_paint & paint =
                    fill ? canvas->current().fill : canvas->current().stroke;
                if (paint.is_solid()) {
                    return c.string(fill ? canvas->current().fill_spec
                                         : canvas->current().stroke_spec);
                }
                // The object this level holds, if it is still the one in use.
                const std::size_t at = 2 * canvas->depth() + which;
                std::vector<value> * slots =
                    style_slots(c, c.current_this(), canvas->depth() + 1, true);
                const value held = slots != nullptr ? (*slots)[at] : value::undefined();
                if (paint.gradient &&
                    unbox<canvas_gradient>(held, gradient_key) == paint.gradient) {
                    return held;
                }
                if (paint.pattern && unbox<canvas_pattern>(held, pattern_key) == paint.pattern) {
                    return held;
                }
                // Otherwise no script object was assigned for this paint (or it
                // was replaced): make one now and keep it, so the next read
                // returns the same object.
                auto * made = c.allocate<script::object_object>();
                if (paint.gradient) {
                    made->prototype = value::object(gradient_proto);
                    box(c, *made, gradient_key, paint.gradient);
                } else {
                    made->prototype = value::object(pattern_proto);
                    box(c, *made, pattern_key,
                        std::const_pointer_cast<canvas_pattern>(paint.pattern));
                }
                slots = style_slots(c, c.current_this(), canvas->depth() + 1, true);
                if (slots != nullptr) { (*slots)[at] = value::object(made); }
                return value::object(made);
            },
            [target, canvas_of, fill, which, parse_color_for](context & c, std::span<value> a) {
                canvas_context * canvas = target(c);
                if (canvas == nullptr) { return value::undefined(); }
                const value v = arg(a, 0);
                canvas_context::state & s = canvas->current();
                canvas_paint & paint = fill ? s.fill : s.stroke;
                // This level's slot holds the object, or nothing for a colour.
                const auto hold = [&](value object) {
                    const bool grow = !object.is_undefined();
                    if (std::vector<value> * slots =
                            style_slots(c, c.current_this(), canvas->depth() + 1, grow)) {
                        const std::size_t at = 2 * canvas->depth() + which;
                        if (at < slots->size()) { (*slots)[at] = object; }
                    }
                };
                if (auto gradient = unbox<canvas_gradient>(v, gradient_key)) {
                    paint = canvas_paint{};
                    paint.gradient = std::move(gradient);
                    hold(v);
                    return value::undefined();
                }
                if (auto pattern = unbox<canvas_pattern>(v, pattern_key)) {
                    paint = canvas_paint{};
                    paint.pattern = std::move(pattern);
                    hold(v);
                    return value::undefined();
                }
                const std::string text = c.to_string(v);
                if (c.throw_pending()) { return value::undefined(); }
                const std::optional<parsed_color> parsed = parse_color_for(canvas_of(c), text);
                if (!parsed) { return value::undefined(); }
                paint = canvas_paint{};
                paint.solid = parsed->value;
                (fill ? s.fill_spec : s.stroke_spec) = parsed->text;
                hold(value::undefined());
                return value::undefined();
            });
    };
    style_attribute("fillStyle", true);
    style_attribute("strokeStyle", false);
    attribute(
        "shadowColor",
        [target](context & c, std::span<value>) {
            canvas_context * canvas = target(c);
            return canvas == nullptr ? value::undefined() : c.string(canvas->current().shadow_spec);
        },
        [target, canvas_of, parse_color_for](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            const std::string text = c.to_string(arg(a, 0));
            if (c.throw_pending()) { return value::undefined(); }
            if (const std::optional<parsed_color> parsed = parse_color_for(canvas_of(c), text)) {
                canvas->current().shadow_color = parsed->value;
                canvas->current().shadow_spec = parsed->text;
            }
            return value::undefined();
        });

    // --- the state ----------------------------------------------------------

    // A method that takes `count` numbers and does nothing unless all are
    // finite: the spec's rule for every drawing and transform method taking
    // `unrestricted double`s.
    const auto finite_op = [&](const std::string & name, unsigned count, auto body) {
        canvas_operation(
            cx, proto, name, count, [target, count, body](context & c, std::span<value> a) {
                canvas_context * canvas = target(c);
                if (canvas == nullptr) { return value::undefined(); }
                std::array<double, 9> v{};
                if (!read_numbers(c, a, count, v.data()) || !all_finite(v.data(), count)) {
                    return value::undefined();
                }
                body(*canvas, v);
                return value::undefined();
            });
    };
    const auto plain_op = [&](const std::string & name, auto body) {
        canvas_operation(cx, proto, name, 0, [target, body](context & c, std::span<value>) {
            if (canvas_context * canvas = target(c)) { body(*canvas); }
            return value::undefined();
        });
    };
    // save(), restore() and reset() keep the style slots in step with the
    // state stack: save() copies the current level's objects to the new level,
    // and restore() or reset() drops the popped levels' objects.
    canvas_operation(cx, proto, "save", 0, [target](context & c, std::span<value>) {
        canvas_context * canvas = target(c);
        if (canvas == nullptr) { return value::undefined(); }
        const std::size_t level = canvas->depth();
        canvas->save();
        if (std::vector<value> * slots = style_slots(c, c.current_this(), 0, false)) {
            slots->resize(2 * (level + 2), value::undefined());
            (*slots)[2 * level + 2] = (*slots)[2 * level];
            (*slots)[2 * level + 3] = (*slots)[2 * level + 1];
        }
        return value::undefined();
    });
    const auto unwind = [target](bool all) {
        return [target, all](context & c, std::span<value>) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            if (all) {
                canvas->reset();
            } else {
                canvas->restore();
            }
            if (std::vector<value> * slots = style_slots(c, c.current_this(), 0, false)) {
                slots->resize(all ? 0 : 2 * (canvas->depth() + 1), value::undefined());
            }
            return value::undefined();
        };
    };
    canvas_operation(cx, proto, "restore", 0, unwind(false));
    canvas_operation(cx, proto, "reset", 0, unwind(true));
    plain_op("beginPath", [](canvas_context & canvas) { canvas.begin_path(); });
    plain_op("resetTransform", [](canvas_context & canvas) { canvas.reset_transform(); });
    canvas_operation(cx, proto, "isContextLost", 0, [target](context & c, std::span<value>) {
        if (target(c) == nullptr && c.throw_pending()) { return value::undefined(); }
        return value::boolean(false);
    });
    // getContextAttributes(): the settings the context was made with, as a new
    // dictionary each call. ponytail: the settings are reported but not all
    // honoured; `alpha: false` and the wide-gamut colour spaces still draw an
    // sRGB canvas with alpha. Honouring them needs support in canvas_context.
    canvas_operation(cx, proto, "getContextAttributes", 0, [target](context & c, std::span<value>) {
        if (target(c) == nullptr) { return value::undefined(); }
        const value settings = prototype_slot(c.current_this(), settings_key);
        const auto read = [&](const char * name, value fallback) {
            const value held = settings.is_object() ? c.lookup_property(settings, name) : fallback;
            return held.is_undefined() ? fallback : held;
        };
        auto * out = c.allocate<script::object_object>();
        out->set("alpha", read("alpha", value::boolean(true)));
        out->set("colorSpace", read("colorSpace", c.string("srgb")));
        out->set("colorType", read("colorType", c.string("unorm8")));
        out->set("desynchronized", read("desynchronized", value::boolean(false)));
        out->set("willReadFrequently", read("willReadFrequently", value::boolean(false)));
        auto * tone = c.allocate<script::object_object>();
        tone->set("mode", read("toneMapping", c.string("standard")));
        out->set("toneMapping", value::object(tone));
        return value::object(out);
    });

    // --- transforms ---------------------------------------------------------

    const auto f = [](double v) { return static_cast<float>(v); };
    finite_op("scale", 2, [f](canvas_context & canvas, const std::array<double, 9> & v) {
        canvas.scale(f(v[0]), f(v[1]));
    });
    finite_op("rotate", 1, [f](canvas_context & canvas, const std::array<double, 9> & v) {
        canvas.rotate(f(v[0]));
    });
    finite_op("translate", 2, [f](canvas_context & canvas, const std::array<double, 9> & v) {
        canvas.translate(f(v[0]), f(v[1]));
    });
    finite_op("transform", 6, [f](canvas_context & canvas, const std::array<double, 9> & v) {
        canvas.multiply_transform(transform{f(v[0]), f(v[1]), f(v[2]), f(v[3]), f(v[4]), f(v[5])});
    });
    // setTransform(a, b, c, d, e, f) replaces the matrix; setTransform(init)
    // takes a DOMMatrix2DInit, and setTransform() sets the identity.
    canvas_operation(cx, proto, "setTransform", 0, [target, f](context & c, std::span<value> a) {
        canvas_context * canvas = target(c);
        if (canvas == nullptr) { return value::undefined(); }
        if (a.size() <= 1) {
            if (const std::optional<transform> m = matrix_2d(c, arg(a, 0))) {
                canvas->set_transform(*m);
            }
            return value::undefined();
        }
        if (a.size() < 6) {
            c.throw_error("TypeError", "setTransform: 6 arguments required");
            return value::undefined();
        }
        std::array<double, 6> v{};
        if (!read_numbers(c, a, 6, v.data()) || !all_finite(v.data(), 6)) {
            return value::undefined();
        }
        canvas->set_transform(transform{f(v[0]), f(v[1]), f(v[2]), f(v[3]), f(v[4]), f(v[5])});
        return value::undefined();
    });
    canvas_operation(cx, proto, "getTransform", 0, [this, target](context & c, std::span<value>) {
        canvas_context * canvas = target(c);
        return canvas == nullptr ? value::undefined()
                                 : matrix_object(c, canvas->current_transform());
    });

    // --- rectangles -----------------------------------------------------------

    finite_op("fillRect", 4, [f](canvas_context & canvas, const std::array<double, 9> & v) {
        canvas.fill_rect(f(v[0]), f(v[1]), f(v[2]), f(v[3]));
    });
    finite_op("clearRect", 4, [f](canvas_context & canvas, const std::array<double, 9> & v) {
        canvas.clear_rect(f(v[0]), f(v[1]), f(v[2]), f(v[3]));
    });
    finite_op("strokeRect", 4, [f](canvas_context & canvas, const std::array<double, 9> & v) {
        canvas.stroke_rect(f(v[0]), f(v[1]), f(v[2]), f(v[3]));
    });

    // --- paths ----------------------------------------------------------------

    install_path_methods(cx, proto, throw_dom, [target](context & c, const path_verb & verb) {
        if (canvas_context * canvas = target(c)) {
            apply_verb(verb, canvas->path(), canvas->current_transform());
        }
    });

    // CanvasFillRule: "nonzero" or "evenodd", and anything else a TypeError.
    const auto fill_rule_of = [](context & c, value v) -> std::optional<canvas_context::fill_rule> {
        if (v.is_undefined()) { return canvas_context::fill_rule::nonzero; }
        const std::string text = c.to_string(v);
        if (c.throw_pending()) { return std::nullopt; }
        if (text == "nonzero") { return canvas_context::fill_rule::nonzero; }
        if (text == "evenodd") { return canvas_context::fill_rule::even_odd; }
        c.throw_error("TypeError", "'" + text + "' is not a valid CanvasFillRule");
        return std::nullopt;
    };
    // The overloads `f(rule)` and `f(path, rule)`: a Path2D is replayed through
    // the current transform into a temporary path, which does not replace the
    // context's current default path; without one, the default path is used.
    const auto with_path = [target, fill_rule_of](auto body) {
        return [target, fill_rule_of, body](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            if (const auto recording = unbox<path_recording>(arg(a, 0), path_key)) {
                const auto rule = fill_rule_of(c, arg(a, 1));
                if (!rule) { return value::undefined(); }
                canvas_path path;
                replay(*recording, path, canvas->current_transform());
                body(*canvas, &path, *rule);
                return value::undefined();
            }
            const auto rule = fill_rule_of(c, arg(a, 0));
            if (!rule) { return value::undefined(); }
            body(*canvas, nullptr, *rule);
            return value::undefined();
        };
    };
    canvas_operation(cx, proto, "fill", 0,
                     with_path([](canvas_context & canvas, const canvas_path * path,
                                  canvas_context::fill_rule rule) {
                         canvas.fill(path != nullptr ? *path : canvas.path(), rule);
                     }));
    canvas_operation(cx, proto, "clip", 0,
                     with_path([](canvas_context & canvas, const canvas_path * path,
                                  canvas_context::fill_rule rule) {
                         canvas.clip(path != nullptr ? *path : canvas.path(), rule);
                     }));
    canvas_operation(cx, proto, "stroke", 0, [target](context & c, std::span<value> a) {
        canvas_context * canvas = target(c);
        if (canvas == nullptr) { return value::undefined(); }
        if (const auto recording = unbox<path_recording>(arg(a, 0), path_key)) {
            canvas_path path;
            replay(*recording, path, canvas->current_transform());
            canvas->stroke(path);
            return value::undefined();
        }
        canvas->stroke();
        return value::undefined();
    });
    // isPointInPath(x, y, rule) / isPointInPath(path, x, y, rule): the point
    // is in canvas pixels, untransformed; the path is the one that fill() would
    // fill.
    canvas_operation(
        cx, proto, "isPointInPath", 2, [target, fill_rule_of](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            const auto recording = unbox<path_recording>(arg(a, 0), path_key);
            const std::size_t at = recording ? 1 : 0;
            if (recording && a.size() < 3) {
                c.throw_error("TypeError", "isPointInPath: 3 arguments required");
                return value::undefined();
            }
            std::array<double, 2> p{};
            if (!read_numbers(c, a.subspan(at), 2, p.data())) { return value::undefined(); }
            const auto rule = fill_rule_of(c, arg(a, at + 2));
            if (!rule) { return value::undefined(); }
            if (!recording) {
                return value::boolean(canvas->is_point_in_path(canvas->path(), p[0], p[1], *rule));
            }
            canvas_path path;
            replay(*recording, path, canvas->current_transform());
            return value::boolean(canvas->is_point_in_path(path, p[0], p[1], *rule));
        });
    canvas_operation(cx, proto, "isPointInStroke", 2, [target](context & c, std::span<value> a) {
        canvas_context * canvas = target(c);
        if (canvas == nullptr) { return value::undefined(); }
        const auto recording = unbox<path_recording>(arg(a, 0), path_key);
        const std::size_t at = recording ? 1 : 0;
        if (recording && a.size() < 3) {
            c.throw_error("TypeError", "isPointInStroke: 3 arguments required");
            return value::undefined();
        }
        std::array<double, 2> p{};
        if (!read_numbers(c, a.subspan(at), 2, p.data())) { return value::undefined(); }
        if (!recording) {
            return value::boolean(canvas->is_point_in_stroke(canvas->path(), p[0], p[1]));
        }
        canvas_path path;
        replay(*recording, path, canvas->current_transform());
        return value::boolean(canvas->is_point_in_stroke(path, p[0], p[1]));
    });
    // drawFocusIfNeeded(element) / (path, element): the argument must be an
    // Element, and the ring is drawn only when that element is focused and is
    // the canvas's own fallback content, i.e. a descendant of the canvas.
    canvas_operation(
        cx, proto, "drawFocusIfNeeded", 1,
        [this, target, canvas_of](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            const auto recording = unbox<path_recording>(arg(a, 0), path_key);
            const value element = arg(a, recording ? 1 : 0);
            const node_id id = handle_of(element);
            bool inside = false;
            {
                const auto txn = doc_->read();
                if (!id || txn.kind(id).value_or(node_kind::text) != node_kind::element) {
                    c.throw_error("TypeError", "drawFocusIfNeeded: the argument is not an Element");
                    return value::undefined();
                }
                const node_id host = canvas_of(c);
                for (node_id up = txn.parent(id); up && !inside;) {
                    inside = up == host;
                    const node_id next = txn.parent(up);
                    if (next == up) { break; }
                    up = next;
                }
            }
            if (!inside || focused_ != id) { return value::undefined(); }
            if (recording) {
                canvas_path path;
                replay(*recording, path, canvas->current_transform());
                canvas->draw_focus_ring(path);
            } else {
                canvas->draw_focus_ring(canvas->path());
            }
            return value::undefined();
        });

    // setLineDash(segments): every entry must be finite and non-negative, or
    // the call does nothing; an odd-length list is doubled. getLineDash
    // returns a copy.
    canvas_operation(cx, proto, "setLineDash", 1, [target](context & c, std::span<value> a) {
        canvas_context * canvas = target(c);
        if (canvas == nullptr) { return value::undefined(); }
        const value segments = arg(a, 0);
        if (!segments.is_object_like()) {
            c.throw_error("TypeError", "setLineDash: the argument is not a sequence");
            return value::undefined();
        }
        const value items = c.iterable_values(segments);
        if (c.throw_pending() || !items.is_array()) { return value::undefined(); }
        const std::vector<value> list = static_cast<script::array_object *>(items.as_heap())->items;
        const context::rooted_values keep{c, list};
        std::vector<double> dash;
        for (const value & item : list) {
            const double d = c.to_number_value(item);
            if (c.throw_pending()) { return value::undefined(); }
            if (!std::isfinite(d) || d < 0) { return value::undefined(); }
            dash.push_back(d);
        }
        if (dash.size() % 2 != 0) { dash.insert(dash.end(), dash.begin(), dash.end()); }
        canvas->current().dash = std::move(dash);
        return value::undefined();
    });
    canvas_operation(cx, proto, "getLineDash", 0, [target](context & c, std::span<value>) {
        canvas_context * canvas = target(c);
        if (canvas == nullptr) { return value::undefined(); }
        const value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const double d : canvas->current().dash) { items->items.push_back(value::number(d)); }
        return out;
    });

    // --- gradients and patterns -------------------------------------------------

    const auto make_gradient = [gradient_proto](context & c, std::shared_ptr<canvas_gradient> g) {
        auto * made = c.allocate<script::object_object>();
        made->prototype = value::object(gradient_proto);
        box(c, *made, gradient_key, std::move(g));
        return value::object(made);
    };
    // The arguments are `double`, not `unrestricted double`: a non-finite one
    // is a TypeError here rather than a silent no-op.
    const auto gradient_numbers = [](context & c, std::span<value> a, std::size_t count,
                                     double * out) {
        if (!read_numbers(c, a, count, out)) { return false; }
        if (!all_finite(out, count)) {
            c.throw_error("TypeError", "A gradient coordinate is not finite");
            return false;
        }
        return true;
    };
    canvas_operation(cx, proto, "createLinearGradient", 4,
                     [target, make_gradient, gradient_numbers](context & c, std::span<value> a) {
                         if (target(c) == nullptr) { return value::undefined(); }
                         std::array<double, 4> v{};
                         if (!gradient_numbers(c, a, 4, v.data())) { return value::undefined(); }
                         auto g = std::make_shared<canvas_gradient>();
                         g->shape = canvas_gradient::kind::linear;
                         g->x0 = v[0];
                         g->y0 = v[1];
                         g->x1 = v[2];
                         g->y1 = v[3];
                         return make_gradient(c, std::move(g));
                     });
    canvas_operation(
        cx, proto, "createRadialGradient", 6,
        [target, make_gradient, gradient_numbers, throw_dom](context & c, std::span<value> a) {
            if (target(c) == nullptr) { return value::undefined(); }
            std::array<double, 6> v{};
            if (!gradient_numbers(c, a, 6, v.data())) { return value::undefined(); }
            if (v[2] < 0 || v[5] < 0) {
                throw_dom(c, "IndexSizeError", "createRadialGradient: a radius is negative");
                return value::undefined();
            }
            auto g = std::make_shared<canvas_gradient>();
            g->shape = canvas_gradient::kind::radial;
            g->x0 = v[0];
            g->y0 = v[1];
            g->r0 = v[2];
            g->x1 = v[3];
            g->y1 = v[4];
            g->r1 = v[5];
            return make_gradient(c, std::move(g));
        });
    canvas_operation(cx, proto, "createConicGradient", 3,
                     [target, make_gradient, gradient_numbers](context & c, std::span<value> a) {
                         if (target(c) == nullptr) { return value::undefined(); }
                         std::array<double, 3> v{};
                         if (!gradient_numbers(c, a, 3, v.data())) { return value::undefined(); }
                         auto g = std::make_shared<canvas_gradient>();
                         g->shape = canvas_gradient::kind::conic;
                         g->angle = v[0];
                         g->x0 = v[1];
                         g->y0 = v[2];
                         return make_gradient(c, std::move(g));
                     });
    // addColorStop(offset, color): an offset outside [0, 1] is an
    // IndexSizeError and a colour that does not parse a SyntaxError.
    // `currentcolor` has no element to resolve against here, so it is black.
    canvas_operation(
        cx, *gradient_proto, "addColorStop", 2, [throw_dom](context & c, std::span<value> a) {
            const auto gradient = unbox<canvas_gradient>(c.current_this(), gradient_key);
            if (!gradient) {
                c.throw_error("TypeError", "Illegal invocation: not a CanvasGradient");
                return value::undefined();
            }
            double offset = 0;
            if (!read_numbers(c, a, 1, &offset)) { return value::undefined(); }
            if (!std::isfinite(offset)) {
                c.throw_error("TypeError", "addColorStop: the offset is not finite");
                return value::undefined();
            }
            const std::string text = c.to_string(arg(a, 1));
            if (c.throw_pending()) { return value::undefined(); }
            if (offset < 0 || offset > 1) {
                throw_dom(c, "IndexSizeError", "addColorStop: the offset is outside 0..1");
                return value::undefined();
            }
            const std::optional<parsed_color> parsed = parse_canvas_color(text, "rgb(0, 0, 0)");
            if (!parsed) {
                throw_dom(c, "SyntaxError", "addColorStop: '" + text + "' is not a colour");
                return value::undefined();
            }
            gradient->add_stop(offset, parsed->value);
            return value::undefined();
        });
    canvas_operation(cx, *pattern_proto, "setTransform", 0, [](context & c, std::span<value> a) {
        const auto pattern = unbox<canvas_pattern>(c.current_this(), pattern_key);
        if (!pattern) {
            c.throw_error("TypeError", "Illegal invocation: not a CanvasPattern");
            return value::undefined();
        }
        if (const std::optional<transform> m = matrix_2d(c, arg(a, 0))) { pattern->matrix = *m; }
        return value::undefined();
    });

    // Returns a CanvasImageSource's pixels, or throws why there are none: a
    // TypeError for a value that is not an image source, an InvalidStateError
    // for a canvas with no area or a broken image. nullptr with nothing thrown
    // means "draw nothing".
    const auto image_source = [this, throw_dom](context & c,
                                                value v) -> std::shared_ptr<const paint::bitmap> {
        if (v.is_number()) { return image_argument(v); } // a loadImage() handle
        if (unbox<canvas_pattern>(v, pattern_key) || !v.is_object()) {
            c.throw_error("TypeError", "The argument is not a CanvasImageSource");
            return nullptr;
        }
        auto * obj = static_cast<script::object_object *>(v.as_heap());
        if (obj->find(image_bitmap_property) != nullptr) { return image_argument(v); }
        const node_id id = handle_of(v);
        std::string tag;
        if (id) {
            const auto txn = doc_->read();
            tag = std::string{atoms_->text(txn.tag(id).value_or(atom{}))};
        }
        if (tag == "canvas") {
            const auto pixels = image_argument(v);
            const auto txn = doc_->read();
            if (size_attribute(txn, id, "width", 300) == 0 ||
                size_attribute(txn, id, "height", 150) == 0) {
                throw_dom(c, "InvalidStateError", "The canvas has no area");
                return nullptr;
            }
            if (pixels) { return pixels; }
            // A canvas nobody has drawn on is transparent black at its size.
            return std::make_shared<const paint::bitmap>(
                static_cast<int>(size_attribute(txn, id, "width", 300)),
                static_cast<int>(size_attribute(txn, id, "height", 150)));
        }
        if (tag == "img" || tag == "image") {
            // An image with no source to fetch is not broken, merely not
            // available: it draws nothing and makes no pattern (WPT
            // 2d.pattern.image.incomplete.*). One whose fetch or decode failed
            // is broken, which is the InvalidStateError.
            {
                const auto txn = doc_->read();
                const std::string_view src =
                    trim(txn.attribute_value(id, atoms_->intern("src")), html_whitespace);
                if (src.empty()) { return nullptr; }
            }
            // This engine decodes synchronously, so the pixels are available
            // as soon as `src` is set (which is also why `img.complete` is
            // already true), and an image that decodes draws whether or not
            // its load event has fired. One that does not decode is broken
            // only once its load has been delivered; while that is still
            // queued it is merely not yet "fully decodable" and draws nothing
            // (WPT 2d.drawImage.incomplete.*).
            const auto pixels = image_argument(v);
            if (pixels) { return pixels; }
            for (const pending_image & loading : image_loads_) {
                if (loading.id == id) { return nullptr; }
            }
            throw_dom(c, "InvalidStateError", "The image is broken");
            return nullptr;
        }
        if (tag == "video") { return nullptr; }
        c.throw_error("TypeError", "The argument is not a CanvasImageSource");
        return nullptr;
    };
    canvas_operation(
        cx, proto, "createPattern", 2,
        [target, image_source, throw_dom, pattern_proto](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            std::shared_ptr<const paint::bitmap> pixels = image_source(c, arg(a, 0));
            if (c.throw_pending()) { return value::undefined(); }
            const value repetition_value = arg(a, 1);
            const std::string repetition =
                repetition_value.is_null() ? std::string{} : c.to_string(repetition_value);
            if (c.throw_pending()) { return value::undefined(); }
            // Check the image's usability first: one that is not ready makes no
            // pattern, whatever the repetition says (HTML 4.12.5.1.10 steps
            // 1-2).
            if (!pixels || pixels->empty()) { return value::null(); }
            auto pattern = std::make_shared<canvas_pattern>();
            if (repetition == "repeat-x") {
                pattern->repeat_y = false;
            } else if (repetition == "repeat-y") {
                pattern->repeat_x = false;
            } else if (repetition == "no-repeat") {
                pattern->repeat_x = pattern->repeat_y = false;
            } else if (!repetition.empty() && repetition != "repeat") {
                throw_dom(c, "SyntaxError",
                          "createPattern: '" + repetition + "' is not a repetition");
                return value::undefined();
            }
            // Copy the pixels so that drawing on a source canvas afterwards does
            // not change a pattern already made from it.
            pattern->image = std::make_shared<const paint::bitmap>(*pixels);
            auto * made = c.allocate<script::object_object>();
            made->prototype = value::object(pattern_proto);
            box(c, *made, pattern_key, std::move(pattern));
            return value::object(made);
        });

    // drawImage(image, dx, dy), (image, dx, dy, dw, dh) or (image, sx, sy,
    // sw, sh, dx, dy, dw, dh). Any other argument count matches no overload.
    canvas_operation(
        cx, proto, "drawImage", 3, [target, image_source](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            const std::size_t count = a.size() >= 9 ? 9 : a.size();
            if (count != 3 && count != 5 && count != 9) {
                c.throw_error("TypeError", "drawImage: no overload takes " +
                                               std::to_string(a.size()) + " arguments");
                return value::undefined();
            }
            std::shared_ptr<const paint::bitmap> source = image_source(c, arg(a, 0));
            if (c.throw_pending()) { return value::undefined(); }
            std::array<double, 8> v{};
            if (!read_numbers(c, a.subspan(1), count - 1, v.data()) ||
                !all_finite(v.data(), count - 1) || !source) {
                return value::undefined();
            }
            // A canvas drawn onto itself reads what it is writing: copy it first.
            if (source == canvas->surface()) {
                source = std::make_shared<const paint::bitmap>(*source);
            }
            const auto w = static_cast<double>(source->width);
            const auto h = static_cast<double>(source->height);
            if (count == 3) {
                canvas->draw_image(source, 0, 0, w, h, v[0], v[1], w, h);
            } else if (count == 5) {
                canvas->draw_image(source, 0, 0, w, h, v[0], v[1], v[2], v[3]);
            } else {
                canvas->draw_image(source, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
            }
            return value::undefined();
        });

    // --- text -------------------------------------------------------------------

    // fillText and strokeText(text, x, y [, maxWidth]) do nothing for a
    // non-finite coordinate or a maxWidth that is not positive. An undefined
    // maxWidth counts as missing (WebIDL: an optional argument with no
    // default); Chart.js passes `opts.maxWidth` on every label, and it is
    // usually undefined.
    const auto text_op = [&](const std::string & name, bool stroke) {
        canvas_operation(cx, proto, name, 3, [target, stroke](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            const std::string text = c.to_string(arg(a, 0));
            if (c.throw_pending()) { return value::undefined(); }
            std::array<double, 3> v{};
            const std::size_t count = a.size() > 3 && !a[3].is_undefined() ? 3 : 2;
            if (!read_numbers(c, a.subspan(1), count, v.data()) || !all_finite(v.data(), count)) {
                return value::undefined();
            }
            if (count == 3 && !(v[2] > 0)) { return value::undefined(); }
            const auto x = static_cast<float>(v[0]);
            const auto y = static_cast<float>(v[1]);
            if (stroke) {
                canvas->stroke_text(text, x, y);
            } else {
                canvas->fill_text(text, x, y);
            }
            return value::undefined();
        });
    };
    text_op("fillText", false);
    text_op("strokeText", true);
    // measureText: the advance width and the bounding box, both from the same
    // code that draws text (see docs/raster.md). p5.js measures a line as
    // actualBoundingBoxLeft + actualBoundingBoxRight, so the box is required.
    canvas_operation(
        cx, proto, "measureText", 1, [target, metrics_proto](context & c, std::span<value> a) {
            canvas_context * canvas = target(c);
            if (canvas == nullptr) { return value::undefined(); }
            const std::string text = c.to_string(arg(a, 0));
            if (c.throw_pending()) { return value::undefined(); }
            auto * metrics = c.allocate<script::object_object>();
            metrics->prototype = value::object(metrics_proto);
            const canvas_context::state & s = canvas->current();
            const double width = static_cast<double>(canvas->measure_text(text));
            metrics->set("width", value::number(width));
            // The box is measured from the alignment point, so which side of it
            // the run falls on depends on textAlign; left + right always equals
            // the width.
            const bool rtl = s.direction == "rtl";
            double left = 0;
            if (s.text_align == "center") {
                left = width / 2;
            } else if (s.text_align == "right" || (s.text_align == "end" && !rtl) ||
                       (s.text_align == "start" && rtl)) {
                left = width;
            }
            metrics->set("actualBoundingBoxLeft", value::number(left));
            metrics->set("actualBoundingBoxRight", value::number(width - left));
            // The font's ascent and descent stand in for the glyphs' own
            // extents: an over-estimate for a run with no ascenders, which is
            // the safe side.
            const auto & backend = canvas->fonts();
            const auto ascent = static_cast<double>(
                backend.ascent(s.font_size, s.font_family, s.font_bold, s.font_italic));
            const auto descent = static_cast<double>(
                backend.descent(s.font_size, s.font_family, s.font_bold, s.font_italic));
            metrics->set("actualBoundingBoxAscent", value::number(ascent));
            metrics->set("actualBoundingBoxDescent", value::number(descent));
            metrics->set("fontBoundingBoxAscent", value::number(ascent));
            metrics->set("fontBoundingBoxDescent", value::number(descent));
            metrics->set("emHeightAscent", value::number(ascent));
            metrics->set("emHeightDescent", value::number(descent));
            metrics->set("hangingBaseline", value::number(ascent * 0.8));
            metrics->set("alphabeticBaseline", value::number(0));
            metrics->set("ideographicBaseline", value::number(-descent));
            return value::object(metrics);
        });

    // --- the pixels, as bytes
    //
    // getImageData/putImageData are how a page reads what it drew and writes
    // back something it computed (filters, colour picking, p5.js `pixels[]`
    // loops). The buffer is RGBA bytes in that order, which is not the
    // engine's packed ARGB, so both directions convert per channel rather than
    // copying memory; getting that wrong swaps red and blue.
    canvas_operation(
        cx, proto, "createImageData", 1, [this, target](context & c, std::span<value> a) {
            if (target(c) == nullptr && c.throw_pending()) { return value::undefined(); }
            // The one-argument overload takes the other image's size, not its pixels.
            const bool given = a.size() == 1 && a[0].is_object();
            const auto width =
                detail::image_data_long(c, given ? c.lookup_property(a[0], "width") : arg(a, 0));
            if (!width) { return value::undefined(); }
            const auto height =
                detail::image_data_long(c, given ? c.lookup_property(a[0], "height") : arg(a, 1));
            if (!height) { return value::undefined(); }
            if (*width == 0 || *height == 0) {
                throw_dom_exception(c, "IndexSizeError", "ImageData dimensions must be nonzero");
                return value::undefined();
            }
            return detail::make_image_data(
                c, static_cast<std::uint64_t>(std::abs(std::int64_t{*width})),
                static_cast<std::uint64_t>(std::abs(std::int64_t{*height})));
        });
    canvas_operation(cx, proto, "getImageData", 4, [this, target](context & c, std::span<value> a) {
        canvas_context * canvas = target(c);
        if (canvas == nullptr) { return value::undefined(); }
        const auto sx = detail::image_data_long(c, arg(a, 0));
        if (!sx) { return value::undefined(); }
        const auto sy = detail::image_data_long(c, arg(a, 1));
        if (!sy) { return value::undefined(); }
        const auto sw = detail::image_data_long(c, arg(a, 2));
        if (!sw) { return value::undefined(); }
        const auto sh = detail::image_data_long(c, arg(a, 3));
        if (!sh) { return value::undefined(); }
        if (*sw == 0 || *sh == 0) {
            throw_dom_exception(c, "IndexSizeError", "ImageData dimensions must be nonzero");
            return value::undefined();
        }
        // Negative extents select left/up from the origin; additions stay wide
        // until the intersection is known to lie within the actual surface.
        const std::int64_t x = std::int64_t{*sx} + std::min(0, *sw);
        const std::int64_t y = std::int64_t{*sy} + std::min(0, *sh);
        const auto width = std::abs(std::int64_t{*sw});
        const auto height = std::abs(std::int64_t{*sh});
        const value out = detail::make_image_data(c, static_cast<std::uint64_t>(width),
                                                  static_cast<std::uint64_t>(height));
        if (out.is_undefined()) { return out; }
        const auto & surface = canvas->surface();
        if (!surface) { return out; }
        auto * store =
            static_cast<script::array_object *>(c.lookup_property(out, "data").as_heap());
        for (auto row = std::max<std::int64_t>(0, y);
             row < std::min<std::int64_t>(surface->height, y + height); ++row) {
            for (auto column = std::max<std::int64_t>(0, x);
                 column < std::min<std::int64_t>(surface->width, x + width); ++column) {
                const color pixel{surface->at(static_cast<int>(column), static_cast<int>(row))};
                const auto at = static_cast<std::size_t>((row - y) * width + column - x) * 4;
                store->items[at] = value::number(pixel.red());
                store->items[at + 1] = value::number(pixel.green());
                store->items[at + 2] = value::number(pixel.blue());
                store->items[at + 3] = value::number(pixel.alpha());
            }
        }
        return out;
    });
    // putImageData(data, dx, dy [, dirtyX, dirtyY, dirtyWidth, dirtyHeight]):
    // writes the dirty rectangle, clipped to the data, directly, ignoring the
    // transform, clip, globalAlpha and composite operator (HTML 4.12.5.1.19).
    canvas_operation(cx, proto, "putImageData", 3, [this, target](context & c, std::span<value> a) {
        canvas_context * canvas = target(c);
        if (canvas == nullptr) { return value::undefined(); }
        if (a.size() != 3 && a.size() < 7) {
            c.throw_error("TypeError", "putImageData: 3 or 7 arguments required");
            return value::undefined();
        }
        // The argument must be a real ImageData, not just an object shaped like
        // one: WebIDL converts it to the interface, so a plain {width, height,
        // data} is a TypeError (WPT 2d.imageData.put.wrongtype). The check uses
        // the private brand every real ImageData carries, so a subclass
        // instance passes and reassigning `window.ImageData` has no effect.
        if (a.empty() || !a[0].is_object() ||
            static_cast<script::object_object *>(a[0].as_heap())->find(detail::image_data_brand) ==
                nullptr) {
            c.throw_error("TypeError", "putImageData: the argument is not an ImageData");
            return value::undefined();
        }
        const value source = c.lookup_property(a[0], "data");
        if (!source.is_array()) {
            c.throw_error("TypeError", "putImageData: the argument is not an ImageData");
            return value::undefined();
        }
        const auto * bytes = static_cast<script::array_object *>(source.as_heap());
        const auto width = detail::image_data_long(c, c.lookup_property(a[0], "width"));
        if (!width) { return value::undefined(); }
        const auto height = detail::image_data_long(c, c.lookup_property(a[0], "height"));
        if (!height) { return value::undefined(); }
        std::array<std::int64_t, 6> n{};
        for (std::size_t i = 0; i < (a.size() >= 7 ? 6U : 2U); ++i) {
            const auto got = detail::image_data_long(c, arg(a, i + 1));
            if (!got) { return value::undefined(); }
            n[i] = *got;
        }
        const auto length =
            detail::image_data_length(c, static_cast<std::uint64_t>(std::max(0, *width)),
                                      static_cast<std::uint64_t>(std::max(0, *height)));
        if (!length) { return value::undefined(); }
        if (*length == 0 || *length != bytes->length()) {
            throw_dom_exception(c, "InvalidStateError",
                                "ImageData dimensions do not match its data");
            return value::undefined();
        }
        std::int64_t dirty_x = 0, dirty_y = 0, dirty_w = *width, dirty_h = *height;
        if (a.size() >= 7) {
            dirty_x = n[2];
            dirty_y = n[3];
            dirty_w = n[4];
            dirty_h = n[5];
            if (dirty_w < 0) {
                dirty_x += dirty_w;
                dirty_w = -dirty_w;
            }
            if (dirty_h < 0) {
                dirty_y += dirty_h;
                dirty_h = -dirty_h;
            }
            if (dirty_x < 0) {
                dirty_w += dirty_x;
                dirty_x = 0;
            }
            if (dirty_y < 0) {
                dirty_h += dirty_y;
                dirty_y = 0;
            }
            dirty_w = std::min<std::int64_t>(dirty_w, *width - dirty_x);
            dirty_h = std::min<std::int64_t>(dirty_h, *height - dirty_y);
            if (dirty_w <= 0 || dirty_h <= 0) { return value::undefined(); }
        }
        const auto & surface = canvas->surface();
        if (!surface) { return value::undefined(); }
        const std::int64_t dx = n[0];
        const std::int64_t dy = n[1];
        for (std::int64_t row = dirty_y; row < dirty_y + dirty_h; ++row) {
            const std::int64_t to_y = dy + row;
            if (to_y < 0 || to_y >= surface->height) { continue; }
            for (std::int64_t column = dirty_x; column < dirty_x + dirty_w; ++column) {
                const std::int64_t to_x = dx + column;
                if (to_x < 0 || to_x >= surface->width) { continue; }
                const auto at = static_cast<std::size_t>(row * *width + column) * 4;
                const auto channel = [&](std::size_t offset) {
                    const double number = bytes->is_view()
                                              ? script::view_get(*bytes, at + offset)
                                              : context::to_number(bytes->items[at + offset]);
                    return static_cast<std::uint8_t>(
                        script::coerce_element(script::element_kind::u8_clamped, number));
                };
                surface->pixels[static_cast<std::size_t>(to_y) *
                                    static_cast<std::size_t>(surface->width) +
                                static_cast<std::size_t>(to_x)] =
                    color::rgba(channel(0), channel(1), channel(2), channel(3)).argb;
            }
        }
        canvas->note_foreign_draw();
        return value::undefined();
    });
}

// Applies `font` with relative sizes resolved against the 10px default, for
// callers that have no element to resolve against.
bool dom_bindings::apply_canvas_font(canvas_context & canvas, std::string_view font) {
    if (const auto serialized = apply_font(canvas.current(), font, [] { return 10.0f; })) {
        canvas.current().font_spec = *serialized;
        return true;
    }
    return false;
}

} // namespace ctbrowser::shell
