#include "../../Analysis/ClosedValueFlow.h"
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {
bool fieldKey(llvm::StringRef key) {
    return !key.empty() && key != "__proto__" && llvm::all_of(key, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    });
}
} // namespace

void closureLifter::returnedMethodTableCensus(const OwnedGlobalRoots * globals) {
    // A later local lift can expose more flow. Re-derive the table proof;
    // a previous round's successful prefix must not survive a later refusal.
    module.walk([](mlir::Operation * op) {
        if (!methodTableName(op).empty() && llvm::isa<ctjs::GetPropertyOp>(op)) {
            op->removeAttr(kNativeEnvironment);
        }
        op->removeAttr(kNativeMethodTable);
        op->removeAttr(kNativeTableField);
    });
    closedValueFlow flow;
    flow.build(module);
    OwnedMethodTableSlots slots(module);
    flow.connectOwnedMethodTableSlots(slots);
    llvm::DenseSet<mlir::Operation *> globalCalls;
    if (globals) {
        flow.connectOwnedGlobalMethodTables(*globals);
        for (const OwnedGlobalRoot & owner : globals->roots()) {
            if (!owner.methodTable) { continue; }
            for (const HostCallableEdge & call : owner.methodTable->calls) {
                globalCalls.insert(call.call);
            }
        }
    }
    const auto globalSlot = [&](mlir::Operation * operation) {
        const auto * owner = globals ? globals->lookup(operation) : nullptr;
        return owner && owner->methodTable.has_value();
    };
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> families;
    for (mlir::Value value : flow.nodes) { families[flow.find(value)].push_back(value); }
    llvm::DenseMap<mlir::Operation *, unsigned> creations;
    for (ctjs::CreateClosureOp made : closures) { ++creations[targetOf(made)]; }
    mlir::DominanceInfo dominance(module);
    unsigned ordinal = 0;
    for (ctjs::CreateObjectOp object : objects) {
        const auto site = "table_" + std::to_string(ordinal++);
        const auto root = flow.find(object.getResult());
        if (!root) { continue; }
        const auto & family = families[root];
        bool returned = false;
        for (mlir::Value value : family) {
            for (mlir::Operation * user : value.getUsers()) {
                returned |= llvm::isa<ctjs::ReturnOp>(user);
            }
        }
        if (!returned) { continue; }

        llvm::StringMap<ctjs::SetPropertyOp> fields;
        llvm::SmallVector<ctjs::GetPropertyOp> reads;
        llvm::SmallVector<mlir::Operation *> boundaries;
        std::string reason;
        const auto reject = [&](llvm::StringRef why) {
            if (reason.empty()) { reason = ("returned method table " + why).str(); }
        };
        for (mlir::Value value : family) {
            if (value == object.getResult()) {
                // The only concrete producer. Schema equality never equates
                // distinct runtime instances created by different calls.
            } else if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(value)) {
                auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
                if (!closedValueFlow::closed(fn) || arg.getOwner() != &fn.getBody().front() ||
                    arg.getArgNumber() < 3 || flow.callers[fn].empty()) {
                    reject("parameter requires a closed function with visible callers");
                } else {
                    for (ctjs::CallDirectOp call : flow.callers[fn]) {
                        if (call->getNumOperands() != fn.getBody().front().getNumArguments()) {
                            reject("parameter has a missing or surplus argument");
                        }
                    }
                }
            } else if (auto call = value.getDefiningOp<ctjs::CallDirectOp>()) {
                auto fn = closedValueFlow::target(call);
                if (!closedValueFlow::closed(fn) || flow.returns[fn].empty()) {
                    reject("result requires a closed function with visible returns");
                }
            } else if (auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
                       read && (slots.lookup(read) || globalSlot(read))) {
                // The field is a fixed own-data slot on a confined local
                // owner. Its complete incoming table family is checked here;
                // the slot query by itself does not prove a table carrier.
            } else {
                reject("flow contains another object or a non-table producer");
            }
            for (mlir::OpOperand & use : value.getUses()) {
                auto * user = use.getOwner();
                if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                    const auto key = constantKeyOf(set.getKey());
                    if (use.getOperandNumber() == 2 && (slots.lookup(set) || globalSlot(set))) {
                        boundaries.push_back(user);
                        continue;
                    }
                    if (use.getOperandNumber() != 0 || value != object.getResult()) {
                        reject("is written through an alias or stored into another object");
                    } else if (!fieldKey(key)) {
                        reject("field needs a supported constant key");
                    } else if (!fields.try_emplace(key, set).second) {
                        reject("field is written more than once");
                    }
                    continue;
                }
                if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                    if (use.getOperandNumber() != 0 || !fieldKey(constantKeyOf(get.getKey()))) {
                        reject("read needs a supported constant key");
                    }
                    reads.push_back(get);
                    boundaries.push_back(user);
                    continue;
                }
                if (auto ret = llvm::dyn_cast<ctjs::ReturnOp>(user)) {
                    if (!closedValueFlow::closed(ret->getParentOfType<ctjs::FuncOp>())) {
                        reject("return requires a closed function");
                    }
                    boundaries.push_back(user);
                    continue;
                }
                if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
                    auto fn = closedValueFlow::target(call);
                    if (use.getOperandNumber() == 0 && globalCalls.contains(call)) {
                        continue; // the live host edge checks this exact receiver
                    }
                    if (use.getOperandNumber() >= 3 && closedValueFlow::closed(fn) &&
                        call->getNumOperands() == fn.getBody().front().getNumArguments()) {
                        boundaries.push_back(user);
                        continue;
                    }
                }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
                    auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    if (use.getOperandNumber() == 1 && get && get.getObject() == value) {
                        continue; // checked with every use of the property read below
                    }
                }
                reject(("escapes or is inspected through `" + user->getName().getStringRef() + "`")
                           .str());
            }
        }
        if (llvm::none_of(fields, [](const auto & field) {
                auto set = field.second;
                return set.getValue().template getDefiningOp<ctjs::CreateClosureOp>();
            })) {
            continue; // Ordinary returned data objects keep their existing proof and diagnostic.
        }
        llvm::DenseMap<mlir::Operation *, returnedClosure> plans;
        for (auto & field : fields) {
            auto set = field.second;
            auto made = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            auto target = made ? targetOf(made) : ctjs::FuncOp{};
            if (made) { plans[made].stored = true; }
            if (!made || !target || target.getBody().empty() || !made.getResult().hasOneUse() ||
                creations.lookup(target) != 1) {
                reject("field requires one closure created and stored only here");
                continue;
            }
            for (mlir::Operation * boundary : boundaries) {
                if (boundary->getParentOfType<ctjs::FuncOp>() ==
                        object->getParentOfType<ctjs::FuncOp>() &&
                    !dominance.properlyDominates(set.getOperation(), boundary)) {
                    reject("field initialization does not dominate every read or publication");
                }
            }
        }
        for (ctjs::GetPropertyOp read : reads) {
            auto field = fields.find(constantKeyOf(read.getKey()));
            if (field == fields.end()) {
                reject("reads a field that was never initialized");
                continue;
            }
            auto made = field->second.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (!made || !plans.contains(made)) { continue; }
            for (mlir::OpOperand & use : read.getResult().getUses()) {
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                    call && use.getOperandNumber() == 0 && call.getReceiver() == read.getObject()) {
                    plans[made].calls.push_back(call);
                    continue;
                }
                if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                    call && use.getOperandNumber() == 2 &&
                    (call->hasAttr(kNativeStoredCall) || globalCalls.contains(call)) &&
                    closedValueFlow::target(call) == targetOf(made)) {
                    plans[made].calls.push_back(call);
                    continue;
                }
                // Synthetic capture reads exist only for the solver's call
                // signature. Emission invokes the stored callable directly.
                if (use.getOwner()->hasAttr(kNativeStoredRead)) { continue; }
                reject("field is detached, inspected or called with another receiver");
            }
        }
        for (auto & [operation, plan] : plans) {
            auto made = llvm::cast<ctjs::CreateClosureOp>(operation);
            if (plan.calls.empty()) { reject("field has no visible invocation"); }
            for (ctjs::CallDirectOp call : flow.callers[targetOf(made)]) {
                if (!llvm::is_contained(plan.calls, call.getOperation())) {
                    reject("method target has a caller outside the proved table flow");
                }
            }
        }
        for (auto & [operation, plan] : plans) {
            if (!environmentTarget(operation).empty()) { continue; }
            plan.reason = reason;
            returnedClosures[operation] = std::move(plan);
        }
        if (!reason.empty()) {
            object->setAttr("ctnative.object_reason", mlir::StringAttr::get(context, reason));
            continue;
        }
        const auto name = mlir::StringAttr::get(context, site);
        object->setAttr(kNativeMethodTable, name);
        for (auto & field : fields) {
            field.second->setAttr(kNativeMethodTable, name);
            field.second->setAttr(kNativeTableField, mlir::StringAttr::get(context, field.first()));
        }
        for (ctjs::GetPropertyOp read : reads) {
            read->setAttr(kNativeMethodTable, name);
            read->setAttr(kNativeTableField,
                          mlir::StringAttr::get(context, constantKeyOf(read.getKey())));
            auto made = fields[constantKeyOf(read.getKey())]
                            .getValue()
                            .getDefiningOp<ctjs::CreateClosureOp>();
            read->setAttr(kNativeEnvironment,
                          mlir::StringAttr::get(context, targetOf(made).getSymName()));
        }
    }
}

