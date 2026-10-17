//===- Error.hpp - Errors native code throws --------------------*- C++ -*-===//
//
// ctnative::type_error, the TypeError native code throws where the
// interpreter throws one. Its what() is the error's `message`, worded as the
// interpreter words it, so a caller that reports `e.message` (the engine seam
// answers "threw: <message>") reports the same text from either engine.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>

namespace ctnative {

/// A JavaScript TypeError (ECMA-262 20.5.5.5), with the interpreter's message.
struct type_error : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

} // namespace ctnative
