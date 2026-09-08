#pragma once
// The page, asked what it logged, and a file handed over as bytes - the helpers
// more than one of the eight files carved out of bindings_basics.cpp on
// 2026-09-07 needs. Verbatim from that file but for `inline` and the namespace;
// `find_id`, which it also had, is the copy in test/support/dom_probe.hpp now.
// Private to unittests/unit/, like the tests themselves.

#include <ctbrowser/shell/shell.hpp>

#include "check.hpp"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser_test {

using ctbrowser::shell::browser;

inline void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

// A file as bytes, for the asset registry. p5.js is 4.4 MB on disk and the
// registry is what a `<script src>` resolves against, so the test loads it the
// same way a page would rather than inlining it.
[[nodiscard]] inline std::vector<std::byte> read_bytes(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return std::vector<std::byte>{reinterpret_cast<const std::byte *>(text.data()),
                                  reinterpret_cast<const std::byte *>(text.data() + text.size())};
}

[[nodiscard]] inline const std::vector<std::string> & log_of(browser & page) {
    return page.bindings().console_output();
}

} // namespace ctbrowser_test
