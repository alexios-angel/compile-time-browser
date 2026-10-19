// Global-held records: an object literal stored into a `var` at top level,
// then read back through `load_global` for constant-key accesses and method
// calls. Today the `ctjs.store_global` opens the shape, so the record's
// method field is refused as "an object whose shape is not closed". This
// pins the boundary the closed-record carrier will move.
//
// The positive witness (positive.js) is the smallest Popper-style record:
// one literal with one method field, stored into a `var`, called through the
// global. The interpreter and Node agree on the answer; the bundle does not
// lower natively yet. Each negative isolates one opening use the carrier
// must keep refusing, named by its reason.
//
// `var` and top-level `function` are also properties of the global object,
// so the carrier must check the whole page's treatment of the global object
// (window[k], Object.keys(window), delete window.X, for-in over it) before
// admitting a `var`-held record. `const` and `let` are lexical: they are not
// global-object properties, so a `const R = {...}` record needs only the
// load check.
//
// RUN: split-file %s %t
// RUN: %native_reference %t/positive.js | FileCheck %s --check-prefix=ANSWER
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/positive.js | FileCheck %s --check-prefix=ANSWERNODE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/positive.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDARY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/computed-key.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=COMPUTEDKEY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/object-keys.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OBJECTKEYS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/post-publication-write.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=POSTWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unknown-callee.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNKNOWNCALLEE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/const-binding.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CONSTBOUNDARY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window-computed-key.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=WINDOWCOMPUTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window-object-keys.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=WINDOWKEYS

// ANSWER: r=6
// ANSWERNODE: r=6
// BOUNDARY: ctjs.func @_script_$0
// BOUNDARY-SAME: ctnative.not_native = "a closure used as a value: it is a method field of an object whose shape is not closed, so `fn` cannot become a free function taking that object"
// BOUNDARY-COUNT-1: ctnative.closure_reason = "it is a method field of an object whose shape is not closed, so `fn` cannot become a free function taking that object"

// COMPUTEDKEY: ctjs.func @_script_$0
// COMPUTEDKEY-SAME: ctnative.not_native = "a closure used as a value: it is a method field of an object whose shape is not closed, so `fn` cannot become a free function taking that object"

// OBJECTKEYS: ctjs.func @_script_$0
// OBJECTKEYS-SAME: ctnative.not_native = "a closure used as a value: it is a method field of an object whose shape is not closed, so `fn` cannot become a free function taking that object"

// POSTWRITE: ctjs.func @_script_$0
// POSTWRITE-SAME: ctnative.not_native = "a closure used as a value: it is stored into something that is not an object literal made here - Phase 59 slice 2"

// UNKNOWNCALLEE: ctjs.func @_script_$0
// UNKNOWNCALLEE-SAME: ctnative.not_native = "a closure used as a value: it is a method field of an object whose shape is not closed, so `fn` cannot become a free function taking that object"

// CONSTBOUNDARY: ctjs.func @_script_$0
// CONSTBOUNDARY-SAME: ctnative.not_native = "a closure used as a value: it is a method field of an object whose shape is not closed, so `fn` cannot become a free function taking that object"

// WINDOWCOMPUTED: ctjs.func @_script_$0
// WINDOWCOMPUTED-SAME: ctnative.not_native = "a closure used as a value: it is a method field of an object whose shape is not closed, so `fn` cannot become a free function taking that object"

// WINDOWKEYS: ctjs.func @_script_$0
// WINDOWKEYS-SAME: ctnative.not_native = "a closure used as a value: it is a method field of an object whose shape is not closed, so `fn` cannot become a free function taking that object"

//--- positive.js
var popperOffsets = {
    name: "popperOffsets",
    enabled: true,
    phase: "read",
    fn: function (t) { return t + 1; },
    data: {}
};
var r = popperOffsets.fn(5);

//--- computed-key.js
// A computed-key read on the record: `m[k]` with a non-constant `k`. Today
// this is refused by the same "shape is not closed" reason (the store_global
// opens the shape first); once the carrier exists, this must refuse with a
// reason naming the computed-key read, not the store_global.
var m = {
    name: "x",
    fn: function (t) { return t + 1; }
};
var k = "f" + "n";
var r = m[k](5);
console.log("r=" + r);

//--- object-keys.js
// Object.keys(m): a reflection the carrier must refuse. Today the
// store_global opens the shape first; once the carrier exists, Object.keys
// must refuse with a reason naming the reflection.
var m = {
    name: "x",
    fn: function (t) { return t + 1; }
};
var r = Object.keys(m).length;
console.log("r=" + r);

//--- post-publication-write.js
// A second write of `fn` after the literal's publication (the store_global).
// The carrier proves write-once; this must refuse naming the second write.
var m = {
    name: "x",
    fn: function (t) { return t + 1; }
};
m.fn = function (t) { return t + 2; };
var r = m.fn(5);
console.log("r=" + r);

//--- unknown-callee.js
// m.fn detached from the record and passed to an unknown callee. The carrier
// must refuse (different reason: the closure is used as a value, not
// through the record).
function run(f) { return f(5); }
var m = {
    name: "x",
    fn: function (t) { return t + 1; }
};
var r = run(m.fn);
console.log("r=" + r);

//--- const-binding.js
// A `const` binding is lexical, not a global-object property. Once the
// carrier exists, this is the simplest positive case; today it is refused
// the same way as `var`.
const m = {
    name: "x",
    fn: function (t) { return t + 1; }
};
var r = m.fn(5);
console.log("r=" + r);

//--- window-computed-key.js
// A computed-key read on the global object itself. Under the closed page,
// this is a global-object escape that must refuse every var-held record
// carrier, because the page may be reading m through window[k].
var m = {
    name: "x",
    fn: function (t) { return t + 1; }
};
var r = m.fn(5);
var k = "f" + "n";
var x = window[k];
console.log("r=" + r + " x=" + x);

//--- window-object-keys.js
// Object.keys(window): an enumeration of the global object. Under the
// closed page, this must refuse every var-held record carrier, because it
// reflects on m's presence as a global-object property.
var m = {
    name: "x",
    fn: function (t) { return t + 1; }
};
var r = m.fn(5);
var ks = Object.keys(window);
console.log("r=" + r + " ks=" + ks.length);
