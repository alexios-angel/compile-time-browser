// overwrite-loop.js lowers fully native through the bounded array-contents proof.
// LiftToSCF recovers the loop's 1/0 guard exactly, and MLIR's own SCF patterns
// move the guarded body out of the loop header, so no poison value, nested header
// control or escape exemption is needed. The native fixture test runs it too.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../../Fixtures/Objects/array/overwrite-loop.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../../Fixtures/Objects/array/overwrite-loop.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s
// CHECK-NOT: ctnative.not_native
// CHECK: emitc.func @main()
// CHECK: emitc.func @overwritten_loop_1()
// CHECK: std::vector<double>
// CHECK: verbatim "{}[static_cast<std::vector<double>::size_type>({})] = {};"
// CHECK: scf.while
// CHECK-NOT: scf.if
// CHECK: scf.condition
// CHECK: do {
// CHECK: call_opaque "ctnative::vec_at"
// CHECK-NOT: ctnative.not_native
