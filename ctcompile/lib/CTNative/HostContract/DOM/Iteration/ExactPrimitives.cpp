//===- ExactPrimitives.cpp - Exact constant folding -------------*- C++ -*-===//
//
// Folds unary, comparison and arithmetic operations whose operands are
// constant primitives, for normalizeDOMCustomIteration's helper expansion.
// Constant operands mean no conversion hook (valueOf, toString) can run, so
// the folded value matches what the VM would compute.
//
//===----------------------------------------------------------------------===//

#include "ExactPrimitives.h"

#include "../../../Analysis/StringNumber.h"
#include "../../Prefix/Analysis.h"
#include "ctcompile/CTNative/Runtime/String.hpp"
#include "mlir/IR/Matchers.h"
#include "llvm/ADT/APFloat.h"

#include <cmath>
#include <limits>

namespace ctcompile::ctnative::dom_iteration_detail {

namespace {
/// Charges `text`'s byte count to `work`. When it does not fit, drains the
/// budget and returns false.
bool chargeText(std::string_view text, dom_source_detail::DOMSource & work) {
    if (text.size() > work.remaining) {
        work.remaining = 0;
        return false;
    }
    work.remaining -= static_cast<unsigned>(text.size());
    return true;
}

/// ToNumber of a constant primitive; nullopt for anything else or when the
/// budget cannot pay for a string's bytes.
std::optional<double> exactNumber(mlir::Attribute value, dom_source_detail::DOMSource & work) {
    if (auto numeric = llvm::dyn_cast<ctjs::NumberAttr>(value)) { return numeric.getDouble(); }
    if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(value)) {
        return boolean.getValue() ? 1.0 : 0.0;
    }
    if (llvm::isa<ctjs::NullAttr>(value)) { return 0.0; }
    if (llvm::isa<ctjs::UndefinedAttr>(value)) { return std::numeric_limits<double>::quiet_NaN(); }
    if (auto text = llvm::dyn_cast<ctjs::StringAttr>(value)) {
        if (!chargeText(text.getValue(), work)) { return std::nullopt; }
        return literalStringNumber(text.getValue());
    }
    return std::nullopt;
}
} // namespace

std::optional<bool> exactBoolean(mlir::Value condition) {
    mlir::IntegerAttr value;
    if (mlir::matchPattern(condition, mlir::m_Constant(&value))) {
        return !value.getValue().isZero();
    }
    if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
        auto constant = truthy.getValue().getDefiningOp<ctjs::ConstantOp>();
        if (constant) {
            auto value = PrimitiveAlternatives::literal(constant.getValue());
            if (value.known) { return value.truthy != 0; }
        }
    }
    return std::nullopt;
}

mlir::Attribute exactUnary(ctjs::UnaryOp unary, dom_source_detail::DOMSource & work) {
    if (unary->getNumOperands() != 1 || !unary.getKindAttr()) { return {}; }
    auto input = unary->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
    if (!input || !input.getValue()) { return {}; }
    if (unary.getKind() == ctjs::UnaryKind::Not) {
        const auto value = PrimitiveAlternatives::literal(input.getValue());
        return value.known ? ctjs::BooleanAttr::get(unary.getContext(), !value.truthy)
                           : mlir::Attribute{};
    }
    if (unary.getKind() == ctjs::UnaryKind::TypeOf || unary.getKind() == ctjs::UnaryKind::Void) {
        if (!ctjs::isPrimitiveAttr(input.getValue())) { return {}; }
        return host_detail::prefixUnary(unary.getKind(),
                                        host_detail::prefixValue::constant(input.getValue()),
                                        unary.getContext())
            .literal;
    }
    if (unary.getKind() != ctjs::UnaryKind::Plus && unary.getKind() != ctjs::UnaryKind::Neg &&
        unary.getKind() != ctjs::UnaryKind::BitNot) {
        return {};
    }
    auto number = llvm::dyn_cast<ctjs::NumberAttr>(input.getValue());
    if (!number) {
        const auto converted = exactNumber(input.getValue(), work);
        if (!converted) { return {}; }
        number = ctjs::numberAttr(unary.getContext(), *converted);
    }
    if (unary.getKind() == ctjs::UnaryKind::Plus) { return number; }
    if (unary.getKind() == ctjs::UnaryKind::BitNot) {
        return ctjs::numberAttr(unary.getContext(),
                                (~::ctnative::js_num{number.getDouble()}).value());
    }
    // Number negation changes only the sign, including zero and NaN.
    llvm::APFloat result(llvm::APFloat::IEEEdouble(), llvm::APInt(64, number.getBits()));
    result.changeSign();
    return ctjs::NumberAttr::get(unary.getContext(), result.bitcastToAPInt().getZExtValue());
}

