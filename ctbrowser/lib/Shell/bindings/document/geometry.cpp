// dom_bindings - CSSOM View §5 on the Document: `scrollingElement`,
// `elementFromPoint`, `elementsFromPoint` and `caretPositionFromPoint`.

#include "internal.hpp"

#include <ctbrowser/layout/overflow.hpp>

#include <limits>

namespace ctbrowser::shell {

using namespace detail;

// The hit test over the FRAGMENT tree rather than the recorded display list
// the pointer path asks (browser::hit_test): a script asks between layout
// and paint, and the display list is stale then. Every fragment whose border
// box holds the point - in viewport coordinates, so scrolled containers and
// the page scroll are taken off, and clipped to every `overflow` ancestor's
// padding box - names its element, a text run naming its parent. Painted-
// last first: a positioned box sits above the in-flow content around it,
// later siblings above earlier ones and a box above its ancestors. The root
// closes the list, as §5 says.
//
// ponytail: no `z-index`, `pointer-events: none` or transform here; the
// stacking-context order the recorder computes is the upgrade when a test
// wants it.
std::vector<node_id> dom_bindings::elements_from_point(double x, double y, bool all) {
    std::vector<node_id> out;
    flush_layout();
    if (fragments_ == nullptr || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
        x > viewport_width_ || y > viewport_height_) {
        return out;
    }
    const point at{static_cast<float>(x), static_cast<float>(y)};
    const point viewport = viewport_scroll();
    // (paint rank, tree order) per hit: rank 1 for a positioned box and
    // everything inside it, 0 for in-flow content.
    struct hit {
        int rank;
        std::size_t order;
        node_id node;
    };
    std::vector<hit> hits;
    std::size_t order = 0;
    const auto txn = doc_->read();
    const auto walk = [&](auto && self, const layout::fragment & f, float dx, float dy, bool fixed,
                          int rank, std::optional<rect> clip) -> void {
        const bool is_fixed =
            fixed || (f.box != nullptr && f.box->position == layout::position_kind::fixed);
        rect box = f.absolute_bounds(dx, dy);
        if (!is_fixed) {
            box.x -= viewport.x;
            box.y -= viewport.y;
        }
        const int own_rank =
            std::max(rank, f.box != nullptr && (f.box->is_positioned() || is_fixed) ? 1 : 0);
        const std::size_t here = order++;
        const bool inside = box.contains(at) && (!clip || clip->contains(at));
        if (inside && f.source) {
            node_id named = f.source;
            if (txn.kind(named).value_or(node_kind::element) != node_kind::element) {
                named = txn.parent(named);
            }
            if (named && txn.kind(named).value_or(node_kind::element) == node_kind::element) {
                hits.push_back(hit{own_rank, here, named});
            }
        }
        // Into the children in the container's scrolled coordinates.
        point inner{f.bounds.x + dx, f.bounds.y + dy};
        std::optional<rect> inner_clip = clip;
        if (f.box != nullptr && f.source) {
            if (f.box->scroll_container) {
                const point offset = scroll_offset_of(f.source);
                inner.x -= offset.x;
                inner.y -= offset.y;
            }
            if (f.box->clips_overflow) {
                const rect pad = layout::padding_box_of(f);
                const rect region{box.x + pad.x, box.y + pad.y, pad.width, pad.height};
                inner_clip = clip ? clip->intersected(region) : region;
            }
        }
        for (const layout::fragment & child : f.children) {
            self(self, child, inner.x, inner.y, is_fixed, own_rank, inner_clip);
        }
    };
    walk(walk, *fragments_, 0, 0, false, 0, std::nullopt);
    std::ranges::stable_sort(hits, [](const hit & a, const hit & b) {
        return a.rank != b.rank ? a.rank > b.rank : a.order > b.order;
    });
    for (const hit & h : hits) {
        if (std::ranges::find(out, h.node) == out.end()) { out.push_back(h.node); }
        if (!all) { break; }
    }
    const node_id root = txn.root();
    if (root && (out.empty() || out.back() != root)) {
        if (!all && !out.empty()) { return out; }
        out.push_back(root);
    }
    return out;
}

// --- the geometry interfaces (Geometry Interfaces Module Level 1) ----------
//
// DOMPoint, DOMRect and DOMQuad with their ReadOnly bases: what getBoxQuads
// and the convert*FromNode methods answer in and what a page hands them. A
// point is four numbers and a rect four; a quad is four points and a bounds
// rect. DOMMatrix is not here - nothing in the engine transforms beyond a
// translation, which the fragments already carry.
namespace {

struct interface_pair {
    script::native_object * ctor;
    script::object_object * proto;
};

[[nodiscard]] interface_pair make_interface(context & cx, const char * name,
                                            script::native_fn construct, value base = {}) {
    auto * proto = cx.allocate<script::object_object>();
    if (base.is_object()) { proto->prototype = base; }
    auto * ctor = cx.allocate<script::native_object>(name, std::move(construct));
    ctor->define("prototype", value::object(proto), script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    proto->define("@@toStringTag", cx.string(name), script::attr_configurable);
    cx.define_global(name, value::object(ctor));
    return {ctor, proto};
}

[[nodiscard]] value prototype_of_global(context & cx, const char * name) {
    const value ctor = cx.global(name);
    if (!ctor.is_kind(script::heap_kind::native)) { return value::undefined(); }
    const value * proto = static_cast<script::native_object *>(ctor.as_heap())->find("prototype");
    return proto == nullptr ? value::undefined() : *proto;
}

[[nodiscard]] script::object_object * self_object(context & c) {
    const value self = c.current_this();
    return self.is_object() ? static_cast<script::object_object *>(self.as_heap()) : nullptr;
}

[[nodiscard]] double number_of(context & c, value v, const char * name, double fallback = 0) {
    const value held = v.is_object_like() ? c.lookup_property(v, name) : value::undefined();
    return held.is_undefined() ? fallback : context::to_number(held);
}

[[nodiscard]] double own_number(script::object_object * self, const char * name) {
    if (self == nullptr) { return 0; }
    const value * held = self->find(name);
    return held == nullptr ? 0 : context::to_number(*held);
}

[[nodiscard]] value point_with(context & cx, const char * interface, double x, double y, double z,
                               double w) {
    auto * made = cx.allocate<script::object_object>();
    if (const value proto = prototype_of_global(cx, interface); proto.is_object()) {
        made->prototype = proto;
    }
    made->set("x", value::number(x));
    made->set("y", value::number(y));
    made->set("z", value::number(z));
    made->set("w", value::number(w));
    return value::object(made);
}

[[nodiscard]] value rect_with(context & cx, const char * interface, double x, double y,
                              double width, double height) {
    auto * made = cx.allocate<script::object_object>();
    if (const value proto = prototype_of_global(cx, interface); proto.is_object()) {
        made->prototype = proto;
    }
    made->set("x", value::number(x));
    made->set("y", value::number(y));
    made->set("width", value::number(width));
    made->set("height", value::number(height));
    return value::object(made);
}

[[nodiscard]] value quad_with(context & cx, value p1, value p2, value p3, value p4) {
    auto * made = cx.allocate<script::object_object>();
    if (const value proto = prototype_of_global(cx, "DOMQuad"); proto.is_object()) {
        made->prototype = proto;
    }
    made->define("p1", p1, script::attr_none);
    made->define("p2", p2, script::attr_none);
    made->define("p3", p3, script::attr_none);
    made->define("p4", p4, script::attr_none);
    return value::object(made);
}

} // namespace

value dom_bindings::make_dom_point(context & cx, double x, double y) {
    return point_with(cx, "DOMPoint", x, y, 0, 1);
}

value dom_bindings::make_dom_rect(context & cx, const rect & r) {
    return rect_with(cx, "DOMRect", r.x, r.y, r.width, r.height);
}

value dom_bindings::make_dom_quad(context & cx, const rect & r) {
    return quad_with(cx, make_dom_point(cx, r.x, r.y), make_dom_point(cx, r.x + r.width, r.y),
                     make_dom_point(cx, r.x + r.width, r.y + r.height),
                     make_dom_point(cx, r.x, r.y + r.height));
}

void dom_bindings::install_geometry_interfaces(context & cx) {
    // DOMPointReadOnly and DOMPoint: (x, y, z, w) with the defaults (0, 0, 0,
    // 1), `fromPoint`, `matrixTransform` for an identity, `toJSON`.
    const auto point_ctor = [](const char * interface) {
        return [interface](context & c, std::span<value> args) {
            const auto at = [&](std::size_t i, double fallback) {
                return i < args.size() && !args[i].is_undefined() ? context::to_number(args[i])
                                                                  : fallback;
            };
            return point_with(c, interface, at(0, 0), at(1, 0), at(2, 0), at(3, 1));
        };
    };
    const interface_pair point_ro =
        make_interface(cx, "DOMPointReadOnly", point_ctor("DOMPointReadOnly"));
    const interface_pair point =
        make_interface(cx, "DOMPoint", point_ctor("DOMPoint"), value::object(point_ro.proto));
    for (const auto & [pair, interface] :
         {std::pair{point_ro, "DOMPointReadOnly"}, std::pair{point, "DOMPoint"}}) {
        set_method(cx, *pair.ctor, "fromPoint", [interface](context & c, std::span<value> args) {
            const value init = args.empty() ? value::undefined() : args[0];
            return point_with(c, interface, number_of(c, init, "x"), number_of(c, init, "y"),
                              number_of(c, init, "z"), number_of(c, init, "w", 1));
        });
    }
    set_method(cx, *point_ro.proto, "toJSON", [](context & c, std::span<value>) {
        script::object_object * self = self_object(c);
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        for (const char * name : {"x", "y", "z", "w"}) {
            out->set(name, value::number(own_number(self, name)));
        }
        return value::object(out);
    });
    set_method(cx, *point_ro.proto, "matrixTransform", [](context & c, std::span<value>) {
        script::object_object * self = self_object(c);
        return point_with(c, "DOMPoint", own_number(self, "x"), own_number(self, "y"),
                          own_number(self, "z"), own_number(self, "w"));
    });

    // DOMRectReadOnly and DOMRect: (x, y, width, height), the four derived
    // edges on the prototype, `fromRect`, `toJSON`.
    const auto rect_ctor = [](const char * interface) {
        return [interface](context & c, std::span<value> args) {
            const auto at = [&](std::size_t i) {
                return i < args.size() && !args[i].is_undefined() ? context::to_number(args[i])
                                                                  : 0.0;
            };
            return rect_with(c, interface, at(0), at(1), at(2), at(3));
        };
    };
    const interface_pair rect_ro =
        make_interface(cx, "DOMRectReadOnly", rect_ctor("DOMRectReadOnly"));
    const interface_pair rect_rw =
        make_interface(cx, "DOMRect", rect_ctor("DOMRect"), value::object(rect_ro.proto));
    for (const auto & [pair, interface] :
         {std::pair{rect_ro, "DOMRectReadOnly"}, std::pair{rect_rw, "DOMRect"}}) {
        set_method(cx, *pair.ctor, "fromRect", [interface](context & c, std::span<value> args) {
            const value init = args.empty() ? value::undefined() : args[0];
            return rect_with(c, interface, number_of(c, init, "x"), number_of(c, init, "y"),
                             number_of(c, init, "width"), number_of(c, init, "height"));
        });
    }
    // NaN-aware, as the specification's min/max are: a NaN edge makes the
    // derived edge NaN.
    const auto edge = [](bool far, bool vertical) {
        return [far, vertical](context & c, std::span<value>) {
            script::object_object * self = self_object(c);
            const double a = own_number(self, vertical ? "y" : "x");
            const double b = a + own_number(self, vertical ? "height" : "width");
            if (std::isnan(a) || std::isnan(b)) { return value::number(a + b); }
            return value::number(far ? std::max(a, b) : std::min(a, b));
        };
    };
    define_getter(cx, *rect_ro.proto, "top", edge(false, true));
    define_getter(cx, *rect_ro.proto, "right", edge(true, false));
    define_getter(cx, *rect_ro.proto, "bottom", edge(true, true));
    define_getter(cx, *rect_ro.proto, "left", edge(false, false));
    set_method(cx, *rect_ro.proto, "toJSON", [](context & c, std::span<value>) {
        script::object_object * self = self_object(c);
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        for (const char * name : {"x", "y", "width", "height"}) {
            out->set(name, value::number(own_number(self, name)));
        }
        for (const char * name : {"top", "right", "bottom", "left"}) {
            out->set(name, c.lookup_property(c.current_this(), name));
        }
        return value::object(out);
    });

    // DOMQuad: four points, `fromRect`, `fromQuad`, `getBounds`, `toJSON`.
    const interface_pair quad =
        make_interface(cx, "DOMQuad", [](context & c, std::span<value> args) {
            const auto corner = [&](std::size_t i) {
                const value init = i < args.size() ? args[i] : value::undefined();
                return point_with(c, "DOMPoint", number_of(c, init, "x"), number_of(c, init, "y"),
                                  number_of(c, init, "z"), number_of(c, init, "w", 1));
            };
            return quad_with(c, corner(0), corner(1), corner(2), corner(3));
        });
    set_method(cx, *quad.ctor, "fromRect", [](context & c, std::span<value> args) {
        const value init = args.empty() ? value::undefined() : args[0];
        return make_dom_quad(c, rect{static_cast<float>(number_of(c, init, "x")),
                                     static_cast<float>(number_of(c, init, "y")),
                                     static_cast<float>(number_of(c, init, "width")),
                                     static_cast<float>(number_of(c, init, "height"))});
    });
    set_method(cx, *quad.ctor, "fromQuad", [](context & c, std::span<value> args) {
        const value init = args.empty() ? value::undefined() : args[0];
        const auto corner = [&](const char * name) {
            const value p =
                init.is_object_like() ? c.lookup_property(init, name) : value::undefined();
            return point_with(c, "DOMPoint", number_of(c, p, "x"), number_of(c, p, "y"),
                              number_of(c, p, "z"), number_of(c, p, "w", 1));
        };
        return quad_with(c, corner("p1"), corner("p2"), corner("p3"), corner("p4"));
    });
    set_method(cx, *quad.proto, "getBounds", [](context & c, std::span<value>) {
        const value self = c.current_this();
        double left = std::numeric_limits<double>::infinity();
        double top = left;
        double right = -left;
        double bottom = -left;
        for (const char * name : {"p1", "p2", "p3", "p4"}) {
            const value p = c.lookup_property(self, name);
            const double x = number_of(c, p, "x");
            const double y = number_of(c, p, "y");
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
        return rect_with(c, "DOMRect", left, top, right - left, bottom - top);
    });
    set_method(cx, *quad.proto, "toJSON", [](context & c, std::span<value>) {
        const value self = c.current_this();
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        for (const char * name : {"p1", "p2", "p3", "p4"}) {
            out->set(name, c.lookup_property(self, name));
        }
        return value::object(out);
    });
}

// A box of `node` in viewport coordinates - §10's "the box of node" with the
// four box keywords - or nothing when the node has no box. The Document
// names the viewport, and a Text node its first fragment.
std::optional<rect> dom_bindings::box_rect_of(context & cx, value node, std::string_view box) {
    (void)cx;
    if (is_a_document(node)) {
        return rect{0, 0, static_cast<float>(viewport_width_),
                    static_cast<float>(viewport_height_)};
    }
    dom_bindings * owner = owner_of(node);
    if (owner == nullptr) { return std::nullopt; }
    const node_id id = owner->handle_of(node);
    owner->flush_layout();
    const located at = owner->locate(id, true);
    if (at.f == nullptr) { return std::nullopt; }
    rect out = at.abs;
    const layout::resolved_edges e = layout::edges_of(*at.f);
    if (box == "margin") {
        out = rect{out.x - at.f->margin_left, out.y - at.f->margin_top,
                   out.width + at.f->margin_left + at.f->margin_right,
                   out.height + at.f->margin_top + at.f->margin_bottom};
    } else if (box == "padding" || box == "content") {
        const float left = e.border_left + (box == "content" ? e.pad_left : 0);
        const float top = e.border_top + (box == "content" ? e.pad_top : 0);
        const float right = e.border_right + (box == "content" ? e.pad_right : 0);
        const float bottom = e.border_bottom + (box == "content" ? e.pad_bottom : 0);
        out = rect{out.x + left, out.y + top, std::max(0.0f, out.width - left - right),
                   std::max(0.0f, out.height - top - bottom)};
    }
    return out;
}

void dom_bindings::install_document_geometry(context & cx, script::object_object & doc) {
    if (!secondary_) { install_geometry_interfaces(cx); }
    // Document.convertPointFromNode and its two siblings: the viewport is the
    // target box, so the point moves by `from`'s box origin alone.
    for (const char * name :
         {"convertQuadFromNode", "convertRectFromNode", "convertPointFromNode"}) {
        set_method(cx, doc, name, [this, name](context & c, std::span<value> args) {
            const value from = args.size() > 1 ? args[1] : value::undefined();
            const value options = args.size() > 2 ? args[2] : value::undefined();
            std::string from_box = dict_string(c, options, "fromBox");
            if (from_box.empty()) { from_box = "border"; }
            const std::optional<rect> origin = box_rect_of(c, from, from_box);
            if (!origin) {
                throw_dom_exception(c, "NotFoundError",
                                    std::string{name} + ": the node has no box");
                return value::undefined();
            }
            const value given = args.empty() ? value::undefined() : args[0];
            const std::string_view which = name;
            if (which == "convertPointFromNode") {
                return make_dom_point(c, number_of(c, given, "x") + origin->x,
                                      number_of(c, given, "y") + origin->y);
            }
            if (which == "convertRectFromNode") {
                return make_dom_quad(c,
                                     rect{static_cast<float>(number_of(c, given, "x") + origin->x),
                                          static_cast<float>(number_of(c, given, "y") + origin->y),
                                          static_cast<float>(number_of(c, given, "width")),
                                          static_cast<float>(number_of(c, given, "height"))});
            }
            value corners[4];
            std::size_t i = 0;
            for (const char * corner : {"p1", "p2", "p3", "p4"}) {
                const value p =
                    given.is_object_like() ? c.lookup_property(given, corner) : value::undefined();
                corners[i++] = make_dom_point(c, number_of(c, p, "x") + origin->x,
                                              number_of(c, p, "y") + origin->y);
            }
            return quad_with(c, corners[0], corners[1], corners[2], corners[3]);
        });
    }
    define_getter(cx, doc, "scrollingElement", [this](context & c, std::span<value>) {
        const node_id which = scrolling_element();
        return which ? wrap(c, which) : value::null();
    });
    // The two coordinates are WebIDL `double`s: absent, NaN or infinite is a
    // TypeError (elementFromPoint-parameters).
    const auto coordinates = [](context & c, std::span<value> args, const char * who, double & x,
                                double & y) {
        x = args.empty() ? std::nan("") : context::to_number(args[0]);
        y = args.size() < 2 ? std::nan("") : context::to_number(args[1]);
        if (!std::isfinite(x) || !std::isfinite(y)) {
            c.throw_error("TypeError", std::string{who} + ": the coordinates must be finite");
            return false;
        }
        return true;
    };
    set_method(cx, doc, "elementFromPoint",
               [this, coordinates](context & c, std::span<value> args) {
                   double x = 0, y = 0;
                   if (!coordinates(c, args, "elementFromPoint", x, y)) {
                       return value::undefined();
                   }
                   const std::vector<node_id> found = elements_from_point(x, y, false);
                   return found.empty() ? value::null() : wrap(c, found.front());
               });
    set_method(cx, doc, "elementsFromPoint",
               [this, coordinates](context & c, std::span<value> args) {
                   double x = 0, y = 0;
                   if (!coordinates(c, args, "elementsFromPoint", x, y)) {
                       return value::undefined();
                   }
                   const std::vector<node_id> found = elements_from_point(x, y, true);
                   const value out = c.make_array();
                   auto * items = static_cast<script::array_object *>(out.as_heap());
                   for (const node_id id : found) { items->items.push_back(wrap(c, id)); }
                   return out;
               });
    // caretPositionFromPoint: the element under the point as the caret node,
    // with offset 0 - the insertion point within a text run is what the
    // browser's own click-to-caret path knows (browser/input.cpp) and this
    // object model does not reach it yet. null off the viewport.
    set_method(cx, doc, "caretPositionFromPoint",
               [this, coordinates](context & c, std::span<value> args) {
                   double x = 0, y = 0;
                   if (!coordinates(c, args, "caretPositionFromPoint", x, y)) {
                       return value::undefined();
                   }
                   const std::vector<node_id> found = elements_from_point(x, y, false);
                   if (found.empty()) { return value::null(); }
                   auto * position = c.allocate<script::object_object>();
                   position->set("offsetNode", wrap(c, found.front()));
                   position->set("offset", value::number(0));
                   set_method(c, *position, "getClientRect",
                              [this, id = found.front()](context & cc, std::span<value>) {
                                  const rect box = client_rect_of(id);
                                  auto * out = cc.allocate<script::object_object>();
                                  const auto set = [&](const char * name, float v) {
                                      out->set(name, value::number(static_cast<double>(v)));
                                  };
                                  set("x", box.x);
                                  set("y", box.y);
                                  set("left", box.x);
                                  set("top", box.y);
                                  set("width", 0);
                                  set("height", box.height);
                                  set("right", box.x);
                                  set("bottom", box.y + box.height);
                                  return value::object(out);
                              });
                   return value::object(position);
               });
}

} // namespace ctbrowser::shell
