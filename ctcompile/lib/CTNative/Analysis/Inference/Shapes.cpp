//===- Shapes.cpp - Closed shapes, receivers and cells --------------------===//
//
// TypeInference's structural predicates: which objects have a fixed set of
// fields, which values are lifted-method receivers or object parameters, which
// captured variables became frame-local cells, and which arrays can be stored
// as dense native vectors.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTNative/Analysis/EscapeAnalysis.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

namespace ctcompile::ctnative {
using ctjs::constantKey;

namespace {
/// True when \p v is an entry argument of a ctjs.func whose `attr` lists its
/// index.
bool listedArgument(mlir::Value v, llvm::StringRef attr) {
    auto arg = llvm::dyn_cast<mlir::BlockArgument>(v);
    if (!arg || !arg.getOwner()->isEntryBlock()) { return false; }
    auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
    auto listed = fn ? fn->getAttrOfType<mlir::DenseI32ArrayAttr>(attr) : mlir::DenseI32ArrayAttr{};
    return listed &&
           llvm::is_contained(listed.asArrayRef(), static_cast<int32_t>(arg.getArgNumber()));
}

/// Maps every member of `classes` to its whole class.
template <unsigned N>
llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, N>> flatten(
    const llvm::EquivalenceClasses<mlir::Value> & classes) {
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, N>> out;
    for (const auto * leader : classes) {
        if (!leader->isLeader()) { continue; }
        const auto members = llvm::to_vector<N>(classes.members(*leader));
        for (mlir::Value member : members) { out[member] = members; }
    }
    return out;
}
} // namespace

/// True when \p v is `%arg0` of a ctjs.func marked `ctnative.receiver`, i.e.
/// the `this` of a lifted method. Closure lifting (part of
/// --ctnative-lower-to-emitc) writes the attribute before this analysis runs,
/// so the check is one attribute lookup instead of a module-wide walk over
/// call sites; it runs once per property access per solver visit.
bool TypeInference::isReceiverArgument(mlir::Value v) {
    auto arg = llvm::dyn_cast<mlir::BlockArgument>(v);
    if (!arg || arg.getArgNumber() != 0 || !arg.getOwner()->isEntryBlock()) { return false; }
    auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
    return fn && fn->hasAttr("ctnative.receiver");
}

/// True when \p v is a lifted method's receiver or an entry argument listed in
/// `ctnative.object_args`. The attribute holds a list of indices because one
/// function can take several object parameters, e.g. `join(a, b)`, and the
/// operand number is what both the call site and the emitted parameter list
/// count in.
bool TypeInference::namesAnObjectParameter(mlir::Value v) {
    return isReceiverArgument(v) || listedArgument(v, "ctnative.object_args");
}

/// True when \p v names a captured variable that closure lifting turned into a
/// frame-local cell: either the `ctjs.create_cell` marked `ctnative.carried` in
/// the owning frame, or a callee parameter listed in `ctnative.cell_args`. Both
/// are views of the same storage location, so both must infer the same type.
bool TypeInference::namesACarriedCell(mlir::Value v) {
    if (auto made = v.getDefiningOp<ctjs::CreateCellOp>()) {
        return made->hasAttr("ctnative.carried");
    }
    return listedArgument(v, "ctnative.cell_args");
}

/// Groups carried cells with the callee parameters they are passed to through
/// `ctjs.call_direct` operands listed in `ctnative.cell_args`. A union-find is
/// used (as in groupReceivers) because a cell passed through two levels of
/// closures reaches the third function by way of the second.
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
    return flatten<4>(classes);
}

namespace {
/// True when \p use passes the object as a lifted method's `this` or as a listed
/// object argument. Such a use keeps the shape closed: closure lifting proved
/// the callee reaches the object only through constant keys, and it marked the
/// call as well as the callee so this test is one attribute lookup.
bool passesAReceiver(mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    if (!call) { return false; }
    if (use.getOperandNumber() == 0 && call->hasAttr("ctnative.receiver")) { return true; }
    // An object argument: ctjs.call_direct's operands are the callee's entry
    // block arguments in order, so the operand number is the index the
    // attribute lists, with no adjustment and no symbol lookup.
    auto listed = call->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.object_args");
    return listed &&
           llvm::is_contained(listed.asArrayRef(), static_cast<int32_t>(use.getOperandNumber()));
}
} // namespace

