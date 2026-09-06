#include "Admission.h"
#include "mlir/Dialect/Arith/IR/Arith.h"

namespace ctcompile::ctnative::lowering_detail {

bool admission::identityField(mlir::Operation * op) {
    const mlir::Value object = op->getOperand(0);
    const mlir::Type type = typeOf(object);
    bool owned = llvm::isa_and_nonnull<ObjectIdentityType>(type);
    // Refine the exact SSA receiver inside a structured guard. Comparing a
    // different lookup or a reloaded binding cannot establish this fact.
    unsigned ancestors = 0;
    for (auto * child = op; !owned && child && ancestors++ < 256; child = child->getParentOp()) {
        auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(child->getParentOp());
        if (!branch) { continue; }
        bool positive = child->getParentRegion() == &branch.getThenRegion();
        mlir::Value condition = branch.getCondition();
        for (unsigned depth = 0; depth < 16; ++depth) {
            if (auto trunc = condition.getDefiningOp<mlir::arith::TruncIOp>()) {
                condition = trunc.getIn();
            } else if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
                condition = truthy.getValue();
            } else if (auto unary = condition.getDefiningOp<ctjs::UnaryOp>();
                       unary && unary.getKind() == ctjs::UnaryKind::Not) {
                positive = !positive;
                condition = unary.getOperand();
            } else {
                break;
            }
        }
        if (!positive) { continue; }
        if (condition == object && identityOrAbsent(type) && isObjectValueType(type)) {
            owned = true;
        }
        if (auto compare = condition.getDefiningOp<ctjs::CompareOp>();
            compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
            mlir::Value other;
            if (compare.getLhs() == object) { other = compare.getRhs(); }
            if (compare.getRhs() == object) { other = compare.getLhs(); }
            owned |= other && llvm::isa_and_nonnull<ObjectIdentityType>(typeOf(other));
        }
    }
    if (!owned || !isObjectCarrier(carrierOf(type))) {
        return refuse("an owning field receiver may be scalar, null or undefined; "
                      "a strict identity or optional-object truthiness guard is required");
    }
    if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
        return isScalarCarrier(carrierOf(typeOf(store.getValue()))) ||
               refuse("owning object fields require number/boolean/null/undefined values");
    }
    return isScalarCarrier(carrierOf(typeOf(op->getResult(0)))) ||
           refuse("an owning field read has an unsupported scalar schema");
}

} // namespace ctcompile::ctnative::lowering_detail
