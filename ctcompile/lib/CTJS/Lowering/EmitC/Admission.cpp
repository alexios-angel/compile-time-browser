//===- Admission.cpp - What the EmitC lowering accepts --------------------===//
//
// Decides whether the VM-backed EmitC lowering can compile a ctjs.func, and
// records why not (a ctjs.not_lowered attribute) when it cannot.
//
//===----------------------------------------------------------------------===//
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

/// Whether the runtime has a body for `helper`.
///
/// A helper declared in the ABI tables (ctbrowser/include/ctbrowser/aot/
/// helpers/*.def) may have a prototype in aot.hpp but no body yet. A call to
/// one compiles and then fails at link time, so the .def alone cannot decide
/// what the backend may emit. The ctcompile_linkable test keeps this list
/// accurate by linking a translation unit that uses every operation the
/// backend accepts.
bool runtime_defines(llvm::StringRef helper) {
    // Currently empty: every helper the backend can emit has a body. Keep the
    // list, because the next operation to be lowered may need a helper that
    // does not exist yet.
    static constexpr llvm::StringLiteral undefined_yet[] = {llvm::StringLiteral("")};
    for (const llvm::StringLiteral & absent : undefined_yet) {
        if (helper == absent) { return false; }
    }
    return true;
}

/// Marks `function` as refused, with the reason as an attribute ctjs-opt
/// prints.
void refuse(FuncOp function, llvm::StringRef because) {
    function->setAttr(notLoweredMarker, mlir::StringAttr::get(function.getContext(), because));
}

