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
// An array destructuring is an `__ctbrowser_iter_open`, one
// `__ctbrowser_iter_next` per binding and a close; over an array that is its
// elements in order, so a flat pattern becomes index reads.
//
// Both rely on the realm's Array iteration being the original: the
// destructuring calls Array.prototype[Symbol.iterator] and its iterator's
// `next`. arrayIterationUnproved proves the program cannot change them before
// either fold runs.
//
//===----------------------------------------------------------------------===//

#include "ForOf.h"

#include "ctcompile/CTJS/Lowering/NativeRealm.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

#include "llvm/ADT/STLExtras.h"

#include <optional>
#include <string>

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

/// Folds the array destructuring whose iterator `open` makes, or says why
/// not. The steps are the `__ctbrowser_iter_next` calls on the record in
/// `open`'s block, in order; anything else on the record but its close, or a
/// step used as a pattern of its own, is a shape this fold does not handle.
std::optional<std::string> foldDestructuring(ctjs::CallOp open) {
    llvm::SmallVector<ctjs::CallOp> steps;
    llvm::SmallVector<ctjs::CallOp> closes;
    for (mlir::Operation * user : open.getResult().getUsers()) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(user);
        const llvm::StringRef helper = call ? helperOf(call) : llvm::StringRef{};
        if (helper == "__ctbrowser_iter_close") {
            closes.push_back(call);
            continue;
        }
        if (helper != "__ctbrowser_iter_next") {
            return std::string("array destructuring whose iterator is used by `") +
                   user->getName().getStringRef().str() + "`";
        }
        if (call->getBlock() != open->getBlock()) {
            return std::string("array destructuring with a rest element, which steps the "
                               "iterator in a loop");
        }
        steps.push_back(call);
    }
    for (ctjs::CallOp step : steps) {
        for (mlir::Operation * user : step.getResult().getUsers()) {
            auto call = llvm::dyn_cast<ctjs::CallOp>(user);
            if (call && helperOf(call) == "__ctbrowser_iter_open") {
                return std::string("a nested array destructuring pattern");
            }
            auto compare = llvm::dyn_cast<ctjs::CompareOp>(user);
            if (compare && (ctjs::isUndefinedConstant(compare.getLhs()) ||
                            ctjs::isUndefinedConstant(compare.getRhs()))) {
                return std::string("array destructuring with a default value");
            }
        }
    }
    llvm::sort(steps,
               [](ctjs::CallOp a, ctjs::CallOp b) { return a->isBeforeInBlock(b.getOperation()); });
    mlir::Value row = open.getArgs().front();
    for (auto [index, step] : llvm::enumerate(steps)) {
        mlir::OpBuilder at(step);
        auto key =
            ctjs::ConstantOp::create(at, step.getLoc(), row.getType(),
                                     ctjs::numberAttr(at.getContext(), static_cast<double>(index)));
        auto read = ctjs::GetPropertyOp::create(at, step.getLoc(), row.getType(), row, key);
        read->setAttr(kDestructureArray, at.getUnitAttr());
        step.getResult().replaceAllUsesWith(read.getResult());
    }
    llvm::SmallVector<mlir::Operation *> gone;
    for (ctjs::CallOp call : llvm::concat<ctjs::CallOp>(steps, closes)) {
        gone.push_back(call.getCallee().getDefiningOp());
        call.erase();
    }
    gone.push_back(open.getCallee().getDefiningOp());
    open.erase();
    for (mlir::Operation * helper : gone) {
        if (helper && helper->use_empty()) { helper->erase(); }
    }
    return std::nullopt;
}

/// The value `global.key` reads, when `read` is that read of a load of the
/// realm's `global`.
bool readsGlobalMember(ctjs::GetPropertyOp read, llvm::StringRef global, llvm::StringRef key) {
    auto load = read.getObject().getDefiningOp<ctjs::LoadGlobalOp>();
    return load && load.getName() == global && ctjs::constantKey(read.getKey()) == key;
}

} // namespace

