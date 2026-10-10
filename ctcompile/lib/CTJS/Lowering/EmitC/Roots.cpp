//===- Roots.cpp - Cells and upvalue lowering -----------------------------===//
//
// GC rooting for the VM-backed EmitC backend: parking values in frame slots,
// argument windows of reserved slots, and reading or writing an upvalue's
// cell.
//
//===----------------------------------------------------------------------===//
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

/// Frame slot `index`, as an lvalue through a freshly fetched span.
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

/// Roots one value in the frame by storing it in slot `slot`.
///
/// The span is re-fetched every time. ct_aot_slots' ABI entry says the
/// pointer is valid only until the next safepoint, because ct_aot_enter and
/// every nested call resize context::registers_ and may reallocate it, so
/// hoisting the call out of a loop containing a safepoint would miscompile.
///
/// Storing once is enough because the collector does not move objects: it
/// marks, then deletes what it did not mark. A value reachable from a slot
/// keeps its bits, so the C++ local holding a copy stays valid. A moving
/// collector would need every use to reload instead.
void lowering::park(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                    mlir::Value rooted, unsigned slot) {
    ec::AssignOp::create(build, where, frame_slot(scope, build, where, slot), rooted);
}

/// Parks `emitted` in `original`'s slot, if `original` is a JavaScript value
/// this function tracks.
void lowering::park_if_tracked(compiled_entry & scope, mlir::OpBuilder & build,
                               mlir::Location where, mlir::Value original, mlir::Value emitted) {
    const auto found = scope.slots.find(original);
    if (found != scope.slots.end()) { park(scope, build, where, emitted, found->second); }
}

/// Fetches the cell upvalue `index` names, then reads it (returning the value)
/// or writes `written` into it (returning null).
///
/// Neither call can fail: the helper ABI entries
/// (ctbrowser/include/ctbrowser/aot/helpers/*.def) declare ct_aot_upvalue_cell,
/// ct_aot_cell_get and ct_aot_cell_set with no status return and with
/// may_throw, may_reenter and is_safepoint all 0, so there is nothing to test
/// between them.
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

/// A pointer to a reserved run of frame slots starting at `base`.
///
/// ct_aot_call's argv and ct_aot_make_closure's upvalue array both need one,
/// in the frame rather than in C++ locals, because both helpers are
/// safepoints that can collect before they read it.
mlir::Value lowering::window_pointer(compiled_entry & scope, mlir::OpBuilder & build,
                                     mlir::Location where, unsigned base) {
    return ec::AddressOfOp::create(build, where, ec::PointerType::get(scope.value),
                                   frame_slot(scope, build, where, base))
        .getResult();
}

/// Parks `op`'s window operands in the window reserved for it and returns the
/// window's base slot.
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
