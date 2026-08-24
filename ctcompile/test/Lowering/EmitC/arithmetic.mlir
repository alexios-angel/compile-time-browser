// A HELPER THAT CAN FAIL, AND THE EDGE THAT CARRIES ITS FAILURE.
//
// This is the shape most of the ABI has - a call, a result through an
// out-parameter, and a ct_aot_status the caller MUST test - so getting it right
// once is most of the remaining work. `a + b` is the smallest program that
// needs all of it.
//
// WHAT THE EDGE HAS TO KNOW, and neither part is guessable from the signature:
//
//   `*out` IS WRITTEN ONLY ON CT_AOT_OK. The row says so, so the load sits in
//   the block reached when the status matched and nowhere else. On any other
//   status the local still holds what it held before the call.
//
//   ct_aot_leave IS CONDITIONAL ON THE STATUS. On CT_AOT_UNWOUND the unwinder
//   has already truncated the frame stack and destroyed this frame - the row
//   says leave "must NOT run on the CT_AOT_UNWOUND path" - so calling it would
//   pop somebody else's frame. On CT_AOT_FAILED the frame is still standing and
//   must be left. A single unconditional leave on the failure path reads
//   perfectly well and corrupts the frame stack on one of the two.
//
// AND THE OPCODE IS AN ENUMERATOR, NEVER A NUMBER. `uint32_t op_kind` is a
// ctbrowser::script::op that aot_bridge.cpp casts straight back, and Phases 13
// and 14 renumber that enum deliberately. Emitted as a number this call keeps
// compiling and starts meaning a different operator; emitted as a name, the
// renumbering is a build error in the generated translation unit.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top \
// RUN:   | FileCheck %s

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp \
// RUN:   && %cxx %t.cpp

function add(a, b) { return a + b; }

// CHECK: extern "C" int32_t add_1(

// THE CALL. Source `+` is add_generic - the RE-ENTERING family, which runs
// ToPrimitive and can call a user valueOf. op::add is a different operator that
// the runtime reaches only from `++`, and compiling `a + b` into it would make
// `{valueOf: () => 3} + 1` answer NaN instead of 4.
// CHECK: [[OUT:v[0-9]+]] = &[[SLOT:v[0-9]+]];
// CHECK: [[ST:v[0-9]+]] = ctbrowser::aot::ct_aot_binary_op({{v[0-9]+}}, static_cast<uint32_t>(ctbrowser::script::op::add_generic), {{.*}}, [[OUT]]);

// THE STATUS, COMPARED BY NAME. aot.hpp: "THE PRECEDENCE IS THE CONTRACT; THE
// NUMBERS ARE NOT" - and `ok` is 3, so a test against zero would read every
// successful call as a failure.
// CHECK-NEXT: {{v[0-9]+}} = [[ST]] == static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok);

// THE RESULT IS LOADED ON THE SURVIVING PATH, after the branch.
// CHECK: {{v[0-9]+}} = [[SLOT]];
// CHECK: ct_aot_return_value(

// AND THE FAILURE PATH TESTS FOR unwound BEFORE LEAVING, because on that status
// there is no frame left to leave.
// CHECK: [[GONE:v[0-9]+]] = {{v[0-9]+}} == static_cast<int32_t>(ctbrowser::aot::ct_aot_status::unwound);
// CHECK: if ([[GONE]]) {
// CHECK: ctbrowser::aot::ct_aot_leave(
