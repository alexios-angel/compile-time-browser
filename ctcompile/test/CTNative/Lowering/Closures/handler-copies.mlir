// A handler closure handed down a chain of functions to where it is used, as
// an event registry's `on` hands it to `addHandler`.
//
// Each call passes its own closure, so the chain's parameter is no one
// closure, and the closure, passed as an argument, has no call site to move
// its captures to. ClosureLifting/CallSiteCopies.cpp gives each call its own
// copy of `on` and `add`, with the closure's captures passed down in place of
// the closure, and makes the closure in the copy of `add`. Each copy then
// holds one known closure, which it stores in a record and calls directly:
// no closure value and no std::variant reach the C++. The originals, left
// with no caller, are removed after admission and listed as removed. The
// functions are local to a script-level function that runs once, as
// Bootstrap's registry is.
//
// The positive prints what Node and the interpreter print with both
// compilers. The negatives pin three refusals: a handler that is a choice of
// closures, which no copy makes one closure; a handler whose capture is
// written after the call, whose value a copy would take too early (the
// registry would call it with k = 2, where JavaScript sees 12); and a chain
// whose last function boxes the parameter in a cell for a nested closure,
// where no rule lowers the closure the copy would make.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/chain.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/chain.mlir
// RUN: FileCheck %s --check-prefix=CHAIN --implicit-check-not=ctnative.not_native < %t/chain.mlir
// RUN: ctjs-opt %t/chain.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/chain.cpp
// RUN: FileCheck %s --check-prefix=CHAINCPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/chain.cpp
// RUN: %gxx -O2 %t/chain.cpp -o %t/chain-gcc
// RUN: %t/chain-gcc | FileCheck %s --check-prefix=CHAINOUT
// RUN: %clangxx -O2 %t/chain.cpp -o %t/chain-clang
// RUN: %t/chain-clang | FileCheck %s --check-prefix=CHAINOUT
// RUN: %native_reference %t/chain.js | FileCheck %s --check-prefix=CHAINOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/chain.js | FileCheck %s --check-prefix=CHAINOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/choice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CHOICE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/later.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LATER
// RUN: %native_reference %t/later.js | FileCheck %s --check-prefix=LATEROUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/later.js | FileCheck %s --check-prefix=LATEROUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boxed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOXED

// Three copies each of `on` and `add`, recorded against their sources, and the
// originals removed as unreachable rather than left refused.
// CHAIN: ctnative.copies = {add${{[0-9]+}} = "add$2", add${{[0-9]+}} = "add$2", add${{[0-9]+}} = "add$2", on${{[0-9]+}} = "on$3", on${{[0-9]+}} = "on$3", on${{[0-9]+}} = "on$3"}
// CHAIN-SAME: ctnative.unreachable_refusals = [{function = "add$2", reason = "removed: unreachable
// CHAIN-SAME: {function = "on$3", reason = "removed: unreachable
// The handler's own parameter is gone: each copy takes the number and the
// captured `k`.
// CHAIN-COUNT-3: emitc.func @add_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_num">, %arg1: !emitc.opaque<"ctnative::js_num">) ->
// CHAIN-COUNT-3: emitc.func @on_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_num">, %arg1: !emitc.opaque<"ctnative::js_num">) ->
// CHAIN: emitc.func @run_
// CHAIN-NOT: ctjs.func
// Each copy of `add` calls its own handler by name.
// CHAINCPP: ctnative::js_num add_{{[0-9]+}}(ctnative::js_num const v1, ctnative::js_num const v2) {
// CHAINCPP: = fn_[[H1:[0-9]+]](v2, v{{[0-9]+}});
// CHAINCPP: ctnative::js_num add_{{[0-9]+}}(ctnative::js_num const v1, ctnative::js_num const v2) {
// CHAINCPP-NOT: fn_[[H1]](
// CHAINCPP: = fn_[[H2:[0-9]+]](v2, v{{[0-9]+}});
// CHAINCPP: ctnative::js_num add_{{[0-9]+}}(ctnative::js_num const v1, ctnative::js_num const v2) {
// CHAINCPP-NOT: fn_[[H2]](
// CHAINCPP: = fn_{{[0-9]+}}(v2, v{{[0-9]+}});
// CHAINOUT: r=163004

// Negative: the handler is one of two closures, chosen at run time, so no
// copy of the chain makes it one closure, and both closures keep the
// refusal of a value that reaches the choice.
// CHOICE-NOT: ctnative.copies
// CHOICE: closure_reason = "it reaches `scf.yield`, which slice 1 does not lower"
// CHOICE: closure_reason = "it reaches `scf.yield`, which slice 1 does not lower"

// Negative: `k` is written after the call, so its value at the call is not
// the value the stored handler reads, and the call is not copied.
// LATER-NOT: ctnative.copies
// LATER: closure_reason = "it is passed as an argument
// LATEROUT: r=36

// Negative: `add` boxes `h` in a cell, because `later` captures it, so the
// call is not copied and the handler keeps its refusal at the call.
// BOXED-NOT: ctnative.copies
// BOXED: closure_reason = "it is passed as an argument

//--- chain.js
var r = (function () {
    function add(n, h) { const entry = { n: n, h: h }; return entry.h(entry.n); }
    function on(n, h) { return add(n, h); }
    function run(c) {
        const k = c ? 2 : 4;
        return on(5, x => x + k) * 10000 + on(5, x => x * k) * 100 + on(5, x => x - k);
    }
    return run(true) + run(false);
})();

//--- choice.js
var r = (function () {
    function add(n, h) { const entry = { n: n, h: h }; return entry.h(entry.n); }
    function on(n, h) { return add(n, h); }
    function run(c) {
        const k = c ? 2 : 4;
        return on(5, c ? x => x + k : x => x * k);
    }
    return run(true) + run(false);
})();

//--- later.js
var r = (function () {
    function add(reg, h) { reg.click = h; }
    function on(reg, h) { add(reg, h); }
    function run(c) {
        let k = c ? 2 : 4;
        const reg = { click: x => -x };
        on(reg, x => x + k);
        k = k + 10;
        return reg.click(5);
    }
    return run(true) + run(false);
})();

//--- boxed.js
var r = (function () {
    function add(n, h) { const later = () => h(n); return later(); }
    function on(n, h) { return add(n, h); }
    function run(c) {
        const k = c ? 2 : 4;
        return on(5, x => x + k);
    }
    return run(true) + run(false);
})();
