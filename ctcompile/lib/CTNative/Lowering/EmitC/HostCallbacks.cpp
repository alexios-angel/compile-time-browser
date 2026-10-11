//===- HostCallbacks.cpp - Emission of callbacks handed to the host -------===//
//
// Spells an `el.addEventListener(type, f)` or `setTimeout(f, delay)` call
// closure lifting specialised for its callback with its JavaScript name and
// the lifted callback bound to copies of its captures:
//
//   ctnative::EventTarget.prototype.addEventListener.call(el, type,
//                                                         std::bind_front(listener, label))
//   ctnative::setTimeout(std::bind_front(handler, label), delay)
//
// or the bare callback when it captures nothing. The runtime's listener table
// (Runtime/Events.hpp) or timer table (Runtime/Timers.hpp) owns the bound
// callable from then on.
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
    if (call.getArgs().size() > 1) {
        llvm::SmallVector<mlir::Value> captured{bound};
        // A shared cell is bound as its handle (rule 2b in ClosureLifter.h).
        for (mlir::Value capture : call.getArgs().drop_front()) {
            captured.push_back(loadedHandle(b, where, capture));
        }
        bound = callValue(b, where, any, "std::bind_front", captured);
    }
    if (TypeInference::isTimerCall(call)) {
        const mlir::Value operands[] = {bound, call.getArgs().front()};
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::setTimeout"), operands);
    } else {
        const mlir::Value operands[] = {lowering::cellPlace(b, where, call.getReceiver()),
                                        call.getArgs().front(), bound};
        callWithConstValueOperands(
            b, where, mlir::TypeRange{},
            b.getStringAttr("ctnative::EventTarget.prototype.addEventListener.call"), operands);
    }
    lowering::eraseIfUnused(o);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
