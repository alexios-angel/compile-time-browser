// A bitwise operator can run user code, so it lowers to ctjs.binary and ct_aot_binary_op.
//
// The VM's six bitwise handlers ToNumeric both operands, left first: an
// object's valueOf runs and a Symbol is a TypeError. context::binary_op runs
// them the same way; ct_aot_binary_op_static converts nothing, so a body
// compiled through it answers 0 for `{valueOf: () => 5} & 7` where the
// interpreter answers 5, and carries no exception edge for a valueOf that
// throws.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | FileCheck %s --check-prefix=IMPORT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top \
// RUN:   | FileCheck %s

function mask(o) { return o & 7; }

// IMPORT-LABEL: ctjs.func @mask$
// IMPORT: ctjs.binary bitand
// IMPORT-NOT: ctjs.binary_static

// CHECK: extern "C" int32_t mask_1(
// CHECK: ctbrowser::aot::ct_aot_binary_op({{v[0-9]+}}, static_cast<uint32_t>(ctbrowser::script::op::bit_and), {{.*}});
// CHECK-NOT: ct_aot_binary_op_static
