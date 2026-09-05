// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-partial-evaluation-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate | FileCheck %s --check-prefix=HEAP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-partial-evaluation-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate --ctnative-partial-evaluate | FileCheck %s --check-prefix=REPEAT
// RUN: split-file %s %t
// RUN: ctjs-opt %t/cycle.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=CYCLE
// RUN: ctjs-opt %t/effect.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=EFFECT
// RUN: ctjs-opt %t/arguments.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=ARGS
// RUN: ctjs-opt %t/loop.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=LOOP
// RUN: ctjs-opt %t/loop.mlir '--ctnative-partial-evaluate=max-steps=8' | FileCheck %s --check-prefix=BUDGET
// RUN: ctjs-opt %t/cycle.mlir '--ctnative-partial-evaluate=max-nodes=0' | FileCheck %s --check-prefix=NODES
// RUN: ctjs-opt %t/depth.mlir '--ctnative-partial-evaluate=max-depth=1' | FileCheck %s --check-prefix=DEPTH
// RUN: ctjs-opt %t/prototype.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-opt %t/primitives.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=PRIMITIVE
// RUN: ctjs-opt %t/unknown.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNKNOWN

// The graph has one child shared by two entries. A dead plain object and a
// dead Map disappear. The observers' heap arguments remain runtime values.
// HEAP-LABEL: ctjs.func private @sharedSeed$1
// HEAP-SAME: ctnative.partial_evaluated = {evaluated_nodes = 4 : i64, residual_nodes = 2 : i64
// HEAP: %[[ROOT:[0-9]+]] = ctjs.construct
// HEAP: %[[CHILD:[0-9]+]] = ctjs.construct
// HEAP-NOT: ctjs.create_object
// HEAP-NOT: temporary
// HEAP: ctjs.call {{.*}}(%[[ROOT]], {{.*}}, %[[CHILD]])
// HEAP: ctjs.call {{.*}}(%[[ROOT]], {{.*}}, %[[CHILD]])
// HEAP: ctjs.return %[[ROOT]]
// HEAP-LABEL: ctjs.func private @observeSharing$2
// HEAP-SAME: ctnative.partial_eval_reason = "callers do not supply one set of constant arguments"
// HEAP-LABEL: ctjs.func private @keyedSeed$5
// HEAP-SAME: residual_nodes = 3 : i64
// HEAP: ctjs.create_object
// HEAP: ctjs.create_object
// HEAP-LABEL: ctjs.func private @constantLoop$9
// HEAP-SAME: ctnative.partial_evaluated =
// HEAP-NOT: scf.while
// HEAP: ctjs.return
// HEAP-LABEL: ctjs.func private @branchSeed$10
// HEAP-SAME: ctnative.partial_evaluated =
// HEAP-NOT: scf.if
// HEAP: ctjs.return
// HEAP-LABEL: ctjs.func private @primitiveSeed$11
// HEAP-SAME: ctnative.partial_evaluated =
// HEAP-NOT: ctjs.binary
// HEAP-NOT: scf.if
// HEAP: ctjs.return
// HEAP-LABEL: ctjs.func private @numericKeySeed$12
// HEAP-SAME: evaluated_nodes = 1 : i64, residual_nodes = 1 : i64
// REPEAT-LABEL: ctjs.func private @sharedSeed$1
// REPEAT-SAME: evaluated_nodes = 2 : i64, residual_nodes = 2 : i64
// REPEAT: ctjs.construct
// REPEAT: ctjs.construct
// REPEAT-LABEL: ctjs.func private @observeSharing$2
// REPEAT-SAME: ctnative.partial_eval_reason = "callers do not supply one set of constant arguments"


//--- cycle.mlir
module {
  // CYCLE-LABEL: ctjs.func private @cycle
  // CYCLE-SAME: ctnative.partial_eval_reason = "returned heap has an ownership cycle or unsupported value"
  // CYCLE-NOT: ctnative.partial_evaluated
  // CYCLE: %[[OBJECT:.*]] = ctjs.create_object
  // CYCLE: ctjs.set_property %[[OBJECT]][{{.*}}], %[[OBJECT]]
  // NODES-LABEL: ctjs.func private @cycle
  // NODES-SAME: ctnative.partial_eval_reason = "heap-node budget exhausted"
  // NODES: ctjs.set_property
  ctjs.func private @cycle(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.partial_evaluated = "forged"} {
    %object = ctjs.create_object {ctnative.partial_evaluated = "forged"}
    %key = ctjs.constant #ctjs.string<"self">
    ctjs.set_property %object[%key], %object
    ctjs.return %object
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @cycle(%u, %u, %u)
    ctjs.return %r
  }
}

