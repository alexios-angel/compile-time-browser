//===- Status.cpp - EmitC lowering of status-returning helpers ------------===//
//
// EmitC lowering for runtime-helper calls that return a ct_aot_status, and the
// shared failure and catch paths those calls branch to. Each such call splits
// its block: the ok path continues in a fresh block, every other status goes
// to the handler or the function's epilogue.
//
//===----------------------------------------------------------------------===//
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

/// Returns the block a failed helper call branches to, built once per function
/// and shared.
///
/// The status arrives as a block argument. That is safe because
/// --emitc-eliminate-block-arguments runs after this pass and turns it into a
/// variable.
///
/// ct_aot_leave runs only on CT_AOT_FAILED. On CT_AOT_UNWOUND the unwinder has
/// already destroyed this frame (the helper contract says leave "must NOT run
/// on the CT_AOT_UNWOUND path"), so leaving again would pop another frame.
///
/// CT_AOT_CAUGHT never reaches this block. Inside a try region the caller
/// branches through caught_or_failure(), which tests for `caught` first and
/// jumps to the handler; only `unwound` and `failed` arrive here.
mlir::Block * lowering::failure_path(compiled_entry & scope, mlir::OpBuilder & build,
                                     mlir::Location where) {
    if (scope.propagate) { return scope.propagate; }
    mlir::OpBuilder::InsertionGuard keep(build);

    mlir::Block * propagate = scope.entry.addBlock();
    propagate->addArgument(scope.status, where);
    mlir::Block * leaving = scope.entry.addBlock();
    mlir::Block * finish = scope.entry.addBlock();

    build.setInsertionPointToEnd(propagate);
    const mlir::Value destroyed =
        status_is(build, where, scope.status, propagate->getArgument(0), "unwound");
    mlir::cf::CondBranchOp::create(build, where, destroyed, finish, mlir::ValueRange{}, leaving,
                                   mlir::ValueRange{});

    build.setInsertionPointToEnd(leaving);
    opaque_call(build, where, {}, "ct_aot_leave", {scope.frame});
    mlir::cf::BranchOp::create(build, where, finish);

    build.setInsertionPointToEnd(finish);
    ec::ReturnOp::create(build, where, propagate->getArgument(0));

    scope.propagate = propagate;
    return propagate;
}

/// Returns the block a failing status branches to, which is not always the
/// epilogue.
///
/// Outside a protected region it is failure_path(). Inside one, CT_AOT_CAUGHT
/// means a handler in this frame took the throw and execution continues at the
/// landing pad, so this emits a block that tests for `caught` first and
/// branches to the handler with the register file as of the throw, falling
/// through to the epilogue for `unwound` and `failed`.
///
/// The block is built per call site rather than shared, because it needs the
/// status twice: once to compare and once to pass on.
mlir::Block * lowering::caught_or_failure(compiled_entry & scope, mlir::OpBuilder & build,
                                          mlir::Location where) {
    if (scope.caught_target == nullptr) { return failure_path(scope, build, where); }
    mlir::Block * sorted = scope.entry.addBlock();
    sorted->addArgument(scope.status, where);
    mlir::OpBuilder::InsertionGuard keep{build};
    build.setInsertionPointToEnd(sorted);
    const mlir::Value landed =
        status_is(build, where, scope.status, sorted->getArgument(0), "caught");
    mlir::cf::CondBranchOp::create(build, where, landed, scope.caught_target, scope.caught_operands,
                                   failure_path(scope, build, where),
                                   mlir::ValueRange{sorted->getArgument(0)});
    return sorted;
}

