// Boxed bindings of a function that runs once become static variables.
//
// A bundle wraps its module in a function the page calls once: an IIFE, or a
// UMD wrapper that calls its factory. Every binding the factory boxes then
// exists once. When a closure over such a binding would be refused (here: a
// returned arrow that writes the binding, which needs an owning shared cell,
// as a trial lift of a copy of the module finds), ClosureLifting/RunOnce.cpp
// makes the binding a namespace-scope variable before the lift, and the
// closures over it read and write that variable by name instead of capturing
// it.
//
// The positives lower completely, print the same `r` as Node and the
// interpreter with both compilers, and name the variable after the binding.
// The binding name is debug data in the box's location, so their import is
// printed with --mlir-print-debuginfo, as tools/check/native-claims.py does;
// without it the location does not reach ctjs-opt and the variable is
// `static_binding`.
// The negatives keep the box, and the factory carries the reason it is not
// proved to run once: called twice, its closure value passed somewhere that can
// call it, or calling itself.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/iife.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/iife.mlir
// RUN: FileCheck %s --check-prefix=IIFE --implicit-check-not=ctnative.not_native < %t/iife.mlir
// RUN: ctjs-opt %t/iife.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/iife.cpp
// RUN: %gxx -O2 %t/iife.cpp -o %t/iife-gcc
// RUN: %t/iife-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/iife.cpp -o %t/iife-clang
// RUN: %t/iife-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/iife.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/iife.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/umd.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/umd.mlir
// RUN: FileCheck %s --check-prefix=UMD --implicit-check-not=ctnative.not_native < %t/umd.mlir
// RUN: ctjs-opt %t/umd.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/umd.cpp
// RUN: %gxx -O2 %t/umd.cpp -o %t/umd-gcc
// RUN: %t/umd-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/umd.cpp -o %t/umd-clang
// RUN: %t/umd-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/umd.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/amd.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/amd.mlir
// RUN: FileCheck %s --check-prefix=UMD --implicit-check-not=ctnative.not_native < %t/amd.mlir
// RUN: ctjs-opt %t/amd.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/amd.cpp
// RUN: %gxx -O2 %t/amd.cpp -o %t/amd-gcc
// RUN: %t/amd-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/amd.cpp -o %t/amd-clang
// RUN: %t/amd-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/amd.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/amd.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/declared.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/declared.mlir
// RUN: FileCheck %s --check-prefix=DECLARED --implicit-check-not=ctnative.not_native < %t/declared.mlir
// RUN: ctjs-opt %t/declared.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/declared.cpp
// RUN: %gxx -O2 %t/declared.cpp -o %t/declared-gcc
// RUN: %t/declared-gcc | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: %clangxx -O2 %t/declared.cpp -o %t/declared-clang
// RUN: %t/declared-clang | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: %native_reference %t/declared.js | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/declared.js | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/declared-relayed.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/declared-relayed.mlir
// RUN: FileCheck %s --check-prefix=RELAYED --implicit-check-not=ctnative.not_native --implicit-check-not=static_count < %t/declared-relayed.mlir
// RUN: ctjs-opt %t/declared-relayed.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/declared-relayed.cpp
// RUN: %gxx -O2 %t/declared-relayed.cpp -o %t/declared-relayed-gcc
// RUN: %t/declared-relayed-gcc | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: %clangxx -O2 %t/declared-relayed.cpp -o %t/declared-relayed-clang
// RUN: %t/declared-relayed-clang | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: %native_reference %t/declared-relayed.js | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/declared-sibling.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/declared-sibling.mlir
// RUN: FileCheck %s --check-prefix=SIBLING --implicit-check-not=ctnative.not_native --implicit-check-not=static_count < %t/declared-sibling.mlir
// RUN: ctjs-opt %t/declared-sibling.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/declared-sibling.cpp
// RUN: %gxx -O2 %t/declared-sibling.cpp -o %t/declared-sibling-gcc
// RUN: %t/declared-sibling-gcc | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: %clangxx -O2 %t/declared-sibling.cpp -o %t/declared-sibling-clang
// RUN: %t/declared-sibling-clang | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: %native_reference %t/declared-sibling.js | FileCheck %s --check-prefix=DECLAREDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/twice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=TWICE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ESCAPES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/recursive.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RECURSIVE

// --- A plain IIFE ------------------------------------------------------------
//
// The variable is spelled after the JavaScript binding, declared once at
// namespace scope, and named from both the factory and the returned arrow.
//
// IIFE: emitc.global static @static_value :
// IIFE-NOT: ctnative.cell_reason
// IIFE: get_global @static_value :
// OBSERVATION: r=21

// --- A UMD-shaped wrapper calling its factory once ---------------------------
//
// UMD: emitc.global static @static_value :
// UMD: get_global @static_value :

