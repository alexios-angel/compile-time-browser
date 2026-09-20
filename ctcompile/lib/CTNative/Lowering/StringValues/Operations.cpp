#include "../EmitC/Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

bool lowering::replaceStringValue(mlir::Operation * op) {
    mlir::OpBuilder b(op);
    const auto where = op->getLoc();
    const auto swap = [&](mlir::Value with) {
        op->getResult(0).replaceAllUsesWith(with);
        eraseIfUnused(op);
    };
    const auto string = carrierType(context, carrier::string);
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && read.getObject().getType() == string) {
        // Admission proved the length key before key constants were lowered.
        // ND-1: the engine's String length counts its stored UTF-8 bytes.
        auto size = callWithConstValueOperands(
            b, where, mlir::TypeRange{ec::OpaqueType::get(context, "std::size_t")},
            b.getStringAttr("std::size"),
            mlir::ValueRange{convertScalar(b, where, read.getObject(),
                                           ec::OpaqueType::get(context, kRawStringType))});
        auto count = ec::CastOp::create(b, where, b.getF64Type(), size.getResult(0));
        swap(convertScalar(b, where, count, carrierType(context, carrier::number)));
        return true;
    }
    if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op);
        binary &&
        (binary.getKind() == ctjs::BinaryKind::Add ||
         binary.getKind() == ctjs::BinaryKind::Concat) &&
        binary.getResult().getType() == string) {
        swap(ec::AddOp::create(b, where, string, convertScalar(b, where, binary.getLhs(), string),
                               convertScalar(b, where, binary.getRhs(), string)));
        return true;
    }
    llvm::StringRef helper;
    llvm::SmallVector<mlir::Value> operands;
    mlir::Type result;
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op);
        unary && unary.getKind() == ctjs::UnaryKind::TypeOf &&
        isNullableStringCarrier(unary.getOperand().getType())) {
        helper = "ctnative::string_typeof";
        operands.push_back(unary.getOperand());
        result = ec::OpaqueType::get(context, kRawStringType);
    } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op);
               compare &&
               (compare.getKind() == ctjs::CompareKind::Eq ||
                compare.getKind() == ctjs::CompareKind::StrictEq) &&
               ((isNullableStringCarrier(compare.getLhs().getType()) ||
                 isNullableStringCarrier(compare.getRhs().getType())) ||
                (compare.getLhs().getType() == string &&
                 isNullableCarrier(compare.getRhs().getType())) ||
                (compare.getRhs().getType() == string &&
                 isNullableCarrier(compare.getLhs().getType())))) {
        helper = compare.getKind() == ctjs::CompareKind::Eq ? "ctnative::string_equal"
                                                            : "ctnative::string_strict_equal";
        operands = {compare.getLhs(), compare.getRhs()};
        result = carrierType(context, carrier::boolean);
    } else {
        return false;
    }
    auto value = callWithConstValueOperands(b, where, mlir::TypeRange{result},
                                            b.getStringAttr(helper), operands)
                     .getResult(0);
    swap(convertScalar(b, where, value, op->getResult(0).getType()));
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
