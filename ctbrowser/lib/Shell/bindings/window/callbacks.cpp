// dom_bindings - the callback queue: what a tick runs and in what order, the
// fault it reports, `console`, and the timers.
//
// One of two files carved out of a 1,106-line bindings/window.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. Both are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; nothing is shared between them but
// the includes, which internal.hpp carries. Nothing about the public header
// changed.

#include "internal.hpp"

namespace ctbrowser::shell {

void dom_bindings::set_alert_hook(std::function<void(const std::string &)> hook) {
    on_alert_ = std::move(hook);
}

std::size_t dom_bindings::run_due_callbacks() {
    if (cx_ == nullptr) { return 0; }
    std::size_t ran = 0;
    // FETCHES FIRST, so a handler waiting on one runs in the same turn as the
    // timers rather than a turn behind them. Copied before running, because a
    // handler resolved by one may start another.
    if (!fetches_.empty()) {
        std::vector<pending_fetch> due;
        due.swap(fetches_);
        for (const pending_fetch & waiting : due) {
            settle_fetch(*cx_, waiting);
            note_callback_fault("fetch");
            ++ran;
        }
    }
    // FRAME LOADS, announced the way an image load is. `reconcile_frames` -
    // which BUILT these documents - runs earlier in the tick, before the
    // window's own `load` event, because that handler is where a page reads
    // `frame.contentDocument`.
    if (!frame_loads_.empty()) {
        std::vector<pending_frame> due;
        due.swap(frame_loads_);
        for (const pending_frame & waiting : due) {
            settle_frame(*cx_, waiting);
            note_callback_fault("frame load");
            ++ran;
        }
    }
    // IMAGE LOADS with them, and for the same reason: p5's loadImage awaits a
    // fetch and then awaits an image load, so a turn that ran one but not the
    // other would need two ticks per image instead of one.
    if (!image_loads_.empty()) {
        std::vector<pending_image> due;
        due.swap(image_loads_);
        for (const pending_image & waiting : due) {
            settle_image(*cx_, waiting);
            note_callback_fault("image load");
            ++ran;
        }
    }
    // FileReader results, beside the image loads and for the same reason.
    if (!reads_.empty()) {
        std::vector<pending_read> due;
        due.swap(reads_);
        for (const pending_read & waiting : due) {
            settle_read(*cx_, waiting);
            note_callback_fault("FileReader");
            ++ran;
        }
    }
    // Copied before running: a callback may add or cancel timers, and
    // iterating the live list while it does is how a timer that
    // re-registers itself becomes an infinite loop inside one tick.
    std::vector<timer> due;
    for (timer & t : timers_) {
        if (!t.cancelled && t.due_ms <= now_ms_) { due.push_back(t); }
    }
    for (const timer & t : due) {
        const auto still = std::ranges::find_if(
            timers_, [&](const timer & x) { return x.id == t.id && !x.cancelled; });
        if (still == timers_.end()) { continue; }
        if (still->repeating) {
            still->due_ms = now_ms_ + still->interval_ms;
        } else {
            still->cancelled = true;
        }
        (void)cx_->call(t.callback, {});
        note_callback_fault("setTimeout");
        ++ran;
    }
    std::erase_if(timers_, [](const timer & t) { return t.cancelled; });
    // AFTER THE TIMERS, BEFORE THE ANIMATION FRAMES. That is where a browser
    // puts its microtask checkpoint, and the ordering is observable: a promise
    // resolved by a timer must have run its handlers before the frame that
    // follows draws.
    cx_->drain_microtasks();

    std::vector<value> frame_callbacks;
    frame_callbacks.swap(animation_callbacks_);
    for (const value & cb : frame_callbacks) {
        const value ms = value::number(now_ms_);
        (void)cx_->call(cb, std::span<const value>{&ms, 1});
        note_callback_fault("requestAnimationFrame");
        ++ran;
    }
    cx_->drain_microtasks();
    note_callback_fault("microtask");
    // THE FRAME IS OVER, SO THE CANVAS GETS ITS PIXELS. A WebGL context draws
    // into a GL surface the compositor cannot see; this is the one copy back,
    // and here is the only place that is a FRAME rather than a draw. The old
    // engine did it after every drawArrays - 267 full-surface copies for one
    // Babylon frame - and the rewrite moved it here and then, for one commit,
    // called it from nowhere at all.
    present_webgl_contexts();
    return ran;
}

// A FAULT IN A CALLBACK IS REPORTED AND CLEARED, not left set.
//
// `context::run` clears the failure flag on entry, so a fault in a <script> is
// reported once and forgotten. `call` has no such entry point, so a fault in
// the first animation frame stayed set for the life of the page: every later
// callback was refused and the page silently stopped moving. p5.js drives its
// entire draw loop through requestAnimationFrame, so that is one faulting frame
// between a sketch that runs and a sketch that renders one frame and dies with
// no message.
//
// Clearing matches a browser, where an exception in one callback cancels
// neither the rest of the queue nor the next frame.
void dom_bindings::note_callback_fault(std::string_view source) {
    if (cx_ == nullptr || !cx_->failed()) { return; }
    const std::string message = std::string{source} + " callback: " + cx_->take_error();
    // The FIRST one is kept: a loop that faults every frame would otherwise
    // replace the original diagnosis with the thousandth copy of it.
    if (callback_error_.empty()) { callback_error_ = message; }
    ++callback_faults_;
}

double dom_bindings::next_callback_ms() const {
    if (!animation_callbacks_.empty()) { return 0; }
    double soonest = std::numeric_limits<double>::infinity();
    for (const timer & t : timers_) {
        if (t.cancelled) { continue; }
        soonest = std::min(soonest, std::max(0.0, t.due_ms - now_ms_));
    }
    return soonest;
}

std::size_t dom_bindings::pending_animation_frames() const noexcept {
    return animation_callbacks_.size();
}

const std::vector<std::string> & dom_bindings::console_output() const noexcept {
    return console_;
}

void dom_bindings::install_console(context & cx) {
    auto * console = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto log = [this](context & c, std::span<value> args) {
        std::string line;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) { line += ' '; }
            line += c.to_string(args[i]);
        }
        console_.push_back(std::move(line));
        return value::undefined();
    };
    // EVERY name a page calls, all writing to the one list. `console.debug` was
    // absent, and a missing console method is worse than a silent one: p5's own
    // error REPORTER calls it, so a library that had something to say about a
    // failed load threw a second error on top of the first and the real message
    // was never printed. A page's diagnostics must not be able to fail.
    //
    // The grouping and timing ones exist and do nothing, deliberately: a page
    // calls them for a console a test has no way to look at, and throwing is the
    // only outcome that would change what the page does.
    for (const char * name : {"log", "warn", "error", "debug", "info", "trace", "dir"}) {
        console->set(name, value::object(cx.allocate<script::native_object>(name, log)));
    }
    const auto ignore = [](context &, std::span<value>) { return value::undefined(); };
    for (const char * name :
         {"group", "groupEnd", "groupCollapsed", "table", "time", "timeEnd", "assert", "count"}) {
        console->set(name, value::object(cx.allocate<script::native_object>(name, ignore)));
    }
    cx.define_global("console", value::object(console));
}

