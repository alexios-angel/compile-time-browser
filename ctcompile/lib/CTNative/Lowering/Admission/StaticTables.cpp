//===- Admission/StaticTables.cpp - Constant tables of run-once statics ---===//
//
// Admission of the operations that build and read a run-once static's constant
// table (Analysis/Inference/StaticTables.cpp), and the refusal for a reader of
// a static whose object literal the proof did not accept.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Analysis/StaticTables.h"
#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

std::optional<bool> admission::staticTableOperation(mlir::Operation * o) {
    // Each cell of a static carries the proof's reason when its literal is
    // not a table. When the static has no carrier for another reason either
    // (a method table or an identity object has one), that is why the frame
    // cannot read it.
    if (llvm::isa<ctjs::CreateCellOp>(o)) {
        auto why = o->getAttrOfType<mlir::StringAttr>(kNativeStaticTableReason);
        auto name = o->getAttrOfType<mlir::StringAttr>(kNativeStaticCell);
        if (!why || !name || carrierOf(typeOf(o->getResult(0))) != carrier::none) {
            return std::nullopt;
        }
        return refuse(("it reads `" +
                       name.getValue().drop_front(llvm::StringRef("static_").size()) +
                       "`, a static variable of a function that runs once, holding an object "
                       "literal that " +
                       why.getValue())
                          .str());
    }
    if (!o->hasAttr(kNativeStaticTable)) {
        if (llvm::isa<ctjs::GetPropertyOp>(o) &&
            carrierOf(typeOf(o->getOperand(0))) == carrier::staticTable) {
            return refuse("a read of a run-once static's constant table that its proof did "
                          "not check");
        }
        return std::nullopt;
    }
    // The literal, its fields and the proved reads: the proof checked them,
    // and EmitC/StaticTables.cpp spells any key as a C++ member.
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
