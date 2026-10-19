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
    if (!call) { return false; }
    if (call->hasAttr(kNativeHostTimer)) { return true; }
    mlir::Operation * callee = call.getCallee().getDefiningOp();
    return isRealmGlobal(callee, "setTimeout") || isRealmGlobal(callee, "setInterval");
}

bool TypeInference::isTimerClear(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    if (!call || call.getArgs().size() != 1) { return false; }
    mlir::Operation * callee = call.getCallee().getDefiningOp();
    return isRealmGlobal(callee, "clearTimeout") || isRealmGlobal(callee, "clearInterval");
}

/// The one list of uses of the global object that leave the globals table
/// alone. Initialization.cpp treats any other load of `window`, `self` or
/// `globalThis` as a possible dynamic write to every global. Each entry is
/// safe because the host defines the member and the program never writes
/// through it:
/// - `.devicePixelRatio`: a Number read (TypeInference::domReadType);
/// - `.addEventListener` or `.removeEventListener` read only to call it with a
///   listener closure lifting specialised (isHostCallbackCall), and the global
///   object as that call's receiver: the runtime's listener table keeps or
///   drops the listener, and no property of the window is read or written.
/// Other lanes add their inert host reads here.
bool TypeInference::isInertWindowLoad(mlir::Operation * op) {
    auto load = llvm::dyn_cast_or_null<ctjs::LoadGlobalOp>(op);
    if (!load || (load.getName() != "window" && load.getName() != "self" &&
                  load.getName() != "globalThis")) {
        return false;
    }
    // Only the listed uses are inert: a load with no use at all is still a
    // read of the global object, which keeps the globals dynamic.
    if (load.getResult().use_empty()) { return false; }
    return llvm::all_of(load.getResult().getUses(), [&](mlir::OpOperand & use) {
        mlir::Operation * user = use.getOwner();
        if (use.getOperandNumber() == 1 && isHostCallbackCall(user)) { return true; }
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
        if (!read || use.getOperandNumber() != 0) { return false; }
        const llvm::StringRef key = ctjs::constantKey(read.getKey());
        if (key == "devicePixelRatio") { return true; }
        if ((key != "addEventListener" && key != "removeEventListener") ||
            !read.getResult().hasOneUse()) {
            return false;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(*read.getResult().getUsers().begin());
        return call && isHostCallbackCall(call) && call.getCallee() == read.getResult() &&
               call.getReceiver() == load.getResult();
    });
}

bool TypeInference::isEventConstruct(mlir::Operation * op) {
    auto construct = llvm::dyn_cast_or_null<ctjs::ConstructOp>(op);
    return construct && (construct.getArgs().size() == 1 || construct.getArgs().size() == 2) &&
           isRealmGlobal(construct.getCallee().getDefiningOp(), "Event") &&
           construct.getNewTarget() == construct.getCallee() && eventInitFields(op);
}

std::optional<llvm::SmallVector<std::pair<llvm::StringRef, mlir::Value>>> TypeInference::
    eventInitFields(mlir::Operation * op) {
    auto construct = llvm::cast<ctjs::ConstructOp>(op);
    llvm::SmallVector<std::pair<llvm::StringRef, mlir::Value>> fields;
    if (construct.getArgs().size() < 2) { return fields; }
    auto literal = construct.getArgs()[1].getDefiningOp<ctjs::CreateObjectOp>();
    if (!literal) { return std::nullopt; }
    for (mlir::OpOperand & use : literal.getResult().getUses()) {
        if (use.getOwner() == op) {
            // Only as the init, never also as the type.
            if (use.getOperandNumber() != 3) { return std::nullopt; }
            continue;
        }
        auto define = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
        if (!define || use.getOperandNumber() != 0 || !define->hasAttr("ctjs.define_own")) {
            return std::nullopt;
        }
        const llvm::StringRef key = ctjs::constantKey(define.getKey());
        if (key != "bubbles" && key != "cancelable") { return std::nullopt; }
        fields.emplace_back(key, define.getValue());
    }
    return fields;
}

bool TypeInference::isEventInitPart(mlir::Operation * op) {
    mlir::Value literal;
    if (llvm::isa_and_nonnull<ctjs::CreateObjectOp>(op)) {
        literal = op->getResult(0);
    } else if (auto define = llvm::dyn_cast_or_null<ctjs::SetPropertyOp>(op)) {
        literal = define.getObject();
    }
    if (!literal || !literal.getDefiningOp<ctjs::CreateObjectOp>()) { return false; }
    return llvm::any_of(literal.getUsers(), [&](mlir::Operation * user) {
        auto construct = llvm::dyn_cast<ctjs::ConstructOp>(user);
        return construct && construct.getArgs().size() == 2 && construct.getArgs()[1] == literal &&
               isEventConstruct(construct);
    });
}

bool TypeInference::isDispatchEventCall(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    if (!call || call.getArgs().size() != 1 || ctjs::methodKey(call) != "dispatchEvent") {
        return false;
    }
    auto read = ctjs::receiverMethod(call);
    return read && read.getObject() == call.getReceiver() && read.getResult().hasOneUse();
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
    // new Event(type, init) is an Event; el.dispatchEvent(e) answers whether
    // no listener canceled it.
    if (isEventConstruct(op)) { return EventType::get(c); }
    if (isDispatchEventCall(op) && event(llvm::cast<ctjs::CallOp>(op).getArgs()[0])) {
        return BoolType::get(c);
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op); read && event(read.getObject())) {
        const llvm::StringRef key = ctjs::constantKey(read.getKey());
        if (key == "type") { return defaultStringType(c); }
        if (key == "target" || key == "currentTarget") { return DOMElementType::get(c); }
        if (key == "defaultPrevented") { return BoolType::get(c); }
        return {};
    }
    if (isTimerClear(op)) { return OptType::get(c, BottomType::get(c)); }
    if (!isHostCallbackCall(op)) { return {}; }
    auto listener = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
        op, op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostCallback));
    if (!listener || listener.getBody().empty()) { return {}; }
    mlir::Block & entry = listener.getBody().front();
    // A removal passes no captures and calls nothing.
    if (op->hasAttr(kNativeHostRemove)) { return OptType::get(c, BottomType::get(c)); }
    // Operands are the callee, the receiver, the event type or the delay, a
    // listener's identity, then the captures, which the callback takes after
    // its implicit arguments, then a listener's capture flag.
    const unsigned begin = op->hasAttr(kNativeHostIdentified) ? 4 : 3;
    const auto end =
        static_cast<unsigned>(operands.size()) - (op->hasAttr(kNativeHostCapture) ? 1 : 0);
    for (unsigned i = begin; i < end; ++i) {
        const unsigned index = ctjs::implicit_arguments + i - begin;
        const mlir::Type type = operands[i]->getValue().getType();
        if (!type || index >= entry.getNumArguments()) { continue; }
        TypeLattice * lattice = getLatticeElement(entry.getArgument(index));
        propagateIfChanged(lattice, lattice->join(TypeValue{type}));
    }
    // A listener's declared parameter, after its captures, is the Event.
    if (const unsigned index = ctjs::implicit_arguments + end - begin;
        !isTimerCall(op) && index < entry.getNumArguments()) {
        TypeLattice * lattice = getLatticeElement(entry.getArgument(index));
        propagateIfChanged(lattice, lattice->join(TypeValue{EventType::get(c)}));
    }
    // setTimeout and setInterval answer the timer's handle.
    if (isTimerCall(op)) { return TimerType::get(c); }
    return OptType::get(c, BottomType::get(c));
}

} // namespace ctcompile::ctnative