std::optional<bool> exactComparison(ctjs::CompareOp comparison,
                                    dom_source_detail::DOMSource & work) {
    if (comparison->getNumOperands() != 2 || !comparison.getKindAttr()) { return std::nullopt; }
    auto lhs = comparison->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
    auto rhs = comparison->getOperand(1).getDefiningOp<ctjs::ConstantOp>();
    if (!lhs || !rhs || !lhs.getValue() || !rhs.getValue()) { return std::nullopt; }
    // Reuse the source-prefix proof: strict primitive equality and exact
    // String equality need neither conversion nor object hooks.
    const auto primitive = host_detail::prefixCompare(
        comparison.getKind(), host_detail::prefixValue::constant(lhs.getValue()),
        host_detail::prefixValue::constant(rhs.getValue()), comparison.getContext());
    if (auto result = llvm::dyn_cast_if_present<ctjs::BooleanAttr>(primitive.literal)) {
        return result.getValue();
    }
    if (comparison.getKind() == ctjs::CompareKind::Eq) {
        // Absence compares by tag, before any numeric conversion. Only
        // exact primitives can bypass conversion; objects/BigInts refuse.
        const auto absent = [](mlir::Attribute value) {
            return llvm::isa<ctjs::NullAttr, ctjs::UndefinedAttr>(value);
        };
        if (absent(lhs.getValue()) || absent(rhs.getValue())) {
            if (!ctjs::isPrimitiveAttr(lhs.getValue()) || !ctjs::isPrimitiveAttr(rhs.getValue())) {
                return std::nullopt;
            }
            return absent(lhs.getValue()) && absent(rhs.getValue());
        }
    } else if (llvm::isa<ctjs::StringAttr>(lhs.getValue()) &&
               llvm::isa<ctjs::StringAttr>(rhs.getValue())) {
        const std::string_view a = llvm::cast<ctjs::StringAttr>(lhs.getValue()).getValue();
        const std::string_view b = llvm::cast<ctjs::StringAttr>(rhs.getValue()).getValue();
        // Charge the full inputs to the budget before decoding or allocating.
        // ctbrowser::wtf8_to_utf16 never fails, even on malformed bytes, so
        // require a lossless WTF-8 round trip before treating them as JS
        // strings.
        for (auto text : {a, b}) {
            if (!chargeText(text, work)) { return std::nullopt; }
        }
        const auto left = ctbrowser::wtf8_to_utf16(a), right = ctbrowser::wtf8_to_utf16(b);
        if (ctbrowser::utf16_to_wtf8(left) != a || ctbrowser::utf16_to_wtf8(right) != b) {
            return std::nullopt;
        }
        const auto order = left <=> right;
        switch (comparison.getKind()) {
        case ctjs::CompareKind::Lt: return order < 0;
        case ctjs::CompareKind::Le: return order <= 0;
        case ctjs::CompareKind::Gt: return order > 0;
        case ctjs::CompareKind::Ge: return order >= 0;
        default: return std::nullopt;
        }
    }
    const auto left = exactNumber(lhs.getValue(), work), right = exactNumber(rhs.getValue(), work);
    if (!left || !right) { return std::nullopt; }
    // Converting constant primitives cannot call hooks. IEEE-754 double
    // comparison already gives JS semantics: NaN is unordered and -0 == +0.
    const double a = *left, b = *right;
    switch (comparison.getKind()) {
    case ctjs::CompareKind::Eq: return a == b;
    case ctjs::CompareKind::Lt: return a < b;
    case ctjs::CompareKind::Le: return a <= b;
    case ctjs::CompareKind::Gt: return a > b;
    case ctjs::CompareKind::Ge: return a >= b;
    default: return std::nullopt;
    }
}

