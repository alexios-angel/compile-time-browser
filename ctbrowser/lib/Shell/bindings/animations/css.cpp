// dom_bindings - CSS Animations and CSS Transitions, driven from the cascade.
//
// WHAT HAPPENS AFTER A STYLE RESOLUTION. The browser hands over the previous
// style map and the new one (browser::resolve_styles) and this walks the
// document once, in tree order, looking only at elements whose computed style
// object CHANGED - the cascade interns styles, so an untouched element's
// pointer is the same and costs nothing here.
//
//   * CSS Transitions 1 §3, per property named by `transition-property` (or
//     every property, for `all`): the before-change value is the previous
//     style's text with the element's running animations sampled on top of it
//     - which is what makes a reversed transition start from where it is - and
//     the after-change value is the new style's. A difference the pair can
//     interpolate starts a CSSTransition; `transition-behavior: allow-discrete`
//     lets a pair that cannot start one that flips at 50%. A running transition
//     whose end value changed is cancelled and replaced, with §3.1's reversing
//     shortening when the new end is where it started.
//   * CSS Animations 1 §5, per index in `animation-name`: a name that means a
//     `@keyframes` rule gets a CSSAnimation whose KeyframeEffect is built from
//     the rule and the element's `animation-*` longhands. The SAME record
//     survives a restyle while the name stays at its index - a page that
//     changes `animation-duration` mid-flight keeps its start time - and a name
//     that goes away cancels it.
//
// Both are `animation_record`s in the Web Animations model (animations.cpp):
// the overlay samples them at the flush that made them, which is how a
// `-50s` delay on a `100s` duration reads as the midpoint synchronously.
//
// THE PROPERTY TABLE IS THE LIMIT. A longhand it does not carry -
// `animation-timing-function`, `animation-fill-mode`, `transition-behavior` -
// is an expando on `el.style` and never reaches the cascade, so its initial
// value is what applies here until the table grows the row.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/paint/values.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/easing.hpp>
#include <ctbrowser/style/engine.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell {
namespace {

constexpr double nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

[[nodiscard]] bool unresolved(double t) noexcept {
    return std::isnan(t);
}

using items = std::vector<std::string>;

// A comma-separated property value as its items, each trimmed. An empty value
// is an empty list; the caller supplies the initial value.
[[nodiscard]] items items_of(std::string_view text) {
    items out;
    for (const std::string_view part : split_top_level(text, ",")) {
        const std::string_view trimmed = trim(part, html_whitespace);
        if (!trimmed.empty()) { out.emplace_back(trimmed); }
    }
    return out;
}

// CSS Animations 1 §5.2: a list shorter than `animation-name` repeats.
[[nodiscard]] std::string_view item_at(const items & list, std::size_t i,
                                       std::string_view fallback) {
    return list.empty() ? fallback : list[i % list.size()];
}

// `<time>` as milliseconds: `100s`, `250ms`; anything else is `fallback`.
[[nodiscard]] double time_ms(std::string_view text, double fallback) {
    double n = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (ec != std::errc{}) { return fallback; }
    const std::string_view unit{end, static_cast<std::size_t>(text.data() + text.size() - end)};
    if (ascii_iequals(unit, "s")) { return n * 1000; }
    if (ascii_iequals(unit, "ms")) { return n; }
    return fallback;
}

[[nodiscard]] bool is_time(std::string_view text) {
    return !unresolved(time_ms(text, nan));
}

// THE TWO SHORTHANDS, READ HERE BECAUSE THE CASCADE KEEPS THEM WHOLE. The
// property table files `transition: opacity .15s linear` as one declaration
// (lib/Style/css/properties/shorthands.cpp, shape::whole), so the longhands
// this file asks for are empty while the page said everything. Their grammars
// are `<single-transition>#` (CSS Transitions 1 §2.5) and `<single-animation>#`
// (CSS Animations 1 §5.9): per item, the first `<time>` is the duration and
// the second the delay, an easing is the timing function, and the keywords
// that are not one of those are the property, or the animation's count,
// direction, fill, play state and name. A longhand the page did declare wins
// over the shorthand's item, which is the cascade's answer for a page that
// wrote the longhand after it.
// ponytail: this belongs in shorthands.cpp as a real split; it moves there
// when the table grows `transition-behavior` and the animation longhands.
struct shorthand_lists {
    items property, duration, delay, easing, behavior; // transition
    items name, count, direction, fill, play_state;    // animation, with the above
};

[[nodiscard]] shorthand_lists read_shorthand(std::string_view text, bool animation) {
    shorthand_lists out;
    for (const std::string & item : items_of(text)) {
        std::string property = animation ? "none" : "all";
        std::string duration = "0s";
        std::string delay = "0s";
        std::string easing = "ease";
        std::string behavior = "normal";
        std::string count = "1";
        std::string direction = "normal";
        std::string fill = "none";
        std::string play_state = "running";
        bool seen_duration = false;
        bool seen_delay = false;
        bool seen_easing = false;
        bool seen_count = false;
        bool seen_direction = false;
        bool seen_fill = false;
        bool seen_play_state = false;
        bool seen_name = false;
        for (const std::string_view part : split_top_level(item, html_whitespace)) {
            if (part.empty()) { continue; }
            if (is_time(part)) {
                if (!seen_duration) {
                    duration = std::string{part};
                    seen_duration = true;
                } else if (!seen_delay) {
                    delay = std::string{part};
                    seen_delay = true;
                }
                continue;
            }
            const std::string lower = ascii_lower_copy(part);
            // An easing keyword may also be a name: the easing reading wins
            // only while the slot is free, as the grammar's `||` says.
            if (!seen_easing && style::parse_easing(part)) {
                easing = std::string{part};
                seen_easing = true;
                continue;
            }
            if (!animation) {
                if (lower == "allow-discrete" || lower == "normal") {
                    behavior = lower;
                } else {
                    property = lower;
                }
                continue;
            }
            double n = 0;
            const bool numeric = std::from_chars(part.data(), part.data() + part.size(), n).ptr ==
                                     part.data() + part.size() &&
                                 n >= 0;
            if (!seen_count && (numeric || lower == "infinite")) {
                count = lower;
                seen_count = true;
            } else if (!seen_direction && (lower == "normal" || lower == "reverse" ||
                                           lower == "alternate" || lower == "alternate-reverse")) {
                direction = lower;
                seen_direction = true;
            } else if (!seen_fill && (lower == "none" || lower == "forwards" ||
                                      lower == "backwards" || lower == "both")) {
                fill = lower;
                seen_fill = true;
            } else if (!seen_play_state && (lower == "running" || lower == "paused")) {
                play_state = lower;
                seen_play_state = true;
            } else if (!seen_name) {
                property = std::string{part};
                seen_name = true;
            }
        }
        out.property.push_back(property);
        out.duration.push_back(duration);
        out.delay.push_back(delay);
        out.easing.push_back(easing);
        out.behavior.push_back(behavior);
        out.name.push_back(property);
        out.count.push_back(count);
        out.direction.push_back(direction);
        out.fill.push_back(fill);
        out.play_state.push_back(play_state);
    }
    return out;
}

// The computed text of one property on a style: what it declared or inherited,
// else the table's initial value.
[[nodiscard]] std::string_view value_on(const style::computed_style & style, atom_table & atoms,
                                        std::string_view property) {
    const std::string_view held = trim(style.get(atoms.intern(property)), html_whitespace);
    if (!held.empty()) { return held; }
    const style::css::property_syntax * known = style::css::find_property(property);
    return known != nullptr ? known->initial : std::string_view{};
}

// A property `transition-property: all` covers: not the animation and
// transition properties themselves, and not a custom property.
[[nodiscard]] bool covered_by_all(std::string_view property) {
    return !property.starts_with("--") && !property.starts_with("animation") &&
           !property.starts_with("transition");
}

[[nodiscard]] std::string_view unquoted_name(std::string_view text) {
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') &&
        text.back() == text.front()) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

} // namespace

