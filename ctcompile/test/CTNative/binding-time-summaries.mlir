// RUN: split-file %s %t
// RUN: ctjs-opt %t/caller-first.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=KEY
// RUN: ctjs-opt %t/callee-first.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=KEY
// RUN: ctjs-opt %t/caller-first.mlir --ctnative-binding-time-analysis --ctnative-binding-time-analysis | FileCheck %s --check-prefix=KEY
// RUN: ctjs-opt %t/joins.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=JOIN
// RUN: ctjs-opt %t/recursive.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=RECURSIVE

// The caller needs the exact returned string, not just static completion or
// a primitive domain, to prove that its property read stays on an own field.
// Both source orders must reach the same complete return facts. Reprocessing
// dependents must preserve facts already computed for unrelated functions.
// KEY-LABEL: ctjs.func private @consumer
// KEY-SAME: ctnative.binding_time_summary = {dynamic_ops = 0 : i64
// KEY: ctjs.call_direct @forward{{.*}}ctnative.binding_time = "static"
// KEY: ctjs.get_property {{.*}}ctnative.binding_time = "static"
// KEY: ctjs.binary add {{.*}}ctnative.binding_time = "static"
// KEY-LABEL: ctjs.func private @unrelated
// KEY-SAME: ctnative.binding_time_summary = {dynamic_ops = 0 : i64
// KEY: ctjs.binary add {{.*}}ctnative.binding_time = "static"

//--- caller-first.mlir
module {
  ctjs.func private @consumer(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %object = ctjs.create_object
    %own = ctjs.constant #ctjs.string<"answer">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%own], %one
    %key = ctjs.call_direct @forward(%u, %u, %u)
    %value = ctjs.get_property %object[%key]
    %result = ctjs.binary add %value, %one
    ctjs.return %result
  }
  ctjs.func private @unrelated(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %result = ctjs.binary add %one, %one
    ctjs.return %result
  }
  ctjs.func private @forward(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @key(%u, %u, %u)
    ctjs.return %result
  }
  ctjs.func private @key(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"answer">
    ctjs.return %key
  }
}

//--- callee-first.mlir
module {
  ctjs.func private @key(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"answer">
    ctjs.return %key
  }
  ctjs.func private @forward(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @key(%u, %u, %u)
    ctjs.return %result
  }
  ctjs.func private @consumer(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %object = ctjs.create_object
    %own = ctjs.constant #ctjs.string<"answer">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%own], %one
    %key = ctjs.call_direct @forward(%u, %u, %u)
    %value = ctjs.get_property %object[%key]
    %result = ctjs.binary add %value, %one
    ctjs.return %result
  }
  ctjs.func private @unrelated(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %result = ctjs.binary add %one, %one
    ctjs.return %result
  }
}

//--- joins.mlir
module {
  // BTA joins static arms conservatively. The two possible literal keys must
  // not turn into one exact own-field name merely because the call is static.
  // JOIN-LABEL: ctjs.func private @joined
  // JOIN: ctjs.call_direct @keys{{.*}}ctnative.binding_time = "static"
  // JOIN: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @joined(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %object = ctjs.create_object
    %own = ctjs.constant #ctjs.string<"answer">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%own], %one
    %key = ctjs.call_direct @keys(%u, %u, %u)
    %result = ctjs.get_property %object[%key]
    ctjs.return %result
  }
  ctjs.func private @keys(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %take = arith.constant true
    %key = scf.if %take -> (!ctjs.value) {
      %a = ctjs.constant #ctjs.string<"answer">
      scf.yield %a : !ctjs.value
    } else {
      %b = ctjs.constant #ctjs.string<"other">
      scf.yield %b : !ctjs.value
    }
    ctjs.return %key
  }
}

//--- recursive.mlir
module {
  // Literal normal returns do not bootstrap purity/completion in an SCC.
  // The recursive call and its heap invalidation stay dynamic even though
  // a constant after the call still has a static SSA value.
  // RECURSIVE-LABEL: ctjs.func private @first
  // RECURSIVE: ctjs.call_direct @second{{.*}}ctnative.binding_time = "dynamic"
  // RECURSIVE: ctjs.return {{.*}}ctnative.binding_time = "static"
  ctjs.func private @first(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %ignored = ctjs.call_direct @second(%u, %u, %u)
    %key = ctjs.constant #ctjs.string<"answer">
    ctjs.return %key
  }
  // RECURSIVE-LABEL: ctjs.func private @second
  // RECURSIVE: ctjs.call_direct @first{{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @second(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %ignored = ctjs.call_direct @first(%u, %u, %u)
    %key = ctjs.constant #ctjs.string<"answer">
    ctjs.return %key
  }
  // RECURSIVE-LABEL: ctjs.func private @caller
  // RECURSIVE: ctjs.call_direct @first{{.*}}ctnative.binding_time = "dynamic"
  // RECURSIVE: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @caller(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %object = ctjs.create_object
    %own = ctjs.constant #ctjs.string<"answer">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%own], %one
    %ignored = ctjs.call_direct @first(%u, %u, %u)
    %result = ctjs.get_property %object[%own]
    ctjs.return %result
  }
}
