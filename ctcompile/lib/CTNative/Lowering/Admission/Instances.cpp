//===- Admission/Instances.cpp - Instances a registry Map owns ------------===//
//
// Admission of the operations on instances of a class a registry Map owns
// (Analysis/Instances.h). The registry proof already checked every use of
// such an instance before the constructor was marked: each field access on a
// value read back from the Map sits under a check or in a parameter proved
// non-null. What is left here is what the carrier, a `std::shared_ptr<C>`
// that is null for both null and undefined, cannot represent.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Analysis/Instances.h"
#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

std::optional<bool> admission::instanceOperation(mlir::Operation * o) {
    const auto instance = [&](mlir::Value v) { return carrierOf(typeOf(v)) == carrier::instance; };
    // A field of the class's struct, by a constant key.
    if (llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(o) && instance(o->getOperand(0))) {
        const llvm::StringRef key = ctjs::constantKey(o->getOperand(1));
        if (!ctjs::ordinaryKey(key) || !isCIdentifier(key) || isReservedInCpp(key)) {
            return refuse("a field of an instance whose key is not a plain C++ member name");
        }
        return true; // The field value's carrier is checked with every other value.
    }
    // A null pointer stands for both null and undefined, so a test answers
    // the program's question only where the registry proof found that the
    // two cannot differ (kNativeInstanceTest).
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(o);
        compare && (instance(compare.getLhs()) || instance(compare.getRhs()))) {
        return o->hasAttr(kNativeInstanceTest) ||
               refuse("a comparison of an instance with something that tells null from "
                      "undefined, which one null pointer stands for");
    }
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(o);
        unary && unary.getKind() == ctjs::UnaryKind::TypeOf && instance(unary.getOperand())) {
        return refuse("`typeof` of an instance, which tells null from undefined");
    }
    return std::nullopt;
}

} // namespace ctcompile::ctnative::lowering_detail
