//===- Error.hpp - Errors native code throws --------------------*- C++ -*-===//
//
// ctnative::type_error and range_error, the TypeError and RangeError native
// code throws where the interpreter throws one. Its what() is the error's `message`, worded as the
// interpreter words it, so a caller that reports `e.message` (the engine seam
// answers "threw: <message>") reports the same text from either engine.
//
// A type_error nothing catches ends the program as an uncaught TypeError ends
// the interpreter's run: "threw: uncaught TypeError: <message>" on stderr and
// exit status 1, instead of std::terminate's abort.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Output.hpp"

#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

namespace ctnative {

/// A JavaScript TypeError (ECMA-262 20.5.5.5), with the interpreter's message.
struct type_error : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

/// A JavaScript RangeError (ECMA-262 20.5.5.2), with V8's message.
struct range_error : std::range_error {
    using std::range_error::range_error;
};

/// `throw new TypeError(message)` (ECMA-262 20.5.6.1.1) where the program uses
/// the new object for nothing but the throw, so its message is all that can
/// be observed.
template <class Text> [[noreturn]] void throw_type_error(const Text & message) {
    throw type_error(std::string{message.value()});
}

namespace detail {
/// The terminate handler that reports an uncaught type_error the way the
/// interpreter reports an uncaught TypeError, after flushing what the program
/// printed. Any other reason to terminate keeps the default abort.
[[noreturn]] inline void report_uncaught() noexcept {
    if (const std::exception_ptr thrown = std::current_exception()) {
        try {
            std::rethrow_exception(thrown);
        } catch (const type_error & error) {
            print_error_line(std::string{"threw: uncaught TypeError: "} + error.what());
            std::_Exit(1);
        } catch (const range_error & error) {
            print_error_line(std::string{"threw: uncaught RangeError: "} + error.what());
            std::_Exit(1);
        } catch (...) {}
    }
    std::abort();
}
/// Installs report_uncaught once per program, before `main` runs.
inline const std::terminate_handler default_terminate = std::set_terminate(report_uncaught);
} // namespace detail

} // namespace ctnative
