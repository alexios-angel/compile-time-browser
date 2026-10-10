// Lowering a helper call that can fail, and the edge that carries the failure.
//
// Most of the ABI has this shape: a call, a result through an out-parameter,
// and a ct_aot_status the caller must test. `a + b` is the smallest program
// that needs all of it.
//
// Two facts about the failure edge cannot be read from the signature:
//
//   `*out` is written only on CT_AOT_OK (the helper's row says so), so the
//   load sits in the block reached when the status matched and nowhere else.
//   On any other status the local still holds what it held before the call.
//
//   ct_aot_leave is conditional on the status. On CT_AOT_UNWOUND the unwinder
//   has already truncated the frame stack and destroyed this frame (the row
//   says leave "must NOT run on the CT_AOT_UNWOUND path"), so calling it would
//   pop somebody else's frame. On CT_AOT_FAILED the frame is still standing and
//   must be left. A single unconditional leave on the failure path looks right
//   and corrupts the frame stack on one of the two.
//
// The opcode is emitted as an enumerator, never a number. `uint32_t op_kind`
// is a ctbrowser::script::op that aot_bridge/operators.cpp casts straight back,
// and that enum may be renumbered. Emitted as a number, this call would keep
// compiling and silently mean a different operator; emitted as a name, a
// renumbering is a build error in the generated translation unit.
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

// The call. Source `+` is add_generic, the re-entering family, which runs
// ToPrimitive and can call a user valueOf. op::add is a different operator that
// the runtime reaches only from `++`, and compiling `a + b` into it would make
// `{valueOf: () => 3} + 1` answer NaN instead of 4.
// CHECK: [[OUT:v[0-9]+]] = &[[SLOT:v[0-9]+]];
// CHECK: [[ST:v[0-9]+]] = ctbrowser::aot::ct_aot_binary_op({{v[0-9]+}}, static_cast<uint32_t>(ctbrowser::script::op::add_generic), {{.*}}, [[OUT]]);

// The status is compared by name. aot.hpp states that the precedence of the
// statuses is the contract and their numbers are not; `ok` is 3, so a test
// against zero would read every successful call as a failure.
// CHECK-NEXT: {{v[0-9]+}} = [[ST]] == static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok);

// The result is loaded on the surviving path, after the branch.
// CHECK: {{v[0-9]+}} = [[SLOT]];
// CHECK: ct_aot_return_value(

// The failure path tests for `unwound` before leaving, because on that status
// there is no frame left to leave.
// CHECK: [[GONE:v[0-9]+]] = {{v[0-9]+}} == static_cast<int32_t>(ctbrowser::aot::ct_aot_status::unwound);
// CHECK: if ([[GONE]]) {
// CHECK: ctbrowser::aot::ct_aot_leave(
