// A CAPTURE FILLED FROM THE ENCLOSING CLOSURE CARRIES BY LIFTING - part 24
// Phase 59 slice 1b, as IR.
//
// `deep` names `k`, which belongs to `outer` and not to `inner` or `mid`. The
// bytecode compiler boxes `k` once, in `outer`'s frame; `mid` captures the cell
// as a from_parent_local descriptor, and `inner` and `deep` each say "fill my
// slot from the enclosing closure's slot 0". The importer writes those two
// operands as a ctjs.load_upvalue of the enclosing function's own closure, and
// slice 1 refused them: "capture 0 is not a cell of this frame". Now the lift is
// a fixpoint. Round one lifts `mid` - its capture IS a cell of `outer`'s frame
// and nothing writes it - and rewrites every ctjs.load_upvalue in `mid` to its
// new parameter, `inner`'s capture operand included. Round two admits `inner`
// on exactly that: its operand is an entry-block argument of a lifted function,
// inside that function's capture range. Round three admits `deep` the same way.
//
// THE SIGNATURES ARE THE ASSERTION. Every level lowers to a function of ONE
// double, the value of `k`, and every call passes it straight through - the
// parameter, not a cell, not a load. A lift that passed the wrong thing would
// verify, compile and print the wrong number, which is why the counts and the
// signatures are pinned here and the answer in native-nested-closure-fixture.js.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --implicit-check-not=ctnative.not_native \
// RUN:                  --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              '--ctnative-lower-to-emitc=report=1' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REPORT

function outer(k) {
    function mid() {
        function inner() {
            function deep() { return k * 3; }
            return deep() + 1;
        }
        return inner() * 2;
    }
    return mid();
}
var r = outer(5);

// --- EVERY FUNCTION IS CLAIMED ----------------------------------------------
//
// CHECK: emitc.func @main() -> i32

// --- ONE VALUE, PASSED DOWN THREE LEVELS ------------------------------------
//
// CHECK: emitc.func @outer_1(%arg0: f64) -> f64
// CHECK: call @mid_2(%arg0)
// CHECK: emitc.func @mid_2(%arg0: f64) -> f64
// CHECK: call @inner_3(%arg0)
// CHECK: emitc.func @inner_3(%arg0: f64) -> f64
// CHECK: call @deep_4(%arg0)
// CHECK: emitc.func @deep_4(%arg0: f64) -> f64
// CHECK: mul %arg0

// --- AND THE COUNTS ---------------------------------------------------------
//
// Three closures lifted, one capture each - the same value at every level -
// three calls rewritten, and ONE cell unboxed: `k` is boxed once, in `outer`,
// and the two inner levels never had a cell to unbox.
//
// REPORT: remark: ctnative: lifted 3 closure(s) over 3 capture(s) into 3 function(s), rewrote 3 call(s), unboxed 1 cell(s)
