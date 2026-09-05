#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {
// THE HELPERS THE RUNTIME DECLARES BUT DOES NOT DEFINE.
//
// aot_helpers.def declares 69 rows and aot_bridge.cpp defines 32 of them. The
// other 37 have prototypes in aot.hpp and no body anywhere, so a call to one
// COMPILES PERFECTLY and fails at link.
//
// THAT IS EXACTLY WHAT HAPPENED. This backend emitted ct_aot_global_get and
// ct_aot_negate for two commits, and every test passed - because every EmitC
// test compiled the output with -fsyntax-only and none of them linked it.
// Linking it by hand gives "undefined reference to `ct_aot_global_get'".
//
// SO THE ROWS ARE NOT ENOUGH TO DECIDE WHAT TO EMIT, and this is the one place
// where the .def cannot be the single source of truth: it records what the ABI
// IS, not what has been built yet. This list is the second source, and it is
// held honest by ctcompile_linkable, which links a translation unit exercising
// every operation the backend accepts - so a name here that is wrong in either
// direction fails the build rather than a program.

bool runtime_defines(llvm::StringRef helper) {
    // EMPTY, AND THAT IS THE POINT OF KEEPING IT. Every row the backend can
    // name now has a body: ct_aot_global_get, ct_aot_global_set,
    // ct_aot_negate and ct_aot_bit_not were all here and are all implemented.
    //
    // The list stays because 30 of the 69 rows still have none, and the next
    // operation lowered will need it again - and because ctcompile_linkable is
    // what keeps it honest in both directions, by linking a translation unit
    // that exercises everything the backend accepts.
    static constexpr llvm::StringLiteral undefined_yet[] = {llvm::StringLiteral("")};
    for (const llvm::StringLiteral & absent : undefined_yet) {
        if (helper == absent) { return false; }
    }
    return true;
}

// WHY A FUNCTION WAS LEFT ALONE, in a form ctjs-opt prints.
void refuse(FuncOp function, llvm::StringRef because) {
    function->setAttr("ctjs.not_lowered", mlir::StringAttr::get(function.getContext(), because));
}

