// Closure lifting across nesting levels: a capture filled from the enclosing
// closure's slot is lifted to a parameter.
//
// `deep` names `k`, which belongs to `outer` and not to `inner` or `mid`. The
// bytecode compiler boxes `k` once, in `outer`'s frame; `mid` captures the cell
// as a from_parent_local descriptor, and `inner` and `deep` each say "fill my
// slot from the enclosing closure's slot 0". The importer leaves an `undefined`
// placeholder in those two operands and writes the 0 on `enclosing_indices`.
// Taken alone, those captures would be refused ("capture 0 is not a cell of
// this frame"), so the lift iterates to a fixpoint. Round one lifts `mid`: its
// capture is a cell of `outer`'s frame and nothing writes it, which puts
// `ctnative.captures` on `mid` and makes its upvalue 0 the entry-block argument
// %arg3. Round two admits `inner` on exactly that: its slot names upvalue 0 of
// a lifted function, inside that function's capture range. Round three admits
// `deep` the same way.
//
// The signatures are the assertion. Every level lowers to a function of one
// js_num, the value of `k`, and every call passes it straight through: the
// parameter, not a cell or a load. A lift that passed the wrong thing would
// verify, compile and print the wrong number, which is why the counts and the
// signatures are pinned here and the answer in Fixtures/Closures/nested-closure.js.
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

// --- every function is lowered ----------------------------------------------
//
// CHECK: emitc.func @main() -> i32

// --- one value, passed down three levels ------------------------------------
//
// CHECK: emitc.func @outer_1(%arg0: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: call @mid_2(%arg0)
// CHECK: emitc.func @mid_2(%arg0: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: call @inner_3(%arg0)
// CHECK: emitc.func @inner_3(%arg0: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: call @deep_4(%arg0)
// CHECK: emitc.func @deep_4(%arg0: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: mul %arg0

// --- the counts -------------------------------------------------------------
//
// Three closures lifted, one capture each (the same value at every level),
// three calls rewritten, and one cell unboxed: `k` is boxed once, in `outer`,
// and the two inner levels never had a cell to unbox.
//
// REPORT: remark: ctnative: lifted 3 closure(s) over 3 capture(s) into 3 function(s), rewrote 3 call(s), unboxed 1 cell(s)
