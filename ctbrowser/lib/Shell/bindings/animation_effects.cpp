#include "animations/helpers.hpp"

namespace ctbrowser::shell {
using namespace animation_detail;
// --- the effect value ---------------------------------------------------------

std::vector<std::pair<std::string, std::string>> dom_bindings::animated_values(
    node_id id, float font_size,
    const std::function<std::string_view(std::string_view)> & underlying) const {
    std::vector<std::pair<std::string, std::string>> out;
    if (animations_.empty() || !id) { return out; }
    style::css::length_context ctx;
    ctx.font_size = font_size;
    ctx.viewport_width = static_cast<float>(viewport_width_);
    ctx.viewport_height = static_cast<float>(viewport_height_);

    for (const std::size_t index : animations_on(id, false)) {
        const animation_record & a = animations_[index];
        const keyframe_effect_record & e = effects_[a.effect];
        const double progress = sample_timing(a).progress;
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
        // What `currentcolor` means on this element, for every property but
        // `color` itself (whose currentcolor is the parent's).
        const std::string current_color{trim(underlying("color"), html_whitespace)};
        for (const std::string & property : properties) {
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
                return property == "color" || current_color.empty()
                           ? std::string{value}
                           : style::with_currentcolor(value, current_color);
            };
            for (const animation_keyframe & k : e.keyframes) {
                for (const auto & [name, text] : k.values) {
                    if (name != property) { continue; }
                    // A CSS-wide keyword in a keyframe is its computed value
                    // (CSS Animations 1 §3): `initial` is the table's, as is
                    // `unset` for a property that does not inherit.
                    std::string_view value = text;
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
                    points.push_back(point{k.offset, resolved_color(value), k.easing, k.composite});
                }
            }
            // THE UNDERLYING VALUE (§5.4.3): what the animations before this
            // one in composite order left, else the cascade's text, else the
            // property's initial value. It is the neutral keyframe at each
            // missing end, and what a keyframe that adds or accumulates
            // composites onto.
            const auto seen = std::ranges::find_if(
                out, [&property](const auto & entry) { return entry.first == property; });
            std::string base = seen != out.end()
                                   ? seen->second
                                   : resolved_color(trim(underlying(property), html_whitespace));
            if (base.empty() && known != nullptr) { base = std::string{known->initial}; }
            if (points.front().offset != 0) {
                // The neutral keyframe's easing is linear (§5.4.3) - unless
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
            // §4.5.1: a keyframe's composite operation - its own, or the
            // effect's when `auto` - applies to its value against the
            // underlying value BEFORE the interpolation.
            const auto composited = [&](const point & pt) {
                const std::string & op = pt.composite == "auto" ? e.composite : pt.composite;
                return style::composite_text(property, base, pt.text,
                                             op == "add"          ? style::composite_op::add
                                             : op == "accumulate" ? style::composite_op::accumulate
                                                                  : style::composite_op::replace,
                                             ctx);
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
            std::string text;
            if (single) {
                text = composited(points[start]);
            } else {
                const double span = points[stop].offset - points[start].offset;
                const double distance = (progress - points[start].offset) / span;
                const style::easing keyframe_easing =
                    style::parse_easing(points[start].easing).value_or(style::easing{});
                text =
                    interpolate_value(property, composited(points[start]), composited(points[stop]),
                                      keyframe_easing(distance, false), ctx);
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

// "Process a keyframes argument", §5.4.5: the array form and the
// property-indexed form, then "compute missing keyframe offsets".
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
    // `el.style` validates one; an invalid value is dropped and an unknown
    // property - one CSS.supports says no to - is not animatable and is skipped.
    const auto add_value = [&cx](animation_keyframe & frame, const std::string & idl, value given) {
        const std::string property = css_property_of(idl);
        std::string text = cx.to_string(given);
        if (property.starts_with("--")) {
            frame.values.emplace_back(property, std::move(text));
            return;
        }
        const style::css::property_syntax * known = style::css::find_property(property);
        if (known == nullptr) { return; }
        // A SHORTHAND IS ITS LONGHANDS (§5.4.5 "process a keyframe-like
        // object"), as a `@keyframes` block already stores it: `borderWidth:
        // '20px 40px'` animates border-top-width and the rest.
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
    if (!read_keyframes(cx, keyframes, made.keyframes)) { return value::undefined(); }
    return effects_[push_effect(cx, std::move(made))].self;
}

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
