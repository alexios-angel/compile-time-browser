// ClosureLifting/Cells.cpp - native lowering implementation.
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

// WHICH CELLS ARE CONSTANT AFTER ONE WRITE - part 24 Phase 59 slice 2 step
// 1, the four conditions stated beside `writtenOnce`. Run once, before any
// rewrite, so that the verdict a closure is judged on in round 3 of the
// lift's fixpoint is the one it was judged on in round 1.
void closureLifter::singleWriteCensus() {
    module.walk([&](ctjs::CreateCellOp cell) {
        ctjs::CellSetOp store;
        // Every use that must come AFTER the store for its value to be the
        // one this cell yields: conditions 2 and 3, collected in one walk
        // because condition 1 is only known when the walk ends.
        llvm::SmallVector<mlir::Operation *> mustFollow;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(user)) {
                // THE CELL AS THE BOX, NOT AS THE VALUE PUT IN ONE.
                // `ctjs.cell_set %other, %cell` stores this cell INTO
                // another and is not a write of it - and it is a use this
                // rule does not carry, so it fails outright.
                if (use.getOperandNumber() != 0) { return; }
                // CONDITION 1: EXACTLY ONE. Two writes make the binding
                // shared mutable state again, and which one a capture sees
                // depends on the path taken to it.
                if (store) {
                    whyNotWrittenOnce[cell.getOperation()] = "it is assigned more than once";
                    return;
                }
                store = write;
                continue;
            }
            if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) {
                mustFollow.push_back(user);
                continue;
            }
            // CONDITION 4: the use list slice 1 already admits, word for
            // word - isConstantCell asks the same of the same uses.
            //
            // AND THE CAPTURE IS NOT ASKED TO FOLLOW THE STORE. The
            // create_closure is hoisted for a function declaration and the
            // closure reads the box when it RUNS, so it is the CALL that
            // has to follow - condition 3, asked by whyCapturesDoNotReach
            // where the call sites are known.
            auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
            if (!made || use.getOperandNumber() < kFirstCapture) { return; }
            ctjs::FuncOp target = targetOf(made);
            if (!target || mutatesUpvalue.contains(target.getOperation())) { return; }
        }
        // NO STORE AT ALL IS SLICE 1'S CELL, whose value is its initial.
        // Nothing to record, and nothing to explain.
        if (!store) { return; }
        for (mlir::Operation * later : mustFollow) {
            if (dominance.properlyDominates(store.getOperation(), later)) { continue; }
            // CONDITION 2. A read the store does not dominate yields the
            // `undefined` the hoist boxed - the honest answer, and the one
            // a lift that ignored this would silently replace with the
            // stored value.
            whyNotWrittenOnce[cell.getOperation()] =
                "its one assignment does not dominate every read of it, and a read before "
                "it yields the undefined the binding was hoisted with";
            return;
        }
        writtenOnce[cell.getOperation()] = store;
    });
}

