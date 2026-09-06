#include "Bindings.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinTypes.h"

namespace ctcompile::cpp {

bool supportsConstBinding(mlir::Type type) {
    if (llvm::isa<mlir::IntegerType, mlir::FloatType, mlir::IndexType, mlir::emitc::PointerType,
                  mlir::emitc::SizeTType, mlir::emitc::SignedSizeTType, mlir::emitc::PtrDiffTType>(
            type)) {
        return true;
    }
    const auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueType>(type);
    if (!opaque) { return false; }
    // These are copyable native value carriers. Arbitrary opaque C++ types
    // can hide references, move-only ownership or const-sensitive operators.
    const auto name = opaque.getValue();
    return name == "std::string" || name == "ctnative::nullable_scalar" ||
           name == "ctnative::nullable_string" || name == "ctnative::object_value" ||
           name == "std::vector<double>" || name == "std::vector<std::string>" ||
           (name.starts_with("std::shared_ptr<") && name.ends_with(">")) ||
           (name.starts_with("ctnative::ctn_env_") &&
            name.find_first_of(" &*") == llvm::StringRef::npos);
}

} // namespace ctcompile::cpp
