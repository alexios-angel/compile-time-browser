//===- HostCallbacks.cpp - Emission of callbacks handed to the host -------===//
//
// Spells an `el.addEventListener(type, f)` or `setTimeout(f, delay)` call
// closure lifting specialised for its callback with its JavaScript name and
// the lifted callback bound to copies of its captures:
//
//   ctnative::EventTarget.prototype.addEventListener.call(el, type,
//                                                         std::bind_front(listener, label))
//   ctnative::EventTarget.prototype.addEventListener.call(ctnative::document, type,
//                                                         listener, capture)
//   ctnative::setTimeout(std::bind_front(handler, label), delay)
//
// or the bare callback when it captures nothing. The runtime's listener table
// (Runtime/Events.hpp) or timer table (Runtime/Timers.hpp) owns the bound
// callable from then on.
//
// A listener's Event is the runtime's js_event: `e.type`, `e.target`,
// `e.currentTarget` and `e.defaultPrevented` are its members of those names,
// and `e.preventDefault()` is `ctnative::Event.prototype.preventDefault.call(e)`.
//
//===----------------------------------------------------------------------===//

#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

bool replaceHostCallback(lowering & lower, mlir::Operation * o) {
    auto call = llvm::cast<ctjs::CallOp>(o);
    mlir::MLIRContext * context = o->getContext();
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    const llvm::StringRef symbol =
        o->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostCallback).getValue();
    const auto named = lower.names.find(symbol);
    const std::string listener = named == lower.names.end() ? cIdentifier(symbol) : named->second;
    const auto any = ec::OpaqueType::get(context, "auto");
    mlir::Value bound = ec::LiteralOp::create(b, where, any, listener);
    // The captures, without a listener's capture flag.
    auto captures = call.getArgs().drop_front();
    const bool flagged = o->hasAttr(kNativeHostCapture);
    if (flagged) { captures = captures.drop_back(); }
    if (!captures.empty()) {
        llvm::SmallVector<mlir::Value> captured{bound};
        llvm::append_range(captured, captures);
        bound = callValue(b, where, any, "std::bind_front", captured);
    }
    if (TypeInference::isTimerCall(call)) {
        const mlir::Value operands[] = {bound, call.getArgs().front()};
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::setTimeout"), operands);
    } else {
        // The realm's document or window by its runtime name, or the element.
        const auto host = o->getAttrOfType<mlir::StringAttr>(kNativeHostTarget);
        llvm::SmallVector<mlir::Value> operands{
            host ? ec::LiteralOp::create(b, where, any, ("ctnative::" + host.getValue()).str())
                 : lowering::cellPlace(b, where, call.getReceiver()),
            call.getArgs().front(), bound};
        if (flagged) { operands.push_back(call.getArgs().back()); }
        callWithConstValueOperands(
            b, where, mlir::TypeRange{},
            b.getStringAttr("ctnative::EventTarget.prototype.addEventListener.call"), operands);
    }
    lowering::eraseIfUnused(o);
    return true;
}

std::optional<std::string> eventAction(lowering & lower, mlir::Operation * o) {
    const auto event = [&](mlir::Value v) {
        return carrierOf(lower.typeOf(v)) == carrier::domEvent;
    };
    if (TypeInference::isEventCall(o) && event(llvm::cast<ctjs::CallOp>(o).getReceiver())) {
        // The names are recorded now: by replacement time the keys are
        // emitc constants.
        return ("event_call:" + ctjs::methodKey(llvm::cast<ctjs::CallOp>(o))).str();
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(o); read && event(read.getObject())) {
        return ("event:" + ctjs::constantKey(read.getKey())).str();
    }
    return std::nullopt;
}

bool replaceEvent(mlir::Operation * o, llvm::StringRef kind) {
    mlir::MLIRContext * context = o->getContext();
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    if (llvm::StringRef method = kind; method.consume_front("event_call:")) {
        const mlir::Value operands[] = {llvm::cast<ctjs::CallOp>(o).getReceiver()};
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::Event.prototype." + method + ".call"),
                                   operands);
        lowering::eraseIfUnused(o);
        return true;
    }
    llvm::StringRef member = kind;
    if (!member.consume_front("event:")) { return false; }
    const mlir::Type result = member == "type" ? carrierType(context, carrier::string)
                              : member == "defaultPrevented"
                                  ? carrierType(context, carrier::boolean)
                                  : ec::OpaqueType::get(context, kPageElementType);
    o->getResult(0).replaceAllUsesWith(memberCall(b, where, result, o->getOperand(0), member));
    lowering::eraseIfUnused(o);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
