// EVERY OPERATION AND EVERY TYPE, parsed, verified, printed, and parsed again.
//
// THE SECOND ctjs-opt IS THE TEST. One pass proves the parser accepts the
// syntax; the pair proves the PRINTER emits something the parser accepts, which
// is the property that makes every later test's expected output trustworthy. A
// printer that dropped an operand or a kind would pass a single-pass check
// against its own output forever.
//
// The Phase 8 gate asks that this cover every operation. It does, and the
// anti-pattern list names the alternative: "An operation added without a
// roundtrip test."

// RUN: ctjs-opt %s | ctjs-opt | FileCheck %s

// CHECK-LABEL: func.func private @every_ctjs_type(
// CHECK-SAME: !ctjs.value
// CHECK-SAME: !ctjs.context
// CHECK-SAME: !ctjs.program
// CHECK-SAME: !ctjs.closure
// CHECK-SAME: -> !ctjs.coroutine
func.func private @every_ctjs_type(%value: !ctjs.value, %context: !ctjs.context,
                                   %program: !ctjs.program, %closure: !ctjs.closure)
    -> !ctjs.coroutine

// ---- constants -------------------------------------------------------------
// EVERY CONSTANT SHAPE, because the reason these are dialect attributes rather
// than builtin ones is that -0.0, NaN and BigInt have to survive exactly.
// CHECK-LABEL: ctjs.func @constants
ctjs.func @constants(%unused: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // CHECK: ctjs.constant #ctjs.undefined
  %undef = ctjs.constant #ctjs.undefined
  // CHECK: ctjs.constant #ctjs.null
  %null = ctjs.constant #ctjs.null
  // CHECK: ctjs.constant #ctjs.boolean<true>
  %yes = ctjs.constant #ctjs.boolean<true>
  // THE BITS OF -0.0, which is 0x8000000000000000 and is a DIFFERENT value from
  // 0.0: Object.is(-0, 0) is false and 1 / -0 is -Infinity.
  // CHECK: ctjs.constant #ctjs.number<9223372036854775808>
  %negzero = ctjs.constant #ctjs.number<9223372036854775808>
  // CHECK: ctjs.constant #ctjs.number<0>
  %zero = ctjs.constant #ctjs.number<0>
  // CHECK: ctjs.constant #ctjs.string<"hello">
  %text = ctjs.constant #ctjs.string<"hello">
  // CHECK: ctjs.constant #ctjs.bigint<"123456789012345678901234567890">
  %big = ctjs.constant #ctjs.bigint<"123456789012345678901234567890">
  ctjs.return %undef
}

// ---- bindings --------------------------------------------------------------
// CHECK-LABEL: ctjs.func @bindings
ctjs.func @bindings(%closure: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 2 : i32} {
  // CHECK: ctjs.load_global "console"
  %g = ctjs.load_global "console"
  // CHECK: ctjs.store_global "answer", %{{.*}}
  ctjs.store_global "answer", %g
  ctjs.return %g
}

// CHECK-LABEL: ctjs.func @upvalues
ctjs.func @upvalues(%v: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
  %ctx = ctjs.frame_enter 4
  // A !ctjs.program FROM NOWHERE. ctjs.func takes only !ctjs.value parameters -
  // its verifier says so - and no operation produces a program yet: Phase 9's
  // ITS FIRST OPERAND IS THE ENCLOSING CLOSURE, a value, and it used to be a
  // !ctjs.program that nothing produced - which is exactly why the importer
  // could not emit this operation and skipped every `closure` opcode. The
  // program is selected from `enclosing->owner`, because a context can be
  // running functions from more than one program and a function index means
  // nothing outside the one it was compiled in.
  //
  // AND THE LAST IS THE EFFECTIVE RECEIVER, used only when the target is an
  // arrow: an arrow's `this` is decided where it is WRITTEN.
  // CHECK: ctjs.create_closure %{{.*}}[3] this %{{.*}} captures %{{.*}}
  %fn = ctjs.create_closure %v[3] this %v captures %v
  // CHECK: ctjs.create_cell %{{.*}}
  %cell = ctjs.create_cell %v
  // CHECK: ctjs.cell_get %{{.*}}
  %inside = ctjs.cell_get %cell
  // CHECK: ctjs.cell_set %{{.*}}, %{{.*}}
  ctjs.cell_set %cell, %inside
  // THE CLOSURE OPERAND IS A VALUE, because a frame's own closure arrives as an
  // ordinary argument and ctjs.func takes only !ctjs.value.
  // CHECK: ctjs.load_upvalue %{{.*}}[0]
  %up = ctjs.load_upvalue %v[0]
  // CHECK: ctjs.store_upvalue %{{.*}}[0], %{{.*}}
  ctjs.store_upvalue %v[0], %up
  ctjs.frame_exit %ctx
  ctjs.return %up
}

// ---- properties ------------------------------------------------------------
// CHECK-LABEL: ctjs.func @properties
ctjs.func @properties(%obj: !ctjs.value, %key: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  // CHECK: ctjs.get_property %{{.*}}[%{{.*}}]
  %read = ctjs.get_property %obj[%key]
  // CHECK: ctjs.set_property %{{.*}}[%{{.*}}], %{{.*}}
  ctjs.set_property %obj[%key], %read
  // NO RESULT: ct_aot_delete_index answers with a ct_aot_status, and the
  // bytecode loads `true` as a separate step. Declaring a !ctjs.value here
  // made `delete o[k]` evaluate to the status, whose `ok` is 3.
  // CHECK: ctjs.delete_property %{{.*}}[%{{.*}}]
  ctjs.delete_property %obj[%key]
  // CHECK: ctjs.has_property %{{.*}} in %{{.*}}
  %has = ctjs.has_property %key in %obj
  ctjs.return %has
}

