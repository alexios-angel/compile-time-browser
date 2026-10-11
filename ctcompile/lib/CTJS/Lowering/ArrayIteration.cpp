//===- ArrayIteration.cpp - Fold iteration over known arrays --------------===//
//
// The importer compiles `for (const [k, v] of Object.entries(o))` to calls of
// the VM's iteration natives: __ctbrowser_for_of_open picks the index loop or
// the iterator protocol at run time, the loop closes the iterator on every
// exit (inside a handler when the body can throw), and the destructuring opens,
// steps and closes an iterator over each pair. When the iterated value is an
// array made before the loop, every one of those decisions is known here, and
// folding them leaves an index loop over plain property reads, which native
// lowering and the structuring pass can follow. ArrayIteration.h lists the
// folds and what they assume.
//
//===----------------------------------------------------------------------===//
#include "ArrayIteration.h"
#include "Globals/RegisterFlow.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::ctjs {

namespace {

constexpr llvm::StringLiteral forOfOpen = "__ctbrowser_for_of_open";
constexpr llvm::StringLiteral iteratorOpen = "__ctbrowser_iter_open";
constexpr llvm::StringLiteral iteratorNext = "__ctbrowser_iter_next";
constexpr llvm::StringLiteral iteratorClose = "__ctbrowser_iter_close";

/// The global a call's callee is read from, or empty. The importer passes
/// every register through block arguments, so the read can be blocks away.
llvm::StringRef calleeName(CallOp call) {
    const mlir::Value origin = globals_detail::registerFlowOrigin(call.getCallee());
    auto load = origin ? origin.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
    return load ? load.getName() : llvm::StringRef{};
}

/// True when no store in the module replaces global `name`.
bool neverStored(mlir::Operation * at, llvm::StringRef name) {
    const auto stored = at->getParentOfType<mlir::ModuleOp>()->walk([&](StoreGlobalOp store) {
        return store.getName() == name ? mlir::WalkResult::interrupt()
                                       : mlir::WalkResult::advance();
    });
    return !stored.wasInterrupted();
}

/// True when the program writes no property of the global `Object` that
/// could replace one of its statics: no write through a dynamic key, and none
/// to `keys`, `values` or `entries`.
bool objectStaticsIntact(mlir::Operation * at) {
    const auto written = at->getParentOfType<mlir::ModuleOp>()->walk([](SetPropertyOp write) {
        const mlir::Value object = globals_detail::registerFlowOrigin(write.getObject());
        auto load = object ? object.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
        if (!load || load.getName() != "Object") { return mlir::WalkResult::advance(); }
        const llvm::StringRef key = constantKey(write.getKey());
        return key.empty() || key == "keys" || key == "values" || key == "entries"
                   ? mlir::WalkResult::interrupt()
                   : mlir::WalkResult::advance();
    });
    return !written.wasInterrupted();
}

/// "keys", "values" or "entries" when `value` is the result of that static
/// method of the realm's `Object`, else empty.
llvm::StringRef objectStatic(mlir::Value value) {
    value = globals_detail::registerFlowOrigin(value);
    auto call = value ? value.getDefiningOp<CallOp>() : CallOp{};
    GetPropertyOp read = call ? receiverMethod(call) : GetPropertyOp{};
    auto object = read ? read.getObject().getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
    if (!object || object.getName() != "Object" || call.getReceiver() != read.getObject() ||
        !neverStored(call, "Object") || !objectStaticsIntact(call)) {
        return {};
    }
    // Any argument: the result is always a new Array, and a null or
    // undefined argument throws at the call, before any loop over it.
    const llvm::StringRef key = constantKey(read.getKey());
    return key == "keys" || key == "values" || key == "entries" ? key : llvm::StringRef{};
}

/// True when `value` is an element of `Object.entries(...)` read at an index,
/// which is a new two-element [key, value] array.
bool entryPair(mlir::Value value) {
    value = globals_detail::registerFlowOrigin(value);
    auto read = value ? value.getDefiningOp<GetPropertyOp>() : GetPropertyOp{};
    return read && !constantAttr<StringAttr>(read.getKey()) &&
           objectStatic(read.getObject()) == "entries";
}

/// True when `value` is an array made before it is iterated.
bool knownArray(mlir::Value value) {
    return !objectStatic(value).empty() || entryPair(value);
}

mlir::Value undefinedAt(mlir::Operation * at) {
    mlir::OpBuilder builder{at};
    return ConstantOp::create(builder, at->getLoc(), ValueType::get(at->getContext()),
                              UndefinedAttr::get(at->getContext()));
}

/// Replaces `check`, which follows an operation that can no longer throw, by
/// a branch to its continuation.
void dropCheck(CheckOp check) {
    mlir::OpBuilder builder{check};
    mlir::cf::BranchOp::create(builder, check.getLoc(), check.getCont(), check.getContOperands());
    check.erase();
}

/// Erases `call`, which can no longer throw or do anything, answering
/// `replacement` (undefined by default) to its uses, and the check after it.
/// Returns what the uses now read.
mlir::Value eraseCall(CallOp call, mlir::Value replacement = {}) {
    if (!replacement) { replacement = undefinedAt(call); }
    call.getResult().replaceAllUsesWith(replacement);
    if (auto check = llvm::dyn_cast_or_null<CheckOp>(call->getNextNode())) { dropCheck(check); }
    call.erase();
    return replacement;
}

/// Folds one destructuring of an entry pair: the n-th step is `pair[n]` (or
/// undefined past the second) and the close does nothing. Only when every use
/// of the record is a step or a close in the open's own block, so the steps
/// run in block order.
bool foldPairDestructuring(CallOp open) {
    const mlir::Value record = open.getResult();
    llvm::SmallVector<CallOp> uses;
    for (mlir::OpOperand & use : record.getUses()) {
        auto call = llvm::dyn_cast<CallOp>(use.getOwner());
        if (!call || call->getBlock() != open->getBlock() || call.getArgs().empty() ||
            call.getArgs().front() != record ||
            (calleeName(call) != iteratorNext && calleeName(call) != iteratorClose)) {
            return false;
        }
        uses.push_back(call);
    }
    llvm::sort(uses, [](CallOp a, CallOp b) { return a->isBeforeInBlock(b); });
    const mlir::Value pair = open.getArgs().front();
    unsigned step = 0;
    for (CallOp use : uses) {
        if (calleeName(use) == iteratorClose) {
            eraseCall(use);
            continue;
        }
        mlir::Value value;
        if (step < 2) {
            mlir::OpBuilder builder{use};
            const mlir::Value index =
                ConstantOp::create(builder, use.getLoc(), ValueType::get(use.getContext()),
                                   numberAttr(use.getContext(), static_cast<double>(step)));
            value = GetPropertyOp::create(builder, use.getLoc(), ValueType::get(use.getContext()),
                                          pair, index);
        }
        ++step;
        eraseCall(use, value);
    }
    eraseCall(open);
    return true;
}

/// True when `value` is the constant undefined.
bool undefinedConstant(mlir::Value value) {
    const mlir::Value origin = globals_detail::registerFlowOrigin(value);
    return origin && isUndefinedConstant(origin);
}

} // namespace

