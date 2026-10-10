//===- ClosureLifting/Cells.cpp - Captured cell analysis and unboxing -----===//
//
// Decides how each captured `ctjs.create_cell` is lowered: unboxed to a
// constant (rule 1 in ClosureLifter.h), carried as a frame-local variable
// passed by pointer (rules 2 and 3), or left as a box with a refusal reason.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

/// Finds cells assigned exactly once where that assignment comes before every
/// read (rule 1; the four conditions are listed beside `writtenOnce` in
/// ClosureLifter.h). Runs once, before any rewrite, so every iteration of the
/// lift's fixpoint judges a closure against the same verdict.
void closureLifter::singleWriteCensus() {
    module.walk([&](ctjs::CreateCellOp cell) {
        ctjs::CellSetOp store;
        // Every use that must come after the store for its value to be the
        // one this cell yields (conditions 2 and 3), collected in one walk
        // because condition 1 is only known when the walk ends.
        llvm::SmallVector<mlir::Operation *> mustFollow;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            switch (classify(use)) {
            case cellUse::write:
                // Condition 1: exactly one write. Two writes make the binding
                // shared mutable state again, and which one a capture sees
                // depends on the path taken to it.
                if (store) {
                    whyNotWrittenOnce[cell.getOperation()] = "it is assigned more than once";
                    return;
                }
                store = llvm::cast<ctjs::CellSetOp>(user);
                continue;
            case cellUse::read: mustFollow.push_back(user); continue;
            case cellUse::capture: {
                // Condition 4: the capture's target writes no upvalue, the
                // same check isConstantCell applies to these uses.
                //
                // The capture itself need not follow the store. The
                // create_closure is hoisted for a function declaration and the
                // closure reads the box when it runs, so it is the call that
                // has to follow: condition 3, checked by whyCapturesDoNotReach
                // where the call sites are known.
                ctjs::FuncOp target = targetOf(llvm::cast<ctjs::CreateClosureOp>(user));
                if (!target || mutatesUpvalue.contains(target.getOperation())) { return; }
                continue;
            }
            // `ctjs.cell_set %other, %cell` stores this cell into another
            // one and is not a write of it. This rule cannot carry that use,
            // so it fails outright, as does every other use.
            default: return;
            }
        }
        // No store at all: the cell always holds its initial value, which
        // isConstantCell handles without a census entry.
        if (!store) { return; }
        for (mlir::Operation * later : mustFollow) {
            if (dominance.properlyDominates(store.getOperation(), later)) { continue; }
            // Condition 2. A read the store does not dominate yields the
            // `undefined` the hoist boxed, and a lift that ignored this would
            // silently replace it with the stored value.
            whyNotWrittenOnce[cell.getOperation()] =
                "its one assignment does not dominate every read of it, and a read before "
                "it yields the undefined the binding was hoisted with";
            return;
        }
        writtenOnce[cell.getOperation()] = store;
    });
}

