#include "Bindings.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/StringSet.h"

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
    static const llvm::StringSet<> copyableCarriers{"std::string",
                                                    "ctnative::js_num",
                                                    "ctnative::js_string",
                                                    "ctnative::boolean_string",
                                                    "ctnative::nullable_boolean_string",
                                                    "ctnative::number_string",
                                                    "ctnative::nullable_number_string",
                                                    "std::optional<ctnative::number_string>",
                                                    "ctnative::nullable_scalar",
                                                    "ctnative::nullable_string",
                                                    "ctnative::object_value",
                                                    "std::vector<double>",
                                                    "std::vector<std::string>"};
    return copyableCarriers.contains(name) ||
           (name.starts_with("std::shared_ptr<") && name.ends_with(">")) ||
           (name.starts_with("ctnative::ctn_env_") &&
            name.find_first_of(" &*") == llvm::StringRef::npos);
}

} // namespace ctcompile::cpp
