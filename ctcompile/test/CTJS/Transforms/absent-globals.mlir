// --ctjs-resolve-globals folds `typeof` of an absent global to `undefined`,
// on a whole page (--whole-page, WholePage.h).
//
// A whole page is the closed program: every script its page will ever run is
// in it. A name it never stores, that the native realm does not provide
// (NativeRealm.h) and that nothing can bind dynamically does not exist at run
// time, so `typeof` of it is "undefined". The read becomes that constant and
// the globals row says why. css-dump.js's `typeof PROPS` and a UMD wrapper's
// `typeof define`/`exports`/`module` are the shapes this is for.
//
// Each negative control keeps the read: a name the program stores, a name the
// host provides, a dynamic write through the global object (computed key, via
// `globalThis` and via `window`), and a plain read, which throws in the VM and
// is left for native admission to refuse.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/probe.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PROBE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/host.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=HOST
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/plain.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PLAIN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prefix-umd.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PREFIXUMD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prefix-declarations.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PREFIXPROPS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prefix-call.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prefix-twice.js --whole-page 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/probe.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=OPEN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/probe.js --whole-page --whole-page-named=PROPS 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/probe.js --whole-page --whole-page-named=other 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PROBE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prefix-declarations.js --whole-page --whole-page-named=PROPS 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT

// PROBE: {name = "PROPS", reason = "absent: never stored and not provided by the native realm; 1 typeof read(s) folded to undefined", resolved = "none", stores = 0 : i32}
// PROBE-NOT: ctjs.load_global "PROPS"
// The frame's six registers start as `undefined` constants; the folded read is
// the seventh, right before the `typeof` it feeds.
// PROBE: ctjs.frame_enter 6
// PROBE-COUNT-6: = ctjs.constant #ctjs.undefined
// PROBE-NEXT: %[[ABSENT:[^ ]+]] = ctjs.constant #ctjs.undefined
// PROBE-NEXT: ctjs.unary typeof %[[ABSENT]]

// KEPT: ctjs.load_global "PROPS" {typeof_lookup = true}

// HOST: ctjs.load_global "document" {typeof_lookup = true}

// PLAIN: ctjs.load_global "PROPS"
// PLAIN-NOT: typeof_lookup

// Only a whole page folds: a module that is only part of one keeps the read,
// since a later script can bind the name.
// OPEN: ctjs.load_global "PROPS" {typeof_lookup = true}

// --- The prefix rule ---------------------------------------------------------
//
// These modules pass the window to a callee the program never binds, so clause
// 5 refuses them and the whole-module rule does not apply. A `typeof` read that
// runs before anything could bind a global still folds: the UMD wrapper's
// checks at the top of the IIFE the script calls once, and a feature test
// after nothing but function declarations. The rows name the rule. A call to
// an unknown function before the read, a computed write through the window
// before it (window.js above), and a read in a function the script also calls
// again after the escape keep the read.
//
// PREFIXUMD: {name = "define", reason = "absent before any global can be bound: never stored and not provided by the native realm; 1 typeof read(s) in the script's prefix folded to undefined", resolved = "none", stores = 0 : i32}
// PREFIXUMD-NOT: ctjs.load_global "define" {typeof_lookup = true}
// PREFIXUMD-NOT: ctjs.load_global "exports" {typeof_lookup = true}
// PREFIXUMD-NOT: ctjs.load_global "module" {typeof_lookup = true}
//
// PREFIXPROPS: {name = "PROPS", reason = "absent before any global can be bound: never stored and not provided by the native realm; 1 typeof read(s) in the script's prefix folded to undefined", resolved = "none", stores = 0 : i32}
// PREFIXPROPS-NOT: ctjs.load_global "PROPS" {typeof_lookup = true}

//--- probe.js
var r = typeof PROPS === "undefined" ? 1 : 2;

//--- stored.js
var r = typeof PROPS === "undefined" ? 1 : 2;
var PROPS = 3;

//--- host.js
var r = typeof document === "undefined" ? 1 : 2;

//--- dynamic.js
var key = "PROPS";
globalThis[key] = 3;
var r = typeof PROPS === "undefined" ? 1 : 2;

//--- window.js
var key = "PROPS";
window[key] = 3;
var r = typeof PROPS === "undefined" ? 1 : 2;

//--- plain.js
var r = PROPS;

//--- prefix-umd.js
var r = (function (factory) {
    return typeof exports === "object" && typeof module !== "undefined" ? factory()
        : typeof define === "function" && define.amd ? define(factory)
        : factory();
})(function () { return 1; });
someHost(window);

//--- prefix-declarations.js
function dump(p) { return p; }
var r = typeof PROPS !== "undefined" ? dump(PROPS) : 0;
someHost(window);

//--- prefix-call.js
someHost(0);
var r = typeof PROPS === "undefined" ? 1 : 2;
someHost(window);

//--- prefix-twice.js
var probe = function () { return typeof PROPS === "undefined" ? 1 : 2; };
var a = probe();
someHost(window);
var b = probe();
