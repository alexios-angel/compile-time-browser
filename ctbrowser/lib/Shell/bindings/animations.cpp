// dom_bindings - Web Animations: `element.animate`, Animation, KeyframeEffect,
// `document.timeline` and `getAnimations`.
//
// WHAT IT IS MEASURED AGAINST. css/support/interpolation-testcommon.js drives
// every interpolation test in `css/css-values` four ways, and the "Web
// Animations" way is exactly this:
//
//     var animation = target.animate(keyframes, {fill: 'forwards',
//                                                duration: 100 * 1000,
//                                                easing: createEasing(at)});
//     animation.pause();
//     animation.currentTime = 50 * 1000;
//     ... getComputedStyle(target).getPropertyValue(property)
//
// where `createEasing(at)` is `steps(1, end)`, `steps(1, start)`, `linear` or a
// `cubic-bezier(0, b, 1, b)` chosen so that the easing maps 0.5 to `at` - which
// may be outside [0, 1]. So the pieces that have to be right are the timing
// model (a paused animation seeked to a time), the easing functions including
// their extrapolation, and the interpolation of a length-percentage and of a
// number. Everything else on the interfaces is the specification's shape with
// the part that no test here reaches left out and named in bindings.hpp.
//
// THE EFFECT VALUE IS AN OVERLAY ON THE CASCADE'S TEXT. `animated_values` hands
// computed_style_entries (css name, text) pairs and that file treats them as
// if a rule had declared them - so a percentage resolves against the containing
// block there, an inset becomes a used value there, and this file never has to
// know what a computed value looks like. It interpolates on the engine's own
// calc() arithmetic (`evaluate_math` / `serialize_calc`), which is what makes
// `calc(50% - 25px)` to `calc(100% - 10px)` come out as one two-term calc that
// layout::parse_length can read, rather than as a string nothing can.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/easing.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell {
namespace {

constexpr std::string_view animation_index_key = "__ctbrowser_animation_index";
constexpr std::string_view effect_index_key = "__ctbrowser_effect_index";
constexpr double nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

[[nodiscard]] bool unresolved(double t) noexcept {
    return std::isnan(t);
}

// An interface object is not callable: `new` arrives with an object receiver
// and a plain call with none - the test every constructor in bindings/ makes.
[[nodiscard]] value illegal_new(context & c, const char * name) {
    c.throw_error("TypeError", std::string{"Failed to construct '"} + name +
                                   "': please use the 'new' operator.");
    return value::undefined();
}

// The keyframe property name as the CSS spelling: `marginLeft` -> `margin-left`,
// `cssFloat` -> `float`, `cssOffset` -> `offset`, and a custom property as is.
[[nodiscard]] std::string css_property_of(std::string_view idl) {
    if (idl.starts_with("--")) { return std::string{idl}; }
    if (idl == "cssFloat") { return "float"; }
    if (idl == "cssOffset") { return "offset"; }
    return style::css::css_name_of(idl);
}

} // namespace

// --- records ------------------------------------------------------------------

std::size_t dom_bindings::animation_index(value v) const {
    if (!v.is_object()) { return no_record; }
    const value * held =
        static_cast<script::object_object *>(v.as_heap())->find(std::string{animation_index_key});
    if (held == nullptr) { return no_record; }
    const double i = context::to_number(*held);
    return i >= 0 && i < static_cast<double>(animations_.size()) ? static_cast<std::size_t>(i)
                                                                 : no_record;
}

std::size_t dom_bindings::effect_index(value v) const {
    if (!v.is_object()) { return no_record; }
    const value * held =
        static_cast<script::object_object *>(v.as_heap())->find(std::string{effect_index_key});
    if (held == nullptr) { return no_record; }
    const double i = context::to_number(*held);
    return i >= 0 && i < static_cast<double>(effects_.size()) ? static_cast<std::size_t>(i)
                                                              : no_record;
}

void dom_bindings::sync_animation_roots() {
    if (animation_interface_ == nullptr) { return; }
    std::vector<value> & roots = animation_interface_->retained;
    roots.clear();
    roots.push_back(animation_prototype_);
    roots.push_back(keyframe_effect_prototype_);
    roots.push_back(timeline_);
    for (const keyframe_effect_record & e : effects_) { roots.push_back(e.self); }
    for (const animation_record & a : animations_) {
        roots.push_back(a.self);
        roots.push_back(a.finished);
    }
}

