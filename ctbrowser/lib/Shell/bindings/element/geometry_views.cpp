#include "../computed_style/internal.hpp"
#include "internal.hpp"

#include "view_geometry.hpp"
#include <ctbrowser/dom/token_list.hpp>
#include <ctbrowser/layout/overflow.hpp>
#include <ctbrowser/shell/page/canvas.hpp>

namespace ctbrowser::shell {

using namespace detail;

dom_bindings::located dom_bindings::locate(node_id id, bool scrolled) const {
    located out;
    if (fragments_ == nullptr || !id) { return out; }
    const auto walk = [&](auto && self, const layout::fragment & at, float dx, float dy,
                          bool under_fixed) -> bool {
        const rect box = at.absolute_bounds(dx, dy);
        const bool fixed =
            under_fixed || (at.box != nullptr && at.box->position == layout::position_kind::fixed);
        if (at.source == id) {
            out.f = &at;
            out.abs = box;
            if (scrolled && !fixed) {
                const point viewport = viewport_scroll();
                out.abs.x -= viewport.x;
                out.abs.y -= viewport.y;
            }
            return true;
        }
        // A scrolled container's content sits `offset` further up and left
        // than layout put it.
        point inner{box.x, box.y};
        if (scrolled && at.box != nullptr && at.box->scroll_container && at.source) {
            const point offset = scroll_offset_of(at.source);
            inner.x -= offset.x;
            inner.y -= offset.y;
        }
        for (const layout::fragment & child : at.children) {
            if (self(self, child, inner.x, inner.y, fixed)) { return true; }
        }
        return false;
    };
    (void)walk(walk, *fragments_, 0, 0, false);
    return out;
}

rect dom_bindings::client_rect_of(node_id id) const {
    return locate(id, true).abs;
}

point dom_bindings::scroll_offset_of(node_id id) const {
    const auto it = element_scrolls_.find(id.key());
    return it == element_scrolls_.end() ? point{} : it->second;
}

// "Scroll an element to x, y" - CSSOM View §6, with the clamp of §2's
// rightward-and-downward overflow directions. A box with nothing to scroll -
// `overflow: visible`, or no box at all - ignores the write, as the setters'
// "no associated scrolling box" step says.
void dom_bindings::scroll_element_to(node_id id, double x, double y) {
    flush_layout();
    const located at = locate(id);
    if (at.f == nullptr || at.f->box == nullptr || !at.f->box->scroll_container) { return; }
    const rect area = layout::scrolling_area_of(*at.f);
    const rect padding = layout::padding_box_of(*at.f);
    const auto clamp = [](double v, float most) {
        return static_cast<float>(std::max(0.0, std::min(v, static_cast<double>(most))));
    };
    const point wanted{clamp(x, area.width - padding.width),
                       clamp(y, area.height - padding.height)};
    if (wanted == scroll_offset_of(id)) { return; }
    element_scrolls_[id.key()] = wanted;
    queue_scroll_event(id);
}

// The scrollTop/scrollLeft getters, §6: zero for the root in quirks mode and
// for a box-less element; the window's position for the root, and for a
// quirks-mode body that is not potentially scrollable; else the offset.
double dom_bindings::scroll_position(node_id id, char axis) {
    flush_layout();
    const bool quirks = doc_->quirks();
    const bool root = id == doc_->read().root();
    if (root && quirks) { return 0; }
    if (root || (quirks && id == body_element() && !potentially_scrollable(id))) {
        const point viewport = viewport_scroll();
        return axis == 'x' ? viewport.x : viewport.y;
    }
    const point offset = scroll_offset_of(id);
    return axis == 'x' ? offset.x : offset.y;
}

void dom_bindings::set_scroll_position(node_id id, char axis, double v) {
    if (!std::isfinite(v)) { v = 0; } // "normalize non-finite values"
    flush_layout();
    const bool quirks = doc_->quirks();
    const bool root = id == doc_->read().root();
    if (root && quirks) { return; }
    if (root || (quirks && id == body_element() && !potentially_scrollable(id))) {
        const point viewport = viewport_scroll();
        scroll_viewport_to(axis == 'x' ? v : viewport.x, axis == 'x' ? viewport.y : v);
        return;
    }
    const point offset = scroll_offset_of(id);
    scroll_element_to(id, axis == 'x' ? v : offset.x, axis == 'x' ? offset.y : v);
}

node_id dom_bindings::scrolling_element() {
    if (!doc_->quirks()) { return doc_->read().root(); }
    flush_layout(); // potentially_scrollable reads the boxes
    // Quirks mode: the body when it is not potentially scrollable in either
    // axis, else null - with the root's `overflow: clip` read as `hidden`,
    // which is what potentially_scrollable already does (clip is not
    // visible).
    const node_id body = body_element();
    if (!body) { return node_id{}; }
    return potentially_scrollable(body) ? node_id{} : body;
}

// "Determine the scroll-into-view position" (§6.1) for one scrolling box,
// given the target's rectangle and the box's, both in viewport coordinates,
// and the box's current position: where the box would have to scroll to.
namespace {

[[nodiscard]] float align_edge(std::string_view how, float current, float target_a, float target_b,
                               float box_a, float box_b) {
    const float element = target_b - target_a;
    const float box = box_b - box_a;
    if (how == "start") { return current + (target_a - box_a); }
    if (how == "end") { return current + (target_b - box_b); }
    if (how == "center") { return current + ((target_a + target_b) / 2 - (box_a + box_b) / 2); }
    // "nearest"
    const bool a_out = target_a < box_a;
    const bool b_out = target_b > box_b;
    if (a_out && b_out) { return current; }
    if ((a_out && element < box) || (b_out && element > box)) {
        return current + (target_a - box_a);
    }
    if ((a_out && element > box) || (b_out && element < box)) {
        return current + (target_b - box_b);
    }
    return current;
}

} // namespace

void dom_bindings::scroll_into_view(node_id id, std::string_view block, std::string_view inline_,
                                    bool nearest_container) {
    flush_layout();
    if (locate(id).f == nullptr) { return; }
    // Innermost scroll container to outermost, then the viewport. Each step
    // re-reads the target's viewport rectangle, because the scroll it just
    // performed moved it.
    std::vector<node_id> containers;
    {
        const auto txn = doc_->read();
        for (node_id up = txn.parent(id); up; up = txn.parent(up)) {
            const located at = locate(up);
            if (at.f != nullptr && at.f->box != nullptr && at.f->box->scroll_container) {
                containers.push_back(up);
            }
        }
    }
    for (const node_id container : containers) {
        const rect target = client_rect_of(id);
        const located at = locate(container, true);
        const rect pad = layout::padding_box_of(*at.f);
        const rect box{at.abs.x + pad.x, at.abs.y + pad.y, pad.width, pad.height};
        const point current = scroll_offset_of(container);
        scroll_element_to(container,
                          align_edge(inline_, current.x, target.x, target.x + target.width, box.x,
                                     box.x + box.width),
                          align_edge(block, current.y, target.y, target.y + target.height, box.y,
                                     box.y + box.height));
        if (nearest_container) { return; }
    }
    const rect target = client_rect_of(id);
    const point current = viewport_scroll();
    scroll_viewport_to(align_edge(inline_, current.x, target.x, target.x + target.width, 0,
                                  static_cast<float>(viewport_width_)),
                       align_edge(block, current.y, target.y, target.y + target.height, 0,
                                  static_cast<float>(viewport_height_)));
}

namespace {

// ScrollToOptions, read the way the methods' overloads resolve: two numbers,
// or one dictionary - whose `behavior` must be one of the three keywords -
// or nothing. `bad` is the TypeError the IDL conversion would raise.
struct scroll_arguments {
    std::optional<double> x, y;
    bool bad = false;
};

[[nodiscard]] scroll_arguments read_scroll_arguments(context & c, std::span<value> args) {
    scroll_arguments out;
    const auto finite = [](double v) { return std::isfinite(v) ? v : 0.0; };
    if (args.size() >= 2) {
        out.x = finite(context::to_number(args[0]));
        out.y = finite(context::to_number(args[1]));
        return out;
    }
    if (args.empty() || args[0].is_nullish()) { return out; }
    if (!args[0].is_object_like()) {
        out.bad = true;
        return out;
    }
    if (const value left = dict_member(c, args[0], "left"); !left.is_undefined()) {
        out.x = finite(context::to_number(left));
    }
    if (const value top = dict_member(c, args[0], "top"); !top.is_undefined()) {
        out.y = finite(context::to_number(top));
    }
    if (const value behavior = dict_member(c, args[0], "behavior"); !behavior.is_undefined()) {
        const std::string how = c.to_string(behavior);
        if (how != "auto" && how != "instant" && how != "smooth") { out.bad = true; }
    }
    return out;
}

} // namespace

// Every fragment carries its untransformed box and the mapping from box-local
// coordinates to the viewport. Rectangle and GeometryUtils APIs share this walk.
std::vector<dom_bindings::box_geometry> dom_bindings::client_boxes_of(node_id self,
                                                                      std::string_view kind) const {
    std::vector<box_geometry> out;
    if (fragments_ == nullptr || !self) { return out; }
    const point viewport = viewport_scroll();
    const auto walk = [&](auto && walk_, const layout::fragment & at, float dx, float dy,
                          bool fixed, transform matrix) -> void {
        const rect box = at.absolute_bounds(dx, dy);
        const bool is_fixed =
            fixed || (at.box != nullptr && at.box->position == layout::position_kind::fixed);
        // Layout already applies a translation to fragment positions. Replace
        // that shift with the full transform about this box's origin, then
        // compose with its ancestors before bounding the four corners.
        // ponytail: same 2D/px transform subset as computed style; extend the
        // shared parser for 3D and relative transform lengths.
        if (at.box != nullptr && !is_inline_box(at)) {
            if (const auto m = transform_matrix(cascade_value(at.source, "transform"))) {
                const transform local{static_cast<float>((*m)[0]), static_cast<float>((*m)[1]),
                                      static_cast<float>((*m)[2]), static_cast<float>((*m)[3]),
                                      static_cast<float>((*m)[4]), static_cast<float>((*m)[5])};
                style::css::length_context lengths;
                lengths.font_size = at.box->font_size;
                lengths.line_height = at.box->line_height;
                lengths.viewport_width = static_cast<float>(viewport_width_);
                lengths.viewport_height = static_cast<float>(viewport_height_);
                style::css::color_context context;
                context.lengths = &lengths;
                const std::string origin = style::css::computed_transform_property(
                    "transform-origin", cascade_value(at.source, "transform-origin"), context,
                    box.width, box.height);
                point pivot{box.width / 2, box.height / 2};
                const auto parts = split_top_level(origin, html_whitespace);
                if (parts.size() >= 2) {
                    pivot = {layout::parse_length(parts[0]).resolve(box.width, lengths.font_size),
                             layout::parse_length(parts[1]).resolve(box.height, lengths.font_size)};
                }
                const point shift{at.box->translate.x.resolve(box.width, lengths.font_size),
                                  at.box->translate.y.resolve(box.height, lengths.font_size)};
                matrix = transform::translation(-box.x - pivot.x, -box.y - pivot.y)
                             .then(local)
                             .then(transform::translation(box.x - shift.x + pivot.x,
                                                          box.y - shift.y + pivot.y))
                             .then(matrix);
            }
        }
        if (at.source == self) {
            rect selected = box;
            const layout::resolved_edges e = layout::edges_of(at);
            if (kind == "margin") {
                selected = {box.x - at.margin_left, box.y - at.margin_top,
                            box.width + at.margin_left + at.margin_right,
                            box.height + at.margin_top + at.margin_bottom};
            } else if (kind == "padding" || kind == "content") {
                const float left = e.border_left + (kind == "content" ? e.pad_left : 0);
                const float top = e.border_top + (kind == "content" ? e.pad_top : 0);
                const float right = e.border_right + (kind == "content" ? e.pad_right : 0);
                const float bottom = e.border_bottom + (kind == "content" ? e.pad_bottom : 0);
                selected = {box.x + left, box.y + top, std::max(0.0f, box.width - left - right),
                            std::max(0.0f, box.height - top - bottom)};
            }
            transform to_viewport = transform::translation(selected.x, selected.y).then(matrix);
            if (!is_fixed) {
                to_viewport = to_viewport.then(transform::translation(-viewport.x, -viewport.y));
            }
            out.push_back({rect{0, 0, selected.width, selected.height}, to_viewport});
        }
        point inner{box.x, box.y};
        if (at.box != nullptr && at.box->scroll_container && at.source) {
            const point offset = scroll_offset_of(at.source);
            inner.x -= offset.x;
            inner.y -= offset.y;
        }
        for (const layout::fragment & child : at.children) {
            walk_(walk_, child, inner.x, inner.y, is_fixed, matrix);
        }
    };
    walk(walk, *fragments_, 0, 0, false, transform{});
    return out;
}

rect binding_detail::box_geometry::bounding_rect() const {
    const point origin = to_viewport.apply(0, 0);
    const point x_axis{to_viewport.a * bounds.width, to_viewport.b * bounds.width};
    const point y_axis{to_viewport.c * bounds.height, to_viewport.d * bounds.height};
    return {origin.x + std::min(0.0f, x_axis.x) + std::min(0.0f, y_axis.x),
            origin.y + std::min(0.0f, x_axis.y) + std::min(0.0f, y_axis.y),
            std::abs(x_axis.x) + std::abs(y_axis.x), std::abs(x_axis.y) + std::abs(y_axis.y)};
}

std::vector<rect> dom_bindings::client_rects_of(node_id self) {
    flush_layout();
    std::vector<rect> out;
    for (const box_geometry & box : client_boxes_of(self, "border")) {
        out.push_back(box.bounding_rect());
    }
    return out;
}

// getClientRects() (§7), GeometryUtils (§10) and checkVisibility (HTML) on
// Element.prototype.
void dom_bindings::install_element_geometry(context & cx) {
    // §7 "getBoundingClientRect()": the smallest rectangle round every
    // client rect that has a size - an inline over three lines answers the
    // union, not its first line - the first rect when all are empty, and a
    // zero DOMRect for an element with no box.
    define_operation(
        cx, {"Element"}, "getBoundingClientRect", 0, [this](context & c, std::span<value>) {
            const std::vector<rect> rects = client_rects_of(receiver(c));
            if (rects.empty()) { return make_dom_rect(c, rect{}); }
            std::optional<rect> bound;
            for (const rect & r : rects) {
                if (r.width == 0 || r.height == 0) { continue; }
                if (!bound) {
                    bound = r;
                    continue;
                }
                const float right = std::max(bound->x + bound->width, r.x + r.width);
                const float bottom = std::max(bound->y + bound->height, r.y + r.height);
                bound->x = std::min(bound->x, r.x);
                bound->y = std::min(bound->y, r.y);
                bound->width = right - bound->x;
                bound->height = bottom - bound->y;
            }
            return make_dom_rect(c, bound.value_or(rects.front()));
        });
    // Every fragment of the element, in viewport coordinates and tree order -
    // an inline split over lines has one per line - as a DOMRectList: an
    // array carrying `item`, which is what a page indexes and measures.
    define_operation(cx, {"Element"}, "getClientRects", 0, [this](context & c, std::span<value>) {
        const value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const rect & r : client_rects_of(receiver(c))) {
            items->items.push_back(make_dom_rect(c, r));
        }
        c.store_property(out, "item", native(c, "item", [out](context &, std::span<value> args) {
                             const auto * list =
                                 static_cast<const script::array_object *>(out.as_heap());
                             const double i = args.empty() ? 0 : context::to_number(args[0]);
                             if (!(i >= 0) || i >= static_cast<double>(list->items.size())) {
                                 return value::null();
                             }
                             return list->items[static_cast<std::size_t>(i)];
                         }));
        return out;
    });
    // getBoxQuads({box, relativeTo}): one DOMQuad per box of the element, in
    // the coordinate space of `relativeTo`'s border box - the viewport when
    // it is the document or absent. Nothing for an element with no box.
    define_operation(cx, {"Element"}, "getBoxQuads", 0, [this](context & c, std::span<value> args) {
        const value options = args.empty() ? value::undefined() : args[0];
        std::string box = dict_string(c, options, "box");
        if (box.empty()) { box = "border"; }
        const value out = c.make_array();
        dom_bindings * owner = owner_of(c.current_this());
        if (owner == nullptr) { return out; }
        owner->flush_layout();
        const auto own = owner->client_boxes_of(owner->handle_of(c.current_this()), box);
        if (own.empty()) { return out; }
        transform to_relative;
        if (const value relative = dict_member(c, options, "relativeTo");
            !relative.is_undefined()) {
            const auto base = box_geometry_of(relative, "border");
            if (!base) {
                throw_dom_exception(c, "NotFoundError", "getBoxQuads: relativeTo has no box");
                return value::undefined();
            }
            to_relative = base->to_viewport.inverse();
        }
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const box_geometry & part : own) {
            items->items.push_back(
                make_dom_quad(c, part.bounds, part.to_viewport.then(to_relative)));
        }
        return out;
    });
    // convertQuadFromNode / convertRectFromNode / convertPointFromNode: the
    // geometry in `from`'s fromBox space, moved into this element's toBox
    // space. NotFoundError when either side has no box.
    for (const char * name :
         {"convertQuadFromNode", "convertRectFromNode", "convertPointFromNode"}) {
        define_operation(
            cx, {"Element"}, name, 2, [this, name](context & c, std::span<value> args) {
                const value from = args.size() > 1 ? args[1] : value::undefined();
                const value options = args.size() > 2 ? args[2] : value::undefined();
                std::string from_box = dict_string(c, options, "fromBox");
                std::string to_box = dict_string(c, options, "toBox");
                if (from_box.empty()) { from_box = "border"; }
                if (to_box.empty()) { to_box = "border"; }
                const auto source = box_geometry_of(from, from_box);
                const auto target = box_geometry_of(c.current_this(), to_box);
                if (!source || !target) {
                    throw_dom_exception(c, "NotFoundError",
                                        std::string{name} + ": the node has no box");
                    return value::undefined();
                }
                const transform conversion =
                    source->to_viewport.then(target->to_viewport.inverse());
                const value given = args.empty() ? value::undefined() : args[0];
                const auto number = [&](value v, const char * member) {
                    const value held =
                        v.is_object_like() ? c.lookup_property(v, member) : value::undefined();
                    return held.is_undefined() ? 0.0 : context::to_number(held);
                };
                const std::string_view which = name;
                if (which == "convertPointFromNode") {
                    const point p = conversion.apply(static_cast<float>(number(given, "x")),
                                                     static_cast<float>(number(given, "y")));
                    return make_dom_point(c, p.x, p.y);
                }
                if (which == "convertRectFromNode") {
                    return make_dom_quad(c,
                                         rect{static_cast<float>(number(given, "x")),
                                              static_cast<float>(number(given, "y")),
                                              static_cast<float>(number(given, "width")),
                                              static_cast<float>(number(given, "height"))},
                                         conversion);
                }
                const value quad = make_dom_quad(c, rect{});
                auto * made = static_cast<script::object_object *>(quad.as_heap());
                for (const char * corner : {"p1", "p2", "p3", "p4"}) {
                    const value p = given.is_object_like() ? c.lookup_property(given, corner)
                                                           : value::undefined();
                    const point mapped = conversion.apply(static_cast<float>(number(p, "x")),
                                                          static_cast<float>(number(p, "y")));
                    made->define(corner, make_dom_point(c, mapped.x, mapped.y), script::attr_none);
                }
                return quad;
            });
    }
    // checkVisibility(options), HTML: false with no box; with the option, a
    // `visibility` other than visible, an inclusive ancestor at `opacity: 0`,
    // or a `content-visibility: auto` ancestor whose content is off-screen.
    // ponytail: `content-visibility` is not a property the cascade carries,
    // so its hidden case is not seen here.
    define_operation(
        cx, {"Element"}, "checkVisibility", 0, [this](context & c, std::span<value> args) {
            const node_id self = receiver(c);
            flush_layout();
            const located at = locate(self, true);
            if (at.f == nullptr) { return value::boolean(false); }
            const value options = args.empty() ? value::undefined() : args[0];
            const bool check_visibility = dict_flag(c, options, "visibilityProperty") ||
                                          dict_flag(c, options, "checkVisibilityCSS");
            const bool check_opacity =
                dict_flag(c, options, "opacityProperty") || dict_flag(c, options, "checkOpacity");
            const bool check_auto = dict_flag(c, options, "contentVisibilityAuto");
            if (check_visibility && !ascii_iequals(cascade_value(self, "visibility"), "visible") &&
                !cascade_value(self, "visibility").empty()) {
                return value::boolean(false);
            }
            const auto txn = doc_->read();
            for (node_id up = self; up; up = txn.parent(up)) {
                if (check_opacity) {
                    const std::string_view opacity = cascade_value(up, "opacity");
                    if (!opacity.empty() &&
                        std::strtod(std::string{opacity}.c_str(), nullptr) == 0) {
                        return value::boolean(false);
                    }
                }
                if (up != self && check_auto &&
                    ascii_iequals(cascade_value(up, "content-visibility"), "auto") &&
                    (at.abs.y + at.abs.height <= 0 ||
                     at.abs.y >= static_cast<float>(viewport_height_))) {
                    return value::boolean(false);
                }
                if (up != self &&
                    ascii_iequals(cascade_value(up, "content-visibility"), "hidden")) {
                    return value::boolean(false);
                }
            }
            return value::boolean(true);
        });
}