// --- interpolation -------------------------------------------------------------

namespace {

[[nodiscard]] bool numeric_pair(std::string_view from, std::string_view to) {
    style::css::length_context ctx;
    const style::css::math_answer a = style::css::evaluate_math(from, ctx);
    const style::css::math_answer b = style::css::evaluate_math(to, ctx);
    return a.outcome == style::css::math_outcome::resolved &&
           b.outcome == style::css::math_outcome::resolved && a.value.type == b.value.type &&
           a.value.is_number == b.value.is_number;
}

// One colour between two, CSS Color 4 §17: in sRGB, premultiplied by alpha, so
// a transparent endpoint contributes no hue. Extrapolation clamps to the
// gamut, as a computed colour does. The text goes back through the same
// `rgb()` parser the computed-style serialiser reads, which rounds.
[[nodiscard]] std::string lerp_color(const color & a, const color & b, double p) {
    const auto channel = [](std::uint8_t v) { return static_cast<double>(v) / 255.0; };
    const double alpha_a = channel(a.alpha());
    const double alpha_b = channel(b.alpha());
    const auto lerp = [p](double x, double y) { return (1 - p) * x + p * y; };
    const double alpha = std::clamp(lerp(alpha_a, alpha_b), 0.0, 1.0);
    const auto mixed = [&](std::uint8_t x, std::uint8_t y) {
        const double premultiplied = lerp(channel(x) * alpha_a, channel(y) * alpha_b);
        const double v = alpha == 0 ? 0 : premultiplied / alpha;
        return std::clamp(v, 0.0, 1.0) * 255.0;
    };
    const auto number = [](double v) {
        char buffer[32];
        const auto [end, ec] =
            std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::fixed, 4);
        return ec == std::errc{} ? std::string{buffer, end} : std::string{"0"};
    };
    return "rgba(" + number(mixed(a.red(), b.red())) + ", " + number(mixed(a.green(), b.green())) +
           ", " + number(mixed(a.blue(), b.blue())) + ", " + number(alpha) + ")";
}