std::optional<std::string> arrayIterationUnproved(mlir::ModuleOp module) {
    auto rows = module->getAttrOfType<mlir::ArrayAttr>("ctjs.globals");
    const bool dynamicWrites = module->hasAttr("ctjs.global_escapes");
    for (llvm::StringRef name : {"Array", "Symbol"}) {
        // A name the program mentions has a row; one it never mentions is
        // the realm's unless a dynamic global write could bind it.
        std::optional<bool> host;
        for (auto row : rows ? rows.getAsRange<mlir::DictionaryAttr>()
                             : mlir::ArrayAttr::get(module.getContext(), {})
                                   .getAsRange<mlir::DictionaryAttr>()) {
            auto bound = row.getAs<mlir::StringAttr>("name");
            if (bound && bound.getValue() == name) {
                host = row.contains(ctjs::kGlobalsHostBinding);
            }
        }
        if (host ? !*host : dynamicWrites || !rows) {
            return ("`" + name + "` may be rebound").str();
        }
    }
    std::optional<std::string> why;
    module.walk([&](ctjs::GetPropertyOp read) {
        const llvm::StringRef key = ctjs::constantKey(read.getKey());
        if (key == "__proto__" || key == "getPrototypeOf" || key == "setPrototypeOf") {
            why = ("the program reads `" + key + "`, which reaches a prototype").str();
        } else if (auto owner = read.getObject().getDefiningOp<ctjs::GetPropertyOp>();
                   key == "prototype" && owner &&
                   ctjs::constantKey(owner.getKey()) == "constructor") {
            why = "the program reads `constructor.prototype`, which can be Array.prototype";
        } else if (readsGlobalMember(read, "Array", "prototype")) {
            for (mlir::OpOperand & use : read.getResult().getUses()) {
                auto member = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                if (!member || use.getOperandNumber() != 0 ||
                    ctjs::constantKey(member.getKey()).empty()) {
                    why = "the program uses Array.prototype other than to read a method";
                }
            }
        } else if (readsGlobalMember(read, "Symbol", "iterator")) {
            for (mlir::OpOperand & use : read.getResult().getUses()) {
                if (!llvm::isa<ctjs::GetPropertyOp>(use.getOwner()) ||
                    use.getOperandNumber() != 1) {
                    why = "the program uses Symbol.iterator other than to read an iterator";
                }
            }
        }
        return why ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
    });
    return why;
}

ForOfOriginals foldArrayForOf(mlir::ModuleOp module) {
    const std::optional<std::string> unproved = arrayIterationUnproved(module);
    ForOfOriginals originals;
    module.walk([&](ctjs::FuncOp fn) {
        llvm::SmallVector<ctjs::CallOp> opens;
        llvm::SmallVector<ctjs::CallOp> patterns;
        fn.walk([&](ctjs::CallOp call) {
            if (call.getArgs().size() != 1 || !ctjs::isUndefinedConstant(call.getReceiver())) {
                return;
            }
            if (helperOf(call) == "__ctbrowser_for_of_open") { opens.push_back(call); }
            if (helperOf(call) == "__ctbrowser_iter_open") { patterns.push_back(call); }
        });
        if (opens.empty() && patterns.empty()) { return; }
        // Without the proof nothing folds, and every open says why.
        if (unproved) {
            const auto reason = mlir::StringAttr::get(
                fn.getContext(),
                "array iteration needs the realm's own Array iterator: " + *unproved);
            for (ctjs::CallOp open : llvm::concat<ctjs::CallOp>(opens, patterns)) {
                open->setAttr(kIterationRefusal, reason);
                open.getCallee().getDefiningOp()->setAttr(kIterationRefusal, reason);
            }
            return;
        }
        mlir::OwningOpRef<ctjs::FuncOp> original(llvm::cast<ctjs::FuncOp>(fn->clone()));
        bool changed = false;
        for (ctjs::CallOp open : opens) { changed |= fold(open); }
        // Outer patterns first: an inner one opens on an outer step, so it
        // refuses as nested whether or not the outer folded.
        for (ctjs::CallOp open : patterns) {
            if (auto why = foldDestructuring(open)) {
                // On the helper's load too, which admission meets first.
                const auto reason = mlir::StringAttr::get(open.getContext(), *why);
                open->setAttr(kIterationRefusal, reason);
                open.getCallee().getDefiningOp()->setAttr(kIterationRefusal, reason);
            } else {
                changed = true;
            }
        }
        if (changed) { originals.emplace_back(fn, std::move(original)); }
    });
    return originals;
}

} // namespace ctcompile::ctnative
