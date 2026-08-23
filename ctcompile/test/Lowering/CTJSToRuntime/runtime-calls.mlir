// ONE PATTERN, AND IT MATCHES ON THE INTERFACE.
//
// Phase 10's acceptance criterion is not that the output looks right - it is
// that the pass never names an operation. "A pass that switches on operation
// names must be revisited every time an operation is added. A pass that queries
// a trait never is." Nothing in CTJSToRuntime.cpp mentions frame_exit, cell_get
// or any other operation by name; they lower because they implement
// CTJS_RuntimeCallOpInterface.

// RUN: ctjs-opt %s --ctjs-lower-to-runtime | FileCheck %s

// THE DECLARATIONS, in the order the pass inserts them - each at the top of the
// module as it is first needed, so the last one needed is printed first.
// CHECK: func.func private @ct_aot_cell_get
// CHECK: func.func private @ct_aot_leave

// CHECK-LABEL: ctjs.func @lowers
ctjs.func @lowers(%v: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter

  // A HELPER WHOSE PARAMETERS ARE THE OPERATION'S OPERANDS lowers to a direct
  // call, with the frame handle first because that is the ABI's order.
  // CHECK: func.call @ct_aot_cell_get(%{{.*}}) : (!ctjs.value) -> !ctjs.value
  %inside = ctjs.cell_get %v

  // AND ONE WHOSE PARAMETERS ARE NOT lowers to nothing yet, on purpose.
  // ct_aot_binary_op is (fr, op_kind, lhs, rhs, out): the kind is an ATTRIBUTE
  // and `out` is an out-parameter, so neither is an operand and the arity check
  // refuses the match rather than emitting a call with a garbage argument.
  // Materialising those is the rest of this phase.
  // CHECK: ctjs.binary add
  %sum = ctjs.binary add %v, %inside

  // CHECK: func.call @ct_aot_leave(%{{.*}}) : (!ctjs.context) -> ()
  ctjs.frame_exit %ctx
  ctjs.return %sum
}
