//===- EmitC/VariantFields.cpp - Union-typed closed-shape fields ----------===//
//
// The shape census's member type for a field stored as a String union carrier
// (ObjectValues/VariantFields.cpp): the union's carrier, in place of the
// tagged numeric carrier fieldsOf() gives any field seen with two carriers.
//
//===----------------------------------------------------------------------===//

#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::variantFields(mlir::Value object, llvm::StringMap<mlir::Type> & stored) {
    llvm::StringMap<llvm::SmallVector<mlir::Operation *>> accesses;
    for (mlir::Value alias : aliasesOf(groups, object)) {
        for (mlir::Operation * user : alias.getUsers()) {
            if (user->hasAttr(kNativeMethod)) { continue; }
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                get && get.getObject() == alias) {
                accesses[ctjs::constantKey(get.getKey())].push_back(user);
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                       set && set.getObject() == alias) {
                accesses[ctjs::constantKey(set.getKey())].push_back(user);
            }
        }
    }
    for (auto & [key, fieldAccesses] : accesses) {
        const mlir::Type held =
            variantFieldType(fieldAccesses, [&](mlir::Value value) { return typeOf(value); });
        if (held) { stored[key] = carrierType(context, carrierOf(held)); }
    }
}

} // namespace ctcompile::ctnative::lowering_detail