/// Whether every operation in the body is one this pass knows how to emit;
/// otherwise `why` holds a reason.
///
/// An allow-list, not a deny-list: a deny-list would lower an operation nobody
/// considered, and being wrong here gives a translation unit that compiles and
/// computes something else.
bool body_is_supported(FuncOp function, std::string & why) {
    bool supported = true;
    // The walk does not stop at a refusal, so the last reason wins.
    const auto fail = [&](std::string because) {
        supported = false;
        why = std::move(because);
    };
    function.getBody().walk([&](mlir::Operation * op) {
        // ctjs.frame_exit must come immediately before the return. The
        // importer puts it there. The
        // boxed emitter coalesces their release into ct_aot_leave_return so
        // the normalized result can be handed to the oracle. Keeping the
        // adjacency requirement makes the deferred release match that marker.
        if (mlir::isa<FrameExitOp>(op)) {
            if (op->getNextNode() == nullptr || !mlir::isa<ReturnOp>(op->getNextNode())) {
                fail("ctjs.frame_exit is not immediately followed by ctjs.return - the boxed "
                     "exit requires a return-value handoff");
            }
            return;
        }
        // Arithmetic. Both binary families return a ct_aot_status, so each
        // becomes a call, a test against ct_aot_status::ok by name, and a
        // branch to the shared failure path. A kind the family does not serve
        // is refused rather than compiled into op::halt, which
        // ct_aot_binary_op's switch would answer with undefined.
        //
        // Every unary kind lowers. Negation and bitwise-not still check
        // runtime_defines(), so a missing helper body is refused instead of
        // causing a link failure.
        if (auto unary = mlir::dyn_cast<UnaryOp>(op)) {
            if ((unary.getKind() == UnaryKind::Neg && !runtime_defines("ct_aot_negate")) ||
                (unary.getKind() == UnaryKind::BitNot && !runtime_defines("ct_aot_bit_not"))) {
                fail("the helper for this unary operator is declared in aot.hpp and defined "
                     "nowhere - emitting a call to it compiles and fails at link");
            }
            return;
        }
        // A ctjs.check's handler operands must be its own block's arguments.
        //
        // The lowering resolves them when it enters the block, before the
        // block's operations are converted, because that is where the caught
        // edge has to be armed for every fallible call the block contains. That
        // is sound exactly while the importer's one-instruction-per-protected-
        // block split holds, which makes the snapshot the block's own
        // arguments - and p5 contains shapes where it does not.
        //
        // Refused rather than asserted. A null operand is accepted silently by
        // MLIR and crashes later inside Operation::create with a stack naming
        // neither this file nor the function, so refusing loses only the few
        // functions with the shape instead of the whole compile.
        if (auto guarded = mlir::dyn_cast<CheckOp>(op)) {
            for (const mlir::Value each : guarded.getHandlerOperands()) {
                const auto argument = mlir::dyn_cast<mlir::BlockArgument>(each);
                if (argument == nullptr || argument.getOwner() != guarded->getBlock()) {
                    fail("a protected block carries a handler snapshot that is not its own "
                         "block argument, so the caught edge cannot be armed before the block "
                         "is converted");
                    return;
                }
            }
            return;
        }
        // A resolved call lowers as ctjs.call (ct_aot_call through the callee
        // value), and ct_aot_call has no new.target parameter: the runtime
        // pushes the frame with pending_new_target_, which a plain call leaves
        // undefined. So the operand must be that constant, or this backend
        // would drop a value the resolver put there on purpose.
        if (auto direct = mlir::dyn_cast<CallDirectOp>(op)) {
            auto constant = direct.getNewTarget().getDefiningOp<ConstantOp>();
            if (!constant || !mlir::isa<UndefinedAttr>(constant.getValue())) {
                fail("ctjs.call_direct carries a new.target that is not the undefined constant, "
                     "and ct_aot_call has no parameter to pass one through");
            }
            return;
        }
        if (mlir::isa<ReturnOp, CompareOp, ThrowOp, FromBoolOp, DefineAccessorOp, DeleteNamedOp,
                      PushHandlerOp, PopHandlerOp, CatchLandOp>(op)) {
            return;
        }

        // The upvalue operations lower to two calls each, which is why they
        // are plain CTJS_Ops rather than CTJS_RuntimeOps. The ABI splits the
        // fused opcode deliberately: ct_aot_upvalue_cell answers undefined for
        // a missing closure or an out-of-range index, ct_aot_cell_get no-ops on
        // a non-cell, and composed they are exactly the guard VM_CASE
        // (get_upvalue) writes inline.
        //
        // The operand must be this frame's own closure. ct_aot_upvalue_cell
        // reads the frame, so an operation naming a different closure would
        // read the wrong one. The importer only ever names the callee
        // argument, so refusing anything else costs nothing and closes the
        // hole for hand-written IR.
        if (mlir::isa<LoadUpvalueOp, StoreUpvalueOp>(op)) {
            const mlir::Value named = mlir::isa<LoadUpvalueOp>(op)
                                          ? mlir::cast<LoadUpvalueOp>(op).getClosure()
                                          : mlir::cast<StoreUpvalueOp>(op).getClosure();
            if (named != function.getBody().front().getArgument(arg_callee)) {
                fail("an upvalue operation names a closure other than this frame's own - "
                     "ct_aot_upvalue_cell reads the frame, so it would read the wrong one");
            }
            return;
        }
        if (mlir::isa<LoadGlobalOp>(op) && !runtime_defines("ct_aot_global_get")) {
            fail("ct_aot_global_get is declared in aot.hpp and defined nowhere - emitting a call "
                 "to it compiles and fails at link");
            return;
        }
        if (mlir::isa<StoreGlobalOp>(op) && !runtime_defines("ct_aot_global_set")) {
            fail("ct_aot_global_set is declared in aot.hpp and defined nowhere - emitting a call "
                 "to it compiles and fails at link");
            return;
        }
        if (auto binary = mlir::dyn_cast<BinaryOp>(op)) {
            if (!is_valid_binary(binary.getKind())) {
                fail("ctjs.binary was given a kind only the static family serves");
            }
            return;
        }
        if (auto binary = mlir::dyn_cast<BinaryStaticOp>(op)) {
            if (!is_valid_binary_static(binary.getKind())) {
                fail("ctjs.binary_static was given a kind only the re-entering family serves");
            }
            return;
        }
        // Every other CTJS_RuntimeOp: each has a lowering of its own or is the
        // single helper call convertRuntime() builds from its ABI entry.
        if (mlir::isa<RuntimeCallOpInterface>(op)) { return; }
        // Branches. Their block arguments are handled by a later pass
        // (EmitCBlockArguments.cpp), because emitting them as they stand would
        // hit the copy the C++ emitter loses.
        if (mlir::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp>(op)) { return; }
        if (auto constant = mlir::dyn_cast<ConstantOp>(op)) {
            // Constants the backend can emit: undefined, null, booleans,
            // numbers, strings and BigInts (isConstantAttr). A number is
            // emitted from its attribute's bit pattern, not from a decimal
            // literal, because -0.0 and 0.0 are different JavaScript values
            // but print the same way (see constant_value).
            if (isConstantAttr(constant.getValue())) { return; }
            fail("no lowering yet for this constant");
            return;
        }
        fail(("no lowering yet for " + op->getName().getStringRef()).str());
    });
    return supported;
}

} // namespace ctcompile::ctjs::emitc_detail
