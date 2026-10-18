//===- ClosureLifting/ArrowThis.cpp - An arrow's `this` as a capture ------===//
//
// An arrow function has no `this` of its own. The interpreter fixes it once,
// when the closure is made: make_closure stores the create_closure's
// `$enclosing_this` operand as the closure's captured_this, and a call of the
// arrow enters it with that value as its receiver, whatever receiver the call
// names. So an arrow's `this` is a binding of the creating frame that never
// changes after creation, which is exactly a constant cell captured by the
// closure.
//
// captureLexicalThis() rewrites it that way before any census runs: the
// creating frame boxes `$enclosing_this` in a new ctjs.create_cell with no
// store, the closure captures that cell in a new last slot, the arrow's reads
// of its receiver argument become a ctjs.load_upvalue of that slot, and
// `$enclosing_this` becomes undefined. The value every read sees is the value
// the interpreter would have captured, so the rewrite cannot change an
// answer. Afterwards the arrow is an ordinary closure with one more capture,
// and every capture rule (constant cells, carried cells, captures relayed
// from an enclosing closure, bound local functions) decides it as it decides
// any other. A `this` no rule can carry, such as the boxed receiver of a
// function no caller proves, is refused there with that rule's reason.
//
// One case needs more than the capture rules: an arrow that reads its
// method's (or constructor's) `this` through constant keys, as in
// `total() { const f = () => this._x; return f(); }`. The capture is an
// object, so the lift passes it as an object pointer (lexicalThisObjectArgs),
// the method may hand its receiver to that call (passesLexicalThis), and the
// cell, which nothing reads once every arrow capturing it is lifted, is
// erased (releaseLexicalThisCells) so it does not open the receiver's shape.
// The pointer is a borrow: a lifted closure is only ever called directly, in
// a frame that holds its captures, and a borrowed callback only through a
// ctnative::function_ref during its callee's call, so every call ends before
// the method that made it returns, and the receiver outlives the method. An
// arrow made inside such an arrow takes the outer one's pointer the same way.
// An arrow that is stored is not lifted and keeps its refusal.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"
#include "ctcompile/CTNative/Analysis/Instances.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {
constexpr llvm::StringLiteral kLexicalThis = kNativeLexicalThis;

/// The slot that `target`'s lexical `this` is captured in, if it has one.
std::optional<unsigned> lexicalThisSlot(ctjs::FuncOp target) {
    auto slot = target ? target->getAttrOfType<mlir::IntegerAttr>(kLexicalThis) : nullptr;
    if (!slot) { return std::nullopt; }
    return static_cast<unsigned>(slot.getInt());
}

/// True when `op` (a function or one of its direct calls) lists entry argument
/// `index` as an object pointer.
bool listsObjectArg(mlir::Operation * op, unsigned index) {
    auto listed = op ? op->getAttrOfType<mlir::DenseI32ArrayAttr>(kNativeObjectArgs) : nullptr;
    return listed && llvm::is_contained(listed.asArrayRef(), static_cast<int32_t>(index));
}
} // namespace

