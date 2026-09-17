// dom_bindings - AbortController and AbortSignal, DOM §3.2
// (https://dom.spec.whatwg.org/#aborting-ongoing-activities).
//
// A signal is an EventTarget with three pieces of state - aborted, its reason,
// and the signals that depend on it - kept in private slots so the accessors
// on the prototype are the IDL's. Aborting runs the abort algorithms (here:
// every listener registered with the signal goes, which is what removeEvent-
// Listener-by-signal means), fires `abort` at the signal, then does the same
// for each dependent signal made by `AbortSignal.any`. `AbortSignal.timeout`
// is one of the page's own timers aimed at the signal; the reason it carries
// is the TimeoutError the spec names.
//
// The controller exists before this file did - p5 makes one per sketch to take
// every listener down at once - but its signal was a plain object with two
// data properties and nothing was an event target, so `signal.addEventListener
// ("abort", ...)`, `onabort`, `throwIfAborted`, the statics and the default
// AbortError reason were all missing (dom/abort, six files).

#include <ctbrowser/shell/bindings.hpp>

#include "events/internal.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace ctbrowser::shell {

using namespace detail;

namespace {

const std::string aborted_slot = std::string{script::private_key_prefix} + "abort:aborted";
const std::string reason_slot = std::string{script::private_key_prefix} + "abort:reason";
// The signals that follow this one (an `any` signal is appended to each of
// its sources), and, on an `any` signal, the sources it follows - which is
// what keeps a chain of `any` calls flat, as the spec's "source signals" does.
const std::string dependents_slot = std::string{script::private_key_prefix} + "abort:dependents";
const std::string sources_slot = std::string{script::private_key_prefix} + "abort:sources";
const std::string signal_slot = std::string{script::private_key_prefix} + "abort:signal";

[[nodiscard]] script::object_object * object_of(value v) {
    return v.is_object() ? static_cast<script::object_object *>(v.as_heap()) : nullptr;
}

[[nodiscard]] bool is_aborted(value signal) {
    auto * object = object_of(signal);
    if (object == nullptr) { return false; }
    const value * flag = object->find(aborted_slot);
    return flag != nullptr && context::truthy(*flag);
}

[[nodiscard]] std::vector<value> list_of(value signal, const std::string & slot) {
    std::vector<value> out;
    auto * object = object_of(signal);
    if (object == nullptr) { return out; }
    if (const value * held = object->find(slot); held != nullptr && held->is_array()) {
        for (const value & each : static_cast<script::array_object *>(held->as_heap())->items) {
            out.push_back(each);
        }
    }
    return out;
}

void append_to(context & cx, value signal, const std::string & slot, value item) {
    auto * object = object_of(signal);
    if (object == nullptr) { return; }
    value * held = object->find(slot);
    if (held == nullptr || !held->is_array()) {
        object->set(slot, cx.make_array());
        held = object->find(slot);
    }
    static_cast<script::array_object *>(held->as_heap())->items.push_back(item);
}

} // namespace

bool dom_bindings::is_abort_signal(value v) const {
    if (!v.is_object() || !abort_signal_prototype_.is_object()) { return false; }
    value link = static_cast<script::object_object *>(v.as_heap())->prototype;
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (link.as_heap() == abort_signal_prototype_.as_heap()) { return true; }
        link = static_cast<script::object_object *>(link.as_heap())->prototype;
    }
    return false;
}

value dom_bindings::make_abort_signal(context & cx) {
    auto * signal = cx.allocate<script::object_object>();
    signal->prototype = abort_signal_prototype_;
    signal->set(aborted_slot, value::boolean(false));
    signal->set(reason_slot, value::undefined());
    return value::object(signal);
}

