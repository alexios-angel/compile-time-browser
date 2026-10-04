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
    llvm::SmallVector<ctjs::CreateClosureOp> callbacks;
    llvm::DenseSet<mlir::Operation *> callbackOperations;
    llvm::DenseSet<mlir::Operation *> stateBranches;
    llvm::SmallVector<ctjs::RootOp> roots;
    const auto orderedUse = [&](mlir::Operation * creator, mlir::Operation * user) {
        auto * scope = creator->getBlock();
        auto * position = user;
        unsigned nesting = 0;
        while (position->getBlock() != scope) {
            if (!step()) { return false; }
            auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(position->getParentOp());
            if (!branch || ++nesting == 64) {
                return refuse("class Boolean state requires bounded local conditional uses");
            }
            position = branch;
        }
        if (!creator->isBeforeInBlock(position)) {
            return refuse("class Boolean state requires creation before its conditional uses");
        }
        // Cell uses reach the owner block; a callback's own users must stay
        // within its creation arm. The capture already records that outer If.
        if (scope == &block && position != user) { stateBranches.insert(position); }
        return true;
    };
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (!step()) { return false; }
        auto * user = use.getOwner();
        if (!orderedUse(cell, user)) { return false; }
        if (auto root = llvm::dyn_cast<ctjs::RootOp>(user); root && use.getOperandNumber() == 1) {
            roots.push_back(root);
            continue;
        }
        if (llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) {
            continue;
        }
        auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
        auto function = target(closure);
        if (!closure || use.getOperandNumber() != 2 || closure.getUpvalues().size() != 1 ||
            hasOuterCapture(closure) || !function || function == owner ||
            function.getUpvalueCount() != 1 || !function.getBody().hasOneBlock() ||
            function.getBody().front().getNumArguments() != ctjs::implicit_arguments ||
            closure.getEnclosingClosure() != block.getArgument(ctjs::arg_callee)) {
            return refuse("class Boolean state requires exact local leaf capture slots");
        }
        callbacks.push_back(closure);
        callbackOperations.insert(closure);
    }
    llvm::DenseSet<mlir::Operation *> leaves, calls;
    for (auto closure : callbacks) {
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
        // This is transport of non-reentrant primitive leaves. The later DOM
        // proof establishes actual Boolean origins, stores and result types.
        for (auto & operation : body) {
            if (!spend(cloneCost(&operation))) { return false; }
            if (operation.getNumRegions() || operation.getNumSuccessors()) {
                return refuse("class Boolean callback requires a flat leaf body");
            }
            bool accepted =
                llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, ctjs::ReturnOp>(
                    operation);
            if (auto literal = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
                accepted = ctjs::isPrimitiveAttr(literal.getValue());
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                accepted = load.getClosure() == callee && load.getIndex() == 0;
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreUpvalueOp>(operation)) {
                accepted = store.getClosure() == callee && store.getIndex() == 0;
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
                accepted = unary.getKind() == ctjs::UnaryKind::Not;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
                accepted = compare.getKind() == ctjs::CompareKind::StrictEq;
            }
            if (!accepted) { return refuse("class Boolean leaf has an unproved operation"); }
        }
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
    // Only owner-block stores initialize this proof. A store in an arm cannot
    // authorize another arm, a later join, or callback creation before the If.
    bool initialized = !undefined(cell.getInitial());
    for (auto & operation : block) {
        if (!step()) { return false; }
        if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(operation);
            write && write.getCell() == cell.getResult()) {
            initialized = true;
        }
        auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation);
        if (!initialized &&
            ((read && read.getCell() == cell.getResult()) ||
             callbackOperations.contains(&operation) || stateBranches.contains(&operation))) {
            return refuse("class Boolean state is used before unconditional initialization");
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
    // Roots of cell/closure/implicit-callee identities would otherwise obstruct
    // lifting or disappear with a lifted closure. Preserve every original
    // position and context for the later complete frame proof.
    for (auto root : roots) {
        mlir::OpBuilder at(root);
        root.getValueMutable().assign(undefinedAt(at, root.getLoc()).getResult());
    }
    booleanCells.insert(cell.getResult());
    for (auto * leaf : leaves) {
        booleanLeaves.insert(leaf);
        helpers.insert(leaf);
        domEntryHelpers.insert(leaf);
    }
    needsDOMMethodProof = true;
    return true;
}

} // namespace ctcompile::ctnative::class_detail
