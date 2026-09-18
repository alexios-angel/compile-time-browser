#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {

llvm::Expected<LocalCallableObject> analyzeLocalCallableObject(ctjs::CreateObjectOp object,
                                                               llvm::function_ref<bool()> spend) {
    const auto error = [](llvm::StringRef message) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), message);
    };
    LocalCallableObject result;
    llvm::DenseMap<mlir::Attribute, ctjs::SetPropertyOp> slots;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
    const auto key = [](mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())
                        : ctjs::StringAttr{};
    };
    // Callers prove standard Object.prototype and exclude prototype mutation,
    // unknown calls and script reentry. Only __proto__ has an inherited setter.
    for (mlir::OpOperand & use : object.getResult().getUses()) {
        if (!spend()) { return error("DOM helper work budget exhausted"); }
        auto * operation = use.getOwner();
        if (operation->getBlock() != object->getBlock() || !object->isBeforeInBlock(operation)) {
            return error("DOM helper object has nonlocal or unordered uses");
        }
        if (llvm::isa<ctjs::RootOp>(operation)) { continue; }
        if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            auto name = key(store.getKey());
            auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (use.getOperandNumber() != 0 || !name || name.getValue() == "__proto__" ||
                !closure || closure->getBlock() != object->getBlock() ||
                !closure->isBeforeInBlock(store) || !slots.try_emplace(name, store).second) {
                return error("DOM helper object requires unique own callable slots");
            }
        } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                   read && use.getOperandNumber() == 0) {
            reads.push_back(read);
        } else {
            auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
            auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
            mlir::Value callee;
            if (call && use.getOperandNumber() == 1) { callee = call.getCallee(); }
            if (direct && use.getOperandNumber() == 0) { callee = direct.getCalleeValue(); }
            auto methodRead =
                callee ? callee.getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
            if (!methodRead || methodRead.getObject() != object.getResult() ||
                methodRead->getBlock() != object->getBlock() ||
                !methodRead->isBeforeInBlock(operation)) {
                return error("DOM helper object escapes or observes its identity");
            }
            result.calls.push_back(operation);
        }
    }
    if (slots.empty()) { return error("DOM helper object has no callable slots"); }
    for (ctjs::GetPropertyOp read : reads) {
        if (!spend()) { return error("DOM helper work budget exhausted"); }
        auto name = key(read.getKey());
        auto store = name ? slots.lookup(name) : ctjs::SetPropertyOp{};
        if (!store || !store->isBeforeInBlock(read)) {
            return error("DOM helper object read lacks a preceding own callable slot");
        }
        result.reads.emplace_back(read, store.getValue().getDefiningOp<ctjs::CreateClosureOp>());
    }
    for (auto [name, store] : slots) {
        (void)name;
        result.stores.push_back(store);
    }
    return result;
}

bool directCalleeUse(mlir::OpOperand & use, ctjs::FuncOp target) {
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    if (!direct || use.getOperandNumber() != 2) { return false; }
    if (direct.getCallee() == target.getSymName()) { return true; }
    // A specialized native target retains its original callee value for the
    // boxed dispatcher. That value does not escape merely by reaching an
    // unused implicit slot of another private body. Recheck non-observation
    // structurally; provenance annotations cannot authorize this exception.
    auto body = direct.getTarget();
    if (!body || body.getBody().empty() || body.getUpvalueCount() != 0 ||
        mlir::SymbolTable::getSymbolVisibility(body) != mlir::SymbolTable::Visibility::Private) {
        return false;
    }
    auto & entry = body.getBody().front();
    if (entry.getNumArguments() < 3 || direct->getNumOperands() != entry.getNumArguments()) {
        return false;
    }
    for (mlir::Operation * user : entry.getArgument(2).getUsers()) {
        if (!llvm::isa<ctjs::RootOp>(user)) { return false; }
    }
    bool environment = false;
    body.getBody().walk([&](mlir::Operation * op) {
        environment |= llvm::isa<ctjs::CreateClosureOp, ctjs::CreateCellOp, ctjs::LoadUpvalueOp,
                                 ctjs::StoreUpvalueOp, ctjs::CellGetOp, ctjs::CellSetOp>(op);
    });
    return !environment;
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
