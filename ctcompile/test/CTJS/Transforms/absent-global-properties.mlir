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
// A module that can reach the run-time compiler through a function's
// `.constructor` and call it (compiled.js) writes no global the VM can rule
// out, so the pass folds nothing there; it marks each such read
// `ctjs.absent_global_property` instead. Native code cannot make that call
// (it is refused where it sits), so the native lowering folds the marked
// reads, records the name in `ctnative.absent_globals`, and precomputation
// drops the branch the probe guards, before the closure lift: the plugin's
// `noConflict` arrow is created nowhere and is removed as unreachable. The
// same module writing `window.jQuery` (compiled-written.js) gets no mark and
// is refused as before, and with eval reachable (compiled-eval.js) the walk
// that finds writes through the window stops early, so nothing is marked.
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
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=MARK
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled-written.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals \
// RUN:   | FileCheck %s --check-prefix=KEPT --implicit-check-not=ctjs.absent_global_property
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled-written.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=WRITTEN --implicit-check-not=ctnative.absent_globals
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled-eval.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals \
// RUN:   | FileCheck %s --check-prefix=KEPT --implicit-check-not=ctjs.absent_global_property

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

// MARK: ctjs.global_escapes = ["a value that may be the run-time compiler escapes into ctjs.call
// MARK-COUNT-2: ctjs.get_property %{{[0-9]+}}[%{{[0-9]+}}] {ctjs.absent_global_property}

// The top level, the library's closure, getjQuery, definePlugin and the
// plugin's arrow all lower; only `build`, which calls the compiler, is refused.
// NATIVE: ctnative.absent_globals = ["jQuery"]
// NATIVE-SAME: ctnative.unreachable_refusals = [{function = "fn$6", reason = "removed: unreachable
// NATIVE: emitc.func @main
// NATIVE: ctjs.func @build$1
// NATIVE: emitc.func @fn_3()

// WRITTEN: ctjs.func private @fn$4
// WRITTEN-SAME: ctnative.not_native = "a closure used as a value: it is stored into something that is not an object literal made here

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

//--- compiled.js
function build(source) { return build.constructor(source); }
var plugins = (function () {
    var registered = 0;
    const getjQuery = () =>
        window.jQuery && !document.body.hasAttribute("data-bs-no-jquery") ? window.jQuery : null;
    const definePlugin = name => {
        const $ = getjQuery();
        if ($) {
            $.fn[name] = () => 1;
            $.fn[name].noConflict = () => $;
            registered = registered + 1;
        }
        return registered;
    };
    return definePlugin("collapse") + definePlugin("modal");
})();

//--- compiled-written.js
window.jQuery = {fn: {}};
function build(source) { return build.constructor(source); }
var plugins = (function () {
    var registered = 0;
    const getjQuery = () =>
        window.jQuery && !document.body.hasAttribute("data-bs-no-jquery") ? window.jQuery : null;
    const definePlugin = name => {
        const $ = getjQuery();
        if ($) {
            $.fn[name] = () => 1;
            $.fn[name].noConflict = () => $;
            registered = registered + 1;
        }
        return registered;
    };
    return definePlugin("collapse") + definePlugin("modal");
})();

//--- compiled-eval.js
var run = eval;
var plugins = (function () {
    var registered = 0;
    const getjQuery = () =>
        window.jQuery && !document.body.hasAttribute("data-bs-no-jquery") ? window.jQuery : null;
    const definePlugin = name => {
        const $ = getjQuery();
        if ($) {
            $.fn[name] = () => 1;
            $.fn[name].noConflict = () => $;
            registered = registered + 1;
        }
        return registered;
    };
    return definePlugin("collapse") + definePlugin("modal");
})();
