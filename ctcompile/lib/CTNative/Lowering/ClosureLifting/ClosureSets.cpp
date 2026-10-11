//===- ClosureLifting/ClosureSets.cpp - Calls through a closure set -------===//
//
// Lifts closures whose values meet before they are called, such as
//
//   const f = c ? (x => x + k) : (x => x * 3);
//   return f(5) + f(1);
//
// No call site names one closure, so none can receive the captures the way a
// lift passes them. Instead each closure stays a value bound to its captures,
// and the value the call goes through is one of that finite set. Type
// inference gives it the lattice's `closure_set` type, and the emitted C++ is
// a std::variant of ctnative::bound members, which a call dispatches with
// std::visit (Runtime/Callable.hpp):
//
//   std::variant<ctnative::bound<fn_2, js_num>, ctnative::bound<fn_3>> v8 = ...;
//   ctnative::invoke_callable(v8, 5.0);
//
// Closure lifting decides the structure: every value in the group is a
// closure or an scf.if result fed only by the group's values, and every use is
// a call through it or another such merge, so no value leaves the frame and
// the bound copies of the captures are all any call reads. Each member lifts
// the way a borrowed callback does (BorrowedCallbacks.cpp) and carries the
// same attributes, so type inference types the members and the calls, and
// admission requires one signature for all of them. The `ctnative.closure_set`
// attribute on a member's closure names the whole set, so emission
// (EmitC/ClosureSets.cpp) can spell the variant where the closure is made.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include "ctcompile/CTNative/Analysis/NativeClosure.h"

#include "mlir/Dialect/SCF/IR/SCF.h"

#include <array>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// The scf.if result that `use`, an operand of the scf.yield ending one of the
/// scf.if's branches, becomes; null for any other use.
mlir::Value mergedBy(mlir::OpOperand & use) {
    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(use.getOwner());
    auto merge = yield ? llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp()) : nullptr;
    return merge ? merge.getResult(use.getOperandNumber()) : mlir::Value{};
}

/// The values the two branches of `merge`'s scf.if yield for it.
std::array<mlir::Value, 2> sourcesOf(mlir::OpResult merge) {
    auto branch = llvm::cast<mlir::scf::IfOp>(merge.getOwner());
    const unsigned at = merge.getResultNumber();
    if (branch.getElseRegion().empty()) { return {mlir::Value{}, mlir::Value{}}; }
    return {branch.thenYield().getOperand(at), branch.elseYield().getOperand(at)};
}

} // namespace

/// Groups the closures whose values meet at scf.if results and are otherwise
/// only called, and keeps the groups of two or more closures with at least
/// one call.
void closureLifter::closureSetCensus() {
    closureSets.clear();
    llvm::DenseSet<mlir::Operation *> seen;
    for (ctjs::CreateClosureOp start : closures) {
        if (seen.contains(start.getOperation()) || lifted.contains(start.getOperation()) ||
            isDeclarationClosure(start)) {
            continue;
        }
        // Grow the group from both ends: forward through the merges a value
        // feeds, backward through the values a merge is fed by.
        closureSet group;
        llvm::SetVector<mlir::Value> values;
        values.insert(start.getResult());
        bool closed = true;
        for (size_t i = 0; i < values.size() && closed; ++i) {
            const mlir::Value v = values[i];
            for (mlir::OpOperand & use : v.getUses()) {
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                    call && use.getOperandNumber() == 0) {
                    group.calls.insert(call);
                } else if (const mlir::Value merged = mergedBy(use)) {
                    values.insert(merged);
                } else {
                    closed = false;
                    break;
                }
            }
            if (auto merge = llvm::dyn_cast<mlir::OpResult>(v);
                merge && llvm::isa<mlir::scf::IfOp>(merge.getOwner())) {
                for (mlir::Value source : sourcesOf(merge)) {
                    if (!source ||
                        (!source.getDefiningOp<ctjs::CreateClosureOp>() &&
                         !llvm::isa_and_nonnull<mlir::scf::IfOp>(source.getDefiningOp()))) {
                        closed = false;
                        break;
                    }
                    values.insert(source);
                }
            }
        }
        for (mlir::Value v : values) {
            if (auto made = v.getDefiningOp<ctjs::CreateClosureOp>()) {
                seen.insert(made.getOperation());
                group.members.push_back(made);
            }
        }
        if (!closed || group.members.size() < 2 || group.calls.empty()) { continue; }
        bool eligible = true;
        for (ctjs::CreateClosureOp c : group.members) {
            ctjs::FuncOp target = targetOf(c);
            eligible &= target && !lifted.contains(c.getOperation()) && !isDeclarationClosure(c) &&
                        uniqueClosureByTarget.lookup(target.getOperation()) == c;
        }
        if (!eligible) { continue; }
        auto shared = std::make_shared<closureSet>(std::move(group));
        for (ctjs::CreateClosureOp c : shared->members) { closureSets[c.getOperation()] = shared; }
    }
}

