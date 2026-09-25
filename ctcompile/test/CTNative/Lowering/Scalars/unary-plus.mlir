// THE UNARY-PLUS PATTERN, AS IR.
//
// `+x` on a value admission has proved a number is `x`, and that rule is the
// UnaryPlusIsIdentity pattern in lib/CTNative/Lowering/LoweringSupport.cpp
// rather than an arm of replace()'s switch. What is asserted here is the pair
// of things a pattern can get wrong and a build cannot:
//
//   IT FIRES. `ctjs.unary plus` is gone and its operand flows straight into
//   the addition - pinned with CHECK-NEXT, so there is nothing between the
//   Number constructions and the add for it to have become. A driver reports
//   nothing on a non-match, so if the pattern stopped matching the pass would abort in
//   replace()'s Plus arm rather than miscompile; a test that only proved it
//   does not crash would prove nothing about the rewrite.
//
//   IT DISCRIMINATES. `ctjs.unary neg` in the second function is UNTOUCHED.
//   A pattern that had lost its kind test would replace `-2` with `2` and
//   emit no `unary_minus` at all. If this file ever stops showing
//   `unary_minus`, the constraint has gone silent.
//
// The importer maps op::to_number to unary plus. string-coercions.test covers
// source String conversion; this IR fixture isolates the numeric identity rule.
//
// THE `$0` AND `$1` ARE NOT DECORATION. cIdentifier() maps `$` to `_`, and
// without the suffix the emitted `emitc.func` takes the SAME symbol name as
// the `ctjs.func` it replaces - whereupon finish()'s "still has symbol uses"
// invariant fires on the emitc.declare_func prototype. The importer names
// every function this way; a hand-written module has to as well.

// RUN: ctjs-opt %s --ctnative-lower-to-emitc=optimize=false | FileCheck %s
// Keep literal arithmetic for the lowering pattern under test; default
// precomputation can remove the entire expression before that pattern runs.

// --- `+2 + 3` ---------------------------------------------------------------
//
// CHECK-LABEL: emitc.func @plus_is_identity_0
// CHECK-NEXT: %[[TWO_RAW:.*]] = "emitc.constant"() <{value = 2.000000e+00 : f64}>
// CHECK-NEXT: %[[TWO:.*]] = cast %[[TWO_RAW]] : f64 to !emitc.opaque<"ctnative::js_num">
// CHECK-NEXT: %[[THREE_RAW:.*]] = "emitc.constant"() <{value = 3.000000e+00 : f64}>
// CHECK-NEXT: %[[THREE:.*]] = cast %[[THREE_RAW]] : f64 to !emitc.opaque<"ctnative::js_num">
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
// CHECK-NEXT: %[[TWO_RAW:.*]] = "emitc.constant"() <{value = 2.000000e+00 : f64}>
// CHECK-NEXT: %[[TWO:.*]] = cast %[[TWO_RAW]] : f64 to !emitc.opaque<"ctnative::js_num">
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