// CSS Values 4 §"combining values": two values interpolate when they are one
// number, length or percentage each; two colours; or LISTS of the same shape -
// comma-separated, then space-separated - whose items pair off as one of
// those or as identical text (`inset`, `/`, `auto`). `border-width: 20px 40px`,
// `box-shadow: red 2px 2px`, `background-size: 10px 20%` are all that. When the
// pair does not, `interpolable` is false and the answer flips at the midpoint.
[[nodiscard]] std::string interpolate_pair(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const style::css::length_context & ctx,
                                           bool & interpolable) {
    from = trim(from, html_whitespace);
    to = trim(to, html_whitespace);
    interpolable = true;
    if (const auto a = paint::parse_color(from), b = paint::parse_color(to); a && b) {
        return lerp_color(*a, *b, p);
    }
    if (numeric_pair(from, to)) { return style::interpolate_text(property, from, to, p, ctx); }
    const std::vector<std::string_view> lists_a = split_top_level(from, ",");
    const std::vector<std::string_view> lists_b = split_top_level(to, ",");
    const auto discrete = [&] {
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
    };
    if (lists_a.size() != lists_b.size()) { return discrete(); }
    std::string out;
    for (std::size_t i = 0; i < lists_a.size(); ++i) {
        const std::vector<std::string_view> items_a = split_top_level(lists_a[i], html_whitespace);
        const std::vector<std::string_view> items_b = split_top_level(lists_b[i], html_whitespace);
        if (items_a.size() != items_b.size() || items_a.empty()) { return discrete(); }
        // A single item on each side is the pair itself, already refused above.
        if (items_a.size() == 1 && lists_a.size() == 1) { return discrete(); }
        if (i != 0) { out += ", "; }
        for (std::size_t k = 0; k < items_a.size(); ++k) {
            const std::string_view x = trim(items_a[k], html_whitespace);
            const std::string_view y = trim(items_b[k], html_whitespace);
            if (x.empty() && y.empty()) { continue; }
            if (k != 0) { out += ' '; }
            if (x == y) {
                out += x;
                continue;
            }
            bool item_ok = true;
            const std::string piece = interpolate_pair(property, x, y, p, ctx, item_ok);
            if (!item_ok) { return discrete(); }
            out += piece;
        }
    }
    return out;
}

// THE COMPUTED SHAPE OF A VALUE WHOSE GRAMMAR LETS THE AUTHOR REORDER OR OMIT:
// a shadow is `<color> <x> <y> <blur> <spread> inset?` with the colour first
// and the omitted lengths zero (CSS Backgrounds 3 §7.2, the order the
// computed-style serialiser prints), a corner radius is two lengths. Paired
// as written, `10px 30px orange` against `green 20px 20px 20px` has nothing
// to interpolate; in computed shape it has a colour and three lengths.
[[nodiscard]] std::string computed_shape(std::string_view property, std::string_view text) {
    const bool box = property == "box-shadow";
    if (box || property == "text-shadow") {
        std::string out;
        for (const std::string_view shadow : split_top_level(text, ",")) {
            std::string colour;
            std::vector<std::string> lengths;
            bool inset = false;
            for (const std::string_view raw : split_top_level(shadow, html_whitespace)) {
                const std::string_view part = trim(raw, html_whitespace);
                if (part.empty()) { continue; }
                if (ascii_iequals(part, "inset")) {
                    inset = true;
                } else if (ascii_iequals(part, "currentcolor") || paint::parse_color(part)) {
                    colour = std::string{part};
                } else {
                    lengths.emplace_back(part);
                }
            }
            const std::size_t wanted = box ? 4 : 3;
            if (lengths.size() < 2 || lengths.size() > wanted) { return std::string{text}; }
            while (lengths.size() < wanted) { lengths.emplace_back("0px"); }
            if (!out.empty()) { out += ", "; }
            out += colour.empty() ? std::string{"currentcolor"} : colour;
            for (const std::string & len : lengths) { out += ' ' + len; }
            if (inset) { out += " inset"; }
        }
        return out.empty() ? std::string{text} : out;
    }
    if (property.starts_with("border-") && property.ends_with("-radius")) {
        const std::vector<std::string_view> parts = split_top_level(text, html_whitespace);
        if (parts.size() == 1) { return std::string{parts[0]} + ' ' + std::string{parts[0]}; }
    }
    return std::string{text};
}

} // namespace

