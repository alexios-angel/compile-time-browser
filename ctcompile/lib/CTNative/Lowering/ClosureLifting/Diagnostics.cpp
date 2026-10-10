//===- ClosureLifting/Diagnostics.cpp - The open-literal census -----------===//
//
// Labels the uses that keep a method-bearing object literal open and counts
// them in the open-literal census (see the census fields in
// ClosureLifter.h). The census is a measurement for choosing what to support
// next; it does not change what is lifted.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

/// Labels one blocking use by its operation and operand position, without
/// judging it. The label is refined only where the operand number alone
/// would merge two genuinely different things (a dynamic key and a value
/// store are both `set_property`).
std::string closureLifter::blockingLabel(mlir::Value object, mlir::OpOperand & use) {
    mlir::Operation * user = use.getOwner();
    const unsigned n = use.getOperandNumber();
    if (llvm::isa<ctjs::GetPropertyOp>(user)) { return n == 0 ? "get.dynamic-key" : "get.as-key"; }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
        if (n == 1) { return "set.as-key"; }
        if (n == 2) {
            // Where it is stored decides whether it needs an owner. Stored
            // into another literal made here, it could be a member of that
            // literal's class, which owns it outright; anywhere else it
            // outlives the frame that made it.
            mlir::Value into = set.getObject();
            if (!into.getDefiningOp<ctjs::CreateObjectOp>()) {
                return "set.stored-into.not-a-literal";
            }
            if (ctjs::constantKey(set.getKey()).empty()) {
                return "set.stored-into.a-literal-under-a-dynamic-key";
            }
            return closedAfterLift(into) ? "set.stored-into.a-closed-literal"
                                         : "set.stored-into.an-open-literal";
        }
        return "set.dynamic-key";
    }
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
        if (n == 0) { return "call.as-callee"; }
        if (n >= 2) { return "call.argument." + argumentDetail(user, n - 2); }
        auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (!load) { return "call.receiver-callee-not-a-load"; }
        if (load.getObject() != object) { return "call.receiver-callee-off-another-object"; }
        return "call.receiver-dynamic-key";
    }
    if (llvm::isa<ctjs::CallDirectOp>(user)) {
        // A ctjs.call that --ctjs-resolve-globals rewrote into
        // ctjs.call_direct is labelled like the original call
        // (`call.argument.<detail>`), so the census keeps the callee-known /
        // callee-opaque split instead of merging both into
        // `call_direct.argument`.
        if (n == 0) { return "call_direct.receiver"; }
        if (n >= 3 && closureCalledBy(user)) {
            return "call.argument." + argumentDetail(user, n - 3);
        }
        return "call_direct.argument";
    }
    if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(user)) {
        if (n == 0) { return "construct.callee"; }
        return made.getCallee().getDefiningOp<ctjs::CreateClosureOp>()
                   ? "construct.argument.callee-known"
                   : "construct.argument.callee-opaque";
    }
    if (auto append = llvm::dyn_cast<ctjs::AppendOp>(user); append && n == 1) {
        return TypeInference::isDenseVectorSite(append.getArray()) ? "append.into-a-dense-array"
                                                                   : "append.into-an-open-array";
    }
    return (user->getName().getStringRef() + "#" + llvm::Twine(n)).str();
}

/// Is the callee one known function, and does it only read the argument?
/// This is the question the receiver lift asks of `this`, asked of an
/// argument position, because if the answer is yes the carrier is the same.
std::string closureLifter::argumentDetail(mlir::Operation * call, unsigned j) {
    ctjs::CreateClosureOp made = closureCalledBy(call);
    if (!made) { return "callee-opaque"; }
    ctjs::FuncOp target = targetOf(made);
    if (!target || target.getBody().empty()) { return "callee-not-imported"; }
    mlir::Block & entry = target.getBody().front();
    // `j` is the JavaScript argument index, not an operand number, because
    // the two call shapes number their operands differently and the entry
    // block does not: argument j always lands on entry argument j + 3.
    const unsigned slot = j + 3;
    if (slot >= entry.getNumArguments()) { return "argument-has-no-parameter"; }
    return onlyConstantKeyAccess(entry.getArgument(slot)) ? "parameter-is-read-only"
                                                          : "parameter-escapes";
}

/// Every use of `object` that `closedAfterLift` would refuse, labelled.
void closureLifter::blockingLabelsOf(mlir::Value object, llvm::StringSet<> & into,
                                     llvm::StringMap<unsigned> * tally) {
    for (mlir::OpOperand & use : object.getUses()) {
        if (isConstantKeyAccess(use)) { continue; }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner())) {
            if (use.getOperandNumber() == 1 && methodCallOn(call, object)) { continue; }
            // Mirror closedAfterLift: an argument passed to a parameter that
            // carries an object is not a blocker. Without this, the
            // sole-blocker count would be wrong for literals passed to such
            // parameters.
            if (use.getOperandNumber() >= 2) {
                auto made = call.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
                if (made && slotCarriesAnObject(made, use.getOperandNumber() - 2)) { continue; }
            }
        }
        const std::string label = blockingLabel(object, use);
        if (tally) { ++(*tally)[label]; }
        into.insert(label);
    }
}

/// The blocking label of the outermost literal in a chain of nested literals,
/// which says whether supporting nested literals would cascade. `{a: {b: {c:
/// {x: 1, m: f}}}}` reports `set.stored-into.an-open-literal` three times,
/// which says nothing; what matters is what blocks the outermost literal,
/// because until that one closes none of the inner ones can be a member of
/// anything. Returns "mixed" when a level has several blockers.
std::string closureLifter::rootBlockingLabel(mlir::Value object, unsigned & depthOut) {
    llvm::SmallPtrSet<mlir::Operation *, 8> seen;
    mlir::Value at = object;
    for (unsigned depth = 0;; ++depth) {
        llvm::StringSet<> here;
        blockingLabelsOf(at, here, nullptr);
        depthOut = depth;
        if (here.size() != 1) { return "mixed"; }
        const llvm::StringRef only = here.begin()->first();
        if (only != "set.stored-into.an-open-literal") { return only.str(); }
        mlir::Value into;
        for (mlir::OpOperand & use : at.getUses()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            if (set && use.getOperandNumber() == 2) { into = set.getObject(); }
        }
        if (!into || !seen.insert(into.getDefiningOp()).second) { return "a-cycle"; }
        at = into;
    }
}

/// Adds one open object literal to the census: its blocking labels, its sole
/// blocker if it has one, and the label at the root of its nesting chain.
void closureLifter::censusOpenLiteral(mlir::Value object) {
    // Only literals that hold a method field are counted: one with no method
    // field is refused for some other reason and would drown the count.
    // Weighted by how many method fields it holds as well as counted once,
    // because a refusal is per method field, not per literal.
    unsigned fields = 0;
    for (mlir::Operation * user : object.getUsers()) {
        auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
        if (set && set.getObject() == object && !ctjs::constantKey(set.getKey()).empty() &&
            set.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
            ++fields;
        }
    }
    if (fields == 0) { return; }
    ++censusOpenObjects;
    censusMethodFields += fields;
    llvm::StringSet<> here;
    blockingLabelsOf(object, here, &censusUses);
    if (here.size() == 1) {
        censusSole[here.begin()->first()] += 1;
        censusSoleFields[here.begin()->first()] += fields;
    }
    unsigned depth = 0;
    const std::string root = rootBlockingLabel(object, depth);
    ++censusRoot[root];
    ++censusDepth[std::to_string(depth)];
    censusExample.try_emplace(root, object.getLoc());
}

} // namespace ctcompile::ctnative::lowering_detail
