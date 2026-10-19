//===- ClosedCallable.cpp - Closed callables and callable holders ---------===//
//
// Proofs that a function is only ever entered through calls visible in the
// module (closedCallableProblem() and its helpers), and that an object is a
// callable holder: a fresh object whose slots each hold one closure and that is
// used only to call them (analyzeLocalCallableObject(),
// analyzeGlobalCallableObject()). Declarations are in ClosedCallable.h.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {

/// Proves that \p object is a callable holder: a fresh object whose own slots
/// are each set exactly once to a closure, and that is only ever used to call
/// those slots. With \p publication, the holder is also stored in a global
/// once, unconditionally, in the entry function, before any code that could
/// call back into the program. On success, returns the slot stores, the reads
/// that resolve to them and the calls through them. \p spend charges the work
/// budget; \p projectedUses, when given, replaces each alias's direct uses.
static llvm::Expected<CallableObject> analyzeCallableObject(
    ctjs::CreateObjectOp object, ctjs::StoreGlobalOp publication, llvm::function_ref<bool()> spend,
    llvm::function_ref<llvm::SmallVector<mlir::OpOperand *>(mlir::Value)> projectedUses = {}) {
    const auto error = [](llvm::StringRef message) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), message);
    };
    const auto exhausted = [&] { return error("DOM helper work budget exhausted"); };
    const auto precedes = [&](mlir::Operation * definition, mlir::Operation * use) {
        // Immutable slots may be read in branches, loops and exception
        // continuations. The complete use census still requires unique unconditional stores;
        // no backedge can replace a slot or carry the holder into another scope.
        while (use->getBlock() != definition->getBlock()) {
            if (!spend()) { return false; }
            auto invocation = llvm::dyn_cast_or_null<ctjs::InvokeOp>(use->getParentOp());
            // Only the call executes before dispatch. Direct continuation uses
            // follow dispatch; the Invoke must follow initialization. Saved
            // holder transport is still rejected by the complete use census.
            const bool protectedCall = invocation &&
                                       use->getParentRegion() == &invocation.getBody() &&
                                       llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(use);
            const bool continuation =
                invocation && (use->getParentRegion() == &invocation.getNormalBody() ||
                               use->getParentRegion() == &invocation.getUnwindBody());
            if (!protectedCall && !continuation &&
                !llvm::isa_and_nonnull<mlir::scf::IfOp, mlir::scf::WhileOp, ctjs::TryOp>(
                    use->getParentOp())) {
                return false;
            }
            use = use->getParentOp();
        }
        return definition->isBeforeInBlock(use);
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
        // ponytail: everything before the global store must be side-effect free
        // (constants, closures, object literals and their own-key stores), so no
        // script can run and observe the holder before it is published. Holders
        // set up after a call would need a full caller graph to prove instead.
        for (mlir::Operation & op : entry.getBody().front()) {
            if (!spend()) { return exhausted(); }
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
        if (walked.wasInterrupted()) { return exhausted(); }
        if (publications != 1) { return error("global callable holder binding is replaced"); }
        for (ctjs::LoadGlobalOp load : result.loads) {
            if (!spend()) { return exhausted(); }
            if (load->getParentOfType<ctjs::FuncOp>() == entry) {
                auto * anchor = load.getOperation();
                while (anchor->getParentOp() != entry) {
                    if (!spend()) { return exhausted(); }
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
    const auto key = ctjs::constantAttr<ctjs::StringAttr>;
    // Callers prove standard Object.prototype and exclude prototype mutation,
    // unknown calls and script reentry. Only __proto__ has an inherited setter.
    llvm::SmallVector<mlir::Value> aliases{object.getResult()};
    for (ctjs::LoadGlobalOp load : result.loads) { aliases.push_back(load.getResult()); }
    for (mlir::Value value : aliases) {
        llvm::SmallVector<mlir::OpOperand *> uses;
        if (projectedUses) {
            uses = projectedUses(value);
        } else {
            for (mlir::OpOperand & use : value.getUses()) {
                if (!spend()) { return exhausted(); }
                uses.push_back(&use);
            }
        }
        for (mlir::OpOperand * operand : uses) {
            auto & use = *operand;
            auto alias = use.get();
            if (!spend()) { return exhausted(); }
            auto * operation = use.getOwner();
            auto * definition = alias.getDefiningOp();
            if (!precedes(definition, operation)) {
                return error("DOM helper object has nonlocal or unordered uses");
            }
            if (llvm::isa<ctjs::RootOp>(operation)) { continue; }
            if (publication && operation == publication && alias == object.getResult()) {
                continue;
            }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                auto name = key(store.getKey());
                auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                if (alias != object.getResult() || store->getBlock() != object->getBlock() ||
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
                    !precedes(methodRead, operation)) {
                    return error("DOM helper object escapes or observes its identity");
                }
                result.calls.push_back(operation);
            }
        }
    }
    if (slots.empty()) { return error("DOM helper object has no callable slots"); }
    for (ctjs::GetPropertyOp read : reads) {
        if (!spend()) { return exhausted(); }
        auto name = key(read.getKey());
        auto store = name ? slots.lookup(name) : ctjs::SetPropertyOp{};
        if (!store || (read.getObject() == object.getResult() && !precedes(store, read))) {
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

llvm::Expected<CallableObject> analyzeLocalCallableObject(
    ctjs::CreateObjectOp object, llvm::function_ref<bool()> spend,
    llvm::function_ref<llvm::SmallVector<mlir::OpOperand *>(mlir::Value)> uses) {
    return analyzeCallableObject(object, {}, spend, uses);
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
    if (!body || body.getBody().empty() || body.getUpvalueCount() != 0 || !body.isPrivate()) {
        return false;
    }
    auto & entry = body.getBody().front();
    if (entry.getNumArguments() < 3 || direct->getNumOperands() != entry.getNumArguments()) {
        return false;
    }
    return llvm::all_of(entry.getArgument(2).getUsers(), llvm::IsaPred<ctjs::RootOp>) &&
           !usesEnvironment(body);
}

bool calleeSlotOnly(ctjs::LoadGlobalOp load, bool rooted) {
    return llvm::all_of(load.getResult().getUses(), [&](mlir::OpOperand & use) {
        return (rooted && llvm::isa<ctjs::RootOp>(use.getOwner())) ||
               (llvm::isa<ctjs::CallDirectOp>(use.getOwner()) &&
                use.getOperandNumber() == ctjs::arg_callee);
    });
}

bool implicitArgumentsRooted(mlir::Block & entry) {
    for (mlir::BlockArgument argument : entry.getArguments().take_front(ctjs::implicit_arguments)) {
        if (!llvm::all_of(argument.getUsers(), llvm::IsaPred<ctjs::RootOp>)) { return false; }
    }
    return true;
}

bool usesEnvironment(ctjs::FuncOp function) {
    bool found = false;
    function.getBody().walk([&](mlir::Operation * op) {
        found |= llvm::isa<ctjs::CreateClosureOp, ctjs::CreateCellOp, ctjs::LoadUpvalueOp,
                           ctjs::StoreUpvalueOp, ctjs::CellGetOp, ctjs::CellSetOp>(op);
    });
    return found;
}

bool hasAlternateCalleeCalls(ctjs::FuncOp function, mlir::ModuleOp module) {
    const auto index = functionIndex(function);
    if (!index) { return false; }
    const auto alternate = [&](mlir::Value value) {
        return llvm::any_of(value.getUses(), [&](mlir::OpOperand & use) {
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            return call && use.getOperandNumber() == 2 && call.getCallee() != function.getSymName();
        });
    };
    bool found = false;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (found || ctjs::closureFunction(made) != index) { return; }
        found |= alternate(made.getResult());
        for (mlir::Operation * user : made.getResult().getUsers()) {
            auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(user);
            if (!store) { continue; }
            module.walk([&](ctjs::LoadGlobalOp load) {
                if (load.getName() == store.getName()) { found |= alternate(load.getResult()); }
            });
        }
    });
    return found;
}

std::optional<llvm::SmallVector<ctjs::CallDirectOp>> directCallers(ctjs::FuncOp function,
                                                                   mlir::ModuleOp module) {
    const auto uses = mlir::SymbolTable::getSymbolUses(function, module);
    if (!uses) { return std::nullopt; }
    llvm::SmallVector<ctjs::CallDirectOp> calls;
    for (const auto & use : *uses) {
        auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
        if (!call || call.getTarget() != function ||
            call->getNumOperands() != function.getBody().front().getNumArguments()) {
            return std::nullopt;
        }
        calls.push_back(call);
    }
    return calls;
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
    bool closed = true;
    module.walk([&](ctjs::LoadGlobalOp load) {
        if (load.getName() != store.getName()) { return; }
        for (mlir::OpOperand & use : load.getResult().getUses()) {
            if (!llvm::isa<ctjs::RootOp>(use.getOwner()) && !directCalleeUse(use, target)) {
                closed = false;
            }
        }
    });
    return closed && ctjs::uniqueGlobalStore(module, store.getName());
}

std::string closedCallableProblem(ctjs::FuncOp function, mlir::ModuleOp module) {
    const auto index = functionIndex(function);
    if (!index) { return {}; } // No closure index can name this symbol.
    std::string reason;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!reason.empty() || ctjs::closureFunction(made) != index) { return; }
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
