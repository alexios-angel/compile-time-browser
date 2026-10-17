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

namespace {
/// The attribute Vectors.cpp marks each page operation's action in.
constexpr llvm::StringLiteral kAction = "ctnative.string_array";
} // namespace

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
    // The captures, without a listener's identity and capture flag.
    auto captures = call.getArgs().drop_front();
    const bool identified = o->hasAttr(kNativeHostIdentified);
    const bool removal = o->hasAttr(kNativeHostRemove);
    const bool flagged = o->hasAttr(kNativeHostCapture);
    if (identified) { captures = captures.drop_front(); }
    if (flagged) { captures = captures.drop_back(); }
    if (!captures.empty()) {
        llvm::SmallVector<mlir::Value> captured{bound};
        llvm::append_range(captured, captures);
        bound = callValue(b, where, any, "std::bind_front", captured);
    }
    // A removal names the listener by its identity; an addition of a listener
    // that has one carries it.
    if (removal) {
        bound = call.getArgs()[1];
    } else if (identified) {
        const mlir::Value made[] = {call.getArgs()[1], bound};
        bound = callValue(b, where, any, "ctnative::identified_listener", made);
    }
    if (TypeInference::isTimerCall(call)) {
        const mlir::Value operands[] = {bound, call.getArgs().front()};
        const std::string function =
            ("ctnative::" + o->getAttrOfType<mlir::StringAttr>(kNativeHostTimer).getValue()).str();
        if (call.getResult().use_empty()) {
            callWithConstValueOperands(b, where, mlir::TypeRange{}, b.getStringAttr(function),
                                       operands);
        } else {
            // The handle, kept to clear the timer.
            call.getResult().replaceAllUsesWith(
                callValue(b, where, carrierType(context, carrier::timer), function, operands));
        }
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
            b.getStringAttr(removal ? "ctnative::EventTarget.prototype.removeEventListener.call"
                                    : "ctnative::EventTarget.prototype.addEventListener.call"),
            operands);
    }
    lowering::eraseIfUnused(o);
    return true;
}

std::optional<std::string> eventAction(lowering & lower, mlir::Operation * o) {
    // clearTimeout(handle), with its name recorded now: by replacement time
    // the load that names it is gone. A handle's truthiness is whether it
    // names a timer.
    if (TypeInference::isTimerClear(o)) {
        auto call = llvm::cast<ctjs::CallOp>(o);
        // Clearing with null or undefined does nothing, so it prints nothing.
        if (carrierOf(lower.typeOf(call.getArgs()[0])) != carrier::timer) {
            return std::string{"timer_clear_nothing"};
        }
        auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
        return ("timer_clear:" + load.getName()).str();
    }
    if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(o);
        truthy && carrierOf(lower.typeOf(truthy.getValue())) == carrier::timer) {
        return std::string{"timer_truthy"};
    }
    // The closure of a listener that is removed or added twice is its
    // identity.
    if (o->hasAttr(kNativeHostIdentity)) { return std::string{"host_identity"}; }
    if (auto declared = o->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostDeclared)) {
        return ("host_declared:" + declared.getValue()).str();
    }
    const auto event = [&](mlir::Value v) {
        return carrierOf(lower.typeOf(v)) == carrier::domEvent;
    };
    // The names are recorded now: by replacement time the keys are emitc
    // constants.
    if (TypeInference::isEventInitPart(o)) {
        auto define = llvm::dyn_cast<ctjs::SetPropertyOp>(o);
        return define ? ("event_init:" + ctjs::constantKey(define.getKey())).str()
                      : std::string{"event_init_object"};
    }
    if (TypeInference::isEventConstruct(o)) { return std::string{"event_new"}; }
    if (TypeInference::isDispatchEventCall(o) && event(llvm::cast<ctjs::CallOp>(o).getArgs()[0])) {
        return std::string{"event_dispatch"};
    }
    if (TypeInference::isEventCall(o) && event(llvm::cast<ctjs::CallOp>(o).getReceiver())) {
        return ("event_call:" + ctjs::methodKey(llvm::cast<ctjs::CallOp>(o))).str();
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(o); read && event(read.getObject())) {
        return ("event:" + ctjs::constantKey(read.getKey())).str();
    }
    return std::nullopt;
}

