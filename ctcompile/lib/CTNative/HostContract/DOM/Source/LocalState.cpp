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
    const auto numericBit = [](mlir::Value value) {
        auto number = ctjs::constantAttr<ctjs::NumberAttr>(value);
        return number && (number.getDouble() == 0.0 || number.getDouble() == 1.0);
    };
    // Bootstrap spells Boolean literals !0 and !1. Prove that exact origin,
    // retaining the original numeric constant and not operation for typed
    // lowering. Other computed stores keep their existing refusal boundary.
    const auto booleanOrigin = [&](mlir::Value value) {
        if (!step()) { return false; }
        if (ctjs::constantAttr<ctjs::BooleanAttr>(value)) { return true; }
        if (!step(2)) { return false; }
        auto unary = value.getDefiningOp<ctjs::UnaryOp>();
        return unary && unary.getKind() == ctjs::UnaryKind::Not && numericBit(unary.getOperand());
    };
    const bool initiallyBoolean = booleanOrigin(cell.getInitial());
    if (!reason.empty()) { return false; }
    if (!initiallyBoolean && !undefined(cell.getInitial())) {
        return refuse("DOM Boolean state requires a literal or !0/!1 initialization");
    }
    // An undefined owner cell may have its entire initialized lifetime inside
    // one arm. Select the common block of every non-root use, including writes;
    // the ordered census below must still prove initialization in that block.
    mlir::Block * scope = initiallyBoolean ? &block : nullptr;
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (!step()) { return false; }
        auto * user = use.getOwner();
        auto * position = user;
        llvm::DenseSet<mlir::Block *> ancestors{position->getBlock()};
        unsigned nesting = 0;
        while (position->getBlock() != &block) {
            if (!step()) { return false; }
            auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(position->getParentOp());
            if (!branch || ++nesting == 64) {
                return refuse("DOM Boolean state requires bounded local conditional uses");
            }
            position = branch;
            ancestors.insert(position->getBlock());
        }
        if (!cell->isBeforeInBlock(position)) {
            return refuse("DOM Boolean state requires ordered local uses");
        }
        if (llvm::isa<ctjs::RootOp>(user) && use.getOperandNumber() == 1) { continue; }
        if (!((llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) ||
              (llvm::isa<ctjs::CallDirectOp>(user) &&
               use.getOperandNumber() == ctjs::implicit_arguments))) {
            return refuse("DOM Boolean state identity escapes its local borrowers");
        }
        if (!scope) { scope = user->getBlock(); }
        while (!ancestors.contains(scope)) {
            if (!step()) { return false; }
            scope = scope->getParentOp()->getBlock();
        }
    }
    llvm::DenseSet<mlir::Operation *> stateBranches;
    const auto findBranches = [&](bool afterInlining) {
        stateBranches.clear();
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!step()) { return false; }
            auto * position = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(position) && use.getOperandNumber() == 1) { continue; }
            if (!((llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(position) &&
                   use.getOperandNumber() == 0) ||
                  (!afterInlining && llvm::isa<ctjs::CallDirectOp>(position) &&
                   use.getOperandNumber() == ctjs::implicit_arguments))) {
                return refuse("DOM Boolean state has an unproved use after expansion");
            }
            bool reachedScope = position->getBlock() == scope;
            unsigned nesting = 0;
            while (position->getBlock() != &block) {
                if (!step()) { return false; }
                auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(position->getParentOp());
                if (!branch || ++nesting == 64) {
                    return refuse("DOM Boolean state requires bounded local conditional uses");
                }
                if (!reachedScope) { stateBranches.insert(branch); }
                position = branch;
                reachedScope |= position->getBlock() == scope;
            }
            if (!reachedScope) { return refuse("DOM Boolean state escaped its selected scope"); }
        }
        return true;
    };
    if (!findBranches(false)) { return false; }

    llvm::SmallVector<ctjs::CallDirectOp> calls;
    const auto collect = [&](auto && self, mlir::Block & source, bool initialized,
                             unsigned nesting) -> bool {
        if (nesting == 64) { return refuse("DOM Boolean state conditional depth exhausted"); }
        for (auto & operation : source) {
            if (!step(1 + operation.getNumOperands() + operation.getNumResults())) { return false; }
            if (stateBranches.contains(&operation)) {
                if (!initialized) {
                    return refuse("DOM Boolean state must be initialized before a conditional");
                }
                for (auto & region : operation.getRegions()) {
                    if (!step()) { return false; }
                    if (region.empty() && operation.getNumResults() == 0) { continue; }
                    if (!region.hasOneBlock() || region.front().getNumArguments() ||
                        region.front().empty()) {
                        return refuse("DOM Boolean state requires complete conditional arms");
                    }
                    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().back());
                    if (!step(1 + (yield ? yield.getNumOperands() : 0))) { return false; }
                    if (!yield || yield.getOperandTypes() != operation.getResultTypes() ||
                        !self(self, region.front(), true, nesting + 1)) {
                        return refuse("DOM Boolean state requires complete conditional yields");
                    }
                }
            }
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(operation);
                write && write.getCell() == cell.getResult()) {
                if (!booleanOrigin(write.getValue())) {
                    return refuse("DOM Boolean state stores must be literal Booleans or !0/!1");
                }
                initialized = true;
            }
            if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation);
                read && read.getCell() == cell.getResult() && !initialized) {
                return refuse("DOM Boolean state is read before initialization");
            }
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
            if (!call || !llvm::is_contained(call->getOperands(), cell.getResult())) { continue; }
            if (!initialized) {
                return refuse("DOM Boolean state is borrowed before initialization");
            }
            calls.push_back(call);
        }
        return true;
    };
    if (!collect(collect, *scope, initiallyBoolean, 0)) { return false; }
    llvm::DenseSet<mlir::Operation *> selectedCalls, helpers;
    for (auto call : calls) {
        if (!step()) { return false; }
        auto target = call.getTarget();
        const auto index = target ? functionIndex(target) : std::nullopt;
        if (!index || functions.lookup(*index) != target || target == function ||
            !target.isPrivate() || creations.lookup(*index) != 0 || target.getUpvalueCount() != 0 ||
            call.getArgs().size() != 1 || call.getArgs().front() != cell.getResult() ||
            target.getBody().front().getNumArguments() != ctjs::implicit_arguments + 1 ||
            !undefined(call.getReceiver()) || !undefined(call.getNewTarget()) ||
            !undefined(call.getCalleeValue()) || depth + callDepth.lookup(call) >= 63) {
            return refuse("DOM Boolean state requires initialized exact local leaf calls");
        }
        selectedCalls.insert(call);
        if (!helpers.insert(target).second) { continue; }
        // Keep the ordinary frame, dominance, completion and implicit-argument
        // proof. The extra census below narrows these bodies to conditional
        // Boolean leaves; it does not trust native annotations.
        if (!checkBody(target, false)) { return false; }
        auto & body = target.getBody().front();
        const auto formal = body.getArgument(ctjs::implicit_arguments);
        for (mlir::OpOperand & use : formal.getUses()) {
            if (!step()) { return false; }
            auto * user = use.getOwner();
            auto * position = user;
            unsigned nesting = 0;
            while (position->getBlock() != &body) {
                if (!step()) { return false; }
                auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(position->getParentOp());
                if (!branch || ++nesting == 64) {
                    return refuse("DOM Boolean borrower has an unproved conditional use");
                }
                position = branch;
            }
            if ((llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(user) &&
                 use.getOperandNumber() == 0) ||
                (llvm::isa<ctjs::RootOp>(user) && use.getOperandNumber() == 1)) {
                continue;
            }
            return refuse("DOM Boolean borrower forwards or exposes its cell");
        }
        llvm::DenseSet<mlir::Value> booleans;
        const auto proveLeaf = [&](auto && self, mlir::Block & source, unsigned nesting) -> bool {
            if (nesting == 64) {
                return refuse("DOM Boolean borrower conditional depth exhausted");
            }
            for (auto & leaf : source) {
                if (!step(1 + leaf.getNumOperands() + leaf.getNumResults())) { return false; }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(leaf)) {
                    auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
                    if (!truth || !booleans.contains(truth.getValue())) {
                        return refuse("DOM Boolean borrower requires a Boolean conditional");
                    }
                    for (auto & region : branch->getRegions()) {
                        if (!step()) { return false; }
                        if (region.empty()) { continue; }
                        if (!self(self, region.front(), nesting + 1)) { return false; }
                    }
                    // checkBody proved complete arm/result correspondence and
                    // dominance. Every yielded value must now be Boolean before
                    // any result is used as a fact by a later operation.
                    booleans.insert(branch.getResults().begin(), branch.getResults().end());
                    continue;
                }
                if (leaf.getNumRegions() || leaf.getNumSuccessors()) {
                    return refuse("DOM Boolean borrower requires an If-only leaf body");
                }
                if (auto literal = llvm::dyn_cast<ctjs::ConstantOp>(leaf)) {
                    if (ctjs::constantAttr<ctjs::BooleanAttr>(literal.getResult())) {
                        booleans.insert(literal.getResult());
                    } else if (!undefined(literal.getResult()) &&
                               !numericBit(literal.getResult())) {
                        return refuse("DOM Boolean borrower has a non-Boolean literal");
                    }
                } else if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(leaf);
                           read && read.getCell() == formal) {
                    booleans.insert(read.getResult());
                } else if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(leaf);
                           write && write.getCell() == formal && booleanOrigin(write.getValue())) {
                    continue;
                } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(leaf);
                           unary && unary.getKind() == ctjs::UnaryKind::Not &&
                           (booleans.contains(unary.getOperand()) ||
                            numericBit(unary.getOperand()))) {
                    booleans.insert(unary.getResult());
                } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(leaf);
                           compare && compare.getKind() == ctjs::CompareKind::StrictEq &&
                           booleans.contains(compare.getLhs()) &&
                           booleans.contains(compare.getRhs())) {
                    booleans.insert(compare.getResult());
                } else if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(leaf);
                           truth && booleans.contains(truth.getValue())) {
                    continue;
                } else if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(leaf);
                           returned && nesting == 0 &&
                           (booleans.contains(returned.getValue()) ||
                            undefined(returned.getValue()))) {
                    continue;
                } else if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(leaf);
                           yield && nesting) {
                    for (auto value : yield.getOperands()) {
                        if (!booleans.contains(value)) {
                            return refuse("DOM Boolean borrower requires Boolean yields");
                        }
                    }
                } else if (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(leaf)) {
                    return refuse("DOM Boolean borrower contains an unproved operation");
                }
            }
            return true;
        };
        if (!proveLeaf(proveLeaf, body, 0)) { return false; }
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
    // Reserve every nested operation and result use before erasing any call.
    // Count even branches without cell uses toward the combined nesting limit.
    const auto reserveClone = [&](auto && self, mlir::Block & source, unsigned nesting) -> bool {
        if (nesting == 64) {
            return refuse("DOM Boolean borrower combined conditional depth exhausted");
        }
        for (auto & operation : source) {
            if (!step(operationWork(&operation, true))) { return false; }
            for (auto result : operation.getResults()) {
                for (auto & use : result.getUses()) {
                    (void)use;
                    if (!step()) { return false; }
                }
            }
            for (auto & region : operation.getRegions()) {
                for (auto & nested : region) {
                    if (!self(self, nested, nesting + 1)) { return false; }
                }
            }
        }
        return true;
    };
    for (auto call : calls) {
        if (!step()) { return false; }
        unsigned nesting = 0;
        for (auto * position = call.getOperation(); position->getBlock() != &block;
             position = position->getParentOp()) {
            if (!step()) { return false; }
            ++nesting;
        }
        if (!reserveClone(reserveClone, call.getTarget().getBody().front(), nesting)) {
            return false;
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
    // Inlining introduces fresh If operations. Rebuild their ancestry from the
    // actual cell uses, then prove initialization and every state edge again.
    calls.clear();
    if (!findBranches(true) || !collect(collect, *scope, initiallyBoolean, 0)) { return false; }
    // Each read snapshots the current value at its original source position.
    // Keep every producer and unrelated effect for the complete DOM reproof;
    // only the already-proved local storage operations disappear.
    const auto scalarize = [&](auto && self, mlir::Block & source, mlir::Value & current,
                               unsigned nesting) -> bool {
        if (nesting == 64) { return refuse("DOM Boolean state conditional depth exhausted"); }
        for (auto & operation : llvm::make_early_inc_range(source)) {
            if (!step()) { return false; }
            if (stateBranches.contains(&operation)) {
                // Retain the original result prefix and arm bodies. Only the
                // private Boolean state becomes one additional value per edge.
                if (!step(operationWork(&operation, true) + 1)) { return false; }
                for (auto result : operation.getResults()) {
                    for (auto & use : result.getUses()) {
                        (void)use;
                        if (!step()) { return false; }
                    }
                }
                mlir::OperationState built(operation.getLoc(), operation.getName());
                built.addOperands(operation.getOperands());
                built.addTypes(operation.getResultTypes());
                built.addTypes(current.getType());
                built.addAttributes(operation.getAttrs());
                for (auto & region : operation.getRegions()) {
                    if (!step()) { return false; }
                    built.addRegion()->takeBody(region);
                }
                mlir::OpBuilder at(&operation);
                auto * joined = at.create(built);
                for (auto & region : joined->getRegions()) {
                    if (!step()) { return false; }
                    auto armState = current;
                    if (region.empty()) {
                        if (!step(2)) { return false; }
                        auto & arm = region.emplaceBlock();
                        mlir::OpBuilder inside(&arm, arm.end());
                        mlir::scf::YieldOp::create(inside, operation.getLoc(), armState);
                        ++operationCount;
                    } else {
                        if (!self(self, region.front(), armState, nesting + 1)) { return false; }
                        auto * yield = region.front().getTerminator();
                        if (!step(1 + yield->getNumOperands())) { return false; }
                        yield->insertOperands(yield->getNumOperands(), armState);
                    }
                }
                operation.replaceAllUsesWith(
                    joined->getResults().take_front(operation.getNumResults()));
                current = joined->getResults().back();
                operation.erase();
            } else if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation);
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
        return true;
    };
    mlir::Value current = cell.getInitial();
    if (!scalarize(scalarize, *scope, current, 0)) { return false; }
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
