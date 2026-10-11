// What the first class slice refuses, one program per reason.
//
// The class census (ClosureLifting/Classes.cpp) proves a class whose prototype
// and constructor are touched only by its own definitions and setup. This
// slice lowers a class without `extends`, static members or accessors; each
// of those is refused on the constructor's closure with the reason, so the
// next slice has a list to work from. A method read as a value keeps the
// method rule's own refusal, and an instance that leaves its frame keeps the
// constructor rule's.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/extends.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EXTENDS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/static.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STATIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/getter.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=GETTER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/method-value.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=METHODVALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reserved.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RESERVED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/returned.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RETURNED

// A class that extends another is flattened (ClosureLifting/ClassInheritance.cpp)
// and lowers like any other; inheritance-refusals.mlir lists what it refuses.
// EXTENDS: emitc.class @Square
// EXTENDS-NOT: ctnative.class_reason
// STATIC: ctnative.class_reason = "it has static method `make`; static members are the next slice"
// GETTER: ctnative.class_reason = "it has getter `size`; accessors are the next slice"
// METHODVALUE: {{(observed|read) as a value}}
// RETURNED: ctnative.not_native
//
// The importer gives the VM's class helpers their old global names
// (`__ctbrowser_class_defined` and the rest; CTJS/Import/EngineGlobals.h). The
// VM keeps the helpers apart, so a script's own global of that name changes
// nothing about its classes, but in the IR it would read as the helper, so
// native lowering refuses the script's use by name.
// RESERVED: ctjs.func {{.*}}@_script_$0
// RESERVED-SAME: ctnative.not_native = "uses the engine-reserved name `__ctbrowser_class_defined`"

//--- extends.js
function run() {
    class Shape { constructor(w) { this.w = w; } }
    class Square extends Shape { constructor(w) { super(w); this.h = w; } }
    return new Square(3).h;
}
var r = run();

//--- static.js
function run() {
    class Box { constructor(v) { this.v = v; } static make() { return 1; } }
    return new Box(2).v;
}
var r = run();

//--- getter.js
function run() {
    class Box { constructor(v) { this.v = v; } get size() { return this.v; } }
    return new Box(2).v;
}
var r = run();

//--- method-value.js
function run() {
    class Box { constructor(v) { this.v = v; } read() { return this.v; } }
    const b = new Box(2);
    const f = b.read;
    return b.v + (f ? 1 : 0);
}
var r = run();

//--- returned.js
function make() {
    class Box { constructor(v) { this.v = v; } }
    return new Box(2);
}
var r = make().v;

//--- reserved.js
__ctbrowser_class_defined = 1;
function run() {
    class Box { constructor(v) { this.v = v; } }
    return new Box(2).v;
}
var r = run();