// WHICH CELLS BECOME A FRAME-LOCAL VARIABLE - part 24 Phase 59 slice 2
// step 2, the argument stated beside `carriedCells`. Run once, before any
// rewrite, for the reason singleWriteCensus is: the verdict a closure is
// judged on in round 3 of the lift's fixpoint has to be the one it was
// judged on in round 1, and lift() ADDS a use of the cell (its address, at
// each call site it rewrites) that a later walk would see and this one
// must not.
//
// ONE STRUCTURAL CLAUSE, AND IT IS THE WHOLE OF IT: every use of the box
// is a read of it, a write of it, or a capture. A cell stored into an
// object, put in another cell, returned or passed is a box something else
// holds a reference to, and a stack variable cannot stand in for one.
//
// NOTHING HERE ASKS WHO CAPTURES IT. A capturing closure that does not
// lift is refused by name, and `carriedCellReason` - run after the
// fixpoint, where the verdicts are - is what turns that into the cell's
// own diagnostic. Asking here would be asking before the answer exists.
void closureLifter::sharedCellCensus() {
    module.walk([&](ctjs::CreateCellOp cell) {
        // THE BY-VALUE PATH HAS FIRST REFUSAL - WHERE IT ACTUALLY WORKS.
        // A cell it takes is copied into a parameter: no pointer, no
        // indirection, and no aliasing question at all. But "it takes it"
        // is TWO questions and only one of them is isConstantCell:
        // whyCapturesDoNotReach then asks, at each call site, whether the
        // value and the assignment reach it, and a cell that fails THAT
        // was refused outright rather than carried. Both halves are asked
        // here so that ONE verdict per cell decides the path, and the
        // OUTERSTORE and LOOPWRITE programs - a store on one path, a store
        // in a loop whose call is after it - become pointers instead of
        // refusals.
        if (isConstantCell(cell) && !byValueMissesACall(cell)) { return; }
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
            // THE CELL AS THE BOX, NOT AS THE VALUE PUT IN ONE - the same
            // distinction singleWriteCensus draws, and for a stronger
            // reason here: `ctjs.cell_set %other, %cell` puts this box
            // inside another one, where a pointer to this frame would
            // outlive the frame.
            if (llvm::isa<ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) { continue; }
            if (llvm::isa<ctjs::CreateClosureOp>(user) && use.getOperandNumber() >= kFirstCapture) {
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
        // AND WHETHER ITS HOISTED INITIAL CAN STILL BE READ - slice 2 step
        // 3, the argument stated beside `assignedBeforeRead`. Asked HERE,
        // before any rewrite, for the reason the two censuses above are:
        // the question is about the `ctjs.call`s of the closures that
        // capture the cell, and `lift()` erases every one of them.
        if (dominatingWriteOf(cell)) { assignedBeforeRead.insert(cell.getOperation()); }
    });
}

// THE WRITE THAT MAKES THE BOX'S INITIAL UNOBSERVABLE, or null. A cell may
// have several `ctjs.cell_set`s and it is enough that ONE of them comes
// before every read: from there on the box holds a stored value, and which
// of the stores it holds only widens the join.
ctjs::CellSetOp closureLifter::dominatingWriteOf(ctjs::CreateCellOp cell) {
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
        if (!write || use.getOperandNumber() != 0) { continue; }
        if (writeReachesEveryRead(cell, write)) { return write; }
    }
    return {};
}

// DOES THIS STORE COME BEFORE EVERY READ OF THE BINDING? Two halves, and
// they are conditions 2 and 3 of slice 2 step 1 asked of the same use list
// with the same `DominanceInfo`:
//
//   2. it properly dominates every `ctjs.cell_get` of the box - the reads
//      in the frame that owns it, which is every read of the SSA value
//      because a `ctjs.func` body is the region the cell is defined in;
//   3. and it properly dominates every CALL of every closure that captured
//      it. THE CALL AND NOT THE `ctjs.create_closure`: a function
//      declaration is hoisted, so its closure is built in the prologue
//      before any store, and the interpreter reads the box when the closure
//      RUNS (run_loop.cpp, VM_CASE(get_upvalue)). A read through the
//      pointer, at any nesting depth, happens inside one of those calls -
//      the capture cannot escape (whyNotLiftable condition 4) and the
//      pointer cannot either (whyUpvalueReadsDoNotLift), so there is no
//      other way to reach the storage.
//
// A USE IN ANOTHER `ctjs.func` FAILS OUTRIGHT rather than being dominated,
// and the reason is the same one `byValueMissesACall` gives: builtin.
// module's body is a graph region, in which `properlyDominates` answers yes
// for every pair of operations, so a bare dominance question about a call
// in another frame is not a question at all. That is the METHODCAP shape.
bool closureLifter::writeReachesEveryRead(ctjs::CreateCellOp cell, ctjs::CellSetOp store) {
    auto owner = cell->getParentOfType<ctjs::FuncOp>();
    if (!owner || store->getParentOfType<ctjs::FuncOp>() != owner) { return false; }
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) {
            // CONDITION 2.
            if (!dominance.properlyDominates(store.getOperation(), user)) { return false; }
            continue;
        }
        // ANOTHER WRITE ONLY WIDENS THE JOIN. It cannot restore the initial,
        // so it is not asked to follow anything.
        if (llvm::isa<ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) { continue; }
        if (llvm::isa<ctjs::CreateClosureOp>(user) && use.getOperandNumber() >= kFirstCapture) {
            // CONDITION 3, over every use of the closure VALUE and not only
            // over the calls. This census runs before `whyNotLiftable` has
            // said which of those uses are calls at all, and a use that is
            // not one refuses the lift - so requiring the store to dominate
            // all of them is the same set, asked without waiting for a
            // verdict that does not exist yet.
            for (mlir::Operation * at : user->getResult(0).getUsers()) {
                // THE STORE IS NOT A CALL - PHASE 59 SLICE 2 STEP 4.
                // `var f = function () { ... f(); };` captures the very box
                // it is then written into, so this store IS one of the
                // closure's users and asking it to dominate itself asks the
                // wrong question: a read of the binding inside that
                // function happens when the function RUNS, and every call
                // of it is a read of the box that this same walk has
                // already required the store to dominate. Without this
                // clause the recursive local function is refused by
                // arithmetic rather than by an argument.
                if (at == store.getOperation()) { continue; }
                if (at->getParentOfType<ctjs::FuncOp>() != owner) { return false; }
                if (!dominance.properlyDominates(store.getOperation(), at)) { return false; }
            }
            continue;
        }
        // EVERY OTHER USE IS ONE sharedCellCensus HAS ALREADY REFUSED, so
        // this arm is unreachable from that caller - and it answers `false`
        // rather than trusting that, because the whole of this rule is that
        // there is no way to the storage it has not looked at.
        return false;
    }
    return true;
}

