//===- activation.cpp - User activation -------------------------*- C++ -*-===//
//
// User activation (HTML 6.4.3) and `navigator.userActivation`.
//
// A window's last activation timestamp is +inf until a trusted
// activation-triggering input event (a mousedown, or a keydown other than
// Escape) reaches its document; events/input.cpp calls notify_activation()
// before dispatching one. Activation is transient for a few seconds after that
// and sticky for good. Consuming it (showPicker, and anything else an
// activation gates) sets every window in the tree that had one to -inf, which
// ends the transient part and keeps the sticky one.
//
// ponytail: notification reaches this window, its frames and the top-level
// one, not a nested frame's intermediate ancestors; and the frame tree is
// taken as one origin, which is all this engine loads.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/bindings.hpp>

#include "internal.hpp"

#include <limits>

namespace ctbrowser::shell {

namespace {

/// The transient activation duration. HTML leaves it to the user agent ("at
/// most a few seconds"); 5000 ms is Chromium's value.
constexpr double transient_activation_ms = 5000;
/// The timestamp of a window that has never been activated.
constexpr double never = std::numeric_limits<double>::infinity();

} // namespace

void dom_bindings::each_window_in_tree(const std::function<void(dom_bindings &)> & visit) {
    visit(*this);
    for (const frame_entry & frame : frames_) {
        if (frame.bindings != nullptr) { frame.bindings->each_window_in_tree(visit); }
    }
}

void dom_bindings::notify_activation() {
    const double now = primary().now_ms_;
    each_window_in_tree([now](dom_bindings & window) { window.last_activation_ = now; });
    primary().last_activation_ = now;
}

bool dom_bindings::has_transient_activation() const {
    const double now = primary().now_ms_;
    return now >= last_activation_ && now < last_activation_ + transient_activation_ms;
}

bool dom_bindings::has_sticky_activation() const {
    return last_activation_ != never;
}

void dom_bindings::consume_activation() {
    primary().each_window_in_tree([](dom_bindings & window) {
        if (window.last_activation_ != never) { window.last_activation_ = -never; }
    });
}

// `navigator.userActivation` ([SameObject]) and its interface: two readonly
// booleans read off this window when asked, not when made.
void dom_bindings::install_user_activation(context & cx, script::object_object & navigator) {
    const interface_pair made =
        make_interface(cx, "UserActivation", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "Illegal constructor: UserActivation");
            return value::undefined();
        });
    define_getter(cx, *made.proto, "hasBeenActive", [this](context &, std::span<value>) {
        return value::boolean(has_sticky_activation());
    });
    define_getter(cx, *made.proto, "isActive", [this](context &, std::span<value>) {
        return value::boolean(has_transient_activation());
    });
    auto * activation = cx.allocate<script::object_object>();
    activation->prototype = value::object(made.proto);
    navigator.set("userActivation", value::object(activation));
}

} // namespace ctbrowser::shell
