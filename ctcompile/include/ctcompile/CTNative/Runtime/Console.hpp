//===- Console.hpp - The native console object ------------------*- C++ -*-===//
//
// `console.log(...)`, as generated native code calls it:
// `ctnative::console.log(a, b)`. It prints one line on stdout, exactly the
// line the ctbrowser VM's console binding records (Shell/bindings/window/
// callbacks.cpp, install_console): each argument converted by ToString and
// joined by one space. The check-workload.py probe and the console fixtures
// compare those lines with the VM's.
//
// ctnative.hpp includes this header last, after the carriers it prints, and it
// does not include ctnative.hpp back: a test driver that inlines ctnative.hpp's
// text into the generated file (CTNative/harness.py, inline_runtime) would
// otherwise get the runtime a second time from the real header.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Output.hpp"

#include <string>
#include <type_traits>

namespace ctnative {

namespace console_detail {

/// ToString (ECMA-262 7.1.17) of one argument, as the VM's context::to_string
/// writes it: to_js_string for a carrier, the same text String(x) and `{}`
/// give, and Number::toString for a C++ number. A C++ bool is not a
/// JavaScript value, so it has none.
template <class T>
requires(!std::is_same_v<T, bool>)
void append(std::string & line, const T & value) {
    if constexpr (std::is_arithmetic_v<T>) {
        line += ctbrowser::number_to_string(static_cast<double>(value));
    } else {
        line += to_js_string(value).value();
    }
}

/// The line console.log prints for `args`: their strings joined by spaces.
template <class... Args> std::string line(const Args &... args) {
    std::string out;
    bool first = true;
    ((out += first ? "" : " ", first = false, append(out, args)), ...);
    (void)first; // An empty argument list leaves it unread.
    return out;
}

} // namespace console_detail

/// The console object's methods native code calls.
struct console_object {
    /// console.log (WHATWG Console Standard 1.1.6, Logger with "log"): one
    /// line of the arguments' strings joined by spaces, through print_line,
    /// the stream every native line uses. Returns undefined.
    struct log_method {
        template <class... Args> nullable_scalar operator()(const Args &... args) const {
            print_line(console_detail::line(args...));
            return undefined_t{};
        }
    };
    log_method log;
};
inline constexpr console_object console{};

} // namespace ctnative
