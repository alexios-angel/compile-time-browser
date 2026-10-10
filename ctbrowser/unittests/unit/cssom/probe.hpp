//===- probe.hpp - Console-line test helper ---------------------*- C++ -*-===//
//
// Shared helper for the CSSOM tests: find a logged console line by its prefix.
// Private to unittests/unit/.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/shell/shell.hpp>

#include <string>
#include <string_view>

namespace ctbrowser_test {

using ctbrowser::shell::browser;

/// Returns the first console line of \p page that starts with \p prefix, or a
/// "<no line beginning ...>" marker. Looking lines up by prefix means a case
/// that adds a `console.log` in the middle does not renumber every assertion
/// after it.
[[nodiscard]] inline std::string logged(browser & page, std::string_view prefix) {
    for (const std::string & line : page.bindings().console_output()) {
        if (line.starts_with(prefix)) { return line; }
    }
    return std::string{"<no line beginning "} + std::string{prefix} + ">";
}

} // namespace ctbrowser_test
