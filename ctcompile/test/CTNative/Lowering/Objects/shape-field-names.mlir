// A closed shape is not enough: each key also has to work as a C++ field name.
//
// Programs native lowering must refuse. Accepting `inherited` would compute a
// different answer from the interpreter with no diagnostic; accepting
// `keyword` or `macro` would emit C++ that fails to compile under -Werror.
//
// One program per split file, because a refusal is contagious in both
// directions: a refused callee refuses its caller, and a refused `main`
// refuses everything it calls, so one bad key in a shared module would refuse
// every other function in it and the positive case below could not be
// written. `inherited` is worse: `p.constructor` is how a program reaches
// `Function` without naming it, so --ctjs-resolve-globals stops resolving
// anything in that module and the refusals that follow name globals rather
// than keys.
//
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inherited.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=INHERITED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shadowed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SHADOWED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/keyword.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=KEYWORD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/macro.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MACRO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto-read.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto-read.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto-write.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto-write.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto-computed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto-computed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto-method.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto-method.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PROTO

// --- read but never written, and the prototype answers it -------------------
//
// The field would be an uninitialized double, while the interpreter finds
// Object.prototype.constructor, a function, which is truthy. So
// `if (o.constructor)` would take different branches on the two sides with
// nothing refused.
//
// INHERITED: ctnative.not_native = "field `constructor` is read but never written, and Object.prototype answers that name - the interpreter finds a function where this would find undefined"

// --- but writing it shadows the inherited one, so that stays admitted -------
//
// The rule should cost no precision: an own property is what both sides read,
// so only the read-without-write case is wrong and only it is refused. If this
// goes red the rule has become a blanket ban on a set of key names.
//
// SHADOWED: emitc.func @shadowed_1() -> !emitc.opaque<"ctnative::js_num">
// SHADOWED-NOT: ctnative.not_native

// __proto__ is an inherited accessor. Its setter ignores a primitive value, and
// a following read still returns the prototype object, so treating the store as
// an own field would make typeof return "number". The same proof guards object
// literals and lifted method receivers, with and without optimization.
// PROTO: ctnative.not_native = "{{.*}}Object.prototype.__proto__ accessor

// --- a JavaScript name that is also a C++ keyword ---------------------------
//
// Field names are emitted verbatim (cIdentifier() sanitizes symbols, not
// members), so accepting this would emit `double class;`.
//
// KEYWORD: ctnative.not_native = "field `class` is a C++ keyword or a macro of <cmath>/<cstdio>, so the generated struct would not compile"

// --- and one that is a macro of a header this file emits itself -------------
//
// MACRO: ctnative.not_native = "field `NAN` is a C++ keyword or a macro of <cmath>/<cstdio>, so the generated struct would not compile"

//--- inherited.js
function inherited() {
  var p = { x: 1 };
  return p.constructor;
}
var a = inherited();

//--- shadowed.js
function shadowed() {
  var p = { toString: 2 };
  return p.toString;
}
var b = shadowed();

//--- keyword.js
function keyword() {
  var p = { class: 1 };
  return p.class;
}
var c = keyword();

//--- macro.js
function macro() {
  var p = { NAN: 1 };
  return p.NAN;
}
var d = macro();

//--- proto-read.js
function proto_read() { var o = {}; return typeof o.__proto__; }
var trace = proto_read();

//--- proto-write.js
function probe() { var o = {}; o.__proto__ = 1; return typeof o.__proto__; }
var trace = probe();
//--- proto-computed.js
function probe() { var o = {}; o["__proto__"] = 1; return typeof o["__proto__"]; }
var trace = probe();
//--- proto-method.js
function probe() { var o = { run() { this.__proto__ = 1; return typeof this.__proto__; } }; return o.run(); }
var trace = probe();