// DOM §3.2.1 "signal abort": the reason, the dependents that will follow, the
// abort steps (algorithms, then the event) for this signal and then for each
// of them. A signal that is already aborted does nothing.
void dom_bindings::signal_abort(context & cx, value signal, value reason) {
    auto * object = object_of(signal);
    if (object == nullptr || is_aborted(signal)) { return; }
    if (reason.is_undefined()) {
        reason = make_dom_exception(cx, "AbortError", "signal is aborted without reason");
    }
    object->set(aborted_slot, value::boolean(true));
    object->set(reason_slot, reason);
    std::vector<value> to_abort;
    for (const value & dependent : list_of(signal, dependents_slot)) {
        if (auto * each = object_of(dependent); each != nullptr && !is_aborted(dependent)) {
            each->set(aborted_slot, value::boolean(true));
            each->set(reason_slot, reason);
            to_abort.push_back(dependent);
        }
    }
    const auto run_abort_steps = [&](value at) {
        // The abort algorithms: every listener added with `{signal}` is
        // removed - on this document and on any it made, since a frame's
        // element can be listened to with the page's signal.
        const auto remove_from = [&](auto && self, dom_bindings & owner) -> void {
            std::erase_if(owner.listeners_, [&](const listener & l) {
                return l.abort_signal.is_heap() && l.abort_signal.as_heap() == at.as_heap();
            });
            for (const auto & made : owner.secondary_documents_) { self(self, *made); }
        };
        remove_from(remove_from, primary_ == nullptr ? *this : *primary_);
        // Then `abort`, which neither bubbles nor cancels.
        value event = make_event_object(cx, "abort", false, false);
        if (auto * carrier = object_of(event)) {
            carrier->set(std::string{trusted_property}, value::boolean(true));
        }
        (void)dispatch_to(event, path_step{node_id{}, listen_on::object, at});
    };
    run_abort_steps(signal);
    for (const value & dependent : to_abort) { run_abort_steps(dependent); }
}

