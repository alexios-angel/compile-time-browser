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

// --- USES THE CALLEE --------------------------------------------------------
//
// Same gap, and one more behind it: ct_aot_callee reads call_frame::closure,
// and ct_aot_enter never sets that field - so even once the helper exists it
// would answer `undefined` for every compiled frame.
//
// CHECK-LABEL: ctjs.func @uses_callee
// CHECK-SAME: ctjs.not_lowered = "uses the callee
ctjs.func @uses_callee(%receiver: !ctjs.value, %new_target: !ctjs.value,
                       %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  ctjs.frame_exit %ctx
  ctjs.return %callee
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
// The allow-list's default. `ctjs.call` needs an argv span marshalled into the
// frame's GC-rooted slots, an argc, a baked call site and a suspension edge -
// and the reason names the operation, so the work list reads itself.
//
// CHECK-LABEL: ctjs.func @calls
// CHECK-SAME: ctjs.not_lowered = "no lowering yet for ctjs.call"
ctjs.func @calls(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value,
                 %f: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %r = ctjs.call %f(%receiver)
  ctjs.frame_exit %ctx
  ctjs.return %r
}

// --- frame_exit THAT IS NOT THE LAST THING BEFORE THE RETURN ----------------
//
// The shared failure path leaves the frame too, so anything fallible after an
// in-place exit emits a SECOND ct_aot_leave on the way out. The runtime makes
// that harmless - leave truncates to this frame's own recorded index rather
// than popping, and its row calls it "a harmless no-op after a failure" - so
// this is an unchecked invariant rather than a live defect. Checked anyway: the
// importer emits frame_exit immediately before every return, and a module where
// that stopped being true is one nobody has looked at.
//
// CHECK-LABEL: ctjs.func @exits_early
// CHECK-SAME: ctjs.not_lowered = "ctjs.frame_exit is not immediately followed
ctjs.func @exits_early(%receiver: !ctjs.value, %new_target: !ctjs.value,
                       %callee: !ctjs.value, %x: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  ctjs.frame_exit %ctx
  %sum = ctjs.binary add %x, %x
  ctjs.return %sum
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
// A STRING STILL IS. It reaches ct_aot_new_string, which allocates and is a
// safepoint, and nothing roots the result across it yet.
//
// CHECK-LABEL: ctjs.func @returns_a_string
// CHECK-SAME: ctjs.not_lowered = "no lowering yet for this constant
ctjs.func @returns_a_string(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %s = ctjs.constant #ctjs.string<"hello">
  ctjs.frame_exit %ctx
  ctjs.return %s
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
