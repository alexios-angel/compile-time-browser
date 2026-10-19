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
// The positives print what Node and the interpreter print with both
// compilers; in the second, `add` boxes `h` in a cell only because a nested
// arrow reuses the name, and the copy forwards that cell to its reads. The
// negatives pin four refusals: a handler that is a choice of closures, which
// no copy makes one closure; a handler whose capture is written after the
// call, whose value a copy would take too early (the registry would call it
// with k = 2, where JavaScript sees 12); a chain whose last function boxes
// the parameter in a cell a nested closure captures and passes on; and one
// whose last function passes the parameter on to another call, as Bootstrap's
// addHandler passes its handler to its argument normaliser. In the last two
// no rule would lift the closure a copy makes, so copying would only move
// the refusal. A nested closure that only calls the boxed parameter makes the
// handler itself in the copy (ClosureLifting/ExecuteCalls.cpp's relay), so
// that chain is a positive.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/chain.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/chain.mlir
// RUN: FileCheck %s --check-prefix=CHAIN --implicit-check-not=ctnative.not_native < %t/chain.mlir
// RUN: ctjs-opt %t/chain.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/chain.cpp
// RUN: FileCheck %s --check-prefix=CHAINCPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/chain.cpp
// RUN: %gxx -O2 %t/chain.cpp -o %t/chain-gcc
// RUN: %t/chain-gcc | FileCheck %s --check-prefix=CHAINOUT
// RUN: %clangxx -O2 %t/chain.cpp -o %t/chain-clang
// RUN: %t/chain-clang | FileCheck %s --check-prefix=CHAINOUT
// RUN: %native_reference %t/chain.js | FileCheck %s --check-prefix=CHAINOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/chain.js | FileCheck %s --check-prefix=CHAINOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/forward.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/forward.mlir
// RUN: FileCheck %s --check-prefix=FORWARD --implicit-check-not=ctnative.not_native < %t/forward.mlir
// RUN: ctjs-opt %t/forward.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/forward.cpp
// RUN: %gxx -O2 %t/forward.cpp -o %t/forward-gcc
// RUN: %t/forward-gcc | FileCheck %s --check-prefix=FORWARDOUT
// RUN: %clangxx -O2 %t/forward.cpp -o %t/forward-clang
// RUN: %t/forward-clang | FileCheck %s --check-prefix=FORWARDOUT
// RUN: %native_reference %t/forward.js | FileCheck %s --check-prefix=FORWARDOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/forward.js | FileCheck %s --check-prefix=FORWARDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/choice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=CHOICE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/later.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=LATER
// RUN: %native_reference %t/later.js | FileCheck %s --check-prefix=LATEROUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/later.js | FileCheck %s --check-prefix=LATEROUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boxed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/boxed.mlir
// RUN: FileCheck %s --check-prefix=BOXED --implicit-check-not=ctnative.not_native < %t/boxed.mlir
// RUN: ctjs-opt %t/boxed.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/boxed.cpp
// RUN: %gxx -O2 %t/boxed.cpp -o %t/boxed-gcc
// RUN: %t/boxed-gcc | FileCheck %s --check-prefix=BOXEDOUT
// RUN: %clangxx -O2 %t/boxed.cpp -o %t/boxed-clang
// RUN: %t/boxed-clang | FileCheck %s --check-prefix=BOXEDOUT
// RUN: %native_reference %t/boxed.js | FileCheck %s --check-prefix=BOXEDOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/boxed.js | FileCheck %s --check-prefix=BOXEDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boxed-passed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=BOXEDPASSED
// RUN: %native_reference %t/boxed-passed.js | FileCheck %s --check-prefix=BOXEDOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/boxed-passed.js | FileCheck %s --check-prefix=BOXEDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/passed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=PASSED
// RUN: %native_reference %t/passed.js | FileCheck %s --check-prefix=PASSEDOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/passed.js | FileCheck %s --check-prefix=PASSEDOUT

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

// The same chain through a cell that only holds `h`: copied, the cell
// forwarded, and every function native.
// FORWARD: ctnative.copies = {add${{[0-9]+}} = "add$2", add${{[0-9]+}} = "add$2", add${{[0-9]+}} = "add$2", {{.*}}on${{[0-9]+}} = "on$4", on${{[0-9]+}} = "on$4", on${{[0-9]+}} = "on$4"}
// FORWARD-NOT: ctjs.func
// FORWARDOUT: r=326008

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

// `add` boxes `h` in a cell, because `later` captures it; `later` only calls
// it, so the copy of `add` gives `later` the handler's captures and `later`
// makes the handler and calls it, and every function is native.
// BOXED: ctnative.copies = {add${{[0-9]+}} = "add$2", fn${{[0-9]+}} = "fn$3", on${{[0-9]+}} = "on$4"}
// BOXED-NOT: ctjs.func
// BOXEDOUT: r=16

// Negative: `later` passes the boxed `h` on to `apply`, so the call is not
// copied and the handler keeps its refusal at the call.
// BOXEDPASSED-NOT: ctnative.copies
// BOXEDPASSED: closure_reason = "it is passed as an argument

// Negative: `add` hands `h` (through a cell that only holds it) to `norm`,
// which would refuse the closure a copy makes, so the call is not copied and
// the handler keeps its refusal at the call.
// PASSED-NOT: ctnative.copies
// PASSED: closure_reason = "it is passed as an argument
// PASSEDOUT: r=32

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

//--- boxed-passed.js
var r = (function () {
    function apply(f, v) { return f(v); }
    function add(n, h) { const later = () => apply(h, n); return later(); }
    function on(n, h) { return add(n, h); }
    function run(c) {
        const k = c ? 2 : 4;
        return on(5, x => x + k);
    }
    return run(true) + run(false);
})();

//--- forward.js
var r = (function () {
    function add(n, h) {
        const twice = h => h * 2;
        const entry = { n: n, h: h };
        return twice(entry.h(entry.n));
    }
    function on(n, h) { return add(n, h); }
    function run(c) {
        const k = c ? 2 : 4;
        return on(5, x => x + k) * 10000 + on(5, x => x * k) * 100 + on(5, x => x - k);
    }
    return run(true) + run(false);
})();

//--- passed.js
var r = (function () {
    function norm(n, h) { return n > 0 ? h : null; }
    function add(n, h) {
        const twice = h => h * 2;
        const g = norm(n, h);
        return twice(g(n));
    }
    function on(n, h) { return add(n, h); }
    function run(c) {
        const k = c ? 2 : 4;
        return on(5, x => x + k);
    }
    return run(true) + run(false);
})();
