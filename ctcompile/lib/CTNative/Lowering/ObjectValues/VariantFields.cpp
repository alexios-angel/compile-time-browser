//===- VariantFields.cpp - Union-typed closed-shape fields ----------------===//
//
// A closed-shape field whose writes and reads join to an optional String, or
// to a union of String with Boolean or Number, is stored as that union's
// carrier (nullable_string, boolean_string, nullable_number_string, ...): one
// member whose tag keeps the JavaScript type of each value.
// A component's options are such fields: `pause` is a String or a Boolean.
// So is an element that may be null (`parent: null`, later an element), whose
// carrier is the optional element view (nullable_element).
//
//===----------------------------------------------------------------------===//

#include "../LoweringSupport.h"

namespace ctcompile::ctnative::lowering_detail {

mlir::Type variantFieldType(llvm::ArrayRef<mlir::Operation *> accesses,
                            llvm::function_ref<mlir::Type(mlir::Value)> typeOf) {
    mlir::Type joined;
    for (mlir::Operation * access : accesses) {
        mlir::Value value = access->getNumResults() ? access->getResult(0) : mlir::Value{};
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(access)) { value = set.getValue(); }
        const mlir::Type type = typeOf(value);
        if (!type) { return {}; }
        joined = joined ? meet(joined, type) : type;
    }
    const carrier held = carrierOf(joined);
    return held == carrier::nullableString || held == carrier::nullableElement ||
                   isStringUnionCarrier(held)
               ? joined
               : mlir::Type{};
}

} // namespace ctcompile::ctnative::lowering_detail
