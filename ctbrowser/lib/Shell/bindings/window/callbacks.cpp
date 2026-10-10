//===- callbacks.cpp - Timers, rAF and console ------------------*- C++ -*-===//
//
// The callback queue of dom_bindings: what one tick runs and in what order
// (fetches, frame and image loads, FileReader results, timers, microtasks,
// animation frames), how a callback's fault is reported, `console`, and the
// timer and requestAnimationFrame globals. WPT names in parentheses are the
// web-platform-tests files that check each behaviour.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/bindings.hpp>

#include "../events/internal.hpp" // initialised_property, for readystatechange

namespace ctbrowser::shell {

namespace {

/// The global a callback reports its exceptions at: its function's realm's
/// global ("report the exception", HTML 8.1.4.4), else the running realm's.
[[nodiscard]] value callback_global(context & cx, value callback) {
    const auto * closure = callback.is_kind(script::heap_kind::function)
                               ? static_cast<const script::closure_object *>(callback.as_heap())
                               : nullptr;
    return closure != nullptr && closure->realm != nullptr ? closure->realm->global_this
                                                           : cx.global_this();
}

} // namespace

void dom_bindings::set_alert_hook(std::function<void(const std::string &)> hook) {
    on_alert_ = std::move(hook);
}

/// Sets `document.readyState` (HTML 3.1.3): "loading" while the parser's
/// scripts run, "interactive" before DOMContentLoaded, "complete" before load.
/// Each change fires a non-bubbling `readystatechange` at the document.
void dom_bindings::set_ready_state(std::string_view state) {
    if (cx_ == nullptr) { return; }
    auto * doc = document_object();
    if (doc == nullptr) { return; }
    if (const value * held = doc->find("readyState");
        held != nullptr && held->is_string() && cx_->to_string(*held) == state) {
        return;
    }
    doc->set("readyState", cx_->string(std::string{state}));
    value event = make_event_object(*cx_, "readystatechange", false, false);
    static_cast<script::object_object *>(event.as_heap())
        ->set(std::string{detail::initialised_property}, value::boolean(true));
    (void)dispatch_event("readystatechange", node_id{}, event);
}

/// Sets `document.currentScript` (HTML 4.12.1): the <script> whose classic
/// script is running, and null between scripts and inside every callback (a
/// timer, a listener, a microtask that outlived its script). The browser sets
/// it around each parser-inserted script's run. It is a plain data property
/// because only the browser writes it.
void dom_bindings::set_current_script(node_id script) {
    if (cx_ == nullptr) { return; }
    if (auto * doc = document_object()) {
        doc->set("currentScript", script ? wrap(*cx_, script) : value::null());
    }
}

/// Runs the due timers, each as a task, and returns how many ran. With
/// `queued_tasks_only`, runs only the tasks the host queued (a notification, an
/// event, a script's load) and none of the page's setTimeout/setInterval;
/// browser::tick uses that before `DOMContentLoaded`, because those tasks were
/// queued by the parse ahead of it while the page's timers are a separate task
/// source.
std::size_t dom_bindings::run_timers(bool queued_tasks_only) {
    if (cx_ == nullptr) { return 0; }
    std::size_t ran = 0;
    // Copied before running: a callback may add or cancel timers, and
    // iterating the live list while it does is how a timer that
    // re-registers itself becomes an infinite loop inside one tick.
    std::vector<timer> due;
    const auto entries = timer_schedule_.entries();
    for (std::size_t i = 0; i < timers_.size(); ++i) {
        if (entries[i].due(now_ms_, queued_tasks_only)) { due.push_back(timers_[i]); }
    }
    for (const timer & t : due) {
        const auto index = timer_schedule_.find_active(t.id);
        if (!index) { continue; }
        const timer & still = timers_[*index];
        // A removed frame's timer is dropped, not run: its task's document is
        // no longer fully active (WPT settimeout-detached-iframe.html).
        const bool eligible = !still.window.is_heap() || find_bindings_of(still.window) != nullptr;
        if (!timer_schedule_.begin(t.id, now_ms_, eligible)) { continue; }
        // Run behind a fence and report a throw to the page (HTML 8.6 "report the
        // exception"), so `window.onerror` hears it (WPT window-runtime-error.html).
        // A string handler (initialisation step 8.2) is compiled as a classic
        // script and run the same way; a SyntaxError from compiling it is
        // reported likewise (WPT compile-error-in-setTimeout.html).
        bool threw = false;
        value thrown = value::undefined();
        if (t.callback.is_string()) {
            script::program compiled = script::compiler::compile(cx_->to_string(t.callback));
            if (!compiled.ok) {
                (void)dispatch_error("uncaught SyntaxError: " + compiled.error);
            } else {
                const script::program & held = cx_->own_program(std::move(compiled));
                for (const std::string & name : held.hoisted_vars) {
                    cx_->declare_global_var(name, false);
                }
                auto * entry = cx_->allocate<script::closure_object>(&held.functions[0]);
                entry->owner = &held;
                (void)cx_->call_fenced(value::object(entry), {}, cx_->global_this(), threw, thrown);
            }
        } else {
            (void)cx_->call_fenced(t.callback, t.arguments, cx_->global_this(), threw, thrown);
        }
        if (threw || cx_->failed()) {
            const context::rooted keep_thrown{*cx_, thrown};
            const std::string fault = cx_->failed()
                                          ? cx_->take_error()
                                          : "uncaught " + detail::describe_thrown(*cx_, thrown);
            // Reported at the callback's own global ("report the exception" in
            // the timer task): a frame's function handed to the page's
            // setTimeout reports in the frame (WPT
            // settimeout-cross-realm-callback-*).
            const bool handled =
                bindings_of(callback_global(*cx_, t.callback)).dispatch_error_value(fault, thrown);
            if (!handled && callback_error_.empty()) {
                callback_error_ = "setTimeout callback: " + fault;
            }
        }
        note_callback_fault("setTimeout");
        ++ran;
        // A timer is a task, and every task ends in a microtask checkpoint: a
        // promise the callback settled runs its reactions before the next
        // timer, not after the whole batch.
        cx_->drain_microtasks();
    }
    const auto completed = timer_schedule_.entries();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < timers_.size(); ++i) {
        if (!completed[i].cancelled) {
            if (kept != i) { timers_[kept] = std::move(timers_[i]); }
            ++kept;
        }
    }
    timers_.resize(kept);
    timer_schedule_.discard_cancelled();
    return ran;
}

