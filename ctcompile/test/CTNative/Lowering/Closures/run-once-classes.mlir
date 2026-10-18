// A class binding of a function that runs once, read only as the class `new`
// constructs in other functions, becomes a static variable.
//
// The importer writes a class's closure into its binding once or twice (the
// declaration and the definition store the same closure). When every read of
// that binding, in its owner or through captures, is the callee of a `new`,
// ClosureLifting/RunOnce.cpp keeps the binding a static variable even though
// every closure over it would lift: a `new` in another function then names
// the class through that variable (Singletons.cpp marks such reads), not
// through a capture of a boxed binding. A binding the program assigns again,
// or reads other than to construct, stays a capture.
//
// In two-sites.js Modal owns its Helper by value (a member of Modal), so
// every `new Helper` lifts and the program is native; the binding is still
// a static variable. In shared.js Modal stores its Helper into two fields, so
// the instance is not owned, the `new` stays, and the function that reads the
// static variable is refused.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/two-sites.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STATIC --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shared.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SHARED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reassigned.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=REASSIGNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/value.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=VALUE

// STATIC: emitc.global static @static_Helper
// SHARED: static_cell = "static_Helper"
// SHARED: not_native = "it reads `Helper`, a static variable of a function that runs once
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

//--- shared.js
var r = (function () {
    class Helper {
        constructor() { this.width = 3; }
        grow(n) { return this.width * n; }
    }
    class Modal {
        constructor() { const h = new Helper(); this.helper = h; this.spare = h; }
        size() { return this.helper.grow(2) + this.spare.width; }
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
