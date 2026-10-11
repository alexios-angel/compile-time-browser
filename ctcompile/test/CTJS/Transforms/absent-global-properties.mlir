// --ctjs-resolve-globals folds a read of an absent global through the global
// object, `window.NAME`, to `undefined`.
//
// In a module where nothing writes the globals table dynamically (clause 5),
// a name the program never binds and the native realm does not provide is no
// property of the window either, so `window.jQuery` is undefined. Bootstrap's
// getjQuery() is the shape this is for: once its probe folds, the helper
// returns null, and the caller's `if ($) { ... }` that registers a jQuery
// plugin drops out of the native lowering, taking the plugin's closure with it.
//
// Each negative control keeps the read: a module that can write any global
// (eval reachable), a name written through the window, a name declared with
// `var`, a name the host provides, and a name every object inherits from
// Object.prototype.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/plugin.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=FOLD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/plugin.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DROP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/written.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/declared.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/host.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=HOST
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inherited.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=INHERITED

// FOLD: {name = "jQuery", reason = "absent: never stored and not provided by the native realm; 2 read(s) through the global object folded to undefined", resolved = "none", stores = 0 : i32}
// FOLD-NOT: ctjs.constant #ctjs.string<"jQuery">
// FOLD: ctjs.func {{.*}}@getjQuery$1
// FOLD-NOT: ctjs.load_global "window"
// FOLD: ctjs.return

// The plugin branch is gone: definePlugin calls getjQuery and returns the
// count, and nothing creates or calls the plugin's function.
// DROP: emitc.func @definePlugin_{{[0-9]+}}(
// DROP-NEXT: static_cast<void>
// DROP-NEXT: call @getjQuery_{{[0-9]+}}()
// DROP-NOT: call
// DROP: return

// KEPT: ctjs.constant #ctjs.string<"jQuery">

// HOST: ctjs.constant #ctjs.string<"innerWidth">

// INHERITED: ctjs.constant #ctjs.string<"hasOwnProperty">

//--- plugin.js
var registered = 0;
function getjQuery() {
    return window.jQuery && !document.body.hasAttribute("data-bs-no-jquery") ? window.jQuery : null;
}
function definePlugin(name) {
    var $ = getjQuery();
    if ($) {
        $.fn[name] = function () {
            return this.each(function () {
                return 1;
            });
        };
        registered = registered + 1;
    }
    return registered;
}
var plugins = definePlugin("collapse") + definePlugin("modal");

//--- dynamic.js
function getjQuery() {
    return window.jQuery ? window.jQuery : null;
}
var found = getjQuery() === null;
var run = eval;

//--- written.js
window.jQuery = {fn: {}};
function getjQuery() {
    return window.jQuery ? window.jQuery : null;
}
var found = getjQuery() !== null;

//--- declared.js
var jQuery = {fn: {}};
function getjQuery() {
    return window.jQuery ? window.jQuery : null;
}
var found = getjQuery() !== null;

//--- host.js
var wide = window.innerWidth > 0;

//--- inherited.js
var owns = window.hasOwnProperty("x");
