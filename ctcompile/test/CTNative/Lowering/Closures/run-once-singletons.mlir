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
// The positive lowers completely and prints the same `r` as Node and the
// interpreter with both compilers. The negatives keep the call through the
// literal: a read that uses the binding as a value, a method that reads
// `this`, and a read in a function started before the literal is written.
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

// Each negative keeps the literal, and its static box says why.
// VALUE: ctnative.singleton_reason = "a read reaches `ctjs.compare` in `{{.*}}`"
// VALUE: ctjs.constant #ctjs.string<"on">
// RECEIVER: ctnative.singleton_reason = "the method `on` reads its receiver, new.target or its own closure"
// RECEIVER: ctjs.constant #ctjs.string<"on">
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