mlir::Attribute exactArithmetic(mlir::Operation * binary, dom_source_detail::DOMSource & work) {
    if (!llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp>(binary) || binary->getNumOperands() != 2) {
        return {};
    }
    auto kind = binary->getAttrOfType<ctjs::BinaryKindAttr>("kind");
    if (!kind) { return {}; }
    const bool isStatic = llvm::isa<ctjs::BinaryStaticOp>(binary);
    switch (kind.getValue()) {
    // Add and the six bitwise/shift operators fold for both ctjs.binary and
    // ctjs.binary_static: on the constant operands required below, the two
    // agree. The rest fold only for the generic ctjs.binary.
    case ctjs::BinaryKind::Add:
    case ctjs::BinaryKind::BitAnd:
    case ctjs::BinaryKind::BitOr:
    case ctjs::BinaryKind::BitXor:
    case ctjs::BinaryKind::Shl:
    case ctjs::BinaryKind::Shr:
    case ctjs::BinaryKind::UShr: break;
    case ctjs::BinaryKind::Concat:
    case ctjs::BinaryKind::Sub:
    case ctjs::BinaryKind::Mul:
    case ctjs::BinaryKind::Div:
    case ctjs::BinaryKind::Mod:
    case ctjs::BinaryKind::Pow:
        if (isStatic) { return {}; }
        break;
    default: return {};
    }
    auto lhs = binary->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
    auto rhs = binary->getOperand(1).getDefiningOp<ctjs::ConstantOp>();
    if (!lhs || !rhs || !lhs.getValue() || !rhs.getValue()) { return {}; }
    // Concat always converts both primitives to String; generic Add does
    // so only if either is a String. Static Add remains numeric.
    if (!isStatic && (kind.getValue() == ctjs::BinaryKind::Concat ||
                      (kind.getValue() == ctjs::BinaryKind::Add &&
                       (llvm::isa<ctjs::StringAttr>(lhs.getValue()) ||
                        llvm::isa<ctjs::StringAttr>(rhs.getValue()))))) {
        if (!ctjs::isPrimitiveAttr(lhs.getValue()) || !ctjs::isPrimitiveAttr(rhs.getValue())) {
            return {};
        }
        const auto text = [&](mlir::Attribute value) -> std::optional<::ctnative::js_string> {
            if (auto string = llvm::dyn_cast<ctjs::StringAttr>(value)) {
                const std::string_view bytes = string.getValue();
                // Charge before decoding or copying. Operands that round-trip
                // leave at most one surrogate pair for concatenation to join.
                if (!chargeText(bytes, work) ||
                    ctbrowser::utf16_to_wtf8(ctbrowser::wtf8_to_utf16(bytes)) != bytes) {
                    return std::nullopt;
                }
                return ::ctnative::js_string{bytes};
            }
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(value)) {
                return ::ctnative::js_string{ctbrowser::number_to_string(number.getDouble())};
            }
            if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(value)) {
                return boolean.getValue() ? ::ctnative::js_string{"true"}
                                          : ::ctnative::js_string{"false"};
            }
            if (llvm::isa<ctjs::NullAttr>(value)) { return ::ctnative::js_string{"null"}; }
            if (llvm::isa<ctjs::UndefinedAttr>(value)) {
                return ::ctnative::js_string{"undefined"};
            }
            return std::nullopt;
        };
        auto left = text(lhs.getValue()), right = text(rhs.getValue());
        if (!left || !right || !chargeText(left->value(), work) ||
            !chargeText(right->value(), work)) {
            return {};
        }
        // js_string's operator+ joins a trailing lead surrogate to a leading
        // trail surrogate exactly as the VM does, so the folded result matches.
        const auto joined = std::move(*left) + *right;
        return ctjs::StringAttr::get(binary->getContext(), joined.value());
    }
    const auto number = [&](mlir::Attribute value) -> ctjs::NumberAttr {
        if (auto original = llvm::dyn_cast<ctjs::NumberAttr>(value)) { return original; }
        const auto converted = exactNumber(value, work);
        return converted ? ctjs::numberAttr(binary->getContext(), *converted)
                         : ctjs::NumberAttr{};
    };
    auto left = number(lhs.getValue()), right = number(rhs.getValue());
    if (!left || !right) { return {}; }
    if (ctjs::isBitwise(kind.getValue())) {
        // Reuse ::ctnative::js_num's operators, which already apply
        // ToInt32/ToUint32 and mask shift counts to five bits.
        const ::ctnative::js_num a{left.getDouble()}, b{right.getDouble()};
        ::ctnative::js_num result;
        switch (kind.getValue()) {
        case ctjs::BinaryKind::BitAnd: result = a & b; break;
        case ctjs::BinaryKind::BitOr: result = a | b; break;
        case ctjs::BinaryKind::BitXor: result = a ^ b; break;
        case ctjs::BinaryKind::Shl: result = a << b; break;
        case ctjs::BinaryKind::Shr: result = a >> b; break;
        case ctjs::BinaryKind::UShr: result = a.unsigned_shift_right(b); break;
        default: return {};
        }
        return ctjs::numberAttr(binary->getContext(), result.value());
    }
    if (kind.getValue() == ctjs::BinaryKind::Pow) {
        const double base = left.getDouble(), exponent = right.getDouble();
        // Match native Number exponentiation: libm differs at magnitude one
        // with a NaN/infinite exponent. Keep NaN literals canonical.
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double result = exponent == 0 ? 1
                              : std::abs(base) == 1 && !std::isfinite(exponent)
                                  ? nan
                                  : std::pow(base, exponent);
        return ctjs::numberAttr(binary->getContext(), std::isnan(result) ? nan : result);
    }
    // Exact Number operands cannot invoke conversion hooks. LLVM supplies
    // binary64 rounding independently of the host's floating-point state.
    llvm::APFloat result(llvm::APFloat::IEEEdouble(), llvm::APInt(64, left.getBits()));
    const llvm::APFloat operand(llvm::APFloat::IEEEdouble(), llvm::APInt(64, right.getBits()));
    switch (kind.getValue()) {
    case ctjs::BinaryKind::Add:
        (void)result.add(operand, llvm::APFloat::rmNearestTiesToEven);
        break;
    case ctjs::BinaryKind::Sub:
        (void)result.subtract(operand, llvm::APFloat::rmNearestTiesToEven);
        break;
    case ctjs::BinaryKind::Mul:
        (void)result.multiply(operand, llvm::APFloat::rmNearestTiesToEven);
        break;
    case ctjs::BinaryKind::Div:
        (void)result.divide(operand, llvm::APFloat::rmNearestTiesToEven);
        break;
    case ctjs::BinaryKind::Mod: (void)result.mod(operand); break;
    default: return {};
    }
    return ctjs::NumberAttr::get(binary->getContext(), result.bitcastToAPInt().getZExtValue());
}

} // namespace ctcompile::ctnative::dom_iteration_detail
