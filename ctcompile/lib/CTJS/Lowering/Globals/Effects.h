#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseSet.h"
#include <cstdint>
#include <optional>
#include <string>

namespace ctcompile::ctjs::globals_detail {
// The two kinds of value this walk follows. They escape and are written
// through in the same places, but they are different objects and a diagnostic
// that names the wrong one sends a reader to the wrong line.
enum class watched {
    global_object, // globalThis / window, whose properties ARE the globals
    compiler,      // a value that may be `Function`, which compiles source
};

std::optional<std::uint32_t> function_index_of(FuncOp function);
bool names_global_object(llvm::StringRef name);
bool names_eval(llvm::StringRef name);
std::string describe(mlir::Operation * op);
llvm::StringRef constant_key(mlir::Value key);
bool may_be_function(mlir::Value value);
bool prototype_replaced(mlir::ModuleOp module);
bool hands_back_the_global_object(llvm::StringRef key);
bool hands_back_the_compiler(llvm::StringRef key);
std::optional<std::string> opaque_refusal(mlir::ModuleOp module);
llvm::DenseSet<mlir::StringAttr> refused_store_names(mlir::ModuleOp module);
bool in_prologue(StoreGlobalOp store);
} // namespace ctcompile::ctjs::globals_detail