// ---- operators -------------------------------------------------------------
// CHECK-LABEL: ctjs.func @operators
ctjs.func @operators(%a: !ctjs.value, %b: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  // CHECK: ctjs.binary add %{{.*}}, %{{.*}}
  %sum = ctjs.binary add %a, %b
  // CHECK: ctjs.binary concat %{{.*}}, %{{.*}}
  %joined = ctjs.binary concat %a, %b
  // THE STATIC FAMILY IS A DIFFERENT OPERATION, not a flag: it cannot run user
  // code, which is a difference in traits.
  // CHECK: ctjs.binary_static add %{{.*}}, %{{.*}}
  %bumped = ctjs.binary_static add %a, %b
  // CHECK: ctjs.unary neg %{{.*}}
  %neg = ctjs.unary neg %a
  // CHECK: ctjs.unary typeof %{{.*}}
  %kind = ctjs.unary typeof %a
  // CHECK: ctjs.compare strict_eq %{{.*}}, %{{.*}}
  %same = ctjs.compare strict_eq %a, %b
  // CHECK: ctjs.compare lt %{{.*}}, %{{.*}}
  %less = ctjs.compare lt %a, %b
  // CHECK: ctjs.convert to_boolean %{{.*}}
  %truthy = ctjs.convert to_boolean %a
  // THE BRIDGE TO A BRANCH: an i1, not a JavaScript boolean.
  // CHECK: ctjs.truthy %{{.*}}
  %bit = ctjs.truthy %a
  // AN i1: ct_aot_instance_of returns a uint32_t 0 or 1. As a !ctjs.value it
  // would have been value::from_bits(1) - a subnormal double.
  // CHECK: ctjs.instanceof %{{.*}}, %{{.*}}
  %isa = ctjs.instanceof %a, %b
  // BOTH PREDICATES ARE i1 AND NEITHER IS RETURNABLE AS A VALUE, which is the
  // point: boxing is a separate step the front end asks for when it needs one.
  ctjs.return %a
}

// ---- allocation ------------------------------------------------------------
// CHECK-LABEL: ctjs.func @allocation
ctjs.func @allocation(%v: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // CHECK: ctjs.create_object
  %obj = ctjs.create_object
  // CHECK: ctjs.create_array[%{{.*}}]
  %arr = ctjs.create_array [%v]
  // CHECK: ctjs.create_array[]
  %empty = ctjs.create_array []
  // CHECK: ctjs.append %{{.*}} to %{{.*}}
  ctjs.append %v to %arr
  // CHECK: ctjs.create_regexp "ab+c", "gi"
  %re = ctjs.create_regexp "ab+c", "gi"
  ctjs.return %obj
}

// ---- calls -----------------------------------------------------------------
// CHECK-LABEL: ctjs.func @calls
ctjs.func @calls(%callee: !ctjs.value, %recv: !ctjs.value, %arg: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  // CHECK: ctjs.call %{{.*}}(%{{.*}}, %{{.*}})
  %called = ctjs.call %callee(%recv, %arg)
  // CHECK: ctjs.call %{{.*}}(%{{.*}})
  %bare = ctjs.call %callee(%recv)
  // CHECK: ctjs.construct %{{.*}}(%{{.*}}, %{{.*}})
  %made = ctjs.construct %callee(%recv, %arg)
  ctjs.return %made
}

// ---- control flow and completion -------------------------------------------
// AN UNSTRUCTURED CFG, which is the plan's decision: "Import produces an
// unstructured CFG matching the bytecode: basic blocks, cf terminators, and
// explicit handler blocks derived from the bytecode handler table."
// CHECK-LABEL: ctjs.func @control
ctjs.func @control(%v: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // CHECK: ctjs.push_handler ^{{.*}} catch ^{{.*}}
  ctjs.push_handler ^body catch ^handler
^body:
  // CHECK: ctjs.pop_handler
  ctjs.pop_handler
  cf.br ^done(%v : !ctjs.value)
^handler:
  // CHECK: ctjs.resume_throw
  ctjs.resume_throw
^done(%out: !ctjs.value):
  ctjs.return %out
}

// CHECK-LABEL: ctjs.func @throws
ctjs.func @throws(%v: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // CHECK: ctjs.throw %{{.*}}
  ctjs.throw %v
}

// ---- suspension ------------------------------------------------------------
// CHECK-LABEL: ctjs.func @suspends
ctjs.func @suspends(%v: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // CHECK: ctjs.resume_point 0
  ctjs.resume_point 0
  // CHECK: ctjs.suspend await %{{.*}}
  %awaited = ctjs.suspend await %v
  // CHECK: ctjs.resume_point 1
  ctjs.resume_point 1
  // CHECK: ctjs.suspend yield %{{.*}}
  %yielded = ctjs.suspend yield %awaited
  ctjs.return %yielded
}

// ---- frames and rooting ----------------------------------------------------
// CHECK-LABEL: ctjs.func @frames
ctjs.func @frames(%v: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // CHECK: %[[CTX:.*]] = ctjs.frame_enter
  %ctx = ctjs.frame_enter 4
  // CHECK: ctjs.root %{{.*}} in %[[CTX]]
  ctjs.root %v in %ctx
  // CHECK: ctjs.frame_exit %[[CTX]]
  ctjs.frame_exit %ctx
  ctjs.return %v
}