/// Creates the block that continues after the current insertion block, placed
/// directly after it rather than at the end of the function.
///
/// Block order is emission order for mlir-translate, and an `emitc.literal` is
/// printed inline at each use rather than declared at the top. If CSE moved a
/// literal's definition into a continuation appended at the end, a use in an
/// earlier block would print before its definition and name an undeclared
/// variable. Keeping the continuation adjacent keeps every definition ahead of
/// its uses in the text.
mlir::Block * continuation_after(compiled_entry & scope, mlir::OpBuilder & build) {
    mlir::Region & body = scope.entry.getBody();
    mlir::Block * current = build.getInsertionBlock();
    if (current == nullptr || current->getParent() != &body) { return scope.entry.addBlock(); }
    auto * fresh = new mlir::Block();
    body.getBlocks().insertAfter(current->getIterator(), fresh);
    return fresh;
}

/// Emits a call to a helper that returns a status and writes its result through
/// a pointer, and returns the loaded result.
///
/// Most of the ABI has this shape: declare a local for the result, take its
/// address, call, test the status against ct_aot_status::ok by name, and
/// continue in a fresh block where the result is loaded. The builder is left
/// pointing at that continuation, because everything after the call in the
/// source block belongs there.
///
/// `seed` is the out-slot's starting value. Only ct_aot_module_export_cell uses
/// it: outside a module that helper writes nothing, so seeding the slot with the
/// register's current value keeps the register unchanged without needing a
/// "no write" status (see ctjs.module_export_cell in Modules.td).
///
/// The seed is written with emitc.assign rather than as a VariableOp
/// initialiser, because `emitc.variable` takes an attribute, not an SSA value,
/// and --declare-variables-at-top hoists the declaration away from this point.
mlir::Value lowering::status_call(compiled_entry & scope, mlir::OpBuilder & build,
                                  mlir::Location where, const std::string & symbol,
                                  llvm::ArrayRef<mlir::Value> arguments, mlir::Type produces,
                                  mlir::Value seed) {
    // The out-parameter is not always a boxed value, which is why this takes a
    // type: ct_aot_binary_op writes a `uint64_t *`, ct_aot_loose_equals a
    // `uint32_t *` boolean and ct_aot_compare an `int32_t *` ordering. The
    // pointee must match the helper's signature, or the emitted call is a type
    // error at best and a reinterpreted write at worst.
    auto slot = ec::VariableOp::create(build, where, ec::LValueType::get(produces),
                                       ec::OpaqueAttr::get(build.getContext(), ""));
    if (seed != nullptr) { ec::AssignOp::create(build, where, slot.getResult(), seed); }
    auto address =
        ec::AddressOfOp::create(build, where, ec::PointerType::get(produces), slot.getResult());

    llvm::SmallVector<mlir::Value> passed(arguments.begin(), arguments.end());
    passed.push_back(address.getResult());
    status_call_void(scope, build, where, symbol, passed);

    // Loaded only on the ok path, which the ABI requires: "*out written ONLY
    // on CT_AOT_OK", so on any other status the local still holds whatever it
    // held before the call.
    return ec::LoadOp::create(build, where, produces, slot.getResult()).getResult();
}

/// Emits a call to a helper that returns only a status (ct_aot_set_index, for
/// example), branches on it like status_call(), and leaves the builder in the
/// continuation. No out-slot is declared, since the helper writes no result.
void lowering::status_call_void(compiled_entry & scope, mlir::OpBuilder & build,
                                mlir::Location where, const std::string & symbol,
                                llvm::ArrayRef<mlir::Value> arguments) {
    auto answered =
        ec::CallOpaqueOp::create(build, where, mlir::TypeRange{scope.status}, symbol, arguments);
    const mlir::Value survived = status_is(build, where, scope.status, answered.getResult(0), "ok");
    mlir::Block * carry_on = continuation_after(scope, build);
    mlir::cf::CondBranchOp::create(build, where, survived, carry_on, mlir::ValueRange{},
                                   caught_or_failure(scope, build, where),
                                   mlir::ValueRange{answered.getResult(0)});
    build.setInsertionPointToEnd(carry_on);
}

} // namespace ctcompile::ctjs::emitc_detail
