// A class held as a field of an object that is read back by constant keys.
//
// ClosureLifting/ClassStatics.cpp resolves every read of such a field to the
// class, so `t.A.NAME` and `t.A.twice(3)` are direct calls of A's static
// members, and the field then holds A's tag: `struct A_class {}`, which
// carries no data. The same proof follows a library's export object through
// its factory's return and a UMD header's write to `globalThis.lib` into the
// reads `lib.A...` of a later script.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/local.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/local.mlir
// RUN: FileCheck %s --check-prefix=LOCAL --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.class_reason < %t/local.mlir
// RUN: ctjs-opt %t/local.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/local.cpp
// RUN: FileCheck %s --check-prefix=CPP < %t/local.cpp
// RUN: %gxx -O2 %t/local.cpp -o %t/local-gcc
// RUN: %t/local-gcc | FileCheck %s --check-prefix=RUNS
// RUN: %clangxx -O2 %t/local.cpp -o %t/local-clang
// RUN: %t/local-clang | FileCheck %s --check-prefix=RUNS
// RUN: %native_reference %t/local.js | FileCheck %s --check-prefix=RUNS
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/local.js | FileCheck %s --check-prefix=RUNS
//
// The UMD shape: the classes are proved and every `lib.X` read is resolved,
// so no read of the global is left. The program stays refused for what is
// not class work: the factory reaches the header as an argument, and the
// object it returns leaves its frame.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/umd.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UMD --implicit-check-not=ctnative.class_reason --implicit-check-not='ctjs.load_global "lib"'
//
// What the proof refuses (ctnative.static_reason keeps the rewrite's reason
// when the class census refuses the class for the store first).
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EARLY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/passed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PASSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DYNAMIC

// LOCAL: emitc.func @main
// CPP: struct A_class {};
// CPP: A_class A;
// RUNS: r=10
// UMD: ctnative.not_native
//
// A read of the published global before the header writes it.
// EARLY: ctnative.static_reason = "`A` is published as global `lib`, which in `probe_run${{[0-9]+}}` may read before it is written in `fn${{[0-9]+}}`"
//
// The object is passed to a function, which could read any field.
// PASSED: ctnative.static_reason = "`A` is stored into an object that `ctjs.call{{(_direct)?}}` uses in `run${{[0-9]+}}` in `run${{[0-9]+}}`"
//
// A read through a dynamic key could be the class's field.
// DYNAMIC: ctnative.static_reason = "`A` is stored into an object that `ctjs.get_property` uses in `run${{[0-9]+}}` in `run${{[0-9]+}}`"

//--- local.js
function run() {
    class A { constructor(v) { this.v = v; } static get NAME() { return "a"; } static twice(x) { return 2 * x; } }
    const t = { A: A, n: 1 };
    return t.A.NAME.length + t.A.twice(3) + t.n + new A(2).v;
}
var r = run();

//--- umd.js
! function(t, e) {
    "object" == typeof exports && "undefined" != typeof module ? module.exports = e() : "function" == typeof define && define.amd ? define(e) : (t = "undefined" != typeof globalThis ? globalThis : t || self).lib = e()
}(this, function() {
    "use strict";
    class A { constructor(v) { this.v = v; } static get NAME() { return "a"; } }
    class B { constructor(v) { this.v = v; } static get NAME() { return "bb"; } }
    return { A: A, B: B };
});
function probe_run() { return lib.A.NAME.length + lib.B.NAME.length; }
var r = probe_run();

//--- early.js
function probe_run() { return lib.A.NAME.length; }
var early = probe_run();
! function(t, e) {
    "object" == typeof exports && "undefined" != typeof module ? module.exports = e() : "function" == typeof define && define.amd ? define(e) : (t = "undefined" != typeof globalThis ? globalThis : t || self).lib = e()
}(this, function() {
    "use strict";
    class A { constructor(v) { this.v = v; } static get NAME() { return "a"; } }
    return { A: A };
});
var r = probe_run() + early;

//--- passed.js
function keys(o) { return Object.keys(o).length; }
function run() {
    class A { constructor(v) { this.v = v; } static get NAME() { return "a"; } }
    const t = { A: A };
    return t.A.NAME.length + keys(t) + new A(2).v;
}
var r = run();

//--- dynamic.js
function run(k) {
    class A { constructor(v) { this.v = v; } static get NAME() { return "a"; } }
    const t = { A: A };
    return t.A.NAME.length + (t[k] ? 1 : 0) + new A(2).v;
}
var r = run("A");
