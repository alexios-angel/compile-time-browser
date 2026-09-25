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
                const auto tag = proof.getValue();
                auto * context = call.getContext();
                if (tag == "bool") { exact = BoolType::get(context); }
                if (tag == "number") { exact = NumType::getDouble(context); }
                if (tag == "string") { exact = StrType::get(context, StrEncoding::UTF8); }
            }
            mapKeys_[group].push_back({call.getArgs()[0], exact});
        }
        if (action == "set") { mapValues_[group].push_back(call.getArgs()[1]); }
    });
    // THE FIELD INDEX IS OVER THE GROUP, NOT OVER ONE VALUE, and that is the
    // whole of what a receiver parameter costs this analysis. `this.x = 5`
    // inside a lifted method is a store the CALLER's `o.x` has to see, and
    // `this.x` inside it is a read of the store the caller made - two values,
    // `%arg0` and the literal, naming one object. Every member of a group gets
    // every store made through any of them; a literal no method is lifted onto
    // is a group of one, which is the row this file had before.
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
        // PHASE 59 SLICE 2 STEP 3, THE FIELD HALF: the store SITE, on its own
        // object and not on the group's. Beside the value index rather than in
        // a second walk, so the two cannot disagree about which stores exist -
        // they differ only in what they are keyed on, and the header says why.
        fieldStoreSites_[{store.getObject(), key}].push_back(store.getOperation());
        const auto group = groups.find(store.getObject());
        if (group == groups.end()) {
            fieldStores_[{store.getObject(), key}].push_back(store.getValue());
            return;
        }
        for (mlir::Value member : group->second) {
            fieldStores_[{member, key}].push_back(store.getValue());
        }
    });
    top->walk([&](ctjs::GetPropertyOp read) {
        if (nativeObjectFieldGroup(read) >= 0 && queryNativeObjectFieldPresence(read).assigned) {
            assignedIdentityFields_.insert(read);
        }
    });
    if (auto module = llvm::dyn_cast<mlir::ModuleOp>(top)) {
        top->walk([&](ctjs::FuncOp function) {
            if (function.getBody().empty() || mlir::SymbolTable::getSymbolVisibility(function) !=
                                                  mlir::SymbolTable::Visibility::Private) {
                return;
            }
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
                // original stores until emission. Recheck that EVERY numeric
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
            // Numeric closure identities are checked above; symbol uses must
            // also be a complete census of ordinary direct calls. Each slot
            // retains its actual operands, never the receiver schema group.
            const auto uses = mlir::SymbolTable::getSymbolUses(function, module);
            if (!uses) { return; }
            llvm::SmallVector<mlir::Operation *, 2> callers;
            for (const auto & use : *uses) {
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                if (!call || call.getTarget() != function ||
                    call->getNumOperands() != function.getBody().front().getNumArguments()) {
                    return;
                }
                callers.push_back(call);
            }
            if (callers.empty()) { return; }
            for (mlir::Value parameter : parameters) {
                objectParameterCallSites_[parameter] = callers;
            }
        });
    }
    // THE APPENDS INDEX, beside fieldStores_ and for the same reason: an
    // element read has to find every value appended or stored into the array, and
    // walking the uses at each read would be the same walk done once per read.
    // A literal's own inline elements come first - the importer emits an empty
    // `create_array` and one `append` per element, but the operation carries
    // them and a lowering that ignored them would drop values.
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
        // Every selected root has one numeric storage schema, including writes
        // through another result in the connected ownership group.
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
    // THE SHARED-BINDING INDEX, over the group and not over one value - the
    // reason the field index is, one operand along. A `ctjs.cell_set` through
    // a capture pointer is in a DIFFERENT ctjs.func from the box, and the
    // owning frame's read has to see it: `var n = 0; function tick() { n = n +
    // 1; }` writes a double from inside `tick` into a box the frame built
    // holding `undefined`, and a rule that indexed only the frame's own store
    // would report `opt<i32>` for a binding that holds a double.
    const auto cells = groupCells(top);
    top->walk([&](ctjs::CreateCellOp cell) {
        if (!cell->hasAttr("ctnative.carried")) { return; }
        // PHASE 59 SLICE 2 STEP 3: AND THE INITIAL IS LEFT OUT WHEN NO READ CAN
        // SEE IT. The lift proved one `ctjs.cell_set` of this box properly
        // dominates every `ctjs.cell_get` of it AND every call of every closure
        // that captured it, so from that store onwards the binding holds a
        // stored value on every path that reaches any read - and the hoisted
        // `undefined` the box was built with is unreachable. Dropping it here
        // rather than in `cellTypeOf` keeps the join in one place: the type is
        // still "everything this binding can hold", over a set the lift made
        // one element smaller. `kAssignedBeforeRead` says what the lift proved.
        if (cell->hasAttr(kAssignedBeforeRead)) { return; }
        const auto group = cells.find(cell.getResult());
        if (group == cells.end()) {
            cellStores_[cell.getResult()].push_back(cell.getInitial());
            return;
        }
        for (mlir::Value member : group->second) {
            cellStores_[member].push_back(cell.getInitial());
        }
    });
    top->walk([&](ctjs::CellSetOp store) {
        if (!namesACarriedCell(store.getCell())) { return; }
        const auto group = cells.find(store.getCell());
        if (group == cells.end()) {
            cellStores_[store.getCell()].push_back(store.getValue());
            return;
        }
        for (mlir::Value member : group->second) {
            cellStores_[member].push_back(store.getValue());
        }
    });
    top->walk([&](mlir::Operation * op) {
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
            globalStores_[store.getName()].push_back(store.getValue());
            return;
        }
        // A PROPERTY WRITE THROUGH THE GLOBAL OBJECT is a write to the globals
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
