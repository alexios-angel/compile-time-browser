// The five CTJS types, parsed, verified, printed, and parsed again.
//
// THE SECOND ctjs-opt IS THE TEST. One pass proves the parser accepts the
// syntax; the pair proves the PRINTER emits something the parser accepts,
// which is the property that makes every later test's expected output
// trustworthy. A printer that dropped a type's mnemonic would pass a
// single-pass check against its own output forever.
//
// There are no operations yet, so the types are carried on a func signature -
// which is also a small check that the dialect coexists with the three the
// pipeline registers alongside it.

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

// AND ONE IN A BODY, so the types survive a region rather than only a
// signature: a block argument and a result are different code paths in the
// printer from a function type's operand list.
// CHECK-LABEL: func.func @carries
// CHECK: return %{{.*}} : !ctjs.value
func.func @carries(%v: !ctjs.value) -> !ctjs.value {
  return %v : !ctjs.value
}