std::string dom_bindings::interpolate_value(std::string_view property, std::string_view from,
                                            std::string_view to, double p,
                                            const style::css::length_context & ctx) {
    bool interpolable = true;
    return interpolate_pair(property, computed_shape(property, from), computed_shape(property, to),
                            p, ctx, interpolable);
}

bool dom_bindings::transitionable(std::string_view property, std::string_view from,
                                  std::string_view to) {
    bool interpolable = true;
    style::css::length_context ctx;
    (void)interpolate_pair(property, computed_shape(property, from), computed_shape(property, to),
                           0.5, ctx, interpolable);
    return interpolable;
}

// --- the records -----------------------------------------------------------------

void dom_bindings::cancel_record(std::size_t index) {
    animation_record & a = animations_[index];
    const double active = sample_timing(a).active_time;
    a.cancelled_at = unresolved(active) ? 0 : active;
    if (cx_ != nullptr && play_state(a) != "idle" && !a.finished_settled) {
        cx_->settle_promise(a.finished,
                            make_dom_exception(*cx_, "AbortError", "The user aborted a request."),
                            true);
        a.finished = cx_->make_pending_promise();
        a.finished_settled = false;
    }
    a.hold_time = nan;
    a.start_time = nan;
    ++animation_generation_;
    sync_animation_roots();
}

void dom_bindings::fire_animation_event(std::size_t index, std::string_view type,
                                        double elapsed_ms) {
    if (cx_ == nullptr) { return; }
    const animation_record & a = animations_[index];
    const bool transition = a.kind == animation_kind::css_transition;
    const value event = make_event(*cx_, type, a.owner);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("cancelable", value::boolean(false));
    // The interface's prototype, off its global constructor: the event
    // interfaces are not in the node-interface registry `interface_prototype`
    // reads, and `instanceof AnimationEvent` is what a page asks.
    // The constructor is a native, whose `prototype` is an own slot rather
    // than a property the chain walk finds - the same read `instanceof` does.
    if (const value ctor = cx_->global(transition ? "TransitionEvent" : "AnimationEvent");
        ctor.is_kind(script::heap_kind::native)) {
        const value * proto =
            static_cast<script::native_object *>(ctor.as_heap())->find("prototype");
        if (proto != nullptr && proto->is_object()) { object->prototype = *proto; }
    }
    object->set(transition ? "propertyName" : "animationName", cx_->string(a.name));
    object->set("elapsedTime", value::number(elapsed_ms / 1000.0));
    object->set("pseudoElement", cx_->string(""));
    (void)dispatch_event(type, a.owner, event);
}

// --- the update ------------------------------------------------------------------

void dom_bindings::update_css_animations(const read_txn & txn, const style::style_map & before,
                                         const style::style_map & after) {
    if (cx_ == nullptr || selector_engine_ == nullptr) { return; }
    // WHO OWNS WHAT, once: a page in the interpolation harness holds hundreds
    // of elements and hundreds of live records, and asking "this element's
    // records" by scanning the list per element made a flush quadratic.
    owned_.clear();
    for (std::size_t i = 0; i < animations_.size(); ++i) {
        animation_record & a = animations_[i];
        a.seen = false;
        if (a.kind != animation_kind::script && play_state(a) != "idle") {
            owned_[a.owner.key()].push_back(i);
        }
    }
    static const std::vector<std::size_t> nothing;
    const auto mine = [&](node_id node) -> const std::vector<std::size_t> & {
        const auto it = owned_.find(node.key());
        return it == owned_.end() ? nothing : it->second;
    };
    std::size_t order = 0;
    const auto walk = [&](auto && self, node_id node) -> void {
        if (txn.kind(node).value_or(node_kind::comment) == node_kind::element) {
            const std::size_t here = order++;
            const auto now = after.find(style::engine::key_of(node));
            if (now != after.end() && now->second) {
                const auto was = before.find(style::engine::key_of(node));
                const style::computed_style_ptr previous =
                    was != before.end() ? was->second : style::computed_style_ptr{};
                if (previous == now->second) {
                    // Unchanged: everything it owns stays, at its new place.
                    for (const std::size_t i : mine(node)) {
                        animations_[i].seen = true;
                        animations_[i].tree_order = here;
                    }
                } else {
                    const std::vector<std::size_t> & records = mine(node);
                    if (previous) {
                        // The before-change style: the previous text with the
                        // running animations sampled on top, at this moment.
                        std::vector<std::pair<std::string, std::string>> current;
                        if (!records.empty()) {
                            current = animated_values(
                                node, 16.0f, [&](std::string_view property) -> std::string_view {
                                    return previous->get(atoms_->intern(property));
                                });
                        }
                        update_css_transitions(node, here, *previous, *now->second, current,
                                               records);
                    }
                    update_css_animation_list(node, here, *now->second, records);
                }
            }
        }
        for (const node_id child : txn.children(node)) { self(self, child); }
    };
    walk(walk, txn.root());
    // Whatever no element kept - the element left the document, lost its
    // style, or its name went away - is cancelled. Its event, like every
    // other, fires from the browser's next tick (tick_animations): nothing
    // here runs script, because this runs inside a flush a script asked for.
    for (std::size_t i = 0; i < animations_.size(); ++i) {
        animation_record & a = animations_[i];
        if (a.kind == animation_kind::script || a.seen || play_state(a) == "idle") { continue; }
        cancel_record(i);
    }
}