// WHAT A FINISHED SCROLL'S PROMISE RESOLVES WITH: a ScrollResult whose
// `interrupted` is false - an instant scroll is never cut short by another
// (element-scroll-promises reads the member off the settled value).
value dom_bindings::scroll_settled(context & c) {
    auto * result = c.allocate<script::object_object>();
    result->set("interrupted", value::boolean(false));
    return c.make_promise(value::object(result), false);
}

// scroll(), scrollTo(), scrollBy() and scrollIntoView() on Element.prototype
// (§6). The three scroll methods return the Promise the specification gives
// them - a rejected one for an argument the IDL refuses, a resolved one
// once the (instant) scroll is done; `smooth` completes at once, which the
// suite accepts because it reads the end position.
void dom_bindings::install_element_scrolling(context & cx) {
    const auto rejected = [](context & c, std::string_view what) {
        const value error = c.make_error("TypeError", std::string{what});
        return c.make_promise(error, true);
    };
    const auto scroll = [this, rejected](bool relative) {
        return [this, rejected, relative](context & c, std::span<value> args) {
            const node_id self = receiver(c);
            const scroll_arguments read = read_scroll_arguments(c, args);
            if (read.bad) { return rejected(c, "scroll: the argument is not a ScrollToOptions"); }
            if (!self) { return scroll_settled(c); }
            flush_layout();
            const bool quirks = doc_->quirks();
            const bool root = self == doc_->read().root();
            const bool viewport_element =
                root || (quirks && self == body_element() && !potentially_scrollable(self));
            const point current = viewport_element ? viewport_scroll() : scroll_offset_of(self);
            double x = read.x.value_or(relative ? 0.0 : current.x);
            double y = read.y.value_or(relative ? 0.0 : current.y);
            if (relative) {
                x += current.x;
                y += current.y;
            }
            if (root && quirks) { return scroll_settled(c); }
            if (viewport_element) {
                scroll_viewport_to(x, y);
            } else {
                scroll_element_to(self, x, y);
            }
            return scroll_settled(c);
        };
    };
    define_operation(cx, {"Element"}, "scroll", 0, scroll(false));
    define_operation(cx, {"Element"}, "scrollTo", 0, scroll(false));
    define_operation(cx, {"Element"}, "scrollBy", 0, scroll(true));
    install_element_geometry(cx);
    define_operation(
        cx, {"Element"}, "scrollIntoView", 0, [this](context & c, std::span<value> args) {
            const node_id self = receiver(c);
            // `true` and an omitted argument are block "start"; `false` is block
            // "end"; a dictionary names both axes and the container.
            std::string block = "start";
            std::string inline_ = "nearest";
            bool nearest_container = false;
            if (!args.empty() && args[0].is_object_like()) {
                if (const value b = dict_member(c, args[0], "block"); !b.is_undefined()) {
                    block = c.to_string(b);
                }
                if (const value i = dict_member(c, args[0], "inline"); !i.is_undefined()) {
                    inline_ = c.to_string(i);
                }
                nearest_container = dict_string(c, args[0], "container") == "nearest";
                for (const std::string * how : {&block, &inline_}) {
                    if (*how != "start" && *how != "center" && *how != "end" && *how != "nearest") {
                        c.throw_error("TypeError",
                                      "scrollIntoView: " + *how + " is not a position");
                        return value::undefined();
                    }
                }
            } else if (!args.empty() && !args[0].is_nullish() && !context::truthy(args[0])) {
                // `null` is the (boolean or ScrollIntoViewOptions) union's
                // dictionary arm - block "start" - and only a falsy boolean is
                // "end".
                block = "end";
            }
            if (self) { scroll_into_view(self, block, inline_, nearest_container); }
            return scroll_settled(c);
        });
}

// FROM THE CASCADE rather than the box: the root's box carries no resolved
// properties (box_builder::build sets its style and nothing else), and the
// rule is about computed values in either axis.

} // namespace ctbrowser::shell
