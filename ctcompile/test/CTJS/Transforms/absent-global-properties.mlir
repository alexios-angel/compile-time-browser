// --ctjs-resolve-globals folds a read of an absent global through the global
// object, `window.NAME`, to `undefined`, on a whole page (--whole-page,
// WholePage.h).
//
// In a whole page where nothing writes the globals table dynamically
// (clause 5), a name the program never binds and the native realm does not
// provide is no property of the window either, so `window.jQuery` is
// undefined. Bootstrap's getjQuery() is the shape this is for: once its probe
// folds, the helper returns null, and the caller's `if ($) { ... }` that
// registers a jQuery plugin drops out of the native lowering, taking the
// plugin's closure with it.
//
// Each negative control keeps the read: a module that can write any global
// (eval reachable), a name written through the window, a name declared with
// `var`, a name the host provides, a name every object inherits from
// Object.prototype, a name the page names an element by, and a module that
// is only part of its page.
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
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/plugin.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PLUGIN-VM
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/bounded-plugin.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=FOLD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/plugin.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DROP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/written.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/declared.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/host.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=HOST
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inherited.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=INHERITED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=MARK
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled-written.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals \
// RUN:   | FileCheck %s --check-prefix=KEPT --implicit-check-not=ctjs.absent_global_property
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled-written.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=WRITTEN --implicit-check-not=ctnative.absent_globals
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiled-eval.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals \
// RUN:   | FileCheck %s --check-prefix=KEPT --implicit-check-not=ctjs.absent_global_property
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/plugin.js --whole-page --whole-page-named=jQuery 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ELEMENT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/plugin.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ELEMENT
// RUN: for name in named-id named-name named-markup named-prefix; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals \
// RUN:     | FileCheck %s --check-prefix=NAMED; done
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/other-name.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=OTHER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/known-construct.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=OTHER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unknown-name.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=UNKNOWN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/pruned-naming.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   > %t/pruned-naming.mlir
// RUN: FileCheck %s --check-prefix=PERSISTENT --implicit-check-not=ctnative.not_native < %t/pruned-naming.mlir
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reflect-alias.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ALIAS
// RUN: for name in opaque-dom opaque-receiver opaque-document opaque-window-alias opaque-construct opaque-spread opaque-construct-spread opaque-tag; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=OPAQUE; done
// RUN: for name in object-constructor descriptor-setter; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:     | FileCheck %s --check-prefix=CLAUSE5; done
// RUN: for name in window-reflect computed-reflect; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PROPERTY-ALIAS; done
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/no-absence.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   > %t/no-absence.mlir
// RUN: FileCheck %s --check-prefix=NO-ABSENCE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.naming_assumed_absent < %t/no-absence.mlir
// RUN: ctjs-opt %t/no-absence.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' \
// RUN:   | ctjs-translate --mlir-to-cpp | FileCheck %s --check-prefix=CPP
// RUN: ctjs-opt %t/pruned-naming.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' -o %t/pruned-output.mlir
// RUN: not ctjs-translate --mlir-to-cpp %t/pruned-output.mlir 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OUTPUT

// PLUGIN-VM: ctjs.unbounded_page_naming = ["DOM naming mutation is unbounded:
// PLUGIN-VM: ctjs.constant #ctjs.string<"jQuery">
// PLUGIN-VM: ctjs.get_property {{.*}}ctjs.absent_global_property

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

// The fold takes the page to have no element of that id or name (HTML's named
// access on the Window object, 7.3.3). When the page driver records a
// `jQuery` element (`--whole-page-named=jQuery`, the `<div id="jQuery">`
// case) `window.jQuery` is that element, and when the module is only part of
// its page a later script may bind the name: both keep the read.
// ELEMENT: ctjs.constant #ctjs.string<"jQuery">
// ELEMENT: ctjs.get_property

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

