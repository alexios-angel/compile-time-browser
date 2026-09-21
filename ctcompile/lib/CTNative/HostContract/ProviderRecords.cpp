#include "Analysis.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative::host_detail {

std::string analyzer::localRecordProblem() {
    if (contract.provider != HostContract::Provider::ctbrowserDOMDataSession || !entry ||
        !llvm::hasSingleElement(entry.getBody())) {
        return {};
    }
    // Reconstruct the prepared source graph. No preparation/report attribute,
    // native Map annotation or previously inferred category is evidence here.
    // ponytail: uncaptured literal/formal constructors and same-block String-key
    // Maps. Captured constructors and branch mutations need a wider owner proof.
    llvm::DenseSet<mlir::Operation *> operations, constructors, closures, maps;
    bool primitiveFields = true;
    llvm::DenseMap<mlir::Value, mlir::Value> origins;
    llvm::DenseMap<mlir::Value, mlir::Operation *> initializers;
    llvm::DenseMap<mlir::Value, llvm::StringMap<PrimitiveAlternatives>> fields;
    llvm::DenseMap<mlir::Value, llvm::StringMap<mlir::Value>> entries;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> scalars;
    const auto reject = [&] {
        return exhausted ? "host contract analysis work budget exhausted"
                         : "provider local constructor/Map graph is not closed";
    };
    const auto literal = [](mlir::Value value) -> mlir::Attribute {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && ctjs::isPrimitiveAttr(constant.getValue()) ? constant.getValue()
                                                                      : mlir::Attribute{};
    };
    for (mlir::Operation & operation : entry.getBody().front()) {
        if (!step()) { return reject(); }
        auto made = llvm::dyn_cast<ctjs::ConstructOp>(operation);
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
        auto instance = direct ? direct.getReceiver().getDefiningOp<ctjs::CreateObjectOp>()
                               : ctjs::CreateObjectOp{};
        auto callee = made ? made.getCallee() : instance ? direct.getCalleeValue() : mlir::Value{};
        auto closure =
            callee ? callee.getDefiningOp<ctjs::CreateClosureOp>() : ctjs::CreateClosureOp{};
        if (!closure) { continue; }
        const auto owner = made ? made.getResult() : instance.getResult();
        const auto actuals = made ? made.getArgs() : direct.getArgs();
        auto function = callable(closure.getResult());
        if (!function || function == entry || function->hasAttr("ctjs.skipped") ||
            !llvm::hasSingleElement(function.getBody()) || function.getUpvalueCount() != 0 ||
            !closure.getUpvalues().empty() ||
            !llvm::isa_and_nonnull<ctjs::UndefinedAttr>(literal(closure.getEnclosingThis())) ||
            closure->getParentOp() != entry ||
            !dominance.properlyDominates(closure.getOperation(), &operation)) {
            return reject();
        }
        if (made) {
            if (made.getNewTarget() != closure.getResult()) { return reject(); }
        } else {
            // The emitted initializer is an ordinary call on its exact fresh
            // receiver. Neither native markers nor the original proof can
            // supply a callable identity or move initialization before a read.
            if (!exactCall(direct) || instance->getParentOp() != entry ||
                !dominance.properlyDominates(instance.getOperation(), direct) ||
                !llvm::isa_and_nonnull<ctjs::UndefinedAttr>(literal(direct.getNewTarget())) ||
                !initializers.try_emplace(owner, direct.getOperation()).second) {
                return reject();
            }
            for (mlir::Operation * user : direct.getResult().getUsers()) {
                if (!step() || !llvm::isa<ctjs::RootOp>(user)) { return reject(); }
            }
            operations.insert(instance);
        }
        auto & body = function.getBody().front();
        if (body.getNumArguments() != ctjs::implicit_arguments + actuals.size()) {
            return reject();
        }
        for (mlir::Value actual : actuals) {
            if (!step() || !literal(actual) || !dominance.dominates(actual, &operation)) {
                return reject();
            }
        }
        for (unsigned index : {ctjs::arg_new_target, ctjs::arg_callee}) {
            for (mlir::Operation * user : body.getArgument(index).getUsers()) {
                if (!step() || !llvm::isa<ctjs::RootOp>(user)) { return reject(); }
            }
        }
        bool returned = false;
        for (mlir::Operation & op : body) {
            if (!step() || op.hasAttr("ctjs.skipped")) { return reject(); }
            for (mlir::Value operand : op.getOperands()) {
                if (!step() || !dominance.dominates(operand, &op)) { return reject(); }
            }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
                if (write.getObject() != body.getArgument(ctjs::arg_receiver) ||
                    !ctjs::ordinaryKey(write.getKey())) {
                    return reject();
                }
                mlir::Value value = write.getValue();
                if (auto formal = llvm::dyn_cast<mlir::BlockArgument>(value)) {
                    if (formal.getOwner() != &body ||
                        formal.getArgNumber() < ctjs::implicit_arguments) {
                        return reject();
                    }
                    value = actuals[formal.getArgNumber() - ctjs::implicit_arguments];
                }
                auto initial = literal(value);
                if (!initial) { return reject(); }
                fields[owner][ctjs::constantKey(write.getKey())] =
                    PrimitiveAlternatives::forTag(initial.getTypeID());
            } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
                if (!literal(result.getValue())) { return reject(); }
                returned = true;
            } else if (!llvm::isa<ctjs::ConstantOp, ctjs::RootOp, ctjs::FrameEnterOp,
                                  ctjs::FrameExitOp>(op)) {
                return reject();
            }
            operations.insert(&op);
        }
        if (!returned) { return reject(); }
        origins[owner] = owner;
        constructors.insert(function);
        closures.insert(closure);
        operations.insert(closure);
        operations.insert(&operation);
    }
    if (constructors.empty()) { return exhausted ? reject() : std::string{}; }
    const auto census = module.walk([&](ctjs::CreateClosureOp closure) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (!constructors.contains(callable(closure.getResult()))) {
            return mlir::WalkResult::advance();
        }
        if (!closures.contains(closure)) { return mlir::WalkResult::interrupt(); }
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                direct && use.getOperandNumber() == 2 &&
                initializers.lookup(direct.getReceiver()) == direct.getOperation()) {
                continue;
            }
            auto made = llvm::dyn_cast<ctjs::ConstructOp>(use.getOwner());
            if (!made || !operations.contains(made) || use.getOperandNumber() >= 2 ||
                made.getCallee() != closure.getResult() ||
                made.getNewTarget() != closure.getResult()) {
                return mlir::WalkResult::interrupt();
            }
        }
        return mlir::WalkResult::advance();
    });
    if (census.wasInterrupted()) { return reject(); }
    for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                              mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
        if (!uses) { return reject(); }
        for (const auto & use : *uses) {
            if (!step()) { return reject(); }
            if (constructors.contains(mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                    use.getUser(), use.getSymbolRef()))) {
                auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                if (!direct || initializers.lookup(direct.getReceiver()) != direct.getOperation()) {
                    return reject();
                }
            }
        }
    }
    // Preparation keeps retired holder/prototype allocations in the source.
    // Only an allocation with no observable use belongs to this local graph.
    for (ctjs::CreateObjectOp made : entry.getBody().front().getOps<ctjs::CreateObjectOp>()) {
        if (!step()) { return reject(); }
        bool inert = true;
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            if (!step()) { return reject(); }
            inert &= use.getOperandNumber() == 0 && llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                     use.getOwner()->getParentOfType<ctjs::FuncOp>() == entry &&
                     dominance.dominates(made.getResult(), use.getOwner());
        }
        if (inert) { operations.insert(made); }
    }
    for (ctjs::ConstructOp made : entry.getBody().front().getOps<ctjs::ConstructOp>()) {
        if (!step()) { return reject(); }
        auto intrinsic = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
        if (!intrinsic || intrinsic.getName() != "Map") { continue; }
        if (!llvm::is_contained(contract.initialIntrinsics, "Map") ||
            made.getNewTarget() != made.getCallee() || !made.getArgs().empty() ||
            intrinsic->getParentOp() != entry ||
            !dominance.properlyDominates(intrinsic.getOperation(), made)) {
            return reject();
        }
        maps.insert(made);
        operations.insert(made);
        operations.insert(intrinsic);
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            if (!step() || use.getOwner()->getParentOp() != entry ||
                !dominance.dominates(made.getResult(), use.getOwner())) {
                return reject();
            }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!read || use.getOperandNumber() != 0) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                auto method = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                   : ctjs::GetPropertyOp{};
                if (call && use.getOperandNumber() == 1 && method &&
                    method.getObject() == made.getResult()) {
                    continue;
                }
                return reject();
            }
            operations.insert(read);
            const auto key = ctjs::constantKey(read.getKey());
            if (key == "size") { continue; }
            static const llvm::StringMap<unsigned> arities{
                {"set", 2}, {"get", 1}, {"has", 1}, {"delete", 1}, {"clear", 0}};
            auto method = key.size() <= 6 ? arities.find(key) : arities.end();
            if (method == arities.end()) { return reject(); }
            for (mlir::OpOperand & selected : read.getResult().getUses()) {
                if (!step()) { return reject(); }
                if (llvm::isa<ctjs::RootOp>(selected.getOwner())) { continue; }
                auto call = llvm::dyn_cast<ctjs::CallOp>(selected.getOwner());
                if (!call || selected.getOperandNumber() != 0 || call->getParentOp() != entry ||
                    call.getReceiver() != made.getResult() ||
                    call.getArgs().size() != method->second ||
                    !dominance.properlyDominates(read.getOperation(), call)) {
                    return reject();
                }
                for (mlir::Value argument : call.getArgs()) {
                    if (!step() || !dominance.dominates(argument, call)) { return reject(); }
                }
                if (key == "set") {
                    for (mlir::Operation * user : call->getUsers()) {
                        if (!step() || !llvm::isa<ctjs::RootOp>(user)) { return reject(); }
                    }
                }
                operations.insert(call);
            }
        }
    }
    const auto category = [&](mlir::Value value, mlir::Operation * consumer) {
        std::vector<mlir::Value> dependencies;
        auto result = entryCategories(value, scalars, consumer, 0, &dependencies);
        for (mlir::Value dependency : dependencies) {
            if (!step() || !scalars.contains(dependency)) { return PrimitiveAlternatives{}; }
        }
        return result;
    };
    // Postorder visits both read-only arms before their scalar join. All Map
    // operations and record writes are required to be in the entry block.
    const auto ordered = entry.walk([&](mlir::Operation * op) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op); call && operations.contains(call)) {
            auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            const auto action = ctjs::constantKey(method.getKey());
            auto & state = entries[call.getReceiver()];
            if (action == "clear") {
                state.clear();
                scalars[call.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::UndefinedAttr>());
                return mlir::WalkResult::advance();
            }
            auto key = llvm::dyn_cast_or_null<ctjs::StringAttr>(literal(call.getArgs().front()));
            if (!key) { return mlir::WalkResult::interrupt(); }
            if (action == "set") {
                auto owner = origins.lookup(call.getArgs()[1]);
                if (!owner) { return mlir::WalkResult::interrupt(); }
                state[key.getValue()] = owner;
            } else if (action == "get") {
                auto owner = state.lookup(key.getValue());
                if (!owner) { return mlir::WalkResult::interrupt(); }
                origins[call.getResult()] = owner;
            } else {
                if (action == "delete") { state.erase(key.getValue()); }
                scalars[call.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
            }
        } else if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
            if (auto owner = origins.lookup(write.getObject())) {
                if (write->getParentOp() != entry || !ctjs::ordinaryKey(write.getKey())) {
                    return mlir::WalkResult::interrupt();
                }
                const auto value = category(write.getValue(), op);
                primitiveFields &= value.known;
                fields[owner][ctjs::constantKey(write.getKey())] = value;
                operations.insert(write);
            }
        } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            auto owner = origins.lookup(read.getObject());
            if (owner) {
                if (!ctjs::ordinaryKey(read.getKey())) { return mlir::WalkResult::interrupt(); }
                auto value = fields[owner].lookup(ctjs::constantKey(read.getKey()));
                if (!value.known) { return mlir::WalkResult::interrupt(); }
                scalars[read.getResult()] = value;
                operations.insert(read);
            } else if (maps.contains(read.getObject().getDefiningOp()) &&
                       ctjs::constantKey(read.getKey()) == "size") {
                scalars[read.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>());
            }
        } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
            if (compare.getKind() == ctjs::CompareKind::StrictEq &&
                category(compare.getLhs(), op).known && category(compare.getRhs(), op).known) {
                scalars[compare.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
            }
        } else if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op)) {
            switch (binary.getKind()) {
            case ctjs::BinaryKind::Add:
            case ctjs::BinaryKind::Sub:
            case ctjs::BinaryKind::Mul:
            case ctjs::BinaryKind::Div:
            case ctjs::BinaryKind::Mod:
            case ctjs::BinaryKind::Pow: break;
            default: return mlir::WalkResult::advance();
            }
            const auto numeric = [&](mlir::Value value) {
                auto tag = category(value, op).tag();
                return tag == mlir::TypeID::get<ctjs::NumberAttr>() ||
                       tag == mlir::TypeID::get<ctjs::BooleanAttr>();
            };
            if (numeric(binary.getLhs()) && numeric(binary.getRhs())) {
                scalars[binary.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>());
                operations.insert(binary);
            }
        }
        return exhausted ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
    });
    if (ordered.wasInterrupted()) { return reject(); }
    for (auto [alias, owner] : origins) {
        const auto initializer = initializers.lookup(owner);
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!step() || use.getOwner()->getParentOfType<ctjs::FuncOp>() != entry ||
                !dominance.dominates(alias, use.getOwner())) {
                return reject();
            }
            auto * op = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            if (initializer && op != initializer && !dominance.properlyDominates(initializer, op)) {
                return reject();
            }
            if (!operations.contains(op)) { return reject(); }
            if (op == initializer && alias == owner && use.getOperandNumber() == 0) { continue; }
            if (use.getOperandNumber() == 0 &&
                llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(op)) {
                continue;
            }
            auto call = llvm::dyn_cast<ctjs::CallOp>(op);
            if (!call || use.getOperandNumber() != 3 ||
                ctjs::constantKey(call.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey()) !=
                    "set") {
                return reject();
            }
        }
    }
    for (mlir::Operation * op : operations) {
        if (!step() || op->hasAttr("ctjs.skipped")) { return reject(); }
        for (mlir::Value operand : op->getOperands()) {
            if (!step() || !dominance.dominates(operand, op)) { return reject(); }
        }
    }
    // Commit only a complete source proof. Categories never select a branch,
    // replace an allocation/read, or establish its native storage lifetime.
    for (auto [value, result] : scalars) {
        if (!step()) { return reject(); }
        classScalarReads[value] = result;
    }
    for (mlir::Operation * op : operations) {
        if (!step()) { return reject(); }
        capturedOperations.insert(op);
        localRecords.operations.push_back(op);
    }
    for (mlir::Operation * function : constructors) {
        if (!step()) { return reject(); }
        localRecords.constructors.push_back(llvm::cast<ctjs::FuncOp>(function));
    }
    localRecords.primitiveFields = primitiveFields;
    return {};
}

} // namespace ctcompile::ctnative::host_detail