/// Finds cells that can become a frame-local variable that lifted closures
/// reach by pointer (rule 2; the argument is beside `carriedCells` in
/// ClosureLifter.h). Runs once, before any rewrite: lift() adds a use of the
/// cell (its address at each rewritten call site) that a later walk must not
/// see, and every fixpoint iteration must judge against the same verdict.
///
/// The only structural requirement is that every use of the box is a read, a
/// write or a capture. A cell stored into an object, put in another cell,
/// returned or passed is a box something else holds a reference to, and a
/// stack variable cannot stand in for one.
///
/// Who captures the cell is not checked here. A capturing closure that does
/// not lift is refused by name, and `whyCarriedCellStaysABox`, run after the
/// fixpoint has produced the verdicts, turns that into the cell's own
/// diagnostic.
void closureLifter::sharedCellCensus() {
    module.walk([&](ctjs::CreateCellOp cell) {
        // The by-value path has first choice, where it actually works. A cell
        // it takes is copied into a parameter: no pointer, no indirection and
        // no aliasing question. But taking it needs two checks:
        // isConstantCell, and then whyCapturesDoNotReach at each call site
        // (do the value and the assignment reach it?). A cell failing the
        // second would be refused outright rather than carried, so both are
        // checked here and one verdict per cell decides the path. A store on
        // only one path, or a store in a loop whose call follows it, then
        // becomes a pointer instead of a refusal.
        if (isConstantCell(cell) && !byValueMissesACall(cell)) { return; }
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            // `ctjs.cell_set %other, %cell` puts this box inside another one,
            // where a pointer to this frame would outlive the frame, so only
            // reads, writes and captures are allowed.
            const cellUse kind = classify(use);
            if (kind == cellUse::read || kind == cellUse::write || kind == cellUse::capture) {
                continue;
            }
            whyNotCarried[cell.getOperation()] =
                ("it reaches `" + user->getName().getStringRef() +
                 "`, so something other than this frame holds the box and a local variable "
                 "cannot stand in for it")
                    .str();
            return;
        }
        carriedCells.insert(cell.getOperation());
        // Also record whether some write precedes every read, so the hoisted
        // `undefined` can never be observed (rule 3; see
        // `assignedBeforeRead`). Checked before any rewrite because it
        // inspects the calls of the capturing closures, and lift() erases
        // them.
        if (dominatingWriteOf(cell)) { assignedBeforeRead.insert(cell.getOperation()); }
    });
}

/// The write that makes the box's initial unobservable, or null. A cell may
/// have several `ctjs.cell_set`s and it is enough that one of them comes
/// before every read: from there on the box holds a stored value, and which
/// of the stores it holds only widens the join.
ctjs::CellSetOp closureLifter::dominatingWriteOf(ctjs::CreateCellOp cell) {
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
        if (!write || use.getOperandNumber() != 0) { continue; }
        if (writeReachesEveryRead(cell, write)) { return write; }
    }
    return {};
}

/// Does this store come before every read of the binding? Two halves,
/// conditions 2 and 3 of rule 1 (`writtenOnce`), checked against the same use
/// list and `DominanceInfo`:
///
///   2. it properly dominates every `ctjs.cell_get` of the box: the reads in
///      the frame that owns it, which is every read of the SSA value because
///      a `ctjs.func` body is the region the cell is defined in;
///   3. it properly dominates every call of every closure that captured it.
///      The call, not the `ctjs.create_closure`: a function declaration is
///      hoisted, so its closure is built in the prologue before any store,
///      and the interpreter reads the box when the closure runs
///      (VM_CASE(get_upvalue) in ctbrowser/lib/Script/vm/run_loop.cpp). A
///      read through the pointer, at any nesting depth, happens inside one of
///      those calls: the capture cannot escape (whyNotLiftable condition 4)
///      and neither can the pointer (whyUpvalueReadsDoNotLift), so there is
///      no other way to reach the storage.
///
/// A use in another `ctjs.func` fails outright rather than being checked for
/// dominance, for the reason `byValueMissesACall` gives: builtin.module's body
/// is a graph region, in which `properlyDominates` answers yes for every pair
/// of operations, so a bare dominance question about a call in another frame
/// is meaningless. (This is the case of a capture called from a method
/// defined in another function.)
bool closureLifter::writeReachesEveryRead(ctjs::CreateCellOp cell, ctjs::CellSetOp store) {
    auto owner = cell->getParentOfType<ctjs::FuncOp>();
    if (!owner || store->getParentOfType<ctjs::FuncOp>() != owner) { return false; }
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        const cellUse kind = classify(use);
        if (kind == cellUse::read) {
            // Condition 2.
            if (!dominance.properlyDominates(store.getOperation(), user)) { return false; }
            continue;
        }
        // Another write only widens the join. It cannot restore the initial,
        // so it is not required to follow anything.
        if (kind == cellUse::write) { continue; }
        if (kind == cellUse::capture) {
            // Condition 3, over every use of the closure value, not only the
            // calls. This census runs before `whyNotLiftable` has decided
            // which of those uses are calls, and a use that is not a call
            // refuses the lift anyway, so requiring the store to dominate all
            // of them gives the same result without waiting for that verdict.
            for (mlir::Operation * at : user->getResult(0).getUsers()) {
                // Skip the store itself. In `var f = function () { f(); };`
                // the closure is stored into the very cell it captures, so the
                // store is one of its users. Requiring the store to dominate
                // itself would refuse every recursive local function; the
                // calls of `f` are reads of the cell and are checked above.
                if (at == store.getOperation()) { continue; }
                if (!reachesInFrame(store.getOperation(), at)) { return false; }
            }
            continue;
        }
        // Every other use is one sharedCellCensus has already refused, so this
        // arm is unreachable from that caller. It still answers false rather
        // than trusting that, because the rule must account for every way to
        // reach the storage.
        return false;
    }
    return true;
}