std::uint64_t dom_bindings::animation_stamp() const noexcept {
    if (animations_.empty()) { return 0; }
    // The clock is part of the answer while an animation is live: a running
    // one's value moves between two reads with no other state change.
    std::uint64_t clock = 0;
    static_assert(sizeof(clock) == sizeof(now_ms_));
    std::memcpy(&clock, &now_ms_, sizeof(clock));
    return (animation_generation_ * 0x9E3779B97F4A7C15ull) ^ clock ^ 1;
}

// --- the timing model ---------------------------------------------------------

double dom_bindings::effect_end_time(const keyframe_effect_record & e) const noexcept {
    const effect_timing & t = e.timing;
    const double active = t.duration == 0 || t.iterations == 0 ? 0 : t.duration * t.iterations;
    return std::max(t.delay + active + t.end_delay, 0.0);
}

double dom_bindings::animation_current_time(const animation_record & a) const noexcept {
    if (!unresolved(a.hold_time)) { return a.hold_time; }
    if (unresolved(a.start_time)) { return nan; }
    return (now_ms_ - a.start_time) * a.playback_rate;
}

// "Silently set the current time", §4.4.14.
void dom_bindings::set_animation_current_time(animation_record & a, double t) {
    if (!unresolved(a.hold_time) || unresolved(a.start_time) || a.playback_rate == 0) {
        a.hold_time = t;
    } else {
        a.start_time = now_ms_ - t / a.playback_rate;
    }
}

std::string_view dom_bindings::play_state(const animation_record & a) const noexcept {
    const double current = animation_current_time(a);
    if (unresolved(current)) { return "idle"; }
    // Paused before finished, in the specification's order: a paused
    // animation seeked past its end reports `paused`, which is exactly the
    // state the interpolation harness leaves every target in.
    if (unresolved(a.start_time)) { return "paused"; }
    const double end = a.effect == no_record ? 0 : effect_end_time(effects_[a.effect]);
    if ((a.playback_rate > 0 && current >= end) || (a.playback_rate < 0 && current <= 0)) {
        return "finished";
    }
    return "running";
}

// "Update the finished state", §4.4.16, with the seek case folded in by the
// caller having already set the hold time. Every state change ends here.
void dom_bindings::update_finished_state(context & cx, std::size_t index) {
    ++animation_generation_;
    animation_record & a = animations_[index];
    const double current = animation_current_time(a);
    if (!unresolved(current) && !unresolved(a.start_time) && a.effect != no_record) {
        const double end = effect_end_time(effects_[a.effect]);
        if (a.playback_rate > 0 && current >= end) {
            if (unresolved(a.hold_time)) { a.hold_time = end; }
        } else if (a.playback_rate < 0 && current <= 0) {
            if (unresolved(a.hold_time)) { a.hold_time = 0; }
        } else if (a.playback_rate != 0 && !unresolved(a.hold_time)) {
            // A seek on a running animation: the hold becomes a new start.
            a.start_time = now_ms_ - a.hold_time / a.playback_rate;
            a.hold_time = nan;
        }
    }
    const bool finished = play_state(a) == "finished";
    if (finished && !a.finished_settled) {
        a.finished_settled = true;
        cx.settle_promise(a.finished, a.self, false);
    } else if (!finished && a.finished_settled) {
        a.finished_settled = false;
        a.finished = cx.make_pending_promise();
        sync_animation_roots();
    }
}