/// Makes the lexical `this` of every arrow that reads it a constant capture.
/// Needs `byIndex` (indexAndNewTargets). Idempotent: a rewritten closure's
/// `$enclosing_this` is undefined, so a second run finds nothing to do.
void closureLifter::captureLexicalThis() {
    llvm::SmallVector<ctjs::CreateClosureOp> arrows;
    module.walk([&](ctjs::CreateClosureOp c) {
        if (ctjs::isUndefinedConstant(c.getEnclosingThis())) { return; }
        ctjs::FuncOp target = targetOf(c);
        // A target with no body, or whose capture list disagrees with the
        // closure's, keeps its operand: whyTargetIsNotLiftable refuses it for
        // that, and the new slot's number would be ambiguous.
        if (!target || target.getBody().empty() ||
            c.getUpvalues().size() != static_cast<size_t>(target.getUpvalueCount()) ||
            target.getBody().front().getNumArguments() < ctjs::implicit_arguments ||
            target.getBody().front().getArgument(ctjs::arg_receiver).use_empty()) {
            return;
        }
        arrows.push_back(c);
    });
    const auto valueType = ctjs::ValueType::get(context);
    // Every closure of one target has the same capture count (checked above),
    // so they all append the same slot; the target is rewritten once.
    llvm::SmallPtrSet<mlir::Operation *, 8> rewritten;
    for (ctjs::CreateClosureOp c : arrows) {
        ctjs::FuncOp target = targetOf(c);
        const auto slot = static_cast<unsigned>(c.getUpvalues().size());
        // The creating frame: box the value make_closure would capture, at
        // the point it would capture it.
        mlir::OpBuilder at(c);
        auto cell = ctjs::CreateCellOp::create(at, c.getLoc(), valueType, c.getEnclosingThis());
        cell->setAttr(kLexicalThis, mlir::UnitAttr::get(context));
        c.getUpvaluesMutable().append(cell.getResult());
        if (const mlir::DenseI32ArrayAttr indices = c.getEnclosingIndicesAttr()) {
            llvm::SmallVector<int32_t> extended(indices.asArrayRef());
            extended.push_back(-1); // From this frame, not relayed.
            c.setEnclosingIndicesAttr(mlir::DenseI32ArrayAttr::get(context, extended));
        }
        c.getEnclosingThisMutable().assign(undefinedAt(at, c.getLoc()));
        if (!rewritten.insert(target.getOperation()).second) { continue; }
        // The arrow: read the capture where it read its receiver. The read
        // goes right after the entry block's frame_enter, which is where the
        // importer puts its own upvalue reads and where makeRunOnceCellsStatic
        // puts the cell it may turn this read into, so that cell still
        // dominates it. Nothing reads the receiver before the frame is
        // entered, so the read dominates every former use, nested arrows'
        // `$enclosing_this` operands included.
        mlir::Block & entry = target.getBody().front();
        mlir::OpBuilder in(&entry, entry.begin());
        for (auto frame : entry.getOps<ctjs::FrameEnterOp>()) {
            in.setInsertionPointAfter(frame);
            break;
        }
        auto read = ctjs::LoadUpvalueOp::create(in, target.getLoc(), valueType,
                                                entry.getArgument(ctjs::arg_callee), slot);
        entry.getArgument(ctjs::arg_receiver).replaceAllUsesWith(read.getResult());
        target->setAttr("upvalue_count",
                        mlir::Builder(context).getI32IntegerAttr(static_cast<int32_t>(slot + 1)));
        target->setAttr(kLexicalThis,
                        mlir::Builder(context).getI32IntegerAttr(static_cast<int32_t>(slot)));
    }
}

/// The lexical `this` slot of `made`'s target, as an entry-block index, when
/// lift() can pass it as an object pointer: every closure captures the cell
/// captureLexicalThis made from the receiver of the function that creates the
/// closure, the slot is not carried by address, and the arrow uses `this`
/// only to read or write constant keys. Whether that receiver is a closed
/// object is the method's own proof, made where the method lifts.
///
/// A borrowed callback qualifies too, although its bound callable copies the
/// pointer: the callee takes it as a ctnative::function_ref, which it can only
/// call during the call, never store, so the callable and every call through
/// it end within the method's frame, and the receiver outlives that frame.
llvm::SmallVector<int32_t> closureLifter::lexicalThisObjectArgs(
    llvm::ArrayRef<ctjs::CreateClosureOp> made) {
    if (made.empty()) { return {}; }
    ctjs::FuncOp target = targetOf(made.front());
    const std::optional<unsigned> slot = lexicalThisSlot(target);
    if (!slot) { return {}; }
    for (ctjs::CreateClosureOp c : made) {
        if (*slot >= c.getUpvalues().size() || slotIsCarried(c, *slot)) { return {}; }
        auto cell = c.getUpvalues()[*slot].getDefiningOp<ctjs::CreateCellOp>();
        // The host keeps a callback holding a registry instance: it binds the
        // instance's owner (passesLexicalThis), not a pointer it would outlive.
        if (cell && cell->hasAttr(kNativeHeldThis) && hostCallbacks.contains(c.getOperation())) {
            return {};
        }
        auto self = cell ? llvm::dyn_cast<mlir::BlockArgument>(cell.getInitial()) : nullptr;
        auto maker = c->getParentOfType<ctjs::FuncOp>();
        if (!cell || !cell->hasAttr(kLexicalThis) || !self || !maker ||
            self.getOwner() != &maker.getBody().front()) {
            return {};
        }
        // The maker's receiver, or, for an arrow made inside an arrow, the
        // maker's own lexical `this`, which its lift already made an object
        // pointer: the enclosing arrow is lifted first, because this closure
        // reads a capture it relays.
        const std::optional<unsigned> relayed = lexicalThisSlot(maker);
        const bool fromReceiver = self.getArgNumber() == ctjs::arg_receiver;
        const bool fromArrow = relayed && self.getArgNumber() == captureArgument(*relayed) &&
                               listsObjectArg(maker, self.getArgNumber());
        if (!fromReceiver && !fromArrow) { return {}; }
    }
    // Constant-key reads and writes, the receiver of a method call the method
    // lift makes direct (or has made direct), which takes the pointer as its
    // `this`, or the cell of an arrow made in here, which that arrow's own
    // lift decides; if it does not take `this` as a pointer, the cell stays,
    // opens this parameter's shape, and admission refuses this arrow.
    const auto receives = [&](mlir::OpOperand & use) {
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner())) {
            return use.getOperandNumber() == 1 && resolveMethod(call).has_value();
        }
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
        return direct && use.getOperandNumber() == ctjs::arg_receiver &&
               direct->hasAttr(kNativeReceiver);
    };
    bool constantKeysOnly = true;
    target.getBody().walk([&](ctjs::LoadUpvalueOp read) {
        if (static_cast<unsigned>(read.getIndex()) != *slot) { return; }
        for (mlir::OpOperand & use : read.getResult().getUses()) {
            auto inner = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
            constantKeysOnly &= isConstantKeyAccess(use) || receives(use) ||
                                (inner && inner->hasAttr(kLexicalThis));
        }
    });
    if (!constantKeysOnly) { return {}; }
    return {static_cast<int32_t>(captureArgument(*slot))};
}