// IS THIS CELL'S HOISTED INITIAL UNREADABLE? Keyed on the operation, like
// `isCarried`, because the census and the stamp are in different passes
// over the module.
bool closureLifter::isAssignedBeforeRead(ctjs::CreateCellOp cell) const {
    return assignedBeforeRead.contains(cell.getOperation());
}

// WOULD THE BY-VALUE PATH REACH EVERY CALL? The question
// whyCapturesDoNotReach asks per call site, asked here per CELL, because
// the choice between copying a binding and pointing at it has to be made
// once for the whole program: capturedValue, the call-site rewrite and the
// parameter's type all read it, and two of them disagreeing is a pointer
// passed where a double is expected.
//
// ONLY THE CALLS IN THE CELL'S OWN FUNCTION, and that restriction is the
// point. A call in another ctjs.func is the METHODCAP refusal - lifting
// has nothing to prepend there - and carrying cannot help it: the variable
// is not in that frame either. Comparing the functions first also keeps
// `properlyDominates` honest, since builtin.module's body is a graph
// region in which every operation dominates every other.
bool closureLifter::byValueMissesACall(ctjs::CreateCellOp cell) {
    auto owner = cell->getParentOfType<ctjs::FuncOp>();
    const mlir::Value value = constantValueOf(cell);
    ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation());
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
        if (!made || use.getOperandNumber() < kFirstCapture) { continue; }
        for (mlir::Operation * at : made.getResult().getUsers()) {
            if (at->getParentOfType<ctjs::FuncOp>() != owner) { continue; }
            if (!dominance.properlyDominates(value, at)) { return true; }
            if (write && !dominance.properlyDominates(write.getOperation(), at)) { return true; }
        }
    }
    return false;
}

// IS THIS CELL CARRIED BY POINTER? Spelled as a function because the
// census's set is keyed on the operation and three callers ask.
bool closureLifter::isCarried(ctjs::CreateCellOp cell) const {
    return carriedCells.contains(cell.getOperation());
}

// AND IS CAPTURE SLOT i OF THIS CLOSURE ONE? Two shapes, exactly the two
// `capturedValue` carries: the operand is a carried cell of this frame, or
// the slot is filled from the ENCLOSING closure's upvalue k and that
// function's own capture parameter 3 + k is already a pointer (slice 1b,
// carried outward). The second row is what makes a shared binding reach a
// closure two levels in, and it is read off the attribute the enclosing
// lift wrote rather than re-derived.
//
// IT IS A PROPERTY OF THE SLOT AND NOT OF THE TARGET, deliberately. A
// target that only READS a capture still takes a pointer when the operand
// is a pointer - the alternative is one ctjs.func with two signatures.
bool closureLifter::slotIsCarried(ctjs::CreateClosureOp c, unsigned i) {
    if (auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>()) {
        return isCarried(cell);
    }
    const std::int32_t k = enclosingIndex(c, i);
    if (k < 0) { return false; }
    auto enclosing = c->getParentOfType<ctjs::FuncOp>();
    if (!enclosing) { return false; }
    auto listed = enclosing->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.cell_args");
    return listed &&
           llvm::is_contained(listed.asArrayRef(),
                              static_cast<int32_t>(captureArgument(static_cast<unsigned>(k))));
}