void closureLifter::discardNativeSourceFacts() {
    // Called on a speculative clone after the original manifest passed.
    // Reconstruct all native source-operation facts before the census. Old
    // local-lifting markers such as ctnative.method would otherwise erase a
    // real field initialization or observation store in the generic emitter.
    // Module reports and presentation locations are not operation authority.
    module.walk([](mlir::Operation * op) {
        if (llvm::isa<mlir::ModuleOp>(op)) { return; }
        llvm::SmallVector<mlir::StringAttr> discard;
        for (mlir::NamedAttribute attribute : op->getAttrs()) {
            if (attribute.getName().getValue().starts_with("ctnative.")) {
                discard.push_back(attribute.getName());
            }
        }
        for (mlir::StringAttr name : discard) { op->removeAttr(name); }
    });
}

std::optional<liftReport> closureLifter::prepareOwnedGlobalMethodTables(
    const OwnedGlobalRoots & globals, const HostContract & contract, unsigned maxSteps) {
    if (!globals.proved() || globals.roots().size() != 1 || !globals.roots().front().methodTable) {
        return std::nullopt;
    }
    discardNativeSourceFacts();
    const auto sourceTable = *globals.roots().front().methodTable;
    liftReport out;
    if (sourceTable.capturedMap) {
        // The input proof follows the factory through one call-only wrapper
        // parameter. Reuse that parameter's checked removal and the ordinary
        // local closure lift, retaining the source wrapper/factory calls.
        indexAndNewTargets();
        specializeCallbacks(out);
    }
    census();
    std::optional<OwnedGlobalRoots> prepared;
    if (sourceTable.capturedMap) {
        for (ctjs::FuncOp function : {sourceTable.wrapper, sourceTable.factory}) {
            if (!function) { return std::nullopt; }
            llvm::SmallVector<ctjs::CreateClosureOp, 1> sites;
            for (ctjs::CreateClosureOp closure : closures) {
                if (targetOf(closure) == function) { sites.push_back(closure); }
            }
            if (sites.size() != 1 || whyNotLiftable(sites.front())) { return std::nullopt; }
            lift(function, sites, out);
        }
        // Reconstruct the source-owner graph after callback transport changes.
        // The original manifest has already passed; this derived fingerprint
        // cannot rescue stale input, and an incomplete query discards the clone.
        HostContract transformed = contract;
        transformed.moduleSha256 = hostContractFingerprint(module);
        prepared.emplace(module, transformed, maxSteps);
        if (!prepared->proved() || prepared->roots().size() != 1 ||
            !prepared->roots().front().methodTable) {
            return std::nullopt;
        }
    }
    const auto & current = prepared ? *prepared : globals;
    returnedMethodTableCensus(&current);
    const auto table = *current.roots().front().methodTable;
    for (const auto & method : table.methods) {
        auto made = method.closure;
        const auto plan = returnedClosures.find(made);
        if (plan == returnedClosures.end() || !plan->second.reason.empty() ||
            whyNotReturnedClosure(made)) {
            return std::nullopt;
        }
    }
    // Preserve the actual source receiver for the post-rewrite callable proof.
    // Each capture becomes a leading typed argument and an owning environment
    // slot. Validate the entire family before changing any method, and unbox
    // the shared cell only after every closure has acquired its own Map owner.
    for (const auto & method : table.methods) {
        if (table.capturedMap) {
            lift(method.function, {method.closure}, out, true);
        } else {
            liftReturnedClosure(method.closure, method.function, 0, 0, out, true);
        }
    }
    if (table.capturedMap) {
        unboxCells(out);
        llvm::SmallVector<ctjs::CreateCellOp> dead;
        module.walk([&](ctjs::CreateCellOp cell) {
            if (cell->hasAttr("ctnative.unboxed") && cell.getResult().use_empty()) {
                dead.push_back(cell);
            }
        });
        for (ctjs::CreateCellOp cell : dead) { cell.erase(); }
    }
    return out;
}

} // namespace ctcompile::ctnative::lowering_detail
