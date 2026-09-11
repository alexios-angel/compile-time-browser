// Part 24 Phase 63 Step 7: a provenance comment above every generated
// definition. The lowering composes the text as `ctnative.provenance`; the
// fork's emitter prints it, and nothing else reads it.
//
// RUN: ctjs-translate --mlir-to-cpp %s | FileCheck %s

emitc.global static @g_x : f64 = 1.0 {ctnative.provenance = "global x"}
emitc.class @ctn_shape_0 attributes {ctnative.provenance = "object literal at fixture.js:3:13"} {
  emitc.field @x : f64
}
emitc.func @area_1() -> f64 attributes {ctnative.provenance = "function area, fixture.js:3:1"} {
  %k = "emitc.constant"() {value = 2.0 : f64} : () -> f64
  emitc.return %k : f64
}
emitc.func @main() -> i32 attributes {ctnative.provenance = "the top level, fixture.js:1:1"} {
  %z = "emitc.constant"() {value = 0 : i32} : () -> i32
  emitc.return %z : i32
}

// CHECK: // ctcompile: global x
// CHECK-NEXT: static double g_x = 1.0
// CHECK: // ctcompile: object literal at fixture.js:3:13
// CHECK-NEXT: class ctn_shape_0 {
// CHECK: // ctcompile: function area, fixture.js:3:1
// CHECK-NEXT: double area_1() {
// CHECK: // ctcompile: the top level, fixture.js:1:1
// CHECK-NEXT: int32_t main() {
