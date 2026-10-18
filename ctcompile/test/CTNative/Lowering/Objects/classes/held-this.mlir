// A registry instance's `this` that an arrow keeps. The registry proof
// (Analysis/Inference/Instances.cpp) lets the constructor's or a method's
// `this` reach an arrow's lexical-this cell (ClosureLifting/ArrowThis.cpp)
// when the arrow only reads fields and calls methods through it, and marks the
// cell `ctnative.held_this`. Such an arrow may outlive the call that made it.
// Handed to setTimeout, the host binds a copy of its capture: the instance's
// std::shared_ptr owner, which a method gets from its borrowed `C *` through
// shared_from_this (EmitC/HostCallbacks.cpp). So the instance lives as long as
// the callback, even after the registry drops it.
//
// The checks pin the lift: the method or constructor hands its own `this` to
// the timer, and the callback calls the class's method through its capture.
// Whether the whole program is native also depends on the registry Map's
// identity proof (Analysis/NativeMap.cpp), which these checks leave out.
//
// RUN: split-file %s %t
// RUN: for name in timer constructor owned by-value writes passes stored constructor-stored promise; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/$name.mlir; done
// RUN: FileCheck %s --check-prefix=TIMER --implicit-check-not="this rewrite cannot make direct" --implicit-check-not="does not carry a receiver through" < %t/timer.mlir
// RUN: FileCheck %s --check-prefix=CONSTRUCTOR --implicit-check-not="this rewrite cannot make direct" --implicit-check-not="does not carry a receiver through" < %t/constructor.mlir
// RUN: FileCheck %s --check-prefix=OWNED --implicit-check-not="does not own it by value" < %t/owned.mlir
// RUN: FileCheck %s --check-prefix=BY-VALUE --implicit-check-not=ctnative.held_this --implicit-check-not=ctnative.instance_class < %t/by-value.mlir
// RUN: FileCheck %s --check-prefix=BY-VALUE --implicit-check-not=ctnative.held_this --implicit-check-not=ctnative.instance_class < %t/writes.mlir
// RUN: FileCheck %s --check-prefix=BY-VALUE --implicit-check-not=ctnative.held_this --implicit-check-not=ctnative.instance_class < %t/passes.mlir
// RUN: FileCheck %s --check-prefix=STORED < %t/stored.mlir
// RUN: FileCheck %s --check-prefix=CONSTRUCTOR-STORED < %t/constructor-stored.mlir
// RUN: FileCheck %s --check-prefix=PROMISE < %t/promise.mlir

// timer.js: `later` hands its `this` to setTimeout as the capture of the arrow
// it registers, and the callback calls `grow` on that capture directly.
// TIMER: ctjs.func {{(private )?}}@Widget${{[0-9]+}}({{.*}}ctnative.instance_class = "Widget"
// TIMER: ctjs.func {{(private )?}}@later${{[0-9]+}}(
// TIMER: ctjs.call %{{[0-9]+}}(%{{[0-9]+}}, %{{[0-9]+}}, %arg0) {ctnative.host_callback = @fn$[[CB:[0-9]+]], ctnative.host_timer = "setTimeout"
// TIMER: ctjs.func @fn$[[CB]](
// TIMER: ctjs.call_direct @grow${{[0-9]+}}(%arg3,

// constructor.js: the constructor's `this` is the owner itself, which the
// timer's callback keeps the same way.
// CONSTRUCTOR: ctjs.func {{(private )?}}@Widget${{[0-9]+}}({{.*}}ctnative.instance_class = "Widget"
// CONSTRUCTOR: ctjs.call %{{[0-9]+}}(%{{[0-9]+}}, %{{[0-9]+}}, %arg0) {ctnative.host_callback = @fn$[[CB:[0-9]+]], ctnative.host_timer = "setTimeout"
// CONSTRUCTOR: ctjs.func @fn$[[CB]](
// CONSTRUCTOR: ctjs.call_direct @grow${{[0-9]+}}(%arg3,

// owned.js: the registry class owns a helper by value
// (instances-by-value.mlir), and its timer callback reads that field through
// the `this` it holds. The helper stays an owned member, read in the callback
// as in the method, since the callback keeps the member's owner alive.
// (instances-by-value.mlir's arrow-passed.js is the same read with no
// registry, refused.)
// OWNED: ctjs.func {{(private )?}}@Modal${{[0-9]+}}({{.*}}ctnative.instance_class = "Modal"
// OWNED: ctjs.set_property %arg0[%{{[0-9]+}}], %{{[0-9]+}} {ctnative.owned_instance}
// OWNED: ctjs.call %{{[0-9]+}}(%{{[0-9]+}}, %{{[0-9]+}}, %arg0) {ctnative.host_callback = @fn$[[CB:[0-9]+]], ctnative.host_timer = "setTimeout"
// OWNED: ctjs.func @fn$[[CB]](
// OWNED: ctjs.get_property %arg3[%{{[0-9]+}}] {ctnative.owned_instance