/// Is this cell's hoisted initial unreadable? Keyed on the operation, like
/// `isCarried`, because the census and the attribute stamp are in different
/// passes over the module.
bool closureLifter::isAssignedBeforeRead(ctjs::CreateCellOp cell) const {
    return assignedBeforeRead.contains(cell.getOperation());
}

/// Would the by-value path miss a call? This is the question
/// whyCapturesDoNotReach asks per call site, asked here per cell, because the
/// choice between copying a binding and pointing at it has to be made once:
/// capturedValue, the call-site rewrite and the parameter's type all read it,
/// and if two disagreed a pointer would be passed where a double is expected.
///
/// Only calls in the cell's own function are checked. A call from another
/// ctjs.func is refused anyway (lifting has nothing to prepend there) and
/// carrying cannot help, since the variable is not in that frame either.
/// Comparing the functions first also keeps `properlyDominates` meaningful,
/// since builtin.module's body is a graph region in which every operation
/// dominates every other.
bool closureLifter::byValueMissesACall(ctjs::CreateCellOp cell) {
    auto owner = cell->getParentOfType<ctjs::FuncOp>();
    const mlir::Value value = constantValueOf(cell);
    ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation());
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (classify(use) != cellUse::capture) { continue; }
        for (mlir::Operation * at : use.getOwner()->getResult(0).getUsers()) {
            if (at->getParentOfType<ctjs::FuncOp>() != owner) { continue; }
            if (!dominance.properlyDominates(value, at)) { return true; }
            if (write && !dominance.properlyDominates(write.getOperation(), at)) { return true; }
        }
    }
    return false;
}

/// Is this cell carried by pointer? A function because the census's set is
/// keyed on the operation and several callers ask.
bool closureLifter::isCarried(ctjs::CreateCellOp cell) const {
    return carriedCells.contains(cell.getOperation());
}

/// Is capture slot i of this closure carried by pointer? Two shapes, the two
/// `capturedValue` handles: the operand is a carried cell of this frame, or
/// the slot is filled from the enclosing closure's upvalue k and that
/// function's own capture parameter 3 + k is already a pointer (the enclosing
/// closure itself received the cell by pointer). The second shape is what
/// lets a shared binding reach a closure two levels in, and it is read off
/// the attribute the enclosing lift wrote rather than re-derived.
///
/// This is deliberately a property of the slot, not of the target. A target
/// that only reads a capture still takes a pointer when the operand is a
/// pointer; the alternative would be one ctjs.func with two signatures.
bool closureLifter::slotIsCarried(ctjs::CreateClosureOp c, unsigned i) {
    if (auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>()) {
        return isCarried(cell);
    }
    const std::int32_t k = enclosingIndex(c, i);
    if (k < 0) { return false; }
    auto enclosing = c->getParentOfType<ctjs::FuncOp>();
    if (!enclosing) { return false; }
    return listsSlot(enclosing, kNativeCellArgs, captureArgument(static_cast<unsigned>(k)));
}

/// The value every read of a constant cell yields: the cell's initial when
/// nothing writes it, or the stored operand when one write dominates every
/// read and capture. capturedValue(), unboxCells() and isConstantCell() all
/// use this, so they cannot disagree about the value.
mlir::Value closureLifter::constantValueOf(ctjs::CreateCellOp cell) {
    if (ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation())) {
        return write.getValue();
    }
    return cell.getInitial();
}