unsigned foldArrayIteration(mlir::FunctionOpInterface body) {
    unsigned folded = 0;
    // The undefined records of the loops folded to their index path, and the
    // tests of them those loops make, so only those tests are decided here.
    llvm::DenseSet<mlir::Value> records, decided;
    const auto isRecord = [&](mlir::Value value) {
        const mlir::Value origin = globals_detail::registerFlowOrigin(value);
        return origin && records.contains(origin);
    };
    // `ctjs.iterable` of an array is the array itself.
    llvm::SmallVector<IterableOp> iterables;
    body->walk([&](IterableOp iterable) { iterables.push_back(iterable); });
    for (IterableOp iterable : iterables) {
        if (!knownArray(iterable.getSource()) || entryPair(iterable.getSource())) { continue; }
        iterable.getResult().replaceAllUsesWith(iterable.getSource());
        if (auto check = llvm::dyn_cast_or_null<CheckOp>(iterable->getNextNode())) {
            dropCheck(check);
        }
        iterable.erase();
        ++folded;
    }
    llvm::SmallVector<CallOp> calls;
    body->walk([&](CallOp call) { calls.push_back(call); });
    for (CallOp call : calls) {
        const llvm::StringRef name = calleeName(call);
        if (call.getArgs().empty() || !name.starts_with("__ctbrowser_") ||
            !neverStored(call, name)) {
            continue;
        }
        const mlir::Value source = call.getArgs().front();
        if (name == forOfOpen && knownArray(source) && !entryPair(source)) {
            // The index path: the record is undefined.
            records.insert(eraseCall(call));
            ++folded;
        }
    }
    // A close of an undefined record does nothing; the comparisons that chose
    // the index path are now between two undefined constants.
    const auto closeUndefined = [&] {
        calls.clear();
        body->walk([&](CallOp call) { calls.push_back(call); });
        for (CallOp call : calls) {
            if (calleeName(call) == iteratorClose && !call.getArgs().empty() &&
                undefinedConstant(call.getArgs().front()) && neverStored(call, iteratorClose)) {
                eraseCall(call);
                ++folded;
            }
        }
    };
    closeUndefined();
    // Nothing of the protocol was folded: leave the function as it is (a
    // constant branch elsewhere is a shape other proofs read).
    if (folded == 0) { return 0; }
    llvm::SmallVector<CompareOp> compares;
    body->walk([&](CompareOp compare) { compares.push_back(compare); });
    for (CompareOp compare : compares) {
        if (compare.getKind() != CompareKind::StrictEq || !undefinedConstant(compare.getLhs()) ||
            !undefinedConstant(compare.getRhs()) ||
            (!isRecord(compare.getLhs()) && !isRecord(compare.getRhs()))) {
            continue;
        }
        mlir::OpBuilder builder{compare};
        const mlir::Value same =
            ConstantOp::create(builder, compare.getLoc(), compare.getResult().getType(),
                               BooleanAttr::get(compare.getContext(), true));
        decided.insert(same);
        compare.getResult().replaceAllUsesWith(same);
        compare.erase();
        ++folded;
    }
    llvm::SmallVector<TruthyOp> truths;
    body->walk([&](TruthyOp truthy) { truths.push_back(truthy); });
    for (TruthyOp truthy : truths) {
        const mlir::Value origin = globals_detail::registerFlowOrigin(truthy.getValue());
        auto boolean = constantAttr<BooleanAttr>(truthy.getValue());
        if (!boolean || !origin || !decided.contains(origin)) { continue; }
        mlir::OpBuilder builder{truthy};
        const mlir::Value bit = mlir::arith::ConstantOp::create(
            builder, truthy.getLoc(), builder.getBoolAttr(boolean.getValue()));
        decided.insert(bit);
        truthy.getResult().replaceAllUsesWith(bit);
        truthy.erase();
        ++folded;
    }
    // A branch on a condition the folds above decided takes its one side.
    llvm::SmallVector<mlir::cf::CondBranchOp> branches;
    body->walk([&](mlir::cf::CondBranchOp branch) { branches.push_back(branch); });
    for (mlir::cf::CondBranchOp branch : branches) {
        if (!decided.contains(branch.getCondition())) { continue; }
        auto bit = branch.getCondition().getDefiningOp<mlir::arith::ConstantOp>();
        auto value = llvm::dyn_cast<mlir::BoolAttr>(bit.getValue());
        mlir::OpBuilder builder{branch};
        if (value.getValue()) {
            mlir::cf::BranchOp::create(builder, branch.getLoc(), branch.getTrueDest(),
                                       branch.getTrueDestOperands());
        } else {
            mlir::cf::BranchOp::create(builder, branch.getLoc(), branch.getFalseDest(),
                                       branch.getFalseDestOperands());
        }
        branch.erase();
        ++folded;
    }
    // With the protocol's paths gone, each pair a loop reads reaches its
    // destructuring through block arguments with one origin.
    mlir::IRRewriter rewriter{body->getContext()};
    (void)mlir::eraseUnreachableBlocks(rewriter, body.getFunctionBody());
    llvm::SmallVector<CallOp> opens;
    body->walk([&](CallOp call) {
        if (calleeName(call) == iteratorOpen && !call.getArgs().empty() &&
            entryPair(call.getArgs().front()) && neverStored(call, iteratorOpen) &&
            neverStored(call, iteratorNext) && neverStored(call, iteratorClose)) {
            opens.push_back(call);
        }
    });
    for (CallOp open : opens) { folded += foldPairDestructuring(open) ? 1 : 0; }
    // A close the first pass saw through a block argument with two origins.
    closeUndefined();
    folded += foldIteratorCloseHandlers(body);
    // The natives' reads are left without a use.
    llvm::SmallVector<LoadGlobalOp> loads;
    body->walk([&](LoadGlobalOp load) {
        if (load.getName().starts_with("__ctbrowser_") && load->use_empty()) {
            loads.push_back(load);
        }
    });
    for (LoadGlobalOp load : loads) { load.erase(); }
    return folded;
}

