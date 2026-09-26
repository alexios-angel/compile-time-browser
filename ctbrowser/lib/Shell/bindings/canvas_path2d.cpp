// dom_bindings - CanvasPath (the path verbs) and Path2D.

#include "canvas_objects.hpp"

#include <cmath>

// The verbs are PARSED here once, for both of their homes: WebIDL's argument
// conversions (ToNumber, which may throw), then the spec's own steps - a
// non-finite argument does nothing, a negative radius is an IndexSizeError,
// roundRect's radii are a RangeError when there are too many - and then the
// verb goes to a sink. The context's sink builds its current path through the
// current transform; a Path2D's records it to be replayed at draw time.

namespace ctbrowser::shell::canvas_detail {

namespace {

// A WebIDL operation: `length` is the required count and a call with fewer
// arguments is a TypeError.
void operation(script::context & cx, script::object_object & proto, const std::string & name,
               unsigned required, script::native_fn fn) {
    set_operation(cx, proto, name, required, std::move(fn));
    proto.set_attrs(name,
                    script::attr_enumerable | script::attr_configurable | script::attr_writable);
}

// One DOMPointInit's x and y (WebIDL dictionary: members read in
// lexicographic order, each ToNumber with its default).
[[nodiscard]] bool point_init(script::context & c, script::value init, corner_radius & out) {
    out = corner_radius{};
    if (init.is_null() || init.is_undefined()) { return true; }
    for (const char * member : {"w", "x", "y", "z"}) {
        const script::value held = c.lookup_property(init, member);
        if (c.throw_pending()) { return false; }
        if (held.is_undefined()) { continue; }
        const double number = c.to_number_value(held);
        if (c.throw_pending()) { return false; }
        if (member[0] == 'x') { out.x = number; }
        if (member[0] == 'y') { out.y = number; }
    }
    return true;
}

// roundRect's radii, the union `(unrestricted double or DOMPointInit or
// sequence<(unrestricted double or DOMPointInit)>)`, converted. Each entry
// is a (x, y) pair plus whether it came as one number.
struct radius_entry {
    corner_radius r;
    bool from_number = false;
};
[[nodiscard]] bool radius_union(script::context & c, script::value v,
                                std::vector<radius_entry> & out) {
    const auto one = [&](script::value item) {
        radius_entry entry;
        if (item.is_object_like() || item.is_null() || item.is_undefined()) {
            if (!point_init(c, item, entry.r)) { return false; }
        } else {
            const double number = c.to_number_value(item);
            if (c.throw_pending()) { return false; }
            entry.r = corner_radius{number, number};
            entry.from_number = true;
        }
        out.push_back(entry);
        return true;
    };
    if (v.is_undefined()) {
        out.push_back(radius_entry{corner_radius{}, true});
        return true;
    }
    if (v.is_object_like()) {
        const script::value iterator =
            c.lookup_property(v, std::string{CTBROWSER_SYMBOL_KEY("iterator")});
        if (c.throw_pending()) { return false; }
        if (iterator.is_callable()) {
            const script::value items = c.iterable_values(v);
            if (c.throw_pending() || !items.is_array()) { return false; }
            const std::vector<script::value> list =
                static_cast<script::array_object *>(items.as_heap())->items;
            const script::context::rooted_values keep{c, list};
            for (const script::value & item : list) {
                if (!one(item)) { return false; }
            }
            return true;
        }
    }
    return one(v);
}

} // namespace

void apply_verb(const path_verb & verb, canvas_path & into, const transform & ctm) {
    path_builder b{into, verb.m.then(ctm)};
    const auto & v = verb.v;
    switch (verb.op) {
    case 'M': b.move_to(v[0], v[1]); break;
    case 'L': b.line_to(v[0], v[1]); break;
    case 'Z': b.close_path(); break;
    case 'Q': b.quadratic_curve_to(v[0], v[1], v[2], v[3]); break;
    case 'C': b.bezier_curve_to(v[0], v[1], v[2], v[3], v[4], v[5]); break;
    case 'T': b.arc_to(v[0], v[1], v[2], v[3], v[4]); break;
    case 'R': b.rect(v[0], v[1], v[2], v[3]); break;
    case 'O':
        b.round_rect(v[0], v[1], v[2], v[3],
                     {corner_radius{v[4], v[5]}, corner_radius{v[6], v[7]},
                      corner_radius{v[8], v[9]}, corner_radius{v[10], v[11]}});
        break;
    case 'A': b.arc(v[0], v[1], v[2], v[3], v[4], verb.anticlockwise); break;
    case 'E': b.ellipse(v[0], v[1], v[2], v[3], v[4], v[5], v[6], verb.anticlockwise); break;
    default: break;
    }
}

void replay(const path_recording & recording, canvas_path & into, const transform & ctm) {
    for (const path_verb & verb : recording.verbs) { apply_verb(verb, into, ctm); }
}

std::optional<transform> matrix_2d(script::context & c, script::value init) {
    transform out;
    if (init.is_undefined() || init.is_null()) { return out; }
    if (!init.is_object_like()) {
        c.throw_error("TypeError", "The matrix is not a DOMMatrix2DInit");
        return std::nullopt;
    }
    // Each of a..f may be given under its m11..m42 alias, and a pair that is
    // given twice has to agree (Geometry Interfaces 1 "validate and fixup").
    struct member {
        const char * name;
        const char * alias;
        float * into;
    };
    const member members[] = {{"a", "m11", &out.a}, {"b", "m12", &out.b}, {"c", "m21", &out.c},
                              {"d", "m22", &out.d}, {"e", "m41", &out.e}, {"f", "m42", &out.f}};
    for (const member & m : members) {
        const script::value named = c.lookup_property(init, m.name);
        if (c.throw_pending()) { return std::nullopt; }
        const script::value aliased = c.lookup_property(init, m.alias);
        if (c.throw_pending()) { return std::nullopt; }
        std::optional<double> first;
        std::optional<double> second;
        if (!named.is_undefined()) {
            first = c.to_number_value(named);
            if (c.throw_pending()) { return std::nullopt; }
        }
        if (!aliased.is_undefined()) {
            second = c.to_number_value(aliased);
            if (c.throw_pending()) { return std::nullopt; }
        }
        // The members are `unrestricted double`: a non-finite one is no
        // error, and every caller's answer to it is to do nothing
        // (2d.pattern.transform.infinity).
        if ((first && !std::isfinite(*first)) || (second && !std::isfinite(*second))) {
            return std::nullopt;
        }
        if (first && second && *first != *second && !(std::isnan(*first) && std::isnan(*second))) {
            c.throw_error("TypeError", std::string{"DOMMatrix2DInit "} + m.name + " and " +
                                           m.alias + " disagree");
            return std::nullopt;
        }
        if (second) { *m.into = static_cast<float>(*second); }
        if (first) { *m.into = static_cast<float>(*first); }
    }
    return out;
}

void install_path_methods(script::context & cx, script::object_object & proto,
                          const dom_thrower & throw_dom, const verb_sink & sink) {
    // The plain ones: N numbers, nothing done unless all are finite.
    const auto simple = [&](const char * name, char op, unsigned count) {
        operation(cx, proto, name, count,
                  [sink, op, count](script::context & c, std::span<script::value> a) {
                      path_verb verb;
                      verb.op = op;
                      if (!read_numbers(c, a, count, verb.v.data())) {
                          return script::value::undefined();
                      }
                      if (all_finite(verb.v.data(), count)) { sink(c, verb); }
                      return script::value::undefined();
                  });
    };
    operation(cx, proto, "closePath", 0, [sink](script::context & c, std::span<script::value>) {
        path_verb verb;
        verb.op = 'Z';
        sink(c, verb);
        return script::value::undefined();
    });
    simple("moveTo", 'M', 2);
    simple("lineTo", 'L', 2);
    simple("quadraticCurveTo", 'Q', 4);
    simple("bezierCurveTo", 'C', 6);
    simple("rect", 'R', 4);
    operation(cx, proto, "arcTo", 5,
              [sink, throw_dom](script::context & c, std::span<script::value> a) {
                  path_verb verb;
                  verb.op = 'T';
                  if (!read_numbers(c, a, 5, verb.v.data()) || !all_finite(verb.v.data(), 5)) {
                      return script::value::undefined();
                  }
                  if (verb.v[4] < 0) {
                      throw_dom(c, "IndexSizeError", "arcTo: the radius is negative");
                      return script::value::undefined();
                  }
                  sink(c, verb);
                  return script::value::undefined();
              });
    operation(cx, proto, "arc", 5,
              [sink, throw_dom](script::context & c, std::span<script::value> a) {
                  path_verb verb;
                  verb.op = 'A';
                  if (!read_numbers(c, a, 5, verb.v.data())) { return script::value::undefined(); }
                  verb.anticlockwise = script::context::truthy(arg(a, 5));
                  if (!all_finite(verb.v.data(), 5)) { return script::value::undefined(); }
                  if (verb.v[2] < 0) {
                      throw_dom(c, "IndexSizeError", "arc: the radius is negative");
                      return script::value::undefined();
                  }
                  sink(c, verb);
                  return script::value::undefined();
              });
    operation(cx, proto, "ellipse", 7,
              [sink, throw_dom](script::context & c, std::span<script::value> a) {
                  path_verb verb;
                  verb.op = 'E';
                  if (!read_numbers(c, a, 7, verb.v.data())) { return script::value::undefined(); }
                  verb.anticlockwise = script::context::truthy(arg(a, 7));
                  if (!all_finite(verb.v.data(), 7)) { return script::value::undefined(); }
                  if (verb.v[2] < 0 || verb.v[3] < 0) {
                      throw_dom(c, "IndexSizeError", "ellipse: a radius is negative");
                      return script::value::undefined();
                  }
                  sink(c, verb);
                  return script::value::undefined();
              });
    // roundRect(x, y, w, h, radii = 0) - 4.12.5.1.14's whole algorithm.
    operation(cx, proto, "roundRect", 4, [sink](script::context & c, std::span<script::value> a) {
        path_verb verb;
        verb.op = 'O';
        if (!read_numbers(c, a, 4, verb.v.data())) { return script::value::undefined(); }
        std::vector<radius_entry> radii;
        if (!radius_union(c, arg(a, 4), radii)) { return script::value::undefined(); }
        if (!all_finite(verb.v.data(), 4)) { return script::value::undefined(); }
        if (radii.empty() || radii.size() > 4) {
            c.throw_error("RangeError", "roundRect: radii must hold one to four entries");
            return script::value::undefined();
        }
        for (const radius_entry & entry : radii) {
            if (!std::isfinite(entry.r.x) || !std::isfinite(entry.r.y)) {
                return script::value::undefined();
            }
            if (entry.r.x < 0 || entry.r.y < 0) {
                c.throw_error("RangeError", "roundRect: a radius is negative");
                return script::value::undefined();
            }
        }
        // Which entry is which corner: top-left, top-right, bottom-right,
        // bottom-left, with the missing ones taken from the opposite side.
        std::array<corner_radius, 4> corner{};
        switch (radii.size()) {
        case 4: corner = {radii[0].r, radii[1].r, radii[2].r, radii[3].r}; break;
        case 3: corner = {radii[0].r, radii[1].r, radii[2].r, radii[1].r}; break;
        case 2: corner = {radii[0].r, radii[1].r, radii[0].r, radii[1].r}; break;
        default: corner = {radii[0].r, radii[0].r, radii[0].r, radii[0].r}; break;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            verb.v[4 + 2 * i] = corner[i].x;
            verb.v[5 + 2 * i] = corner[i].y;
        }
        sink(c, verb);
        return script::value::undefined();
    });
}

// --- the context's settings ---------------------------------------------------

script::value context_settings(script::context & c, script::value options) {
    // WebIDL: undefined and null are the empty dictionary; members in
    // lexicographic order, each with its default.
    const auto member = [&](const char * name) {
        return options.is_object_like() ? c.lookup_property(options, name)
                                        : script::value::undefined();
    };
    const auto flag = [&](const char * name, bool fallback) {
        const script::value v = member(name);
        return v.is_undefined() ? fallback : script::context::truthy(v);
    };
    const auto keyword = [&](script::value v, const char * fallback,
                             std::initializer_list<std::string_view> allowed) -> std::string {
        if (v.is_undefined()) { return fallback; }
        std::string text = c.to_string(v);
        if (c.throw_pending()) { return {}; }
        if (std::ranges::find(allowed, std::string_view{text}) == allowed.end()) {
            c.throw_error("TypeError", "'" + text + "' is not a valid enumeration value");
            return {};
        }
        return text;
    };
    auto * out = c.allocate<script::object_object>();
    out->set("alpha", script::value::boolean(flag("alpha", true)));
    if (c.throw_pending()) { return script::value::undefined(); }
    const std::string space = keyword(member("colorSpace"), "srgb",
                                      {"srgb", "display-p3", "srgb-linear", "display-p3-linear"});
    if (c.throw_pending()) { return script::value::undefined(); }
    out->set("colorSpace", c.string(space));
    const std::string type = keyword(member("colorType"), "unorm8", {"unorm8", "float16"});
    if (c.throw_pending()) { return script::value::undefined(); }
    out->set("colorType", c.string(type));
    out->set("desynchronized", script::value::boolean(flag("desynchronized", false)));
    if (c.throw_pending()) { return script::value::undefined(); }
    const script::value tone = member("toneMapping");
    if (c.throw_pending()) { return script::value::undefined(); }
    const script::value mode =
        tone.is_object_like() ? c.lookup_property(tone, "mode") : script::value::undefined();
    if (c.throw_pending()) { return script::value::undefined(); }
    const std::string mapping = keyword(mode, "standard", {"standard", "extended"});
    if (c.throw_pending()) { return script::value::undefined(); }
    out->set("toneMapping", c.string(mapping));
    out->set("willReadFrequently", script::value::boolean(flag("willReadFrequently", false)));
    if (c.throw_pending()) { return script::value::undefined(); }
    return script::value::object(out);
}

// --- Path2D -------------------------------------------------------------------

void install_path2d(script::context & cx, const dom_thrower & throw_dom) {
    auto * proto = cx.allocate<script::object_object>();
    const script::value proto_value = script::value::object(proto);
    // ROOTED until the global holds it. Nothing reaches the prototype while
    // the constructor and a dozen methods are allocated around it, and the
    // collector took it: every Path2D then had a freed object for a
    // prototype, and `path.moveTo` read whatever lived there next.
    const script::context::rooted keep{cx, proto_value};
    // `new Path2D()`, `new Path2D(path)` - a copy - or `new Path2D(d)`, SVG
    // path data. The last is not parsed here and makes an empty path.
    auto * ctor = cx.allocate<script::native_object>(
        "Path2D", [proto_value](script::context & c, std::span<script::value> args) {
            auto * path = c.allocate<script::object_object>();
            path->prototype = proto_value;
            auto recording = std::make_shared<path_recording>();
            if (const auto source = unbox<path_recording>(arg(args, 0), path_key)) {
                recording->verbs = source->verbs;
            }
            box(c, *path, path_key, std::move(recording));
            return script::value::object(path);
        });
    ctor->define("prototype", proto_value, script::attr_none);
    // The lambda above holds the prototype too, and a capture is invisible to
    // the collector: retained, so the constructor keeps it alive whatever
    // becomes of its `prototype` property.
    ctor->retained.push_back(proto_value);
    ctor->define("length", script::value::number(0), script::attr_configurable);
    proto->define("constructor", script::value::object(ctor),
                  script::attr_writable | script::attr_configurable);
    proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("Path2D"),
                  script::attr_configurable);
    cx.define_builtin_global("Path2D", script::value::object(ctor));

    install_path_methods(cx, *proto, throw_dom, [](script::context & c, const path_verb & verb) {
        if (const auto path = unbox<path_recording>(c.current_this(), path_key)) {
            path->verbs.push_back(verb);
        }
    });
    // `addPath(path, transform)` - the other path's verbs, each under the
    // matrix as well as whatever it was already under.
    operation(cx, *proto, "addPath", 1, [](script::context & c, std::span<script::value> a) {
        const auto into = unbox<path_recording>(c.current_this(), path_key);
        const auto from = unbox<path_recording>(arg(a, 0), path_key);
        if (!from) {
            c.throw_error("TypeError", "addPath: the argument is not a Path2D");
            return script::value::undefined();
        }
        const std::optional<transform> matrix = matrix_2d(c, arg(a, 1));
        if (!matrix || !into) { return script::value::undefined(); }
        const std::vector<path_verb> verbs = from->verbs; // `p.addPath(p)` is allowed
        for (path_verb verb : verbs) {
            verb.m = verb.m.then(*matrix);
            into->verbs.push_back(verb);
        }
        return script::value::undefined();
    });
}

} // namespace ctbrowser::shell::canvas_detail
