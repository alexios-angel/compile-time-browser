#include "ctcompile/CTNative/Analysis/ClosedCallable.h"

namespace ctcompile::ctnative {

// create_closure names a bytecode index, not an MLIR symbol use. Private
// visibility alone therefore does not establish that every call is visible.
// Recheck these value uses before replacing a parameterized function's body.
std::optional<unsigned> functionIndex(ctjs::FuncOp function) {
    const llvm::StringRef name = function.getSymName();
    const auto dollar = name.rfind('$');
    unsigned index = 0;
    if (dollar == llvm::StringRef::npos || name.substr(dollar + 1).getAsInteger(10, index)) {
        return {};
    }
    return index;
}

bool directCalleeUse(mlir::OpOperand & use, ctjs::FuncOp target) {
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    return direct && use.getOperandNumber() == 2 && direct.getCallee() == target.getSymName();
}

bool closedDeclaration(ctjs::StoreGlobalOp store, ctjs::FuncOp target, mlir::ModuleOp module) {
    auto parent = store->getParentOfType<ctjs::FuncOp>();
    if (!parent || functionIndex(parent) != 0 || parent.getBody().empty() ||
        store->getBlock() != &parent.getBody().front()) {
        return false;
    }
    for (mlir::Operation & before : *store->getBlock()) {
        if (&before == store.getOperation()) { break; }
        if (!llvm::isa<ctjs::FrameEnterOp, ctjs::ConstantOp, ctjs::CreateClosureOp,
                       ctjs::StoreGlobalOp, ctjs::RootOp>(before)) {
            return false;
        }
    }
    unsigned stores = 0;
    bool closed = true;
    module.walk([&](mlir::Operation * op) {
        if (auto other = llvm::dyn_cast<ctjs::StoreGlobalOp>(op);
            other && other.getName() == store.getName()) {
            ++stores;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
            load && load.getName() == store.getName()) {
            for (mlir::OpOperand & use : load.getResult().getUses()) {
                if (!llvm::isa<ctjs::RootOp>(use.getOwner()) && !directCalleeUse(use, target)) {
                    closed = false;
                }
            }
        }
    });
    return closed && stores == 1;
}

std::string closedCallableProblem(ctjs::FuncOp function, mlir::ModuleOp module) {
    const auto index = functionIndex(function);
    if (!index) { return {}; } // No numeric-index closure can name this symbol.
    std::string reason;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!reason.empty() || made.getFunction() < 0 ||
            static_cast<unsigned>(made.getFunction()) != *index) {
            return;
        }
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) || directCalleeUse(use, function)) {
                continue;
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(use.getOwner());
                store && closedDeclaration(store, function, module)) {
                continue;
            }
            reason = ("function closure escapes through `" +
                      use.getOwner()->getName().getStringRef() + "`")
                         .str();
            return;
        }
    });
    return reason;
}

} // namespace ctcompile::ctnative
