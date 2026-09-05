// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-partial-prefix-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate | FileCheck %s --check-prefix=PREFIX
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-partial-prefix-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate --ctnative-partial-evaluate | FileCheck %s --check-prefix=REPEAT
// RUN: split-file %s %t
// RUN: ctjs-opt %t/rollback.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=ROLLBACK
// RUN: ctjs-opt %t/cycle.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=CYCLE
// RUN: ctjs-opt %t/cycle.mlir '--ctnative-partial-evaluate=max-nodes=0' | FileCheck %s --check-prefix=NODES
// RUN: ctjs-opt %t/control.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=CONTROL

// The first dynamic write remains. Shared child identity crosses the boundary,
// while the discarded object, Map, and obsolete stores disappear.
// PREFIX-LABEL: ctjs.func private @mixedSeed$1
// PREFIX-SAME: ctnative.partial_evaluated = {boundary = "ctjs.call"
// PREFIX-SAME: evaluated_nodes = 4 : i64
// PREFIX-SAME: mode = "prefix"
// PREFIX-SAME: residual_nodes = 2 : i64
// PREFIX-NOT: ctjs.create_object
// PREFIX-NOT: temporary
// PREFIX: %[[CHILD:[0-9]+]] = ctjs.construct
// PREFIX: %[[ROOT:[0-9]+]] = ctjs.construct
// PREFIX: ctjs.call {{.*}}(%[[ROOT]], {{.*}}, %[[CHILD]])
// PREFIX: ctjs.call {{.*}}(%[[ROOT]], {{.*}}, %[[CHILD]])
// PREFIX: ctjs.call {{.*}}(%[[CHILD]], {{.*}}, %arg3)
// PREFIX: ctjs.return %[[ROOT]]
// PREFIX-LABEL: ctjs.func private @scalarSnapshot$3
// PREFIX-SAME: mode = "prefix"
// PREFIX-SAME: residual_nodes = 1 : i64
// PREFIX: ctjs.call {{.*}}(%{{[0-9]+}}, %{{[0-9]+}}, %arg3)
// PREFIX: ctjs.binary mul
// PREFIX: ctjs.binary add
// PREFIX-LABEL: ctjs.func private @branchPrefix$4
// PREFIX-SAME: mode = "prefix"
// PREFIX: ctjs.truthy %arg3
// PREFIX: scf.if
// PREFIX-LABEL: ctjs.func private @loopPrefix$5
// PREFIX-SAME: mode = "prefix"
// PREFIX: scf.while
// PREFIX-LABEL: ctjs.func private @effectPrefix$6
// PREFIX-SAME: boundary = "ctjs.store_global"
// PREFIX-SAME: mode = "prefix"
// PREFIX-SAME: residual_nodes = 0 : i64
// PREFIX-NOT: ctjs.construct
// PREFIX: ctjs.store_global "published", %arg3
// PREFIX-LABEL: ctjs.func private @callPrefix$8
// PREFIX-SAME: boundary = "ctjs.call_direct"
// PREFIX-SAME: mode = "prefix"
// PREFIX: ctjs.call_direct @mutateOnce$7
// PREFIX-NOT: ctjs.call_direct
// PREFIX: ctjs.return
// PREFIX-LABEL: ctjs.func private @keyPrefix$9
// PREFIX-SAME: mode = "prefix"
// PREFIX-SAME: residual_nodes = 3 : i64
// REPEAT-LABEL: ctjs.func private @mixedSeed$1
// REPEAT-SAME: mode = "prefix"
// REPEAT-SAME: residual_nodes = 2 : i64
// REPEAT-LABEL: ctjs.func private @scalarSnapshot$3
// REPEAT-SAME: mode = "prefix"
// REPEAT-SAME: residual_nodes = 1 : i64

//--- rollback.mlir
module {
  // A whole-function attempt executes the first callee write, then refuses
  // its global effect. Prefix evaluation starts from fresh state and leaves
  // the one call against value 1, not the failed attempt's value 2.
  // ROLLBACK-LABEL: ctjs.func private @outer
  // ROLLBACK-SAME: boundary = "ctjs.call_direct"
  // ROLLBACK-SAME: mode = "prefix"
  // ROLLBACK: %[[OBJECT:.*]] = ctjs.create_object
  // ROLLBACK: %[[ONE:.*]] = ctjs.constant #ctjs.number<4607182418800017408>
  // ROLLBACK: ctjs.set_property %[[OBJECT]][{{.*}}], %[[ONE]]
  // ROLLBACK: ctjs.call_direct @effect
  // ROLLBACK-NOT: ctjs.call_direct
  // ROLLBACK: ctjs.return
  ctjs.func private @outer(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %one
    %result = ctjs.call_direct @effect(%u, %u, %u, %object)
    ctjs.return %result
  }
  ctjs.func private @effect(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    ctjs.set_property %object[%key], %two
    ctjs.store_global "effect", %two
    ctjs.return %object
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @outer(%u, %u, %u)
    ctjs.return %r
  }
}

//--- cycle.mlir
module {
  // CYCLE-LABEL: ctjs.func private @cycle
  // CYCLE-SAME: ctnative.partial_eval_reason = "prefix live heap has an ownership cycle or unsupported value"
  // CYCLE-NOT: ctnative.partial_evaluated
  // CYCLE: %[[OBJECT:.*]] = ctjs.create_object
  // CYCLE: ctjs.set_property %[[OBJECT]][{{.*}}], %[[OBJECT]]
  // CYCLE: ctjs.set_property %[[OBJECT]][{{.*}}], %arg3
  // NODES-LABEL: ctjs.func private @cycle
  // NODES-SAME: ctnative.partial_eval_reason = "heap-node budget exhausted"
  // NODES: ctjs.set_property
  ctjs.func private @cycle(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %self = ctjs.constant #ctjs.string<"self">
    %key = ctjs.constant #ctjs.string<"input">
    ctjs.set_property %object[%self], %object
    ctjs.set_property %object[%key], %input
    ctjs.return %object
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @cycle(%u, %u, %u, %input)
    ctjs.return %r
  }
}

//--- control.mlir
module {
  // CONTROL-LABEL: ctjs.func private @control
  // CONTROL-SAME: boundary = "cf.cond_br"
  // CONTROL-SAME: mode = "prefix"
  // CONTROL: %[[TAKE:.*]] = arith.constant true
  // CONTROL: cf.cond_br %[[TAKE]]
  // CONTROL: ctjs.binary add {{.*}}, %arg3
  // CONTROL: ctjs.return
  ctjs.func private @control(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %yes = ctjs.constant #ctjs.boolean<true>
    %take = ctjs.truthy %yes
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %two = ctjs.binary add %one, %one
    cf.cond_br %take, ^left, ^right
  ^left:
    %sum = ctjs.binary add %two, %input
    ctjs.return %sum
  ^right:
    ctjs.return %input
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @control(%u, %u, %u, %input)
    ctjs.return %r
  }
}
