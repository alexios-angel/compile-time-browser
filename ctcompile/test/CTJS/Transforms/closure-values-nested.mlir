// inFrame maps only the FUNCTION entry block's arguments to the call's
// arguments (ClosureValues.cpp). A nested region's entry block (a ctjs.try
// catch) is also an entry block, but its arguments are the thrown value and
// the throw site's state, never this call's arguments: the walk must leave
// the frame there and answer unknown, not the call's matching argument.
//
// nested.mlir is hand-written importer IR: f returns a cell that its try's
// catch writes with the catch's fourth argument, and w calls f with a
// closure as the first argument and then calls f's result. If inFrame read
// the catch argument as the call's first argument, the second call would
// wrongly answer ["onClick$2"].
//
// RUN: ctjs-opt %s --ctjs-test-closure-values | FileCheck %s

// CHECK-LABEL: ctjs.func @w$1(
// CHECK: ctjs.call {{.*}} {ctjs.test_closures_unknown = "an argument of ctjs.try at {{.*}}closure-values-nested.mlir:37"}

module {
  ctjs.func @w$1(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %0 = ctjs.constant #ctjs.undefined
    %1 = ctjs.create_closure %arg2[3] this %0
    %2 = ctjs.create_closure %arg2[2] this %0
    %3 = ctjs.constant #ctjs.undefined
    %4 = ctjs.call %1(%3, %2)
    %5 = ctjs.constant #ctjs.undefined
    %6 = ctjs.call %4(%5)
    %7 = ctjs.constant #ctjs.undefined
    ctjs.return %7
  }
  ctjs.func private @onClick$2(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %0 = ctjs.constant #ctjs.number<1>
    ctjs.return %0
  }
  ctjs.func private @f$3(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %arg3: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %0 = ctjs.constant #ctjs.undefined
    %1 = ctjs.create_cell %0
    %2 = ctjs.try {
      %3 = arith.constant false
      %4 = ctjs.constant #ctjs.undefined
      %5 = ctjs.constant #ctjs.undefined
      ctjs.try_exit %3 normal %4 caught(%5, %5, %5, %5)
    } catch {
    ^bb0(%6: !ctjs.value, %7: !ctjs.value, %8: !ctjs.value, %9: !ctjs.value):
      ctjs.cell_set %1, %9
      %10 = ctjs.constant #ctjs.undefined
      ctjs.try_yield %10
    } : !ctjs.value
    %11 = ctjs.cell_get %1
    ctjs.return %11
  }
}
