// dom_bindings - the viewport's half of CSSOM View: `scrollX`/`scrollY` and
// their `page*Offset` aliases, `scroll()`/`scrollTo()`/`scrollBy()` on the
// window (§4), the `scroll` event the scroll steps queue (§13), and where the
// viewport's position actually lives - see set_viewport_scroll_hooks.

#include <ctbrowser/layout/overflow.hpp>
#include <ctbrowser/shell/bindings.hpp>

#include "internal.hpp"

#include <cmath>

namespace ctbrowser::shell {

point dom_bindings::viewport_scroll() const {
    return viewport_scroll_get_ ? viewport_scroll_get_() : viewport_scroll_;
}

void dom_bindings::scroll_viewport_to(double x, double y) {
    if (!std::isfinite(x)) { x = 0; }
    if (!std::isfinite(y)) { y = 0; }
    flush_layout();
    point wanted{static_cast<float>(x), static_cast<float>(y)};
    if (viewport_scroll_set_) {
        // The browser clamps against the document it laid out.
        const point before = viewport_scroll();
        viewport_scroll_set_(wanted);
        if (viewport_scroll() == before) { return; }
    } else {
        // A frame's document: the clamp of §4 against its own scrolling area.
        rect area{0, 0, static_cast<float>(viewport_width_), static_cast<float>(viewport_height_)};
        if (fragments_ != nullptr) {
            area = layout::viewport_scrolling_area(*fragments_, area.width, area.height);
        }
        wanted.x = std::clamp(wanted.x, 0.0f, area.width - static_cast<float>(viewport_width_));
        wanted.y = std::clamp(wanted.y, 0.0f, area.height - static_cast<float>(viewport_height_));
        if (wanted == viewport_scroll_) { return; }
        viewport_scroll_ = wanted;
    }
    queue_scroll_event(node_id{});
}

// "Run the scroll steps" (§13.1), one tick later: the pending targets are
// drained in the order they were queued, each once, the document's event
// bubbling to the window and an element's not bubbling at all. On the
// PRIMARY's timer queue, which is the one the tick drains - a frame document's
// own timers never run.
void dom_bindings::queue_scroll_event(node_id target) {
    if (std::ranges::find(pending_scroll_targets_, target) == pending_scroll_targets_.end()) {
        pending_scroll_targets_.push_back(target);
    }
    if (scroll_events_queued_ || cx_ == nullptr) { return; }
    scroll_events_queued_ = true;
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    (void)top.add_timer(native(*cx_, "scroll steps",
                               [this](context & c, std::span<value>) {
                                   scroll_events_queued_ = false;
                                   std::vector<node_id> due;
                                   due.swap(pending_scroll_targets_);
                                   for (const node_id at : due) {
                                       const value event = make_event(c, "scroll", at);
                                       auto * object =
                                           static_cast<script::object_object *>(event.as_heap());
                                       object->set("bubbles", value::boolean(!at));
                                       object->set("cancelable", value::boolean(false));
                                       (void)dispatch_event("scroll", at, event);
                                   }
                                   return value::undefined();
                               }),
                        0, false);
}

void dom_bindings::install_window_scrolling(context & cx, script::object_object & window) {
    for (const auto & [name, axis] : {std::pair{"scrollX", 'x'}, std::pair{"pageXOffset", 'x'},
                                      std::pair{"scrollY", 'y'}, std::pair{"pageYOffset", 'y'}}) {
        define_getter(cx, window, name, [this, axis](context &, std::span<value>) {
            flush_layout();
            const point at = viewport_scroll();
            return value::number(axis == 'x' ? at.x : at.y);
        });
    }
    // §4's screenX/screenY and their Left/Top aliases: where the window sits
    // on the screen, which for a headless engine is the origin.
    for (const char * name : {"screenX", "screenY", "screenLeft", "screenTop"}) {
        define_getter(cx, window, name,
                      [](context &, std::span<value>) { return value::number(0); });
    }
    // §4's scroll(): two numbers, or a ScrollToOptions whose absent members
    // keep the current position; scrollBy() adds to it. The Promise is the
    // one the Element methods return (element/views.cpp): rejected for an
    // argument the IDL refuses, resolved once the instant scroll is done.
    const auto scroll = [this](bool relative) {
        return [this, relative](context & c, std::span<value> args) {
            std::optional<double> x, y;
            const auto finite = [](double v) { return std::isfinite(v) ? v : 0.0; };
            if (args.size() >= 2) {
                x = finite(context::to_number(args[0]));
                y = finite(context::to_number(args[1]));
            } else if (!args.empty() && !args[0].is_nullish()) {
                if (!args[0].is_object_like()) {
                    return c.make_promise(
                        c.make_error("TypeError", "scroll: the argument is not a ScrollToOptions"),
                        true);
                }
                if (const value left = dict_member(c, args[0], "left"); !left.is_undefined()) {
                    x = finite(context::to_number(left));
                }
                if (const value top = dict_member(c, args[0], "top"); !top.is_undefined()) {
                    y = finite(context::to_number(top));
                }
                if (const value how = dict_member(c, args[0], "behavior"); !how.is_undefined()) {
                    const std::string behavior = c.to_string(how);
                    if (behavior != "auto" && behavior != "instant" && behavior != "smooth") {
                        return c.make_promise(
                            c.make_error("TypeError", "scroll: " + behavior + " is not a behavior"),
                            true);
                    }
                }
            }
            flush_layout();
            const point current = viewport_scroll();
            const double to_x =
                (relative ? current.x : 0.0) + x.value_or(relative ? 0.0 : current.x);
            const double to_y =
                (relative ? current.y : 0.0) + y.value_or(relative ? 0.0 : current.y);
            scroll_viewport_to(to_x, to_y);
            return scroll_settled(c);
        };
    };
    for (const auto & [name, relative] :
         {std::pair{"scroll", false}, std::pair{"scrollTo", false}, std::pair{"scrollBy", true}}) {
        set_method(cx, window, name, scroll(relative));
    }
}

} // namespace ctbrowser::shell