std::vector<std::size_t> dom_bindings::animations_on(node_id id, bool subtree) const {
    std::vector<std::size_t> out;
    const auto txn = doc_->read();
    for (std::size_t i = 0; i < animations_.size(); ++i) {
        const animation_record & a = animations_[i];
        if (a.effect == no_record || play_state(a) == "idle") { continue; }
        const node_id target = effects_[a.effect].target;
        if (!target) { continue; }
        bool hit = target == id;
        if (!hit && subtree) {
            for (node_id up = txn.parent(target); up && up != target; up = txn.parent(up)) {
                if (up == id) {
                    hit = true;
                    break;
                }
                if (txn.parent(up) == up) { break; }
            }
        }
        if (hit) { out.push_back(i); }
    }
    return out;
}

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
        const effect_timing & t = e.timing;
        const double local = animation_current_time(a);
        if (unresolved(local)) { continue; }

        // §4.8.3: the phase, then the active time, filled or not.
        const double active_duration =
            t.duration == 0 || t.iterations == 0 ? 0 : t.duration * t.iterations;
        const double end = effect_end_time(e);
        const double before_boundary = std::max(std::min(t.delay, end), 0.0);
        const double after_boundary = std::max(std::min(t.delay + active_duration, end), 0.0);
        enum class phase_kind {
            before,
            active,
            after
        };
        phase_kind phase = phase_kind::active;
        if (local < before_boundary || (a.playback_rate < 0 && local == before_boundary)) {
            phase = phase_kind::before;
        } else if (local > after_boundary || (a.playback_rate >= 0 && local == after_boundary)) {
            phase = phase_kind::after;
        }
        const bool fill_backwards = t.fill == "backwards" || t.fill == "both";
        const bool fill_forwards = t.fill == "forwards" || t.fill == "both";
        double active_time = nan;
        switch (phase) {
        case phase_kind::before: active_time = fill_backwards ? 0 : nan; break;
        case phase_kind::active: active_time = local - t.delay; break;
        case phase_kind::after: active_time = fill_forwards ? active_duration : nan; break;
        }
        if (unresolved(active_time)) { continue; }

        // §4.8.4-4.8.7: overall, simple iteration and directed progress.
        double overall = t.duration == 0 ? (phase == phase_kind::before ? 0 : t.iterations)
                                         : active_time / t.duration;
        overall += t.iteration_start;
        double simple = std::isinf(overall) ? 0 : std::fmod(overall, 1.0);
        if (simple < 0) { simple += 1; }
        const bool at_end = active_time == active_duration && t.iterations != 0;
        if (simple == 0 && at_end && (phase == phase_kind::active || phase == phase_kind::after)) {
            simple = 1;
        }
        double iteration = std::floor(overall);
        if (phase == phase_kind::after && std::isinf(t.iterations)) {
            iteration = infinity;
        } else if (simple == 1) {
            iteration = std::floor(overall) - 1;
        }
        bool forwards = true;
        if (t.direction == "reverse") {
            forwards = false;
        } else if (t.direction == "alternate" || t.direction == "alternate-reverse") {
            const bool even = std::isinf(iteration) || std::fmod(iteration, 2.0) == 0;
            forwards = t.direction == "alternate" ? even : !even;
        }
        const double directed = forwards ? simple : 1 - simple;
        const bool before_flag =
            (forwards && phase == phase_kind::before) || (!forwards && phase == phase_kind::after);
        const style::easing timing_easing = style::parse_easing(t.easing).value_or(style::easing{});
        const double progress = timing_easing(directed, before_flag);

        // §5.4.3, "the effect value of a keyframe effect", one property at a time.
        std::vector<std::string> properties;
        for (const animation_keyframe & k : e.keyframes) {
            for (const auto & [name, text] : k.values) {
                if (std::ranges::find(properties, name) == properties.end()) {
                    properties.push_back(name);
                }
            }
        }
        for (const std::string & property : properties) {
            struct point {
                double offset;
                std::string text;
                std::string easing;
            };
            std::vector<point> points;
            for (const animation_keyframe & k : e.keyframes) {
                for (const auto & [name, text] : k.values) {
                    if (name == property) { points.push_back(point{k.offset, text, k.easing}); }
                }
            }
            // A NEUTRAL KEYFRAME AT EACH MISSING END: the underlying value,
            // which is the cascade's text or the property's initial value.
            if (points.front().offset != 0 || points.back().offset != 1) {
                std::string base{trim(underlying(property), html_whitespace)};
                if (base.empty()) {
                    if (const auto * known = style::css::find_property(property)) {
                        base = std::string{known->initial};
                    }
                }
                if (points.front().offset != 0) {
                    points.insert(points.begin(), point{0, base, "linear"});
                }
                if (points.back().offset != 1) { points.push_back(point{1, base, "linear"}); }
            }
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
                text = points[start].text;
            } else {
                const double span = points[stop].offset - points[start].offset;
                const double distance = (progress - points[start].offset) / span;
                const style::easing keyframe_easing =
                    style::parse_easing(points[start].easing).value_or(style::easing{});
                text = style::interpolate_text(property, points[start].text, points[stop].text,
                                               keyframe_easing(distance, false), ctx);
            }
            const auto seen = std::ranges::find_if(
                out, [&property](const auto & entry) { return entry.first == property; });
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
    if (!options.is_object()) { return true; }
    const auto has = [&](const char * name) {
        return !cx.lookup_property(options, name).is_undefined();
    };
    const auto number = [&](const char * name, double & slot, bool nonnegative) {
        if (!has(name)) { return true; }
        const double n = context::to_number(cx.lookup_property(options, name));
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
        const value d = cx.lookup_property(options, "duration");
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
        const std::string f = cx.to_string(cx.lookup_property(options, "fill"));
        if (f != "none" && f != "forwards" && f != "backwards" && f != "both" && f != "auto") {
            return fail("fill must be none, forwards, backwards, both or auto");
        }
        into.fill = f;
    }
    if (has("direction")) {
        const std::string d = cx.to_string(cx.lookup_property(options, "direction"));
        if (d != "normal" && d != "reverse" && d != "alternate" && d != "alternate-reverse") {
            return fail("direction must be normal, reverse, alternate or alternate-reverse");
        }
        into.direction = d;
    }
    if (has("easing")) {
        const std::string e = cx.to_string(cx.lookup_property(options, "easing"));
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
        if (style::css::find_property(property) == nullptr) { return; }
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
    if (options.is_object()) {
        const value composite = cx.lookup_property(options, "composite");
        if (!composite.is_undefined()) {
            const std::string c = cx.to_string(composite);
            if (c != "replace" && c != "add" && c != "accumulate") {
                cx.throw_error("TypeError", "composite must be replace, add or accumulate");
                return value::undefined();
            }
            made.composite = c;
        }
    }
    if (!read_keyframes(cx, keyframes, made.keyframes)) { return value::undefined(); }
    auto * object = cx.allocate<script::object_object>();
    object->prototype = keyframe_effect_prototype_;
    object->define(effect_index_key, value::number(static_cast<double>(effects_.size())),
                   script::attr_none);
    made.self = value::object(object);
    effects_.push_back(std::move(made));
    sync_animation_roots();
    return effects_.back().self;
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

void dom_bindings::install_animations(context & cx) {
    if (animation_interface_ != nullptr) { return; }
    const auto method = [&cx](script::object_object * on, const char * name, script::native_fn fn) {
        on->define(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))),
                   script::attr_builtin);
    };
    const auto accessor = [&cx](script::object_object * on, const char * name,
                                script::native_fn get, script::native_fn set = {}) {
        on->define_accessor(
            name, value::object(cx.allocate<script::native_object>(name, std::move(get))),
            set ? value::object(cx.allocate<script::native_object>(name, std::move(set)))
                : value::undefined());
    };
    const auto illegal = [](context & c) {
        c.throw_error("TypeError", "Illegal invocation");
        return value::undefined();
    };

    // --- DocumentTimeline, and the one the document has -------------------
    auto * timeline_proto = cx.allocate<script::object_object>();
    accessor(timeline_proto, "currentTime",
             [this](context &, std::span<value>) { return value::number(now_ms_); });
    auto * timeline_ctor = cx.allocate<script::native_object>(
        "DocumentTimeline", [timeline_proto](context & c, std::span<value>) {
            const value self = c.current_this();
            if (!self.is_object()) { return illegal_new(c, "DocumentTimeline"); }
            static_cast<script::object_object *>(self.as_heap())->prototype =
                value::object(timeline_proto);
            return self;
        });
    timeline_ctor->define("prototype", value::object(timeline_proto), script::attr_none);
    timeline_proto->define("constructor", value::object(timeline_ctor), script::attr_builtin);
    cx.define_global("DocumentTimeline", value::object(timeline_ctor));
    auto * timeline = cx.allocate<script::object_object>();
    timeline->prototype = value::object(timeline_proto);
    timeline_ = value::object(timeline);

    // --- KeyframeEffect -----------------------------------------------------
    auto * effect_proto = cx.allocate<script::object_object>();
    keyframe_effect_prototype_ = value::object(effect_proto);
    const auto effect_of = [this](context & c) -> keyframe_effect_record * {
        const std::size_t i = effect_index(c.current_this());
        return i == no_record ? nullptr : &effects_[i];
    };
    accessor(effect_proto, "target", [this, effect_of, illegal](context & c, std::span<value>) {
        const keyframe_effect_record * e = effect_of(c);
        if (e == nullptr) { return illegal(c); }
        return e->target ? wrap(c, e->target) : value::null();
    });
    accessor(effect_proto, "pseudoElement",
             [](context &, std::span<value>) { return value::null(); });
    accessor(
        effect_proto, "composite",
        [effect_of, illegal](context & c, std::span<value>) {
            const keyframe_effect_record * e = effect_of(c);
            return e == nullptr ? illegal(c) : c.string(e->composite);
        },
        [this, effect_of, illegal](context & c, std::span<value> args) {
            keyframe_effect_record * e = effect_of(c);
            if (e == nullptr) { return illegal(c); }
            const std::string given = arg_string(c, args, 0);
            if (given == "replace" || given == "add" || given == "accumulate") {
                e->composite = given;
                ++animation_generation_;
            }
            return value::undefined();
        });
    method(effect_proto, "getKeyframes", [effect_of, illegal](context & c, std::span<value>) {
        const keyframe_effect_record * e = effect_of(c);
        if (e == nullptr) { return illegal(c); }
        const value list = c.make_array();
        for (const animation_keyframe & k : e->keyframes) {
            auto * frame = c.allocate<script::object_object>();
            frame->set("offset", k.offset_given ? value::number(k.offset) : value::null());
            frame->set("computedOffset", value::number(k.offset));
            frame->set("easing", c.string(k.easing));
            frame->set("composite", c.string(k.composite));
            for (const auto & [name, text] : k.values) {
                frame->set(name.starts_with("--") ? name : style::css::idl_name_of(name),
                           c.string(text));
            }
            static_cast<script::array_object *>(list.as_heap())
                ->items.push_back(value::object(frame));
        }
        return list;
    });
    method(effect_proto, "setKeyframes",
           [this, effect_of, illegal](context & c, std::span<value> args) {
               keyframe_effect_record * e = effect_of(c);
               if (e == nullptr) { return illegal(c); }
               std::vector<animation_keyframe> fresh;
               if (!read_keyframes(c, arg(args, 0), fresh)) { return value::undefined(); }
               e->keyframes = std::move(fresh);
               ++animation_generation_;
               return value::undefined();
           });
    const auto timing_object = [](context & c, const effect_timing & t) {
        auto * o = c.allocate<script::object_object>();
        o->set("delay", value::number(t.delay));
        o->set("endDelay", value::number(t.end_delay));
        o->set("fill", c.string(t.fill));
        o->set("iterationStart", value::number(t.iteration_start));
        o->set("iterations", value::number(t.iterations));
        o->set("duration", value::number(t.duration));
        o->set("direction", c.string(t.direction));
        o->set("easing", c.string(t.easing));
        return o;
    };
    method(effect_proto, "getTiming",
           [effect_of, illegal, timing_object](context & c, std::span<value>) {
               const keyframe_effect_record * e = effect_of(c);
               if (e == nullptr) { return illegal(c); }
               return value::object(timing_object(c, e->timing));
           });
    method(effect_proto, "getComputedTiming",
           [this, effect_of, illegal, timing_object](context & c, std::span<value>) {
               const keyframe_effect_record * e = effect_of(c);
               if (e == nullptr) { return illegal(c); }
               script::object_object * o = timing_object(c, e->timing);
               const double active = e->timing.duration == 0 || e->timing.iterations == 0
                                         ? 0
                                         : e->timing.duration * e->timing.iterations;
               o->set("activeDuration", value::number(active));
               o->set("endTime", value::number(effect_end_time(*e)));
               o->set("fill", c.string(e->timing.fill == "auto" ? "none" : e->timing.fill));
               // The animation this effect belongs to, for its local time.
               double local = nan;
               for (const animation_record & a : animations_) {
                   if (a.effect != no_record && &effects_[a.effect] == e) {
                       local = animation_current_time(a);
                   }
               }
               o->set("localTime", unresolved(local) ? value::null() : value::number(local));
               o->set("progress", value::null());
               o->set("currentIteration", value::null());
               return value::object(o);
           });
    method(effect_proto, "updateTiming",
           [this, effect_of, illegal](context & c, std::span<value> args) {
               keyframe_effect_record * e = effect_of(c);
               if (e == nullptr) { return illegal(c); }
               effect_timing fresh = e->timing;
               if (!read_timing(c, arg(args, 0), fresh)) { return value::undefined(); }
               e->timing = fresh;
               ++animation_generation_;
               return value::undefined();
           });
    auto * effect_ctor = cx.allocate<script::native_object>(
        "KeyframeEffect", [this](context & c, std::span<value> args) {
            const value self = c.current_this();
            if (!self.is_object()) { return illegal_new(c, "KeyframeEffect"); }
            const value target = arg(args, 0);
            if (!target.is_nullish() && !handle_of(target)) {
                c.throw_error("TypeError", "Failed to construct 'KeyframeEffect': parameter 1 is "
                                           "not of type 'Element'.");
                return value::undefined();
            }
            return make_keyframe_effect(c, handle_of(target), arg(args, 1), arg(args, 2));
        });
    effect_ctor->define("prototype", keyframe_effect_prototype_, script::attr_none);
    effect_proto->define("constructor", value::object(effect_ctor), script::attr_builtin);
    cx.define_global("KeyframeEffect", value::object(effect_ctor));

    // --- Animation ------------------------------------------------------------
    auto * proto = cx.allocate<script::object_object>();
    // An Animation IS an EventTarget - `addEventListener('finish', ...)` must
    // exist even though nothing here fires it.
    proto->prototype = event_target_prototype_;
    animation_prototype_ = value::object(proto);
    const auto self_index = [this, illegal](context & c) {
        const std::size_t i = animation_index(c.current_this());
        if (i == no_record) { (void)illegal(c); }
        return i;
    };
    accessor(
        proto, "effect",
        [this, self_index](context & c, std::span<value>) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            const std::size_t e = animations_[i].effect;
            return e == no_record ? value::null() : effects_[e].self;
        },
        [this, self_index](context & c, std::span<value> args) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            const value given = arg(args, 0);
            const std::size_t e = effect_index(given);
            if (!given.is_nullish() && e == no_record) {
                c.throw_error("TypeError", "effect must be a KeyframeEffect or null");
                return value::undefined();
            }
            animations_[i].effect = e;
            update_finished_state(c, i);
            return value::undefined();
        });
    accessor(proto, "timeline", [this](context &, std::span<value>) { return timeline_; });
    accessor(
        proto, "startTime",
        [this, self_index](context & c, std::span<value>) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            const double t = animations_[i].start_time;
            return unresolved(t) ? value::null() : value::number(t);
        },
        [this, self_index](context & c, std::span<value> args) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            animation_record & a = animations_[i];
            const value given = arg(args, 0);
            a.start_time = given.is_nullish() ? nan : context::to_number(given);
            if (!unresolved(a.start_time)) { a.hold_time = nan; }
            update_finished_state(c, i);
            return value::undefined();
        });
    accessor(
        proto, "currentTime",
        [this, self_index](context & c, std::span<value>) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            update_finished_state(c, i);
            const double t = animation_current_time(animations_[i]);
            return unresolved(t) ? value::null() : value::number(t);
        },
        [this, self_index](context & c, std::span<value> args) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            const value given = arg(args, 0);
            if (given.is_nullish()) {
                if (!unresolved(animation_current_time(animations_[i]))) {
                    c.throw_error("TypeError", "currentTime may not be set to null once it "
                                               "is resolved");
                }
                return value::undefined();
            }
            const double t = context::to_number(given);
            if (std::isnan(t)) {
                c.throw_error("TypeError", "currentTime must be a number");
                return value::undefined();
            }
            set_animation_current_time(animations_[i], t);
            update_finished_state(c, i);
            return value::undefined();
        });
    accessor(
        proto, "playbackRate",
        [this, self_index](context & c, std::span<value>) {
            const std::size_t i = self_index(c);
            return i == no_record ? value::undefined()
                                  : value::number(animations_[i].playback_rate);
        },
        [this, self_index](context & c, std::span<value> args) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            animation_record & a = animations_[i];
            const double previous = animation_current_time(a);
            a.playback_rate = arg_number(args, 0);
            if (!unresolved(previous)) { set_animation_current_time(a, previous); }
            update_finished_state(c, i);
            return value::undefined();
        });
    accessor(proto, "playState", [this, self_index](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        if (i == no_record) { return value::undefined(); }
        update_finished_state(c, i);
        return c.string(std::string{play_state(animations_[i])});
    });
    accessor(proto, "replaceState",
             [](context & c, std::span<value>) { return c.string("active"); });
    accessor(proto, "pending", [](context &, std::span<value>) { return value::boolean(false); });
    accessor(proto, "ready", [this, self_index](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        return i == no_record ? value::undefined() : c.make_promise(animations_[i].self, false);
    });
    accessor(proto, "finished", [this, self_index](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        if (i == no_record) { return value::undefined(); }
        update_finished_state(c, i);
        return animations_[i].finished;
    });

    // "Play an animation", §4.4.10, with auto-rewind and no pending task: the
    // ready promise is already resolved, so the start time is taken now.
    const auto play = [this](context & c, std::size_t i) {
        animation_record & a = animations_[i];
        const double current = animation_current_time(a);
        const double end = a.effect == no_record ? 0 : effect_end_time(effects_[a.effect]);
        if (a.playback_rate > 0 && (unresolved(current) || current < 0 || current >= end)) {
            a.hold_time = 0;
        } else if (a.playback_rate < 0 && (unresolved(current) || current <= 0 || current > end)) {
            if (std::isinf(end)) {
                throw_dom_exception(c, "InvalidStateError",
                                    "cannot play an infinite animation backwards");
                return;
            }
            a.hold_time = end;
        } else if (a.playback_rate == 0 && unresolved(current)) {
            a.hold_time = 0;
        }
        if (!unresolved(a.hold_time)) {
            a.start_time = a.playback_rate == 0 ? now_ms_ : now_ms_ - a.hold_time / a.playback_rate;
            a.hold_time = nan;
        }
        update_finished_state(c, i);
    };
    method(proto, "play", [self_index, play](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        if (i != no_record) { play(c, i); }
        return value::undefined();
    });
    method(proto, "pause", [this, self_index](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        if (i == no_record) { return value::undefined(); }
        animation_record & a = animations_[i];
        if (unresolved(a.hold_time)) {
            const double current = animation_current_time(a);
            if (a.playback_rate >= 0) {
                a.hold_time = unresolved(current) ? 0 : current;
            } else {
                const double end = a.effect == no_record ? 0 : effect_end_time(effects_[a.effect]);
                if (std::isinf(end)) {
                    throw_dom_exception(c, "InvalidStateError",
                                        "cannot pause an infinite animation played backwards");
                    return value::undefined();
                }
                a.hold_time = unresolved(current) ? end : current;
            }
        }
        a.start_time = nan;
        update_finished_state(c, i);
        return value::undefined();
    });
    method(proto, "finish", [this, self_index](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        if (i == no_record) { return value::undefined(); }
        animation_record & a = animations_[i];
        const double end = a.effect == no_record ? 0 : effect_end_time(effects_[a.effect]);
        if (a.playback_rate == 0 || (a.playback_rate > 0 && std::isinf(end))) {
            throw_dom_exception(c, "InvalidStateError", "the animation cannot be finished");
            return value::undefined();
        }
        const double limit = a.playback_rate > 0 ? end : 0;
        set_animation_current_time(a, limit);
        if (unresolved(a.start_time)) { a.start_time = now_ms_ - limit / a.playback_rate; }
        update_finished_state(c, i);
        return value::undefined();
    });
    method(proto, "cancel", [this, self_index](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        if (i == no_record) { return value::undefined(); }
        animation_record & a = animations_[i];
        if (play_state(a) != "idle") {
            if (!a.finished_settled) {
                c.settle_promise(a.finished,
                                 make_dom_exception(c, "AbortError", "The user aborted a request."),
                                 true);
            }
            a.finished = c.make_pending_promise();
            a.finished_settled = false;
        }
        a.hold_time = nan;
        a.start_time = nan;
        ++animation_generation_;
        sync_animation_roots();
        return value::undefined();
    });
    method(proto, "reverse", [this, self_index, play](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        if (i == no_record) { return value::undefined(); }
        animation_record & a = animations_[i];
        const double current = animation_current_time(a);
        a.playback_rate = -a.playback_rate;
        if (!unresolved(current)) { set_animation_current_time(a, current); }
        play(c, i);
        return value::undefined();
    });
    method(proto, "updatePlaybackRate", [this, self_index](context & c, std::span<value> args) {
        const std::size_t i = self_index(c);
        if (i == no_record) { return value::undefined(); }
        animation_record & a = animations_[i];
        const double current = animation_current_time(a);
        a.playback_rate = arg_number(args, 0);
        if (!unresolved(current)) { set_animation_current_time(a, current); }
        update_finished_state(c, i);
        return value::undefined();
    });
    auto * ctor =
        cx.allocate<script::native_object>("Animation", [this](context & c, std::span<value> args) {
            const value self = c.current_this();
            if (!self.is_object()) { return illegal_new(c, "Animation"); }
            const value given = arg(args, 0);
            const std::size_t e = effect_index(given);
            if (!given.is_nullish() && e == no_record) {
                c.throw_error("TypeError", "Failed to construct 'Animation': parameter 1 is not of "
                                           "type 'AnimationEffect'.");
                return value::undefined();
            }
            return make_animation(c, e);
        });
    ctor->define("prototype", animation_prototype_, script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    cx.define_global("Animation", value::object(ctor));
    animation_interface_ = ctor;

    // --- Element.prototype.animate / getAnimations, and the document's --------
    const auto animations_array = [this](context & c, const std::vector<std::size_t> & which) {
        const value list = c.make_array();
        for (const std::size_t i : which) {
            static_cast<script::array_object *>(list.as_heap())
                ->items.push_back(animations_[i].self);
        }
        return list;
    };
    if (const value element = interface_prototype("Element"); element.is_object()) {
        auto * element_proto = static_cast<script::object_object *>(element.as_heap());
        // §5.5, "the animate() method": a KeyframeEffect, an Animation over it,
        // the `id` option, then play().
        method(element_proto, "animate", [this, play](context & c, std::span<value> args) {
            const node_id id = receiver(c);
            if (!id) {
                c.throw_error("TypeError", "Illegal invocation");
                return value::undefined();
            }
            const value effect = make_keyframe_effect(c, id, arg(args, 0), arg(args, 1));
            if (!effect.is_object()) { return value::undefined(); }
            const value animation = make_animation(c, effect_index(effect));
            const value options = arg(args, 1);
            if (options.is_object()) {
                const value name = c.lookup_property(options, "id");
                if (!name.is_undefined()) {
                    static_cast<script::object_object *>(animation.as_heap())
                        ->set("id", c.string(c.to_string(name)));
                }
            }
            play(c, animation_index(animation));
            return animation;
        });
        method(element_proto, "getAnimations",
               [this, animations_array](context & c, std::span<value> args) {
                   const node_id id = receiver(c);
                   if (!id) {
                       c.throw_error("TypeError", "Illegal invocation");
                       return value::undefined();
                   }
                   const value options = arg(args, 0);
                   const bool subtree = options.is_object() &&
                                        context::truthy(c.lookup_property(options, "subtree"));
                   return animations_array(c, animations_on(id, subtree));
               });
    }
    if (script::object_object * doc = document_object()) {
        doc->define_accessor(
            "timeline",
            value::object(cx.allocate<script::native_object>(
                "timeline", [this](context &, std::span<value>) { return timeline_; })),
            value::undefined());
        // Every animation whose target is in the document, in creation order.
        method(doc, "getAnimations", [this, animations_array](context & c, std::span<value>) {
            std::vector<std::size_t> which;
            const auto txn = doc_->read();
            for (std::size_t i = 0; i < animations_.size(); ++i) {
                const animation_record & a = animations_[i];
                if (a.effect == no_record || play_state(a) == "idle") { continue; }
                node_id up = effects_[a.effect].target;
                while (up && txn.parent(up) && txn.parent(up) != up) { up = txn.parent(up); }
                if (up && up == txn.root()) { which.push_back(i); }
            }
            return animations_array(c, which);
        });
    }
    sync_animation_roots();
}

} // namespace ctbrowser::shell
