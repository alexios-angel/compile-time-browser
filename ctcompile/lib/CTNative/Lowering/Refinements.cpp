//===- Refinements.cpp - Narrowing values a branch tested -----------------===//
//
// A JavaScript value has one SSA name in every region, so a branch that tests
// it cannot change its type by itself. This gives the value a second name,
// `ctnative.refine`, inside the region the test proves something in; type
// inference then types that name by the test (TypeInference::refinedType),
// and the native lowering spells it as the operand or one conversion
// (Runtime/Narrowing.hpp). It runs after the closure lift, which turned the
// cells it could into their values. A cell it kept (one a closure that is not
// lifted captures) is refined through its reads when it still has one
// dominating write and no closure writes it; a binding the branch writes is a
// different SSA value, or a cell with a second write, and is never refined.
//
//===----------------------------------------------------------------------===//

#include "Refinements.h"

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeOps.h"

#include "mlir/Dialect/SCF/IR/SCF.h"

#include "llvm/ADT/StringSwitch.h"

namespace ctcompile::ctnative {

namespace {

/// The `typeof` results a refinement narrows. A new kind is a row here and a
/// rule in TypeInference::refinedType.
bool narrowsTypeof(llvm::StringRef kind) {
    return kind == "string" || kind == "number";
}

/// String methods no Array or object literal shares, so a truthy value read
/// through one is refined without giving an object a use its shape proofs
/// would have to account for.
// ponytail: a fixed list of receivers, not the receiver's type; extend it when
// another truthy-guarded String method needs the narrowing.
bool stringOnlyMethod(llvm::StringRef key) {
    return llvm::StringSwitch<bool>(key)
        .Cases({"split", "trim", "trimStart", "trimEnd", "startsWith", "endsWith"}, true)
        .Cases({"toLowerCase", "toUpperCase", "substring", "charAt", "padStart", "padEnd"}, true)
        .Case("replace", true)
        .Default(false);
}

/// The value a test proves something about, and what: `kind` is a `typeof`
/// result or "truthy", and `inElse` says the proof holds in the else-region.
struct test {
    mlir::Value value;
    llvm::StringRef kind;
    bool inElse = false;
};

std::optional<test> testOf(mlir::scf::IfOp branch) {
    auto truthy = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
    if (!truthy) { return std::nullopt; }
    mlir::Value tested = truthy.getValue();
    bool inElse = false;
    while (auto negated = tested.getDefiningOp<ctjs::UnaryOp>()) {
        if (negated.getKind() != ctjs::UnaryKind::Not) { break; }
        tested = negated.getOperand();
        inElse = !inElse;
    }
    auto compare = tested.getDefiningOp<ctjs::CompareOp>();
    if (!compare) {
        // `v ? ... : ...` and `!v ? ... : ...`.
        return test{tested, "truthy", inElse};
    }
    if (compare.getKind() != ctjs::CompareKind::Eq &&
        compare.getKind() != ctjs::CompareKind::StrictEq) {
        return std::nullopt;
    }
    for (auto [typeOf, name] : {std::pair{compare.getLhs(), compare.getRhs()},
                                std::pair{compare.getRhs(), compare.getLhs()}}) {
        auto unary = typeOf.getDefiningOp<ctjs::UnaryOp>();
        const llvm::StringRef kind = ctjs::constantKey(name);
        if (unary && unary.getKind() == ctjs::UnaryKind::TypeOf && narrowsTypeof(kind)) {
            return test{unary.getOperand(), kind, inElse};
        }
    }
    return std::nullopt;
}

} // namespace

void insertRefinements(mlir::ModuleOp module,
                       llvm::function_ref<bool(ctjs::CreateCellOp)> constantCell) {
    llvm::SmallVector<mlir::scf::IfOp> branches;
    module.walk([&](mlir::scf::IfOp branch) { branches.push_back(branch); });
    for (mlir::scf::IfOp branch : branches) {
        const auto proved = testOf(branch);
        if (!proved) { continue; }
        mlir::Region & region = proved->inElse ? branch.getElseRegion() : branch.getThenRegion();
        if (region.empty()) { continue; }
        // A literal's kind is already known, and its shape proofs count uses.
        if (llvm::isa_and_nonnull<ctjs::CreateObjectOp, ctjs::CreateArrayOp, ctjs::CreateClosureOp>(
                proved->value.getDefiningOp())) {
            continue;
        }
        // The tested value, and the region's other reads of its cell when the
        // cell holds one value at every read.
        llvm::SmallVector<mlir::Value> names{proved->value};
        if (auto read = proved->value.getDefiningOp<ctjs::CellGetOp>()) {
            auto cell = read.getCell().getDefiningOp<ctjs::CreateCellOp>();
            if (cell && constantCell(cell)) {
                for (mlir::Operation * user : cell.getResult().getUsers()) {
                    auto other = llvm::dyn_cast<ctjs::CellGetOp>(user);
                    if (other && region.isAncestor(other->getParentRegion())) {
                        names.push_back(other.getResult());
                    }
                }
            }
        }
        llvm::SmallVector<mlir::OpOperand *> uses;
        for (mlir::Value name : names) {
            for (mlir::OpOperand & use : name.getUses()) {
                if (region.isAncestor(use.getOwner()->getParentRegion())) { uses.push_back(&use); }
            }
        }
        const bool readAsString = llvm::any_of(uses, [](mlir::OpOperand * use) {
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use->getOwner());
            return read && use->getOperandNumber() == 0 &&
                   stringOnlyMethod(ctjs::constantKey(read.getKey()));
        });
        if (uses.empty() || (proved->kind == "truthy" && !readAsString)) { continue; }
        mlir::OpBuilder at = mlir::OpBuilder::atBlockBegin(&region.front());
        auto refined = RefineOp::create(at, proved->value.getLoc(), proved->value.getType(),
                                        proved->value, at.getStringAttr(proved->kind));
        for (mlir::OpOperand * use : uses) { use->set(refined.getResult()); }
    }
}

void dropRefinements(mlir::Operation * root) {
    root->walk([](RefineOp refined) {
        refined.getResult().replaceAllUsesWith(refined.getValue());
        refined.erase();
    });
}

} // namespace ctcompile::ctnative
