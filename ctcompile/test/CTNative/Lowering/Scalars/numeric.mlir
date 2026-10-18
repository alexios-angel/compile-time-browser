// A numeric JavaScript program lowered to native EmitC, checked at the IR level.
//
// The program goes in; EmitC over `double` comes out, with
// no `ctjs` operation and no `!ctjs.value` left in any function that was
// accepted, and a reason on every function that was not. The compilation-unit
// check (Checks/compilation-unit.py) builds it into a binary and compares that
// with the interpreter; this file pins the shape of the IR.
//
// The pipeline below does not run --ctjs-resolve-globals, so calls stay
// unresolved and every function with parameters is refused by name. The
// refusal is the assertion: it names the parameter and what would prove it.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s \
// RUN:   | ctjs-opt --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false observe-globals=true" \
// RUN:   | FileCheck %s
// optimize=false keeps the literal power operation whose runtime lowering
// this test inspects; the default precomputation folds it to a constant.

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

// --- The top level is main, and the globals are static scalars ------------
//
// Undefined until the first store: NaN. Then the loop - lifted to scf.while
// and left there for --convert-scf-to-emitc - and at the end every global is
// printed in the format compilation-unit.py compares, sorted by name.
//
// Operations inside emitc.func print without their dialect prefix at the top
// level of the body and with it inside an scf region, so the patterns below
// name the operation and not the prefix.
//
// CHECK: emitc.include "ctcompile/CTNative/Runtime/ctnative.hpp"
// No initialiser, deliberately: static zero-initialisation gives 0, so a
// global a program never wrote is distinguishable from one it computed a NaN
// into. Initialising these to NaN would make those two cases the same bytes and
// any global whose right answer is NaN impossible to fail in the comparison.
// CHECK: emitc.global static @g_big : !emitc.opaque<"ctnative::nullable_scalar">
// CHECK-NOT: = 0x7FF8000000000000
// CHECK: emitc.global static @g_count : !emitc.opaque<"ctnative::nullable_scalar">
// CHECK: emitc.global static @g_total : !emitc.opaque<"ctnative::nullable_scalar">
// CHECK-LABEL: emitc.func @main() -> i32
// CHECK-NOT: ctjs.
// CHECK: scf.while
// CHECK: cmp lt,
// CHECK: scf.condition
// CHECK: do {
// CHECK: mul
// CHECK: add
// CHECK: div
// CHECK: unary_minus
// `**` is emitted as its guard and then the call: NaN when the base has
// magnitude one and the exponent is not finite, std::pow otherwise.
// Pinning only the bare call would not catch a missing guard.
// CHECK: call_opaque "std::fabs"
// CHECK: call_opaque "std::isfinite"
// CHECK: call_opaque "std::pow"
// CHECK: conditional
// Global reads produce the Number class, then explicitly unwrap for C varargs.
// CHECK: call_opaque "ctnative::global_number"{{.*}} -> !emitc.opaque<"ctnative::js_num">
// CHECK: member_call_opaque {{.*}} "value"() : !emitc.opaque<"ctnative::js_num">, () -> f64
// CHECK: call_opaque "printf"({{.*}}) {ctnative.const_operands = array<i32: 0, 1>} : (!emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()
// CHECK: return %{{.*}} : i32

// --- A function no caller proves is refused, by name ----------------------
//
// `scale` has a parameter and no resolved caller, so its type is boxed and the
// diagnostic says which parameter and what would change that. It stays a
// ctjs.func - the module is not native until every function is.
//
// CHECK: ctjs.func @scale$1
// CHECK-SAME: ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (a closed-world call is Phase 62\C2\BD-A)"
