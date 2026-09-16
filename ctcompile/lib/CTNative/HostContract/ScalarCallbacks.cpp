#include "Analysis.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/StringSet.h"

namespace ctcompile::ctnative::host_detail {

bool analyzer::scalarCallbacks(llvm::ArrayRef<HostMethodParameters> family,
                               HostCapturedMap & result) {
    // ponytail: one fixed field and one String formal per callback; widen only
    // when a source witness needs additional slots or argument categories.
    llvm::DenseSet<mlir::Operation *> members;
    llvm::SmallVector<ctjs::LoadGlobalOp> candidates;
    llvm::StringSet<> bindings;
    for (const auto & method : family) {
        if (!step()) { return false; }
        members.insert(method.function);
        auto function = method.function;
        const auto walked = function.walk([&](ctjs::LoadGlobalOp load) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (load.getName() != "Map" && load.getName() != "Array" &&
                bindings.insert(load.getName()).second) {
                candidates.push_back(load);
            }
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
    }
    const auto ordinaryBinding = [&](llvm::StringRef name) {
        if (!step() || !ctjs::ordinaryKey(name) || name == "globalThis" || name == "undefined" ||
            name == "NaN" || name == "Infinity") {
            return false;
        }
        for (const auto * names : {&contract.initialIntrinsics, &contract.absentBindings,
                                   &contract.undefinedBindings, &contract.realmOwnDataProperties}) {
            for (const auto & reserved : *names) {
                if (!step() || name == reserved) { return false; }
            }
        }
        for (const auto & root : contract.roots) {
            if (!step() || root.binding == name) { return false; }
        }
        return true;
    };
    std::vector<HostScalarCallback> callbacks;
    for (ctjs::LoadGlobalOp candidate : candidates) {
        if (!ordinaryBinding(candidate.getName())) { return false; }
        for (const auto & observation : contract.observations) {
            if (!step() || observation == candidate.getName()) { return false; }
        }
        const auto & stores = globals[candidate.getName()];
        if (stores.size() != 1) { return false; }
        HostScalarCallback callback;
        callback.initialization = stores.front();
        callback.owner = callback.initialization.getValue().getDefiningOp<ctjs::CreateObjectOp>();
        if (!callback.owner || callback.owner->getParentOp() != entry ||
            callback.initialization->getParentOp() != entry ||
            !dominance.properlyDominates(callback.owner.getOperation(), callback.initialization) ||
            !before(callback.initialization, result.allocation)) {
            return false;
        }
        for (mlir::OpOperand & use : callback.owner.getResult().getUses()) {
            if (!step() || use.getOwner()->getParentOfType<ctjs::FuncOp>() != entry ||
                !dominance.dominates(callback.owner.getResult(), use.getOwner())) {
                return false;
            }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (use.getOwner() == callback.initialization && use.getOperandNumber() == 0) {
                continue;
            }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            if (!write || callback.write || use.getOperandNumber() != 0 ||
                write->getParentOp() != entry ||
                !ctjs::ordinaryKey(ctjs::constantKey(write.getKey())) ||
                !dominance.dominates(write.getKey(), write) ||
                !dominance.properlyDominates(write, callback.initialization)) {
                return false;
            }
            callback.write = write;
        }
        callback.closure = callback.write
                               ? callback.write.getValue().getDefiningOp<ctjs::CreateClosureOp>()
                               : ctjs::CreateClosureOp{};
        callback.function =
            callback.closure ? callable(callback.closure.getResult()) : ctjs::FuncOp{};
        if (!callback.function || callback.function == entry ||
            callback.function == result.allocation->getParentOfType<ctjs::FuncOp>() ||
            members.contains(callback.function) || callback.function->getParentOp() != module ||
            !llvm::hasSingleElement(callback.function.getBody()) ||
            callback.function->hasAttr("ctjs.skipped") ||
            callback.function.getUpvalueCount() != 0 ||
            callback.function.getBody().front().getNumArguments() != 4 ||
            callback.closure->getParentOp() != entry || !callback.closure.getUpvalues().empty() ||
            !dominance.properlyDominates(callback.closure.getOperation(), callback.write)) {
            return false;
        }
        auto enclosingThis = callback.closure.getEnclosingThis().getDefiningOp<ctjs::ConstantOp>();
        if (!enclosingThis || !llvm::isa<ctjs::UndefinedAttr>(enclosingThis.getValue()) ||
            (callback.closure.getEnclosingIndicesAttr() &&
             !callback.closure.getEnclosingIndicesAttr().empty())) {
            return false;
        }
        for (mlir::Value operand : callback.closure->getOperands()) {
            if (!step() || !dominance.dominates(operand, callback.closure)) { return false; }
        }
        for (mlir::OpOperand & use : callback.closure.getResult().getUses()) {
            if (!step() || use.getOwner()->getParentOfType<ctjs::FuncOp>() != entry ||
                !dominance.dominates(callback.closure.getResult(), use.getOwner()) ||
                (!llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                 (use.getOwner() != callback.write || use.getOperandNumber() != 2))) {
                return false;
            }
        }
        const auto loads = module.walk([&](ctjs::LoadGlobalOp load) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (load.getName() != candidate.getName()) { return mlir::WalkResult::advance(); }
            if (!members.contains(load->getParentOfType<ctjs::FuncOp>())) {
                return mlir::WalkResult::interrupt();
            }
            callback.loads.push_back(load);
            return mlir::WalkResult::advance();
        });
        if (loads.wasInterrupted()) { return false; }
        llvm::DenseSet<mlir::Operation *> reads, calls;
        for (ctjs::LoadGlobalOp load : callback.loads) {
            bool used = false;
            for (mlir::OpOperand & use : load.getResult().getUses()) {
                if (!step() ||
                    use.getOwner()->getParentOfType<ctjs::FuncOp>() !=
                        load->getParentOfType<ctjs::FuncOp>() ||
                    !dominance.dominates(load.getResult(), use.getOwner())) {
                    return false;
                }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                if (read && use.getOperandNumber() == 0 &&
                    ctjs::constantKey(read.getKey()) ==
                        ctjs::constantKey(callback.write.getKey()) &&
                    dominance.dominates(read.getKey(), read)) {
                    if (reads.insert(read).second) { callback.reads.push_back(read); }
                    used = true;
                    continue;
                }
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                if ((!call || use.getOperandNumber() != 1) &&
                    (!direct || use.getOperandNumber() != 0)) {
                    return false;
                }
                auto callee = call ? call.getCallee() : direct.getCalleeValue();
                auto property = callee.getDefiningOp<ctjs::GetPropertyOp>();
                const auto args = call ? call.getArgs() : direct.getArgs();
                if (!property || property.getObject() != load.getResult() || args.size() != 1 ||
                    ctjs::constantKey(property.getKey()) !=
                        ctjs::constantKey(callback.write.getKey()) ||
                    !dominance.dominates(callee, use.getOwner())) {
                    return false;
                }
                if (direct) {
                    auto newTarget = direct.getNewTarget().getDefiningOp<ctjs::ConstantOp>();
                    if (direct.getTarget() != callback.function || !newTarget ||
                        !llvm::isa<ctjs::UndefinedAttr>(newTarget.getValue())) {
                        return false;
                    }
                }
                for (mlir::Value operand : use.getOwner()->getOperands()) {
                    if (!step() || !dominance.dominates(operand, use.getOwner())) { return false; }
                }
                if (calls.insert(use.getOwner()).second) {
                    callback.calls.push_back(use.getOwner());
                }
            }
            if (!used) { return false; }
        }
        if (callback.calls.empty()) { return false; }
        for (ctjs::GetPropertyOp read : callback.reads) {
            for (mlir::OpOperand & use : read.getResult().getUses()) {
                if (!step() ||
                    use.getOwner()->getParentOfType<ctjs::FuncOp>() !=
                        read->getParentOfType<ctjs::FuncOp>() ||
                    !dominance.dominates(read.getResult(), use.getOwner())) {
                    return false;
                }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                if (!calls.contains(use.getOwner()) ||
                    use.getOperandNumber() != (llvm::isa<ctjs::CallOp>(use.getOwner()) ? 0u : 2u)) {
                    return false;
                }
            }
        }
        unsigned creations = 0;
        const auto identities = module.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
                made && callable(made.getResult()) == callback.function) {
                ++creations;
            }
            if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
                direct && direct.getTarget() == callback.function && !calls.contains(direct)) {
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
        if (identities.wasInterrupted() || creations != 1) { return false; }

        llvm::StringMap<unsigned> state;
        const auto names = callback.function.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            llvm::StringRef name;
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
                name = load.getName();
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
                name = store.getName();
            }
            if (name.empty()) { return mlir::WalkResult::advance(); }
            if (!ordinaryBinding(name) || name == candidate.getName()) {
                return mlir::WalkResult::interrupt();
            }
            if (state.try_emplace(name, static_cast<unsigned>(state.size())).second) {
                callback.globals.emplace_back();
            }
            return mlir::WalkResult::advance();
        });
        if (names.wasInterrupted()) { return false; }
        for (const auto & binding : state) {
            auto & scalar = callback.globals[binding.second];
            for (ctjs::StoreGlobalOp store : globals[binding.first()]) {
                if (!step()) { return false; }
                if (store->getParentOfType<ctjs::FuncOp>() == callback.function) {
                    scalar.writes.push_back(store);
                    continue;
                }
                auto initial = store.getValue().getDefiningOp<ctjs::ConstantOp>();
                if (scalar.initialization || store->getParentOp() != entry || !initial ||
                    !llvm::isa<ctjs::NumberAttr>(initial.getValue()) ||
                    !dominance.dominates(initial.getResult(), store) ||
                    !dominance.properlyDominates(store, callback.closure)) {
                    return false;
                }
                scalar.initialization = store;
            }
            if (!scalar.initialization || scalar.writes.empty()) { return false; }
            const auto scalarLoads = module.walk([&](ctjs::LoadGlobalOp load) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (load.getName() != binding.first()) { return mlir::WalkResult::advance(); }
                const auto function = load->getParentOfType<ctjs::FuncOp>();
                if (function != callback.function &&
                    (load->getParentOp() != entry ||
                     !dominance.properlyDominates(scalar.initialization, load))) {
                    return mlir::WalkResult::interrupt();
                }
                for (mlir::OpOperand & use : load.getResult().getUses()) {
                    if (!step() || use.getOwner()->getParentOfType<ctjs::FuncOp>() != function ||
                        !dominance.dominates(load.getResult(), use.getOwner())) {
                        return mlir::WalkResult::interrupt();
                    }
                }
                scalar.reads.push_back(load);
                return mlir::WalkResult::advance();
            });
            if (scalarLoads.wasInterrupted()) { return false; }
        }

        // Induction starts at the independent Number literals. Every store in
        // every arm must preserve Number; no selected invocation or value is used.
        auto & body = callback.function.getBody().front();
        llvm::DenseMap<mlir::Value, PrimitiveAlternatives> values;
        llvm::DenseSet<mlir::Value> flags;
        values[body.getArgument(3)] =
            PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::StringAttr>());
        ctjs::FrameEnterOp frame;
        bool frameExited = false;
        ctjs::ReturnOp returned;
        const auto walk = [&](auto && self, mlir::Block & block, unsigned depth) -> bool {
            if (!step() || depth > 32) { return false; }
            for (mlir::Operation & operation : block) {
                if (!step() || (frameExited && !llvm::isa<ctjs::ReturnOp>(operation))) {
                    return false;
                }
                for (mlir::Value operand : operation.getOperands()) {
                    if (!step() || !dominance.dominates(operand, &operation)) { return false; }
                }
                callback.operations.push_back(&operation);
                if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
                    if (!ctjs::isPrimitiveAttr(constant.getValue())) { return false; }
                    values[constant.getResult()] =
                        PrimitiveAlternatives::literal(constant.getValue()).categories();
                } else if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
                    if (!llvm::isa<mlir::IntegerAttr>(constant.getValue())) { return false; }
                    if (constant.getType().isInteger(1)) {
                        flags.insert(constant.getResult());
                    } else if (!constant.getResult().use_empty()) {
                        return false;
                    }
                } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
                    if (!state.contains(load.getName())) { return false; }
                    values[load.getResult()] =
                        PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>());
                } else if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
                    if (!state.contains(store.getName()) ||
                        values.lookup(store.getValue()).tag() !=
                            mlir::TypeID::get<ctjs::NumberAttr>()) {
                        return false;
                    }
                } else if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation)) {
                    const auto number = mlir::TypeID::get<ctjs::NumberAttr>();
                    if (binary.getKind() != ctjs::BinaryKind::Add ||
                        values.lookup(binary.getLhs()).tag() != number ||
                        values.lookup(binary.getRhs()).tag() != number) {
                        return false;
                    }
                    values[binary.getResult()] = PrimitiveAlternatives::forTag(number);
                } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
                    if (compare.getKind() != ctjs::CompareKind::StrictEq ||
                        !values.lookup(compare.getLhs()).known ||
                        !values.lookup(compare.getRhs()).known) {
                        return false;
                    }
                    values[compare.getResult()] =
                        PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
                } else if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
                    if (!values.lookup(truthy.getValue()).known) { return false; }
                    flags.insert(truthy.getResult());
                } else if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    if (!flags.contains(branch.getCondition()) ||
                        !llvm::hasSingleElement(branch.getThenRegion()) ||
                        (!branch.getElseRegion().empty() &&
                         !llvm::hasSingleElement(branch.getElseRegion())) ||
                        (branch.getNumResults() && branch.getElseRegion().empty())) {
                        return false;
                    }
                    for (mlir::Region & arm : branch->getRegions()) {
                        if (!step()) { return false; }
                        if (arm.empty()) { continue; }
                        if (!self(self, arm.front(), depth + 1)) { return false; }
                        auto yield =
                            llvm::dyn_cast<mlir::scf::YieldOp>(arm.front().getTerminator());
                        if (!yield || yield.getNumOperands() != branch.getNumResults()) {
                            return false;
                        }
                    }
                    for (unsigned index = 0; index < branch.getNumResults(); ++index) {
                        if (!step()) { return false; }
                        auto thenYield = llvm::cast<mlir::scf::YieldOp>(
                            branch.getThenRegion().front().getTerminator());
                        auto elseYield = llvm::cast<mlir::scf::YieldOp>(
                            branch.getElseRegion().front().getTerminator());
                        auto joined = values.lookup(thenYield.getOperand(index))
                                          .joined(values.lookup(elseYield.getOperand(index)));
                        if (!joined.known) { return false; }
                        values[branch.getResult(index)] = joined.categories();
                    }
                } else if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
                    if (depth == 0 || !llvm::isa<mlir::scf::IfOp>(yield->getParentOp()) ||
                        &operation != block.getTerminator()) {
                        return false;
                    }
                    for (mlir::Value value : yield.getOperands()) {
                        if (!step() || !values.lookup(value).known) { return false; }
                    }
                } else if (auto finish = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                    if (depth != 0 || returned || &operation != block.getTerminator() ||
                        (frame && !frameExited) ||
                        values.lookup(finish.getValue()).tag() !=
                            mlir::TypeID::get<ctjs::UndefinedAttr>()) {
                        return false;
                    }
                    returned = finish;
                } else if (auto entered = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                    if (depth != 0 || frame) { return false; }
                    frame = entered;
                } else if (auto exited = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
                    if (depth != 0 || !frame || exited.getContext() != frame.getContext()) {
                        return false;
                    }
                    frameExited = true;
                } else if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
                    if (!frame || root.getContext() != frame.getContext() ||
                        root->getNumOperands() != 2 || root->getNumResults() != 0 ||
                        !llvm::isa<ctjs::ValueType>(root->getOperand(1).getType())) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            return true;
        };
        if (!walk(walk, body, 0) || !returned) { return false; }
        for (mlir::BlockArgument argument : body.getArguments()) {
            for (mlir::OpOperand & use : argument.getUses()) {
                if (!step() || !dominance.dominates(argument, use.getOwner()) ||
                    use.getOwner()->getParentOfType<ctjs::FuncOp>() != callback.function ||
                    (argument.getArgNumber() < 3 &&
                     (!llvm::isa<ctjs::RootOp>(use.getOwner()) || use.getOperandNumber() != 1))) {
                    return false;
                }
            }
        }
        for (mlir::Operation * operation : callback.operations) {
            for (mlir::Value value : operation->getResults()) {
                for (mlir::OpOperand & use : value.getUses()) {
                    if (!step() || !dominance.dominates(value, use.getOwner()) ||
                        use.getOwner()->getParentOfType<ctjs::FuncOp>() != callback.function) {
                        return false;
                    }
                    if (operation == frame &&
                        (use.getOperandNumber() != 0 ||
                         !llvm::isa<ctjs::RootOp, ctjs::FrameExitOp>(use.getOwner()))) {
                        return false;
                    }
                }
            }
        }
        callbacks.push_back(std::move(callback));
    }
    if (exhausted) { return false; }
    result.scalarCallbacks = std::move(callbacks);
    return true;
}

} // namespace ctcompile::ctnative::host_detail
