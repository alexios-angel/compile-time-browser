#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {

static llvm::Expected<CallableObject> analyzeCallableObject(ctjs::CreateObjectOp object,
                                                            ctjs::StoreGlobalOp publication,
                                                            llvm::function_ref<bool()> spend) {
    const auto error = [](llvm::StringRef message) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), message);
    };
    CallableObject result;
    if (publication) {
        auto entry = publication->getParentOfType<ctjs::FuncOp>();
        if (!entry || functionIndex(entry) != 0 || !llvm::hasSingleElement(entry.getBody()) ||
            publication->getBlock() != &entry.getBody().front() ||
            object->getBlock() != publication->getBlock() ||
            !object->isBeforeInBlock(publication)) {
            return error("global callable holder requires unconditional entry publication");
        }
        // ponytail: require an inert entry prefix; widen with a complete caller
        // graph if holders must be initialized after source calls. With no
        // reentry here, even transitive calls in other functions follow publication.
        for (mlir::Operation & op : entry.getBody().front()) {
            if (!spend()) { return error("DOM helper work budget exhausted"); }
            if (&op == publication.getOperation()) { break; }
            if (llvm::isa<ctjs::FrameEnterOp, ctjs::ConstantOp, ctjs::CreateClosureOp,
                          ctjs::StoreGlobalOp, ctjs::CreateObjectOp, ctjs::RootOp>(op)) {
                continue;
            }
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            auto made = store ? store.getObject().getDefiningOp<ctjs::CreateObjectOp>()
                              : ctjs::CreateObjectOp{};
            if (!made || made->getBlock() != publication->getBlock() ||
                !made->isBeforeInBlock(store) || !ctjs::ordinaryKey(store.getKey())) {
                return error("global callable holder publication follows a possible source call");
            }
        }
        unsigned publications = 0;
        auto walked =
            publication->getParentOfType<mlir::ModuleOp>().walk([&](mlir::Operation * op) {
                if (!spend()) { return mlir::WalkResult::interrupt(); }
                if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op);
                    store && store.getName() == publication.getName()) {
                    ++publications;
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
                    load && load.getName() == publication.getName()) {
                    result.loads.push_back(load);
                }
                return mlir::WalkResult::advance();
            });
        if (walked.wasInterrupted()) { return error("DOM helper work budget exhausted"); }
        if (publications != 1) { return error("global callable holder binding is replaced"); }
        for (ctjs::LoadGlobalOp load : result.loads) {
            if (!spend()) { return error("DOM helper work budget exhausted"); }
            if (load->getParentOfType<ctjs::FuncOp>() == entry) {
                auto * anchor = load.getOperation();
                while (anchor->getParentOp() != entry) {
                    if (!spend()) { return error("DOM helper work budget exhausted"); }
                    anchor = anchor->getParentOp();
                }
                if (!publication->isBeforeInBlock(anchor)) {
                    return error("global callable holder load precedes publication");
                }
            }
        }
    }
    llvm::DenseMap<mlir::Attribute, ctjs::SetPropertyOp> slots;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
    const auto key = [](mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())
                        : ctjs::StringAttr{};
    };
    // Callers prove standard Object.prototype and exclude prototype mutation,
    // unknown calls and script reentry. Only __proto__ has an inherited setter.
    llvm::SmallVector<mlir::Value> aliases{object.getResult()};
    for (ctjs::LoadGlobalOp load : result.loads) { aliases.push_back(load.getResult()); }
    for (mlir::Value alias : aliases) {
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!spend()) { return error("DOM helper work budget exhausted"); }
            auto * operation = use.getOwner();
            auto * definition = alias.getDefiningOp();
            if (operation->getBlock() != definition->getBlock() ||
                !definition->isBeforeInBlock(operation)) {
                return error("DOM helper object has nonlocal or unordered uses");
            }
            if (llvm::isa<ctjs::RootOp>(operation)) { continue; }
            if (publication && operation == publication && alias == object.getResult()) {
                continue;
            }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                auto name = key(store.getKey());
                auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                if (alias != object.getResult() ||
                    (publication && !store->isBeforeInBlock(publication)) ||
                    use.getOperandNumber() != 0 || !name || name.getValue() == "__proto__" ||
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
                if (!methodRead || methodRead.getObject() != alias ||
                    methodRead->getBlock() != definition->getBlock() ||
                    !methodRead->isBeforeInBlock(operation)) {
                    return error("DOM helper object escapes or observes its identity");
                }
                result.calls.push_back(operation);
            }
        }
    }
    if (slots.empty()) { return error("DOM helper object has no callable slots"); }
    for (ctjs::GetPropertyOp read : reads) {
        if (!spend()) { return error("DOM helper work budget exhausted"); }
        auto name = key(read.getKey());
        auto store = name ? slots.lookup(name) : ctjs::SetPropertyOp{};
        if (!store || (read.getObject() == object.getResult() && !store->isBeforeInBlock(read))) {
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

llvm::Expected<CallableObject> analyzeLocalCallableObject(ctjs::CreateObjectOp object,
                                                          llvm::function_ref<bool()> spend) {
    return analyzeCallableObject(object, {}, spend);
}

llvm::Expected<CallableObject> analyzeGlobalCallableObject(ctjs::StoreGlobalOp publication,
                                                           llvm::function_ref<bool()> spend) {
    auto object = publication.getValue().getDefiningOp<ctjs::CreateObjectOp>();
    if (!object) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "global callable holder is not a fresh object");
    }
    return analyzeCallableObject(object, publication, spend);
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
