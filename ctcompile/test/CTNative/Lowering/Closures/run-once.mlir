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
// The negatives keep the box, and the factory carries the reason it is not
// proved to run once: called twice, its closure value passed somewhere that can
// call it, or calling itself.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/iife.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/iife.mlir
// RUN: FileCheck %s --check-prefix=IIFE --implicit-check-not=ctnative.not_native < %t/iife.mlir
// RUN: ctjs-opt %t/iife.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/iife.cpp
// RUN: %gxx -O2 %t/iife.cpp -o %t/iife-gcc
// RUN: %t/iife-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/iife.cpp -o %t/iife-clang
// RUN: %t/iife-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/iife.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/iife.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/umd.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/umd.mlir
// RUN: FileCheck %s --check-prefix=UMD --implicit-check-not=ctnative.not_native < %t/umd.mlir
// RUN: ctjs-opt %t/umd.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/umd.cpp
// RUN: %gxx -O2 %t/umd.cpp -o %t/umd-gcc
// RUN: %t/umd-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/umd.cpp -o %t/umd-clang
// RUN: %t/umd-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/umd.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/amd.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/amd.mlir
// RUN: FileCheck %s --check-prefix=UMD --implicit-check-not=ctnative.not_native < %t/amd.mlir
// RUN: ctjs-opt %t/amd.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/amd.cpp
// RUN: %gxx -O2 %t/amd.cpp -o %t/amd-gcc
// RUN: %t/amd-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/amd.cpp -o %t/amd-clang
// RUN: %t/amd-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/amd.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/amd.js | FileCheck %s --check-prefix=OBSERVATION
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
// IIFE: emitc.get_global @static_value
// OBSERVATION: r=21

// --- A UMD-shaped wrapper calling its factory once ---------------------------
//
// UMD: emitc.global static @static_value :
// UMD: emitc.get_global @static_value

// --- A UMD wrapper with CommonJS and AMD arms ------------------------------
//
// The factory is also passed to `define` and called in the CommonJS arm. The
// program never stores `exports`, `module` or `define` and the native realm
// does not provide them, so --ctjs-resolve-globals folds their `typeof` reads
// to undefined (CTJS/Transforms/absent-globals.mlir), precomputation drops both
// arms, and the one call left makes the factory run once. Its output is
// checked with the UMD prefix.

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