/// True when `use` of a function's receiver only hands it to an arrow lifted
/// with lexicalThisObjectArgs: as that object argument of a direct call, as
/// that capture of a borrowed callback, or as the initial value of the
/// arrow's cell while every closure capturing the cell is such a lifted arrow
/// (releaseLexicalThisCells erases it later).
bool closureLifter::passesLexicalThis(mlir::OpOperand & use) {
    mlir::Operation * user = use.getOwner();
    // A borrowed callback's capture: rewriteBorrowedCallback replaced the cell
    // with its value. The borrow ends within the method's frame (see
    // lexicalThisObjectArgs), so the receiver outlives every call through it.
    if (auto c = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
        c && c->hasAttr(kNativeBorrowedCallback)) {
        ctjs::FuncOp target = targetOf(c);
        const std::optional<unsigned> slot = lexicalThisSlot(target);
        return slot && use.getOperandNumber() == kFirstCapture + *slot &&
               listsObjectArg(target, captureArgument(*slot));
    }
    if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
        const std::optional<unsigned> slot = lexicalThisSlot(call.getTarget());
        return slot && use.getOperandNumber() == captureArgument(*slot) &&
               listsObjectArg(call, use.getOperandNumber()) &&
               listsObjectArg(call.getTarget(), use.getOperandNumber());
    }
    auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(user);
    // A registry instance's `this`, boxed for arrows that only read fields and
    // call methods through it (kNativeHeldThis, Analysis/Instances.h), each
    // lifted as a host callback: the host binds a copy of the capture, which
    // is the instance's std::shared_ptr owner (EmitC/HostCallbacks.cpp), and
    // keeps the instance alive as long as it keeps the callback.
    if (cell && cell->hasAttr(kNativeHeldThis) &&
        llvm::all_of(cell.getResult().getUses(), [&](mlir::OpOperand & capture) {
            auto c = llvm::dyn_cast<ctjs::CreateClosureOp>(capture.getOwner());
            ctjs::FuncOp target = c ? targetOf(c) : ctjs::FuncOp{};
            return c && c->hasAttr(kNativeLifted) && target && target->hasAttr(kNativeHostCallback);
        })) {
        return true;
    }
    // A cell makeRunOnceCellsStatic turned into a static variable is that
    // variable's definition, read by name from other functions, not a box
    // only these arrows read.
    if (!cell || !cell->hasAttr(kLexicalThis) || cell->hasAttr(kNativeStaticCell)) { return false; }
    return llvm::all_of(cell.getResult().getUses(), [&](mlir::OpOperand & capture) {
        auto c = llvm::dyn_cast<ctjs::CreateClosureOp>(capture.getOwner());
        if (!c || !c->hasAttr(kNativeLifted) || c->hasAttr(kNativeBorrowedCallback)) {
            return false;
        }
        ctjs::FuncOp target = targetOf(c);
        const std::optional<unsigned> slot = lexicalThisSlot(target);
        return slot && capture.getOperandNumber() == kFirstCapture + *slot &&
               listsObjectArg(target, captureArgument(*slot));
    });
}

