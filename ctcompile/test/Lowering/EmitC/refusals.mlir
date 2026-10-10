// What the boxed EmitC backend refuses, and how it reports each refusal.
//
// Half-lowering a function the backend was never taught about does not fail:
// it produces a translation unit that compiles and computes something else.
// So --ctjs-lower-to-emitc works from an allow-list, and every function it
// declines is left intact with a `ctjs.not_lowered` attribute saying why; the
// set of those attributes is the backend's work list, as the importer's
// leftovers are the importer's.
//
// The refusals are the test. An allow-list rots quietly: widen it by accident
// and nothing goes red, because the wrong answer still compiles. Each case
// below names the reason it is refused, so a change that starts lowering one
// of them has to update this file. Some cases that used to be refusals now
// check that the construct is lowered.

// RUN: ctjs-opt %s --ctjs-lower-to-emitc | FileCheck %s

// The pass order, the one mistake that is otherwise silent.
//
// --emitc-eliminate-block-arguments walks emitc.func and nothing else, so run
// before the lowering it finds nothing, returns success and says nothing, and
// the lowering then emits block arguments that reach mlir-translate, which
// loses a copy on their edges. Measured on a self-loop from real JavaScript:
// reversed, `sl(10,20,2)` answers 20 where 10 is correct, with ctjs-opt,
// mlir-translate and g++ all exiting 0. A leftover ctjs.func indicates exactly
// that mistake and nothing else, so the elimination pass refuses one.
// RUN: not ctjs-opt %s --emitc-eliminate-block-arguments 2>&1 \
// RUN:   | FileCheck --check-prefix=ORDER %s
// ORDER: ran before the module was lowered

// --- new.target is lowered ---------------------------------------------------
//
// The importer gives every function three implicit arguments (receiver,
// new.target, callee) because the bytecode reads them with their own opcodes.
// Only `receiver` is in the entry signature; the other two are fetched after
// ct_aot_enter, from the frame it pushed.
//
// Passing `undefined` for new.target instead would be easy and wrong. The
// value comes from ct_aot_new_target: ct_aot_enter takes new.target from
// context::pending_new_target_, which context::construct_new sets (that is
// what its save/set/restore is for), so a compiled function called as a
// constructor sees the real new.target and a plain call sees `undefined`.
//
// This matters more than one opcode suggests: Babel's _classCallCheck guard is
// a new.target test, so a transpiled bundle has one in nearly every class,
// and a function that could not read it would make every such class refuse to
// compile.
//
// CHECK-NOT: ctjs.func @uses_new_target
// CHECK: emitc.func @uses_new_target
ctjs.func @uses_new_target(%receiver: !ctjs.value, %new_target: !ctjs.value,
                           %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  ctjs.frame_exit %ctx
  ctjs.return %new_target
}

// --- The callee is lowered ----------------------------------------------------
//
// ct_aot_callee is the only way a compiled function can reach its own
// upvalues: they live on the closure instance, while `site` is the
// function_proto that every closure over the same function shares. The entry
// ABI delivers `site` and not the closure, so the value comes out of
// call_frame::closure once ct_aot_enter has put one there.
//
// new.target is read from the frame in the same way (see above), though the
// two values reach the frame by different routes.
//
// CHECK-NOT: ctjs.func @uses_callee
// CHECK: emitc.func @uses_callee
ctjs.func @uses_callee(%receiver: !ctjs.value, %new_target: !ctjs.value,
                       %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  ctjs.frame_exit %ctx
  ctjs.return %callee
}

// --- An upvalue operation naming somebody else's closure ----------------------
//
// ct_aot_upvalue_cell reads the frame, so the operand is redundant with it:
// the bytecode's get_upvalue reads the current frame's closure and the importer
// only ever names the callee argument. An operation naming a different closure
// would be lowered into a read of the wrong one, silently, so it is refused.
//
// CHECK-LABEL: ctjs.func @foreign_upvalue
// CHECK-SAME: ctjs.not_lowered = "an upvalue operation names a closure other than
ctjs.func @foreign_upvalue(%receiver: !ctjs.value, %new_target: !ctjs.value,
                           %callee: !ctjs.value, %other: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 1 : i32} {
  %ctx = ctjs.frame_enter 1
  %captured = ctjs.load_upvalue %other[0]
  ctjs.frame_exit %ctx
  ctjs.return %captured
}

