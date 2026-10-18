//===- ConstantTests.cpp - Fold the tests a closure answers ---------------===//
//
// A test of a closure is answered before the closure census, so a closure
// that is tested and then called is only called: `typeof` of a closure is
// "function" and a closure is truthy. What those answers meet folds in turn,
// through the script runtime's own operators (partial_eval): a compare with a
// constant, a negation, and an scf.if whose condition they decide, which
// becomes the branch it takes when the branch it drops only computes
// values. Tests that no closure feeds are left to precomputation.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include "../../PartialEvaluation/Heap.h"
#include "../../Symbolic/Facts.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Matchers.h"
#include "llvm/ADT/SetVector.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {
namespace pe = partial_eval;

/// What one run of the fold knows: the closures it has named and the
/// constants it has made.
struct answers {
    llvm::DenseMap<mlir::Operation *, unsigned> closures;
    llvm::DenseSet<mlir::Operation *> made;

    /// What the IR shows about `v`: a ctjs.constant's literal, or a closure as
    /// a reference whose identity is the operation that makes it. Two closure
    /// operations make two objects, and one makes the same object for every
    /// use of its result.
    pe::value shown(mlir::Value v) {
        mlir::Operation * op = v.getDefiningOp();
        if (auto constant = llvm::dyn_cast_or_null<ctjs::ConstantOp>(op)) {
            return pe::value::primitive(constant.getValue());
        }
        if (llvm::isa_and_nonnull<ctjs::CreateClosureOp>(op)) {
            return pe::value::reference(
                closures.try_emplace(op, static_cast<unsigned>(closures.size())).first->second);
        }
        return {};
    }
    /// Whether `v` is a closure or an answer this fold made from one.
    bool fromClosure(mlir::Value v) const {
        mlir::Operation * op = v.getDefiningOp();
        return llvm::isa_and_nonnull<ctjs::CreateClosureOp>(op) || made.contains(op);
    }
    /// The constants that make `values`, outside `inside` when it is given.
    /// Once their last use is folded away they are erased: admission refuses a
    /// function over an unused string constant, which no use asks it to spell.
    static llvm::SmallSetVector<mlir::Operation *, 4> constantsOf(mlir::ValueRange values,
                                                                  mlir::Operation * inside) {
        llvm::SmallSetVector<mlir::Operation *, 4> found;
        for (mlir::Value value : values) {
            mlir::Operation * op = value.getDefiningOp();
            if (llvm::isa_and_nonnull<ctjs::ConstantOp, mlir::arith::ConstantOp>(op) &&
                !(inside && inside->isAncestor(op))) {
                found.insert(op);
            }
        }
        return found;
    }
    void eraseUnused(const llvm::SmallSetVector<mlir::Operation *, 4> & constants) {
        for (mlir::Operation * op : constants) {
            if (!op->use_empty()) { continue; }
            made.erase(op);
            op->erase();
        }
    }
};

/// Whether `region` only computes values. Dropping it then erases nothing the
/// lifter has indexed by this point: no closure, cell or call.
// ponytail: value operations only; a dropped branch with calls or closures
// needs the lifter's indexes updated before it can be erased.
bool onlyValues(mlir::Region & region) {
    return llvm::all_of(region.getOps(), [](mlir::Operation & op) {
        return llvm::isa<ctjs::ConstantOp, ctjs::UnaryOp, ctjs::CompareOp, ctjs::TruthyOp,
                         mlir::arith::ConstantOp, mlir::scf::YieldOp>(op);
    });
}
} // namespace

void closureLifter::foldConstantTests() {
    answers known;
    for (bool changed = true; changed;) {
        changed = false;
        module.walk([&](mlir::Operation * op) {
            mlir::Attribute answer;
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
                if (!known.fromClosure(unary.getOperand())) { return; }
                const pe::value input = known.shown(unary.getOperand());
                answer = unary.getKind() == ctjs::UnaryKind::TypeOf &&
                                 input.tag == pe::value::kind::reference
                             ? mlir::Attribute(ctjs::StringAttr::get(context, "function"))
                             : pe::unary(unary.getKind(), input, context).constant;
            } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
                if (!known.fromClosure(compare.getLhs()) && !known.fromClosure(compare.getRhs())) {
                    return;
                }
                answer = pe::compare(compare.getKind(), known.shown(compare.getLhs()),
                                     known.shown(compare.getRhs()), context)
                             .constant;
            } else if (auto test = llvm::dyn_cast<ctjs::TruthyOp>(op)) {
                if (!known.fromClosure(test.getValue())) { return; }
                const std::optional<bool> truth = pe::truthy(known.shown(test.getValue()));
                // Only a condition the branches it decides give way to: a
                // constant condition left on a kept branch would make the
                // type solve treat the calls in its dead arm as unreachable.
                if (!truth || !llvm::all_of(op->getUsers(), [&](mlir::Operation * user) {
                        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(user);
                        return branch &&
                               onlyValues(*truth ? branch.getElseRegion() : branch.getThenRegion());
                    })) {
                    return;
                }
                answer = mlir::BoolAttr::get(context, *truth);
            }
            if (!answer) { return; }
            mlir::OpBuilder at(op);
            mlir::Operation * folded =
                llvm::isa<ctjs::TruthyOp>(op)
                    ? mlir::arith::ConstantOp::create(at, op->getLoc(),
                                                      llvm::cast<mlir::BoolAttr>(answer))
                          .getOperation()
                    : ctjs::ConstantOp::create(at, op->getLoc(), op->getResult(0).getType(), answer)
                          .getOperation();
            known.made.insert(folded);
            const auto inputs = answers::constantsOf(op->getOperands(), nullptr);
            op->getResult(0).replaceAllUsesWith(folded->getResult(0));
            op->erase();
            known.eraseUnused(inputs);
            changed = true;
        });
        llvm::SmallVector<mlir::scf::IfOp> branches;
        module.walk([&](mlir::scf::IfOp branch) { branches.push_back(branch); });
        for (mlir::scf::IfOp branch : branches) {
            mlir::Operation * condition = branch.getCondition().getDefiningOp();
            if (!known.made.contains(condition)) { continue; }
            const bool take = mlir::matchPattern(condition, mlir::m_One());
            mlir::Region & dropped = take ? branch.getElseRegion() : branch.getThenRegion();
            auto inputs = answers::constantsOf(branch.getCondition(), nullptr);
            if (!dropped.empty()) {
                const auto yielded = answers::constantsOf(
                    dropped.front().getTerminator()->getOperands(), branch.getOperation());
                inputs.insert(yielded.begin(), yielded.end());
            }
            dropped.walk([&](mlir::Operation * gone) { known.made.erase(gone); });
            symbolic::select(branch, take);
            known.eraseUnused(inputs);
            changed = true;
        }
    }
}

} // namespace ctcompile::ctnative::lowering_detail
