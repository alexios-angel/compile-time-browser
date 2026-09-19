#pragma once
#include "internal.hpp"

namespace ctbrowser::shell::detail {
// THE WINDOW-REFLECTING BODY ELEMENT EVENT HANDLER SET, HTML 8.1.8.2: six
// names that on a `<body>` or `<frameset>` ARE the Window's handler - `<body
// onload="init()">` and `document.body.onresize = f` both address the window.
[[nodiscard]] inline bool forwards_to_window(std::string_view name) {
    for (const std::string_view each :
         {"onblur", "onerror", "onfocus", "onload", "onresize", "onscroll"}) {
        if (name == each) { return true; }
    }
    return false;
}

} // namespace ctbrowser::shell::detail
