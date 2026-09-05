// Boxed EmitC status lowering.
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

// ONE OPERATION.
//
// NO DEFAULT ARM AND NO FALLBACK. body_is_supported has already refused
// anything not listed here, so an operation reaching the end is a
// disagreement between the two - which is a bug in this file rather than in
// its input, and is worth crashing over rather than emitting a call to
// something plausible.
// WHERE A FAILED HELPER GOES, built once per function and shared.
//
// THE STATUS ARRIVES AS A BLOCK ARGUMENT, which is safe precisely because
// --emitc-eliminate-block-arguments runs after this pass and turns it into
// a variable. Every status test in the body branches here with its own
// status; writing that variable by hand would be the same thing done worse.
//
// ct_aot_leave IS CONDITIONAL, and that is the part that is easy to get
// wrong. On CT_AOT_UNWOUND the unwinder has already truncated the frame
// stack and destroyed this frame - the row says leave "must NOT run on the
// CT_AOT_UNWOUND path" - so calling it again would pop somebody else's
// frame. On CT_AOT_FAILED the frame is still standing and must be left.
//
// CT_AOT_CAUGHT CANNOT REACH HERE, and that is a fact about the input
// rather than an assumption: `caught` is reported only when a handler in
// THIS frame won, and nothing in the allow-list pushes one. When try/catch
// arrives this block needs a third arm - a `caught` escaping the entry is a
// SILENT wrong answer, because the caller writes no result and reports
// success, so the program sees `undefined` with no error at all.
mlir::Block * lowering::failure_path(compiled_entry & scope, mlir::OpBuilder & build,
                                     mlir::Location where) {
    if (scope.propagate) { return scope.propagate; }
    mlir::OpBuilder::InsertionGuard keep(build);

    mlir::Block * propagate = scope.entry.addBlock();
    propagate->addArgument(scope.status, where);
    mlir::Block * leaving = scope.entry.addBlock();
    mlir::Block * finish = scope.entry.addBlock();

    build.setInsertionPointToEnd(propagate);
    const mlir::Value gone = literal(
        build, where, scope.status, "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::unwound)");
    auto destroyed = ec::CmpOp::create(build, where, mlir::IntegerType::get(build.getContext(), 1),
                                       ec::CmpPredicate::eq, propagate->getArgument(0), gone);
    mlir::cf::CondBranchOp::create(build, where, destroyed, finish, mlir::ValueRange{}, leaving,
                                   mlir::ValueRange{});

    build.setInsertionPointToEnd(leaving);
    ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_leave"),
                             mlir::ValueRange{scope.frame});
    mlir::cf::BranchOp::create(build, where, finish);

    build.setInsertionPointToEnd(finish);
    ec::ReturnOp::create(build, where, propagate->getArgument(0));

    scope.propagate = propagate;
    return propagate;
}

// WHERE A FAILING STATUS GOES, WHICH IS NOT ALWAYS OUT.
//
// Outside a protected region it is the shared epilogue, unchanged. INSIDE
// one, CT_AOT_CAUGHT means a handler in THIS frame took the throw and
// execution continues at the pad rather than returning - so this emits a
// block that tests for caught first and branches to the handler with the
// register file as of the throw, falling through to the epilogue for
// UNWOUND and FAILED.
//
// PER CALL SITE rather than shared, because the epilogue takes the status
// as a block argument and this needs it twice: once to compare and once to
// hand on.
mlir::Block * lowering::caught_or_failure(compiled_entry & scope, mlir::OpBuilder & build,
                                          mlir::Location where) {
    if (scope.caught_target == nullptr) { return failure_path(scope, build, where); }
    mlir::Block * sorted = scope.entry.addBlock();
    sorted->addArgument(scope.status, where);
    mlir::OpBuilder::InsertionGuard keep{build};
    build.setInsertionPointToEnd(sorted);
    const mlir::Value caught = literal(
        build, where, scope.status, "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::caught)");
    auto landed = ec::CmpOp::create(build, where, mlir::IntegerType::get(build.getContext(), 1),
                                    ec::CmpPredicate::eq, sorted->getArgument(0), caught);
    mlir::cf::CondBranchOp::create(build, where, landed, scope.caught_target, scope.caught_operands,
                                   failure_path(scope, build, where),
                                   mlir::ValueRange{sorted->getArgument(0)});
    return sorted;
}

