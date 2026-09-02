// THE FIRST NATIVE ARTEFACT, as IR - part 24 Phase 62½-C.
//
// A numeric JavaScript program goes in; EmitC over `double` comes out, with
// no `ctjs` operation and no `!ctjs.value` left in any function that was
// accepted, and a reason on every function that was not. The compilation-unit
// gate (check-native-unit.cmake) is where this becomes a binary and gets
// compared with the interpreter; this file is where the SHAPE is pinned.
//
// Calls are not resolved here yet - that is --ctjs-resolve-globals, Phase
// 62½-A - so the functions with parameters are refused by name, and the
// refusal is the assertion - it names the parameter and what would prove it.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s \
// RUN:   | ctjs-opt --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s

var total = 0;
var count = 0;
while (count < 10) {
  total = total + count * 0.5;
  count = count + 1;
}
var ratio = total / count;
var neg = -ratio;
var big = 2 ** 31;
function scale(x) { return x * 2; }

// --- THE TOP LEVEL IS main, AND THE GLOBALS ARE STATIC DOUBLES ---------------
//
// Undefined until the first store: NaN. Then the loop - lifted to scf.while
// and left there for --convert-scf-to-emitc - and at the end every global is
// printed under the gate's convention, sorted by name.
//
// OPERATIONS INSIDE emitc.func PRINT WITHOUT THEIR DIALECT PREFIX at the top
// level of the body and WITH it inside an scf region, so the patterns below
// name the operation and not the prefix.
//
// CHECK: emitc.include <"cmath">
// CHECK: emitc.global static @g_big : f64 = 0x7FF8000000000000
// CHECK: emitc.global static @g_count : f64
// CHECK: emitc.global static @g_total : f64
// CHECK-LABEL: emitc.func @main() -> i32
// CHECK-NOT: ctjs.
// CHECK: scf.while
// CHECK: cmp lt,
// CHECK: mul
// CHECK: add
// CHECK: scf.condition
// CHECK: div
// CHECK: unary_minus
// CHECK: call_opaque "std::pow"
// CHECK: call_opaque "printf"({{.*}}) : (!emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()
// CHECK: return %{{.*}} : i32

// --- A FUNCTION NOBODY PROVES IS REFUSED, BY NAME --------------------------
//
// `scale` has a parameter and no resolved caller, so its type is boxed and the
// diagnostic says which parameter and what would change that. It stays a
// ctjs.func - the module is not native until every function is.
//
// CHECK: ctjs.func @scale$1
// CHECK-SAME: ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (a closed-world call is Phase 62\C2\BD-A)"
