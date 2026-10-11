//===- HostCallbacks.cpp - Types of listeners handed to the host ----------===//
//
// Closure lifting (Lowering/ClosureLifting/HostCallbacks.cpp) specialises the
// listener of `el.addEventListener(type, f)` for its one call: the call names
// the listener by `ctnative.host_callback` and passes its captured values
// after the event type. No ctjs.call_direct reaches the listener, so its
// parameters (only captures: a native listener declares none) are typed here
// from the call's operands, the way a direct call types its callee's.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {

bool TypeInference::isHostCallbackCall(mlir::Operation * op) {
    return llvm::isa_and_nonnull<ctjs::CallOp>(op) &&
           op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostCallback);
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

mlir::Type TypeInference::hostCallbackType(mlir::Operation * op,
                                           llvm::ArrayRef<const TypeLattice *> operands) {
    if (!isHostCallbackCall(op)) { return {}; }
    auto listener = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
        op, op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostCallback));
    if (!listener || listener.getBody().empty()) { return {}; }
    mlir::Block & entry = listener.getBody().front();
    // Operands are the callee, the receiver, the event type, then the
    // captures, which the listener takes after its implicit arguments.
    for (unsigned i = 3; i < operands.size(); ++i) {
        const unsigned index = ctjs::implicit_arguments + i - 3;
        const mlir::Type type = operands[i]->getValue().getType();
        if (!type || index >= entry.getNumArguments()) { continue; }
        TypeLattice * lattice = getLatticeElement(entry.getArgument(index));
        propagateIfChanged(lattice, lattice->join(TypeValue{type}));
    }
    mlir::MLIRContext * c = op->getContext();
    return OptType::get(c, BottomType::get(c));
}

} // namespace ctcompile::ctnative