// A HELPER THAT ANSWERS WITH A STATUS AND A VALUE THROUGH A POINTER.
//
// Most of the ABI has this shape, so it is written once: declare a local
// for the result, take its address, call, test the status against
// ct_aot_status::ok BY NAME, and continue in a fresh block where the result
// is loaded.
//
// THE BLOCK SPLITS AND THE CALLER KEEPS EMITTING INTO THE NEW ONE. An
// operation with an exception edge is two blocks, and everything after it
// in the source block belongs to the second - which is why this leaves the
// builder pointing at the continuation rather than restoring it.
//
// `seed` IS THE OUT-SLOT'S STARTING VALUE, and exactly one row needs it.
// ct_aot_module_export_cell's write is CONDITIONAL - outside a module the
// interpreter leaves the destination register alone, and the register holds
// the local being exported - and the row asked for a CT_AOT_NO_WRITE status
// to say so. `ct_aot_status` has four members and none of them is that one,
// and adding a fifth would not be one enumerator: every test below compares
// against `ok` and branches to the shared failure path, so a compiled body
// would RETURN on the ordinary no-module path. Seeding the slot says the
// same thing without a new status - the helper hands the seed straight back
// and the register ends up holding what it held.
//
// AN emitc.assign RATHER THAN A VariableOp INITIALISER, because
// `emitc.variable` takes an ATTRIBUTE and this is an SSA value - and
// because --declare-variables-at-top hoists the declaration, so an
// initialiser would be evaluated in the wrong place even if it fitted.
mlir::Value lowering::status_call(compiled_entry & scope, mlir::OpBuilder & build,
                                  mlir::Location where, const std::string & symbol,
                                  llvm::ArrayRef<mlir::Value> arguments, mlir::Type produces,
                                  mlir::Value seed) {
    // THE OUT-PARAMETER IS NOT ALWAYS A VALUE, which is why this takes a
    // type. ct_aot_binary_op writes a `uint64_t *`, ct_aot_loose_equals a
    // `uint32_t *` boolean and ct_aot_compare an `int32_t *` ORDERING. The
    // pointer's pointee has to match the row or the emitted call is a type
    // error at best and a reinterpreted write at worst.
    auto slot = ec::VariableOp::create(build, where, ec::LValueType::get(produces),
                                       ec::OpaqueAttr::get(build.getContext(), ""));
    if (seed != nullptr) { ec::AssignOp::create(build, where, slot.getResult(), seed); }
    auto address =
        ec::AddressOfOp::create(build, where, ec::PointerType::get(produces), slot.getResult());

    llvm::SmallVector<mlir::Value> passed(arguments.begin(), arguments.end());
    passed.push_back(address.getResult());
    auto answered =
        ec::CallOpaqueOp::create(build, where, mlir::TypeRange{scope.status}, symbol, passed);

    const mlir::Value ok = literal(build, where, scope.status,
                                   "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok)");
    auto survived = ec::CmpOp::create(build, where, mlir::IntegerType::get(build.getContext(), 1),
                                      ec::CmpPredicate::eq, answered.getResult(0), ok);

    mlir::Block * carry_on = scope.entry.addBlock();
    mlir::cf::CondBranchOp::create(build, where, survived, carry_on, mlir::ValueRange{},
                                   caught_or_failure(scope, build, where),
                                   mlir::ValueRange{answered.getResult(0)});

    // LOADED ONLY ON THE OK PATH, which the ABI requires rather than merely
    // permits: "*out written ONLY on CT_AOT_OK", so on any other status the
    // local still holds whatever it held before the call.
    build.setInsertionPointToEnd(carry_on);
    return ec::LoadOp::create(build, where, produces, slot.getResult()).getResult();
}

// A HELPER THAT ANSWERS WITH A STATUS AND NOTHING ELSE.
//
// ct_aot_set_index is the first: the bytecode performs the write and
// evaluates the expression separately, so there is a status to test and no
// result to load. The edge is the same one; only the out-parameter is
// missing, and inventing a slot for a value the helper never writes would
// be a local read before it was ever assigned.
void lowering::status_call_void(compiled_entry & scope, mlir::OpBuilder & build,
                                mlir::Location where, const std::string & symbol,
                                llvm::ArrayRef<mlir::Value> arguments) {
    auto answered =
        ec::CallOpaqueOp::create(build, where, mlir::TypeRange{scope.status}, symbol, arguments);
    const mlir::Value ok = literal(build, where, scope.status,
                                   "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok)");
    auto survived = ec::CmpOp::create(build, where, mlir::IntegerType::get(build.getContext(), 1),
                                      ec::CmpPredicate::eq, answered.getResult(0), ok);
    mlir::Block * carry_on = scope.entry.addBlock();
    mlir::cf::CondBranchOp::create(build, where, survived, carry_on, mlir::ValueRange{},
                                   caught_or_failure(scope, build, where),
                                   mlir::ValueRange{answered.getResult(0)});
    build.setInsertionPointToEnd(carry_on);
}

} // namespace ctcompile::ctjs::emitc_detail
