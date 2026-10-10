// Closure lifting in the native lowering.
//
// Three closures are used as values (stored in a local, then called through
// it), not just declared. Their captured values become extra leading
// parameters of the lowered function, and every call passes them, so the
// output is plain C++ over `ctnative::js_num` with no functor, no
// `std::function` and no allocation. That matters because the native target
// has no garbage collector, and a closure that allocated would need one. (A
// plain function declaration needs none of this: its create_closure /
// store_global pair lowers to nothing.)
//
// The signatures are the assertion. `apply` takes no JavaScript parameter and
// lowers to `apply_2(js_num)`; `at` takes one and lowers to
// `at_6(js_num, js_num, js_num)` with its two captures first. Get the order
// wrong and the program still compiles and prints the wrong number, which is
// why the parameter lists are pinned and not only the calls.
//
// Keep this one program and append new functions at the end: the importer's
// `$N` suffix is its position in `program::functions`, so inserting a function
// in the middle renumbers every symbol below it.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --implicit-check-not=ctnative.not_native \
// RUN:                  --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              '--ctnative-lower-to-emitc=report=1' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REPORT

function scaled(k) {
    function apply() { return k * 2; }
    return apply();
}
function plain(n) {
    function twice(v) { return v + v; }
    return twice(n);
}
function line(m, b) {
    function at(x) { return m * x + b; }
    return at(1) + at(2);
}
var a = scaled(21);
var b = plain(4);
var c = line(4, 2);

// --- every function is lowered ----------------------------------------------
//
// The two --implicit-check-not options above are the whole-file half of this:
// a `ctjs.func` anywhere in the output is a function the lowering left behind,
// and a `ctnative.not_native` anywhere is a refusal. Neither may appear.
//
// CHECK: emitc.func @main() -> i32

// --- one capture, and it is the only parameter ------------------------------
//
// `apply` is written with an empty parameter list and reads `k` out of its
// closure. Lifted, it takes `k` and reads nothing.
//
// CHECK: emitc.func @scaled_1(%arg0: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: call @apply_2(%arg0)
// CHECK: emitc.func @apply_2(%arg0: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: mul %arg0

// --- no capture at all ------------------------------------------------------
//
// A nested helper that uses only its own argument. There is nothing to lift;
// the work is turning a ctjs.call through a local register into a
// ctjs.call_direct the closed-world analysis can type, which makes this the
// most common closure shape lowered in real code.
//
// CHECK: emitc.func @plain_3(%arg0: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: call @twice_4(%arg0)
// CHECK: emitc.func @twice_4(%arg0: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">

// --- two captures and a parameter, and the captures come first --------------
//
// `at(x)` becomes `at_6(m, b, x)`. Both call sites pass the same two captured
// values with a different `x`, and the closure is built once and never
// allocated.
//
// CHECK: emitc.func @line_5(%arg0: !emitc.opaque<"ctnative::js_num">, %arg1: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: call @at_6(%arg0, %arg1, %{{.*}}) : (!emitc.opaque<"ctnative::js_num">, !emitc.opaque<"ctnative::js_num">, !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: call @at_6(%arg0, %arg1, %{{.*}}) : (!emitc.opaque<"ctnative::js_num">, !emitc.opaque<"ctnative::js_num">, !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: emitc.func @at_6(%arg0: !emitc.opaque<"ctnative::js_num">, %arg1: !emitc.opaque<"ctnative::js_num">, %arg2: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">

// --- the counts, because pass statistics are compiled out -------------------
//
// The LLVM package this builds against compiles pass statistics out, so
// --mlir-pass-statistics prints an empty report and a test asserting one
// asserts nothing. `report` prints the numbers as a remark, which a test can
// pin: three closures, three captures between them (1 + 0 + 2), four calls
// rewritten (`at` is called twice), and three cells unboxed.
//
// REPORT: remark: ctnative: lifted 3 closure(s) over 3 capture(s) into 3 function(s), rewrote 4 call(s), unboxed 3 cell(s)
