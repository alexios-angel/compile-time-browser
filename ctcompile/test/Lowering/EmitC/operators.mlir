// Unary and relational operators, where the ABI stops speaking in JavaScript
// values.
//
// Most ABI helpers return a `uint64_t` that is a JavaScript value. These do not:
// ct_aot_strict_equals returns a uint32_t 0 or 1, ct_aot_compare an int32_t
// ordering, ct_aot_to_number a double. The CTJS operation's result is a
// !ctjs.value, so each has to be boxed, and the ABI has no helper that boxes
// one: there is no ct_aot_from_bool and no ct_aot_from_double.
//
// So the backend emits two inline shims, ctc_box_bool and ctc_box_number, into
// its own translation unit. They are generated code, not engine code: nothing
// links against them, no other backend has to agree about them, and no runtime
// ABI row was added for them. In C++ the boxing is
// `value::boolean(b).bits()`, a member call on a temporary, which
// emitc.call_opaque cannot spell because it only emits `callee(args)`.
//
// The ordering is the trap in this file. ct_aot_compare answers less,
// equivalent, greater or unordered, and unordered (a NaN on either side, and
// only that) makes all four relational operators false, `>=` included. So they
// cannot be lowered as negations of one another: `a >= b` as `!(a < b)` would
// make `NaN >= NaN` true. Each is built from equality tests against the
// orderings that make it true, so unordered falls through every one of them.
//
// Unlike the status enum's, the ordering's numbers are contractual (aot.hpp
// says so, because the relational opcodes are derived from constant
// comparisons against -1, 0, 1 and 2). They are still spelled as enumerators
// here, because a name the C++ compiler checks costs nothing.

// RUN: ctjs-opt %s --ctjs-lower-to-emitc --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top \
// RUN:   | FileCheck %s

// RUN: ctjs-opt %s --ctjs-lower-to-emitc --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp \
// RUN:   && %cxx %t.cpp

// The shims, emitted once per module and after the includes they need.
// CHECK: #include <ctbrowser/script/value.hpp>
// CHECK: inline uint64_t ctc_box_bool(bool
// CHECK: inline uint64_t ctc_box_number(double
// There is no ctc_undefined shim: a shim exists only to box an SSA value, and
// `undefined` has no operand, so it is spelled inline - member call and all.
// CHECK-NOT: ctc_undefined

// --- `a >= b` ---------------------------------------------------------------
//
// Two orderings make it true and unordered makes it false, which is the whole
// reason this is not `!(a < b)`.
// CHECK-LABEL: int32_t ge(
// CHECK: ct_aot_compare(
// CHECK: [[G:v[0-9]+]] = {{v[0-9]+}} == static_cast<int32_t>(ctbrowser::aot::ct_aot_ordering::greater);
// CHECK: [[E:v[0-9]+]] = {{v[0-9]+}} == static_cast<int32_t>(ctbrowser::aot::ct_aot_ordering::equivalent);
// CHECK: [[OR:v[0-9]+]] = [[G]] || [[E]];
// CHECK: ctc_box_bool([[OR]]);
ctjs.func @ge(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value,
              %a: !ctjs.value, %b: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 2
  %r = ctjs.compare ge %a, %b
  ctjs.frame_exit %ctx
  ctjs.return %r
}

// --- `a === b` --------------------------------------------------------------
//
// No status and no exception edge. Strict equality's row is (0, 0, 0) and it
// takes no frame handle at all, so there is nothing to test and nothing to
// branch to: it answers with its boolean directly.
// CHECK-LABEL: int32_t strict(
// CHECK: [[S:v[0-9]+]] = ctbrowser::aot::ct_aot_strict_equals(
// CHECK-NEXT: [[T:v[0-9]+]] = [[S]] != 0;
// CHECK-NEXT: {{v[0-9]+}} = ctc_box_bool([[T]]);
ctjs.func @strict(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value,
                  %a: !ctjs.value, %b: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 2
  %r = ctjs.compare strict_eq %a, %b
  ctjs.frame_exit %ctx
  ctjs.return %r
}

// --- `!a` -------------------------------------------------------------------
//
// ToBoolean cannot fail: it inspects a tag, allocates nothing and has no
// valueOf path. So `!a` is one infallible call and a test against zero; the
// test is the negation, and nothing else is emitted for it.
// CHECK-LABEL: int32_t negate(
// CHECK: [[B:v[0-9]+]] = ctbrowser::aot::ct_aot_truthy(
// CHECK-NEXT: [[N:v[0-9]+]] = [[B]] == 0;
// CHECK-NEXT: {{v[0-9]+}} = ctc_box_bool([[N]]);
ctjs.func @negate(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value,
                  %a: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %r = ctjs.unary not %a
  ctjs.frame_exit %ctx
  ctjs.return %r
}

// --- `+a` -------------------------------------------------------------------
//
// ToNumber can fail, and its out-parameter is a `double *`, not a value, so
// the local the call writes through is a double and the result is boxed.
// CHECK-LABEL: int32_t plus(
// CHECK: double {{v[0-9]+}};
// CHECK: ctbrowser::aot::ct_aot_to_number(
// CHECK: [[D:v[0-9]+]] = {{v[0-9]+}};
// CHECK: ctc_box_number([[D]]);
ctjs.func @plus(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value,
                %a: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %r = ctjs.unary plus %a
  ctjs.frame_exit %ctx
  ctjs.return %r
}

// --- `void a` ---------------------------------------------------------------
//
// Reaches no helper at all. Its operand is already an SSA value, so it has
// already been evaluated; there is nothing to emit but the answer.
// CHECK-LABEL: int32_t discard(
// CHECK: ct_aot_return_value(ctbrowser::script::value::undefined().bits()
ctjs.func @discard(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value,
                   %a: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %r = ctjs.unary void %a
  ctjs.frame_exit %ctx
  ctjs.return %r
}