unsigned foldIteratorCloseHandlers(mlir::FunctionOpInterface body) {
    unsigned folded = 0;
    llvm::SmallVector<PushHandlerOp> pushes;
    body->walk([&](PushHandlerOp push) { pushes.push_back(push); });
    for (PushHandlerOp push : pushes) {
        // Every block the protected region runs, up to its pop_handler, must
        // hold nothing that can throw: constants, reads of the VM's natives
        // (always defined), branches and the checks between them.
        llvm::SmallVector<mlir::Block *> work{push.getBody()};
        llvm::SmallPtrSet<mlir::Block *, 8> seen;
        llvm::SmallVector<CheckOp> checks;
        llvm::SmallVector<PopHandlerOp> pops;
        bool inert = true;
        while (inert && !work.empty()) {
            mlir::Block * block = work.pop_back_val();
            if (!seen.insert(block).second) { continue; }
            for (mlir::Operation & op : *block) {
                if (auto pop = llvm::dyn_cast<PopHandlerOp>(op)) {
                    pops.push_back(pop);
                    // The rest of the block runs unprotected.
                    break;
                }
                if (auto check = llvm::dyn_cast<CheckOp>(op)) {
                    checks.push_back(check);
                    work.push_back(check.getCont());
                    continue;
                }
                if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(op)) {
                    work.push_back(branch.getDest());
                    continue;
                }
                auto load = llvm::dyn_cast<LoadGlobalOp>(op);
                if (llvm::isa<ConstantOp, mlir::arith::ConstantOp>(op) ||
                    (load && load.getName().starts_with("__ctbrowser_"))) {
                    continue;
                }
                inert = false;
                break;
            }
        }
        if (!inert || pops.empty()) { continue; }
        for (CheckOp check : checks) { dropCheck(check); }
        for (PopHandlerOp pop : pops) { pop.erase(); }
        mlir::OpBuilder builder{push};
        mlir::cf::BranchOp::create(builder, push.getLoc(), push.getBody(), push.getBodyOperands());
        push.erase();
        folded += 1 + static_cast<unsigned>(checks.size() + pops.size());
    }
    return folded;
}

} // namespace ctcompile::ctjs
