// Closed shapes, receivers, cells and dense vector use proofs.
#include "ctcompile/CTNative/Analysis/EscapeAnalysis.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>
#include <optional>

namespace ctcompile::ctnative {
using ctjs::constantKey;

// THE RECEIVER IS A PARAMETER, and `%arg0` of a lifted method is the only block
// argument that names an object. The attribute is written by the lift inside
// --ctnative-lower-to-emitc, which runs BEFORE this analysis is loaded, so by
// the time anything here asks the question the answer is already in the IR -
// and it is one attribute lookup rather than a walk of the module for the call
// sites. That matters: this predicate is asked once per property access per
// solver visit, and a symbol-table walk here would be quadratic in the module.
bool TypeInference::isReceiverArgument(mlir::Value v) {
    auto arg = llvm::dyn_cast<mlir::BlockArgument>(v);
    if (!arg || arg.getArgNumber() != 0 || !arg.getOwner()->isEntryBlock()) { return false; }
    auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
    return fn && fn->hasAttr("ctnative.receiver");
}

// AND THE ARGUMENT FORM, WHICH IS THE SAME ATTRIBUTE LOOKUP ONE OPERAND ALONG.
// `ctnative.object_args` lists entry-block indices rather than a single bit
// because a function may take two of them - `join(a, b)` is one signature with
// two `ctn_x *` - and because the operand number is what the call site and the
// emitted parameter list both count in.
bool TypeInference::namesAnObjectParameter(mlir::Value v) {
    if (isReceiverArgument(v)) { return true; }
    auto arg = llvm::dyn_cast<mlir::BlockArgument>(v);
    if (!arg || !arg.getOwner()->isEntryBlock()) { return false; }
    auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
    if (!fn) { return false; }
    auto listed = fn->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.object_args");
    return listed &&
           llvm::is_contained(listed.asArrayRef(), static_cast<int32_t>(arg.getArgNumber()));
}

// PHASE 59 SLICE 2 STEP 2: DOES THIS VALUE NAME A SHARED BINDING THE LIFT MADE
// A FRAME-LOCAL VARIABLE? Two spellings of one storage location - the box in
// the frame that owns it, and the pointer parameter every lifted call handed
// the callee - and they must answer the same type, because they are the same
// `double`.
bool TypeInference::namesACarriedCell(mlir::Value v) {
    if (auto made = v.getDefiningOp<ctjs::CreateCellOp>()) {
        return made->hasAttr("ctnative.carried");
    }
    auto arg = llvm::dyn_cast<mlir::BlockArgument>(v);
    if (!arg || !arg.getOwner()->isEntryBlock()) { return false; }
    auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
    if (!fn) { return false; }
    auto listed = fn->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.cell_args");
    return listed &&
           llvm::is_contained(listed.asArrayRef(), static_cast<int32_t>(arg.getArgNumber()));
}

// AND WHICH OF THEM NAME ONE BOX. A union-find over the carried cells and the
// capture parameters, joined at every `ctjs.call_direct` operand the lift
// listed in `ctnative.cell_args` - the same walk groupReceivers makes over
// receivers, and it has to be a fixpoint for the same reason: a pointer passed
// on to a closure two levels in reaches the third function through the second.
llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> TypeInference::groupCells(
    mlir::Operation * top) {
    llvm::EquivalenceClasses<mlir::Value> classes;
    top->walk([&](ctjs::CreateCellOp cell) {
        if (cell->hasAttr("ctnative.carried")) { classes.insert(cell.getResult()); }
    });
    top->walk([&](ctjs::CallDirectOp call) {
        auto listed = call->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.cell_args");
        if (!listed) { return; }
        auto fn = call.getTarget();
        if (!fn || fn.getBody().empty()) { return; }
        mlir::Block & entry = fn.getBody().front();
        for (int32_t index : listed.asArrayRef()) {
            const auto at = static_cast<unsigned>(index);
            if (at >= call->getNumOperands() || at >= entry.getNumArguments()) { continue; }
            classes.unionSets(call->getOperand(at), entry.getArgument(at));
        }
    });
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> out;
    for (const auto * leader : classes) {
        if (!leader->isLeader()) { continue; }
        const auto members = llvm::to_vector<4>(classes.members(*leader));
        for (mlir::Value member : members) { out[member] = members; }
    }
    return out;
}

namespace {
// A USE THAT PASSES THE OBJECT AS A LIFTED METHOD'S `this`, which does NOT open
// its shape: the lift proved that method reaches `this` only through constant
// keys, and it marked the CALL as well as the callee so that this test costs
// one attribute lookup instead of a symbol lookup.
bool passesAReceiver(mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    if (!call) { return false; }
    if (use.getOperandNumber() == 0 && call->hasAttr("ctnative.receiver")) { return true; }
    // AND AN ARGUMENT THE LIFT PROVED IS A PARAMETER. ctjs.call_direct's
    // operands ARE the callee's entry block in order, so the operand number IS
    // the index the attribute lists - no adjustment, and no symbol lookup.
    auto listed = call->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.object_args");
    return listed &&
           llvm::is_contained(listed.asArrayRef(), static_cast<int32_t>(use.getOperandNumber()));
}
} // namespace

bool TypeInference::hasClosedShape(mlir::Value object) {
    if (!object.getDefiningOp<ctjs::CreateObjectOp>() && !namesAnObjectParameter(object)) {
        return false;
    }
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        // Assignment to __proto__ invokes an inherited accessor, so even a
        // dominating constant-key store does not establish an own data field.
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0 || constantKey(get.getKey()).empty() ||
                constantKey(get.getKey()) == "__proto__") {
                return false;
            }
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            // The object as the TARGET only: stored as a value into another
            // object it would escape, and that is not this rule's business.
            if (use.getOperandNumber() != 0 || constantKey(set.getKey()).empty() ||
                constantKey(set.getKey()) == "__proto__") {
                return false;
            }
        } else if (!passesAReceiver(use)) {
            return false;
        }
    }
    return true;
}

llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>> TypeInference::groupReceivers(
    mlir::Operation * top) {
    // A union-find over the two kinds of node there are: a closed object
    // literal, and the `%arg0` of a function the lift marked. A node absent
    // from `classes` is not in any group.
    llvm::EquivalenceClasses<mlir::Value> classes;

    top->walk([&](ctjs::CreateObjectOp object) {
        if (hasClosedShape(object.getResult())) { classes.insert(object.getResult()); }
    });
    top->walk([&](ctjs::FuncOp fn) {
        if (fn.getBody().empty()) { return; }
        mlir::Block & entry = fn.getBody().front();
        if (fn->hasAttr("ctnative.receiver")) {
            const mlir::Value self = entry.getArgument(0);
            classes.insert(self);
        }
        if (auto listed = fn->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.object_args")) {
            for (int32_t index : listed.asArrayRef()) {
                const mlir::Value parameter = entry.getArgument(static_cast<unsigned>(index));
                classes.insert(parameter);
            }
        }
    });
    top->walk([&](ctjs::CallDirectOp call) {
        auto listed = call->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.object_args");
        if (!call->hasAttr("ctnative.receiver") && !listed) { return; }
        auto fn = call.getTarget();
        if (!fn || fn.getBody().empty()) { return; }
        mlir::Block & entry = fn.getBody().front();
        if (call->hasAttr("ctnative.receiver")) {
            classes.unionSets(call.getReceiver(), entry.getArgument(0));
        }
        // ONE GROUP PER PARAMETER, NOT ONE PER FUNCTION. `f(a, b)` taking two
        // objects joins a to %arg3 and b to %arg4; joining them to each other
        // would give both the union of two shapes and a class with fields
        // neither literal has.
        if (listed) {
            for (int32_t index : listed.asArrayRef()) {
                classes.unionSets(call->getOperand(static_cast<unsigned>(index)),
                                  entry.getArgument(static_cast<unsigned>(index)));
            }
        }
    });

    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>> out;
    for (const auto * leader : classes) {
        if (!leader->isLeader()) { continue; }
        const auto members = llvm::to_vector<2>(classes.members(*leader));
        for (mlir::Value member : members) { out[member] = members; }
    }
    return out;
}

// Appends and reads preserve density. An indexed write additionally needs a
// complete current own-contents proof: the exact store must overwrite an own
// element on every path. Sparse writes, length changes, deletion and escape
// still refuse. This use census supplies the local lifetime proof separately;
// contents evidence alone proves neither native element types nor ownership.
// ponytail: direct local uses only; CFG aliases and SCF need transport proofs.
//
// WHAT IT DOES NOT PROVE: that nothing planted a numeric own property on
// `Array.prototype`, which an index past the end would find. That is the same
// boundary hasClosedShape draws with namesObjectPrototypeMember, and the same
// answer: `Array.prototype[7] = x` is not a thing this tier undertakes to
// survive, and it is recorded here rather than assumed away.
bool TypeInference::isDenseVectorSite(mlir::Value array) {
    const bool snapshot = isNativeMapSnapshot(array.getDefiningOp());
    if (!array.getDefiningOp<ctjs::CreateArrayOp>() && !snapshot) { return false; }
    std::optional<ArrayContentsEvidence> contents;
    for (mlir::OpOperand & use : array.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (user->hasAttr(kNativeMapSnapshotCopy) && use.getOperandNumber() == 2 &&
            isDenseVectorSite(user->getResult(0))) {
            continue;
        }
        if (llvm::isa<ctjs::AppendOp>(user)) {
            // OPERAND 0 IS THE ARRAY BEING BUILT; operand 1 is the element,
            // and an array appended INTO another array has escaped into it.
            if (use.getOperandNumber() != 0 || snapshot) { return false; }
            continue;
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0) { return false; }
            const llvm::StringRef key = constantKey(get.getKey());
            // An empty key is a key that is not a constant string - an index,
            // computed or literal, which is what `a[0]` imports as.
            if (key.empty() || key == "length") { continue; }
            return false;
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0 || snapshot || !constantKey(set.getKey()).empty()) {
                return false;
            }
            if (!contents) {
                auto function = user->getParentOfType<ctjs::FuncOp>();
                if (!function) { return false; }
                contents = computeArrayContents(function);
            }
            if (!contents->complete ||
                !llvm::any_of(contents->writes, [&](const ArrayElementWrite & write) {
                    return write.by == user && write.position == 2 &&
                           write.array == array.getDefiningOp();
                })) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

} // namespace ctcompile::ctnative
