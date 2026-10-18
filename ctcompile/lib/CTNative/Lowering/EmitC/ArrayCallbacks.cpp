//===- ArrayCallbacks.cpp - Emission of Array-method callback calls -------===//
//
// Spells a call closure lifting specialised for its Array-method callback
// with its JavaScript name and the lifted callback bound to its captures:
//
//   ctnative::Array.prototype.filter.call(a, std::bind_front(callback, e))
//
// The runtime (Runtime/Array.hpp) calls the callback with as many of
// (element, index, array) as it declares; reduce passes its accumulator first,
// and sort its comparator two elements. A captured array is lent as a
// std::span, as a direct call lends one, and a binding the callback or its
// frame writes is passed by address (`std::bind_front(callback, &n)`).
//
//===----------------------------------------------------------------------===//

#include "Vectors.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {
/// The JavaScript method name, recorded while the method read still names it.
constexpr llvm::StringLiteral kMethod = "ctnative.array_callback_method";
/// Per capture, the std::span spelling that lends it, or empty for a value
/// passed as it is. Recorded while the lattice still types the operands.
constexpr llvm::StringLiteral kSpans = "ctnative.array_callback_spans";
} // namespace

void planArrayCallback(lowering & lower, ctjs::CallOp call) {
    mlir::Builder b(call.getContext());
    const llvm::StringRef method = ctjs::methodKey(call);
    const std::size_t captures = call.getArgs().size() - (method == "reduce" ? 1 : 0);
    llvm::SmallVector<mlir::Attribute> spans;
    for (mlir::Value capture : call.getArgs().take_front(captures)) {
        spans.push_back(
            b.getStringAttr(isElementArray(capture, lower.typeOf(capture), lower.pageViews)
                                ? elementSpanSpelling(lower.typeOf(capture))
                                : std::string{}));
    }
    call->setAttr(kMethod, b.getStringAttr(method));
    call->setAttr(kSpans, b.getArrayAttr(spans));
}

bool replaceArrayCallback(lowering & lower, mlir::Operation * o) {
    auto call = llvm::cast<ctjs::CallOp>(o);
    mlir::MLIRContext * context = o->getContext();
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    const llvm::StringRef method = o->getAttrOfType<mlir::StringAttr>(kMethod).getValue();
    auto spans = o->getAttrOfType<mlir::ArrayAttr>(kSpans).getAsValueRange<mlir::StringAttr>();
    const llvm::StringRef symbol =
        o->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeArrayCallback).getValue();
    const auto named = lower.names.find(symbol);
    const std::string callback = named == lower.names.end() ? cIdentifier(symbol) : named->second;
    const auto any = ec::OpaqueType::get(context, "auto");
    mlir::Value bound = ec::LiteralOp::create(b, where, any, callback);
    llvm::SmallVector<mlir::Value> captured{bound};
    // Captures are operands 2 onwards; the lift lists those passed by address.
    unsigned operand = 2;
    for (auto [capture, span] : llvm::zip(call.getArgs(), spans)) {
        if (listsSlot(o, kNativeCellArgs, operand++)) {
            // The variable in this frame, or the pointer this frame was given.
            captured.push_back(
                llvm::isa<ec::PointerType>(capture.getType())
                    ? capture
                    : ec::AddressOfOp::create(
                          b, where,
                          ec::PointerType::get(
                              llvm::cast<ec::LValueType>(capture.getType()).getValueType()),
                          capture)
                          .getResult());
            continue;
        }
        captured.push_back(span.empty() ? capture
                                        : callValue(b, where, ec::OpaqueType::get(context, span),
                                                    span, lowering::cellPlace(b, where, capture)));
    }
    if (captured.size() > 1) { bound = callValue(b, where, any, "std::bind_front", captured); }
    const mlir::Type result = o->getResult(0).getType();
    llvm::SmallVector<mlir::Value, 3> operands{lowering::cellPlace(b, where, call.getReceiver()),
                                               bound};
    if (method == "reduce") {
        operands.push_back(lower.convertScalar(b, where, call.getArgs().back(), result));
    }
    const std::string name = ("ctnative::Array.prototype." + method + ".call").str();
    if (o->getResult(0).use_empty() || method == "sort") {
        // An unused answer, or `a.sort(f)`, which sorts `a` in place and
        // answers it.
        callWithConstValueOperands(b, where, mlir::TypeRange{}, b.getStringAttr(name), operands);
        o->getResult(0).replaceAllUsesWith(operands[0]);
        lowering::eraseIfUnused(o);
        return true;
    }
    mlir::Value answer;
    if (method == "filter" || method == "map") {
        // A new array, owned by a local as a split's pieces are.
        auto lvalue = llvm::dyn_cast<ec::LValueType>(result);
        answer = ec::VariableOp::create(b, where, result, ec::OpaqueAttr::get(context, ""));
        ec::AssignOp::create(
            b, where, answer,
            callValue(b, where, lvalue ? lvalue.getValueType() : result, name, operands));
    } else {
        answer = callValue(b, where, result, name, operands);
    }
    o->getResult(0).replaceAllUsesWith(answer);
    lowering::eraseIfUnused(o);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
