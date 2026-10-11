//===- BuiltinStatics.cpp - Calls of built-in static methods --------------===//
//
// The table of static methods of the realm's built-in namespace objects
// (`Number.parseInt`, `Math.min`) that native code calls through the runtime's
// built-in objects, `ctnative::Number.parseInt(s, radix)` and
// `ctnative::Math.min(a, b)`, and the recognition of a call of one.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative {

namespace {

using Param = TypeInference::BuiltinStatic::Param;

/// The one place built-in statics are recognised. Each row is a static method
/// whose runtime member (Runtime/Number.hpp, Runtime/Math.hpp) implements the
/// spec for the parameter types listed; a call with fewer than `required`
/// arguments or more than `arity` is not recognised.
constexpr TypeInference::BuiltinStatic builtinStatics[] = {
    // Number.parseInt (ECMA-262 21.1.2.13); a missing radix is 0.
    {"Number", "parseInt", "ctnative::Number.parseInt", {Param::string, Param::number}, 2, 1},
    // Math.min (21.3.2.25) of exactly two Numbers.
    {"Math", "min", "ctnative::Math.min", {Param::number, Param::number}, 2, 2},
};

/// Whether --ctjs-resolve-globals found that this program never binds
/// `name`, so a read of it is the realm's own value. Its `ctjs.globals` row
/// gives that reason only when no store, no write through the global object
/// and no dynamic global write can reach the name (ResolveGlobals.cpp,
/// bound_closure and clause 5).
bool realmBinding(mlir::Operation * op, llvm::StringRef name) {
    auto module = op->getParentOfType<mlir::ModuleOp>();
    auto rows = module ? module->getAttrOfType<mlir::ArrayAttr>("ctjs.globals") : nullptr;
    if (!rows) { return false; }
    for (auto row : rows.getAsRange<mlir::DictionaryAttr>()) {
        auto bound = row.getAs<mlir::StringAttr>("name");
        auto why = row.getAs<mlir::StringAttr>("reason");
        if (bound && bound.getValue() == name) {
            return why && why.getValue() == "never stored in this program - a host binding";
        }
    }
    return false;
}

} // namespace

const TypeInference::BuiltinStatic * TypeInference::builtinStaticOf(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    if (!call) { return nullptr; }
    auto read = ctjs::receiverMethod(call);
    auto global = read ? read.getObject().getDefiningOp<ctjs::LoadGlobalOp>() : nullptr;
    if (!global || read.getObject() != call.getReceiver() || !read.getResult().hasOneUse()) {
        return nullptr;
    }
    const std::size_t count = call.getArgs().size();
    for (const BuiltinStatic & row : builtinStatics) {
        if (global.getName() == row.global && ctjs::methodKey(call) == row.member &&
            count >= row.required && count <= row.arity) {
            return realmBinding(call, row.global) ? &row : nullptr;
        }
    }
    return nullptr;
}

} // namespace ctcompile::ctnative
