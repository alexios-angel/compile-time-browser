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
  // and `out` is an out-parameter, so neither is an operand. The shape is READ
  // from the ABI rather than counted, because counting is not enough - see
  // below. Materialising those arguments is the rest of this phase.
  // CHECK: ctjs.binary add
  %sum = ctjs.binary add %v, %inside

  // A VARIADIC OPERATION AGAINST A HELPER WITH A DIFFERENT SHAPE, which is the
  // case that made arity insufficient. ct_aot_call is
  // (fr, callee, receiver, argv, argc, key, site, out) - eight - and this call
  // site has five arguments plus callee and receiver, so frame + operands is
  // eight too. It matched by ACCIDENT and passed a value where the helper wants
  // an argv pointer and an argc; running the pass over p5.js emitted 17,848
  // such calls before the shape rule refused them.
  // CHECK: ctjs.call
  %called = ctjs.call %v(%inside, %v, %v, %v, %v, %v)

  // A HELPER THAT NEEDS THE FRAME PREPENDED, which is a different path from the
  // two above: ct_aot_new_object is (fr) and ctjs.create_object has NO operands,
  // so the frame is supplied by the lowering rather than by the operation. The
  // first version of this test used only helpers whose frame was already an
  // operand, and blinding the prepend left it green.
  // CHECK: func.call @ct_aot_new_object(%[[CTX:.*]]) : (!ctjs.context) -> !ctjs.value
  %obj = ctjs.create_object
  // CHECK: func.call @ct_aot_append(%[[CTX]], %{{.*}}, %{{.*}})
  ctjs.append %v to %obj

  // CHECK: func.call @ct_aot_leave(%{{.*}}) : (!ctjs.context) -> ()
  ctjs.frame_exit %ctx
  ctjs.return %sum
}