// THE VALUE EVERY READ OF A CONSTANT CELL YIELDS. The cell's initial when
// nothing writes it - slice 1 - and the STORE'S OPERAND when one write
// dominates every read and every capture, because from there on the box
// holds what that write put in it and no read can see anything else.
//
// ONE FUNCTION, THREE CALLERS, and that is the point: capturedValue() hands
// it to a lifted call site, unboxCells() writes it over every read, and
// isConstantCell() decides whether either may happen. They were three
// spellings of `cell.getInitial()` and a rule that changed the value had to
// change all three together or lower a program that prints undefined.
mlir::Value closureLifter::constantValueOf(ctjs::CreateCellOp cell) {
    if (ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation())) {
        return write.getValue();
    }
    return cell.getInitial();
}

// A CELL WHOSE VALUE IS THE SAME AT EVERY READ, which is the whole of the
// immutability proof. Every use is a read, or a capture into a function
// that writes no upvalue, or - PHASE 59 SLICE 2 STEP 1 - the ONE
// ctjs.cell_set that singleWriteCensus proved dominates all of them. Any
// other write, and any use this does not name, fails it.
bool closureLifter::isConstantCell(ctjs::CreateCellOp cell) {
    ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation());
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
        // THE ONE DOMINATING WRITE. `writtenOnce` holds it only when the
        // census proved conditions 1 to 4 of the rule beside it, so this
        // arm is admitting a store that has ALREADY been shown to come
        // before every read and every capture in the map's own walk - the
        // two walks ask the same question of the same use list.
        if (write && user == write.getOperation()) { continue; }
        auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
        if (!made || use.getOperandNumber() < kFirstCapture) { return false; }
        ctjs::FuncOp target = targetOf(made);
        if (!target || mutatesUpvalue.contains(target.getOperation())) { return false; }
    }
    return true;
}

// WHY A CARRIED CELL CANNOT BE A FRAME-LOCAL VARIABLE AFTER ALL, or
// nothing. Asked once, after the lift's fixpoint has settled every verdict.
//
// THE USE LIST IS THE CENSUS'S PLUS ONE: lift() added the cell as an
// ARGUMENT of every ctjs.call_direct it wrote, at an index
// `ctnative.cell_args` lists - the address the callee reads through. That
// use did not exist when sharedCellCensus ran and is the reason the census
// cannot simply be re-run here.
// `ctnative.cell_args` ON A CALL, ASKED HERE. admission has the same
// predicate and is declared below this struct, so this is the one line of
// it that has to be spelled twice - kept to one line for the reason
// admissionIsDeclaration is: two copies of a rule drift, two copies of a
// lookup cannot.
bool closureLifter::namesACellArgument(mlir::Operation * call, mlir::OpOperand & use) {
    auto listed = call->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.cell_args");
    return listed &&
           llvm::is_contained(listed.asArrayRef(), static_cast<int32_t>(use.getOperandNumber()));
}

