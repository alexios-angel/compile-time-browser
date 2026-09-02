// THE FIRST PDLL PATTERN OVER OUR OWN DIALECT, AS IR.
//
// `+x` on a value admission has proved a number is `x`, and that rule lives in
// lib/CTNative/Lowering/UnaryPlusIsIdentity.pdll rather than in replace()'s
// switch. What is asserted here is the pair of things a declarative rule can
// get wrong and a build cannot:
//
//   IT FIRES. `ctjs.unary plus` is gone and its operand flows straight into
//   the addition - pinned with CHECK-NEXT, so there is nothing between the
//   two constants and the add for it to have become. PDL reports nothing on a
//   non-match, so if the pattern stopped matching the pass would abort in
//   replace()'s Plus arm rather than miscompile; a test that only proved it
//   does not crash would prove nothing about the rewrite.
//
//   IT DISCRIMINATES. `ctjs.unary neg` in the second function is UNTOUCHED.
//   That is the assertion the whole native-constraint argument rests on: the
//   PDLL way of writing this test - `op<ctjs.unary> {kind =
//   attr<"#ctjs.unary_kind<plus>">}` - compiles with exit 0 and DROPS the kind
//   constraint, and a pattern that had done so would replace `-2` with `2` and
//   emit no `unary_minus` at all. If this file ever stops showing
//   `unary_minus`, the constraint has gone silent.
//
// The importer has no CTJS operation for op::to_number, so `+x` never reaches
// this pass from real JavaScript yet - test/linkable.js says so at the other
// end of the pipeline. Hand-written IR is the only way to exercise it, exactly
// as test/Lowering/EmitC/operators.mlir does for the boxed tier.
//
// THE `$0` AND `$1` ARE NOT DECORATION. cIdentifier() maps `$` to `_`, and
// without the suffix the emitted `emitc.func` takes the SAME symbol name as
// the `ctjs.func` it replaces - whereupon finish()'s "still has symbol uses"
// invariant fires on the emitc.declare_func prototype. The importer names
// every function this way; a hand-written module has to as well.

// RUN: ctjs-opt %s --ctnative-lower-to-emitc | FileCheck %s

// --- `+2 + 3` ---------------------------------------------------------------
//
// CHECK-LABEL: emitc.func @plus_is_identity_0
// CHECK-NEXT: %[[TWO:.*]] = "emitc.constant"() <{value = 2.000000e+00 : f64}>
// CHECK-NEXT: %[[THREE:.*]] = "emitc.constant"() <{value = 3.000000e+00 : f64}>
// CHECK-NEXT: %[[SUM:.*]] = add %[[TWO]], %[[THREE]]
// CHECK-NEXT: return %[[SUM]]
ctjs.func @plus_is_identity$0(%receiver: !ctjs.value, %new_target: !ctjs.value,
                              %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %two = ctjs.constant #ctjs.number<4611686018427387904>
  %three = ctjs.constant #ctjs.number<4613937818241073152>
  %plus = ctjs.unary plus %two
  %sum = ctjs.binary add %plus, %three
  ctjs.frame_exit %ctx
  ctjs.return %sum
}

// --- `-2`, WHICH THE SAME PATTERN MUST NOT TOUCH ----------------------------
//
// CHECK-LABEL: emitc.func @neg_is_not_identity_1
// CHECK-NEXT: %[[TWO:.*]] = "emitc.constant"() <{value = 2.000000e+00 : f64}>
// CHECK-NEXT: %[[NEG:.*]] = unary_minus %[[TWO]]
// CHECK-NEXT: return %[[NEG]]
ctjs.func @neg_is_not_identity$1(%receiver: !ctjs.value, %new_target: !ctjs.value,
                                 %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %two = ctjs.constant #ctjs.number<4611686018427387904>
  %neg = ctjs.unary neg %two
  ctjs.frame_exit %ctx
  ctjs.return %neg
}

// AND NEITHER FUNCTION WAS REFUSED. A diagnostic would leave a ctjs.func
// behind, and the CHECK-LABELs above would be matching text that is not there.
// CHECK-NOT: ctnative.not_native
// CHECK-NOT: ctjs.func