/// A cell whose value is the same at every read. Every use must be a read, a
/// capture into a function that writes no upvalue, or the one `ctjs.cell_set`
/// that singleWriteCensus proved dominates all of them; anything else fails.
bool closureLifter::isConstantCell(ctjs::CreateCellOp cell) {
    ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation());
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        const cellUse kind = classify(use);
        // The one dominating write. `writtenOnce` holds it only when the census
        // proved conditions 1 to 4 of rule 1, so this admits a store already
        // shown to come before every read and every capture.
        if (kind == cellUse::read || (write && user == write.getOperation())) { continue; }
        if (kind != cellUse::capture) { return false; }
        ctjs::FuncOp target = targetOf(llvm::cast<ctjs::CreateClosureOp>(user));
        if (!target || mutatesUpvalue.contains(target.getOperation())) { return false; }
    }
    return true;
}

/// True when `call` lists `use` in its `ctnative.cell_args`. Admission checks
/// the same attribute; this one-line lookup is duplicated rather than shared.
bool closureLifter::namesACellArgument(mlir::Operation * call, mlir::OpOperand & use) {
    return listsSlot(call, kNativeCellArgs, use.getOperandNumber());
}

/// Why a carried cell cannot be a frame-local variable after all, or nullopt.
/// Asked once, after the lift's fixpoint has settled every verdict.
///
/// The use list is the census's plus one: lift() added the cell as an
/// argument of every ctjs.call_direct it wrote, at an index
/// `ctnative.cell_args` lists (the address the callee reads through). That use
/// did not exist when sharedCellCensus ran, which is why the census cannot
/// simply be re-run here.
std::optional<std::string> closureLifter::whyCarriedCellStaysABox(ctjs::CreateCellOp cell) {
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        const cellUse kind = classify(use);
        if (kind == cellUse::read || kind == cellUse::write) { continue; }
        if (llvm::isa<ctjs::CallDirectOp>(user) && namesACellArgument(user, use)) { continue; }
        if (kind == cellUse::capture) {
            if (user->hasAttr(kNativeLifted)) { continue; }
            auto reason = user->getAttrOfType<mlir::StringAttr>(kNativeClosureReason);
            return "the closure that shares it is not lifted, so the binding needs a real "
                   "box and not a variable in this frame" +
                   (reason ? " - " + reason.getValue().str() : std::string{});
        }
        return ("it reaches `" + user->getName().getStringRef() +
                "` after the lift, which is not a use of a frame-local variable")
            .str();
    }
    return std::nullopt;
}

/// Erases unboxed cells that have no uses left.
void closureLifter::eraseUnusedUnboxedCells() {
    llvm::SmallVector<ctjs::CreateCellOp> dead;
    module.walk([&](ctjs::CreateCellOp cell) {
        if (cell->hasAttr(kNativeUnboxed) && cell.getResult().use_empty()) { dead.push_back(cell); }
    });
    for (ctjs::CreateCellOp cell : dead) { cell.erase(); }
}

