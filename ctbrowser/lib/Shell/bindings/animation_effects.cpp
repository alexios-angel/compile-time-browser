//===- animation_effects.cpp - Keyframe effects -----------------*- C++ -*-===//
//
// Web Animations bindings: computing a keyframe effect's animated values,
// reading the timing and keyframes dictionaries, and creating KeyframeEffect
// and Animation objects. Bare section numbers (§4.5.1, §5.4.3) are Web
// Animations 1 sections unless another spec is named.
//
//===----------------------------------------------------------------------===//

#include "animations/helpers.hpp"

namespace ctbrowser::shell {
using namespace animation_detail;
// --- the effect value ---------------------------------------------------------

/// Returns each animated property of element `id` and its current value as
/// CSS text, after compositing every running animation in composite order.
/// `underlying` returns the cascaded value for a property name, and
/// `font_size` is the element's font size in px, for resolving em units.
std::vector<std::pair<std::string, std::string>> dom_bindings::animated_values(
    node_id id, float font_size,
    const std::function<std::string_view(std::string_view)> & underlying) const {
    std::vector<std::pair<std::string, std::string>> out;
    if (animations_.empty() || !id) { return out; }
    style::css::length_context ctx;
    ctx.font_size = font_size;
    ctx.viewport_width = static_cast<float>(viewport_width_);
    ctx.viewport_height = static_cast<float>(viewport_height_);
    // The element's own `interpolate-size`, not a keyframe's: an animation
    // cannot change how it interpolates itself (CSS Values 5
    // §interpolate-size, interpolate-size-which-value).
    ctx.interpolate_keywords =
        ascii_iequals(trim(underlying("interpolate-size"), html_whitespace), "allow-keywords");
    // `rem` is the root element's font size, and `em` in font-size itself the
    // parent's (CSS Values 4 §6.1.1), each as the cascade folded it to px
    // (font-size-composition).
    float parent_font_size = font_size;
    if (styles_ != nullptr) {
        const auto txn = doc_->read();
        const atom font_key = atoms_->intern_lower("font-size");
        const auto font_of = [&](node_id node) -> std::optional<float> {
            const auto found = styles_->find(style::engine::key_of(node));
            if (found == styles_->end() || !found->second) { return std::nullopt; }
            return style::css::length_text_to_px(found->second->get(font_key), {});
        };
        node_id top = id;
        for (node_id up = txn.parent(top); up && up != txn.root(); up = txn.parent(up)) {
            top = up;
        }
        if (const std::optional<float> root = font_of(top)) { ctx.root_font_size = *root; }
        if (const node_id up = txn.parent(id); up && up != txn.root()) {
            parent_font_size = font_of(up).value_or(font_size);
        }
    }
    // Where the element sits among its siblings, for a keyframe's
    // `sibling-index()` and `sibling-count()` (CSS Values 5 §tree-counting): a
    // keyframe value is computed on the element it animates, as the element's
    // own declaration would be, and changes when a sibling moves.
    {
        const auto txn = doc_->read();
        if (const node_id up = txn.parent(id)) {
            for (const node_id sibling : txn.children(up)) {
                if (txn.kind(sibling).value_or(node_kind::text) != node_kind::element) { continue; }
                ++ctx.sibling_count;
                if (sibling == id) { ctx.sibling_index = ctx.sibling_count; }
            }
        }
    }
    // The containers a keyframe's `cq*` units measure (CSS Conditional 5
    // §4), as the cascade finds them: the nearest ancestor size container on
    // each axis, its content box as last laid out.
    if (styles_ != nullptr && fragments_ != nullptr) {
        const auto txn = doc_->read();
        const atom type_key = atoms_->intern_lower("container-type");
        for (node_id up = txn.parent(id);
             up && (ctx.container_width < 0 || ctx.container_height < 0); up = txn.parent(up)) {
            const auto found = styles_->find(style::engine::key_of(up));
            if (found == styles_->end() || !found->second) { continue; }
            bool size = false, inline_size = false;
            for (const std::string_view word :
                 split_top_level(found->second->get(type_key), html_whitespace)) {
                size = size || word == "size";
                inline_size = inline_size || word == "inline-size";
            }
            const layout::fragment * f = size || inline_size ? fragments_->find(up) : nullptr;
            if (f == nullptr || f->box == nullptr) { continue; }
            const layout::resolved_edges e = layout::edges_of(*f);
            if (ctx.container_width < 0) {
                ctx.container_width = std::max(0.0f, f->bounds.width - e.horizontal_inner());
            }
            if (size && ctx.container_height < 0) {
                ctx.container_height = std::max(0.0f, f->bounds.height - e.vertical_inner());
            }
        }
    }

    for (const std::size_t index : animations_on(id, false)) {
        const animation_record & a = animations_[index];
        const keyframe_effect_record & e = effects_[a.effect];
        const timing_sample sample = sample_timing(a);
        const double progress = sample.progress;
        if (unresolved(progress)) { continue; }

        // §5.4.3, "the effect value of a keyframe effect", one property at a time.
        std::vector<std::string> properties;
        for (const animation_keyframe & k : e.keyframes) {
            for (const auto & [name, text] : k.values) {
                if (std::ranges::find(properties, name) == properties.end()) {
                    properties.push_back(name);
                }
            }
        }
        // Custom properties first: a `var()` in another property's keyframe
        // reads what they animate to (the lookup below).
        std::ranges::stable_partition(properties,
                                      [](const std::string & p) { return p.starts_with("--"); });
        // What `currentcolor` means on this element, for every property but
        // `color` itself (whose currentcolor is the parent's).
        const std::string current_color{trim(underlying("color"), html_whitespace)};
        for (const std::string & property : properties) {
            style::css::length_context own = ctx;
            if (property == "font-size") { own.font_size = parent_font_size; }
            struct point {
                double offset;
                std::string text;
                std::string easing;
                std::string composite;
            };
            std::vector<point> points;
            const auto * known = style::css::find_property(property);
            std::optional<std::string> inherited;
            const auto resolved_color = [&](std::string_view value) {
                if (property == "color" || current_color.empty()) { return std::string{value}; }
                // A drop-shadow's omitted colour is currentcolor too (Filter
                // Effects 1 §12.2), though the text does not say so: the
                // computed filter writes it out, so `drop-shadow(0px 0px)`
                // interpolates with a coloured one (filter-interpolation-004).
                if ((property == "filter" || property == "backdrop-filter") &&
                    value.find("drop-shadow") != std::string_view::npos) {
                    style::css::color_context cc;
                    cc.current_color = current_color;
                    if (std::string computed = style::css::computed_filter(value, cc);
                        !computed.empty()) {
                        return computed;
                    }
                }
                // So is a box or text shadow's (CSS Backgrounds 3 §7.1):
                // `-15px -10px 25px` pairs with `black 15px 10px 5px` as the
                // colour it draws in (box-shadow-interpolation).
                if (property == "box-shadow" || property == "text-shadow") {
                    return shadows_in_color(value, current_color);
                }
                return style::with_currentcolor(value, current_color);
            };
            for (const animation_keyframe & k : e.keyframes) {
                for (const auto & [name, text] : k.values) {
                    if (name != property) { continue; }
                    std::string_view value = text;
                    // A `var()` in a keyframe is substituted on the element it
                    // animates, from its custom properties (their animated
                    // values where an animation sets them, as the element's
                    // own declaration would see them; CSS Variables 1 §2.3,
                    // variable-animation-substitute-*). One invalid at
                    // computed-value time is `unset` (CSS Variables 1 §3.1),
                    // resolved below.
                    std::string substituted;
                    if (style::css::may_have_var(value)) {
                        const style::css::custom_lookup custom =
                            [&](atom name) -> std::optional<std::string_view> {
                            const std::string_view wanted = atoms_->text(name);
                            const auto animated = std::ranges::find_if(
                                out, [&](const auto & entry) { return entry.first == wanted; });
                            const std::string_view held = animated != out.end()
                                                              ? std::string_view{animated->second}
                                                              : underlying(wanted);
                            if (held.empty() || held == style::guaranteed_invalid) {
                                return std::nullopt;
                            }
                            return held;
                        };
                        std::optional<std::string> done =
                            style::css::substitute_var(value, custom, *atoms_);
                        substituted = done ? std::move(*done) : std::string{"unset"};
                        value = substituted;
                    }
                    // A CSS-wide keyword in a keyframe is its computed value
                    // (CSS Animations 1 §3): `initial` is the table's, as is
                    // `unset` for a property that does not inherit.
                    if (ascii_iequals(value, "inherit") ||
                        (ascii_iequals(value, "unset") && known != nullptr && known->inherited)) {
                        if (!inherited) {
                            inherited = known != nullptr ? std::string{known->initial} : "";
                            // Inherit from the flat-tree parent's computed value,
                            // including its animations, not our underlying value.
                            node_id up = assigned_slot_of(id);
                            if (!up) { up = doc_->read().parent(id); }
                            if (const auto * shadow = doc_->shadow_tree_of(up)) {
                                up = shadow->host;
                            }
                            if (styles_ != nullptr && up) {
                                const auto parent = styles_->find(style::engine::key_of(up));
                                if (parent != styles_->end() && parent->second) {
                                    const auto parent_value = [&](std::string_view name) {
                                        return parent->second->get(atoms_->intern(name));
                                    };
                                    std::string physical = style::css::physical_property_of(
                                        property, underlying("writing-mode"),
                                        underlying("direction"));
                                    if (physical.empty()) { physical = property; }
                                    if (const auto held = parent_value(physical); !held.empty()) {
                                        inherited = std::string{held};
                                    }
                                    const float parent_font = style::css::length_text_to_px(
                                                                  parent_value("font-size"), ctx)
                                                                  .value_or(font_size);
                                    for (const auto & [name, text] :
                                         animated_values(up, parent_font, parent_value)) {
                                        std::string mapped = style::css::physical_property_of(
                                            name, parent_value("writing-mode"),
                                            parent_value("direction"));
                                        if ((mapped.empty() ? name : mapped) == physical) {
                                            inherited = text;
                                        }
                                    }
                                }
                            }
                        }
                        value = *inherited;
                    } else if (known != nullptr &&
                               (ascii_iequals(value, "initial") ||
                                (ascii_iequals(value, "unset") && !known->inherited))) {
                        value = known->initial;
                    }
                    // A tree-counting function has its answer here, on this
                    // element (sibling-index-keyframe-*-dynamic).
                    std::string counted;
                    if (value.find("sibling-") != std::string_view::npos) {
                        counted =
                            style::css::fold_math(value, own, style::css::math_context_of(property))
                                .text;
                        value = counted;
                    }
                    points.push_back(point{k.offset, resolved_color(value), k.easing, k.composite});
                }
            }
            // The underlying value (§5.4.3): what the animations before this
            // one in composite order left, else the cascade's text, else the
            // property's initial value. It is the neutral keyframe at each
            // missing end, and what a keyframe that adds or accumulates
            // composites onto.
            const auto seen = std::ranges::find_if(
                out, [&property](const auto & entry) { return entry.first == property; });
            std::string base = seen != out.end()
                                   ? seen->second
                                   : resolved_color(trim(underlying(property), html_whitespace));
            // A logical property's underlying value is its physical one's,
            // the name the cascade stored the value under: `inline-size` is
            // `width` here (interpolate-size-logical-properties).
            if (base.empty() && seen == out.end()) {
                const std::string physical = style::css::physical_property_of(
                    property, underlying("writing-mode"), underlying("direction"));
                if (!physical.empty() && physical != property) {
                    base = resolved_color(trim(underlying(physical), html_whitespace));
                }
            }
            if (base.empty() && known != nullptr) { base = std::string{known->initial}; }
            if (points.front().offset != 0) {
                // The neutral keyframe's easing is linear (§5.4.3), unless
                // the effect carries a valueless keyframe at 0, which is how
                // a CSS animation hands over the element's timing function
                // for the interval it synthesises (bindings/animations/css.cpp).
                std::string easing = "linear";
                for (const animation_keyframe & k : e.keyframes) {
                    if (k.offset == 0 && k.values.empty()) {
                        easing = k.easing;
                        break;
                    }
                }
                points.insert(points.begin(), point{0, base, std::move(easing), "replace"});
            }
            if (points.back().offset != 1) {
                points.push_back(point{1, base, "linear", "replace"});
            }
            // §4.5.1: a keyframe's composite operation (its own, or the
            // effect's when `auto`) applies to its value against the
            // underlying value before the interpolation.
            // A registered custom property composites as its type does: a
            // transform list appends, and one transform function added to
            // another combines their arguments, as accumulation does.
            bool single_function = false;
            const std::string_view as =
                interpolates_as(selector_engine_, *atoms_, property, &single_function);
            const auto composited_once = [&](const point & pt) {
                const std::string & op = pt.composite == "auto" ? e.composite : pt.composite;
                return style::composite_text(as, base, pt.text,
                                             op == "add" ? (single_function
                                                                ? style::composite_op::accumulate
                                                                : style::composite_op::add)
                                             : op == "accumulate" ? style::composite_op::accumulate
                                                                  : style::composite_op::replace,
                                             own);
            };
            // Then the iteration composite (Web Animations 2, "the effect
            // value of a keyframe effect"): with `accumulate`, each endpoint
            // has the final keyframe's value accumulated onto it once per
            // completed iteration, so a 0px -> 100px effect runs 100px ->
            // 200px in its second. The `current iteration` accumulations are
            // built by repeated doubling, so an infinite animation deep into
            // its run costs a logarithm per sample.
            std::string iteration_offset;
            if (e.iteration_composite == "accumulate" && sample.iteration >= 1) {
                const auto accumulate = [&](const std::string & a, const std::string & b) {
                    return style::composite_text(property, a, b, style::composite_op::accumulate,
                                                 own);
                };
                std::string power = composited_once(points.back());
                // Clamp so the double -> uint64 cast is defined for infinite
                // iteration counts.
                for (auto n = static_cast<std::uint64_t>(std::min(sample.iteration, 1e18)); n != 0;
                     n >>= 1) {
                    if ((n & 1) != 0) {
                        iteration_offset =
                            iteration_offset.empty() ? power : accumulate(iteration_offset, power);
                    }
                    if (n > 1) { power = accumulate(power, power); }
                }
            }
            const auto composited = [&](const point & pt) {
                std::string text = composited_once(pt);
                if (iteration_offset.empty()) { return text; }
                return style::composite_text(property, iteration_offset, text,
                                             style::composite_op::accumulate, own);
            };
            std::size_t start = 0;
            std::size_t stop = 0;
            bool single = false;
            const auto count_at = [&points](double offset) {
                return std::ranges::count_if(
                    points, [offset](const point & p) { return p.offset == offset; });
            };
            if (progress < 0 && count_at(0) > 1) {
                single = true;
            } else if (progress >= 1 && count_at(1) > 1) {
                start = points.size() - 1;
                single = true;
            } else {
                bool found = false;
                for (std::size_t i = 0; i < points.size(); ++i) {
                    if (points[i].offset <= progress && points[i].offset < 1) {
                        start = i;
                        found = true;
                    }
                }
                if (!found) {
                    for (std::size_t i = 0; i < points.size(); ++i) {
                        if (points[i].offset == 0) { start = i; }
                    }
                }
                stop = start;
                for (std::size_t i = start + 1; i < points.size(); ++i) {
                    if (points[i].offset > points[start].offset) {
                        stop = i;
                        break;
                    }
                }
                single = stop == start;
            }
            // An unregistered custom property is a token stream and animates
            // discretely (CSS Properties and Values API 1 §2.4): `blue` to
            // `green` flips at the midpoint rather than becoming a colour
            // (variable-animation-from-to).
            const bool discrete =
                property.starts_with("--") &&
                (selector_engine_ == nullptr ||
                 selector_engine_->registration_of(atoms_->intern(property)) == nullptr);
            std::string text;
            if (single) {
                text = composited(points[start]);
            } else {
                const double span = points[stop].offset - points[start].offset;
                const double distance = (progress - points[start].offset) / span;
                const style::easing keyframe_easing =
                    style::parse_easing(points[start].easing).value_or(style::easing{});
                const double eased = keyframe_easing(distance, false);
                text = discrete ? composited(points[eased < 0.5 ? start : stop])
                                : interpolate_value(as, composited(points[start]),
                                                    composited(points[stop]), eased, own);
            }
            if (seen == out.end()) {
                out.emplace_back(property, std::move(text));
            } else {
                seen->second = std::move(text); // a later animation composites on top
            }
        }
    }
    return out;
}

// --- the dictionaries ---------------------------------------------------------

/// Reads an EffectTiming dictionary, or a bare number as the duration, into
/// `into`. Throws a TypeError and returns false on an invalid member.
bool dom_bindings::read_timing(context & cx, value options, effect_timing & into) {
    const auto fail = [&cx](const std::string & what) {
        cx.throw_error("TypeError", "Failed to read the timing options: " + what);
        return false;
    };
    if (options.is_number()) {
        const double d = options.as_number();
        if (std::isnan(d) || d < 0) { return fail("duration must be a non-negative number"); }
        into.duration = d;
        return true;
    }
    const auto has = [&](const char * name) {
        return !dict_member(cx, options, name).is_undefined();
    };
    const auto number = [&](const char * name, double & slot, bool nonnegative) {
        if (!has(name)) { return true; }
        const double n = context::to_number(dict_member(cx, options, name));
        if (std::isnan(n) || (nonnegative && n < 0)) {
            return fail(std::string{name} + " must be a " + (nonnegative ? "non-negative " : "") +
                        "number");
        }
        slot = n;
        return true;
    };
    if (!number("delay", into.delay, false) || !number("endDelay", into.end_delay, false) ||
        !number("iterationStart", into.iteration_start, true) ||
        !number("iterations", into.iterations, true)) {
        return false;
    }
    if (std::isinf(into.delay) || std::isinf(into.end_delay) || std::isinf(into.iteration_start)) {
        return fail("delay, endDelay and iterationStart must be finite");
    }
    if (has("duration")) {
        const value d = dict_member(cx, options, "duration");
        if (d.is_string() && ascii_iequals(cx.to_string(d), "auto")) {
            into.duration = 0;
        } else {
            const double n = context::to_number(d);
            if (std::isnan(n) || n < 0 || d.is_string()) {
                return fail("duration must be a non-negative number or \"auto\"");
            }
            into.duration = n;
        }
    }
    if (has("fill")) {
        const std::string f = dict_string(cx, options, "fill");
        if (f != "none" && f != "forwards" && f != "backwards" && f != "both" && f != "auto") {
            return fail("fill must be none, forwards, backwards, both or auto");
        }
        into.fill = f;
    }
    if (has("direction")) {
        const std::string d = dict_string(cx, options, "direction");
        if (d != "normal" && d != "reverse" && d != "alternate" && d != "alternate-reverse") {
            return fail("direction must be normal, reverse, alternate or alternate-reverse");
        }
        into.direction = d;
    }
    if (has("easing")) {
        const std::string e = dict_string(cx, options, "easing");
        if (!style::parse_easing(e)) { return fail("'" + e + "' is not a valid easing"); }
        into.easing = e;
    }
    return true;
}

/// "Process a keyframes argument", §5.4.5: the array form and the
/// property-indexed form, then "compute missing keyframe offsets". Throws a
/// TypeError and returns false on invalid input.
bool dom_bindings::read_keyframes(context & cx, value keyframes,
                                  std::vector<animation_keyframe> & into) {
    into.clear();
    if (keyframes.is_nullish()) { return true; }
    const auto fail = [&cx](const std::string & what) {
        cx.throw_error("TypeError", "Failed to read the keyframes: " + what);
        return false;
    };
    // One keyframe-like object's members, in their own order; `visit` gets
    // each non-special member as (CSS property, value).
    const auto members = [&cx](value object, auto && visit) {
        if (!object.is_object()) { return; }
        auto * held = static_cast<script::object_object *>(object.as_heap());
        std::vector<std::string> keys;
        held->each_own_entry_in_order(
            [&keys](std::string_view key, std::uint8_t) { keys.emplace_back(key); });
        for (const std::string & key : keys) { visit(key, cx.lookup_property(object, key)); }
    };
    const auto check_easing = [&](const std::string & text) {
        if (!style::parse_easing(text)) { return fail("'" + text + "' is not a valid easing"); }
        return true;
    };
    const auto check_composite = [&](const std::string & text) {
        if (text != "replace" && text != "add" && text != "accumulate" && text != "auto") {
            return fail("composite must be replace, add, accumulate or auto");
        }
        return true;
    };
    // A property's value, validated against the property table the way
    // `el.style` validates one; an invalid value is dropped, and an unknown
    // property (one CSS.supports rejects) is not animatable and is skipped.
    const auto add_value = [&cx](animation_keyframe & frame, const std::string & idl, value given) {
        const std::string property = css_property_of(idl);
        std::string text = cx.to_string(given);
        if (property.starts_with("--")) {
            frame.values.emplace_back(property, std::move(text));
            return;
        }
        const style::css::property_syntax * known = style::css::find_property(property);
        if (known == nullptr || !style::css::animatable(property)) { return; }
        // A shorthand is stored as its longhands (§5.4.5 "process a
        // keyframe-like object"), as a `@keyframes` block already stores it:
        // `borderWidth: '20px 40px'` animates border-top-width and the rest.
        if (known->shorthand) {
            style::css::declaration_block expanded;
            (void)style::css::set_declaration(expanded, property, text, false);
            if (expanded.size() == 1 && expanded[0].name == property) {
                expanded.clear();
                for (const auto & [name, value] :
                     style::css::expand_cascaded_shorthand(property, text)) {
                    const auto checked = style::css::check_declaration(name, value);
                    if (checked.valid) {
                        expanded.push_back({std::string{name}, checked.serialized, false});
                    }
                }
            }
            for (const style::css::declaration & d : expanded) {
                frame.values.emplace_back(d.name, d.value);
            }
            return;
        }
        const style::css::value_check checked = style::css::check_declaration(property, text);
        if (!checked.valid) { return; }
        frame.values.emplace_back(property, checked.serialized);
    };

    if (keyframes.is_array()) {
        auto * list = static_cast<script::array_object *>(keyframes.as_heap());
        for (const value & item : list->items) {
            animation_keyframe frame;
            bool ok = true;
            members(item, [&](const std::string & key, value v) {
                if (!ok) { return; }
                if (key == "offset") {
                    if (v.is_nullish()) { return; }
                    const double o = context::to_number(v);
                    if (std::isnan(o) || o < 0 || o > 1) {
                        ok = fail("an offset must be between 0 and 1");
                        return;
                    }
                    frame.offset = o;
                    frame.offset_given = true;
                } else if (key == "easing") {
                    frame.easing = cx.to_string(v);
                    ok = check_easing(frame.easing);
                } else if (key == "composite") {
                    frame.composite = cx.to_string(v);
                    ok = check_composite(frame.composite);
                } else {
                    add_value(frame, key, v);
                }
            });
            if (!ok) { return false; }
            into.push_back(std::move(frame));
        }
    } else if (keyframes.is_object()) {
        // Property-indexed: each property's list becomes its own run of
        // keyframes with evenly spaced offsets, and `offset`, `easing` and
        // `composite` are distributed over them in turn.
        const auto as_list = [](value v) {
            std::vector<value> items;
            if (v.is_array()) {
                items = static_cast<script::array_object *>(v.as_heap())->items;
            } else {
                items.push_back(v);
            }
            return items;
        };
        std::vector<value> offsets;
        std::vector<value> easings;
        std::vector<value> composites;
        bool ok = true;
        members(keyframes, [&](const std::string & key, value v) {
            if (!ok) { return; }
            if (key == "offset") {
                offsets = as_list(v);
            } else if (key == "easing") {
                easings = as_list(v);
            } else if (key == "composite") {
                composites = as_list(v);
            } else {
                const std::vector<value> values = as_list(v);
                for (std::size_t i = 0; i < values.size(); ++i) {
                    animation_keyframe frame;
                    frame.offset = values.size() == 1 ? 1
                                                      : static_cast<double>(i) /
                                                            static_cast<double>(values.size() - 1);
                    add_value(frame, key, values[i]);
                    into.push_back(std::move(frame));
                }
            }
        });
        if (!ok) { return false; }
        std::ranges::stable_sort(
            into, [](const auto & a, const auto & b) { return a.offset < b.offset; });
        for (std::size_t i = 0; i < into.size(); ++i) {
            if (i < offsets.size() && !offsets[i].is_nullish()) {
                const double o = context::to_number(offsets[i]);
                if (std::isnan(o) || o < 0 || o > 1) {
                    return fail("an offset must be between 0 and 1");
                }
                into[i].offset = o;
                into[i].offset_given = true;
            }
            if (!easings.empty()) {
                into[i].easing = cx.to_string(easings[i % easings.size()]);
                if (!check_easing(into[i].easing)) { return false; }
            }
            if (!composites.empty()) {
                into[i].composite = cx.to_string(composites[i % composites.size()]);
                if (!check_composite(into[i].composite)) { return false; }
            }
        }
    } else {
        return fail("keyframes must be a sequence or a property-indexed object");
    }

    // "Compute missing keyframe offsets", §5.4.6: the given ones must not
    // decrease, then the last is 1, the first is 0, and every run between two
    // given offsets is spaced evenly.
    double previous = 0;
    for (const animation_keyframe & k : into) {
        if (!k.offset_given) { continue; }
        if (k.offset < previous) {
            return fail("keyframe offsets must be monotonically increasing");
        }
        previous = k.offset;
    }
    if (into.empty()) { return true; }
    std::vector<char> known(into.size(), 0);
    for (std::size_t i = 0; i < into.size(); ++i) { known[i] = into[i].offset_given ? 1 : 0; }
    if (!known.back()) {
        into.back().offset = 1;
        known.back() = 1;
    }
    if (into.size() > 1 && !known.front()) {
        into.front().offset = 0;
        known.front() = 1;
    }
    for (std::size_t i = 0; i < into.size(); ++i) {
        if (known[i]) { continue; }
        std::size_t next = i;
        while (next < into.size() && !known[next]) { ++next; }
        const double from = into[i - 1].offset;
        const double to = into[next].offset;
        const std::size_t run = next - i + 1;
        for (std::size_t j = i; j < next; ++j) {
            into[j].offset =
                from + (to - from) * static_cast<double>(j - i + 1) / static_cast<double>(run);
            known[j] = 1;
        }
    }
    return true;
}

// --- the objects --------------------------------------------------------------

/// Creates a KeyframeEffect object for `target`, or throws and returns
/// undefined when the options or keyframes are invalid.
value dom_bindings::make_keyframe_effect(context & cx, node_id target, value keyframes,
                                         value options) {
    keyframe_effect_record made;
    made.target = target;
    if (!read_timing(cx, options, made.timing)) { return value::undefined(); }
    if (const value composite = dict_member(cx, options, "composite"); !composite.is_undefined()) {
        const std::string c = cx.to_string(composite);
        if (c != "replace" && c != "add" && c != "accumulate") {
            cx.throw_error("TypeError", "composite must be replace, add or accumulate");
            return value::undefined();
        }
        made.composite = c;
    }
    if (const value iteration = dict_member(cx, options, "iterationComposite");
        !iteration.is_undefined()) {
        const std::string c = cx.to_string(iteration);
        if (c != "replace" && c != "accumulate") {
            cx.throw_error("TypeError", "iterationComposite must be replace or accumulate");
            return value::undefined();
        }
        made.iteration_composite = c;
    }
    if (!read_keyframes(cx, keyframes, made.keyframes)) { return value::undefined(); }
    return effects_[push_effect(cx, std::move(made))].self;
}

/// Stores an effect record, gives it a script object, and returns its index
/// in effects_.
std::size_t dom_bindings::push_effect(context & cx, keyframe_effect_record made) {
    auto * object = cx.allocate<script::object_object>();
    object->prototype = keyframe_effect_prototype_;
    object->define(effect_index_key, value::number(static_cast<double>(effects_.size())),
                   script::attr_none);
    made.self = value::object(object);
    effects_.push_back(std::move(made));
    sync_animation_roots();
    return effects_.size() - 1;
}

/// Creates an idle Animation object for the effect at index `effect`.
value dom_bindings::make_animation(context & cx, std::size_t effect) {
    auto * object = cx.allocate<script::object_object>();
    object->prototype = animation_prototype_;
    object->define(animation_index_key, value::number(static_cast<double>(animations_.size())),
                   script::attr_none);
    object->set("id", cx.string(""));
    object->set("onfinish", value::null());
    object->set("oncancel", value::null());
    object->set("onremove", value::null());
    animation_record made;
    made.self = value::object(object);
    made.effect = effect;
    made.finished = cx.make_pending_promise();
    animations_.push_back(std::move(made));
    sync_animation_roots();
    ++animation_generation_;
    return animations_.back().self;
}

} // namespace ctbrowser::shell
