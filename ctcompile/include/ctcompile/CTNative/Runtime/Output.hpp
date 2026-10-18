//===- Output.hpp - The native program's output lines -----------*- C++ -*-===//
//
// Every line a native program prints goes through print_line or
// print_error_line, on the C streams stdout and stderr, so its lines come out
// in the order it printed them, as the interpreter's do. Neither formats: the
// caller has already spelled the text the JavaScript way (Format.hpp,
// to_js_string in ctnative.hpp).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdio>
#include <string_view>

namespace ctnative {

/// Writes `line` and a newline to standard output.
inline void print_line(std::string_view line) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
}

/// Writes `line` and a newline to standard error, after everything the
/// program has printed to standard output.
inline void print_error_line(std::string_view line) {
    std::fflush(stdout);
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fputc('\n', stderr);
}

} // namespace ctnative
