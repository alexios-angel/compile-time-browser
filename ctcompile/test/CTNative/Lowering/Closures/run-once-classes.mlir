// A class binding of a function that runs once, read only as the class `new`
// constructs in other functions, becomes a static variable.
//
// The importer writes a class's closure into its binding once or twice (the
// declaration and the definition store the same closure). When every read of
// that binding, in its owner or through captures, is the callee of a `new`,
// ClosureLifting/RunOnce.cpp keeps the binding a static variable even though
// every closure over it would lift: a `new` in another function then names
// the class through that variable (Constructors.cpp marks such reads), not
// through a capture of a boxed binding. A binding the program assigns again,
// or reads other than to construct, stays a capture.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/two-sites.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STATIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reassigned.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=REASSIGNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/value.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=VALUE

// STATIC: static_cell = "static_Helper"
// STATIC: not_native = "it reads `Helper`, a static variable of a function that runs once
// REASSIGNED-NOT: static_cell
// VALUE-NOT: static_cell

//--- two-sites.js
var r = (function () {
    class Helper {
        constructor() { this.width = 3; }
        grow(n) { return this.width * n; }
    }
    class Modal {
        constructor() { this.helper = new Helper(); }
        size() { return this.helper.grow(2); }
    }
    function show() { return new Helper().grow(5); }
    return new Modal().size() * 100 + show();
})();

//--- reassigned.js
var r = (function () {
    let Helper = class {
        constructor() { this.width = 3; }
    };
    function show() { return new Helper().width; }
    const before = show();
    Helper = class {
        constructor() { this.width = 4; }
    };
    return before * 10 + show();
})();

//--- value.js
var r = (function () {
    class Helper {
        constructor() { this.width = 3; }
    }
    function show() { return new Helper().width; }
    function same(k) { return k === Helper ? 1 : 0; }
    return show() + same(Helper);
})();
