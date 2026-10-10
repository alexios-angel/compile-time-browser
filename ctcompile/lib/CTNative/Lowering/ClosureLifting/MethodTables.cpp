//===- MethodTables.cpp - Returned method tables -------------- -*- C++ -*-===//
//
// Proves which returned object literals are method tables (objects whose
// fields are closures created and stored only there) so emission can lower
// them to C++ structs, and prepares the method table owned by a proved global
// root for native lowering.
//
//===----------------------------------------------------------------------===//
#include "../../Analysis/ClosedValueFlow.h"
#include "ClosureLifter.h"
#include "ctcompile/Support/CppLiterals.hpp"

namespace ctcompile::ctnative::lowering_detail {
namespace {
/// True when `key` can be a C++ struct member name.
bool fieldKey(llvm::StringRef key) {
    return !key.empty() && key != "__proto__" && llvm::all_of(key, cpp::is_identifier_char);
}
} // namespace

/// Finds object literals that are returned from a function and whose fields
/// are closures created and stored only there (a "method table"), and proves
/// that every read of a field is either a direct call on the same receiver or
/// a known boundary. A proved table gets ctnative.method_table and
/// ctnative.table_field attributes so emission can lower it to a C++ struct;
/// a refused one gets ctnative.object_reason.
void closureLifter::returnedMethodTableCensus(const OwnedGlobalRoots * globals) {
    // A later closure lift can expose more data flow, so re-derive the proof
    // from scratch: attributes from an earlier run must not survive if this run
    // refuses.
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
    for (mlir::Value value : flow.nodes()) { families[flow.find(value)].push_back(value); }
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
                // The only concrete producer. Two calls create distinct
                // instances; sharing a schema does not make them the same.
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
                auto fn = call.getTarget();
                if (!closedValueFlow::closed(fn) || flow.returns[fn].empty()) {
                    reject("result requires a closed function with visible returns");
                }
            } else if (auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
                       read && (slots.lookup(read) || globalSlot(read))) {
                // A fixed own-data field of a confined local (or proved global)
                // owner. The slot query alone does not prove the value is a
                // table; the rest of this family's checks do.
            } else {
                reject("flow contains another object or a non-table producer");
            }
            for (mlir::OpOperand & use : value.getUses()) {
                auto * user = use.getOwner();
                if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                    const auto key = ctjs::constantKey(set.getKey());
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
                    if (use.getOperandNumber() != 0 || !fieldKey(ctjs::constantKey(get.getKey()))) {
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
                    auto fn = call.getTarget();
                    if (use.getOperandNumber() == 0 && globalCalls.contains(call)) {
                        continue; // The host call edge checks this receiver.
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
                        continue; // Checked with the property read's uses below.
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
            continue; // Not a method table: an ordinary returned data object.
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
            auto field = fields.find(ctjs::constantKey(read.getKey()));
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
                    call.getTarget() == targetOf(made)) {
                    plans[made].calls.push_back(call);
                    continue;
                }
                // Synthetic capture reads exist only to give the solver the
                // call signature; emission invokes the stored callable directly.
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
            object->setAttr(kNativeObjectReason, mlir::StringAttr::get(context, reason));
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
                          mlir::StringAttr::get(context, ctjs::constantKey(read.getKey())));
            auto made = fields[ctjs::constantKey(read.getKey())]
                            .getValue()
                            .getDefiningOp<ctjs::CreateClosureOp>();
            read->setAttr(kNativeEnvironment,
                          mlir::StringAttr::get(context, targetOf(made).getSymName()));
        }
    }
}

/// Removes every ctnative.* attribute from operations (not from the module)
/// so the census rebuilds them from scratch. Called on a speculative clone
/// after the original host contract was checked. Leftover local-lifting
/// markers such as ctnative.method would otherwise make the emitter drop a
/// real field initialization or observation store.
void closureLifter::discardNativeSourceFacts() {
    module.walk([](mlir::Operation * op) {
        if (llvm::isa<mlir::ModuleOp>(op)) { return; }
        removeAttrsWithPrefix(op, "ctnative.");
    });
}

