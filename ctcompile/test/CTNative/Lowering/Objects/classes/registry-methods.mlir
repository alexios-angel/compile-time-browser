// A method of a registry-proved class lifts with its receiver even when no
// call of it resolves.
//
// The registry proof (Analysis/Instances.h) owns a class's instances through
// `std::shared_ptr<C>` when its constructor stores `this` into a local Map,
// and it checks that each prototype method runs only on an instance of the
// class. So a method needs no resolved call to type its receiver: type
// inference gives it the class's instance at the method's entry, and the
// closure lift leaves such a method public, because a private function
// nothing calls is dead to that analysis. Only a method that takes nothing
// but its receiver qualifies: a capture or a parameter would have nothing to
// type it, and a boxed value it stored would reach the class's fields. The
// method still lifts only with its class.
//
// In uncalled.js nothing calls `reset`, which used to leave Widget refused
// ("nothing calls it"); the program now runs natively and prints what Node
// and the interpreter print with both compilers, and `reset` is a function
// over the instance.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/uncalled.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/uncalled.mlir
// RUN: FileCheck %s --check-prefix=UNCALLED --implicit-check-not=ctnative.not_native < %t/uncalled.mlir
// RUN: ctjs-opt %t/uncalled.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/uncalled.cpp
// RUN: FileCheck %s --check-prefix=CPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/uncalled.cpp
// RUN: %gxx -O2 %t/uncalled.cpp -o %t/uncalled-gcc
// RUN: %t/uncalled-gcc | FileCheck %s --check-prefix=UNCALLED-RUN
// RUN: %clangxx -O2 %t/uncalled.cpp -o %t/uncalled-clang
// RUN: %t/uncalled-clang | FileCheck %s --check-prefix=UNCALLED-RUN
// RUN: %native_reference %t/uncalled.js | FileCheck %s --check-prefix=UNCALLED-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("result=" + scope.result);' %t/uncalled.js | FileCheck %s --check-prefix=UNCALLED-RUN
//
// The method no call ties to the instances' makers stays native only with
// its class's constructor (closeNativeSet): in refused-maker.js the function
// that makes the instance is refused, and with it the constructor, so
// `reset` is refused too instead of naming a class the shape census never
// saw.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/refused-maker.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=MAKER
//
// What keeps "nothing calls it": a class with no registry (LOCAL), a method
// with a parameter (PARAM), a class the registry proof refuses because a
// method is read as a value (VALUE) or called through a computed key
// (KEYED), and a method whose class does not lift because another method
// needs a call (ALONE).
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/local.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LOCAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/param.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PARAM
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/value.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=VALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/keyed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=KEYED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/alone.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ALONE
//
// The receiver rule (condition 3) still applies on this route. In calls.js
// the registry proof accepts `reset`, whose `this` only writes a field and
// calls `grow`, but `reset` takes a parameter, so the method census does not
// let its `this` name the class's instances (that is for methods this route
// lifts; registry-receivers.mlir), `this.grow()` is not a direct call, and
// `reset` keeps that refusal.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/calls.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CALLS

// UNCALLED-DAG: emitc.class @Widget
// UNCALLED-DAG: emitc.func @reset_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"Widget">>)
// CPP: ctnative::js_num reset_{{[0-9]+}}(Widget* const v1) {
// UNCALLED-RUN: result=5

// MAKER: @reset${{[0-9]+}}({{.*}}ctnative.not_native = "its class's constructor is not native"

// LOCAL: @reset${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: nothing calls it"
// PARAM: @resize${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: nothing calls it"
// VALUE: @reset${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: nothing calls it"
// KEYED: @reset${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: nothing calls it"
// ALONE: @enable${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: its class does not lift: nothing calls it"

// CALLS: @reset${{[0-9]+}}({{.*}}ctnative.instance_method = "Widget"{{.*}}ctnative.not_native = "uses `this` - a method field: it passes `this` to a call this rewrite cannot make direct"

//--- uncalled.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) {
            this.key = key;
            this.size = size;
            registry.set(key, this);
        }
        grow() {
            this.size = this.size + 1;
            return this.size;
        }
        reset() {
            this.size = 0;
            return this.size;
        }
    }
    function getInstance(key) {
        return registry.get(key) || null;
    }
    new Widget("a", 3);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow(); }
    return total + registry.size;
}
var result = run();

//--- refused-maker.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        grow() { this.size = this.size + 1; return this.size; }
        reset() { this.size = 0; return this.size; }
    }
    function getInstance(key) { return registry.get(key) || null; }
    new Widget("a", 3);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow(); }
    return total + registry.size + (Date.now() > 0 ? 1 : 0);
}
var result = run();

//--- local.js
function run() {
    class Widget {
        constructor(size) { this.size = size; }
        grow() { this.size = this.size + 1; return this.size; }
        reset() { this.size = 0; return this.size; }
    }
    const w = new Widget(3);
    return w.grow();
}
var result = run();

//--- param.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        grow() { this.size = this.size + 1; return this.size; }
        resize(n) { this.size = n; return this.size; }
    }
    function getInstance(key) { return registry.get(key) || null; }
    new Widget("a", 3);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow(); }
    return total + registry.size;
}
var result = run();

//--- value.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        grow() { this.size = this.size + 1; return this.size; }
        reset() { this.size = 0; return this.size; }
    }
    function getInstance(key) { return registry.get(key) || null; }
    new Widget("a", 3);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow() + (a.reset === undefined ? 1 : 2); }
    return total + registry.size;
}
var result = run();

//--- keyed.js
function run(name) {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        grow() { this.size = this.size + 1; return this.size; }
        reset() { this.size = 0; return this.size; }
    }
    function getInstance(key) { return registry.get(key) || null; }
    new Widget("a", 3);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow() + a[name](); }
    return total + registry.size;
}
var result = run(["re", "set"].join(""));

//--- alone.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) { this.key = key; this.on = false; registry.set(key, this); }
        enable() { this.on = true; }
        resize(n) { this.size = n; return this.size; }
    }
    function getInstance(key) { return registry.get(key) || null; }
    new Widget("a");
    const a = getInstance("a");
    return a !== null ? registry.size : 0;
}
var result = run();


//--- calls.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        grow() { this.size = this.size + 1; return this.size; }
        reset(n) { this.size = n; return this.grow(); }
    }
    function getInstance(key) { return registry.get(key) || null; }
    new Widget("a", 3);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow(); }
    return total + registry.size;
}
var result = run();
