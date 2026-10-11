//===- HostCallbacks.cpp - Emission of listeners handed to the host -------===//
//
// Spells an `el.addEventListener(type, f)` call closure lifting specialised for
// its listener with its JavaScript name and the lifted listener bound to
// copies of its captures:
//
//   ctnative::EventTarget.prototype.addEventListener.call(el, type,
//                                                         std::bind_front(listener, label))
//
// or the bare listener when it captures nothing. The runtime's listener table
// (Runtime/Events.hpp) owns the bound callable from then on.
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
        llvm::append_range(captured, call.getArgs().drop_front());
        bound = callValue(b, where, any, "std::bind_front", captured);
    }
    const mlir::Value operands[] = {lowering::cellPlace(b, where, call.getReceiver()),
                                    call.getArgs().front(), bound};
    callWithConstValueOperands(
        b, where, mlir::TypeRange{},
        b.getStringAttr("ctnative::EventTarget.prototype.addEventListener.call"), operands);
    lowering::eraseIfUnused(o);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