/// True when \p object (an object literal, an object parameter or a native Map
/// record) is only ever used as the target of constant, non-`__proto__` keys,
/// as a lifted method's receiver or object argument, or as a native Map record
/// store, so its set of fields is fixed.
bool TypeInference::hasClosedShape(mlir::Value object) {
    if (!object.getDefiningOp<ctjs::CreateObjectOp>() && !namesAnObjectParameter(object) &&
        !nativeMapRecordOrigin(object)) {
        return false;
    }
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        // Assignment to __proto__ invokes an inherited accessor, so even a
        // dominating constant-key store does not establish an own data field.
        // Only as the target (operand 0): stored as a value into another object
        // it would escape, which this predicate refuses.
        if (const mlir::Value key = ctjs::propertyKey(user)) {
            if (use.getOperandNumber() != 0 || constantKey(key).empty() ||
                constantKey(key) == "__proto__") {
                return false;
            }
        } else if (!passesAReceiver(use) && !isNativeMapRecordStore(use)) {
            return false;
        }
    }
    return true;
}

/// Groups closed object literals, native Map record payloads, and the receiver
/// and object parameters they are passed to, so a field store through any member
/// is visible through every other.
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
        // One group per parameter, not one per function. `f(a, b)` taking two
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

    top->walk([&](ctjs::CallOp call) {
        const auto payload = nativeMapRecordPayload(call.getReceiver());
        if (!payload) { return; }
        if (nativeMapAction(call) == "set") {
            classes.unionSets(payload, call.getArgs()[1]);
        } else if (nativeMapAction(call) == "get") {
            classes.unionSets(payload, call.getResult());
        }
    });

    return flatten<2>(classes);
}

/// True when \p array can be stored as a dense native vector.
///
/// Appends and reads preserve density. An indexed write additionally needs a
/// complete computeArrayContents proof that the store overwrites an existing
/// own element on every path. Literal length assignments may only shrink.
/// Sparse writes, growth, deletion and escape refuse. The use scan in
/// denseVectorAliases supplies the local lifetime; contents evidence alone
/// proves neither native element types nor ownership. An array that flows
/// through scf.if or scf.while values must be allocated in the entry block.
/// ponytail: no CFG block arguments or region-local allocations; those need
/// lifetimes.
/// ponytail: repeated whole-function queries can be quadratic; cache within one
/// analysis run if profiling makes that necessary.
///
/// Not proved: that no script added a numeric property to `Array.prototype`,
/// which a read past the end would find. The native compiler does not support
/// that pattern; admission's namesObjectPrototypeMember draws the same line for
/// object keys.
bool TypeInference::isDenseVectorSite(mlir::Value array) {
    return !denseVectorAliases(array).empty();
}

