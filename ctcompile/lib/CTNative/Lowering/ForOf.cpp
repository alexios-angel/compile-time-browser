//===- ForOf.cpp - for-of over arrays as index loops ----------------------===//
//
// The importer compiles `for (x of s)` to both of the interpreter's paths: a
// record from `__ctbrowser_for_of_open(s)`, undefined for an array, selects
// the index loop over `ctjs.iterable of s` (the same array) or the iterator
// protocol (`__ctbrowser_iter_next`), and `__ctbrowser_iter_close` closes a
// record at the end. For an array only the index loop runs and the close does
// nothing, so this keeps that loop alone. The source being an array is a type
// fact, decided later by admission through kForOfArray.
//
//===----------------------------------------------------------------------===//

#include "ForOf.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::ctnative {

namespace {

llvm::StringRef helperOf(ctjs::CallOp call) {
    auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    return load ? load.getName() : llvm::StringRef{};
}

/// The `scf.if` a use of the record decides: `record === undefined` (either
/// side), through `ctjs.truthy`, as an `scf.if` condition. Null otherwise.
mlir::scf::IfOp fastPathTest(mlir::OpOperand & use) {
    auto compare = llvm::dyn_cast<ctjs::CompareOp>(use.getOwner());
    if (!compare || compare.getKind() != ctjs::CompareKind::StrictEq) { return {}; }
    if (!ctjs::isUndefinedConstant(compare->getOperand(1 - use.getOperandNumber()))) { return {}; }
    if (!compare.getResult().hasOneUse()) { return {}; }
    auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(*compare.getResult().getUsers().begin());
    if (!truthy || !truthy.getResult().hasOneUse()) { return {}; }
    auto branch = llvm::dyn_cast<mlir::scf::IfOp>(*truthy.getResult().getUsers().begin());
    return branch && branch.getCondition() == truthy.getResult() ? branch : mlir::scf::IfOp{};
}

/// Replaces `branch` by its then-region, which the fold proved is the one
/// that runs.
void keepThen(mlir::scf::IfOp branch) {
    mlir::Block & then = branch.getThenRegion().front();
    auto yield = llvm::cast<mlir::scf::YieldOp>(then.getTerminator());
    branch->getResults().replaceAllUsesWith(yield.getOperands());
    yield.erase();
    branch->getBlock()->getOperations().splice(branch->getIterator(), then.getOperations());
    mlir::Operation * test = branch.getCondition().getDefiningOp();
    branch.erase();
    // The truthy and the comparison feeding it are dead now.
    if (test && test->use_empty()) {
        mlir::Operation * compare = test->getOperand(0).getDefiningOp();
        test->erase();
        if (compare && compare->use_empty()) { compare->erase(); }
    }
}

/// Folds one for-of whose record `open` makes; false (and nothing changed)
/// when the record has a use this shape does not explain.
bool fold(ctjs::CallOp open) {
    llvm::SmallVector<mlir::scf::IfOp> tests;
    llvm::SmallVector<ctjs::CallOp> closes;
    for (mlir::OpOperand & use : open.getResult().getUses()) {
        if (auto branch = fastPathTest(use)) { tests.push_back(branch); }
        auto close = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (close && helperOf(close) == "__ctbrowser_iter_close" && use.getOperandNumber() == 2) {
            closes.push_back(close);
        }
    }
    // Every other use is in a protocol arm, which the fold removes.
    for (mlir::OpOperand & use : open.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        const bool inProtocolArm = llvm::any_of(tests, [&](mlir::scf::IfOp branch) {
            return branch.getElseRegion().isAncestor(user->getParentRegion());
        });
        if (!fastPathTest(use) && !llvm::is_contained(closes, user) && !inProtocolArm) {
            return false;
        }
    }
    if (tests.empty()) { return false; }
    // The source's materialized array: `ctjs.iterable of s`, in a fast arm.
    mlir::Value source = open.getArgs().front();
    llvm::SmallVector<ctjs::IterableOp> iterables;
    for (mlir::Operation * user : source.getUsers()) {
        auto iterable = llvm::dyn_cast<ctjs::IterableOp>(user);
        if (iterable && llvm::any_of(tests, [&](mlir::scf::IfOp branch) {
                return branch.getThenRegion().isAncestor(iterable->getParentRegion());
            })) {
            iterables.push_back(iterable);
        }
    }
    for (ctjs::CallOp close : closes) {
        mlir::Operation * helper = close.getCallee().getDefiningOp();
        close.erase();
        if (helper && helper->use_empty()) { helper->erase(); }
    }
    for (mlir::scf::IfOp branch : tests) { keepThen(branch); }
    for (ctjs::IterableOp iterable : iterables) {
        for (mlir::Operation * user : iterable.getResult().getUsers()) {
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                read->setAttr(kForOfArray, mlir::UnitAttr::get(read.getContext()));
            }
        }
        iterable.getResult().replaceAllUsesWith(source);
        iterable.erase();
    }
    mlir::Operation * load = open.getCallee().getDefiningOp();
    open.erase();
    if (load && load->use_empty()) { load->erase(); }
    return true;
}

} // namespace

ForOfOriginals foldArrayForOf(mlir::ModuleOp module) {
    ForOfOriginals originals;
    module.walk([&](ctjs::FuncOp fn) {
        llvm::SmallVector<ctjs::CallOp> opens;
        fn.walk([&](ctjs::CallOp call) {
            if (helperOf(call) == "__ctbrowser_for_of_open" && call.getArgs().size() == 1 &&
                ctjs::isUndefinedConstant(call.getReceiver())) {
                opens.push_back(call);
            }
        });
        if (opens.empty()) { return; }
        mlir::OwningOpRef<ctjs::FuncOp> original(llvm::cast<ctjs::FuncOp>(fn->clone()));
        bool changed = false;
        for (ctjs::CallOp open : opens) { changed |= fold(open); }
        if (changed) { originals.emplace_back(fn, std::move(original)); }
    });
    return originals;
}

} // namespace ctcompile::ctnative
