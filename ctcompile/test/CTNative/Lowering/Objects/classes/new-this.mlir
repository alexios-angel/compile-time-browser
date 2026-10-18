// `new this(...)` in a static member constructs the class the member belongs
// to. Flattening (ClosureLifting/ClassInheritance.cpp) gives each concrete
// class its own copy of an inherited static; the static-member rewrite
// (ClassStatics.cpp) proves that copy's `this` is that class, because the
// copy is reached only as `C.m(...)` or `this.m(...)` from another static of
// C, and marks the `new` (ctnative.class_construct); the constructor lift
// (Constructors.cpp, Rewrite.cpp) then builds the instance in the static
// member's frame.
//
// RUN: split-file %s %t
//
// Base, Alert and Button, Bootstrap's shape: each subclass constructor calls
// super and writes its own field, and the inherited static builds `new this`
// and calls a method on it. The whole program is native, and GCC, Clang, the
// interpreter and Node print the same value.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/components.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/components.mlir
// RUN: FileCheck %s --check-prefix=COMPONENTS --implicit-check-not=ctnative.not_native < %t/components.mlir
// RUN: ctjs-opt %t/components.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/components.cpp
// RUN: %gxx -O2 %t/components.cpp -o %t/components-gcc
// RUN: %t/components-gcc | FileCheck %s --check-prefix=RUN-COMPONENTS
// RUN: %clangxx -O2 %t/components.cpp -o %t/components-clang
// RUN: %t/components-clang | FileCheck %s --check-prefix=RUN-COMPONENTS
// RUN: %native_reference %t/components.js | FileCheck %s --check-prefix=RUN-COMPONENTS
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/components.js | FileCheck %s --check-prefix=RUN-COMPONENTS
//
// COMPONENTS-DAG: emitc.class @Alert
// COMPONENTS-DAG: emitc.class @Button
// COMPONENTS-DAG: emitc.func @measure_Alert_{{[0-9]+}}
// COMPONENTS-DAG: emitc.func @measure_Button_{{[0-9]+}}
// RUN-COMPONENTS: r=31042
//
// inheritance.mlir's statics.js, with the instance used where `new this` made
// it: name() reads `this.constructor.NAME`, which the static rewrite resolves
// per class (name_D for D), and describe() builds `new this` and calls it.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/names.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/names.mlir
// RUN: FileCheck %s --check-prefix=NAMES --implicit-check-not=ctnative.not_native < %t/names.mlir
// RUN: ctjs-opt %t/names.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/names.cpp
// RUN: %gxx -O2 %t/names.cpp -o %t/names-gcc
// RUN: %t/names-gcc | FileCheck %s --check-prefix=RUN-NAMES
// RUN: %clangxx -O2 %t/names.cpp -o %t/names-clang
// RUN: %t/names-clang | FileCheck %s --check-prefix=RUN-NAMES
// RUN: %native_reference %t/names.js | FileCheck %s --check-prefix=RUN-NAMES
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/names.js | FileCheck %s --check-prefix=RUN-NAMES
// NAMES-DAG: emitc.func @name_D_{{[0-9]+}}
// NAMES-DAG: emitc.func @describe_D_{{[0-9]+}}
// RUN-NAMES: r={{"?dp"?$}}
//
// getOrCreateInstance returns the instance it builds. The class is proved,
// the `new this` is lifted, and each copy returns its class by value
// (Analysis/Inference/Records.cpp): the caller only reads `k`. The program is
// native and prints the interpreter's value; instances-by-value.mlir's
// twice.js keeps a returned instance that is passed on refused.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/get-or-create.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/get-or-create.mlir
// RUN: FileCheck %s --check-prefix=GETORCREATE --implicit-check-not=ctnative.not_native < %t/get-or-create.mlir
// RUN: ctjs-opt %t/get-or-create.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/get-or-create.cpp
// RUN: %gxx -O2 %t/get-or-create.cpp -o %t/get-or-create-gcc
// RUN: %t/get-or-create-gcc | FileCheck %s --check-prefix=GETORCREATE-RUN
// RUN: %clangxx -O2 %t/get-or-create.cpp -o %t/get-or-create-clang
// RUN: %t/get-or-create-clang | FileCheck %s --check-prefix=GETORCREATE-RUN
// RUN: %native_reference %t/get-or-create.js | FileCheck %s --check-prefix=GETORCREATE-RUN
// GETORCREATE-NOT: ctnative.class_reason
// GETORCREATE-DAG: emitc.func @getOrCreateInstance_Alert_{{[0-9]+}}
// GETORCREATE-DAG: emitc.func @getOrCreateInstance_Button_{{[0-9]+}}
// GETORCREATE-RUN: r=12
//
// The static read as a value and called as a plain function: its `this` is
// then undefined, not the class, so the class keeps its static members.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/extracted.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EXTRACTED
// EXTRACTED: ctnative.class_reason = "it has static method `getOrCreateInstance`; its static method is called with another receiver in `run${{[0-9]+}}`"
//
// `new this.constructor` in an instance method has no such proof: an
// instance's `constructor` is the class only while nothing replaces it.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constructor.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CONSTRUCTOR
// CONSTRUCTOR: ctnative.class_reason = "it has static method `make`; it constructs `new this` or `new this.constructor` in `clone${{[0-9]+}}`"
//
// The constructor captures a value of the frame that defines the class, which
// a `new this` in a static member's own frame cannot reach.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/captures.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CAPTURES
// CAPTURES: ctnative.closure_reason = "it is constructed by `new this` in `measure${{[0-9]+}}`, outside the frame that defines it, and its constructor captures 1 value(s) from that frame"

//--- components.js
function run() {
    class Base {
        constructor(v) { this.v = v; }
        size() { return this.v * 10; }
        static measure(v) { const made = new this(v); return made.size() + made.k; }
    }
    class Alert extends Base { constructor(v) { super(v); this.k = 1; } }
    class Button extends Base { constructor(v) { super(v); this.k = 2; } }
    return Alert.measure(3) * 1000 + Button.measure(4);
}
var r = run();

//--- names.js
function run() {
    class P {
        static get NAME() { return "p"; }
        name() { return this.constructor.NAME; }
        static describe() { const made = new this(); return made.name(); }
    }
    class D extends P { static get NAME() { return "d"; } }
    return D.describe() + P.describe();
}
var r = run();

//--- get-or-create.js
function run() {
    class Base {
        constructor(e) { this.e = e; }
        static getOrCreateInstance(e) { return new this(e); }
    }
    class Alert extends Base { constructor(e) { super(e); this.k = 1; } }
    class Button extends Base { constructor(e) { super(e); this.k = 2; } }
    return Alert.getOrCreateInstance(3).k * 10 + Button.getOrCreateInstance(4).k;
}
var r = run();

//--- extracted.js
function run() {
    class Base {
        constructor(e) { this.e = e; }
        static getOrCreateInstance(e) { return new this(e); }
    }
    const f = Base.getOrCreateInstance;
    return f(1).e;
}
var r = run();

//--- constructor.js
function run() {
    class Box {
        constructor(v) { this.v = v; }
        clone() { return new this.constructor(this.v + 1); }
        static make(v) { return v * 2; }
    }
    return new Box(2).clone().v + Box.make(3);
}
var r = run();

//--- captures.js
function run() {
    let scale = 2;
    scale += 1;
    class Box {
        constructor(v) { this.v = v * scale; }
        static measure(v) { const made = new this(v); return made.v; }
    }
    return Box.measure(5);
}
var r = run();