/// Returns every value that aliases \p array through scf.if/scf.while values if
/// all of them can be stored as a dense native vector, or an empty list if any
/// use breaks density or escapes the function. The walk is capped at 65536
/// steps (values plus uses); a larger group is refused.
llvm::SmallVector<mlir::Value, 4> TypeInference::denseVectorAliases(mlir::Value array) {
    llvm::SmallVector<mlir::Value, 4> members{array};
    llvm::DenseSet<mlir::Value> seen{array};
    llvm::SmallVector<ctjs::SetPropertyOp> stores;
    auto function = array.getParentRegion()->getParentOfType<ctjs::FuncOp>();
    bool borrowed = false;
    std::size_t work = 0;
    const auto add = [&](mlir::Value value) {
        if (seen.insert(value).second) { members.push_back(value); }
    };
    const auto before = [&](mlir::scf::WhileOp loop, unsigned index) {
        borrowed = true;
        add(loop.getInits()[index]);
        add(loop.getBeforeArguments()[index]);
        add(loop.getYieldOp().getOperand(index));
    };
    const auto after = [&](mlir::scf::WhileOp loop, unsigned index) {
        borrowed = true;
        add(loop.getConditionOp().getArgs()[index]);
        add(loop.getAfterArguments()[index]);
        add(loop.getResult(index));
    };
    for (std::size_t at = 0; at < members.size(); ++at) {
        // Any failure returns an empty list, never a partial group.
        if (++work > 65536) { return {}; }
        mlir::Value member = members[at];
        auto * definition = member.getDefiningOp();
        if (member.getParentRegion()->getParentOfType<ctjs::FuncOp>() != function) { return {}; }
        const bool snapshot = definition && isNativeMapSnapshot(definition);
        if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(member)) {
            auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(argument.getOwner()->getParentOp());
            if (!loop) { return {}; }
            if (&loop.getBefore() == argument.getOwner()->getParent()) {
                before(loop, argument.getArgNumber());
            } else {
                after(loop, argument.getArgNumber());
            }
        } else if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(definition)) {
            after(loop, llvm::cast<mlir::OpResult>(member).getResultNumber());
        } else if (auto selected = llvm::dyn_cast<mlir::scf::IfOp>(definition)) {
            borrowed = true;
            const unsigned index = llvm::cast<mlir::OpResult>(member).getResultNumber();
            add(selected.thenYield().getOperand(index));
            add(selected.elseYield().getOperand(index));
        } else if (!llvm::isa<ctjs::CreateArrayOp>(definition) && !snapshot) {
            return {};
        }
        for (mlir::OpOperand & use : member.getUses()) {
            if (++work > 65536) { return {}; }
            mlir::Operation * user = use.getOwner();
            if (user->getParentOfType<ctjs::FuncOp>() != function) { return {}; }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(user)) {
                before(loop, use.getOperandNumber());
                continue;
            }
            if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(user)) {
                auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(condition->getParentOp());
                if (!loop || use.getOperandNumber() == 0) { return {}; }
                after(loop, use.getOperandNumber() - 1);
                continue;
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(user)) {
                if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(yield->getParentOp())) {
                    before(loop, use.getOperandNumber());
                    continue;
                }
                auto selected = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
                if (!selected || use.getOperandNumber() >= selected.getNumResults()) { return {}; }
                add(selected.getResult(use.getOperandNumber()));
                continue;
            }
            if (user->hasAttr(kNativeMapSnapshotCopy) && use.getOperandNumber() == 2 &&
                isDenseVectorSite(user->getResult(0))) {
                continue;
            }
            if (llvm::isa<ctjs::AppendOp>(user)) {
                // Operand 0 is the array being built; operand 1 is the element,
                // and an array appended into another array has escaped into it.
                if (use.getOperandNumber() != 0 || snapshot ||
                    !member.getDefiningOp<ctjs::CreateArrayOp>()) {
                    return {};
                }
                continue;
            }
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (use.getOperandNumber() != 0) { return {}; }
                const llvm::StringRef key = constantKey(get.getKey());
                // An empty key is a key that is not a constant string - an index,
                // computed or literal, which is what `a[0]` imports as.
                if (key.empty() || key == "length") { continue; }
                return {};
            }
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                const llvm::StringRef key = constantKey(set.getKey());
                if (use.getOperandNumber() != 0 || snapshot || (!key.empty() && key != "length")) {
                    return {};
                }
                stores.push_back(set);
                continue;
            }
            return {};
        }
    }
    if (borrowed) {
        if (!function) { return {}; }
        for (mlir::Value member : members) {
            auto * definition = member.getDefiningOp();
            if (!definition || llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(definition)) {
                continue;
            }
            if (!llvm::isa<ctjs::CreateArrayOp>(definition) ||
                definition->getBlock() != &function.getBody().front()) {
                return {};
            }
        }
    }
    if (borrowed || !stores.empty()) {
        if (!function) { return {}; }
        const auto contents = computeArrayContents(function);
        if (!contents.complete) { return {}; }
        for (auto store : stores) {
            // computeArrayContents checked every literal length store against
            // the current contents; a shrink records no element write.
            if (constantKey(store.getKey()) == "length") { continue; }
            if (!llvm::any_of(contents.writes, [&](const ArrayElementWrite & write) {
                    return write.by == store && write.position == 2 &&
                           seen.contains(write.array->getResult(0));
                })) {
                return {};
            }
        }
    }
    return members;
}

} // namespace ctcompile::ctnative
