//===- TypeErrors.h - throw new TypeError(message) natively -----*- C++ -*-===//
//
// `throw new TypeError(message)` where the program uses the new Error for
// nothing but the throw, as Bootstrap's Config type check does. Only its
// message can be observed, so native code throws ctnative::type_error with
// that message (Runtime/Error.hpp), and a program nothing catches it in ends
// as the interpreter ends on an uncaught TypeError.
//
// The importer leaves such a throw as a block terminator that the lift to
// structured control flow cannot place beside the function's returns.
// structureTypeErrorThrows wraps it in a saved-throw region (a result-free,
// no-inline scf.execute_region holding only the throw, as the DOM entry's
// saved throws are) followed by an unreachable return, so the function can
// be lifted again.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/IR/BuiltinOps.h"

#include <optional>

namespace ctcompile::ctnative::lowering_detail {

struct admission;

/// The `new TypeError(message)` that `thrown` throws: the realm's TypeError
/// constructed with one argument, used by nothing but this throw. Null
/// otherwise.
ctjs::ConstructOp thrownTypeError(ctjs::ThrowOp thrown);

/// The throw of a saved-throw region whose throw thrownTypeError accepts, or
/// null for any other operation.
ctjs::ThrowOp savedTypeErrorThrow(mlir::Operation * operation);

/// Whether a JavaScript catch or finally anywhere in `module` could see a
/// throw: a function installs an exception handler.
bool catchesExceptions(mlir::ModuleOp module);

/// Wraps each block-terminating throw thrownTypeError accepts in a saved-throw
/// region followed by an unreachable return, unless catchesExceptions. Returns
/// whether it changed anything, so the caller lifts the functions again.
bool structureTypeErrorThrows(mlir::ModuleOp module);

/// Whether `operation` is consumed by an accepted throw: the construct, or a
/// load of TypeError used only by such constructs. They lower to nothing.
bool consumedByTypeErrorThrow(mlir::Operation * operation);

/// Admits an accepted throw (its message must be a String, and no catch or
/// finally may see it), its saved-throw region and what it consumes; nullopt
/// for any other operation, and in a DOM entry, whose saved throws are its own.
std::optional<bool> typeErrorOperation(admission & self, mlir::Operation * o);

} // namespace ctcompile::ctnative::lowering_detail
