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

#include <cstdio>
#include <string>
#include <type_traits>

namespace ctnative {

namespace console_detail {

/// ToString (ECMA-262 7.1.17) of each argument kind console.log can hold, as
/// the VM's context::to_string writes it.
inline void append(std::string & line, const js_string & value) {
    line += value.value();
}
inline void append(std::string & line, js_num value) {
    line += ctbrowser::number_to_string(static_cast<double>(value.value()));
}
template <class T>
requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
void append(std::string & line, T value) {
    line += ctbrowser::number_to_string(static_cast<double>(value));
}
inline void append(std::string & line, js_boolean_t value) {
    line += value ? "true" : "false";
}
inline void append(std::string & line, undefined_t) {
    line += "undefined";
}
inline void append(std::string & line, js_null_t) {
    line += "null";
}
inline void append(std::string & line, const nullable_scalar & value) {
    line += value.to_string().value();
}
inline void append(std::string & line, const nullable_string & value) {
    switch (value.tag) {
    case nullable_string::kind::undefined: line += "undefined"; return;
    case nullable_string::kind::null_value: line += "null"; return;
    case nullable_string::kind::string: line += value.value; return;
    }
    std::terminate();
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
    /// line of the arguments' strings joined by spaces. It is printed with
    /// `%s`, as ctcompile-test-native-reference prints the VM's lines, so the
    /// two agree byte for byte. Returns undefined.
    struct log_method {
        template <class... Args> nullable_scalar operator()(const Args &... args) const {
            std::printf("%s\n", console_detail::line(args...).c_str());
            return undefined_t{};
        }
    };
    log_method log;
};
inline constexpr console_object console{};

} // namespace ctnative