//--- effect.mlir
module {
  // EFFECT-LABEL: ctjs.func private @effect
  // EFFECT-SAME: ctnative.partial_eval_reason = "unsupported or unknown operation `ctjs.store_global`"
  // EFFECT: ctjs.create_object
  // EFFECT: ctjs.set_property
  // EFFECT: ctjs.store_global "published"
  ctjs.func private @effect(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"x">
    %one = ctjs.constant #ctjs.number<1>
    ctjs.set_property %object[%key], %one
    ctjs.store_global "published", %object
    ctjs.return %object
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @effect(%u, %u, %u)
    ctjs.return %r
  }
}

//--- arguments.mlir
module {
  // ARGS-LABEL: ctjs.func private @different
  // ARGS-SAME: ctnative.partial_eval_reason = "callers do not supply one set of constant arguments"
  // ARGS: ctjs.binary add
  ctjs.func private @different(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.binary add %n, %n
    ctjs.return %result
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<1>
    %two = ctjs.constant #ctjs.number<2>
    %a = ctjs.call_direct @different(%u, %u, %u, %one)
    %b = ctjs.call_direct @different(%u, %u, %u, %two)
    ctjs.return %b
  }
}

//--- loop.mlir
module {
  // LOOP-LABEL: ctjs.func private @loop
  // LOOP-SAME: ctnative.partial_evaluated =
  // LOOP-NOT: cf.cond_br
  // LOOP: ctjs.return
  // BUDGET-LABEL: ctjs.func private @loop
  // BUDGET-SAME: ctnative.partial_eval_reason = "step budget exhausted"
  // BUDGET: cf.cond_br
  ctjs.func private @loop(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %three = ctjs.constant #ctjs.number<4613937818241073152>
    cf.br ^test(%zero : !ctjs.value)
  ^test(%n: !ctjs.value):
    %less = ctjs.compare lt %n, %three
    %take = ctjs.truthy %less
    cf.cond_br %take, ^body, ^done
  ^body:
    %next = ctjs.binary add %n, %one
    cf.br ^test(%next : !ctjs.value)
  ^done:
    ctjs.return %n
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @loop(%u, %u, %u)
    ctjs.return %r
  }
}

//--- depth.mlir
module {
  // DEPTH-LABEL: ctjs.func private @outer
  // DEPTH-SAME: ctnative.partial_eval_reason = "call-depth budget exhausted"
  // DEPTH: ctjs.call_direct @inner
  ctjs.func private @outer(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @inner(%u, %u, %u)
    ctjs.return %r
  }
  ctjs.func private @inner(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<1>
    ctjs.return %one
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @outer(%u, %u, %u)
    ctjs.return %r
  }
}

//--- prototype.mlir
module {
  // PROTO-LABEL: ctjs.func private @prototype
  // PROTO-SAME: ctnative.partial_eval_reason = "module can inspect or mutate shared prototypes"
  // PROTO: ctjs.set_property
  ctjs.func private @prototype(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"__proto__">
    %other = ctjs.create_object
    ctjs.set_property %object[%key], %other
    ctjs.return %object
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @prototype(%u, %u, %u)
    ctjs.return %r
  }
}

//--- primitives.mlir
module {
  // A legal IEEE NaN payload overlaps the VM's Boolean tag. Importing it must
  // retain JavaScript number semantics instead of turning it into a Boolean.
  // PRIMITIVE-LABEL: ctjs.func private @nanType
  // PRIMITIVE-SAME: ctnative.partial_evaluated =
  // PRIMITIVE: ctjs.constant #ctjs.string<"number">
  // PRIMITIVE-NOT: ctjs.unary
  // PRIMITIVE-LABEL: ctjs.func private @nanEqual
  // PRIMITIVE-SAME: ctnative.partial_evaluated =
  // PRIMITIVE: ctjs.constant #ctjs.boolean<false>
  ctjs.func private @nanType(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nan = ctjs.constant #ctjs.number<9222246136947933187>
    %result = ctjs.unary typeof %nan
    ctjs.return %result
  }
  ctjs.func private @nanEqual(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nan = ctjs.constant #ctjs.number<9222246136947933187>
    %result = ctjs.compare strict_eq %nan, %nan
    ctjs.return %result
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %a = ctjs.call_direct @nanType(%u, %u, %u)
    %b = ctjs.call_direct @nanEqual(%u, %u, %u)
    ctjs.return %b
  }
}

//--- unknown.mlir
module {
  // UNKNOWN-LABEL: ctjs.func private @unknown
  // UNKNOWN-SAME: ctnative.partial_eval_reason = "unsupported or unknown operation `ctjs.compare`"
  // UNKNOWN: ctjs.compare strict_eq
  ctjs.func private @unknown(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %unknown = ub.poison : !ctjs.value
    %result = ctjs.compare strict_eq %object, %unknown
    ctjs.return %result
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @unknown(%u, %u, %u)
    ctjs.return %r
  }
}
