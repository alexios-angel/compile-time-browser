// Each static variable of a run-once function gets its own name.
//
// closureLifter::run() makes run-once bindings static in two calls of
// makeRunOnceCellsStatic, data first and then boxes holding a function. Static
// cells are named by an attribute, not a symbol, so the second call must see
// the names the first one gave. When it did not, `count` and `step` below
// both became `static_binding`, and every rule that groups cells by name saw
// one variable written twice: ClosureLifting/Singletons.cpp then refused the
// table `Data`, so `Data.twice(...)` stayed an indirect call.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/names.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --implicit-check-not=ctnative.singleton_reason

// The module function owns three static variables, each under its own name.
// CHECK-LABEL: ctjs.func private @fn$1(
// CHECK: ctjs.create_cell {{.*}}ctnative.static_cell = "static_binding",
// CHECK: ctjs.create_cell {{.*}}ctnative.static_cell = "static_binding_2",
// CHECK: ctjs.create_cell {{.*}}ctnative.static_cell = "static_binding_3",

// `twice` is called directly.
// CHECK-LABEL: ctjs.func private @fn$2(
// CHECK: ctjs.call_direct @fn$2(

//--- names.js
var api = (function () {
    const Data = {
        twice(n) {
            return n * 2;
        }
    };
    let count = 0;
    let step = function (n) { return n + count; };
    return {
        run(n) {
            count = count + 1;
            step = function (m) { return m - count; };
            return Data.twice(step(n));
        }
    };
})();
var r = api.run(5);