// NAMED: ctjs.load_global "PROPS" {typeof_lookup = true}
// NAMED: ctjs.load_global "window"
// NAMED: ctjs.constant #ctjs.string<"PROPS">
// NAMED: ctjs.get_property
// OTHER: 1 typeof read(s) folded to undefined
// OTHER-NOT: ctjs.load_global "PROPS"
// OTHER-NOT: ctjs.load_global "window"
// UNKNOWN: ctjs.unbounded_page_naming = ["DOM naming mutation is unbounded:
// UNKNOWN: ctjs.load_global "PROPS" {ctjs.absent_global_typeof, typeof_lookup = true}
// UNKNOWN: ctjs.get_property {{.*}}ctjs.absent_global_property
// PERSISTENT: ctjs.naming_assumed_absent
// PERSISTENT-SAME: ctjs.unbounded_page_naming = ["DOM naming mutation is unbounded:
// PERSISTENT: emitc.func @main
// OUTPUT: complete native output refused:
// OUTPUT-SAME: DOM naming mutation is unbounded:
// ALIAS: ctjs.unbounded_page_naming = ["DOM naming mutation is unbounded: reflection builtin escapes or is aliased
// ALIAS: ctjs.load_global "PROPS" {ctjs.absent_global_typeof, typeof_lookup = true}
// PROPERTY-ALIAS: ctjs.unbounded_page_naming = ["DOM naming mutation is unbounded: property read may yield an aliased reflection builtin
// PROPERTY-ALIAS: ctjs.load_global "PROPS" {{.*}}typeof_lookup = true
// NO-ABSENCE: ctjs.unbounded_page_naming = ["DOM naming mutation is unbounded:
// NO-ABSENCE: emitc.func @main
// CPP: int32_t main(
// OPAQUE: ctjs.load_global "PROPS" {{.*}}typeof_lookup = true
// OPAQUE: ctjs.constant #ctjs.string<"PROPS">
// OPAQUE: ctjs.get_property
// CLAUSE5: ctnative.not_native =

//--- named-id.js
document.body.setAttribute("id", "PROPS");
var kind = typeof PROPS;
var result = window.PROPS;

//--- named-name.js
document.body.name = "PROPS";
var kind = typeof PROPS;
var result = window.PROPS;

//--- named-markup.js
document.body.innerHTML = '<div id="PR&#79;PS"></div>';
var kind = typeof PROPS;
var result = window.PROPS;

//--- named-prefix.js
(function (key) {
    document.body.id = "PRO" + key;
})();
var kind = typeof PROPS;
var result = window.PROPS;

//--- other-name.js
document.body.id = "other";
var kind = typeof PROPS;
var result = window.PROPS;

//--- unknown-name.js
document.body.setAttribute("id", location.hash);
var kind = typeof PROPS;
var result = window.PROPS;

//--- pruned-naming.js
if (window.PROPS) {
    document.body.innerHTML = location.hash;
}

//--- reflect-alias.js
const R = Reflect;
R.set(document.body, "id", location.hash);
var kind = typeof PROPS;
var result = window.PROPS;

//--- window-reflect.js
const R = window.Reflect;
R.set(document.body, "id", "PROPS");
var result = window.PROPS;
var kind = typeof PROPS;

//--- computed-reflect.js
const R = window[location.hash];
R.set(document.body, "id", "PROPS");
var result = window.PROPS;
var kind = typeof PROPS;

//--- opaque-dom.js
someHost(document.body);
var kind = typeof PROPS;
var result = window.PROPS;

//--- opaque-construct.js
new someHost(document.body);
var kind = typeof PROPS;
var result = window.PROPS;

//--- opaque-spread.js
someHost(...[document.body]);
var kind = typeof PROPS;
var result = window.PROPS;

//--- opaque-construct-spread.js
new someHost(...[document.body]);
var kind = typeof PROPS;
var result = window.PROPS;

//--- opaque-tag.js
someHost`${document.body}`;
var kind = typeof PROPS;
var result = window.PROPS;

//--- known-construct.js
(function () {
    function Known(el) { this.el = el; }
    new Known(document.body);
})();
var kind = typeof PROPS;
var result = window.PROPS;

//--- opaque-receiver.js
document.body[location.hash]("id", "x");
var kind = typeof PROPS;
var result = window.PROPS;

//--- opaque-document.js
document[location.hash]("<img name=x>");
var kind = typeof PROPS;
var result = window.PROPS;

//--- opaque-window-alias.js
var w = window;
w[location.hash](document.body, "id", "x");
var kind = typeof PROPS;
var result = window.PROPS;

//--- object-constructor.js
const O = ({}).constructor;
O.assign(document.body, { id: "PROPS" });
var result = window.PROPS;

//--- descriptor-setter.js
Object.getOwnPropertyDescriptor(Element.prototype, "id").set.call(document.body, "PROPS");
var result = window.PROPS;

//--- no-absence.js
if (false) {
    document.body.innerHTML = location.hash;
}
var result = 1;

//--- bounded-plugin.js
function getjQuery() { return window.jQuery && window.jQuery; }
var result = getjQuery();
document.body.id = "other";

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