std::optional<std::string> closureLifter::whyCarriedCellStaysABox(ctjs::CreateCellOp cell) {
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
        if (llvm::isa<ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) { continue; }
        if (llvm::isa<ctjs::CallDirectOp>(user) && namesACellArgument(user, use)) { continue; }
        if (llvm::isa<ctjs::CreateClosureOp>(user) && use.getOperandNumber() >= kFirstCapture) {
            if (user->hasAttr("ctnative.lifted")) { continue; }
            auto reason = user->getAttrOfType<mlir::StringAttr>("ctnative.closure_reason");
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

// A CELL WHOSE EVERY CLOSURE IS LIFTED holds a value nobody can change, so
// a read of it IS that value and the box is not built at all. Done after
// every lift, because a cell captured by one lifted and one unlifted
// closure must stay a cell for the unlifted one - which is refused, but
// whose IR this pass has no business falsifying.
void closureLifter::unboxCells(liftReport & out) {
    llvm::SmallVector<ctjs::CreateCellOp> cells;
    module.walk([&](ctjs::CreateCellOp cell) { cells.push_back(cell); });
    for (ctjs::CreateCellOp cell : cells) {
        // PHASE 59 SLICE 2 STEP 2: A CARRIED CELL IS NOT UNBOXED. There is
        // no one value to write over its reads - that is why it is
        // carried - so the box stays in the IR and becomes an
        // `emitc.variable` of its carrier, with every read a load of it,
        // every write an assign to it, and every lifted call passing its
        // address.
        //
        // WHAT HAS TO BE ASKED HERE AND NOWHERE ELSE: that every closure
        // capturing it actually lifted. The census could not ask - the
        // verdicts did not exist yet - and it is the condition the whole
        // pointer argument rests on, because an UNLIFTED closure over this
        // binding is a real ctjs.create_closure that needs a real box, and
        // a stack variable is not one. The function is refused either way
        // (an unlifted closure has no lowering), but the sentence has to
        // name the binding rather than the operation.
        if (isCarried(cell)) {
            if (const std::optional<std::string> why = whyCarriedCellStaysABox(cell)) {
                cell->setAttr("ctnative.cell_reason", mlir::StringAttr::get(context, *why));
                continue;
            }
            cell->setAttr("ctnative.carried", mlir::UnitAttr::get(context));
            // PHASE 59 SLICE 2 STEP 3: AND WHETHER ITS INITIAL IS STILL
            // READABLE. The verdict is the census's - taken before the
            // lift, where the calls it asks about still exist - and this
            // is where it is written onto the IR, because
            // `TypeInference::cellTypeOf` is what consumes it and the
            // solve runs after this pass's rewrites.
            if (isAssignedBeforeRead(cell)) {
                cell->setAttr(kAssignedBeforeRead, mlir::UnitAttr::get(context));
            }
            ++out.locals;
            continue;
        }
        // PHASE 59 SLICE 2 STEP 1: THE ONE WRITE THIS CELL IS ALLOWED, or
        // null. The census proved it dominates every read and every
        // capture, so from it onwards the box holds one value and the box
        // itself is not needed.
        ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation());
        std::optional<std::string> why;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
            if (write && user == write.getOperation()) { continue; }
            if (llvm::isa<ctjs::CreateClosureOp>(user) && use.getOperandNumber() >= kFirstCapture) {
                if (user->hasAttr("ctnative.lifted")) { continue; }
                auto reason = user->getAttrOfType<mlir::StringAttr>("ctnative.closure_reason");
                why = "the closure that captures it is not lifted" +
                      (reason ? " - " + reason.getValue().str() : std::string{});
                break;
            }
            if (llvm::isa<ctjs::CellSetOp>(user)) {
                // A WRITE THE RULE ABOVE DID NOT TAKE, and the census knows
                // which clause it failed. Without that sentence the message
                // is the old one, which is true of a cell with two writes
                // and says nothing about which of them is the problem.
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
        // A REASON ON THE BOX ITSELF, because the box is what admission
        // meets first: `op::new_cell` runs in the prologue, before the
        // `closure` opcode that captures it, so a walk in program order
        // reaches the cell and would otherwise refuse the function with
        // "`ctjs.create_cell` is not native yet" - a sentence that names
        // neither the binding nor what is wrong with it.
        if (why) {
            cell->setAttr("ctnative.cell_reason", mlir::StringAttr::get(context, *why));
            continue;
        }
        llvm::SmallVector<ctjs::CellGetOp> reads;
        for (mlir::Operation * user : cell.getResult().getUsers()) {
            if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(user)) { reads.push_back(read); }
        }
        // THE VALUE, WHICHEVER OF THE TWO IT IS. `constantValueOf` is the
        // one place that decides, and capturedValue() has already handed
        // the SAME value to every lifted call site of every closure that
        // captured this cell - which is why the two cannot be allowed to
        // disagree and are one function.
        const mlir::Value value = constantValueOf(cell);
        for (ctjs::CellGetOp read : reads) {
            read.getResult().replaceAllUsesWith(value);
            read.erase();
        }
        // AND THE WRITE GOES WITH THE BOX. There is no box left to write:
        // every read has been replaced by what the write stored, so a
        // ctjs.cell_set left behind would name a create_cell nothing else
        // uses and refuse the whole function for an operation with nothing
        // to do. Erased after the reads, because both use the cell.
        if (write) { write.erase(); }
        cell->setAttr("ctnative.unboxed", mlir::UnitAttr::get(context));
        ++out.cells;
    }
}

} // namespace ctcompile::ctnative::lowering_detail
