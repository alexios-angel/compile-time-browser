#include "Proof.hpp"

namespace ctcompile::ctnative::dom_source_detail {

bool DOMSource::checkBody(ctjs::FuncOp function, bool entry, bool directReceiver,
                          bool beforeReplacement) {
    auto & block = function.getBody().front();
    llvm::DenseSet<mlir::Value> values;
    for (mlir::BlockArgument argument : block.getArguments()) {
        if (!step()) { return false; }
        values.insert(argument);
        if (entry || argument.getArgNumber() >= ctjs::implicit_arguments ||
            (directReceiver && argument.getArgNumber() == ctjs::arg_receiver)) {
            continue;
        }
        for (mlir::OpOperand & use : argument.getUses()) {
            if (!step()) { return false; }
            auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
            if (!llvm::isa<ctjs::RootOp, ctjs::CreateClosureOp>(use.getOwner()) &&
                !(load && argument.getArgNumber() == ctjs::arg_callee &&
                  use.getOperandNumber() == 0)) {
                return refuse("DOM helper observes an implicit argument");
            }
        }
    }
    mlir::DominanceInfo dominance(function);
    const auto visit = [&](auto && self, mlir::Block & body, unsigned depth,
                           mlir::Value & frame) -> bool {
        if (depth == 64 || (depth && body.getNumArguments() &&
                            !llvm::isa<mlir::scf::WhileOp>(body.getParentOp()))) {
            return refuse(entry ? "DOM entry branch depth or arguments are unsupported"
                                : "DOM helper branch depth or arguments are unsupported");
        }
        for (mlir::BlockArgument argument : body.getArguments()) {
            if (!step()) { return false; }
            values.insert(argument);
        }
        bool entered = false, returned = false;
        for (mlir::Operation & operation : body) {
            if (!step()) { return false; }
            // A normalized URI invoke is opaque here: normalizeDOMURI proved
            // its regions, and the complete DOM entry proof reproves the
            // inlined result before anything is published.
            if ((operation.getNumRegions() &&
                 !llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp, ctjs::InvokeOp>(operation)) ||
                operation.getNumSuccessors() || returned) {
                return refuse("DOM helper requires complete structured branches");
            }
            for (mlir::Value operand : operation.getOperands()) {
                if (!step()) { return false; }
                if ((!values.contains(operand) && operand != frame) ||
                    !dominance.dominates(operand, &operation)) {
                    return refuse("DOM helper operand has no preceding local definition");
                }
                if (operand == frame && !llvm::isa<ctjs::RootOp, ctjs::FrameExitOp>(operation)) {
                    return refuse("DOM helper observes its shadow frame");
                }
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                if (!branch.getCondition().getType().isInteger(1) ||
                    !branch.getThenRegion().hasOneBlock() ||
                    (!branch.getElseRegion().empty() && !branch.getElseRegion().hasOneBlock()) ||
                    (branch.getNumResults() && branch.getElseRegion().empty())) {
                    return refuse("DOM helper branch lacks complete Boolean arms");
                }
                mlir::Value thenFrame = frame, elseFrame = frame;
                for (mlir::Region & region : branch->getRegions()) {
                    if (region.empty()) { continue; }
                    auto & armFrame = &region == &branch.getThenRegion() ? thenFrame : elseFrame;
                    if (!self(self, region.front(), depth + 1, armFrame)) { return false; }
                    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                    if (!yield || yield.getOperandTypes() != branch.getResultTypes()) {
                        return refuse("DOM helper branch has incomplete result correspondence");
                    }
                }
                if (thenFrame != elseFrame) {
                    return refuse("DOM helper branch has inconsistent shadow frame exits");
                }
                frame = thenFrame;
                values.insert(branch.getResults().begin(), branch.getResults().end());
                continue;
            }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
                if (!loop.getBefore().hasOneBlock() || !loop.getAfter().hasOneBlock() ||
                    loop.getBefore().front().getArgumentTypes() != loop.getInits().getTypes() ||
                    loop.getAfter().front().getArgumentTypes() != loop.getResultTypes()) {
                    return refuse("DOM helper loop lacks complete source regions");
                }
                mlir::Value beforeFrame = frame, afterFrame = frame;
                if (!self(self, loop.getBefore().front(), depth + 1, beforeFrame) ||
                    !self(self, loop.getAfter().front(), depth + 1, afterFrame)) {
                    return false;
                }
                auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(
                    loop.getBefore().front().getTerminator());
                auto yield =
                    llvm::dyn_cast<mlir::scf::YieldOp>(loop.getAfter().front().getTerminator());
                if (!condition || !condition.getCondition().getType().isInteger(1) ||
                    condition.getArgs().getTypes() != loop.getResultTypes() || !yield ||
                    yield.getOperandTypes() != loop.getInits().getTypes()) {
                    return refuse("DOM helper loop has incomplete result correspondence");
                }
                if (beforeFrame != frame || afterFrame != frame) {
                    return refuse("DOM helper loop changes its shadow frame");
                }
                values.insert(loop.getResults().begin(), loop.getResults().end());
                continue;
            }
            if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                if (depth || entered) { return refuse("DOM helper has repeated shadow frames"); }
                entered = true;
                frame = enter.getContext();
            } else if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
                if (!frame || root.getContext() != frame) {
                    return refuse("DOM helper root is outside its shadow frame");
                }
            } else if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
                if (!frame || exit.getContext() != frame) {
                    return refuse("DOM helper exits an unknown shadow frame");
                }
                frame = {};
            } else if (llvm::isa<ctjs::ReturnOp>(operation)) {
                if (depth || frame) {
                    return refuse("DOM helper returns inside a branch or with a live shadow frame");
                }
                returned = true;
            } else if (llvm::isa<mlir::scf::YieldOp>(operation)) {
                if (!depth) { return refuse("DOM helper yield is outside a branch"); }
                returned = true;
            } else if (llvm::isa<mlir::scf::ConditionOp>(operation)) {
                auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(body.getParentOp());
                if (!depth || !loop || body.getParent() != &loop.getBefore()) {
                    return refuse("DOM helper condition is outside a loop before region");
                }
                returned = true;
            } else {
                values.insert(operation.getResults().begin(), operation.getResults().end());
            }
            // A proved capture-free callback stays in its selected source
            // arm. Other callable/capture definitions still require the
            // entry block; repeated loop-local identities are not proved.
            auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation);
            if (depth) {
                auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
                auto target = closure && closure.getFunction() >= 0
                                  ? functions.lookup(static_cast<unsigned>(closure.getFunction()))
                                  : ctjs::FuncOp{};
                if ((closure && (!target || closure->getParentOfType<mlir::scf::WhileOp>() ||
                                 (!confinedFilterCallback(closure, target) &&
                                  !confinedReplacementCallback(closure, target)))) ||
                    llvm::isa<ctjs::CreateCellOp>(operation) || (object && !dataObject(object))) {
                    return refuse("DOM helper branch contains an unproved local identity");
                }
            }
            if (directReceiver && !beforeReplacement) {
                auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
                auto target = closure && closure.getFunction() >= 0
                                  ? functions.lookup(static_cast<unsigned>(closure.getFunction()))
                                  : ctjs::FuncOp{};
                if (llvm::isa<ctjs::LoadUpvalueOp>(operation) ||
                    (closure && (!target || (!confinedFilterCallback(closure, target) &&
                                             !confinedReplacementCallback(closure, target))))) {
                    return refuse("DOM direct helper contains an unproved closure or capture");
                }
            }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
                if (closure.getFunctionAttr().getInt() < 0 ||
                    closure.getEnclosingClosure() != block.getArgument(ctjs::arg_callee) ||
                    (closure.getEnclosingThis() != block.getArgument(ctjs::arg_receiver) &&
                     !undefined(closure.getEnclosingThis()))) {
                    return refuse("DOM helper lacks an exact local closure identity");
                }
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                if (entry || load.getClosure() != block.getArgument(ctjs::arg_callee) ||
                    load.getIndex() < 0 || load.getIndex() >= function.getUpvalueCount()) {
                    return refuse("DOM helper upvalue lacks an exact capture slot");
                }
            }
        }
        if (!returned) { return refuse("DOM helper has no complete return or yield"); }
        return true;
    };
    mlir::Value frame;
    return visit(visit, block, 0, frame);
}