bool replaceEvent(lowering & lower, mlir::Operation * o, llvm::StringRef kind) {
    mlir::MLIRContext * context = o->getContext();
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    if (llvm::StringRef function = kind; function.consume_front("timer_clear")) {
        if (function.consume_front(":")) {
            const mlir::Value operands[] = {llvm::cast<ctjs::CallOp>(o).getArgs()[0]};
            callWithConstValueOperands(b, where, mlir::TypeRange{},
                                       b.getStringAttr(("ctnative::" + function).str()), operands);
        }
        lowering::eraseIfUnused(o);
        return true;
    }
    if (kind == "timer_truthy") {
        mlir::Value set = memberCall(b, where, b.getI1Type(), o->getOperand(0), "is_set");
        if (isBooleanCarrier(o->getResult(0).getType())) {
            set = lower.convertScalar(b, where, set, o->getResult(0).getType());
        }
        o->getResult(0).replaceAllUsesWith(set);
        lowering::eraseIfUnused(o);
        return true;
    }
    // An init literal and its definitions stay until their construct, which
    // comes after them, reads and erases them.
    if (kind == "event_init_object" || kind.starts_with("event_init:")) { return true; }
    // A top-level declaration's value: one identity per declaration.
    if (llvm::StringRef symbol = kind; symbol.consume_front("host_declared:")) {
        const auto named = lower.names.find(symbol);
        const std::string function =
            named == lower.names.end() ? cIdentifier(symbol) : named->second;
        o->getResult(0).replaceAllUsesWith(
            callValue(b, where, ec::OpaqueType::get(context, "ctnative::callable_identity"),
                      "ctnative::declared_identity<&" + function + ">", mlir::ValueRange{}));
        lowering::eraseIfUnused(o);
        return true;
    }
    if (kind == "host_identity") {
        o->getResult(0).replaceAllUsesWith(
            callValue(b, where, ec::OpaqueType::get(context, "ctnative::callable_identity"),
                      "ctnative::new_callable_identity", mlir::ValueRange{}));
        lowering::eraseIfUnused(o);
        return true;
    }
    if (kind == "event_new") {
        auto construct = llvm::cast<ctjs::ConstructOp>(o);
        const auto boolean = carrierType(context, carrier::boolean);
        mlir::Value fields[] = {
            ec::LiteralOp::create(b, where, boolean, "ctnative::js_boolean_t{}"),
            ec::LiteralOp::create(b, where, boolean, "ctnative::js_boolean_t{}")};
        llvm::SmallVector<mlir::Operation *> parts;
        if (construct.getArgs().size() == 2) {
            mlir::Operation * literal = construct.getArgs()[1].getDefiningOp();
            for (mlir::Operation * user : literal->getUsers()) {
                auto action = user->getAttrOfType<mlir::StringAttr>(kAction);
                if (!action || user == o) { continue; }
                // Program order: the last definition of a key is the value.
                fields[action.getValue() == "event_init:bubbles" ? 0 : 1] = user->getOperand(2);
                parts.push_back(user);
            }
            parts.push_back(literal);
        }
        const mlir::Value made[] = {construct.getArgs()[0],
                                    callValue(b, where,
                                              ec::OpaqueType::get(context, "ctnative::event_init"),
                                              "ctnative::event_init", fields)};
        o->getResult(0).replaceAllUsesWith(
            callValue(b, where, carrierType(context, carrier::domEvent), "ctnative::Event", made));
        lowering::eraseIfUnused(o);
        for (mlir::Operation * part : parts) { part->erase(); }
        return true;
    }
    if (kind == "event_dispatch") {
        auto call = llvm::cast<ctjs::CallOp>(o);
        const mlir::Value operands[] = {lowering::cellPlace(b, where, call.getReceiver()),
                                        call.getArgs()[0]};
        o->getResult(0).replaceAllUsesWith(
            callValue(b, where, carrierType(context, carrier::boolean),
                      "ctnative::EventTarget.prototype.dispatchEvent.call", operands));
        lowering::eraseIfUnused(o);
        return true;
    }
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
