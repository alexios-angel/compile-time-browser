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
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

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
    }
}

} // namespace ctcompile::ctnative::lowering_detail
