#pragma once
// A logged line by its prefix - the one helper both files carved out of
// cssom.cpp on 2026-09-07 need. Verbatim from that file but for `inline` and the
// namespace. Private to unittests/unit/, like the tests themselves.

#include <ctbrowser/shell/shell.hpp>

#include <string>
#include <string_view>

namespace ctbrowser_test {

using ctbrowser::shell::browser;

// A logged line by its prefix, so a case that adds a `console.log` in the
// middle does not renumber every assertion after it.
[[nodiscard]] inline std::string logged(browser & page, std::string_view prefix) {
    for (const std::string & line : page.bindings().console_output()) {
        if (line.starts_with(prefix)) { return line; }
    }
    return std::string{"<no line beginning "} + std::string{prefix} + ">";
}

} // namespace ctbrowser_test