/// Why `c`, one closure of a set, cannot be lifted as a member.
std::optional<std::string> closureLifter::whyNotClosureSetMember(ctjs::CreateClosureOp c) {
    if (auto why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    // A call through the set passes no receiver or new.target the member
    // could read, and its argument count is fixed per call.
    if (!entry.getArgument(ctjs::arg_receiver).use_empty()) {
        return "a closure called through a set of closures reads `this`";
    }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return "a closure called through a set of closures reads new.target";
    }
    // An arrow's lexical `this` (ArrowThis.cpp) is an object capture, which
    // a member's by-value copy does not carry yet.
    if (target->hasAttr("ctnative.lexical_this")) {
        return "a closure called through a set of closures captures its lexical `this`, which a "
               "member does not carry";
    }
    if (ctjs::readsRawArguments(target)) {
        return "a closure called through a set of closures reads its `arguments`";
    }
    const unsigned declared = entry.getNumArguments() - ctjs::implicit_arguments;
    for (ctjs::CallOp call : closureSets.lookup(c.getOperation())->calls) {
        const auto passed = static_cast<unsigned>(call.getArgs().size());
        if (passed != declared) {
            return "a closure called through a set of closures declares " +
                   std::to_string(declared) + " parameters and a call through the set passes " +
                   std::to_string(passed) + " - one C++ signature needs the same count";
        }
    }
    // The member holds copies of its captures, made where the closure is: a
    // binding written afterwards would need the variable's address.
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i)) {
            return "a closure called through a set of closures captures binding " +
                   std::to_string(i) +
                   ", which is written after it is captured - a member takes its captures "
                   "by value";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    return whyLiftTailFails(c, target, std::array<mlir::Operation *, 1>{c.getOperation()});
}

/// Why `c` is not lifted as a member of its set: its own reason, or another
/// member's, since the set has one representation for all of them.
std::optional<std::string> closureLifter::whyNotInClosureSet(ctjs::CreateClosureOp c) {
    if (auto why = whyNotClosureSetMember(c)) { return why; }
    for (ctjs::CreateClosureOp other : closureSets.lookup(c.getOperation())->members) {
        if (other == c) { continue; }
        if (auto why = whyNotClosureSetMember(other)) {
            return "another closure called through the same set is not lifted: " + *why;
        }
    }
    return std::nullopt;
}

/// Keeps `c` as a value bound to its lifted captures, names `target` on it and
/// on every call through the set, and names the whole set on `c`. lift() has
/// made the captures `target`'s leading parameters.
void closureLifter::rewriteClosureSetMember(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                            liftReport & out) {
    const auto captured = liftedCaptures(c);
    c.getUpvaluesMutable().assign(captured);
    c.removeEnclosingIndicesAttr();
    mlir::OpBuilder at(c);
    const mlir::Value undefined = undefinedAt(at, c.getLoc());
    c.getEnclosingClosureMutable().assign(undefined);
    c.getEnclosingThisMutable().assign(undefined);
    const auto symbol = mlir::FlatSymbolRefAttr::get(target.getSymNameAttr());
    c->setAttr(kNativeBorrowedCallback, symbol);
    c->removeAttr(kNativeClosureReason);
    target->setAttr(kNativeBorrowedCallback, mlir::UnitAttr::get(context));
    const closureSet & set = *closureSets.lookup(c.getOperation());
    llvm::SmallVector<llvm::StringRef> names;
    for (ctjs::CreateClosureOp member : set.members) {
        names.push_back(targetOf(member).getSymName());
    }
    llvm::sort(names);
    llvm::SmallVector<mlir::Attribute> members;
    for (llvm::StringRef name : names) {
        members.push_back(mlir::FlatSymbolRefAttr::get(context, name));
    }
    c->setAttr(kNativeClosureSet, mlir::ArrayAttr::get(context, members));
    for (ctjs::CallOp call : set.calls) {
        llvm::SmallVector<mlir::Attribute> callbacks;
        if (auto listed = call->getAttrOfType<mlir::ArrayAttr>(kNativeBorrowedCall)) {
            llvm::append_range(callbacks, listed.getValue());
        }
        callbacks.push_back(symbol);
        call->setAttr(kNativeBorrowedCall, mlir::ArrayAttr::get(context, callbacks));
        call->setAttr(kNativeClosureSet, mlir::UnitAttr::get(context));
    }
    // No ctjs.call_direct names the member, and MLIR's DeadCodeAnalysis treats
    // a private function nothing calls as dead, so it stays public, as a
    // borrowed callback does; its parameters are typed from the closure and
    // the calls (Analysis/Inference/BorrowedCallbacks.cpp).
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    ++out.closures;
    out.captures += static_cast<unsigned>(captured.size());
}

} // namespace ctcompile::ctnative::lowering_detail
