//===- SuppressedCloses.cpp - Iterator closes before a throw --------------===//
//
// Matches the handler that drops an iterator close's errors before a throw
// and rewrites it into an invocation of the close (SuppressedCloses.h).
//
//===----------------------------------------------------------------------===//

#include "SuppressedCloses.h"

#include "../../HostContract/DOM/Snapshot/Bounds.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

namespace ctcompile::ctnative::lowering_detail {
namespace {

constexpr llvm::StringLiteral iteratorClose = "__ctbrowser_iter_close";

/// One handler that only drops an iterator close's errors.
struct suppressedClose {
    ctjs::PushHandlerOp push;
    /// The constants and the read of the close helper, in order. None of them
    /// can throw, so they may run before the handler is installed.
    llvm::SmallVector<mlir::Operation *> setup;
    ctjs::CallOp close;
    /// The protected blocks and the catch block, which the rewrite erases.
    llvm::SmallVector<mlir::Block *> erased;
    /// Where both the protected path and the catch continue.
    mlir::Block * joined = nullptr;
};

/// Whether `op` is a constant or a read of the close helper.
bool setupOperation(mlir::Operation & op) {
    auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
    return llvm::isa<ctjs::ConstantOp>(op) || (load && load.getName() == iteratorClose);
}

/// The handler `push` installs, when it only drops an iterator close's
/// errors. The protected path is one chain of argument-free blocks, each
/// entered only from the one before, that runs the setup, one call of the
/// close helper whose result is unused and the pop, in that order, then
/// branches to the catch's continuation. Every check on it leads to the catch
/// block, which nothing else enters and which holds only a catch_land whose
/// results are unused and that branch.
std::optional<suppressedClose> match(ctjs::PushHandlerOp push) {
    mlir::Block * caught = push.getHandler();
    if (caught == push.getBody() || caught->getNumArguments() != 0 ||
        !push.getBodyOperands().empty() || !push.getHandlerOperands().empty() ||
        caught->getOperations().size() != 2) {
        return std::nullopt;
    }
    auto landing = llvm::dyn_cast<ctjs::CatchLandOp>(caught->front());
    auto resume = llvm::dyn_cast<mlir::cf::BranchOp>(caught->back());
    if (!landing || !landing->use_empty() || !resume || resume.getDest() == caught ||
        !resume.getDestOperands().empty()) {
        return std::nullopt;
    }
    suppressedClose shape{push, {}, {}, {}, resume.getDest()};
    llvm::SmallPtrSet<mlir::Block *, 8> chain;
    ctjs::PopHandlerOp pop;
    mlir::Block * previous = push->getBlock();
    for (mlir::Block * block = push.getBody(); block != shape.joined;) {
        if (block == caught || block->getNumArguments() != 0 ||
            block->getSinglePredecessor() != previous || !chain.insert(block).second) {
            return std::nullopt;
        }
        for (mlir::Operation & op : block->without_terminator()) {
            auto removed = llvm::dyn_cast<ctjs::PopHandlerOp>(op);
            auto call = llvm::dyn_cast<ctjs::CallOp>(op);
            if (removed && shape.close && !pop) {
                pop = removed;
            } else if (call && !shape.close) {
                shape.close = call;
            } else if (!shape.close && setupOperation(op)) {
                shape.setup.push_back(&op);
            } else {
                return std::nullopt;
            }
        }
        previous = block;
        mlir::Operation * terminator = block->getTerminator();
        auto check = llvm::dyn_cast<ctjs::CheckOp>(terminator);
        auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(terminator);
        if (check && !pop && check.getHandler() == caught && check.getContOperands().empty() &&
            check.getHandlerOperands().empty()) {
            block = check.getCont();
        } else if (branch && pop && branch.getDest() == shape.joined &&
                   branch.getDestOperands().empty()) {
            block = shape.joined;
        } else {
            return std::nullopt;
        }
    }
    auto helper = shape.close ? shape.close.getCallee().getDefiningOp<ctjs::LoadGlobalOp>()
                              : ctjs::LoadGlobalOp{};
    if (!pop || !helper || helper.getName() != iteratorClose || !shape.close->use_empty()) {
        return std::nullopt;
    }
    for (mlir::Block * from : caught->getPredecessors()) {
        if (from != push->getBlock() && !chain.contains(from)) { return std::nullopt; }
    }
    shape.erased.assign(chain.begin(), chain.end());
    shape.erased.push_back(caught);
    return shape;
}

/// Replaces the handler with its setup, an invocation of the close whose
/// continuations do nothing, and a branch to where both paths continued.
/// Values the close reads from outside the chain dominate the push: the
/// chain is entered only from the push's block.
void rewrite(suppressedClose & shape) {
    mlir::OpBuilder at(shape.push);
    mlir::IRMapping mapping;
    for (mlir::Operation * op : shape.setup) { at.clone(*op, mapping); }
    emitSuppressedCall(at, *shape.close, mapping);
    mlir::cf::BranchOp::create(at, shape.push.getLoc(), shape.joined);
    shape.push.erase();
    for (mlir::Block * block : shape.erased) { block->dropAllReferences(); }
    for (mlir::Block * block : shape.erased) { block->erase(); }
}

} // namespace

bool structureSuppressedCloses(mlir::ModuleOp module) {
    // The read of the helper moves out of the handler, which keeps the
    // program's meaning only while the helper is the realm's own.
    const bool replaced = module
                              .walk([](ctjs::StoreGlobalOp store) {
                                  return store.getName() == iteratorClose
                                             ? mlir::WalkResult::interrupt()
                                             : mlir::WalkResult::advance();
                              })
                              .wasInterrupted();
    if (replaced) { return false; }
    llvm::SmallVector<ctjs::FuncOp> functions;
    module.walk([&](ctjs::FuncOp function) { functions.push_back(function); });
    bool changed = false;
    for (ctjs::FuncOp function : functions) {
        llvm::SmallVector<suppressedClose> shapes;
        const bool other = function
                               .walk([&](ctjs::PushHandlerOp push) {
                                   std::optional<suppressedClose> shape = match(push);
                                   if (!shape) { return mlir::WalkResult::interrupt(); }
                                   shapes.push_back(std::move(*shape));
                                   return mlir::WalkResult::advance();
                               })
                               .wasInterrupted();
        if (other || shapes.empty()) { continue; }
        for (suppressedClose & shape : shapes) { rewrite(shape); }
        function->removeAttr("ctjs.not_structured");
        changed = true;
    }
    return changed;
}

} // namespace ctcompile::ctnative::lowering_detail