/// Unboxes every cell whose closures are all lifted: such a cell holds a value
/// nobody can change, so each read is replaced by that value and the box is
/// not built. Runs after every lift, because a cell captured by one lifted and
/// one unlifted closure must stay a cell for the unlifted one (which is
/// refused, but whose IR this pass must not falsify). Carried cells are kept
/// as frame-local variables instead. With `localObjectsOnly`, only cells
/// holding a `ctjs.create_object` that are just read and written (never
/// captured) are unboxed.
void closureLifter::unboxCells(liftReport & out, bool localObjectsOnly) {
    llvm::SmallVector<ctjs::CreateCellOp> cells;
    module.walk([&](ctjs::CreateCellOp cell) { cells.push_back(cell); });
    for (ctjs::CreateCellOp cell : cells) {
        if (localObjectsOnly &&
            (!constantValueOf(cell).getDefiningOp<ctjs::CreateObjectOp>() ||
             !llvm::all_of(cell.getResult().getUsers(), [](mlir::Operation * user) {
                 return llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(user);
             }))) {
            continue;
        }
        // A carried cell is not unboxed: it has no single value to substitute
        // for its reads, so the box stays and becomes an `emitc.variable`;
        // reads load it, writes assign it, and every lifted call passes its
        // address.
        //
        // Only here can it be checked that every closure capturing it
        // actually lifted; the census ran before those verdicts existed. The
        // pointer argument depends on it, because an unlifted closure over
        // this binding is a real ctjs.create_closure that needs a real box,
        // and a stack variable is not one. The function is refused either way
        // (an unlifted closure has no lowering), but the message has to name
        // the binding rather than the operation.
        if (isCarried(cell)) {
            if (const std::optional<std::string> why = whyCarriedCellStaysABox(cell)) {
                cell->setAttr(kNativeCellReason, mlir::StringAttr::get(context, *why));
                continue;
            }
            cell->setAttr(kNativeCarried, mlir::UnitAttr::get(context));
            // Record whether its hoisted initial can still be read (rule 3).
            // sharedCellCensus decided this before the lift erased the calls
            // it inspects; it is written here because
            // TypeInference::cellTypeOf consumes it after this pass.
            if (isAssignedBeforeRead(cell)) {
                cell->setAttr(kAssignedBeforeRead, mlir::UnitAttr::get(context));
            }
            ++out.locals;
            continue;
        }
        // The one write this cell is allowed (rule 1), or null.
        // singleWriteCensus proved it dominates every read and capture, so
        // from it onward the box holds one value and is not needed.
        ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation());
        std::optional<std::string> why;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            const cellUse kind = classify(use);
            if (kind == cellUse::read || (write && user == write.getOperation())) { continue; }
            if (kind == cellUse::capture) {
                if (user->hasAttr(kNativeLifted)) { continue; }
                auto reason = user->getAttrOfType<mlir::StringAttr>(kNativeClosureReason);
                why = "the closure that captures it is not lifted" +
                      (reason ? " - " + reason.getValue().str() : std::string{});
                break;
            }
            if (kind == cellUse::write || kind == cellUse::storedInto) {
                // A write rule 1 did not accept; the census recorded which
                // condition failed, which is more useful than the generic
                // message below.
                const auto named = whyNotWrittenOnce.find(cell.getOperation());
                why = named != whyNotWrittenOnce.end()
                          ? named->second
                          : std::string{"it is assigned after it was boxed, so its value is "
                                        "not the one the cell was built with"};
                break;
            }
            why = ("it reaches `" + user->getName().getStringRef() + "`").str();
            break;
        }
        // Put the reason on the box itself, because the box is what admission
        // meets first: `op::new_cell` runs in the prologue, before the
        // `closure` opcode that captures it, so a walk in program order
        // reaches the cell first and would otherwise refuse the function with
        // "`ctjs.create_cell` is not native yet", which names neither the
        // binding nor what is wrong with it.
        if (why) {
            cell->setAttr(kNativeCellReason, mlir::StringAttr::get(context, *why));
            continue;
        }
        llvm::SmallVector<ctjs::CellGetOp> reads;
        for (mlir::Operation * user : cell.getResult().getUsers()) {
            if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(user)) { reads.push_back(read); }
        }
        // `constantValueOf` decides the value, and capturedValue() has already
        // handed the same value to every lifted call site of every closure
        // that captured this cell, so the two cannot disagree.
        const mlir::Value value = constantValueOf(cell);
        for (ctjs::CellGetOp read : reads) {
            read.getResult().replaceAllUsesWith(value);
            read.erase();
        }
        // The write goes with the box. Every read has been replaced by what
        // the write stored, so a ctjs.cell_set left behind would name a
        // create_cell nothing else uses and refuse the whole function.
        // Erased after the reads, because both use the cell.
        if (write) { write.erase(); }
        cell->setAttr(kNativeUnboxed, mlir::UnitAttr::get(context));
        if (localObjectsOnly && cell.getResult().use_empty()) { cell.erase(); }
        ++out.cells;
    }
}

} // namespace ctcompile::ctnative::lowering_detail
