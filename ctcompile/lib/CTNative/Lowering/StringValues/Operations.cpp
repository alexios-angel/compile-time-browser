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
        // ND-1: `length` counts UTF-16 code units over the stored WTF-8 bytes.
        auto size = callValue(b, where, ec::OpaqueType::get(context, "std::size_t"),
                              "ctnative::string_length",
                              convertScalar(b, where, read.getObject(),
                                            ec::OpaqueType::get(context, kRawStringType)));
        auto count = ec::CastOp::create(b, where, b.getF64Type(), size);
        swap(convertScalar(b, where, count, carrierType(context, carrier::number)));
        return true;
    }
    if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op);
        binary &&
        (binary.getKind() == ctjs::BinaryKind::Add ||
         binary.getKind() == ctjs::BinaryKind::Concat) &&
        binary.getResult().getType() == string) {
        const auto operand = [&](mlir::Value value) {
            // Exact primitives select their typed addition overload. Tagged
            // scalar/optional String concatenation first materializes its text.
            return isNumberCarrier(value.getType()) || isBooleanCarrier(value.getType())
                       ? value
                       : convertScalar(b, where, value, string);
        };
        swap(ec::AddOp::create(b, where, string, operand(binary.getLhs()),
                               operand(binary.getRhs())));
        return true;
    }
    if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op);
        binary && binary.getKind() == ctjs::BinaryKind::Add &&
        isNumberStringCarrier(binary.getResult().getType())) {
        swap(callValue(b, where, binary.getResult().getType(), "ctnative::add",
                       mlir::ValueRange{binary.getLhs(), binary.getRhs()}));
        return true;
    }
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op);
        unary && unary.getKind() == ctjs::UnaryKind::TypeOf) {
        static const llvm::StringMap<llvm::StringRef> helpers{
            {kNullableStringType, "ctnative::string_typeof"},
            {kBooleanStringType, "ctnative::boolean_string_typeof"},
            {kNullableBooleanStringType, "ctnative::nullable_boolean_string_typeof"},
            {kNumberStringType, "ctnative::number_string_typeof"},
            {kNullableNumberStringType, "ctnative::nullable_number_string_typeof"}};
        if (auto helper = helpers.find(opaqueSpelling(unary.getOperand().getType()));
            helper != helpers.end()) {
            auto result = callValue(b, where, ec::OpaqueType::get(context, kRawStringType),
                                    helper->second, unary.getOperand());
            swap(convertScalar(b, where, result, op->getResult(0).getType()));
            return true;
        }
    }
    llvm::StringRef helper;
    llvm::SmallVector<mlir::Value> operands;
    mlir::Type result;
    const auto hasStringAlternative = [](mlir::Type type) {
        static const llvm::StringSet<> carriers{kStringType,        kNullableStringType,
                                                kBooleanStringType, kNullableBooleanStringType,
                                                kNumberStringType,  kNullableNumberStringType};
        return carriers.contains(opaqueSpelling(type));
    };
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op);
        compare &&
        (compare.getKind() == ctjs::CompareKind::Eq ||
         compare.getKind() == ctjs::CompareKind::StrictEq) &&
        (hasStringAlternative(compare.getLhs().getType()) ||
         hasStringAlternative(compare.getRhs().getType()))) {
        // Exact String pairs keep their direct C++ comparison. Every mixed or
        // optional pair retains its original tags until the selected JS rule.
        if (compare.getLhs().getType() == string && compare.getRhs().getType() == string) {
            return false;
        }
        helper = compare.getKind() == ctjs::CompareKind::Eq ? "ctnative::primitive_equal"
                                                            : "ctnative::primitive_strict_equal";
        operands = {compare.getLhs(), compare.getRhs()};
        result = carrierType(context, carrier::boolean);
    } else {
        return false;
    }
    auto value = callValue(b, where, result, helper, operands);
    swap(convertScalar(b, where, value, op->getResult(0).getType()));
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