void dom_bindings::update_css_transitions(
    node_id element, std::size_t tree_order, const style::computed_style & before,
    const style::computed_style & after,
    const std::vector<std::pair<std::string, std::string>> & current,
    const std::vector<std::size_t> & records) {
    const shorthand_lists whole = read_shorthand(after.get(atoms_->intern("transition")), false);
    const auto list = [&](std::string_view property, const items & from_shorthand) {
        items own = items_of(after.get(atoms_->intern(property)));
        return own.empty() ? from_shorthand : own;
    };
    const items properties = list("transition-property", whole.property);
    const items durations = list("transition-duration", whole.duration);
    const items delays = list("transition-delay", whole.delay);
    const items easings = list("transition-timing-function", whole.easing);
    const items behaviors = list("transition-behavior", whole.behavior);
    // THE COMMON CASE IS NOTHING: `transition-property` is `all` on every
    // element, so every changed element arrives here, and a duration of 0s
    // with no transition already running is a page that asked for none.
    const bool owns_one = std::ranges::any_of(records, [&](std::size_t i) {
        return animations_[i].kind == animation_kind::css_transition;
    });
    if (!owns_one) {
        bool any_positive = false;
        const std::size_t count = std::max(durations.size(), delays.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (time_ms(item_at(durations, i, "0s"), 0) + time_ms(item_at(delays, i, "0s"), 0) >
                0) {
                any_positive = true;
            }
        }
        if (!any_positive) { return; }
    }
    const bool none = std::ranges::any_of(
        properties, [](const std::string & p) { return ascii_iequals(p, "none"); });
    const bool all =
        !none && (properties.empty() || std::ranges::any_of(properties, [](const std::string & p) {
                      return ascii_iequals(p, "all");
                  }));

    // The properties to look at: everything either style mentions when `all`
    // is in force, plus the ones named, plus the ones a transition already
    // runs on (so a name dropped from the list cancels it).
    std::vector<std::string> candidates;
    const auto consider = [&candidates](std::string_view name) {
        if (std::ranges::find(candidates, name) == candidates.end()) {
            candidates.emplace_back(name);
        }
    };
    if (all) {
        const auto each = [&](const style::declaration_list & declarations) {
            for (const style::declaration & d : declarations) {
                const std::string_view name = atoms_->text(d.property);
                if (covered_by_all(name)) { consider(name); }
            }
        };
        each(before.declarations);
        each(after.declarations);
        if (before.inherited) { each(before.inherited->declarations); }
        if (after.inherited) { each(after.inherited->declarations); }
    }
    for (const std::string & p : properties) {
        if (ascii_iequals(p, "all") || ascii_iequals(p, "none")) { continue; }
        const std::string name = p.starts_with("--") ? p : ascii_lower_copy(p);
        // A SHORTHAND NAMES ITS LONGHANDS (CSS Transitions 1 §2.1): the
        // cascade holds `border-width` as four, and each gets its transition.
        const std::span<const std::string_view> longhands = style::css::longhands_of(name);
        if (longhands.empty()) {
            consider(name);
        } else {
            for (const std::string_view longhand : longhands) { consider(longhand); }
        }
    }
    for (const std::size_t i : records) {
        if (animations_[i].kind == animation_kind::css_transition) {
            consider(animations_[i].name);
        }
    }

    for (const std::string & property : candidates) {
        // The matching `transition-property` item: the last one naming it, or
        // `all` (CSS Transitions 1 §2.1).
        std::size_t match = properties.size();
        for (std::size_t i = 0; i < properties.size(); ++i) {
            const std::string & item = properties[i];
            bool names_it = ascii_iequals(item, property) ||
                            (ascii_iequals(item, "all") && covered_by_all(property));
            for (const std::string_view longhand :
                 style::css::longhands_of(ascii_lower_copy(item))) {
                if (longhand == property) { names_it = true; }
            }
            if (names_it) { match = i; }
        }
        const bool matched = !none && (match < properties.size() ||
                                       (properties.empty() && covered_by_all(property)));
        // The element's transition for this property, running or completed.
        std::size_t existing = no_record;
        for (const std::size_t i : records) {
            const animation_record & a = animations_[i];
            if (a.kind == animation_kind::css_transition && a.name == property &&
                play_state(a) != "idle") {
                existing = i;
            }
        }
        if (!matched) {
            if (existing != no_record) { cancel_record(existing); }
            continue;
        }
        const double duration = std::max(0.0, time_ms(item_at(durations, match, "0s"), 0));
        const double delay = time_ms(item_at(delays, match, "0s"), 0);
        const std::string easing{item_at(easings, match, "ease")};
        const bool discrete_ok =
            ascii_iequals(item_at(behaviors, match, "normal"), "allow-discrete");
        const double combined = duration + delay;

        std::string before_value{value_on(before, *atoms_, property)};
        for (const auto & [name, text] : current) {
            if (name == property) { before_value = trim(text, html_whitespace); }
        }
        const std::string after_value{value_on(after, *atoms_, property)};

        const auto start = [&](const std::string & from, const std::string & to, double start_delay,
                               double active, double factor, const std::string & reversing_start) {
            keyframe_effect_record made;
            made.target = element;
            made.timing.delay = start_delay;
            made.timing.duration = active;
            made.timing.fill = "backwards";
            made.timing.easing = style::parse_easing(easing) ? easing : "ease";
            animation_keyframe first;
            first.offset = 0;
            first.offset_given = true;
            first.values.emplace_back(property, from);
            animation_keyframe last;
            last.offset = 1;
            last.offset_given = true;
            last.values.emplace_back(property, to);
            made.keyframes = {first, last};
            const value animation = make_animation(*cx_, push_effect(*cx_, std::move(made)));
            const std::size_t index = animation_index(animation);
            animation_record & a = animations_[index];
            static_cast<script::object_object *>(animation.as_heap())->prototype =
                css_transition_prototype_;
            a.kind = animation_kind::css_transition;
            a.name = property;
            a.owner = element;
            a.position = index;
            a.tree_order = tree_order;
            a.seen = true;
            a.end_value = to;
            a.reversing_start = reversing_start;
            a.reversing_factor = factor;
            a.start_time = now_ms_;
            a.hold_time = nan;
            update_finished_state(*cx_, index);
        };

        if (existing == no_record) {
            if (before_value != after_value && combined > 0 &&
                (discrete_ok || transitionable(property, before_value, after_value))) {
                start(before_value, after_value, delay, duration, 1, before_value);
            }
            continue;
        }
        animation_record & running = animations_[existing];
        running.seen = true;
        running.tree_order = tree_order;
        if (play_state(running) == "finished") {
            // A COMPLETED transition stays until the property moves on from
            // its end value; then it is forgotten and the change is a fresh one.
            if (running.end_value == after_value) { continue; }
            cancel_record(existing);
            if (before_value != after_value && combined > 0 &&
                (discrete_ok || transitionable(property, before_value, after_value))) {
                start(before_value, after_value, delay, duration, 1, before_value);
            }
            continue;
        }
        if (running.end_value == after_value) { continue; }
        // The running transition's end moved. Its current value is what the
        // before-change style carries.
        if (before_value == after_value || combined <= 0 ||
            !(discrete_ok || transitionable(property, before_value, after_value))) {
            cancel_record(existing);
            continue;
        }
        if (running.reversing_start == after_value) {
            // §3.1: heading back where it came from, in proportion to how far
            // it got - by the timing function's output, not the clock's.
            const double progress = sample_timing(running).progress;
            const double old_factor = running.reversing_factor;
            const double factor = std::clamp(
                std::fabs((unresolved(progress) ? 0 : progress) * old_factor + 1 - old_factor), 0.0,
                1.0);
            const std::string reversing_start = running.end_value;
            cancel_record(existing);
            start(before_value, after_value, delay < 0 ? delay * factor : delay, duration * factor,
                  factor, reversing_start);
            continue;
        }
        cancel_record(existing);
        start(before_value, after_value, delay, duration, 1, before_value);
    }
}

