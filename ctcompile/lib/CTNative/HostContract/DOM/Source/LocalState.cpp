#include "Proof.hpp"

namespace ctcompile::ctnative::dom_source_detail {

bool DOMSource::normalizeBooleanCell(ctjs::FuncOp function, unsigned depth) {
    auto & block = function.getBody().front();
    ctjs::CreateCellOp cell;
    // Only the borrowed-cell seam needs this normalization. Other cells keep
    // their existing fixed-capture proof, including its refusal boundaries.
    for (auto local : block.getOps<ctjs::CreateCellOp>()) {
        if (!step()) { return false; }
        for (mlir::Operation * user : local.getResult().getUsers()) {
            if (!step()) { return false; }
            if (!llvm::isa<ctjs::CallDirectOp>(user)) { continue; }
            if (cell && cell != local) {
                return refuse("DOM Boolean state requires one borrowed local cell");
            }
            cell = local;
        }
    }
    if (!cell) { return true; }
    if (!ctjs::constantAttr<ctjs::BooleanAttr>(cell.getInitial()) &&
        !undefined(cell.getInitial())) {
        return refuse("DOM Boolean state requires a literal initialization");
    }
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (!step()) { return false; }
        auto * user = use.getOwner();
        if (user->getBlock() != &block || !cell->isBeforeInBlock(user)) {
            return refuse("DOM Boolean state requires flat ordered local uses");
        }
        if ((llvm::isa<ctjs::RootOp>(user) && use.getOperandNumber() == 1) ||
            (llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) ||
            (llvm::isa<ctjs::CallDirectOp>(user) &&
             use.getOperandNumber() == ctjs::implicit_arguments)) {
            continue;
        }
        return refuse("DOM Boolean state identity escapes its local borrowers");
    }

