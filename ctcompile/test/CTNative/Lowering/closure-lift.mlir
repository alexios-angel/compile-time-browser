// A CLOSURE CARRIES BY LIFTING - part 24 Phase 59 slice 1, as IR.
//
// Three closures used as VALUES - put in a local, called through it - which is
// the shape the tier refused whole before this slice: the only closure it
// carried was a function DECLARATION, whose create_closure/store_global pair
// lowers to nothing. Here the captured values become extra LEADING parameters
// of the lowered function and each call passes them, so what comes out is
// ordinary C++ over `double` with no functor, no `std::function`, no
// allocation and nothing to own. That last part is the point: this tier has no
// collector, and a carrier that allocated would need one.
//
// THE SIGNATURES ARE THE ASSERTION. `apply` takes no JavaScript parameter and
// lowers to `apply_2(double)`; `at` takes one and lowers to
// `at_6(double, double, double)` with its two captures FIRST. Get the order
// wrong and the program still compiles and prints the wrong number, which is
// why the parameter lists are pinned and not only the calls.
//
// ONE PROGRAM PER FILE, and the functions are appended in source order: the
// importer's `$N` suffix is its position in `program::functions`, so inserting
// a function in the middle renumbers every symbol below it.
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

// --- EVERY FUNCTION IS CLAIMED ----------------------------------------------
//
// The two --implicit-check-not options above are the whole-file half of this:
// a `ctjs.func` anywhere in the output is a function the lowering left behind,
// and a `ctnative.not_native` anywhere is a refusal. Neither may appear.
//
// CHECK: emitc.func @main() -> i32

// --- ONE CAPTURE, AND IT IS THE ONLY PARAMETER ------------------------------
//
// `apply` is written with an empty parameter list and reads `k` out of its
// closure. Lifted, it takes `k` and reads nothing.
//
// CHECK: emitc.func @scaled_1(%arg0: f64) -> f64
// CHECK: call @apply_2(%arg0)
// CHECK: emitc.func @apply_2(%arg0: f64) -> f64
// CHECK: mul %arg0

// --- NO CAPTURE AT ALL ------------------------------------------------------
//
// A nested helper that uses only its own argument. There is nothing to lift,
// and the whole of the work is turning a ctjs.call through a local register
// into a ctjs.call_direct the closed world can type - which is what makes this
// the commonest shape the slice claims in real code.
//
// CHECK: emitc.func @plain_3(%arg0: f64) -> f64
// CHECK: call @twice_4(%arg0)
// CHECK: emitc.func @twice_4(%arg0: f64) -> f64

// --- TWO CAPTURES AND A PARAMETER, AND THE CAPTURES LEAD --------------------
//
// `at(x)` becomes `at_6(m, b, x)`. Both call sites pass the same two captured
// values with a different `x`, and the closure is built once and never
// allocated.
//
// CHECK: emitc.func @line_5(%arg0: f64, %arg1: f64) -> f64
// CHECK: call @at_6(%arg0, %arg1, %{{.*}}) : (f64, f64, f64) -> f64
// CHECK: call @at_6(%arg0, %arg1, %{{.*}}) : (f64, f64, f64) -> f64
// CHECK: emitc.func @at_6(%arg0: f64, %arg1: f64, %arg2: f64) -> f64

// --- AND THE COUNTS, BECAUSE PASS STATISTICS ARE INERT HERE -----------------
//
// The LLVM package this builds against compiles pass statistics out, so
// --mlir-pass-statistics prints an empty report and a test asserting one
// asserts nothing. `report` prints the numbers as a remark, which a test can
// PIN: three closures, three captures between them (1 + 0 + 2), four calls
// rewritten (`at` is called twice), and three cells unboxed.
//
// REPORT: remark: ctnative: lifted 3 closure(s) over 3 capture(s) into 3 function(s), rewrote 4 call(s), unboxed 3 cell(s)