void dom_bindings::install_abort(context & cx) {
    // --- AbortSignal --------------------------------------------------------
    auto * signal_proto = cx.allocate<script::object_object>();
    signal_proto->prototype = event_target_prototype_;
    abort_signal_prototype_ = value::object(signal_proto);
    define_getter(cx, *signal_proto, "aborted", [](context & c, std::span<value>) {
        return value::boolean(is_aborted(c.current_this()));
    });
    define_getter(cx, *signal_proto, "reason", [](context & c, std::span<value>) {
        auto * self = object_of(c.current_this());
        const value * held = self == nullptr ? nullptr : self->find(reason_slot);
        return held == nullptr ? value::undefined() : *held;
    });
    // The `onabort` event handler IDL attribute (HTML 8.1.8.1): a data slot the
    // dispatch reads by name, and a setter that keeps only callables and
    // objects, which is the attribute's coercion.
    define_getter(
        cx, *signal_proto, "onabort",
        [](context & c, std::span<value>) {
            auto * self = object_of(c.current_this());
            const value * held = self == nullptr ? nullptr : self->find("onabort");
            return held == nullptr ? value::null() : *held;
        },
        [](context & c, std::span<value> a) {
            if (auto * self = object_of(c.current_this())) {
                const value given = arg(a, 0);
                self->set("onabort", given.is_object_like() ? given : value::null());
            }
            return value::undefined();
        });
    set_method(
        cx, *signal_proto, "throwIfAborted",
        [](context & c, std::span<value>) {
            const value self = c.current_this();
            if (is_aborted(self)) { c.throw_value(c.lookup_property(self, "reason")); }
            return value::undefined();
        },
        script::attr_builtin);

    auto * signal_ctor =
        cx.allocate<script::native_object>("AbortSignal", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        });
    signal_ctor->define("prototype", abort_signal_prototype_, script::attr_none);
    signal_proto->define("constructor", value::object(signal_ctor), script::attr_builtin);
    // `AbortSignal.abort(reason)`: an already-aborted signal; no event fires,
    // there is nobody listening yet.
    set_method(
        cx, *signal_ctor, "abort",
        [this](context & c, std::span<value> a) {
            const value signal = make_abort_signal(c);
            value reason = arg(a, 0);
            if (reason.is_undefined()) {
                reason = make_dom_exception(c, "AbortError", "signal is aborted without reason");
            }
            auto * object = object_of(signal);
            object->set(aborted_slot, value::boolean(true));
            object->set(reason_slot, reason);
            return signal;
        },
        script::attr_builtin);
    // `AbortSignal.timeout(ms)`: aborted with a TimeoutError once the page's
    // timers reach `ms`. `[EnforceRange] unsigned long long`, so a negative,
    // NaN or non-finite delay is a TypeError rather than "now".
    set_method(
        cx, *signal_ctor, "timeout",
        [this](context & c, std::span<value> a) {
            const double ms = context::to_number(arg(a, 0));
            if (std::isnan(ms) || !std::isfinite(ms) || ms < 0 || ms > 9007199254740991.0) {
                c.throw_error("TypeError", "AbortSignal.timeout: the delay is out of range");
                return value::undefined();
            }
            const value signal = make_abort_signal(c);
            auto * fire = c.allocate<script::native_object>(
                "timeout", [this, signal](context & inner, std::span<value>) {
                    signal_abort(inner, signal,
                                 make_dom_exception(inner, "TimeoutError", "signal timed out"));
                    return value::undefined();
                });
            // THE SIGNAL IS IN A C++ CAPTURE, which the collector cannot see;
            // the timer holds the callback, the callback holds the signal.
            fire->retained.push_back(signal);
            (void)add_timer(value::object(fire), ms, false);
            return signal;
        },
        script::attr_builtin);
    // `AbortSignal.any(signals)`: a dependent signal, §3.2.1 "create a
    // dependent abort signal". Already aborted if any source is; otherwise it
    // follows each source - or each of THEIR sources, so the chain stays one
    // level deep and a source aborting reaches it directly.
    set_method(
        cx, *signal_ctor, "any",
        [this](context & c, std::span<value> a) {
            std::vector<value> sources;
            const value iterator = c.get_iterator(arg(a, 0));
            if (c.throw_pending()) { return value::undefined(); }
            const value next = c.lookup_property(iterator, "next");
            for (;;) {
                bool done = false;
                const value each = c.iterator_step(iterator, next, done);
                if (c.throw_pending()) { return value::undefined(); }
                if (done) { break; }
                if (!is_abort_signal(each)) {
                    c.throw_error("TypeError", "AbortSignal.any: the argument is not an "
                                               "iterable of AbortSignal");
                    return value::undefined();
                }
                sources.push_back(each);
            }
            const value signal = make_abort_signal(c);
            for (const value & source : sources) {
                if (is_aborted(source)) {
                    auto * object = object_of(signal);
                    object->set(aborted_slot, value::boolean(true));
                    object->set(reason_slot, c.lookup_property(source, "reason"));
                    return signal;
                }
            }
            for (const value & source : sources) {
                std::vector<value> roots = list_of(source, sources_slot);
                if (roots.empty()) { roots.push_back(source); }
                for (const value & root : roots) {
                    append_to(c, signal, sources_slot, root);
                    append_to(c, root, dependents_slot, signal);
                }
            }
            return signal;
        },
        script::attr_builtin);
    cx.define_global("AbortSignal", value::object(signal_ctor));

    // --- AbortController ----------------------------------------------------
    auto * controller_proto = cx.allocate<script::object_object>();
    define_getter(cx, *controller_proto, "signal", [](context & c, std::span<value>) {
        auto * self = object_of(c.current_this());
        const value * held = self == nullptr ? nullptr : self->find(signal_slot);
        return held == nullptr ? value::undefined() : *held;
    });
    set_method(
        cx, *controller_proto, "abort",
        [this](context & c, std::span<value> a) {
            auto * self = object_of(c.current_this());
            const value * held = self == nullptr ? nullptr : self->find(signal_slot);
            if (held != nullptr) { signal_abort(c, *held, arg(a, 0)); }
            return value::undefined();
        },
        script::attr_builtin);
    const value controller_prototype = value::object(controller_proto);
    auto * controller_ctor = cx.allocate<script::native_object>(
        "AbortController", [this, controller_prototype](context & c, std::span<value>) {
            // The Error constructor's shape: `this` is the instance under
            // `new` and under a subclass's `super()`.
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<script::object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = controller_prototype; }
            made->set(signal_slot, make_abort_signal(c));
            return self;
        });
    controller_ctor->retained.push_back(controller_prototype);
    controller_ctor->define("prototype", controller_prototype, script::attr_none);
    controller_proto->define("constructor", value::object(controller_ctor), script::attr_builtin);
    cx.define_global("AbortController", value::object(controller_ctor));
}

} // namespace ctbrowser::shell