// Negative: an instance with no registry (by-value.js) has no owner the host
// could keep, and neither has a registry class whose arrow writes a field
// through its `this` (writes.js: the field rules would not see the write) or
// passes it on (passes.js: the registry proof would have to follow it). The
// proof refuses those classes, nothing is held, and `later` keeps the
// receiver rule's refusal.
// BY-VALUE: ctjs.func @later${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: it passes `this` to a call this rewrite cannot make direct"

// Negative: the registry class holds its `this` in the cell (stored.js), but
// the arrow is stored, not handed to the host, so no owner is bound and
// `later` is refused. A constructor's arrow stored the same way
// (constructor-stored.js) is a closure value the constructor cannot make.
// STORED: ctjs.func {{(private )?}}@Widget${{[0-9]+}}({{.*}}ctnative.instance_class = "Widget"
// STORED: ctjs.func @later${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: it reaches `ctjs.create_cell`, which slice 1 does not carry a receiver through"
// STORED: ctjs.create_cell %arg0 {{.*}}ctnative.held_this
// CONSTRUCTOR-STORED: ctjs.func {{(private )?}}@Widget${{[0-9]+}}({{.*}}ctnative.instance_class = "Widget"
// CONSTRUCTOR-STORED: ctjs.create_cell %arg0 {{.*}}ctnative.held_this
// CONSTRUCTOR-STORED: ctjs.create_closure %arg2[{{[0-9]+}}] this %{{[0-9]+}} captures %{{[0-9]+}} {ctnative.closure_reason = "it is passed as an argument

// Negative: a promise callback also outlives the method (promise.js), but
// nothing binds an owner for its captures, so only the host's callbacks hold
// an instance and `later` keeps the receiver rule's refusal.
// PROMISE: ctjs.func {{(private )?}}@Widget${{[0-9]+}}({{.*}}ctnative.instance_class = "Widget"
// PROMISE: ctjs.func @later${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: it passes `this` to a call this rewrite cannot make direct"

//--- timer.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        grow() { this.size = this.size + 1; return this.size; }
        later() { setTimeout(() => { this.grow(); }, 0); return this.size; }
        dispose() { registry.delete(this.key); this.size = 0; return 0; }
    }
    new Widget("a", 3);
    const a = registry.get("a") || null;
    let total = 0;
    if (a !== null) { total = a.later() + a.dispose(); }
    return total;
}
var result = run();

//--- constructor.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) {
            this.key = key;
            this.size = size;
            registry.set(key, this);
            setTimeout(() => { this.grow(); }, 0);
        }
        grow() { this.size = this.size + 1; return this.size; }
        dispose() { registry.delete(this.key); this.size = 0; return 0; }
    }
    new Widget("a", 3);
    const a = registry.get("a") || null;
    return a !== null ? a.dispose() + 1 : 0;
}
var result = run();

//--- owned.js
var result = (function () {
    const registry = new Map();
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        reset() { this.width = 5; return 2; }
    }
    class Modal {
        constructor(key) { this.key = key; this._scrollBar = new ScrollBarHelper(); registry.set(key, this); }
        hide() { setTimeout(() => { this._scrollBar.reset(); }, 0); return this._scrollBar.width; }
        dispose() { registry.delete(this.key); return 0; }
    }
    new Modal("a");
    const a = registry.get("a") || null;
    return a !== null ? a.hide() + a.dispose() : 0;
})();

//--- by-value.js
function run() {
    class Widget {
        constructor(size) { this.size = size; }
        grow() { this.size = this.size + 1; return this.size; }
        later() { setTimeout(() => { this.grow(); }, 0); return this.size; }
    }
    const a = new Widget(3);
    return a.later();
}
var result = run();

//--- writes.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        later() { setTimeout(() => { this.size = 0; }, 0); return this.size; }
    }
    new Widget("a", 3);
    const a = registry.get("a") || null;
    return a !== null ? a.later() : 0;
}
var result = run();

//--- passes.js
function keep(widget) { return 0; }
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        later() { setTimeout(() => { keep(this); }, 0); return this.size; }
    }
    new Widget("a", 3);
    const a = registry.get("a") || null;
    return a !== null ? a.later() : 0;
}
var result = run();

//--- stored.js
var kept = [];
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        grow() { this.size = this.size + 1; return this.size; }
        later() { kept.push(() => this.grow()); return this.size; }
    }
    new Widget("a", 3);
    const a = registry.get("a") || null;
    return a !== null ? a.later() : 0;
}
var result = run();

//--- constructor-stored.js
var kept = [];
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) {
            this.key = key;
            this.size = size;
            registry.set(key, this);
            kept.push(() => this.grow());
        }
        grow() { this.size = this.size + 1; return this.size; }
    }
    new Widget("a", 3);
    const a = registry.get("a") || null;
    return a !== null ? a.size : 0;
}
var result = run();

//--- promise.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) { this.key = key; this.size = size; registry.set(key, this); }
        grow() { this.size = this.size + 1; return this.size; }
        later() { Promise.resolve(1).then(() => { this.grow(); }); return this.size; }
    }
    new Widget("a", 3);
    const a = registry.get("a") || null;
    return a !== null ? a.later() : 0;
}
var result = run();
