// dom_bindings - the callback queue: what a tick runs and in what order, the
// fault it reports, `console`, and the timers.

#include <ctbrowser/shell/bindings.hpp>

#include "../events/internal.hpp" // initialised_property, for readystatechange

namespace ctbrowser::shell {

namespace {

// The global a callback reports its exceptions at: its function's realm's
// ("report the exception", HTML 8.1.4.4), else the running realm's.
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

// `document.readyState`, HTML 3.1.3: "loading" while the parser's scripts
// run, "interactive" before DOMContentLoaded, "complete" before load - and
// `readystatechange` at the document for each change, not bubbling.
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

// `document.currentScript`, HTML 4.12.1: the <script> whose classic script
// is running, null between scripts and inside every callback - a timer, a
// listener, a microtask that outlived its script. The browser sets it around
// each parser-inserted script's run; a data property because nothing but the
// browser writes it and a page reads it.
void dom_bindings::set_current_script(node_id script) {
    if (cx_ == nullptr) { return; }
    if (auto * doc = document_object()) {
        doc->set("currentScript", script ? wrap(*cx_, script) : value::null());
    }
}

// THE DUE TIMERS, each a task: every one, or - `queued_tasks_only` - only the
// tasks the host queued (a notification, an event, a script's load) and none
// of the page's setTimeout/setInterval. The second is the browser's before
// `DOMContentLoaded`: those are tasks the parse queued ahead of it, where the
// page's timers are a task source of their own (browser::tick).
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
        // A REMOVED FRAME'S TIMER is dropped, not run: its task's document is
        // no longer fully active (settimeout-detached-iframe.html).
        const bool eligible = !still.window.is_heap() || find_bindings_of(still.window) != nullptr;
        if (!timer_schedule_.begin(t.id, now_ms_, eligible)) { continue; }
        // BEHIND A FENCE, and REPORTED TO THE PAGE: HTML 8.6's timer task
        // "reports the exception" - `window.onerror` hears a throw in a
        // setTimeout callback (window-runtime-error.html), where before it
        // was an engine fault the embedder alone saw. A STRING handler
        // (initialisation steps 8.2) is compiled as a classic script and run
        // the same way; one that does not parse is a SyntaxError reported
        // likewise (compile-error-in-setTimeout.html).
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
            // At the CALLBACK's global ("report the exception" in the timer
            // task): a frame's function handed to the page's setTimeout
            // reports in the frame (settimeout-cross-realm-callback-...).
            const bool handled =
                bindings_of(callback_global(*cx_, t.callback)).dispatch_error_value(fault, thrown);
            if (!handled && callback_error_.empty()) {
                callback_error_ = "setTimeout callback: " + fault;
            }
        }
        note_callback_fault("setTimeout");
        ++ran;
        // A TIMER IS A TASK, and every task ends in a microtask checkpoint:
        // a promise the callback settled runs its reactions before the NEXT
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
    time_reads_ = 0; // a new tick: the clock has moved, the observations start over
    std::size_t ran = 0;
    // FETCHES FIRST, so a handler waiting on one runs in the same turn as the
    // timers rather than a turn behind them. Copied before running, because a
    // handler resolved by one may start another.
    //
    // AND ROOTED WHILE THEY RUN. The queue is what mark_roots walks, and the
    // swap below empties it - so from the first callback of the batch the
    // rest were reachable from a local vector and nothing else, and a
    // collection during that callback freed the reader, image or promise the
    // next one was about to touch. image_basics' three FileReaders crashed in
    // exactly that order once the heap was one allocation busier.
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
            if (!waiting.resource) { ++ran; }
        }
    }
    // IMAGE LOADS with them, and for the same reason: p5's loadImage awaits a
    // fetch and then awaits an image load, so a turn that ran one but not the
    // other would need two ticks per image instead of one.
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
    // AFTER THE TIMERS, BEFORE THE ANIMATION FRAMES. That is where a browser
    // puts its microtask checkpoint, and the ordering is observable: a promise
    // resolved by a timer must have run its handlers before the frame that
    // follows draws.
    cx_->drain_microtasks();

    // "Run the animation frame callbacks" (HTML 8.9.2): over a copy of the
    // map, each entry only if it is STILL in the map - one callback may
    // cancel the next (cancel-pending.html) - and a callback registered
    // during the run waits for the next frame. Behind the fence and
    // reported, as a timer's throw is: `window.onerror` hears it
    // (callback-exception.html).
    std::vector<animation_frame_callback> frame_callbacks;
    frame_callbacks.swap(animation_callbacks_);
    // DOCUMENT BY DOCUMENT, "for each fully active Document in docs" (HTML
    // 8.1.7.3 "update the rendering" step 14): the page, then each frame's
    // document after the document it is in, sibling frames in tree order
    // (child-document-raf-order.html) - and a document that is no longer
    // fully active, a removed frame's, runs none.
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
            // At the CALLBACK's window, as a timer's is: a frame's callback
            // reports to the frame (callback-cross-realm-report-exception).
            const bool handled = bindings_of(callback_global(*cx_, frame.callback))
                                     .dispatch_error_value(fault, thrown);
            if (!handled && callback_error_.empty()) {
                callback_error_ = "requestAnimationFrame callback: " + fault;
            }
        }
        note_callback_fault("requestAnimationFrame");
        ++ran;
        // EACH CALLBACK ENDS IN A MICROTASK CHECKPOINT, as a timer does:
        // "clean up after running a callback" (HTML 8.1.4.4) checkpoints once
        // the script stack is empty, so the first callback's promise
        // reactions run before the second callback
        // (promise-resolution-order.html).
        cx_->drain_microtasks();
    }
    cancelled_frames_.clear();
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
    cx.define_builtin_global("console", value::object(console));
}

void dom_bindings::install_timers(context & cx) {
    // The IDL conversions, in order (HTML 8.6): a handler that is not a
    // Function is a DOMString NOW - its `toString` runs at the call, not when
    // the timer fires (evil-spec-example.any.js) - the timeout is a `long`, so
    // 2**32 wraps to 0 (type-long-settimeout.any.js), and the rest are the
    // callback's arguments.
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
        // `frame.contentWindow.setTimeout(...)`, or a frame's own script's -
        // the natives are shared, so the window is `this`, else the realm's.
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
    // HTML 8.6 `queueMicrotask`: the VM's own queue, which every promise
    // reaction is already on, so the ordering between the two is the one a
    // page observes in a browser. A non-callable is a TypeError by the IDL.
    //
    // A THROW IS REPORTED, and the checkpoint goes on: the job "invokes the
    // callback with report", so an exception is an `error` event at the
    // callback's own window (queue-microtask-exceptions.any.js). Queued bare,
    // it was an uncaught throw that stopped the drain.
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
        // In a C++ capture, which the collector cannot see.
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
        // WHOSE: the natives are shared, so the window is `this`, else the
        // realm's - a frame's callbacks run in the frame's turn, and none is
        // there for a window whose document is gone (null). A `this` that is
        // no window at all (`requestAnimationFrame.call(obj, cb)`, a sandbox
        // global) is the realm's, as setTimeout takes it.
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
    // `cancelAnimationFrame(handle)`: the entry leaves the map. A handle
    // cancelled while the callbacks are running is remembered until the run
    // ends, which is how the copy being run knows not to call it.
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