/// The value a read of an arrow's lexical `this` stands for: what the cell
/// captureLexicalThis made for the arrow's one closure was made from. That is
/// the receiver of the function making the closure, or, when that function is
/// an arrow too, its own read of this kind. Null for any other value, and for
/// a read rewritten since (a lifted arrow reads a capture parameter instead).
mlir::Value closureLifter::lexicalThisMaker(mlir::Value value) {
    ctjs::CreateCellOp cell = lexicalThisCell(value);
    return cell ? cell.getInitial() : mlir::Value{};
}

/// The cell that value lexicalThisMaker answers for `value` is boxed in, or
/// null.
ctjs::CreateCellOp closureLifter::lexicalThisCell(mlir::Value value) {
    auto read = value.getDefiningOp<ctjs::LoadUpvalueOp>();
    auto arrow = read ? read->getParentOfType<ctjs::FuncOp>() : ctjs::FuncOp{};
    const std::optional<unsigned> slot = lexicalThisSlot(arrow);
    if (!slot || static_cast<unsigned>(read.getIndex()) != *slot ||
        read.getClosure() != arrow.getBody().front().getArgument(ctjs::arg_callee)) {
        return {};
    }
    ctjs::CreateClosureOp c = uniqueClosureByTarget.lookup(arrow);
    auto cell = c && *slot < c.getUpvalues().size()
                    ? c.getUpvalues()[*slot].getDefiningOp<ctjs::CreateCellOp>()
                    : ctjs::CreateCellOp{};
    return cell && cell->hasAttr(kLexicalThis) ? cell : ctjs::CreateCellOp{};
}

/// True when `use` of a function's `this` hands it to the host callback the
/// ctjs.call `use` is in registers, as the capture of a held cell (see
/// passesLexicalThis) whose closure is that callback's.
bool closureLifter::handsHeldThis(mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    auto named = call ? call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostCallback)
                      : mlir::FlatSymbolRefAttr{};
    if (!named || use.getOperandNumber() < 2) { return false; }
    return llvm::any_of(use.get().getUsers(), [&](mlir::Operation * user) {
        auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(user);
        return cell && cell->hasAttr(kNativeHeldThis) &&
               llvm::any_of(cell->getUsers(), [&](mlir::Operation * captor) {
                   auto c = llvm::dyn_cast<ctjs::CreateClosureOp>(captor);
                   ctjs::FuncOp target = c ? targetOf(c) : ctjs::FuncOp{};
                   return target && target.getSymName() == named.getValue();
               });
    });
}

/// The first use of `arrow`'s one closure that is not a call of it (callSiteOf),
/// or null. With none, the arrow runs only during calls its maker makes, so
/// the maker's receiver outlives every one of them; stored or passed on, it
/// can run after the maker returns, when nothing holds that receiver.
mlir::OpOperand * closureLifter::useOutlivingMaker(ctjs::FuncOp arrow) {
    ctjs::CreateClosureOp c = uniqueClosureByTarget.lookup(arrow);
    if (!c) { return nullptr; }
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        if (!callSiteOf(use, arrow)) { return &use; }
    }
    return nullptr;
}

/// Erases each cell captureLexicalThis made whose every capturing closure is
/// an arrow lifted with its `this` as an object argument. Those closures
/// lower to nothing and their targets read the capture from the call's
/// argument, so the cell is never read; left in place it would be a use of
/// the receiver that opens its shape. The capture operand becomes undefined.
void closureLifter::releaseLexicalThisCells() {
    llvm::SmallVector<ctjs::CreateCellOp> cells;
    module.walk([&](ctjs::CreateCellOp cell) {
        if (cell->hasAttr(kLexicalThis)) { cells.push_back(cell); }
    });
    for (ctjs::CreateCellOp cell : cells) {
        // The same rule the method's lift accepted this use of its receiver by.
        if (!passesLexicalThis(cell->getOpOperand(0))) { continue; }
        llvm::SmallVector<mlir::OpOperand *> captures;
        for (mlir::OpOperand & capture : cell.getResult().getUses()) {
            captures.push_back(&capture);
        }
        for (mlir::OpOperand * capture : captures) {
            mlir::OpBuilder at(capture->getOwner());
            capture->set(undefinedAt(at, capture->getOwner()->getLoc()));
        }
        cell.erase();
    }
}

} // namespace ctcompile::ctnative::lowering_detail
