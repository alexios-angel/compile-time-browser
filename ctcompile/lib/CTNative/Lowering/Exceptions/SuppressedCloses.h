//===- SuppressedCloses.h - Iterator closes before a throw ------*- C++ -*-===//
//
// A `throw` inside a for-of body closes the loop's iterator first, and an
// error from that close must not replace the value being thrown. The
// bytecode compiler guards the close with a handler whose catch drops what
// was caught:
//
//     push_handler -> pad
//     __ctbrowser_iter_close(record, true)
//     pop_handler
//     pad: (catch_land, nothing read)
//     throw value
//
// push_handler and the ctjs.check after each instruction are terminators
// with side effects, which the lift to structured control flow refuses, so
// the function is left unstructured (Bootstrap's Config type check).
// structureSuppressedCloses rewrites each such handler into the ctjs.invoke
// that calls the close and drops its errors, as the DOM entry's iterator
// close normalization does, so the function can be lifted again.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative::lowering_detail {

/// Rewrites the handlers of every function whose handlers all only suppress
/// an iterator close's errors: the handler's protected path holds constants,
/// the read of `__ctbrowser_iter_close` and one call of it, and its catch
/// reads nothing and continues where the protected path does. Each becomes
/// the constants and the read, a ctjs.invoke of the close with empty
/// continuations, and a branch to that continuation. A function with any
/// other handler is left as it is. Drops ctjs.not_structured from each
/// function it rewrites and returns whether it rewrote any, so the caller
/// lifts the functions again.
bool structureSuppressedCloses(mlir::ModuleOp module);

} // namespace ctcompile::ctnative::lowering_detail