/// Rewrites the module so the method table owned by a proved global root can
/// be lowered natively: drops unused wrapper parameters, lifts the factory,
/// wrapper and method closures, unboxes the shared Map cell and, for the DOM
/// data-session host, lifts local constructors. Returns std::nullopt, leaving
/// the caller to discard this speculative clone, if any proof fails or
/// `maxSteps` runs out.
std::optional<liftReport> closureLifter::prepareOwnedGlobalMethodTables(
    const OwnedGlobalRoots & globals, const HostContract & contract, unsigned maxSteps) {
    if (!globals.proved() || globals.steps() > maxSteps || globals.roots().empty() ||
        !globals.roots().front().methodTable) {
        return std::nullopt;
    }
    discardNativeSourceFacts();
    const auto sourceTable = *globals.roots().front().methodTable;
    liftReport out;
    if (sourceTable.capturedMap) {
        // The owner proof identified the single call to the wrapper. Drop the
        // wrapper's unused JavaScript parameters; the argument values passed
        // for them are still evaluated.
        auto wrapper = sourceTable.wrapper;
        auto & block = wrapper.getBody().front();
        llvm::BitVector unused(block.getNumArguments());
        unsigned remaining = maxSteps - globals.steps();
        for (unsigned index = ctjs::implicit_arguments; index < block.getNumArguments(); ++index) {
            if (!remaining) { return std::nullopt; }
            --remaining;
            if (block.getArgument(index).use_empty()) { unused.set(index); }
        }
        if (unused.any()) {
            sourceTable.wrapperCall->eraseOperands(unused);
            if (mlir::failed(wrapper.eraseArguments(unused))) {
                llvm::report_fatal_error("ctnative: could not erase unused proved wrapper slots");
            }
        }
        // The owner proof follows the factory through one wrapper parameter
        // that is only called. Bind it directly with the ordinary callback
        // specialization, keeping the wrapper and factory calls.
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
        // Rebuild the owner graph, since the lifts above changed the module.
        // The original host contract was already checked; the new fingerprint
        // only describes this clone, and a failed proof discards the clone.
        HostContract transformed = contract;
        transformed.moduleSha256 = hostContractFingerprint(module);
        prepared.emplace(module, transformed, maxSteps);
        if (!prepared->proved() || prepared->roots().empty() ||
            !prepared->roots().front().methodTable) {
            return std::nullopt;
        }
    }
    const auto & current = prepared ? *prepared : globals;
    returnedMethodTableCensus(&current);
    const auto table = *current.roots().front().methodTable;
    if (table.capturedMap) {
        for (const auto & callback : table.capturedMap->scalarCallbacks) {
            auto & plan = returnedClosures[callback.closure];
            plan.calls.assign(callback.calls.begin(), callback.calls.end());
            plan.stored = true;
            if (whyNotReturnedClosure(callback.closure)) { return std::nullopt; }
        }
    }
    for (const auto & method : table.methods) {
        auto made = method.closure;
        const auto plan = returnedClosures.find(made);
        if (plan == returnedClosures.end() || !plan->second.reason.empty() ||
            whyNotReturnedClosure(made)) {
            return std::nullopt;
        }
    }
    // Every method was validated above, before any is changed. Keep the real
    // receiver (the `true` argument) for the callable proof that runs after
    // the rewrite. Each capture becomes a leading typed argument and an owning
    // environment slot; the shared cell is unboxed only after every closure
    // has its own Map owner.
    for (const auto & method : table.methods) {
        if (table.capturedMap) {
            lift(method.function, {method.closure}, out, true);
        } else {
            liftReturnedClosure(method.closure, method.function, 0, 0, out, true);
        }
    }
    if (table.capturedMap) {
        for (const auto & callback : table.capturedMap->scalarCallbacks) {
            lift(callback.function, {callback.closure}, out, true);
        }
    }
    if (table.capturedMap) {
        unboxCells(out);
        eraseUnusedUnboxedCells();
    }
    if (contract.provider == HostContract::Provider::ctbrowserDOMDataSession) {
        // The ordinary constructor rule cannot prove instances kept in local
        // Maps, so use the host analysis's local records instead; the caller
        // re-proves ownership of the rewritten allocations and initializer
        // calls.
        HostContract transformed = contract;
        transformed.moduleSha256 = hostContractFingerprint(module);
        const HostContractAnalysis host(module, transformed, maxSteps);
        if (!host.proved()) { return std::nullopt; }
        const auto & records = host.localRecords();
        if (!records.constructors.empty() && !records.primitiveFields) { return std::nullopt; }
        closureLifter locals{module};
        locals.census();
        locals.constructorCensus();
        unsigned remaining = maxSteps - host.steps();
        llvm::SmallVector<ctjs::CreateClosureOp> constructors;
        for (ctjs::FuncOp function : records.constructors) {
            if (!remaining) { return std::nullopt; }
            --remaining;
            // An initializer lifted earlier already has its direct calls; only
            // functions still reached through ctjs.construct need the lift.
            if (!locals.constructsOfTarget.contains(function)) { continue; }
            auto closure = locals.uniqueClosureByTarget.lookup(function);
            if (!closure || locals.whyTargetIsNotLiftable(closure)) { return std::nullopt; }
            constructors.push_back(closure);
        }
        for (ctjs::CreateClosureOp closure : constructors) {
            locals.lift(locals.targetOf(closure), {closure}, out, true);
        }
    }
    return out;
}

} // namespace ctcompile::ctnative::lowering_detail
