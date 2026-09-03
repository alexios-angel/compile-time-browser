// A PARAMETER'S REFUSAL NAMES WHAT IS ACTUALLY MISSING.
//
// There are two ways a parameter can have no carrier, and until now both said
// "no caller proves it (a closed-world call is Phase 62½-A)". For
// `function tag(s){ return s; } tag("hello")` that sentence is false in both
// halves: a caller DID prove it - --ctjs-resolve-globals turned the call into a
// direct one and the lattice carried `!ctnative.str<utf8>` all the way to the
// parameter - and the closed world is not what is missing. What is missing is a
// CARRIER for a string, which is the string work and not Phase 62½-A, so the
// message sent a reader to the wrong phase.
//
// The `boxed` case keeps the old wording and is the one the corpora hit: all
// 688 parameter refusals measured over bootstrap and p5 are that kind, so this
// split moves no corpus number. native-numeric.mlir pins that half - `scale`
// with no resolve-globals pass at all - and this file pins the other.
//
// ONE PROGRAM PER FILE: a refused callee refuses its caller, so the two cases
// cannot share a module.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proved.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PROVED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unproved.js 2>/dev/null | ctjs-opt --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=UNPROVED

// --- PROVED, AND STILL NOT REPRESENTABLE -----------------------------------
//
// PROVED: ctjs.func private @tag$1
// PROVED-SAME: ctnative.not_native = "parameter 0 is !ctnative.str<utf8>, which has no native carrier yet"

// --- AND THE ONE NOTHING PROVED, WHICH IS STILL 62½-A -----------------------
//
// UNPROVED: ctjs.func @scale$1
// UNPROVED-SAME: ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (a closed-world call is Phase 62\C2\BD-A)"

//--- proved.js
function tag(s) { return s; }
var a = tag("hello");

//--- unproved.js
function scale(x) { return x * 2; }
var b = 1;
