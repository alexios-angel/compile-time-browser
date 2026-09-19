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

#include "animations/helpers.hpp"

namespace ctbrowser::shell {
using namespace animation_detail;
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
    roots.push_back(css_animation_prototype_);
    roots.push_back(css_transition_prototype_);
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

// Composite order, Web Animations §5.4.2 with CSS Animations 2 §5.2 and CSS
// Transitions 2 §4.2 folded in: every transition before every animation
// before every script-made one; within a class by the owner's tree order,
// then by position in the list that made it; then by creation.
bool dom_bindings::composites_before(std::size_t a, std::size_t b) const noexcept {
    const animation_record & x = animations_[a];
    const animation_record & y = animations_[b];
    if (x.kind != y.kind) { return x.kind < y.kind; }
    if (x.kind != animation_kind::script) {
        if (x.tree_order != y.tree_order) { return x.tree_order < y.tree_order; }
        if (x.position != y.position) { return x.position < y.position; }
    }
    return a < b;
}

std::vector<std::size_t> dom_bindings::animations_on(node_id id, bool subtree) const {
    std::vector<std::size_t> out;
    const auto txn = doc_->read();
    for (std::size_t i = 0; i < animations_.size(); ++i) {
        const animation_record & a = animations_[i];
        if (a.effect == no_record || play_state(a) == "idle") { continue; }
        // RELEVANT ONLY (§5.4): a finished animation that fills forwards is
        // in effect and stays; one that does not is over, and a completed
        // transition is exactly that.
        if (play_state(a) == "finished") {
            const std::string & fill = effects_[a.effect].timing.fill;
            if (fill != "forwards" && fill != "both") { continue; }
        }
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
    std::ranges::sort(out,
                      [this](std::size_t a, std::size_t b) { return composites_before(a, b); });
    return out;
}

dom_bindings::timing_sample dom_bindings::sample_timing(const animation_record & a) const noexcept {
    timing_sample out;
    if (a.effect == no_record) { return out; }
    const keyframe_effect_record & e = effects_[a.effect];
    const effect_timing & t = e.timing;
    const double local = animation_current_time(a);
    if (unresolved(local)) { return out; }

    // §4.8.3: the phase, then the active time, filled or not.
    const double active_duration =
        t.duration == 0 || t.iterations == 0 ? 0 : t.duration * t.iterations;
    const double end = effect_end_time(e);
    const double before_boundary = std::max(std::min(t.delay, end), 0.0);
    const double after_boundary = std::max(std::min(t.delay + active_duration, end), 0.0);
    out.phase = effect_phase::active;
    if (local < before_boundary || (a.playback_rate < 0 && local == before_boundary)) {
        out.phase = effect_phase::before;
    } else if (local > after_boundary || (a.playback_rate >= 0 && local == after_boundary)) {
        out.phase = effect_phase::after;
    }
    const bool fill_backwards = t.fill == "backwards" || t.fill == "both";
    const bool fill_forwards = t.fill == "forwards" || t.fill == "both";
    double active_time = nan;
    switch (out.phase) {
    case effect_phase::before: active_time = fill_backwards ? 0 : nan; break;
    case effect_phase::active: active_time = local - t.delay; break;
    case effect_phase::after: active_time = fill_forwards ? active_duration : nan; break;
    case effect_phase::idle: break;
    }
    // The iteration the events count is the one the ACTIVE time is in,
    // filled or not - so it is computed before the fill decides anything.
    const double counted_time = out.phase == effect_phase::active  ? local - t.delay
                                : out.phase == effect_phase::after ? active_duration
                                                                   : 0;
    out.active_time = active_time;
    if (unresolved(active_time)) {
        out.iteration = t.duration == 0 ? 0 : std::floor(counted_time / t.duration);
        return out;
    }

    // §4.8.4-4.8.7: overall, simple iteration and directed progress.
    double overall = t.duration == 0 ? (out.phase == effect_phase::before ? 0 : t.iterations)
                                     : active_time / t.duration;
    overall += t.iteration_start;
    double simple = std::isinf(overall) ? 0 : std::fmod(overall, 1.0);
    if (simple < 0) { simple += 1; }
    const bool at_end = active_time == active_duration && t.iterations != 0;
    if (simple == 0 && at_end &&
        (out.phase == effect_phase::active || out.phase == effect_phase::after)) {
        simple = 1;
    }
    double iteration = std::floor(overall);
    if (out.phase == effect_phase::after && std::isinf(t.iterations)) {
        iteration = infinity;
    } else if (simple == 1) {
        iteration = std::floor(overall) - 1;
    }
    out.iteration = iteration;
    bool forwards = true;
    if (t.direction == "reverse") {
        forwards = false;
    } else if (t.direction == "alternate" || t.direction == "alternate-reverse") {
        const bool even = std::isinf(iteration) || std::fmod(iteration, 2.0) == 0;
        forwards = t.direction == "alternate" ? even : !even;
    }
    const double directed = forwards ? simple : 1 - simple;
    const bool before_flag = (forwards && out.phase == effect_phase::before) ||
                             (!forwards && out.phase == effect_phase::after);
    const style::easing timing_easing = style::parse_easing(t.easing).value_or(style::easing{});
    out.progress = timing_easing(directed, before_flag);
    return out;
}

void dom_bindings::install_animations(context & cx) {
    if (animation_interface_ != nullptr) { return; }
    const auto illegal = [](context & c) {
        c.throw_error("TypeError", "Illegal invocation");
        return value::undefined();
    };

    // --- DocumentTimeline, and the one the document has -------------------
    auto * timeline_proto = cx.allocate<script::object_object>();
    define_getter(cx, *timeline_proto, "currentTime",
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
    define_getter(cx, *effect_proto, "target",
                  [this, effect_of, illegal](context & c, std::span<value>) {
                      const keyframe_effect_record * e = effect_of(c);
                      if (e == nullptr) { return illegal(c); }
                      return e->target ? wrap(c, e->target) : value::null();
                  });
    define_getter(cx, *effect_proto, "pseudoElement",
                  [](context &, std::span<value>) { return value::null(); });
    define_getter(
        cx, *effect_proto, "composite",
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
    set_method(
        cx, *effect_proto, "getKeyframes",
        [effect_of, illegal](context & c, std::span<value>) {
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
        },
        script::attr_builtin);
    set_method(
        cx, *effect_proto, "setKeyframes",
        [this, effect_of, illegal](context & c, std::span<value> args) {
            keyframe_effect_record * e = effect_of(c);
            if (e == nullptr) { return illegal(c); }
            std::vector<animation_keyframe> fresh;
            if (!read_keyframes(c, arg(args, 0), fresh)) { return value::undefined(); }
            e->keyframes = std::move(fresh);
            ++animation_generation_;
            return value::undefined();
        },
        script::attr_builtin);
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
    set_method(
        cx, *effect_proto, "getTiming",
        [effect_of, illegal, timing_object](context & c, std::span<value>) {
            const keyframe_effect_record * e = effect_of(c);
            if (e == nullptr) { return illegal(c); }
            return value::object(timing_object(c, e->timing));
        },
        script::attr_builtin);
    set_method(
        cx, *effect_proto, "getComputedTiming",
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
        },
        script::attr_builtin);
    set_method(
        cx, *effect_proto, "updateTiming",
        [this, effect_of, illegal](context & c, std::span<value> args) {
            keyframe_effect_record * e = effect_of(c);
            if (e == nullptr) { return illegal(c); }
            effect_timing fresh = e->timing;
            if (!read_timing(c, arg(args, 0), fresh)) { return value::undefined(); }
            e->timing = fresh;
            ++animation_generation_;
            return value::undefined();
        },
        script::attr_builtin);
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
    define_getter(
        cx, *proto, "effect",
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
    define_getter(cx, *proto, "timeline",
                  [this](context &, std::span<value>) { return timeline_; });
    define_getter(
        cx, *proto, "startTime",
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
    define_getter(
        cx, *proto, "currentTime",
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
    define_getter(
        cx, *proto, "playbackRate",
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
    define_getter(cx, *proto, "playState", [this, self_index](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        if (i == no_record) { return value::undefined(); }
        update_finished_state(c, i);
        return c.string(std::string{play_state(animations_[i])});
    });
    define_getter(cx, *proto, "replaceState",
                  [](context & c, std::span<value>) { return c.string("active"); });
    define_getter(cx, *proto, "pending",
                  [](context &, std::span<value>) { return value::boolean(false); });
    define_getter(cx, *proto, "ready", [this, self_index](context & c, std::span<value>) {
        const std::size_t i = self_index(c);
        return i == no_record ? value::undefined() : c.make_promise(animations_[i].self, false);
    });
    define_getter(cx, *proto, "finished", [this, self_index](context & c, std::span<value>) {
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
    set_method(
        cx, *proto, "play",
        [self_index, play](context & c, std::span<value>) {
            const std::size_t i = self_index(c);
            if (i != no_record) { play(c, i); }
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "pause",
        [this, self_index](context & c, std::span<value>) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            animation_record & a = animations_[i];
            if (unresolved(a.hold_time)) {
                const double current = animation_current_time(a);
                if (a.playback_rate >= 0) {
                    a.hold_time = unresolved(current) ? 0 : current;
                } else {
                    const double end =
                        a.effect == no_record ? 0 : effect_end_time(effects_[a.effect]);
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
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "finish",
        [this, self_index](context & c, std::span<value>) {
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
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "cancel",
        [this, self_index](context & c, std::span<value>) {
            const std::size_t i = self_index(c);
            if (i != no_record) { cancel_record(i); }
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "reverse",
        [this, self_index, play](context & c, std::span<value>) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            animation_record & a = animations_[i];
            const double current = animation_current_time(a);
            a.playback_rate = -a.playback_rate;
            if (!unresolved(current)) { set_animation_current_time(a, current); }
            play(c, i);
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "updatePlaybackRate",
        [this, self_index](context & c, std::span<value> args) {
            const std::size_t i = self_index(c);
            if (i == no_record) { return value::undefined(); }
            animation_record & a = animations_[i];
            const double current = animation_current_time(a);
            a.playback_rate = arg_number(args, 0);
            if (!unresolved(current)) { set_animation_current_time(a, current); }
            update_finished_state(c, i);
            return value::undefined();
        },
        script::attr_builtin);
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

    // --- CSSAnimation and CSSTransition ------------------------------------
    // The two the cascade makes (bindings/animations/css.cpp): an Animation
    // with one attribute naming what made it. Neither is constructible.
    const auto css_interface = [this, &cx, proto](const char * name, const char * attribute,
                                                  animation_kind kind) {
        auto * sub = cx.allocate<script::object_object>();
        sub->prototype = value::object(proto);
        define_getter(cx, *sub, attribute, [this, kind](context & c, std::span<value>) {
            const std::size_t i = animation_index(c.current_this());
            if (i == no_record || animations_[i].kind != kind) {
                c.throw_error("TypeError", "Illegal invocation");
                return value::undefined();
            }
            return c.string(animations_[i].name);
        });
        auto * sub_ctor =
            cx.allocate<script::native_object>(name, [](context & c, std::span<value>) {
                c.throw_error("TypeError", "Illegal constructor");
                return value::undefined();
            });
        sub_ctor->define("prototype", value::object(sub), script::attr_none);
        sub->define("constructor", value::object(sub_ctor), script::attr_builtin);
        cx.define_global(name, value::object(sub_ctor));
        return value::object(sub);
    };
    css_animation_prototype_ =
        css_interface("CSSAnimation", "animationName", animation_kind::css_animation);
    css_transition_prototype_ =
        css_interface("CSSTransition", "transitionProperty", animation_kind::css_transition);

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
        set_method(
            cx, *element_proto, "animate",
            [this, play](context & c, std::span<value> args) {
                const node_id id = receiver(c);
                if (!id) {
                    c.throw_error("TypeError", "Illegal invocation");
                    return value::undefined();
                }
                const value effect = make_keyframe_effect(c, id, arg(args, 0), arg(args, 1));
                if (!effect.is_object()) { return value::undefined(); }
                const value animation = make_animation(c, effect_index(effect));
                const value options = arg(args, 1);
                if (const value name = dict_member(c, options, "id"); !name.is_undefined()) {
                    static_cast<script::object_object *>(animation.as_heap())
                        ->set("id", c.string(c.to_string(name)));
                }
                play(c, animation_index(animation));
                return animation;
            },
            script::attr_builtin);
        set_method(
            cx, *element_proto, "getAnimations",
            [this, animations_array](context & c, std::span<value> args) {
                const node_id id = receiver(c);
                if (!id) {
                    c.throw_error("TypeError", "Illegal invocation");
                    return value::undefined();
                }
                // "Update the style" first (§5.5): the cascade's animations
                // exist once the pending restyle has run.
                flush_layout();
                const value options = arg(args, 0);
                const bool subtree = dict_flag(c, options, "subtree");
                return animations_array(c, animations_on(id, subtree));
            },
            script::attr_builtin);
    }
    if (script::object_object * doc = document_object()) {
        doc->define_accessor(
            "timeline",
            value::object(cx.allocate<script::native_object>(
                "timeline", [this](context &, std::span<value>) { return timeline_; })),
            value::undefined());
        // Every animation whose target is in the document, in creation order.
        set_method(
            cx, *doc, "getAnimations",
            [this, animations_array](context & c, std::span<value>) {
                flush_layout();
                std::vector<std::size_t> which;
                const auto txn = doc_->read();
                for (std::size_t i = 0; i < animations_.size(); ++i) {
                    const animation_record & a = animations_[i];
                    if (a.effect == no_record || play_state(a) == "idle") { continue; }
                    node_id up = effects_[a.effect].target;
                    while (up && txn.parent(up) && txn.parent(up) != up) { up = txn.parent(up); }
                    if (up && up == txn.root()) { which.push_back(i); }
                }
                std::ranges::sort(which, [this](std::size_t a, std::size_t b) {
                    return composites_before(a, b);
                });
                return animations_array(c, which);
            },
            script::attr_builtin);
    }
    sync_animation_roots();
}

} // namespace ctbrowser::shell