// --- A UMD wrapper with CommonJS and AMD arms ------------------------------
//
// The factory is also passed to `define` and called in the CommonJS arm. The
// program never stores `exports`, `module` or `define` and the native realm
// does not provide them, so --ctjs-resolve-globals folds their `typeof` reads
// to undefined (CTJS/Transforms/absent-globals.mlir), precomputation drops both
// arms, and the one call left makes the factory run once. Its output is
// checked with the UMD prefix.

// --- A function declaration over a static binding ----------------------------
//
// `bump` is declared in the factory and captures `count`; an arrow inside
// `inner`, inside `run`, calls it. Until `count` is static, the binding rule
// cannot call `bump` directly from the arrow (its capture is not a binding
// that rule erases, and the arrow is made two frames further in than `count`,
// past the one frame a capture is relayed), so the arrow would be refused. Boxes holding a function wait for a second round: with `count`
// static, `bump` is a local function again and every call of it is direct,
// instead of `bump` becoming a static variable called through.
//
// DECLARED-NOT: static_bump
// DECLARED: emitc.global static @static_count :
// DECLARED-NOT: static_bump
// DECLARED: emitc.func @bump_
// DECLARED-NOT: static_bump
// DECLAREDOUT: r=43
//
// When the arrow is made in `run`, one frame in (declared-relayed.js), `run`
// captures `count` and the arrow takes it relayed from `run`
// (canRelaySiblingCapture), so the lift lowers the program as it stands, with
// `count` passed by pointer and no static.
//
// RELAYED: emitc.func @bump_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"ctnative::js_num">>, %arg1: !emitc.opaque<"ctnative::js_num">)
// RELAYED: emitc.func @run_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"ctnative::js_num">>)
// RELAYED: emitc.func @fn_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"ctnative::js_num">>)
//
// When the arrow is made in the factory itself, it captures `count` too
// (canAddSiblingCapture) and passes it on to `bump`, so the lift lowers the
// program as it stands and no static is needed: `count` stays the factory's
// variable, which `bump` and the arrow reach by pointer.
//
// SIBLING: emitc.func @bump_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"ctnative::js_num">>, %arg1: !emitc.opaque<"ctnative::js_num">)
// SIBLING: emitc.func @fn_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"ctnative::js_num">>)

// --- Negative controls -------------------------------------------------------
//
// TWICE: ctnative.not_run_once = "two of its calls can both run"
// TWICE: ctnative.not_native
// ESCAPES: ctnative.not_run_once = "its closure value reaches `ctjs.call`, which can call it any number of times"
// ESCAPES: ctnative.not_native
// RECURSIVE: ctnative.not_run_once = "it reads its own closure as a value (`{{.*}}`), so it can call itself"
// RECURSIVE: ctnative.not_native

//--- iife.js
var r = (function () {
    var value = 0;
    function make() { return () => ++value; }
    const fn = make();
    return fn() + fn() * 10;
})();

//--- umd.js
var r = (function (factory) { return factory(); })(function () {
    var value = 0;
    function make() { return () => ++value; }
    const fn = make();
    return fn() + fn() * 10;
});

//--- amd.js
var r = (function (factory) {
    return typeof exports === "object" && typeof module !== "undefined" ? factory()
        : typeof define === "function" && define.amd ? define(factory)
        : factory();
})(function () {
    var value = 0;
    function make() { return () => ++value; }
    const fn = make();
    return fn() + fn() * 10;
});

//--- declared.js
var r = (function () {
    var count = 0;
    function bump(n) { count = count + n; return count; }
    function run() {
        function inner() {
            const twice = () => bump(1) + bump(2);
            return twice();
        }
        return inner();
    }
    return run() * 10 + count;
})();

//--- declared-relayed.js
var r = (function () {
    var count = 0;
    function bump(n) { count = count + n; return count; }
    function run() {
        const twice = () => bump(1) + bump(2);
        return twice();
    }
    return run() * 10 + count;
})();

//--- declared-sibling.js
var r = (function () {
    var count = 0;
    function bump(n) { count = count + n; return count; }
    const twice = () => bump(1) + bump(2);
    return twice() * 10 + count;
})();

//--- twice.js
var r = (function (factory) { return factory() + factory(); })(function () {
    var value = 0;
    function make() { return () => ++value; }
    const fn = make();
    return fn() + fn() * 10;
});

//--- escapes.js
var keep = [];
var r = (function (factory) { keep.push(factory); return factory(); })(function () {
    var value = 0;
    function make() { return () => ++value; }
    const fn = make();
    return fn() + fn() * 10;
});

//--- recursive.js
var r = (function (factory) { return factory(0); })(function self(depth) {
    var value = 0;
    function make() { return () => ++value; }
    if (depth < 1) { return self(depth + 1); }
    const fn = make();
    return fn() + fn() * 10;
});
