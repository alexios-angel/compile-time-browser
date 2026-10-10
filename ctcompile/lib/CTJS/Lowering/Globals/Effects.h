//===- Effects.h - Global-object and compiler effect checks -----*- C++ -*-===//
//
// Shared by ResolveGlobals.cpp: queries about values that may be the global
// object or the run-time compiler, and the importer's summaries of bodies it
// refused. Implemented in Effects.cpp, which documents each in detail.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseSet.h"
#include <cstdint>
#include <optional>
#include <string>

namespace ctcompile::ctjs::globals_detail {
/// The kinds of value the clause-5 walk follows. They escape and are written
/// through in the same places, but they are different objects, and a
/// diagnostic that names the wrong one sends a reader to the wrong line.
enum class watched {
    global_object, ///< globalThis / window, whose properties are the globals.
    receiver,      ///< A function's `this`, which may be the global object.
    compiler,      ///< A value that may be `Function`, which compiles source.
};

/// Whether global \p name is bound to the window proxy (`window`,
/// `globalThis`, `self`).
bool names_global_object(llvm::StringRef name);
/// Whether global \p name compiles source text at run time (`Function`,
/// `eval`).
bool names_eval(llvm::StringRef name);
/// Describes \p op for a diagnostic: its name and source location.
std::string describe(mlir::Operation * op);
/// Whether \p value may be a function object; "yes" unless its defining
/// operation proves otherwise.
bool may_be_function(mlir::Value value);
/// Whether some `X.prototype = v` store in \p module may store a function.
bool prototype_replaced(mlir::ModuleOp module);
/// Whether reading \p key off the global object can yield it again.
bool hands_back_the_global_object(llvm::StringRef key);
/// Whether reading \p key off `Function` can yield it (or a caller of it).
bool hands_back_the_compiler(llvm::StringRef key);
/// What the importer's ctjs.skipped rows say about the bodies it refused.
struct skipped_summary {
    /// Set when some refused body may store any global; the reason text.
    std::optional<std::string> opaque;
    /// Global names the refused bodies may store.
    llvm::DenseSet<mlir::StringAttr> stores;
};
/// Reads the module's ctjs.skipped rows into a skipped_summary.
skipped_summary read_skipped(mlir::ModuleOp module);
/// Whether \p store sits in the top level's hoisting prologue.
bool in_prologue(StoreGlobalOp store);
} // namespace ctcompile::ctjs::globals_detail