std::size_t dom_bindings::run_due_callbacks() {
    if (cx_ == nullptr) { return 0; }
    time_reads_ = 0; // A new tick: the clock has moved, so the read count restarts.
    std::size_t ran = 0;
    // Fetches settle first, so a handler waiting on one runs in the same turn
    // as the timers rather than a turn behind them. Each queue is swapped out
    // before running, because a handler may start another fetch.
    //
    // Root the swapped-out entries while they run: mark_roots walks the queue,
    // and the swap empties it, so without this a collection during one callback
    // frees the reader, image or promise a later callback in the batch uses.
    const auto root_all = [this](auto & due, auto member) {
        std::vector<value> held;
        held.reserve(due.size() * 2);
        for (const auto & waiting : due) { member(waiting, held); }
        return context::rooted_values{*cx_, held};
    };
    if (!fetches_.empty()) {
        std::vector<pending_fetch> due;
        due.swap(fetches_);
        const auto keep = root_all(due, [](const pending_fetch & w, std::vector<value> & held) {
            held.push_back(w.promise);
            held.push_back(w.signal);
        });
        for (const pending_fetch & waiting : due) {
            settle_fetch(*cx_, waiting);
            note_callback_fault("fetch");
            ++ran;
        }
    }
    // Frame loads are announced the way an image load is. reconcile_frames,
    // which builds these documents, runs earlier in the tick, before the
    // window's own `load` event, because that handler is where a page reads
    // `frame.contentDocument`.
    if (!frame_loads_.empty()) {
        std::vector<pending_frame> due;
        due.swap(frame_loads_);
        for (const pending_frame & waiting : due) {
            settle_frame(*cx_, waiting);
            note_callback_fault("frame load");
            if (!waiting.resource) { ++ran; }
        }
    }
    // Image loads settle in the same turn as fetches: p5.js's loadImage awaits
    // a fetch and then an image load, so running only one per turn would take
    // two ticks per image instead of one.
    if (!image_loads_.empty()) {
        std::vector<pending_image> due;
        due.swap(image_loads_);
        const auto keep = root_all(due, [](const pending_image & w, std::vector<value> & held) {
            held.push_back(w.target);
            held.push_back(w.promise);
        });
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
        const auto keep = root_all(due, [](const pending_read & w, std::vector<value> & held) {
            held.push_back(w.reader);
            held.push_back(w.blob);
        });
        for (const pending_read & waiting : due) {
            settle_read(*cx_, waiting);
            note_callback_fault("FileReader");
            ++ran;
        }
    }
    ran += run_timers(false);
    // A microtask checkpoint after the timers and before the animation frames,
    // where a browser puts it. The order is observable: a promise resolved by a
    // timer must run its handlers before the following frame draws.
    cx_->drain_microtasks();

    // "Run the animation frame callbacks" (HTML 8.9.2): over a copy of the
    // map, running each entry only if it is still in the map, because one
    // callback may cancel the next (WPT cancel-pending.html). A callback
    // registered during the run waits for the next frame. Each runs behind the
    // fence and a throw is reported as a timer's is, so `window.onerror` hears
    // it (WPT callback-exception.html).
    std::vector<animation_frame_callback> frame_callbacks;
    frame_callbacks.swap(animation_callbacks_);
    // Document by document, "for each fully active Document in docs" (HTML
    // 8.1.7.3 "update the rendering" step 14): the page, then each frame's
    // document after the document containing it, sibling frames in tree order
    // (WPT child-document-raf-order.html). A document that is no longer fully
    // active, such as a removed frame's, runs none.
    {
        std::vector<const dom_bindings *> docs;
        const auto walk = [&docs](const auto & self, const dom_bindings & at) -> void {
            docs.push_back(&at);
            // A frame removed since the last reconcile is still listed: its
            // <iframe> is what says the document is gone.
            for (const frame_entry & entry : at.frames_) {
                if (entry.bindings != nullptr && at.is_connected(entry.element)) {
                    self(self, *entry.bindings);
                }
            }
        };
        walk(walk, primary());
        const auto rank = [&docs](const animation_frame_callback & frame) {
            return static_cast<std::size_t>(std::ranges::find(docs, frame.document) - docs.begin());
        };
        std::erase_if(frame_callbacks, [&](const animation_frame_callback & frame) {
            return rank(frame) == docs.size();
        });
        std::ranges::stable_sort(frame_callbacks, {}, rank);
    }
    const auto keep_frames = root_all(
        frame_callbacks, [](const animation_frame_callback & frame, std::vector<value> & held) {
            held.push_back(frame.callback);
        });
    for (const animation_frame_callback & frame : frame_callbacks) {
        if (frame.id == 0) { continue; }
        const bool cancelled = std::ranges::any_of(
            cancelled_frames_, [&](std::uint32_t id) { return id == frame.id; });
        if (cancelled) { continue; }
        const value ms = value::number(now_ms_);
        bool threw = false;
        value thrown = value::undefined();
        (void)cx_->call_fenced(frame.callback, std::span<const value>{&ms, 1}, cx_->global_this(),
                               threw, thrown);
        if (threw || cx_->failed()) {
            const context::rooted keep_thrown{*cx_, thrown};
            const std::string fault = cx_->failed()
                                          ? cx_->take_error()
                                          : "uncaught " + detail::describe_thrown(*cx_, thrown);
            // Reported at the callback's own window, as a timer's is: a frame's
            // callback reports to the frame (WPT
            // callback-cross-realm-report-exception).
            const bool handled = bindings_of(callback_global(*cx_, frame.callback))
                                     .dispatch_error_value(fault, thrown);
            if (!handled && callback_error_.empty()) {
                callback_error_ = "requestAnimationFrame callback: " + fault;
            }
        }
        note_callback_fault("requestAnimationFrame");
        ++ran;
        // Each callback ends in a microtask checkpoint, as a timer does:
        // "clean up after running a callback" (HTML 8.1.4.4) checkpoints once
        // the script stack is empty, so the first callback's promise reactions
        // run before the second callback (WPT promise-resolution-order.html).
        cx_->drain_microtasks();
    }
    cancelled_frames_.clear();
    cx_->drain_microtasks();
    note_callback_fault("microtask");
    // The frame is over, so copy each WebGL context's GL surface back to its
    // canvas; the compositor cannot see the GL surface. This is the only
    // per-frame copy; copying after every draw call would cost a full-surface
    // copy per draw.
    present_webgl_contexts();
    return ran;
}