bool DOMSource::proveUnusedBody(ctjs::FuncOp function, unsigned depth) {
    if (depth == 64 || !active.insert(function).second) {
        return refuse("unused DOM helper nesting is recursive or too deep");
    }
    if (function.getUpvalueCount() != 0 || !normalizeCompletion(function) ||
        !checkBody(function, false)) {
        return false;
    }
    // No invocation supplies parameter facts. These operations are inert for
    // every value, including Objects and Symbols; conversions and calls are
    // deliberately excluded. Check the original body before retiring it.
    // Check both arms, including discarded values and constant-dead arms.
    // checkBody already proved their dominance, completion and shadow frames.
    // Cells hold arbitrary values, never host facts. Their identities must
    // remain private, and every operation on every read is still checked.
    // ponytail: uncaptured conditional bodies; loops and invoked helpers
    // need their own complete independent body proof.
    const auto checked = function.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (operation == function) { return mlir::WalkResult::advance(); }
        if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(operation)) {
            for (mlir::OpOperand & use : cell.getResult().getUses()) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (use.getOwner()->getParentOfType<ctjs::FuncOp>() == function &&
                    ((llvm::isa<ctjs::RootOp>(use.getOwner()) && use.getOperandNumber() == 1) ||
                     (llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(use.getOwner()) &&
                      use.getOperandNumber() == 0))) {
                    continue;
                }
                refuse("unused DOM helper cell identity escapes its local state");
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        }
        if (llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(operation)) {
            auto cell = operation->getOperand(0).getDefiningOp<ctjs::CreateCellOp>();
            if (cell && cell->getParentOfType<ctjs::FuncOp>() == function) {
                return mlir::WalkResult::advance();
            }
            refuse("unused DOM helper cell access lacks a local cell");
            return mlir::WalkResult::interrupt();
        }
        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
            const auto index = static_cast<unsigned>(closure.getFunction());
            auto target = functions.lookup(index);
            if (!target || !closure.getUpvalues().empty() || creations.lookup(index) != 1) {
                refuse("unused DOM helper child is captured or has ambiguous identity");
                return mlir::WalkResult::interrupt();
            }
            for (mlir::OpOperand & use : closure.getResult().getUses()) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (!llvm::isa<ctjs::RootOp>(use.getOwner()) || use.getOperandNumber() != 1) {
                    refuse("unused DOM helper child is invoked or its identity escapes");
                    return mlir::WalkResult::interrupt();
                }
            }
            auto module = function->getParentOfType<mlir::ModuleOp>();
            if (remaining / 2 < operationCount) {
                refuse("DOM helper expansion work budget exhausted");
                return mlir::WalkResult::interrupt();
            }
            remaining -= 2 * operationCount;
            if (!mlir::SymbolTable::symbolKnownUseEmpty(target, module.getOperation()) ||
                !mlir::SymbolTable::symbolKnownUseEmpty(target, &module.getBodyRegion()) ||
                !proveUnusedBody(target, depth + 1)) {
                refuse("unused DOM helper child lacks a complete independent body proof");
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        }
        if (llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                      ctjs::ReturnOp, ctjs::TruthyOp, mlir::scf::IfOp, mlir::scf::YieldOp>(
                operation)) {
            return mlir::WalkResult::advance();
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
            unary &&
            (unary.getKind() == ctjs::UnaryKind::TypeOf ||
             unary.getKind() == ctjs::UnaryKind::Not || unary.getKind() == ctjs::UnaryKind::Void)) {
            return mlir::WalkResult::advance();
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
            compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
            return mlir::WalkResult::advance();
        }
        refuse(("unused DOM helper body contains an unproved operation: " + function.getSymName() +
                " / " + operation->getName().getStringRef())
                   .str());
        return mlir::WalkResult::interrupt();
    });
    if (checked.wasInterrupted()) { return false; }
    active.erase(function);
    expanded.insert(function);
    return true;
}

} // namespace ctcompile::ctnative::dom_source_detail
