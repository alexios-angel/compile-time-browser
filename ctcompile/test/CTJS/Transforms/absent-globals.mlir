// --ctjs-resolve-globals folds `typeof` of an absent global to `undefined`.
//
// A whole-program compile treats the script as a closed program. A name it
// never stores, that the native realm does not provide (NativeRealm.h) and
// that nothing can bind dynamically does not exist at run time, so `typeof`
// of it is "undefined". The read becomes that constant and the globals row
// says why. css-dump.js's `typeof PROPS` and a UMD wrapper's
// `typeof define`/`exports`/`module` are the shapes this is for.
//
// Each negative control keeps the read: a name the program stores, a name the
// host provides, a dynamic write through the global object (computed key, via
// `globalThis` and via `window`), and a plain read, which throws in the VM and
// is left for native admission to refuse.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/probe.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PROBE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/host.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=HOST
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/plain.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PLAIN

// PROBE: {name = "PROPS", reason = "absent: never stored and not provided by the native realm; 1 typeof read(s) folded to undefined", resolved = "none", stores = 0 : i32}
// PROBE-NOT: ctjs.load_global "PROPS"
// PROBE: %[[ABSENT:[^ ]+]] = ctjs.constant #ctjs.undefined
// PROBE-NEXT: ctjs.unary typeof %[[ABSENT]]

// KEPT: ctjs.load_global "PROPS" {typeof_lookup = true}

// HOST: ctjs.load_global "document" {typeof_lookup = true}

// PLAIN: ctjs.load_global "PROPS"
// PLAIN-NOT: typeof_lookup

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