// --- Control flow is lowered --------------------------------------------------
//
// Emitting a block argument as it stands would hit the copy the C++ emitter
// loses, so block arguments are kept here and removed later by
// --emitc-eliminate-block-arguments, which runs after this pass. A function
// with branches lowers here with its block arguments intact and is cleaned up
// downstream. The pipeline order is the contract; end-to-end.mlir runs it and
// compiles the result.
// CHECK-NOT: ctjs.func @has_a_branch
// CHECK: emitc.func @has_a_branch
ctjs.func @has_a_branch(%receiver: !ctjs.value, %new_target: !ctjs.value,
                        %callee: !ctjs.value, %cond: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %bit = ctjs.truthy %cond
  cf.cond_br %bit, ^yes(%cond : !ctjs.value), ^no(%receiver : !ctjs.value)
^yes(%a: !ctjs.value):
  ctjs.frame_exit %ctx
  ctjs.return %a
^no(%b: !ctjs.value):
  ctjs.frame_exit %ctx
  ctjs.return %b
}

// --- Contains an operation with no lowering -----------------------------------
//
// The allow-list's default, and the reason names the operation so the work list
// reads itself. `ctjs.create_regexp` is not a CTJS_RuntimeOp at all
// (aot_helpers.def declares no helper for a regexp literal), so there is
// nothing to call even if a conversion were written.
//
// Keep this case pointed at an operation that is genuinely unsupported, or it
// asserts nothing. Operations that once filled this slot (property writes,
// `ctjs.has_property`) are lowered now; when `ctjs.create_regexp` gains a
// lowering, move the case to another operation that has none.
//
// CHECK-LABEL: ctjs.func @matcher
// CHECK-SAME: ctjs.not_lowered = "no lowering yet for ctjs.create_regexp"
ctjs.func @matcher(%receiver: !ctjs.value, %new_target: !ctjs.value,
                   %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %re = ctjs.create_regexp "a.c", "gi"
  ctjs.frame_exit %ctx
  ctjs.return %re
}

// --- A bitwise ctjs.binary is lowered -----------------------------------------
//
// context::binary_op runs the six bitwise operators as the interpreter does
// (ToNumeric of both operands, then the static operator), so OpcodeMapping.hpp
// maps every kind in the re-entering family and `shl` reaches op::shl by name.
// Admission.cpp keeps the kind check for any future kind a family does not
// handle. Without a mapping, `ctjs.binary shl` would pass op::halt, which is in
// range, so aot_bridge's bounds check passes and the call answers `undefined`
// with status ok.
//
// CHECK-NOT: ctjs.func @bitwise_generic
// CHECK: emitc.func @bitwise_generic
// CHECK: ctbrowser::script::op::shl)
ctjs.func @bitwise_generic(%receiver: !ctjs.value, %new_target: !ctjs.value,
                           %callee: !ctjs.value, %x: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %shifted = ctjs.binary shl %x, %x
  ctjs.frame_exit %ctx
  ctjs.return %shifted
}

// --- Constants: all lowered ---------------------------------------------------
//
// Numbers are emitted from their bit pattern: the attribute carries the
// double's bits, so `value::number(bit_cast<double>(bits))` is exact for every
// double, both zeroes and every NaN payload included. A decimal literal in the
// emitted source would not be: 0.0 and -0.0 print identically and are
// different JavaScript values.
//
// Strings go through ct_aot_new_string. It is a safepoint, so its result must
// be rooted before anything else can collect; the backend parks every value it
// produces where the collector can see it, so the string survives a
// collection triggered by a later helper.
//
// BigInt literals go through ct_aot_new_bigint_literal, which delegates to
// context::interned_bigint_literal, the same code the interpreter uses, so a
// compiled body and the interpreter share one interned value per literal. Its
// result is rooted the same way as a string's.
//
// Every ABI helper the backend calls must have a body in
// lib/Script/aot_bridge/: aot.hpp declares every row, so a call to a helper
// without a body would compile and fail only at link.
//
// So constants have no negative case left, and inventing an attribute to keep
// one would assert against a shape the importer cannot produce. What remains
// is the positive assertion: a BigInt literal must reach its helper memoised
// under the marker rather than the entry's site, because the two number their
// slots differently and sharing the key would let a compiled body read a slot
// the interpreter filled with another literal.
//
// CHECK-NOT: ctjs.func @returns_a_bigint
// CHECK: emitc.func @returns_a_bigint
// The marker is materialised as its own literal and passed by SSA value, so
// the name and the call are on different lines.
// CHECK: ctc_memo_returns_a_bigint
// CHECK: ct_aot_new_bigint_literal
ctjs.func @returns_a_bigint(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %n = ctjs.constant #ctjs.bigint<"1">
  ctjs.frame_exit %ctx
  ctjs.return %n
}

// --- One accepted function, so the allow-list is not simply refusing all -----
//
// Without this the file would pass just as well if the pass lowered nothing at
// all, which is the failure mode of a suite made only of negative cases.
//
// CHECK-NOT: ctjs.func @accepted
// CHECK: emitc.func @accepted
ctjs.func @accepted(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value, %x: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  ctjs.frame_exit %ctx
  ctjs.return %x
}
