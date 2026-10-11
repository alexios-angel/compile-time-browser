// Calls of methods of a run-once function's object literal become direct calls.
//
// A bundle's module factory runs once and keeps tables of functions in
// object literals (Bootstrap's EventHandler: `const P = { on(...) {...}, ... }`)
// that other functions call through: `P.on(...)`. Once the factory's bindings
// are static variables, a binding written once with a literal and read only as
// the receiver of calls through its constant-key fields reaches one function
// per key, so ClosureLifting/Singletons.cpp makes those calls direct and drops
// the literal. The methods' callers then pass known arguments.
//
// A method may also call the table's other methods through `this`
// (Bootstrap's SelectorEngine: `focusableChildren(t) { return this.find(e,
// t)... }`), and may create nested closures (H.getDataAttributes' filter
// arrow); those calls become direct too. Such a table stays a static variable
// even when every closure that reads it lifts (RunOnce.cpp), so a table read
// by an ordinary local function binds as well.
//
// The positives lower completely and print the same `r` as Node and the
// interpreter with both compilers. The negatives keep the call through the
// literal: a read that uses the binding as a value (with readers that lift
// too), a method that reads a field of `this` that is not a method, a method that uses `this` as a value,
// a method that reads `this` while its closure is held outside the literal,
// and a read in a function started before the literal is written.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/table.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/table.mlir
// RUN: FileCheck %s --check-prefix=TABLE --implicit-check-not=ctnative.not_native < %t/table.mlir
// RUN: ctjs-opt %t/table.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/table.cpp
// RUN: %gxx -O2 %t/table.cpp -o %t/table-gcc
// RUN: %t/table-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/table.cpp -o %t/table-clang
// RUN: %t/table-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/table.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/table.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/self.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/self.mlir
// RUN: FileCheck %s --check-prefix=TABLE --implicit-check-not=ctnative.not_native < %t/self.mlir
// RUN: ctjs-opt %t/self.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/self.cpp
// RUN: %gxx -O2 %t/self.cpp -o %t/self-gcc
// RUN: %t/self-gcc | FileCheck %s --check-prefix=SELF
// RUN: %clangxx -O2 %t/self.cpp -o %t/self-clang
// RUN: %t/self-clang | FileCheck %s --check-prefix=SELF
// RUN: %native_reference %t/self.js | FileCheck %s --check-prefix=SELF
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/self.js | FileCheck %s --check-prefix=SELF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/lifted.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/lifted.mlir
// RUN: FileCheck %s --check-prefix=TABLE --implicit-check-not=ctnative.not_native < %t/lifted.mlir
// RUN: ctjs-opt %t/lifted.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/lifted.cpp
// RUN: %gxx -O2 %t/lifted.cpp -o %t/lifted-gcc
// RUN: %t/lifted-gcc | FileCheck %s --check-prefix=LIFTED
// RUN: %clangxx -O2 %t/lifted.cpp -o %t/lifted-clang
// RUN: %t/lifted-clang | FileCheck %s --check-prefix=LIFTED
// RUN: %native_reference %t/lifted.js | FileCheck %s --check-prefix=LIFTED
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/lifted.js | FileCheck %s --check-prefix=LIFTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/lifted-value.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LIFTED-VALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-value.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=THIS-VALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shared.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SHARED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/value.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=VALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/receiver.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RECEIVER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EARLY

// TABLE-NOT: singleton_reason
// TABLE: emitc.func @main
// OBSERVATION: r=3125
// SELF: r=12013
// LIFTED: r=7012

// Each negative keeps the literal, and its static box says why.
// VALUE: ctnative.singleton_reason = "a read reaches `ctjs.compare` in `{{.*}}`"
// VALUE: ctjs.constant #ctjs.string<"on">
// RECEIVER: ctnative.singleton_reason = "the method `on` reads its receiver, new.target or its own closure"
// RECEIVER: ctjs.constant #ctjs.string<"on">
// A table also read as a value stays a capture of the closure that reads it.
// LIFTED-VALUE-NOT: static_cell
// LIFTED-VALUE: ctnative.closure_reason = "it is a method field of an object whose shape is not closed, so `find` cannot become a free function taking that object"
// LIFTED-VALUE: ctnative.not_native = "capture 0 is !ctnative.boxed - no caller proves it
// THIS-VALUE: ctnative.singleton_reason = "the method `on` reads its receiver, new.target or its own closure"
// THIS-VALUE: ctjs.constant #ctjs.string<"on">
// SHARED: ctnative.singleton_reason = "the method `on` reads its receiver, new.target or its own closure"
// SHARED: ctjs.constant #ctjs.string<"on">
// EARLY: ctnative.singleton_reason = "a read in `{{.*}}` can run before the literal is written"
// EARLY: ctjs.constant #ctjs.string<"on">

//--- table.js
var r = (function () {
    var log = 0;
    function note(n) { log = log * 10 + n; }
    const H = {
        on(n) { note(n); return n; },
        off: n => { note(n + 1); return 0; },
    };
    function make() { return () => H.on(1) + H.on(2) + H.off(4); }
    const fn = make();
    return fn() * 1000 + log;
})();

//--- self.js
var r = (function () {
    var log = 0;
    function note(n) { log = log * 10 + n; }
    const R = {
        find(n) { note(n); return n * 2; },
        twice(n) { const double = k => k + k; return double(n); },
        all(n) { return this.find(n) + this.twice(n + 1); },
    };
    function make() { return () => R.all(1) + R.find(3); }
    const fn = make();
    return fn() * 1000 + log;
})();

//--- lifted.js
var r = (function () {
    var log = 0;
    const R = {
        find(n) { log = log * 10 + n; return n * 2; },
        all(n) { return this.find(n) + 1; },
    };
    function run() { return R.all(1) + R.find(2); }
    return run() * 1000 + log;
})();

//--- lifted-value.js
var r = (function () {
    const R = { find(n) { return n * 2; } };
    function run() { return R.find(1) + (R === null ? 1 : 0); }
    return run();
})();

//--- this-value.js
var r = (function () {
    var log = 0;
    const H = { off(n) { return n; }, on(n) { return this === null ? 0 : n; } };
    function make() { return () => H.on(1); }
    const fn = make();
    return fn() * 1000 + log;
})();

//--- shared.js
var r = (function () {
    var log = 0;
    const on = function (n) { return this.off(n); };
    const H = { off(n) { return n; }, on };
    const other = { off(n) { return n + 1; }, on };
    function make() { return () => H.on(1) + other.on(2); }
    const fn = make();
    return fn() * 1000 + log;
})();

//--- value.js
var r = (function () {
    var log = 0;
    const H = { on(n) { log = log * 10 + n; return n; } };
    function make() { return () => H.on(1) + (H === null ? 1 : 0); }
    const fn = make();
    return fn() * 1000 + log;
})();

//--- receiver.js
var r = (function () {
    var log = 0;
    const H = { k: 2, on(n) { log = log * 10 + n; return n + this.k; } };
    function make() { return () => H.on(1); }
    const fn = make();
    return fn() * 1000 + log;
})();

//--- early.js
var r = (function () {
    var log = 0;
    function make() { return () => H.on(1); }
    const fn = make();
    const H = { on(n) { log = log * 10 + n; return n; } };
    return fn() * 1000 + log;
})();
