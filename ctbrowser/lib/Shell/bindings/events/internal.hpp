//===- events/internal.hpp - Private event binding state --------*- C++ -*-===//
//
// Private to lib/Shell/bindings/events/ (not installed): the hidden property
// names that hold an event's internal flags, and helpers shared by more than
// one file of the directory.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>
#include <numbers>
#include <vector>

namespace ctbrowser::shell::detail {

/// The propagation flags, kept on the event where the page can see them through
/// `cancelBubble`. Reading them back off the object rather than out of a local
/// is what makes `evt.stopPropagation()` before dispatch take effect, as
/// dom/events/Event-dispatch-propagation-stopped.html checks.
inline constexpr std::string_view stop_immediate_property = "__stopImmediate";
inline constexpr std::string_view cancel_bubble_property = "__cancelBubble";
/// The DOM's dispatch flag and initialized flag. `dispatchEvent` checks both and
/// throws InvalidStateError for either: an event already being dispatched may
/// not be dispatched again, and one never given a type may not be dispatched
/// at all (`document.createEvent` returns such an event; `initEvent` sets the
/// type).
inline constexpr std::string_view dispatch_property = "__dispatching";
inline constexpr std::string_view initialised_property = "__initialised";
/// Where `isTrusted` reads from. It is an accessor rather than a data property
/// because the IDL says so and because a page can see the difference; see
/// install_event_interfaces.
inline constexpr std::string_view trusted_property = "__isTrusted";
/// Where `timeStamp` reads from. It is an accessor on Event.prototype, as the
/// IDL says, so `Object.getOwnPropertyDescriptor(Event.prototype,
/// "timeStamp").get` exists (Event-timestamp-cross-realm-getter.html calls it).
inline constexpr std::string_view timestamp_property = "__timeStamp";
/// The DOM's in-passive-listener flag. It is stored on the event because the
/// event is all `preventDefault` receives. `{passive: true}` is binding, not a
/// hint: the DOM does not set the canceled flag while a passive listener runs,
/// so a passive listener that calls preventDefault() has no effect.
inline constexpr std::string_view passive_property = "__passive";
/// The propagation path, as a list, for `composedPath()`.
inline constexpr std::string_view path_property = "__path";
/// DOM 2.9's "legacy target override flag": an event dispatched at the window
/// whose `target` reads as the Document, used for the window's `load`
/// (HTML 13.2.7).
inline constexpr std::string_view legacy_target_override_property = "__legacyTargetOverride";

/// Reads the boolean flag stored under the hidden property `name` of `event`.
[[nodiscard]] inline bool flag_of(context & cx, value event, std::string_view name) {
    return context::truthy(cx.lookup_property(event, std::string{name}));
}

// Helpers shared by more than one file of bindings/events/.

/// Formats a thrown value as `Name: message`, reading the two properties rather
/// than calling `toString`, because a reporter that can itself throw is worse
/// than an unreported fault. Defined in dispatch.cpp.
[[nodiscard]] std::string describe_thrown(context & cx, value thrown);
/// Returns the `isTrusted` getter installed on `event_prototype`. Defined in
/// dispatch.cpp beside make_event_object.
[[nodiscard]] value is_trusted_getter_of(value event_prototype);
/// Writes every Event field onto `event` (type, target, phase, bubbles,
/// cancelable, timestamp, the hidden flags above, and the `isTrusted`
/// accessor). It writes onto the receiver so subclassing works. Defined in
/// dispatch.cpp.
void initialise_event(context & cx, script::object_object & event, std::string_view type,
                      bool bubbles, bool cancelable, double timestamp, value is_trusted_getter);

} // namespace ctbrowser::shell::detail