// Whether every operation in the body is one this pass knows how to emit.
//
// AN ALLOW-LIST, NOT A DENY-LIST. A deny-list lowers an operation nobody
// considered by default, and the cost of being wrong here is a translation unit
// that compiles and computes something else.
bool body_is_supported(FuncOp function, std::string & why) {
    bool supported = true;
    function.getBody().walk([&](mlir::Operation * op) {
        // ctjs.frame_exit MUST BE THE LAST THING BEFORE THE RETURN.
        //
        // The shared failure path leaves the frame too, so a fallible operation
        // AFTER an in-place exit would emit a second ct_aot_leave on the way
        // out. The runtime makes that harmless - leave truncates to this
        // frame's own recorded index rather than popping, and its row says it
        // "is a harmless no-op after a failure" - so this is an unchecked
        // invariant rather than a live defect. It is checked anyway, because
        // the importer emits frame_exit immediately before every return and a
        // module where that stopped being true would be one nobody had looked
        // at.
        if (mlir::isa<FrameExitOp>(op)) {
            if (op->getNextNode() == nullptr || !mlir::isa<ReturnOp>(op->getNextNode())) {
                supported = false;
                why = "ctjs.frame_exit is not immediately followed by ctjs.return - anything "
                      "fallible after it would leave the frame twice";
            }
            return;
        }
        if (mlir::isa<FrameEnterOp, ReturnOp, TruthyOp>(op)) { return; }
        // THE ARITHMETIC, AND WITH IT THE FIRST EXCEPTION EDGE. Both families
        // answer with a ct_aot_status, so each becomes a call, a test against
        // ct_aot_status::ok by name, and a branch to the shared failure path.
        // A kind the family does not serve is refused rather than compiled into
        // op::halt, which ct_aot_binary_op's switch would answer with undefined.
        // EVERY UNARY KIND LOWERS NOW. `typeof` was refused because its result
        // is a string, and that reason went stale with the same sentence
        // ctjs.constant's did: ct_aot_new_string has a body, and every value
        // the backend produces is parked in a frame slot.
        if (auto unary = mlir::dyn_cast<UnaryOp>(op)) {
            // THE LIST STILL EXISTS, and it is empty. Every helper the backend
            // can name has a body; the check stays because 27 rows still do
            // not, and ctcompile_linkable is what keeps it honest.
            if ((unary.getKind() == UnaryKind::Neg && !runtime_defines("ct_aot_negate")) ||
                (unary.getKind() == UnaryKind::BitNot && !runtime_defines("ct_aot_bit_not"))) {
                supported = false;
                why = "the helper for this unary operator is declared in aot.hpp and defined "
                      "nowhere - emitting a call to it compiles and fails at link";
                return;
            }
            return;
        }
        // A ctjs.check's HANDLER OPERANDS MUST BE ITS OWN BLOCK'S ARGUMENTS.
        //
        // The lowering resolves them when it ENTERS the block, before the
        // block's operations are converted, because that is where the caught
        // edge has to be armed for every fallible call the block contains. That
        // is sound exactly while the importer's one-instruction-per-protected-
        // block split holds, which makes the snapshot the block's own
        // arguments - and p5 contains shapes where it does not.
        //
        // REFUSED RATHER THAN ASSERTED. A null operand is accepted silently by
        // MLIR and crashes later inside Operation::create with a stack naming
        // neither this file nor the function - so the whole p5 run died instead
        // of losing the handful of functions that actually have the shape.
        if (auto guarded = mlir::dyn_cast<CheckOp>(op)) {
            for (const mlir::Value each : guarded.getHandlerOperands()) {
                const auto argument = mlir::dyn_cast<mlir::BlockArgument>(each);
                if (argument == nullptr || argument.getOwner() != guarded->getBlock()) {
                    supported = false;
                    why = "a protected block carries a handler snapshot that is not its own "
                          "block argument, so the caught edge cannot be armed before the block "
                          "is converted";
                    return;
                }
            }
            return;
        }
        // A RESOLVED CALL LOWERS AS ctjs.call - ct_aot_call through the callee
        // VALUE - and ct_aot_call has no new.target parameter: the runtime
        // pushes the frame with pending_new_target_, which a plain call leaves
        // undefined. So the operand must BE that constant, or this tier would
        // drop a value the resolver put there on purpose.
        if (auto direct = mlir::dyn_cast<CallDirectOp>(op)) {
            auto constant = direct.getNewTarget().getDefiningOp<ConstantOp>();
            if (!constant || !mlir::isa<UndefinedAttr>(constant.getValue())) {
                supported = false;
                why = "ctjs.call_direct carries a new.target that is not the undefined constant, "
                      "and ct_aot_call has no parameter to pass one through";
            }
            return;
        }
        if (mlir::isa<CompareOp, GetPropertyOp, SetPropertyOp, CallOp, CreateClosureOp,
                      CreateCellOp, CellGetOp, CellSetOp, CreateObjectOp, CreateArrayOp, AppendOp,
                      ThrowOp, ConstructOp, IterableOp, HasPropertyOp, InstanceOfOp,
                      DeletePropertyOp, FromBoolOp, LoadHomeOp, GetProtoOp, SetProtoOp,
                      PassNewTargetOp, CallSpreadOp, ConstructSpreadOp, CopyPropsOp,
                      DefineAccessorOp, DeleteNamedOp, OwnKeysOp, MakeArgumentsOp, GatherRestOp,
                      WrapPromiseOp, PushHandlerOp, PopHandlerOp, CheckOp, CatchLandOp,
                      ModuleImportCellOp, ModuleExportCellOp, ModuleNamespaceOp, DynamicImportOp>(
                op)) {
            return;
        }

        // THE UPVALUE OPERATIONS, WHICH ARE TWO CALLS EACH - which is why they
        // are plain CTJS_Ops rather than CTJS_RuntimeOps. The ABI splits the
        // fused opcode deliberately: ct_aot_upvalue_cell answers undefined for
        // a missing closure or an out-of-range index, ct_aot_cell_get no-ops on
        // a non-cell, and composed they are exactly the guard VM_CASE
        // (get_upvalue) writes inline.
        //
        // THE OPERAND MUST BE THIS FRAME'S OWN CLOSURE. ct_aot_upvalue_cell
        // reads the frame, so an operation naming a DIFFERENT closure would be
        // lowered into a read of the wrong one - and the importer only ever
        // names the callee argument, so refusing anything else costs nothing
        // and closes the hole for hand-written IR.
        if (mlir::isa<LoadUpvalueOp, StoreUpvalueOp>(op)) {
            const mlir::Value named = mlir::isa<LoadUpvalueOp>(op)
                                          ? mlir::cast<LoadUpvalueOp>(op).getClosure()
                                          : mlir::cast<StoreUpvalueOp>(op).getClosure();
            if (named != function.getBody().front().getArgument(arg_callee)) {
                supported = false;
                why = "an upvalue operation names a closure other than this frame's own - "
                      "ct_aot_upvalue_cell reads the frame, so it would read the wrong one";
            }
            return;
        }
        if (mlir::isa<LoadGlobalOp>(op) && !runtime_defines("ct_aot_global_get")) {
            supported = false;
            why = "ct_aot_global_get is declared in aot.hpp and defined nowhere - emitting a call "
                  "to it compiles and fails at link";
            return;
        }
        if (mlir::isa<StoreGlobalOp>(op) && !runtime_defines("ct_aot_global_set")) {
            supported = false;
            why = "ct_aot_global_set is declared in aot.hpp and defined nowhere - emitting a call "
                  "to it compiles and fails at link";
            return;
        }
        if (mlir::isa<LoadGlobalOp, StoreGlobalOp>(op)) { return; }
        if (auto binary = mlir::dyn_cast<BinaryOp>(op)) {
            if (!is_valid_binary(binary.getKind())) {
                supported = false;
                why = "ctjs.binary was given a kind only the static family serves";
            }
            return;
        }
        if (auto binary = mlir::dyn_cast<BinaryStaticOp>(op)) {
            if (!is_valid_binary_static(binary.getKind())) {
                supported = false;
                why = "ctjs.binary_static was given a kind only the re-entering family serves";
            }
            return;
        }
        // THE BRANCHES, which is what makes a function with an `if` compilable.
        // Their block arguments are handled by a pass of their own afterwards -
        // see EmitCBlockArguments.cpp - because emitting them as they stand
        // would hit the copy the C++ emitter loses.
        if (mlir::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp>(op)) { return; }
        if (auto constant = mlir::dyn_cast<ConstantOp>(op)) {
            // THE FOUR THAT NEED NO ALLOCATION. A number is included because
            // its attribute carries the double's BIT PATTERN rather than a
            // float, so it has an exact C++ spelling - see constant_value. A
            // decimal literal would not: -0.0 and 0.0 are different JavaScript
            // values and print identically. A string is still refused; it
            // reaches ct_aot_new_string, which is a safepoint, and nothing
            // roots the result yet.
            if (mlir::isa<UndefinedAttr, NullAttr, BooleanAttr, NumberAttr, StringAttr, BigIntAttr>(
                    constant.getValue())) {
                return;
            }
            supported = false;
            why = "no lowering yet for this constant";
            return;
        }
        supported = false;
        why = ("no lowering yet for " + op->getName().getStringRef()).str();
    });
    return supported;
}

} // namespace ctcompile::ctjs::emitc_detail
