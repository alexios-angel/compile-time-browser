// Boxed EmitC roots lowering.
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

// FRAME SLOT `index`, as an lvalue through a freshly fetched span.
static mlir::Value frame_slot(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                              unsigned index) {
    const mlir::Value span =
        opaque_call(build, where, ec::PointerType::get(scope.value), "ct_aot_slots", {scope.frame});
    return ec::SubscriptOp::create(build, where, ec::LValueType::get(scope.value), span,
                                   mlir::ValueRange{literal(
                                       build, where, mlir::IntegerType::get(build.getContext(), 32),
                                       std::to_string(index))})
        .getResult();
}

// ROOT ONE VALUE IN THE FRAME.
//
// THE SPAN IS RE-FETCHED EVERY TIME, and that is not laziness. The row is
// explicit: the pointer "IS VALID UNTIL THE NEXT SAFEPOINT AND NOT ONE
// INSTRUCTION LONGER", because ct_aot_enter and every nested call resize
// context::registers_ and may reallocate it. "A backend that hoists this
// call out of a loop containing a safepoint has miscompiled." One extra
// load per store is the price of not being that backend.
//
// STORING ONCE IS ENOUGH because the collector does not MOVE: it marks, and
// then deletes what it did not mark. A value reachable from a slot keeps
// its bits, so the C++ local holding a copy stays valid. Against a moving
// collector every use would have to reload instead.
void lowering::park(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                    mlir::Value rooted, unsigned slot) {
    ec::AssignOp::create(build, where, frame_slot(scope, build, where, slot), rooted);
}

// Park it if it is a JavaScript value this function tracks.
void lowering::park_if_tracked(compiled_entry & scope, mlir::OpBuilder & build,
                               mlir::Location where, mlir::Value original, mlir::Value emitted) {
    const auto found = scope.slots.find(original);
    if (found != scope.slots.end()) { park(scope, build, where, emitted, found->second); }
}

// THE CELL AN UPVALUE INDEX NAMES, then read or written.
//
// NEITHER CALL CAN FAIL. All three rows are (0, 0, 0) - no status, no
// exception edge, no safepoint - so this is two plain calls with nothing
// between them to test.
mlir::Value lowering::cell_of_upvalue(compiled_entry & scope, mlir::OpBuilder & build,
                                      mlir::Location where, std::uint32_t index, bool read,
                                      mlir::Value written) {
    const mlir::Value cell = opaque_call(build, where, scope.value, "ct_aot_upvalue_cell",
                                         {scope.frame, u32_literal(build, where, index)});
    if (!read) {
        opaque_call(build, where, {}, "ct_aot_cell_set", {cell, written});
        return mlir::Value{};
    }
    return opaque_call(build, where, scope.value, "ct_aot_cell_get", {cell});
}

// A POINTER TO A RESERVED RUN OF FRAME SLOTS.
//
// Two helpers want one - ct_aot_call's argv and ct_aot_make_closure's
// upvalue array - and both want it in the FRAME rather than in C++ locals,
// because both are safepoints that can collect before they read it.
mlir::Value lowering::window_pointer(compiled_entry & scope, mlir::OpBuilder & build,
                                     mlir::Location where, unsigned base) {
    return ec::AddressOfOp::create(build, where, ec::PointerType::get(scope.value),
                                   frame_slot(scope, build, where, base))
        .getResult();
}

// PARK A RUN OF VALUES in the window reserved for `op`; answers its base.
unsigned lowering::park_window(compiled_entry & scope, mlir::OpBuilder & build,
                               mlir::Location where, mlir::IRMapping & mapping,
                               mlir::Operation & op) {
    const unsigned base = scope.argument_windows.lookup(&op);
    const mlir::ValueRange values = *window_operands(op);
    for (auto [index, each] : llvm::enumerate(values)) {
        park(scope, build, where, mapping.lookup(each), base + static_cast<unsigned>(index));
    }
    return base;
}

} // namespace ctcompile::ctjs::emitc_detail