void dom_bindings::install_timers(context & cx) {
    cx.define_native("setTimeout", [this](context &, std::span<value> args) {
        return value::number(add_timer(arg(args, 0), arg_number(args, 1), false));
    });
    cx.define_native("setInterval", [this](context &, std::span<value> args) {
        return value::number(add_timer(arg(args, 0), arg_number(args, 1), true));
    });
    const auto cancel = [this](context &, std::span<value> args) {
        const auto id = static_cast<std::uint32_t>(arg_number(args, 0));
        for (timer & t : timers_) {
            if (t.id == id) { t.cancelled = true; }
        }
        return value::undefined();
    };
    cx.define_native("clearTimeout", cancel);
    cx.define_native("clearInterval", cancel);
    cx.define_native("requestAnimationFrame", [this](context &, std::span<value> args) {
        animation_callbacks_.push_back(arg(args, 0));
        return value::number(++next_timer_id_);
    });
}

std::uint32_t dom_bindings::add_timer(value callback, double delay_ms, bool repeating) {
    const std::uint32_t id = ++next_timer_id_;
    timers_.push_back(timer{id, callback, now_ms_ + std::max(0.0, delay_ms),
                            std::max(0.0, delay_ms), repeating, false});
    return id;
}

} // namespace ctbrowser::shell