void dom_bindings::update_css_animation_list(node_id element, std::size_t tree_order,
                                             const style::computed_style & after,
                                             const std::vector<std::size_t> & records) {
    const shorthand_lists whole = read_shorthand(after.get(atoms_->intern("animation")), true);
    const auto list = [&](std::string_view property, const items & from_shorthand) {
        items own = items_of(after.get(atoms_->intern(property)));
        return own.empty() ? from_shorthand : own;
    };
    const items names = list("animation-name", whole.name);
    const items durations = list("animation-duration", whole.duration);
    const items delays = list("animation-delay", whole.delay);
    const items counts = list("animation-iteration-count", whole.count);
    const items directions = list("animation-direction", whole.direction);
    const items fills = list("animation-fill-mode", whole.fill);
    const items states = list("animation-play-state", whole.play_state);
    const items easings = list("animation-timing-function", whole.easing);
    const items compositions = list("animation-composition", items{});

    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::string name{unquoted_name(names[i])};
        const style::keyframes_rule * rule =
            ascii_iequals(name, "none") ? nullptr : selector_engine_->keyframes_of(name);
        if (rule == nullptr) { continue; }

        // The effect the element's longhands describe (CSS Animations 1 §5).
        effect_timing timing;
        timing.duration = std::max(0.0, time_ms(item_at(durations, i, "0s"), 0));
        timing.delay = time_ms(item_at(delays, i, "0s"), 0);
        {
            const std::string_view count = item_at(counts, i, "1");
            if (ascii_iequals(count, "infinite")) {
                timing.iterations = infinity;
            } else {
                double n = 1;
                const auto [end, ec] =
                    std::from_chars(count.data(), count.data() + count.size(), n);
                timing.iterations = ec == std::errc{} && n >= 0 ? n : 1;
            }
        }
        const std::string direction = ascii_lower_copy(item_at(directions, i, "normal"));
        timing.direction =
            direction == "reverse" || direction == "alternate" || direction == "alternate-reverse"
                ? direction
                : "normal";
        const std::string fill = ascii_lower_copy(item_at(fills, i, "none"));
        timing.fill = fill == "forwards" || fill == "backwards" || fill == "both" ? fill : "none";
        timing.easing = "linear"; // the timing function is the keyframes' (§5.2)
        const std::string_view easing = item_at(easings, i, "ease");
        const std::string element_easing =
            style::parse_easing(easing) ? std::string{easing} : std::string{"ease"};
        const std::string composition = ascii_lower_copy(item_at(compositions, i, "replace"));
        const std::string composite =
            composition == "add" || composition == "accumulate" ? composition : "replace";
        const bool paused = ascii_iequals(item_at(states, i, "running"), "paused");
        std::vector<animation_keyframe> keyframes;
        for (const style::keyframes_rule::keyframe & k : rule->keyframes) {
            animation_keyframe frame;
            frame.offset = k.offset;
            frame.offset_given = true;
            frame.easing = k.easing.empty() ? element_easing : k.easing;
            frame.composite = k.composite.empty() ? "auto" : k.composite;
            frame.values = k.values;
            keyframes.push_back(std::move(frame));
        }

        // The record at this index, kept while its name is the same.
        std::size_t index = no_record;
        for (const std::size_t r : records) {
            const animation_record & a = animations_[r];
            if (a.kind == animation_kind::css_animation && a.position == i &&
                play_state(a) != "idle") {
                if (a.name == name) {
                    index = r;
                } else {
                    cancel_record(r);
                }
            }
        }
        if (index == no_record) {
            keyframe_effect_record made;
            made.target = element;
            made.composite = composite;
            made.timing = timing;
            made.keyframes = std::move(keyframes);
            const value animation = make_animation(*cx_, push_effect(*cx_, std::move(made)));
            index = animation_index(animation);
            animation_record & a = animations_[index];
            static_cast<script::object_object *>(animation.as_heap())->prototype =
                css_animation_prototype_;
            a.kind = animation_kind::css_animation;
            a.name = name;
            a.owner = element;
            a.position = i;
            // Playing from the style change event, or paused at its start.
            if (paused) {
                a.hold_time = 0;
            } else {
                a.start_time = now_ms_;
            }
            a.css_paused = paused;
        } else {
            animation_record & a = animations_[index];
            keyframe_effect_record & e = effects_[a.effect];
            e.composite = composite;
            e.timing = timing;
            e.keyframes = std::move(keyframes);
            // `animation-play-state` moved: the CSS pause and its release,
            // without disturbing a pause a script made (CSS Animations 2 §5.3).
            if (paused && !a.css_paused) {
                const double current = animation_current_time(a);
                a.hold_time = unresolved(current) ? 0 : current;
                a.start_time = nan;
            } else if (!paused && a.css_paused && !unresolved(a.hold_time)) {
                a.start_time = now_ms_ - a.hold_time / (a.playback_rate == 0 ? 1 : a.playback_rate);
                a.hold_time = nan;
            }
            a.css_paused = paused;
        }
        animation_record & a = animations_[index];
        a.seen = true;
        a.tree_order = tree_order;
        update_finished_state(*cx_, index);
    }
}

