//===- Initialization.cpp - TypeInference::initialize ---------------------===//
//
// TypeInference::initialize builds the whole-module store indexes the transfer
// functions read: field stores by receiver group, array appends and element
// stores, captured-cell stores, native Map keys and values, global stores, and
// whether the globals table may be written dynamically.
//
//===----------------------------------------------------------------------===//

#include "../OwnedGlobalRoots.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/EscapeAnalysis.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

namespace ctcompile::ctnative {
using ctjs::constantKey;

namespace {
/// Calls `f` on every member of `value`'s group, or on `value` alone outside any
/// group.
template <class Groups, class F> void forEachMember(const Groups & groups, mlir::Value value, F f) {
    const auto group = groups.find(value);
    if (group == groups.end()) { return f(value); }
    for (mlir::Value member : group->second) { f(member); }
}
} // namespace

mlir::LogicalResult TypeInference::initialize(mlir::Operation * top) {
    globalStores_.clear();
    globalsAreDynamic_ = false;
    fieldStores_.clear();
    identityFieldStores_.clear();
    assignedIdentityFields_.clear();
    fieldStoreSites_.clear();
    objectParameterCallSites_.clear();
    appends_.clear();
    arrayReadValues_.clear();
    cellStores_.clear();
    mapKeys_.clear();
    mapValues_.clear();
    environmentCaptures_.clear();
    top->walk([&](ctjs::CreateClosureOp made) {
        const auto target = environmentTarget(made);
        if (!target.empty()) {
            environmentCaptures_[target].assign(made.getUpvalues().begin(),
                                                made.getUpvalues().end());
        }
    });
    top->walk([&](ctjs::CallOp call) {
        const llvm::StringRef action = nativeMapAction(call);
        if (action.empty()) { return; }
        const int64_t group = nativeMapGroup(call.getReceiver());
        if (group < 0) { return; }
        if (nativeMapKeyAction(action)) {
            mlir::Type exact;
            if (auto proof = call->getAttrOfType<mlir::StringAttr>(kNativeMapKeyType)) {
                exact = mapTagType(call.getContext(), proof.getValue());
            }
            mapKeys_[group].push_back({call.getArgs()[0], exact});
        }
        if (action == "set") { mapValues_[group].push_back(call.getArgs()[1]); }
    });
    // The field index is keyed on the receiver group, not on one value. Inside
    // a lifted method, `this.x = 5` must be visible to the caller's `o.x`, and
    // `this.x` reads what the caller stored: `%arg0` and the literal name one
    // object. Every member of a group gets every store made through any of
    // them; a literal with no lifted method is a group of one.
    const auto groups = groupReceivers(top);
    top->walk([&](ctjs::SetPropertyOp store) {
        const int64_t identityGroup = nativeObjectFieldGroup(store);
        if (identityGroup >= 0) {
            identityFieldStores_[{identityGroup, constantKey(store.getKey())}].push_back(
                store.getValue());
            return;
        }
        if (!hasClosedShape(store.getObject())) { return; }
        const llvm::StringRef key = constantKey(store.getKey());
        // Record the store site, keyed on its own object rather than on the
        // receiver group. It is collected in the same walk as the value index
        // so the two cannot disagree about which stores exist; they differ only
        // in their key (see fieldStoreSites_ in TypeInference.h).
        fieldStoreSites_[{store.getObject(), key}].push_back(store.getOperation());
        forEachMember(groups, store.getObject(), [&](mlir::Value member) {
            fieldStores_[{member, key}].push_back(store.getValue());
        });
    });
    top->walk([&](ctjs::GetPropertyOp read) {
        if (nativeObjectFieldGroup(read) >= 0 && queryNativeObjectFieldPresence(read).assigned) {
            assignedIdentityFields_.insert(read);
        }
    });
    if (auto module = llvm::dyn_cast<mlir::ModuleOp>(top)) {
        top->walk([&](ctjs::FuncOp function) {
            if (function.getBody().empty() || !function.isPrivate()) { return; }
            llvm::SmallVector<mlir::Value> parameters;
            for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
                const auto group = groups.find(argument);
                if (namesAnObjectParameter(argument) && group != groups.end() &&
                    llvm::all_of(group->second, hasClosedShape)) {
                    parameters.push_back(argument);
                }
            }
            if (parameters.empty()) { return; }
            if (!closedCallableProblem(function, module).empty()) {
                // Method lifting removes the callable reads, but leaves its
                // original stores until emission. Recheck that every numeric
                // closure use is now unobservable storage: neither the local
                // literal nor any borrowed alias may still read that key.
                // A `ctnative.method` annotation alone proves none of this.
                const auto index = functionIndex(function);
                if (!index) { return; }
                bool closed = true;
                module.walk([&](ctjs::CreateClosureOp made) {
                    if (!closed || made.getFunction() < 0 ||
                        static_cast<unsigned>(made.getFunction()) != *index) {
                        return;
                    }
                    for (mlir::OpOperand & use : made.getResult().getUses()) {
                        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                        auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                        if (!store || use.getOperandNumber() != 2 ||
                            !store.getObject().getDefiningOp<ctjs::CreateObjectOp>()) {
                            closed = false;
                            return;
                        }
                        const auto aliases = groups.find(store.getObject());
                        if (aliases == groups.end() ||
                            !llvm::all_of(aliases->second, hasClosedShape)) {
                            closed = false;
                            return;
                        }
                        const auto key = constantKey(store.getKey());
                        for (mlir::Value alias : aliases->second) {
                            for (mlir::Operation * user : alias.getUsers()) {
                                auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                                if (load && constantKey(load.getKey()) == key) {
                                    closed = false;
                                    return;
                                }
                            }
                        }
                    }
                });
                if (!closed) { return; }
            }
            // Numeric closure identities are checked above; every symbol use
            // must also be an ordinary direct call. Each parameter records its
            // actual call sites, not the receiver group.
            const auto callers = directCallers(function, module);
            if (!callers || callers->empty()) { return; }
            for (mlir::Value parameter : parameters) {
                objectParameterCallSites_[parameter].assign(callers->begin(), callers->end());
            }
        });
    }
    // The appends index, built here like fieldStores_ for the same reason: an
    // element read must find every value appended or stored into the array,
    // and walking the uses at each read would repeat this walk per read. A
    // literal's inline elements come first: the importer emits an empty
    // `create_array` plus one `append` per element, but the op can carry
    // elements, and ignoring them would drop values.
    top->walk([&](ctjs::CreateArrayOp array) {
        for (mlir::Value member : denseVectorAliases(array.getResult())) {
            auto & into = appends_[member];
            for (mlir::Value element : array.getElements()) { into.push_back(element); }
        }
    });
    top->walk([&](ctjs::AppendOp push) {
        for (mlir::Value member : denseVectorAliases(push.getArray())) {
            appends_[member].push_back(push.getElement());
        }
    });
    top->walk([&](ctjs::SetPropertyOp store) {
        if (constantKey(store.getKey()) == "length") { return; }
        // Every array in a dense-vector alias group shares one element store,
        // including writes made through another member of the group.
        for (mlir::Value member : denseVectorAliases(store.getObject())) {
            appends_[member].push_back(store.getValue());
        }
    });
    llvm::DenseSet<mlir::Operation *> contentsFunctions;
    for (const auto & [array, values] : appends_) {
        (void)values;
        auto function = mlir::Value(array).getParentRegion()->getParentOfType<ctjs::FuncOp>();
        if (!function || !contentsFunctions.insert(function).second) { continue; }
        const auto contents = computeArrayContents(function);
        if (!contents.complete) { continue; }
        for (const auto & read : contents.reads) {
            auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(read.by);
            if (!get || !appends_.contains(get.getObject())) { continue; }
            auto & origins = arrayReadValues_[read.by];
            if (!llvm::is_contained(origins, read.value)) { origins.push_back(read.value); }
        }
    }
    // The captured-cell index is keyed on the cell group, for the same reason
    // as the field index. A `ctjs.cell_set` through a capture pointer is in a
    // different ctjs.func from the cell, and the owning frame's read must see
    // it: `var n = 0; function tick() { n = n + 1; }` writes a double from
    // inside `tick` into a cell created holding `undefined`; indexing only the
    // frame's own stores would report `opt<i32>` for a binding holding a double.
    const auto cells = groupCells(top);
    top->walk([&](ctjs::CreateCellOp cell) {
        if (!cell->hasAttr("ctnative.carried")) { return; }
        // Leave out the cell's initial value when no read can see it. Closure
        // lifting sets kAssignedBeforeRead when one ctjs.cell_set properly
        // dominates every ctjs.cell_get of the cell and every call of every
        // closure that captured it, so the hoisted `undefined` the cell was
        // created with is unreachable. Dropping it here rather than in
        // cellTypeOf keeps the type join in one place.
        if (cell->hasAttr(kAssignedBeforeRead)) { return; }
        forEachMember(cells, cell.getResult(), [&](mlir::Value member) {
            cellStores_[member].push_back(cell.getInitial());
        });
    });
    top->walk([&](ctjs::CellSetOp store) {
        if (!namesACarriedCell(store.getCell())) { return; }
        forEachMember(cells, store.getCell(),
                      [&](mlir::Value member) { cellStores_[member].push_back(store.getValue()); });
    });
    top->walk([&](mlir::Operation * op) {
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
            globalStores_[store.getName()].push_back(store.getValue());
            return;
        }
        // A property write through the global object is a write to the globals
        // table this index cannot see. `globalThis` and `window` are the two
        // names that reach it; a load of either anywhere in the program is
        // taken as "the table may be written dynamically" unless the complete
        // owner proof identifies that exact read as a source-created ordinary
        // object. Its binding name alone does not make it the realm object.
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            if ((load.getName() == "globalThis" || load.getName() == "window") &&
                !(ownedRoots_ && ownedRoots_->proved() && ownedRoots_->lookup(load))) {
                globalsAreDynamic_ = true;
            }
        }
    });
    return SparseForwardDataFlowAnalysis::initialize(top);
}

} // namespace ctcompile::ctnative