/// Records an engine fault raised by a callback and clears it. `call`, unlike
/// `context::run`, does not reset the failure flag, so a fault left set would
/// make every later callback refuse to run (an rAF-driven page would freeze).
/// As in a browser, one failing callback cancels neither the rest of the queue
/// nor the next frame.
void dom_bindings::note_callback_fault(std::string_view source) {
    if (cx_ == nullptr || !cx_->failed()) { return; }
    const std::string message = std::string{source} + " callback: " + cx_->take_error();
    // Keep the first fault: a loop that faults every frame would otherwise
    // replace the original diagnosis with the thousandth copy of it.
    if (callback_error_.empty()) { callback_error_ = message; }
}

double dom_bindings::next_callback_ms() const {
    if (!animation_callbacks_.empty()) { return 0; }
    return timer_schedule_.next_delay(now_ms_);
}

std::size_t dom_bindings::pending_animation_frames() const noexcept {
    return animation_callbacks_.size();
}

const std::vector<std::string> & dom_bindings::console_output() const noexcept {
    return console_;
}

void dom_bindings::install_console(context & cx) {
    auto * console = cx.allocate<script::object_object>();
    const auto log = [this](context & c, std::span<value> args) {
        std::string line;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) { line += ' '; }
            line += c.to_string(args[i]);
        }
        console_.push_back(std::move(line));
        return value::undefined();
    };
    // Every console method a page may call must exist: a missing one throws
    // inside the page's own error reporting (p5.js's reporter calls
    // console.debug) and hides the original message. The logging methods append
    // to console_; the grouping and timing ones deliberately do nothing.
    for (const char * name : {"log", "warn", "error", "debug", "info", "trace", "dir"}) {
        console->set(name, value::object(cx.allocate<script::native_object>(name, log)));
    }
    const auto ignore = [](context &, std::span<value>) { return value::undefined(); };
    for (const char * name :
         {"group", "groupEnd", "groupCollapsed", "table", "time", "timeEnd", "assert", "count"}) {
        console->set(name, value::object(cx.allocate<script::native_object>(name, ignore)));
    }
    cx.define_builtin_global("console", value::object(console));
}

