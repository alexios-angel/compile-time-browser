// RUN: split-file %s %t
// RUN: ctjs-opt %t/mixed.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-opt %t/calls.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=CALLS
// RUN: ctjs-opt %t/loop.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=LOOP

//--- mixed.mlir
module {
  // Common static arguments coexist with unknown runtime arguments. A later
  // write invalidates future reads, not a previously copied scalar value.
  // MIXED-LABEL: ctjs.func private @mixed
  // MIXED-SAME: ctnative.argument_binding_times = ["dynamic", "dynamic", "dynamic", "static", "dynamic"]
  // MIXED: ctjs.create_object {{.*}}ctnative.binding_time = "static"
  // MIXED: ctjs.set_property {{.*}}ctnative.binding_time = "static"
  // MIXED: ctjs.get_property {{.*}}ctnative.binding_time = "static"
  // MIXED: ctjs.set_property {{.*}}ctnative.binding_time = "dynamic"
  // MIXED: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  // MIXED: ctjs.binary add {{.*}}ctnative.binding_time = "static"
  // MIXED: ctjs.binary add {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @mixed(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %seed: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"answer">
    ctjs.set_property %object[%key], %seed
    %saved = ctjs.get_property %object[%key]
    ctjs.set_property %object[%key], %runtime
    %after = ctjs.get_property %object[%key]
    %static = ctjs.binary add %saved, %seed
    %result = ctjs.binary add %static, %after
    ctjs.return %result
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %result = ctjs.call_direct @mixed(%u, %u, %u, %one, %runtime)
    ctjs.return %result
  }
}

//--- calls.mlir
module {
  // Bottom-up summaries need more than source order: outer precedes inner.
  // CALLS-LABEL: ctjs.func private @outer
  // CALLS: ctjs.call_direct @inner{{.*}}ctnative.binding_time = "static"
  // CALLS: ctjs.binary add {{.*}}ctnative.binding_time = "static"
  ctjs.func private @outer(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %a = ctjs.call_direct @inner(%u, %u, %u)
    %result = ctjs.binary add %a, %a
    ctjs.return %result
  }
  ctjs.func private @inner(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.return %one
  }
}

//--- loop.mlir
module {
  // A loop needs an invariant; its carried values cannot inherit the first
  // iteration's constant merely because the initializer was known.
  // LOOP-LABEL: ctjs.func @loop
  // LOOP: arith.constant {{.*}}ctnative.binding_time = "static"{{.*}} true
  // LOOP: arith.constant {{.*}}ctnative.binding_time = "dynamic"{{.*}}ctnative.result_binding_times = ["static"]{{.*}} true
  // LOOP: } attributes {ctnative.binding_time = "dynamic"
  // LOOP: ctjs.binary add {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @loop(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %take = arith.constant true
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %carried = scf.while (%v = %one) : (!ctjs.value) -> !ctjs.value {
      %again = arith.constant true
      scf.condition(%again) %v : !ctjs.value
    } do {
    ^bb0(%next: !ctjs.value):
      scf.yield %next : !ctjs.value
    }
    %result = ctjs.binary add %carried, %one
    ctjs.return %result
  }
}
