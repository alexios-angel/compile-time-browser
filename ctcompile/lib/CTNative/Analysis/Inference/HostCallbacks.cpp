//===- HostCallbacks.cpp - Types of callbacks handed to the host ----------===//
//
// Closure lifting (Lowering/ClosureLifting/HostCallbacks.cpp) specialises the
// callback of `el.addEventListener(type, f)` or `setTimeout(f, delay)` for its
// one call: the call names the callback by `ctnative.host_callback` and passes
// its captured values after the event type or the delay. No ctjs.call_direct
// reaches the callback, so its parameters are typed here from the call's
// operands, the way a direct call types its callee's: the captures, then a
// listener's one declared parameter, the Event (`!ctnative.event`). The reads
// and calls a listener makes on that Event are typed here too.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "ctcompile/CTNative/IR/CTNativeLattice.h"

#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {

bool TypeInference::isHostCallbackCall(mlir::Operation * op) {
    return llvm::isa_and_nonnull<ctjs::CallOp>(op) &&
           op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostCallback);
}

bool TypeInference::isTimerCall(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    return call && (call->hasAttr(kNativeHostTimer) ||
                    isRealmGlobal(call.getCallee().getDefiningOp(), "setTimeout"));
}

bool TypeInference::isHostCallbackParameter(mlir::Value value) {
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
    if (!argument || !argument.getOwner()->isEntryBlock() ||
        argument.getArgNumber() < ctjs::implicit_arguments) {
        return false;
    }
    auto function = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
    return function && function->hasAttr(kNativeHostCallback);
}

bool TypeInference::isEventCall(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    if (!call || !call.getArgs().empty()) { return false; }
    const llvm::StringRef key = ctjs::methodKey(call);
    if (key != "preventDefault" && key != "stopPropagation") { return false; }
    auto read = ctjs::receiverMethod(call);
    return read && read.getObject() == call.getReceiver() && read.getResult().hasOneUse();
}

mlir::Type TypeInference::hostCallbackType(mlir::Operation * op,
                                           llvm::ArrayRef<const TypeLattice *> operands) {
    mlir::MLIRContext * c = op->getContext();
    const auto event = [&](mlir::Value value) {
        return llvm::isa_and_nonnull<EventType>(joinLattices(op, value, mlir::Type{}));
    };
    // e.preventDefault() and e.stopPropagation() answer undefined; e.type is
    // a String, e.target and e.currentTarget elements, e.defaultPrevented a
    // Boolean (Runtime/Events.hpp's js_event).
    if (isEventCall(op) && event(llvm::cast<ctjs::CallOp>(op).getReceiver())) {
        return OptType::get(c, BottomType::get(c));
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op); read && event(read.getObject())) {
        const llvm::StringRef key = ctjs::constantKey(read.getKey());
        if (key == "type") { return defaultStringType(c); }
        if (key == "target" || key == "currentTarget") { return DOMElementType::get(c); }
        if (key == "defaultPrevented") { return BoolType::get(c); }
        return {};
    }
    if (!isHostCallbackCall(op)) { return {}; }
    auto listener = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
        op, op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostCallback));
    if (!listener || listener.getBody().empty()) { return {}; }
    mlir::Block & entry = listener.getBody().front();
    // Operands are the callee, the receiver, the event type or the delay,
    // then the captures, which the callback takes after its implicit
    // arguments.
    for (unsigned i = 3; i < operands.size(); ++i) {
        const unsigned index = ctjs::implicit_arguments + i - 3;
        const mlir::Type type = operands[i]->getValue().getType();
        if (!type || index >= entry.getNumArguments()) { continue; }
        TypeLattice * lattice = getLatticeElement(entry.getArgument(index));
        propagateIfChanged(lattice, lattice->join(TypeValue{type}));
    }
    // A listener's declared parameter, after its captures, is the Event.
    if (const unsigned index =
            ctjs::implicit_arguments + static_cast<unsigned>(operands.size()) - 3;
        !isTimerCall(op) && index < entry.getNumArguments()) {
        TypeLattice * lattice = getLatticeElement(entry.getArgument(index));
        propagateIfChanged(lattice, lattice->join(TypeValue{EventType::get(c)}));
    }
    // setTimeout answers the timer's handle.
    if (isTimerCall(op)) { return NumType::getDouble(c); }
    return OptType::get(c, BottomType::get(c));
}

} // namespace ctcompile::ctnative
