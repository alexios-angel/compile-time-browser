//===- Bases.cpp - emitc.class base classes ------------------------------===//
//
// Implements printClassBases.
//
//===----------------------------------------------------------------------===//

#include "Bases.h"

#include "mlir/IR/Attributes.h"

namespace ctcompile::cpp {

mlir::LogicalResult printClassBases(mlir::emitc::ClassOp classOp, llvm::raw_ostream & os) {
    const auto bases = classOp->getAttrOfType<mlir::ArrayAttr>("ctnative.bases");
    if (!bases) { return mlir::success(); }
    for (size_t i = 0; i < bases.size(); ++i) {
        const auto named = llvm::dyn_cast<mlir::StringAttr>(bases[i]);
        if (!named || named.getValue().empty()) {
            return classOp.emitError("ctnative.bases must be a list of non-empty strings");
        }
        os << (i == 0 ? " : public " : ", public ") << named.getValue();
    }
    return mlir::success();
}

} // namespace ctcompile::cpp
