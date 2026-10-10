//===- events/handler_names.hpp - Handler attribute names -------*- C++ -*-===//
//
// The HTML handler-attribute name lists that more than one file of
// bindings/events/ needs: the WindowEventHandlers mixin and the set of
// `<body>`/`<frameset>` handlers that address the window.
//
//===----------------------------------------------------------------------===//
#pragma once
#include "internal.hpp"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace ctbrowser::shell::detail {
/// WindowEventHandlers (HTML 8.1.7.2): the Window's own handlers, a mixin that
/// HTMLBodyElement and HTMLFrameSetElement include as well.
inline constexpr std::string_view window_event_handlers[] = {
    "onafterprint", "onbeforeprint",        "onbeforeunload", "onhashchange", "onlanguagechange",
    "onmessage",    "onmessageerror",       "onoffline",      "ononline",     "onpagehide",
    "onpagereveal", "onpageshow",           "onpageswap",     "onpopstate",   "onrejectionhandled",
    "onstorage",    "onunhandledrejection", "onunload"};

/// True for a handler that a `<body>` or `<frameset>` forwards to the window
/// (HTML 8.1.8.2): the window-reflecting body element event handler set (six
/// GlobalEventHandlers names) plus every WindowEventHandlers name. On those two
/// elements each of them is the Window's handler: `<body onload="init()">`,
/// `document.body.onresize = f` and `body.onmessage` all address the window.
[[nodiscard]] inline bool forwards_to_window(std::string_view name) {
    for (const std::string_view each :
         {"onblur", "onerror", "onfocus", "onload", "onresize", "onscroll"}) {
        if (name == each) { return true; }
    }
    return std::ranges::find(window_event_handlers, name) != std::end(window_event_handlers);
}

} // namespace ctbrowser::shell::detail
