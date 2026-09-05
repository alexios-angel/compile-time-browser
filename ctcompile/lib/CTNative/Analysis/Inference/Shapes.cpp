// Closed shapes, receivers, cells and dense vector use proofs.
#include "PropertyKey.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

namespace ctcompile::ctnative {
using inference_detail::constantKey;

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
    llvm::DenseMap<mlir::Value, mlir::Value> parent;
    const auto find = [&parent](mlir::Value v) {
        mlir::Value root = v;
        for (auto next = parent.find(root); next != parent.end() && next->second != root;
             next = parent.find(root)) {
            root = next->second;
        }
        for (mlir::Value at = v; at != root;) {
            const mlir::Value next = parent.lookup(at);
            parent[at] = root;
            at = next;
        }
        return root;
    };
    const auto join = [&](mlir::Value a, mlir::Value b) {
        parent.try_emplace(a, a);
        parent.try_emplace(b, b);
        const mlir::Value ra = find(a);
        const mlir::Value rb = find(b);
        if (ra != rb) { parent[rb] = ra; }
    };
    top->walk([&](ctjs::CreateCellOp cell) {
        if (cell->hasAttr("ctnative.carried")) {
            parent.try_emplace(cell.getResult(), cell.getResult());
        }
    });
    top->walk([&](ctjs::CallDirectOp call) {
        auto listed = call->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.cell_args");
        if (!listed) { return; }
        auto fn =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        if (!fn || fn.getBody().empty()) { return; }
        mlir::Block & entry = fn.getBody().front();
        for (int32_t index : listed.asArrayRef()) {
            const auto at = static_cast<unsigned>(index);
            if (at >= call->getNumOperands() || at >= entry.getNumArguments()) { continue; }
            join(call->getOperand(at), entry.getArgument(at));
        }
    });
    llvm::SmallVector<mlir::Value> nodes;
    for (const auto & entry : parent) { nodes.push_back(entry.first); }
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> byRoot;
    for (mlir::Value node : nodes) { byRoot[find(node)].push_back(node); }
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> out;
    for (const auto & [root, members] : byRoot) {
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
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0 || constantKey(get.getKey()).empty()) { return false; }
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            // The object as the TARGET only: stored as a value into another
            // object it would escape, and that is not this rule's business.
            if (use.getOperandNumber() != 0 || constantKey(set.getKey()).empty()) { return false; }
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
    // from `parent` is not in any group.
    llvm::DenseMap<mlir::Value, mlir::Value> parent;
    const auto find = [&parent](mlir::Value v) {
        mlir::Value root = v;
        for (auto next = parent.find(root); next != parent.end() && next->second != root;
             next = parent.find(root)) {
            root = next->second;
        }
        // Path compression: `a.m()` calling `this.n()` calling `this.o()` is a
        // chain, and without this the walk is quadratic in its depth.
        for (mlir::Value at = v; at != root;) {
            const mlir::Value next = parent.lookup(at);
            parent[at] = root;
            at = next;
        }
        return root;
    };
    const auto join = [&](mlir::Value a, mlir::Value b) {
        parent.try_emplace(a, a);
        parent.try_emplace(b, b);
        const mlir::Value ra = find(a);
        const mlir::Value rb = find(b);
        if (ra != rb) { parent[rb] = ra; }
    };

    top->walk([&](ctjs::CreateObjectOp object) {
        if (hasClosedShape(object.getResult())) {
            parent.try_emplace(object.getResult(), object.getResult());
        }
    });
    top->walk([&](ctjs::FuncOp fn) {
        if (fn.getBody().empty()) { return; }
        mlir::Block & entry = fn.getBody().front();
        if (fn->hasAttr("ctnative.receiver")) {
            const mlir::Value self = entry.getArgument(0);
            parent.try_emplace(self, self);
        }
        if (auto listed = fn->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.object_args")) {
            for (int32_t index : listed.asArrayRef()) {
                const mlir::Value parameter = entry.getArgument(static_cast<unsigned>(index));
                parent.try_emplace(parameter, parameter);
            }
        }
    });
    top->walk([&](ctjs::CallDirectOp call) {
        auto listed = call->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.object_args");
        if (!call->hasAttr("ctnative.receiver") && !listed) { return; }
        auto fn =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        if (!fn || fn.getBody().empty()) { return; }
        mlir::Block & entry = fn.getBody().front();
        if (call->hasAttr("ctnative.receiver")) { join(call.getReceiver(), entry.getArgument(0)); }
        // ONE GROUP PER PARAMETER, NOT ONE PER FUNCTION. `f(a, b)` taking two
        // objects joins a to %arg3 and b to %arg4; joining them to each other
        // would give both the union of two shapes and a class with fields
        // neither literal has.
        if (listed) {
            for (int32_t index : listed.asArrayRef()) {
                join(call->getOperand(static_cast<unsigned>(index)),
                     entry.getArgument(static_cast<unsigned>(index)));
            }
        }
    });

    // THE KEYS FIRST, because `find` compresses paths and so writes to
    // `parent`: resolving while iterating it would be a mutation under an
    // iterator, and the fact that DenseMap survives an assignment to a key it
    // already holds is not a thing to rely on.
    llvm::SmallVector<mlir::Value> nodes;
    for (const auto & entry : parent) { nodes.push_back(entry.first); }
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>> byRoot;
    for (mlir::Value node : nodes) { byRoot[find(node)].push_back(node); }
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>> out;
    for (const auto & [root, members] : byRoot) {
        for (mlir::Value member : members) { out[member] = members; }
    }
    return out;
}

// A DENSE ARRAY IS AN ARRAY NOTHING CAN MAKE SPARSE, and the default arm below
// is the whole proof. Three uses keep a `std::vector` a `std::vector`:
//
//   * `ctjs.append` onto it - the elements of the literal, in order, which is
//     how the bytecode builds `[1, 2, 3]` (CTJS_AppendOp's own description);
//   * a read of `length`, which is `size()` exactly BECAUSE nothing else in
//     this list can leave a hole;
//   * a read through any other key, which is an index.
//
// EVERYTHING ELSE OPENS IT, and two of those are why part 24 Stage 57A says
// "prove density, or box" rather than "prove uniformity":
//
//   * `a[i] = v`. `a[100] = 1` on a two-element array gives `length` 101 and
//     three elements, so `.length` stops being `size()` and the C++ has no
//     representation for the ninety-eight holes. Refused, which is a
//     DEVIATION FROM THIS STAGE'S WRITTEN DESIGN (it listed an index store as
//     a vector use); admitting it would also have made the element join below
//     unsound, because a stored value it does not see is a value a later read
//     returns.
//   * `delete a[0]`, which punches a hole in an array that had none.
//
// A return, a call, a store into another object, a loop-carried phi: every one
// of them is some other use and lands in the default arm too.
//
// WHAT IT DOES NOT PROVE: that nothing planted a numeric own property on
// `Array.prototype`, which an index past the end would find. That is the same
// boundary hasClosedShape draws with namesObjectPrototypeMember, and the same
// answer: `Array.prototype[7] = x` is not a thing this tier undertakes to
// survive, and it is recorded here rather than assumed away.
bool TypeInference::isDenseVectorSite(mlir::Value array) {
    const llvm::StringRef action = nativeMapAction(array.getDefiningOp());
    const bool snapshot = action == "keys" || action == "values";
    if (!array.getDefiningOp<ctjs::CreateArrayOp>() && !snapshot) { return false; }
    for (mlir::OpOperand & use : array.getUses()) {
        mlir::Operation * user = use.getOwner();
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
        return false;
    }
    return true;
}

} // namespace ctcompile::ctnative
