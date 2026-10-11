//===- Admission/VariantFields.cpp - Union-typed closed-shape fields ------===//
//
// Admission of one access to a closed-shape field stored as a String union
// carrier (ObjectValues/VariantFields.cpp). A write may store any value the
// union holds, which the lowering converts to the union. A read must be typed
// as the whole union: a read the type lattice narrowed (a field assigned
// before it on every path cannot be undefined there) would need a checked
// narrowing at the read, which this tier does not make yet.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

std::optional<bool> admission::variantField(llvm::StringRef key, mlir::Operation * user,
                                            llvm::ArrayRef<mlir::Operation *> accesses) {
    llvm::SmallVector<mlir::Operation *> same;
    for (mlir::Operation * access : accesses) {
        if (access->hasAttr(kNativeMethod)) { continue; }
        const mlir::Value accessKey = llvm::isa<ctjs::GetPropertyOp>(access)
                                          ? llvm::cast<ctjs::GetPropertyOp>(access).getKey()
                                          : llvm::cast<ctjs::SetPropertyOp>(access).getKey();
        if (ctjs::constantKey(accessKey) == key) { same.push_back(access); }
    }
    const mlir::Type held =
        variantFieldType(same, [&](mlir::Value value) { return typeOf(value); });
    if (!held) { return std::nullopt; }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
        const mlir::Type stored = typeOf(set.getValue());
        return isPrimitiveCarrier(carrierOf(stored)) ||
               refuse(("field `" + key + "` holds a " + printed(held) + " and is stored a " +
                       printed(stored) + ", which has no conversion to it")
                          .str());
    }
    const mlir::Type read = typeOf(user->getResult(0));
    return carrierOf(read) == carrierOf(held) ||
           refuse(("field `" + key + "` holds a " + printed(held) + " but this read is typed " +
                   printed(read) + ", and narrowing a field read is not native yet")
                      .str());
}

} // namespace ctcompile::ctnative::lowering_detail
