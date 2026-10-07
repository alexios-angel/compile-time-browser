#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

bool classInitialization::retainBooleanCaptures(ctjs::FuncOp owner) {
    auto & block = owner.getBody().front();
    ctjs::CreateCellOp cell;
    for (auto local : block.getOps<ctjs::CreateCellOp>()) {
        if (!step()) { return false; }
        bool written = false;
        for (mlir::OpOperand & use : local.getResult().getUses()) {
            if (!step()) { return false; }
            auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
            auto function = target(closure);
            if (!function || use.getOperandNumber() < 2) { continue; }
            for (mlir::Operation * user :
                 function.getBody().front().getArgument(ctjs::arg_callee).getUsers()) {
                if (!step()) { return false; }
                auto store = llvm::dyn_cast<ctjs::StoreUpvalueOp>(user);
                written |= store && store.getIndex() >= 0 &&
                           static_cast<unsigned>(store.getIndex()) == use.getOperandNumber() - 2;
            }
        }
        if (!written) { continue; }
        if (cell && cell != local) { return refuse("class Boolean state requires one local cell"); }
        cell = local;
    }
    if (!cell || booleanCells.contains(cell.getResult())) { return true; }
    if (cells.count(cell.getResult())) {
        return refuse("class Boolean state was already treated as a fixed snapshot");
    }
    llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>> callbacks;
    llvm::DenseSet<mlir::Operation *> callbackOperations;
    llvm::SmallVector<ctjs::RootOp> roots;
    const bool initiallySet = !undefined(cell.getInitial());
    mlir::Block * stateScope = initiallySet ? &block : nullptr;
    const auto orderedUse = [&](mlir::Operation * creator, mlir::Operation * user,
                                llvm::DenseSet<mlir::Block *> * ancestors = nullptr) {
        auto * scope = creator->getBlock();
        auto * position = user;
        if (ancestors) { ancestors->insert(position->getBlock()); }
        unsigned nesting = 0;
        while (position->getBlock() != scope) {
            if (!step()) { return false; }
            auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(position->getParentOp());
            if (!branch || ++nesting == 64) {
                return refuse("class Boolean state requires bounded local conditional uses");
            }
            position = branch;
            if (ancestors) { ancestors->insert(position->getBlock()); }
        }
        if (!creator->isBeforeInBlock(position)) {
            return refuse("class Boolean state requires creation before its conditional uses");
        }
        return true;
    };
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (!step()) { return false; }
        auto * user = use.getOwner();
        llvm::DenseSet<mlir::Block *> ancestors;
        if (!orderedUse(cell, user, &ancestors)) { return false; }
        if (auto root = llvm::dyn_cast<ctjs::RootOp>(user); root && use.getOperandNumber() == 1) {
            roots.push_back(root);
            continue;
        }
        // Like the later DOM state proof, include every write and capture in
        // the common scope. Roots keep their frame obligation, not a lifetime.
        if (!stateScope) { stateScope = user->getBlock(); }
        while (!ancestors.contains(stateScope)) {
            if (!step()) { return false; }
            stateScope = stateScope->getParentOp()->getBlock();
        }
        if (llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) {
            continue;
        }
        auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
        auto function = target(closure);
        if (!closure || use.getOperandNumber() < 2 || !function || function == owner ||
            function.getUpvalueCount() != static_cast<int64_t>(closure.getUpvalues().size()) ||
            !function.getBody().hasOneBlock() ||
            function.getBody().front().getNumArguments() != ctjs::implicit_arguments ||
            closure.getEnclosingClosure() != block.getArgument(ctjs::arg_callee) ||
            !callbackOperations.insert(closure).second) {
            return refuse("class Boolean state requires exact local leaf capture slots");
        }
        const unsigned mutableSlot = use.getOperandNumber() - 2;
        // Only fixed captures may follow an enclosing slot. Keep the actual
        // local mutable identity; callbackPrimitive proves every other slot.
        if (auto indices = closure.getEnclosingIndicesAttr();
            indices && indices[mutableSlot] != -1) {
            return refuse("class Boolean state requires a local mutable capture");
        }
        callbacks.emplace_back(closure, mutableSlot);
    }
    llvm::DenseSet<mlir::Operation *> leaves, calls;
    for (auto [closure, mutableSlot] : callbacks) {
        if (!step()) { return false; }
        auto function = target(closure);
        leaves.insert(function);
        auto & body = function.getBody().front();
        if (!body.getArgument(ctjs::arg_receiver).use_empty() ||
            !body.getArgument(ctjs::arg_new_target).use_empty()) {
            return refuse("class Boolean leaf observes its receiver or new.target");
        }
        const auto callee = body.getArgument(ctjs::arg_callee);
        for (mlir::OpOperand & use : callee.getUses()) {
            if (!step()) { return false; }
            auto * user = use.getOwner();
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(user);
                root && use.getOperandNumber() == 1) {
                roots.push_back(root);
                continue;
            }
            if (!llvm::isa<ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(user) ||
                use.getOperandNumber() != 0) {
                return refuse("class Boolean leaf observes its callee identity");
            }
        }
        // This transports non-reentrant primitive leaves. The later DOM proof
        // establishes actual Boolean conditions, stores and result types.
        const auto proveLeaf = [&](auto && self, mlir::Block & source, unsigned nesting) -> bool {
            if (nesting == 64) { return refuse("class Boolean leaf conditional depth exhausted"); }
            for (auto & operation : source) {
                if (!spend(cloneCost(&operation))) { return false; }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    if (!branch.getCondition().getDefiningOp<ctjs::TruthyOp>() ||
                        !llvm::all_of(branch.getResultTypes(), [](mlir::Type type) {
                            return llvm::isa<ctjs::ValueType>(type);
                        })) {
                        return refuse("class Boolean leaf requires a value conditional");
                    }
                    for (auto & region : branch->getRegions()) {
                        if (!step()) { return false; }
                        if (region.empty() && branch.getNumResults() == 0) { continue; }
                        if (!region.hasOneBlock() || region.front().empty() ||
                            region.front().getNumArguments()) {
                            return refuse("class Boolean leaf requires argument-free arms");
                        }
                        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().back());
                        if (!yield || yield.getOperandTypes() != branch.getResultTypes()) {
                            return refuse("class Boolean leaf requires matching arm results");
                        }
                        if (!self(self, region.front(), nesting + 1)) { return false; }
                    }
                    continue;
                }
                if (operation.getNumRegions() || operation.getNumSuccessors()) {
                    return refuse("class Boolean callback requires an If-only leaf body");
                }
                bool accepted =
                    llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, ctjs::TruthyOp>(
                        operation);
                if (llvm::isa<ctjs::ReturnOp>(operation)) { accepted = nesting == 0; }
                if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
                    accepted = nesting != 0;
                }
                if (auto literal = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
                    accepted = ctjs::isPrimitiveAttr(literal.getValue());
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                    accepted = load.getClosure() == callee && capturesSlot(load, closure);
                }
                if (auto store = llvm::dyn_cast<ctjs::StoreUpvalueOp>(operation)) {
                    accepted = store.getClosure() == callee && store.getIndex() >= 0 &&
                               static_cast<unsigned>(store.getIndex()) == mutableSlot;
                }
                if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
                    accepted = unary.getKind() == ctjs::UnaryKind::Not;
                }
                if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
                    accepted = compare.getKind() == ctjs::CompareKind::StrictEq;
                }
                if (!accepted) { return refuse("class Boolean leaf has an unproved operation"); }
            }
            return true;
        };
        if (!proveLeaf(proveLeaf, body, 0)) { return false; }
        bool called = false;
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            if (!step()) { return false; }
            auto * user = use.getOwner();
            if (!orderedUse(closure, user)) { return false; }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(user);
                root && use.getOperandNumber() == 1) {
                roots.push_back(root);
                continue;
            }
            auto call = host_detail::jsCallOf(user);
            if (!call || use.getOperandNumber() != (call->direct ? 2u : 0u) ||
                !call->args.empty() || !undefined(call->receiver) ||
                (call->direct && (call->direct.getTarget() != function ||
                                  !undefined(call->direct.getNewTarget())))) {
                return refuse("class Boolean leaf escapes its exact synchronous calls");
            }
            calls.insert(user);
            called = true;
        }
        if (!called) { return refuse("class Boolean leaf lacks an original local call"); }
    }
    llvm::DenseSet<mlir::Operation *> stateBranches;
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (!step()) { return false; }
        auto * position = use.getOwner();
        if (llvm::isa<ctjs::RootOp>(position)) { continue; }
        while (position->getBlock() != stateScope) {
            if (!step()) { return false; }
            position = position->getParentOp();
            stateBranches.insert(position);
        }
    }
    // A store must initialize the complete common scope before any read,
    // capture or deeper state-bearing If. Nested stores cannot initialize
    // siblings or a join. Callback uses already stay within their creator's
    // dominated arm, so no borrowed state outlives its capture here.
    bool initialized = initiallySet;
    for (auto & operation : *stateScope) {
        if (!step()) { return false; }
        if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(operation);
            write && write.getCell() == cell.getResult()) {
            initialized = true;
        }
        auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation);
        if (!initialized &&
            ((read && read.getCell() == cell.getResult()) ||
             callbackOperations.contains(&operation) || stateBranches.contains(&operation))) {
            return refuse("class Boolean state is used before its scope initialization");
        }
    }
    // The module's initial census already enforces unique closure creators.
    // Reject symbolic callers/attributes not selected through those creators.
    if (!spendWalk(module, declarationCost)) { return false; }
    if (auto stopped = host_detail::forEachSymbolUse(
            module, true, [&] { return step(); },
            [&](const mlir::SymbolTable::SymbolUse & use,
                ctjs::FuncOp function) -> std::optional<bool> {
                if (!leaves.contains(function)) { return std::nullopt; }
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                if (!call || call.getTarget() != function || !calls.contains(call)) {
                    return false;
                }
                return std::nullopt;
            })) {
        return refuse("class Boolean leaf has an unproved symbolic caller");
    }
    if (!spend(uint64_t(3) * (roots.size() + leaves.size()) + 1)) { return false; }
    // Complete the original mutable-cell/body/caller census before invoking
    // callbackPrimitive: its owner-cell proof must never freeze this cell.
    // Failure still discards the complete disposable module and proof state.
    booleanCells.insert(cell.getResult());
    llvm::SmallVector<llvm::SmallVector<ctjs::ConstantOp>> fixed;
    for (auto [closure, mutableSlot] : callbacks) {
        auto & literals = fixed.emplace_back(closure.getUpvalues().size());
        for (unsigned slot = 0; slot < literals.size(); ++slot) {
            if (!step()) { return false; }
            if (slot == mutableSlot) { continue; }
            auto literal = callbackPrimitive(closure, slot);
            if (!literal || !llvm::isa<ctjs::BooleanAttr>(literal.getValue())) {
                return refuse("class Boolean leaf requires fixed Boolean captures");
            }
            literals[slot] = literal;
        }
    }
    // Resolve every original fixed slot, including dormant sibling uses,
    // before deleting any read or compacting another callback's slot table.
    for (auto [index, callback] : llvm::enumerate(callbacks)) {
        auto [closure, mutableSlot] = callback;
        auto function = target(closure);
        const auto callee = function.getBody().front().getArgument(ctjs::arg_callee);
        for (mlir::OpOperand & use : llvm::make_early_inc_range(callee.getUses())) {
            if (!step()) { return false; }
            if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner())) {
                if (static_cast<unsigned>(load.getIndex()) == mutableSlot) {
                    load.setIndex(0);
                } else if (!copyPrimitiveCapture(
                               load, fixed[index][static_cast<size_t>(load.getIndex())])) {
                    return false;
                }
            } else if (auto store = llvm::dyn_cast<ctjs::StoreUpvalueOp>(use.getOwner())) {
                store.setIndex(0);
            }
        }
        setCaptures(closure, function, cell.getResult());
    }
    // Roots of cell/closure/implicit-callee identities would otherwise obstruct
    // lifting or disappear with a lifted closure. Preserve every original
    // position and context for the later complete frame proof.
    for (auto root : roots) {
        mlir::OpBuilder at(root);
        root.getValueMutable().assign(undefinedAt(at, root.getLoc()).getResult());
    }
    for (auto * leaf : leaves) {
        booleanLeaves.insert(leaf);
        helpers.insert(leaf);
        domEntryHelpers.insert(leaf);
    }
    needsDOMMethodProof = true;
    return true;
}

} // namespace ctcompile::ctnative::class_detail