void dom_bindings::install_timers(context & cx) {
    // The IDL conversions, in order (HTML 8.6): a handler that is not a
    // Function is converted to a DOMString immediately, so its `toString` runs
    // at the call, not when the timer fires (WPT evil-spec-example.any.js); the
    // timeout is a `long`, so 2**32 wraps to 0 (WPT type-long-settimeout.any.js);
    // the remaining arguments are passed to the callback.
    const auto schedule = [this](context & c, std::span<value> args, bool repeating) {
        value handler = arg(args, 0);
        if (!handler.is_callable()) {
            handler = c.string(c.to_string(handler));
            if (c.throw_pending()) { return value::undefined(); }
        }
        const double timeout = context::to_int32(arg(args, 1));
        const std::uint32_t id = add_timer(handler, timeout, repeating, timer_source::page);
        if (args.size() > 2 && handler.is_callable()) {
            timers_.back().arguments.assign(args.begin() + 2, args.end());
        }
        // For `frame.contentWindow.setTimeout(...)` or a frame's own script:
        // the natives are shared between windows, so the owning window is
        // `this`, else the realm's global.
        const value self = c.current_this().is_heap() ? c.current_this() : c.global_this();
        if (const dom_bindings * at = find_bindings_of(self); at != nullptr && at != &primary()) {
            timers_.back().window = self;
        }
        return value::number(id);
    };
    cx.define_native("setTimeout", [schedule](context & c, std::span<value> args) {
        return schedule(c, args, false);
    });
    cx.define_native("setInterval", [schedule](context & c, std::span<value> args) {
        return schedule(c, args, true);
    });
    const auto cancel = [this](context &, std::span<value> args) {
        const auto id = static_cast<std::uint32_t>(arg_number(args, 0));
        timer_schedule_.cancel(id);
        return value::undefined();
    };
    cx.define_native("clearTimeout", cancel);
    cx.define_native("clearInterval", cancel);
    // HTML 8.6 `queueMicrotask` uses the VM's own queue, which every promise
    // reaction is on, so the two interleave as they do in a browser. A
    // non-callable argument is a TypeError by the IDL.
    //
    // A throw is reported as an `error` event at the callback's own window
    // ("invoke the callback with report") and the microtask checkpoint continues
    // (WPT queue-microtask-exceptions.any.js).
    cx.define_native("queueMicrotask", [this](context & c, std::span<value> args) {
        const value callback = arg(args, 0);
        if (!callback.is_callable()) {
            c.throw_error("TypeError", "queueMicrotask: the callback is not a function");
            return value::undefined();
        }
        const value window = callback_global(c, callback);
        auto * job = c.allocate<script::native_object>(
            "queueMicrotask job", [this, callback, window](context & j, std::span<value>) {
                bool threw = false;
                value thrown = value::undefined();
                (void)j.call_fenced(callback, {}, value::undefined(), threw, thrown);
                if (threw) {
                    const context::rooted keep{j, thrown};
                    const std::string fault = "uncaught " + detail::describe_thrown(j, thrown);
                    const bool handled = bindings_of(window).dispatch_error_value(fault, thrown);
                    if (!handled && callback_error_.empty()) {
                        callback_error_ = "queueMicrotask callback: " + fault;
                    }
                }
                return value::undefined();
            });
        // The lambda's C++ captures are invisible to the collector, so keep both
        // values alive through `retained`.
        job->retained.push_back(callback);
        job->retained.push_back(window);
        c.queue_microtask(value::object(job));
        return value::undefined();
    });
    cx.define_native("requestAnimationFrame", [this](context & c, std::span<value> args) {
        const value callback = arg(args, 0);
        if (!callback.is_callable()) {
            c.throw_error("TypeError", "requestAnimationFrame: the callback is not a function");
            return value::undefined();
        }
        // The owning window: the natives are shared, so it is `this`, else the
        // realm's global. A frame's callbacks run in the frame's turn, and a
        // window whose document is gone owns none (owner stays null). A `this`
        // that is not a window at all (`requestAnimationFrame.call(obj, cb)`, a
        // sandbox global) means the realm's window, as setTimeout does.
        const value self = c.current_this();
        const dom_bindings * owner = self.is_heap() ? find_bindings_of(self) : nullptr;
        const auto was_window = [&] {
            return std::ranges::any_of(primary().secondary_documents_, [&](const auto & made) {
                return made->window_view_.is_heap() &&
                       made->window_view_.as_heap() == self.as_heap();
            });
        };
        if (owner == nullptr && (!self.is_heap() || !was_window())) {
            owner = find_bindings_of(c.global_this());
        }
        animation_callbacks_.push_back(animation_frame_callback{++next_timer_id_, callback, owner});
        return value::number(next_timer_id_);
    });
    // `cancelAnimationFrame(handle)` removes the entry. The handle is also
    // remembered in cancelled_frames_ until the current run ends, so a copy
    // already being run skips it.
    cx.define_native("cancelAnimationFrame", [this](context &, std::span<value> args) {
        const double handle = context::to_number(arg(args, 0));
        const auto id = static_cast<std::uint32_t>(handle);
        std::erase_if(animation_callbacks_,
                      [id](const animation_frame_callback & frame) { return frame.id == id; });
        cancelled_frames_.push_back(id);
        return value::undefined();
    });
}

std::uint32_t dom_bindings::add_timer(value callback, double delay_ms, bool repeating,
                                      timer_source source) {
    const std::uint32_t id = ++next_timer_id_;
    timers_.push_back(timer{id, callback, {}, value::undefined()});
    try {
        timer_schedule_.schedule(id, now_ms_, delay_ms, repeating, source);
    } catch (...) {
        timers_.pop_back();
        throw;
    }
    return id;
}

} // namespace ctbrowser::shell
