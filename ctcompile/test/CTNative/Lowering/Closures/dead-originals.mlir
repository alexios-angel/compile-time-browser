// The originals that the per-call-site copies leave with no caller
// (ClosureLifting/CallSiteCopies.cpp) are made private, so MLIR's
// DeadCodeAnalysis gives them no callers and the type solve does not read
// their parameters as boxed.
//
// In shared.js every call of `on` gets its own copy of `on` and `add`. The
// originals still call `trigger`, which the copies and the top level call
// too. Left public, they passed it a boxed value, and `trigger` was refused
// "no caller proves it" together with everything that calls it. Private,
// they are dead to the type solve: the program lowers and prints what the
// interpreter prints with both compilers, and the originals are removed after
// admission. noshared.js is the same chain without the shared callee, native
// before and after. In escaped.js a function the program hands to the global
// object still calls `on`, so the originals keep a caller and stay public,
// and `trigger` keeps its refusal.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shared.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/shared.mlir
// RUN: FileCheck %s --check-prefix=SHARED --implicit-check-not=ctnative.not_native < %t/shared.mlir
// RUN: ctjs-opt %t/shared.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/shared.cpp
// RUN: %gxx -O2 %t/shared.cpp -o %t/shared-gcc
// RUN: %t/shared-gcc | FileCheck %s --check-prefix=SHAREDOUT
// RUN: %clangxx -O2 %t/shared.cpp -o %t/shared-clang
// RUN: %t/shared-clang | FileCheck %s --check-prefix=SHAREDOUT
// RUN: %native_reference %t/shared.js | FileCheck %s --check-prefix=SHAREDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/noshared.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=NOSHARED --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escaped.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=ESCAPED

// SHARED: function = "on${{[0-9]+}}", reason = "removed: unreachable
// SHARED-DAG: emitc.func @trigger_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_num">)
// SHARED-DAG: emitc.func @add_{{[0-9]+}}(
// SHARED-DAG: emitc.func @add_{{[0-9]+}}(
// SHAREDOUT: r=28050

// NOSHARED: emitc.func @main()

// ESCAPED: ctjs.func private @trigger${{[0-9]+}}({{.*}}ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it

//--- shared.js
var r = (function () {
    function trigger(m) { return m * 2; }
    function add(n, h, m) { const entry = { n: n, h: h }; return entry.h(entry.n) + trigger(m); }
    function on(n, h, m) { return add(n, h, m); }
    function run(c) {
        const k = c ? 2 : 4;
        return on(5, x => x + k, 3) * 1000 + on(5, x => x * k, 3);
    }
    return run(true) + run(false) + trigger(4);
})();

//--- noshared.js
var r = (function () {
    function add(n, h) { const entry = { n: n, h: h }; return entry.h(entry.n); }
    function on(n, h) { return add(n, h); }
    function run(c) {
        const k = c ? 2 : 4;
        return on(5, x => x + k) * 1000 + on(5, x => x * k);
    }
    return run(true) + run(false);
})();

//--- escaped.js
var r = (function () {
    function trigger(m) { return m * 2; }
    function add(n, h, m) { const entry = { n: n, h: h }; return entry.h(entry.n) + trigger(m); }
    function on(n, h, m) { return add(n, h, m); }
    function run(c) {
        const k = c ? 2 : 4;
        return on(5, x => x + k, 3) * 1000 + on(5, x => x * k, 3);
    }
    function later(h, m) { return on(1, h, m); }
    globalThis.later = later;
    return run(true) + run(false) + trigger(4);
})();
