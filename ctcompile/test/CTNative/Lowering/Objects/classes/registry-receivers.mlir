// `this.m()` inside a method of a registry-proved class.
//
// The registry proof (Analysis/Instances.h) runs every method of the class
// only on an instance, so the method census lets the `this` of a method that
// takes no parameter and captures nothing name the class's instances, as it
// lets an instance read back from the Map name them (Methods.cpp,
// methodCensus). A call `this.m()` there then resolves to the prototype's
// `m` even when no call of the method resolves, and the method lifts on the
// registry route (liftsOnRegistryReceiver). A method with a parameter does
// not lift on that route and keeps its refusal (registry-methods.mlir,
// CALLS).
//
// In chain.js nothing calls `reset`, which calls `this.grow()`, which calls
// `this.bump()` twice: all three lift with the instance as their receiver,
// and the program prints what Node and the interpreter print with both
// compilers. In copies.js nothing calls `refresh`, which hands a closure to
// `this.queue(...)` twice: each call gets its own copy of `queue` and `add`
// (CallSiteCopies.cpp), and `refresh` then lifts too.
//
// The witness keeps the old refusal: in local.js no registry proves the
// class, so nothing names the `this` of the uncalled `reset`, and its
// `this.grow()` stays a call this rewrite cannot make direct.
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
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("result=" + scope.result);' %t/chain.js | FileCheck %s --check-prefix=CHAINOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/copies.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/copies.mlir
// RUN: FileCheck %s --check-prefix=COPIES --implicit-check-not=ctnative.not_native < %t/copies.mlir
// RUN: ctjs-opt %t/copies.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/copies.cpp
// RUN: %gxx -O2 %t/copies.cpp -o %t/copies-gcc
// RUN: %t/copies-gcc | FileCheck %s --check-prefix=COPIESOUT
// RUN: %clangxx -O2 %t/copies.cpp -o %t/copies-clang
// RUN: %t/copies-clang | FileCheck %s --check-prefix=COPIESOUT
// RUN: %native_reference %t/copies.js | FileCheck %s --check-prefix=COPIESOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/local.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LOCAL

// CHAIN-DAG: emitc.func @reset_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"Widget">>)
// CHAIN-DAG: emitc.func @grow_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"Widget">>)
// CHAIN-DAG: emitc.func @bump_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"Widget">>)
// CHAINCPP: Widget
// CHAINOUT: result=10

// COPIES: ctnative.copies = {add${{[0-9]+}} = "add$2", add${{[0-9]+}} = "add$2", queue${{[0-9]+}} = "queue$5", queue${{[0-9]+}} = "queue$5"}
// COPIES-DAG: emitc.func @queue_{{[0-9]+}}(
// COPIES-DAG: emitc.func @queue_{{[0-9]+}}(
// COPIESOUT: result=5

// LOCAL: @reset${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: it passes `this` to a call this rewrite cannot make direct"

//--- chain.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        bump() { this.size = this.size + 1; return this.size; }
        grow() { return this.bump() + this.bump(); }
        reset() { this.size = 0; return this.grow(); }
    }
    function getInstance(key) { return registry.get(key) || null; }
    new Widget("a", 3);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow(); }
    return total + registry.size;
}
var result = run();

//--- copies.js
function run() {
    const registry = new Map();
    function add(n, h) { const entry = { n: n, h: h }; return entry.h(entry.n); }
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        queue(n, h) { return add(n, h); }
        grow() { this.size = this.size + 1; return this.size; }
        refresh() {
            const k = this.size;
            return this.queue(1, x => x + k) * 100 + this.queue(2, x => x * k);
        }
    }
    function getInstance(key) { return registry.get(key) || null; }
    new Widget("a", 3);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow(); }
    return total + registry.size;
}
var result = run();

//--- local.js
function run() {
    class Widget {
        constructor(size) { this.size = size; }
        grow() { this.size = this.size + 1; return this.size; }
        reset() { this.size = 0; return this.grow(); }
    }
    const w = new Widget(3);
    return w.grow();
}
var result = run();