// --- events ----------------------------------------------------------------------

void dom_bindings::tick_animations() {
    if (cx_ == nullptr) { return; }
    std::vector<std::size_t> which;
    for (std::size_t i = 0; i < animations_.size(); ++i) {
        if (animations_[i].effect != no_record) { which.push_back(i); }
    }
    std::ranges::sort(which,
                      [this](std::size_t a, std::size_t b) { return composites_before(a, b); });
    for (const std::size_t i : which) {
        // Every animation's finished promise settles as the clock moves, not
        // only when a page reads a property off it.
        if (play_state(animations_[i]) != "idle") { update_finished_state(*cx_, i); }
        animation_record & a = animations_[i];
        if (a.kind == animation_kind::script) { continue; }
        const timing_sample s = sample_timing(a);
        const effect_phase was = a.reported_phase;
        const effect_phase now = play_state(a) == "idle" ? effect_phase::idle : s.phase;
        const effect_timing & t = effects_[a.effect].timing;
        const double active_duration =
            t.duration == 0 || t.iterations == 0 ? 0 : t.duration * t.iterations;
        const double entry = std::max(std::min(-t.delay, active_duration), 0.0);
        a.reported_phase = now;
        const double iteration = s.iteration;
        const double was_iteration = a.reported_iteration;
        a.reported_iteration = std::isinf(iteration) ? was_iteration : iteration;
        // The events are the phase crossings (CSS Animations 2 §4.2, CSS
        // Transitions 2 §5), each with the elapsed time the table there gives.
        const bool transition = a.kind == animation_kind::css_transition;
        const auto fire = [&](std::string_view type, double elapsed) {
            fire_animation_event(i, type, elapsed);
        };
        if (was == now) {
            if (!transition && now == effect_phase::active && iteration != was_iteration &&
                !std::isinf(iteration)) {
                fire("animationiteration", iteration * t.duration);
            }
            continue;
        }
        if (now == effect_phase::idle) {
            if (was == effect_phase::active || was == effect_phase::before) {
                fire(transition ? "transitioncancel" : "animationcancel", a.cancelled_at);
            }
            continue;
        }
        if (transition) {
            if (was == effect_phase::idle) { fire("transitionrun", entry); }
            if (now == effect_phase::active && was != effect_phase::after) {
                fire("transitionstart", entry);
            } else if (now == effect_phase::after) {
                if (was != effect_phase::active) { fire("transitionstart", entry); }
                fire("transitionend", active_duration);
            } else if (now == effect_phase::active && was == effect_phase::after) {
                fire("transitionstart", active_duration);
            }
            continue;
        }
        if (now == effect_phase::active) {
            fire("animationstart", was == effect_phase::after ? active_duration : entry);
        } else if (now == effect_phase::after) {
            if (was != effect_phase::active) { fire("animationstart", entry); }
            fire("animationend", active_duration);
        } else if (now == effect_phase::before) {
            if (was == effect_phase::active) { fire("animationend", 0); }
        }
    }
}

double dom_bindings::next_animation_event_ms() const noexcept {
    double soonest = infinity;
    for (const animation_record & a : animations_) {
        if (a.effect == no_record || a.kind == animation_kind::script || a.playback_rate <= 0 ||
            unresolved(a.start_time) || !unresolved(a.hold_time)) {
            continue;
        }
        const effect_timing & t = effects_[a.effect].timing;
        const double local = (now_ms_ - a.start_time) * a.playback_rate;
        const double active_duration =
            t.duration == 0 || t.iterations == 0 ? 0 : t.duration * t.iterations;
        const double end = t.delay + active_duration;
        double next = infinity;
        if (local < t.delay) {
            next = t.delay;
        } else if (local < end) {
            next = end;
            if (t.duration > 0 && a.kind == animation_kind::css_animation) {
                next = std::min(next, t.delay + (std::floor((local - t.delay) / t.duration) + 1) *
                                                    t.duration);
            }
        }
        if (!std::isinf(next)) {
            soonest = std::min(soonest, std::max(0.0, (next - local) / a.playback_rate));
        }
    }
    return soonest;
}

} // namespace ctbrowser::shell