    llvm::SmallVector<ctjs::CallDirectOp> calls;
    llvm::DenseSet<mlir::Operation *> selectedCalls, helpers;
    bool initialized = static_cast<bool>(ctjs::constantAttr<ctjs::BooleanAttr>(cell.getInitial()));
    for (auto & operation : block) {
        if (!step(1 + operation.getNumOperands() + operation.getNumResults())) { return false; }
        if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(operation);
            write && write.getCell() == cell.getResult()) {
            if (!ctjs::constantAttr<ctjs::BooleanAttr>(write.getValue())) {
                return refuse("DOM Boolean state stores must be literal Booleans");
            }
            initialized = true;
        }
        if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation);
            read && read.getCell() == cell.getResult() && !initialized) {
            return refuse("DOM Boolean state is read before initialization");
        }
        auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
        if (!call || !llvm::is_contained(call->getOperands(), cell.getResult())) { continue; }
        auto target = call.getTarget();
        const auto index = target ? functionIndex(target) : std::nullopt;
        if (!initialized || !index || functions.lookup(*index) != target || target == function ||
            !target.isPrivate() || creations.lookup(*index) != 0 || target.getUpvalueCount() != 0 ||
            call.getArgs().size() != 1 || call.getArgs().front() != cell.getResult() ||
            target.getBody().front().getNumArguments() != ctjs::implicit_arguments + 1 ||
            !undefined(call.getReceiver()) || !undefined(call.getNewTarget()) ||
            !undefined(call.getCalleeValue()) || depth + callDepth.lookup(call) >= 63) {
            return refuse("DOM Boolean state requires initialized exact local leaf calls");
        }
        calls.push_back(call);
        selectedCalls.insert(call);
        if (!helpers.insert(target).second) { continue; }
        // Keep the ordinary frame, dominance, completion and implicit-argument
        // proof. The extra census below narrows these bodies to flat Boolean
        // leaves; it does not infer their behavior from native annotations.
        if (!checkBody(target, false)) { return false; }
        auto & body = target.getBody().front();
        const auto formal = body.getArgument(ctjs::implicit_arguments);
        for (mlir::OpOperand & use : formal.getUses()) {
            if (!step()) { return false; }
            auto * user = use.getOwner();
            if (user->getBlock() == &body &&
                ((llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(user) &&
                  use.getOperandNumber() == 0) ||
                 (llvm::isa<ctjs::RootOp>(user) && use.getOperandNumber() == 1))) {
                continue;
            }
            return refuse("DOM Boolean borrower forwards or exposes its cell");
        }
        llvm::DenseSet<mlir::Value> booleans;
        for (auto & leaf : body) {
            if (!step(1 + leaf.getNumOperands() + leaf.getNumResults())) { return false; }
            if (leaf.getNumRegions() || leaf.getNumSuccessors()) {
                return refuse("DOM Boolean borrower requires a flat leaf body");
            }
            if (auto literal = llvm::dyn_cast<ctjs::ConstantOp>(leaf)) {
                if (ctjs::constantAttr<ctjs::BooleanAttr>(literal.getResult())) {
                    booleans.insert(literal.getResult());
                } else if (!undefined(literal.getResult())) {
                    return refuse("DOM Boolean borrower has a non-Boolean literal");
                }
            } else if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(leaf);
                       read && read.getCell() == formal) {
                booleans.insert(read.getResult());
            } else if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(leaf);
                       write && write.getCell() == formal &&
                       ctjs::constantAttr<ctjs::BooleanAttr>(write.getValue())) {
                continue;
            } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(leaf);
                       unary && unary.getKind() == ctjs::UnaryKind::Not &&
                       booleans.contains(unary.getOperand())) {
                booleans.insert(unary.getResult());
            } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(leaf);
                       compare && compare.getKind() == ctjs::CompareKind::StrictEq &&
                       booleans.contains(compare.getLhs()) && booleans.contains(compare.getRhs())) {
                booleans.insert(compare.getResult());
            } else if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(leaf);
                       returned &&
                       (booleans.contains(returned.getValue()) || undefined(returned.getValue()))) {
                continue;
            } else if (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(leaf)) {
                return refuse("DOM Boolean borrower contains an unproved operation");
            }
        }
    }
    // Both symbol scopes matter: a second owner or an attribute naming a
    // borrower is not made safe by the calls found through this cell's uses.
    auto module = function->getParentOfType<mlir::ModuleOp>();
    if (!step(2 * uint64_t(operationCount))) { return false; }
    if (auto stopped = host_detail::forEachSymbolUse(
            module, true, [&] { return step(); },
            [&](const mlir::SymbolTable::SymbolUse & use,
                ctjs::FuncOp target) -> std::optional<bool> {
                if (!helpers.contains(target)) { return std::nullopt; }
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                if (!call || call.getTarget() != target || !selectedCalls.contains(call)) {
                    return false;
                }
                return std::nullopt;
            })) {
        return refuse("DOM Boolean borrower has an unproved symbolic user");
    }
    // Reserve operand copying and use replacement before any call is erased.
    // inlineCall additionally charges its ordinary mapping and clone work.
    for (auto call : calls) {
        if (!step()) { return false; }
        for (auto & operation : call.getTarget().getBody().front()) {
            if (!step(operationWork(&operation, false))) { return false; }
            for (auto result : operation.getResults()) {
                for (auto & use : result.getUses()) {
                    (void)use;
                    if (!step()) { return false; }
                }
            }
        }
        for (auto & use : call.getResult().getUses()) {
            (void)use;
            if (!step()) { return false; }
        }
    }
    for (auto call : calls) {
        if (!inlineCall(function, call.getTarget(), call, call.getArgs(), call.getReceiver(),
                        call.getCalleeValue(), {}, depth)) {
            return false;
        }
        callDepth.erase(call);
        call.erase();
    }
    // Each read snapshots the current value at its original source position.
    // Keep every producer and unrelated effect for the complete DOM reproof;
    // only the already-proved local storage operations disappear.
    mlir::Value current = cell.getInitial();
    for (auto & operation : llvm::make_early_inc_range(block)) {
        if (!step()) { return false; }
        if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation);
            read && read.getCell() == cell.getResult()) {
            for (auto & use : read.getResult().getUses()) {
                (void)use;
                if (!step()) { return false; }
            }
            read.getResult().replaceAllUsesWith(current);
            read.erase();
        } else if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(operation);
                   write && write.getCell() == cell.getResult()) {
            current = write.getValue();
            write.erase();
        }
    }
    if (!eraseRootedUses(cell.getResult(), "DOM Boolean state has a residual use")) {
        return false;
    }
    cell.erase();
    for (auto * helper : helpers) {
        if (!step()) { return false; }
        expanded.insert(helper);
    }
    return true;
}

} // namespace ctcompile::ctnative::dom_source_detail
