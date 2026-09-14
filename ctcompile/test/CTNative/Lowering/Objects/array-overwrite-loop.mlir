// Preserve this called source as the next contents-to-native boundary.
// A dominating direct array now has a bounded read-only contents certificate,
// but the importer puts the body in an scf.if inside the scf.while header.
// That nested control (and its poison/arith flag transport) is not certified.
// Do not weaken the complete contents proof to accept only the initial write.
// Measured before/after the direct-array proof: 0/2 native, both policies.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../Fixtures/Objects/array-overwrite-loop.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../Fixtures/Objects/array-overwrite-loop.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s
// CHECK-NOT: emitc.func
// CHECK: ctjs.func @_script_$0
// CHECK-SAME: ctnative.not_native
// CHECK: ctjs.func private @overwritten_loop$1
// CHECK-SAME: ctnative.not_native = "an array literal written through an index
// CHECK: ctjs.set_property
// CHECK: scf.while
// CHECK: scf.if
// CHECK: scf.condition
// CHECK-NOT: emitc.func
