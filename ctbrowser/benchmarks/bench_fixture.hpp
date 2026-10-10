//===- bench_fixture.hpp - Shared benchmark helpers ----------- -*- C++ -*-===//
//
// What bench_layout and bench_raster share: the same generated document and the
// same clock. The document has a realistic shape (nested containers with text,
// not a flat list) because depth and text are what layout spends its time on.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <string>

namespace bench {

/// Runs `f` `reps` times and returns the mean wall time per run in milliseconds.
/// Runs `f` `reps` times and returns the mean wall time per run in milliseconds.
template <typename F> [[nodiscard]] double time_ms(int reps, F && f) {
    using clock_type = std::chrono::steady_clock;
    const auto start = clock_type::now();
    for (int i = 0; i < reps; ++i) { f(); }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / reps;
}

/// Builds a page of `sections` sections, each an <h2> and a <ul> of `rows`
/// list items long enough to wrap.
/// Builds a page of `sections` sections, each an <h2> and a <ul> of `rows`
/// list items long enough to wrap.
[[nodiscard]] inline std::string build_html(int sections, int rows) {
    std::string out = "<body><div id=root>";
    for (int s = 0; s < sections; ++s) {
        out += "<section><h2>Section heading number " + std::to_string(s) + "</h2><ul>";
        for (int r = 0; r < rows; ++r) {
            out += "<li>Row " + std::to_string(r) +
                   " with enough words in it that the line breaker has real work to do</li>";
        }
        out += "</ul></section>";
    }
    out += "</div></body>";
    return out;
}

} // namespace bench
