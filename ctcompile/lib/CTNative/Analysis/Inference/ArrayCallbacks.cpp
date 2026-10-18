//===- ArrayCallbacks.cpp - Types of specialised Array-method callbacks ---===//
//
// Closure lifting (Lowering/ClosureLifting/ArrayCallbacks.cpp) specialises a
// closure passed to forEach, map, filter, some, every, find, reduce or sort
// for its one call: the call names the callback by `ctnative.array_callback`
// and passes the callback's captured values in place of the closure. No
// ctjs.call_direct reaches the callback, so its parameters are typed here, by
// the call, the way a direct call types its callee's: captured values from the
// call's operands, then (reduce's accumulator,) the receiver's element, the
// index (a Number) and the receiver itself; a sort comparator is passed two
// elements.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/StringSwitch.h"

namespace ctcompile::ctnative {

namespace {

/// The callback a specialised call names, or null.
ctjs::FuncOp callbackOf(ctjs::CallOp call) {
    auto name = call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeArrayCallback);
    return name ? mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, name)
                : ctjs::FuncOp{};
}

/// How many of a specialised call's arguments are the callback's captures:
/// all of them, except reduce's initial value after them.
unsigned capturesOf(ctjs::CallOp call) {
    const auto arguments = static_cast<unsigned>(call.getArgs().size());
    return ctjs::methodKey(call) == "reduce" ? arguments - 1 : arguments;
}

} // namespace

bool TypeInference::isArrayCallbackMethod(llvm::StringRef key) {
    return llvm::StringSwitch<bool>(key)
        .Cases({"forEach", "map", "filter", "some", "every", "find", "reduce", "sort"}, true)
        .Default(false);
}

bool TypeInference::isArrayCallbackCall(mlir::Operation * op) {
    return llvm::isa_and_nonnull<ctjs::CallOp>(op) &&
           op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeArrayCallback);
}

bool TypeInference::arrayCallbackMakesArray(mlir::Operation * op) {
    if (!isArrayCallbackCall(op)) { return false; }
    const llvm::StringRef method = ctjs::methodKey(llvm::cast<ctjs::CallOp>(op));
    return method == "filter" || method == "map";
}

bool TypeInference::arrayCallbackSorts(mlir::Operation * op) {
    return isArrayCallbackCall(op) && ctjs::methodKey(llvm::cast<ctjs::CallOp>(op)) == "sort";
}

bool TypeInference::arrayCallbackBorrows(mlir::Operation * op, unsigned index) {
    if (!isArrayCallbackCall(op)) { return false; }
    // Operand 1 is the receiver; the captures follow the callee and receiver.
    return index == 1 || (index >= 2 && index < 2 + capturesOf(llvm::cast<ctjs::CallOp>(op)));
}

bool TypeInference::isNumberElementVector(mlir::Value array) {
    const auto members = elementVectorAliases(array);
    return !members.empty() && llvm::none_of(members, [](mlir::Value member) {
        return member.getDefiningOp<ctjs::CreateArrayOp>() != nullptr;
    });
}

bool TypeInference::isArrayCallbackParameter(mlir::Value value) {
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
    if (!argument || !argument.getOwner()->isEntryBlock() ||
        argument.getArgNumber() < ctjs::implicit_arguments) {
        return false;
    }
    auto function = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
    return function && function->hasAttr(kNativeArrayCallback);
}

mlir::Type TypeInference::arrayCallbackType(mlir::Operation * op,
                                            llvm::ArrayRef<const TypeLattice *> operands,
                                            bool & pending) {
    mlir::MLIRContext * c = op->getContext();
    auto call = llvm::cast<ctjs::CallOp>(op);
    ctjs::FuncOp callback = callbackOf(call);
    if (!callback || callback.getBody().empty()) { return {}; }
    mlir::Block & entry = callback.getBody().front();
    const llvm::StringRef method = ctjs::methodKey(call);
    const unsigned captures = capturesOf(call);
    const auto typeOf = [](const TypeLattice * lattice) { return lattice->getValue().getType(); };
    // Joins `type` into the callback's entry argument `index`, as a call joins
    // an operand into its callee's parameter. A parameter the callback does
    // not declare is not passed.
    const auto pass = [&](unsigned index, mlir::Type type) {
        if (!type || index >= entry.getNumArguments()) { return; }
        TypeLattice * lattice = getLatticeElement(entry.getArgument(index));
        propagateIfChanged(lattice, lattice->join(TypeValue{type}));
    };
    // Operands 0 and 1 are the callee and the receiver.
    for (unsigned i = 0; i < captures; ++i) {
        pass(ctjs::implicit_arguments + i, typeOf(operands[2 + i]));
    }
    unsigned next = ctjs::implicit_arguments + captures;
    const mlir::Type receiver = typeOf(operands[1]);
    if (!receiver) {
        pending = true;
        return {};
    }
    auto vector = llvm::dyn_cast<VecType>(receiver);
    if (!vector) {
        // Not an array the runtime iterates. Admission refuses the call, and
        // the callback's parameters are unknown.
        for (unsigned i = next; i < entry.getNumArguments(); ++i) { pass(i, BoxedType::get(c)); }
        return {};
    }
    if (method == "sort") {
        // The comparator is asked about two elements, and the call answers
        // the receiver itself, sorted in place.
        pass(next, vector.getElementType());
        pass(next + 1, vector.getElementType());
        return vector;
    }
    llvm::SmallVector<mlir::Value> returned;
    callback.walk([&](ctjs::ReturnOp ret) { returned.push_back(ret.getValue()); });
    const mlir::Type result = joinLattices(op, returned, mlir::Type{});
    mlir::Type accumulator;
    if (method == "reduce") {
        // The accumulator holds the initial value, then each callback result.
        accumulator = meet(typeOf(operands[2 + captures]), result);
        pass(next++, accumulator);
    }
    pass(next, vector.getElementType());
    pass(next + 1, NumType::get(c, NumKind::F64));
    pass(next + 2, vector);
    const mlir::Type undefined = OptType::get(c, BottomType::get(c));
    if (method == "forEach") { return undefined; }
    if (method == "filter") { return vector; }
    if (method == "some" || method == "every") { return BoolType::get(c); }
    // find answers undefined when no element matched.
    if (method == "find") { return meet(undefined, vector.getElementType()); }
    const mlir::Type answer = method == "map"
                                  ? (result ? mlir::Type(VecType::get(c, result)) : mlir::Type{})
                                  : accumulator;
    pending |= answer == nullptr;
    return answer;
}

} // namespace ctcompile::ctnative
