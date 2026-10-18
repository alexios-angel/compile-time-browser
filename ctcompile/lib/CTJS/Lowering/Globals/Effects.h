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
#include "llvm/ADT/StringSet.h"
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
/// What decides whether an object the realm made (`Function`, the global
/// object) can have a property the program named: whether the program may
/// write through a prototype table the realm made, which such an object
/// inherits from, and which keys it may write onto an object it did not
/// create. Read once per module.
struct prototype_writes {
    /// Why a realm prototype table may be written; empty when no constant-key
    /// path reaches one other than to read it.
    std::string prototypes;
    /// Why any key at all may be written onto such an object; empty if none.
    std::string any_key;
    /// The keys the program may write onto an object it did not create.
    llvm::StringSet<> written;
};
/// Reads \p module's prototype_writes.
prototype_writes read_prototype_writes(mlir::ModuleOp module);
/// Whether reading \p key off `Function`, or off any other function the
/// realm or the program has, answers undefined however the program runs:
/// \p key is no property the realm gives functions, Function.prototype or
/// Object.prototype, and \p keys show the program cannot add it to them.
bool compiler_lacks(const prototype_writes & writes, llvm::StringRef key);
/// Whether calling the method \p key off the global object runs no code but
/// the realm's, unless the program binds a global of that name (which the
/// walk checks against every binding once it ends): \p key is no property
/// the realm gives the window, its prototypes or Object.prototype, so the read
/// answers undefined or a named element, neither of them callable, or it is
/// Object.prototype's `toString`, which answers a string; and \p writes show
/// the program cannot add it to a prototype table.
bool global_object_lacks(const prototype_writes & writes, llvm::StringRef key);
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
