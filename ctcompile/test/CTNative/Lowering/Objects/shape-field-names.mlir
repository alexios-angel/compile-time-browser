// A CLOSED SHAPE IS NOT ENOUGH: THE KEY HAS TO BE A FIELD NAME TOO.
//
// Three programs the tier used to ACCEPT. The first computed a different
// answer from the interpreter with no diagnostic anywhere; the other two
// emitted C++ that does not compile, on programs the pipeline had declared
// native and part 24 Phase 63 Step 7 then builds with -Werror.
//
// ONE PROGRAM PER FILE, and that is not tidiness. A refusal is contagious in
// both directions - a refused callee refuses its caller, and a refused `main`
// refuses everything it calls - so a single bad key in a shared module refuses
// every other function in it and the positive case below could not be written
// at all. `inherited` is worse still: `p.constructor` is how a program reaches
// `Function` without naming it, so --ctjs-resolve-globals stops resolving
// anything in that module and the refusals that follow name globals rather
// than keys.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inherited.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=INHERITED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shadowed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SHADOWED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/keyword.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=KEYWORD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/macro.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MACRO

// --- read but never written, and the prototype answers it -------------------
//
// The field would be an uninitialised double; the interpreter finds
// Object.prototype.constructor, which is a function and is truthy. So
// `if (o.constructor)` took different branches on the two sides, with nothing
// refused anywhere.
//
// INHERITED: ctnative.not_native = "field `constructor` is read but never written, and Object.prototype answers that name - the interpreter finds a function where this would find undefined"

// --- but WRITING it shadows the inherited one, so that stays admitted -------
//
// The precision this rule costs is meant to be nil: an own property is what
// both sides read, so only the read-only case is wrong and only it is refused.
// If this goes red the rule has become a blanket ban on a set of key names.
//
// SHADOWED: emitc.func @shadowed_1() -> f64
// SHADOWED-NOT: ctnative.not_native

// --- a JavaScript name that is also a C++ keyword ---------------------------
//
// Field names are emitted verbatim - cIdentifier() sanitises symbols, not
// members - so this emitted `double class;`.
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
