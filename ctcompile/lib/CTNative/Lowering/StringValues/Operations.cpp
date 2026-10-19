//===- Operations.cpp - Lower String-valued ctjs operations ---------------===//
//
// lowering::replaceStringValue: lowers ctjs operations on native String
// carriers (`length`, `text[i]`, concatenation, `typeof` of String unions, and loose or
// strict equality involving a String) to EmitC calls of ctnative runtime
// helpers. A template literal, `a${x}b`, is one
// `ctnative::format("a{}b", x)` (Runtime/Format.hpp).
//
//===----------------------------------------------------------------------===//

#include "../EmitC/Emitter.h"

#include "ctcompile/Support/CppLiterals.hpp"

namespace ctcompile::ctnative::lowering_detail {

namespace {
/// On a ctnative::format call a template literal made, its format text, so
/// the literal's next piece extends the call instead of starting another.
constexpr llvm::StringLiteral kTemplateText = "ctnative.template_text";

/// Appends one piece of a template literal to `text` and `arguments`: a String
/// constant's bytes, with braces doubled for std::format, or `{}` and a value
/// whose carrier the formatter prints as ToString. False for anything else.
bool appendTemplatePiece(mlir::Value piece, std::string & text,
                         llvm::SmallVectorImpl<mlir::Value> & arguments) {
    if (auto constant = piece.getDefiningOp<ec::ConstantOp>()) {
        auto initializer = llvm::dyn_cast<ec::OpaqueAttr>(constant.getValue());
        llvm::StringRef spelled = initializer ? initializer.getValue() : llvm::StringRef{};
        // stringConstant's spelling: `ctnative::js_string(<literal>, <size>)`.
        if (!spelled.consume_front("ctnative::js_string(") || !spelled.consume_back(")")) {
            return false;
        }
        spelled = spelled.rsplit(", ").first;
        std::string bytes;
        if (!cpp::parse_c_string_literal(spelled, bytes)) { return false; }
        for (const char c : bytes) {
            text += c;
            if (c == '{' || c == '}') { text += c; }
        }
        return true;
    }
    if (isFormattableCarrier(piece.getType())) {
        text += "{}";
        arguments.push_back(piece);
        return true;
    }
    return false;
}
} // namespace

bool lowering::replaceStringValue(mlir::Operation * op) {
    mlir::OpBuilder b(op);
    const auto where = op->getLoc();
    const auto swap = [&](mlir::Value with) {
        op->getResult(0).replaceAllUsesWith(with);
        eraseIfUnused(op);
    };
    const auto string = carrierType(context, carrier::string);
    // `text[i]` answers a String or undefined; `text.length` a Number. The
    // key cannot tell them apart any more: its constant is already lowered.
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && read.getObject().getType() == string &&
        read.getResult().getType() == carrierType(context, carrier::nullableString)) {
        // `text[i]`: admission proved the index a Number.
        const auto number = carrierType(context, carrier::number);
        swap(callValue(b, where, read.getResult().getType(), "ctnative::string_at",
                       mlir::ValueRange{convertScalar(b, where, read.getObject(), string),
                                        convertScalar(b, where, read.getKey(), number)}));
        return true;
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && read.getObject().getType() == string) {
        // Admission proved the key is `length` before key constants were
        // lowered. js_string's length() counts UTF-16 code units over the
        // stored WTF-8 bytes, not bytes (divergence ND-1 in
        // ctcompile/docs/native-divergences.md).
        swap(methodCall(b, where, carrierType(context, carrier::number),
                        convertScalar(b, where, read.getObject(), string), "length"));
        return true;
    }
    if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op);
        binary &&
        (binary.getKind() == ctjs::BinaryKind::Add ||
         binary.getKind() == ctjs::BinaryKind::Concat) &&
        binary.getResult().getType() == string) {
        // A template literal's concatenations become one ctnative::format call:
        // the first makes it, each later one extends the call its left side
        // is, and a piece the formatter cannot print keeps `+`.
        if (binary.getKind() == ctjs::BinaryKind::Concat) {
            std::string text;
            llvm::SmallVector<mlir::Value> arguments;
            auto previous = binary.getLhs().getDefiningOp<ec::CallOpaqueOp>();
            const bool extends =
                previous && previous->hasAttr(kTemplateText) && binary.getLhs().hasOneUse();
            if (extends) {
                text = previous->getAttrOfType<mlir::StringAttr>(kTemplateText).getValue();
                llvm::append_range(arguments, previous.getOperands().drop_front());
            }
            if ((extends || appendTemplatePiece(binary.getLhs(), text, arguments)) &&
                appendTemplatePiece(binary.getRhs(), text, arguments) && !arguments.empty()) {
                llvm::SmallVector<mlir::Value> operands{ec::LiteralOp::create(
                    b, where, ec::PointerType::get(ec::OpaqueType::get(context, "const char")),
                    cpp::c_string_literal(text))};
                operands.append(arguments);
                mlir::Value formatted = callValue(b, where, string, "ctnative::format", operands);
                formatted.getDefiningOp()->setAttr(kTemplateText, b.getStringAttr(text));
                swap(formatted);
                if (extends) { eraseIfUnused(previous); }
                return true;
            }
        }
        const auto operand = [&](mlir::Value value) {
            // A definite Number or Boolean selects its typed `+` overload. Any
            // other operand (a tagged or optional String) is converted to a
            // String first.
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
    const auto hasStringAlternative = [](mlir::Type type) {
        static const llvm::StringSet<> carriers{kStringType,        kNullableStringType,
                                                kBooleanStringType, kNullableBooleanStringType,
                                                kNumberStringType,  kNullableNumberStringType};
        return carriers.contains(opaqueSpelling(type));
    };
    auto compare = llvm::dyn_cast<ctjs::CompareOp>(op);
    if (!compare ||
        (compare.getKind() != ctjs::CompareKind::Eq &&
         compare.getKind() != ctjs::CompareKind::StrictEq) ||
        (!hasStringAlternative(compare.getLhs().getType()) &&
         !hasStringAlternative(compare.getRhs().getType()))) {
        return false;
    }
    // Two definite Strings keep the direct C++ comparison. Any mixed or
    // optional pair keeps its tags until the runtime helper applies the
    // JavaScript equality rule.
    if (compare.getLhs().getType() == string && compare.getRhs().getType() == string) {
        return false;
    }
    // The same optional or union carrier compares its tags and values directly.
    if (compare.getKind() == ctjs::CompareKind::StrictEq &&
        compare.getLhs().getType() == compare.getRhs().getType()) {
        auto same = ec::CmpOp::create(b, where, b.getI1Type(), ec::CmpPredicate::eq,
                                      compare.getLhs(), compare.getRhs());
        swap(convertScalar(b, where, same, op->getResult(0).getType()));
        return true;
    }
    const auto helper = compare.getKind() == ctjs::CompareKind::Eq
                            ? "ctnative::loose_equals"
                            : "ctnative::primitive_strict_equal";
    auto value = callValue(b, where, carrierType(context, carrier::boolean), helper,
                           mlir::ValueRange{compare.getLhs(), compare.getRhs()});
    swap(convertScalar(b, where, value, op->getResult(0).getType()));
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
