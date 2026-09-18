// RUN: split-file %s %t
// RUN: ctjs-opt %t/arguments.mlir --ctnative-precompute | FileCheck %s
// RUN: ctjs-opt %t/arguments.mlir '--ctnative-precompute=max-steps=0' | FileCheck %s --check-prefix=BUDGET
// RUN: ctjs-opt %t/alternate.mlir --ctnative-precompute | FileCheck %s --check-prefix=ALTERNATE

// All symbolic callers contribute, including forwarding and effects. A closed
// null argument removes its guarded property read; calls and stores survive.
// CHECK-LABEL: ctjs.func @entry
// CHECK: ctjs.call_direct @forward
// CHECK: ctjs.call_direct @guard
// CHECK-LABEL: ctjs.func private @forward
// CHECK: ctjs.call_direct @guard
// CHECK-LABEL: ctjs.func private @guard
// CHECK: ctjs.store_global "effect"
// CHECK-NOT: scf.if
// CHECK-NOT: ctjs.get_property
// CHECK: ctjs.return
// CHECK-LABEL: ctjs.func private @mixed
// CHECK: ctjs.truthy
// CHECK: scf.if
// CHECK-LABEL: ctjs.func @external
// CHECK: ctjs.truthy
// CHECK: scf.if
// CHECK-LABEL: ctjs.func private @self
// CHECK: ctjs.call %arg2
// CHECK: ctjs.truthy
// CHECK-LABEL: ctjs.func private @_script_$0
// CHECK: ctjs.truthy
// CHECK-LABEL: ctjs.func private @escaped$42
// CHECK: ctjs.truthy
// CHECK-LABEL: ctjs.func private @arguments
// CHECK: ctjs.make_arguments
// CHECK: ctjs.truthy
// CHECK-LABEL: ctjs.func private @short
// CHECK: ctjs.truthy
// BUDGET: budget_exhausted = true
// BUDGET-LABEL: ctjs.func private @guard
// BUDGET: ctjs.truthy
// BUDGET: ctjs.get_property

//--- arguments.mlir
module {
  ctjs.func @entry(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %n = ctjs.constant #ctjs.null
    %t = ctjs.constant #ctjs.boolean<true>
    %a = ctjs.call_direct @forward(%u, %u, %u, %n)
    %b = ctjs.call_direct @guard(%u, %u, %u, %n)
    %c = ctjs.call_direct @mixed(%u, %u, %u, %n)
    %d = ctjs.call_direct @mixed(%u, %u, %u, %t)
    %e = ctjs.call_direct @external(%u, %u, %u, %n)
    %f = ctjs.call_direct @self(%u, %u, %u, %n)
    %g = ctjs.call_direct @_script_$0(%u, %u, %u, %n)
    %h = ctjs.call_direct @escaped$42(%u, %u, %u, %n)
    %args = ctjs.call_direct @arguments(%u, %u, %u, %n)
    %i = ctjs.call_direct @short(%u, %u, %u, %u)
    %j = ctjs.call_direct @short(%u, %u, %u, %n)
    %made = ctjs.create_closure %arg2[42] this %u
    ctjs.return %made
  }
  ctjs.func private @forward(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @guard(%u, %u, %u, %input)
    ctjs.return %r
  }
  ctjs.func private @guard(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.store_global "effect", %input
    %take = ctjs.truthy %input
    %r = scf.if %take -> (!ctjs.value) {
      %key = ctjs.constant #ctjs.string<"field">
      %field = ctjs.get_property %input[%key]
      scf.yield %field : !ctjs.value
    } else {
      %f = ctjs.constant #ctjs.boolean<false>
      scf.yield %f : !ctjs.value
    }
    ctjs.return %r
  }
  ctjs.func private @mixed(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %take = ctjs.truthy %input
    scf.if %take { ctjs.store_global "mixed", %input }
    ctjs.return %input
  }
  ctjs.func @external(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %take = ctjs.truthy %input
    scf.if %take { ctjs.store_global "external", %input }
    ctjs.return %input
  }
  ctjs.func private @self(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %t = ctjs.constant #ctjs.boolean<true>
    %r = ctjs.call %arg2(%u, %t)
    %take = ctjs.truthy %input
    scf.if %take { ctjs.store_global "self", %r }
    ctjs.return %input
  }
  ctjs.func private @_script_$0(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %take = ctjs.truthy %input
    scf.if %take { ctjs.store_global "script", %input }
    ctjs.return %input
  }
  ctjs.func private @escaped$42(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %take = ctjs.truthy %input
    scf.if %take { ctjs.store_global "escaped", %input }
    ctjs.return %input
  }
  ctjs.func private @arguments(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %args = ctjs.make_arguments
    %take = ctjs.truthy %input
    scf.if %take { ctjs.store_global "arguments", %args }
    ctjs.return %input
  }
  ctjs.func private @short(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %take = ctjs.truthy %input
    scf.if %take { ctjs.store_global "short", %input }
    ctjs.return %input
  }
}

//--- alternate.mlir
module {
  ctjs.func @_script_$0(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %n = ctjs.constant #ctjs.null
    %t = ctjs.constant #ctjs.boolean<true>
    %made = ctjs.create_closure %arg2[1] this %u
    ctjs.store_global "original", %made
    %load = ctjs.load_global "original"
    %a = ctjs.call_direct @original$1(%u, %u, %load, %n)
    %b = ctjs.call_direct @variant(%u, %u, %load, %t)
    ctjs.return %a
  }
  // ALTERNATE-LABEL: ctjs.func private @original$1
  // ALTERNATE: ctjs.truthy
  // ALTERNATE: scf.if
  ctjs.func private @original$1(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %take = ctjs.truthy %input
    scf.if %take { ctjs.store_global "observed", %input }
    ctjs.return %input
  }
  ctjs.func private @variant(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.return %input
  }
}
