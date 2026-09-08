#pragma once
// Private to lib/Shell/bindings/events/. NOT installed and in no file set:
// include/ctbrowser/shell/bindings.hpp declares dom_bindings whole, and this
// exists only so its event half can be more than one file - it was 1,647
// lines in one until 2026-09-08. The includes are events.cpp's, so every
// file here sees exactly what that one saw.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>
#include <numbers>
#include <vector>

namespace ctbrowser::shell::detail {

// The propagation flags, kept on the event where the page can see them through
// `cancelBubble`. Reading them back off the object rather than out of a local is
// what makes `evt.stopPropagation()` BEFORE dispatch mean something - which is
// exactly what dom/events/Event-dispatch-propagation-stopped.html does.
inline constexpr std::string_view stop_immediate_property = "__stopImmediate";
inline constexpr std::string_view cancel_bubble_property = "__cancelBubble";
// THE DISPATCH FLAG AND THE INITIALIZED FLAG, which are two different questions
// that `dispatchEvent` asks in the same breath and answers with the same
// exception. An event that is ALREADY travelling may not be dispatched again,
// and an event that has never been given a type may not be dispatched at all -
// `document.createEvent` hands back exactly that kind and `initEvent` is what
// clears it. Both are InvalidStateError, and both were silent successes here:
// dom/events/EventTarget-dispatchEvent.html spends 21 of its 25 assertions on
// the pair.
inline constexpr std::string_view dispatch_property = "__dispatching";
inline constexpr std::string_view initialised_property = "__initialised";
// Where `isTrusted` reads from. It is an accessor rather than a data property
// because the IDL says so and because a page can see the difference - see
// install_event_interfaces.
inline constexpr std::string_view trusted_property = "__isTrusted";
// THE IN-PASSIVE-LISTENER FLAG, which rides on the EVENT because the event is
// the only thing `preventDefault` is handed. `{passive: true}` is not a hint
// the engine may take or leave: the DOM says the canceled flag is not set while
// a passive listener is running, so a page that promises not to cancel and then
// tries is refused rather than believed.
inline constexpr std::string_view passive_property = "__passive";
// The propagation path, as a list, for `composedPath()`.
inline constexpr std::string_view path_property = "__path";

[[nodiscard]] inline bool flag_of(context & cx, value event, std::string_view name) {
    return context::truthy(cx.lookup_property(event, std::string{name}));
}

// --- helpers shared by more than one file of bindings/events/ ----------------
//
// Both were in an anonymous namespace of events.cpp. They gained external
// linkage when that file was split, and nothing else: the bodies are in
// dispatch.cpp beside make_event_object, and interfaces.cpp - where every
// constructor initialises its receiver - calls them through this.
[[nodiscard]] value is_trusted_getter_of(value event_prototype);
void initialise_event(context & cx, script::object_object & event, std::string_view type,
                      bool bubbles, bool cancelable, double timestamp, value is_trusted_getter);

} // namespace ctbrowser::shell::detail
