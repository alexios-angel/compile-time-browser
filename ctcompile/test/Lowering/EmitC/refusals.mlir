// WHAT THE BACKEND REFUSES, AND THAT IT SAYS SO.
//
// The importer set the precedent - "the count of what it leaves behind is the
// work list" - and a backend needs it more, not less. Half-lowering a function
// nobody taught it about does not fail: it produces a translation unit that
// compiles and computes something else, which is the failure this project keeps
// meeting. So the pass carries an ALLOW-LIST, and every function it declines is
// left intact with a `ctjs.not_lowered` attribute saying why.
//
// THE REFUSALS ARE THE TEST. A guard nobody has watched fire is not a guard,
// and an allow-list is exactly the kind that rots quietly: widen it by accident
// and nothing goes red, because the wrong answer still compiles. Each case
// below names the reason it is refused, so a change that starts lowering one of
// them has to come here and say so.

// RUN: ctjs-opt %s --ctjs-lower-to-emitc | FileCheck %s

// AND THE PASS ORDER, WHICH IS THE ONE MISTAKE THAT IS SILENT.
//
// --emitc-eliminate-block-arguments walks emitc.func and nothing else, so run
// BEFORE the lowering it finds nothing, returns success and says nothing - and
// the lowering then emits block arguments that reach mlir-translate, which
// loses a copy on their edges. Measured on a self-loop from real JavaScript:
// reversed, `sl(10,20,2)` answers 20 where 10 is correct, with ctjs-opt,
// mlir-translate and g++ all exiting 0. A leftover ctjs.func is exactly that
// mistake and nothing else looks like it, so it is refused.
// RUN: not ctjs-opt %s --emitc-eliminate-block-arguments 2>&1 \
// RUN:   | FileCheck --check-prefix=ORDER %s
// ORDER: ran before the module was lowered

// --- USES new.target --------------------------------------------------------
//
// The importer gives every function three implicit arguments - receiver,
// new.target, callee - because the bytecode reads them with their own opcodes.
// Only `receiver` is in the entry signature. The other two come from
// ct_aot_new_target and ct_aot_callee, and NEITHER HAS AN IMPLEMENTATION: both
// are declared in aot.hpp and defined nowhere in the runtime, so emitting a
// call to either is a link error rather than a wrong answer.
//
// It would be easy to pass `undefined` instead - the row says new.target IS
// undefined for an ordinary call, and today the runtime never populates it for
// a compiled constructor either. That is precisely why it is refused: undefined
// is an answer, and it would be the wrong one the moment either gap is closed.
//
// CHECK-LABEL: ctjs.func @uses_new_target
// CHECK-SAME: ctjs.not_lowered = "uses new.target
ctjs.func @uses_new_target(%receiver: !ctjs.value, %new_target: !ctjs.value,
                           %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  ctjs.frame_exit %ctx
  ctjs.return %new_target
}

// --- THE CALLEE IS NO LONGER REFUSED ---------------------------------------
//
// ct_aot_callee has a body now, and it is the only way a compiled function can
// reach its own upvalues: they live on the closure INSTANCE, while `site` is
// the function_proto that every closure over the same function shares. The
// entry ABI delivers `site` and not the closure, so the value comes out of
// call_frame::closure once ct_aot_enter has put one there.
//
// new.target IS STILL REFUSED and the two gaps are not the same.
// ct_aot_new_target has no body either, and behind it ct_aot_enter takes
// new.target from pending_new_target_, which op::construct never sets on the
// compiled path - so implementing the helper alone would answer undefined.
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

// --- AN UPVALUE OPERATION NAMING SOMEBODY ELSE'S CLOSURE ---------------------
//
// ct_aot_upvalue_cell reads the FRAME, so the operand is redundant with it -
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

// --- CONTROL FLOW IS NO LONGER REFUSED ------------------------------------
//
// It was, until --emitc-eliminate-block-arguments existed: emitting a block
// argument as it stands hits the copy the C++ emitter loses. That pass runs
// after this one, so a function with branches lowers here with its block
// arguments intact and is cleaned up downstream. The pipeline order is the
// contract; end-to-end.mlir runs it and compiles the result.
//
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

// --- CONTAINS AN OPERATION WITH NO LOWERING ---------------------------------
//
// The allow-list's default, and the reason names the operation so the work list
// reads itself. `ctjs.has_property` is doubly refused: it has no conversion,
// and ct_aot_has_property is one of the rows aot.hpp declares and
// aot_bridge.cpp does not define.
//
// PROPERTY WRITES USED TO BE THIS CASE and are lowered now, which is the right
// reason for a negative test to need rewriting.
//
// CHECK-LABEL: ctjs.func @asks
// CHECK-SAME: ctjs.not_lowered = "no lowering yet for ctjs.has_property"
ctjs.func @asks(%receiver: !ctjs.value, %new_target: !ctjs.value,
                %callee: !ctjs.value, %o: !ctjs.value, %k: !ctjs.value)
    -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 2
  %there = ctjs.has_property %k in %o
  ctjs.frame_exit %ctx
  ctjs.return %there
}

// --- A BINARY KIND ITS FAMILY DOES NOT SERVE --------------------------------
//
// The two families are disjoint apart from `add`. ct_aot_binary_op's switch has
// no arm for a bitwise opcode, so compiling `ctjs.binary shl` would pass
// op::halt - which is in range, so aot_bridge's bounds check passes - and the
// helper would answer `undefined` with status ok. A shift that silently
// evaluates to undefined is exactly the class of defect this project keeps
// meeting, so the kind is checked rather than the opcode trusted.
//
// CHECK-LABEL: ctjs.func @wrong_family
// CHECK-SAME: ctjs.not_lowered = "ctjs.binary was given a kind only the static family serves"
ctjs.func @wrong_family(%receiver: !ctjs.value, %new_target: !ctjs.value,
                        %callee: !ctjs.value, %x: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %shifted = ctjs.binary shl %x, %x
  ctjs.frame_exit %ctx
  ctjs.return %shifted
}

// --- A CONSTANT THAT CANNOT BE MATERIALISED WITHOUT ALLOCATING ------------
//
// A NUMBER IS NO LONGER REFUSED, and the reason is worth keeping: its attribute
// carries the double's BIT PATTERN, so `value::number(bit_cast<double>(bits))`
// is exact for every double there is - both zeroes and every NaN payload. A
// decimal literal in the emitted source would not be: 0.0 and -0.0 print
// identically and are different JavaScript values.
//
// A STRING IS NOT REFUSED ANY MORE, and the reason it used to give had gone
// stale rather than been wrong: "ct_aot_new_string is a safepoint, and nothing
// roots the result yet" was true when it was written and untrue from the moment
// the backend started parking every value it produces. Nobody rechecked it, and
// it was 383 of 417 refusals on bootstrap.bundle.js - 92% of the blockage
// traced to one sentence.
//
// A BIGINT LITERAL STILL IS, and for a reason that is still true:
// ct_aot_new_bigint_literal is one of the rows aot.hpp declares and
// aot_bridge.cpp does not define, so a call to it would compile and fail at
// link.
//
// CHECK-LABEL: ctjs.func @returns_a_bigint
// CHECK-SAME: ctjs.not_lowered = "no lowering yet for this constant
ctjs.func @returns_a_bigint(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %n = ctjs.constant #ctjs.bigint<"1">
  ctjs.frame_exit %ctx
  ctjs.return %n
}

// --- AND ONE THAT IS ACCEPTED, so the allow-list is not simply refusing all --
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
