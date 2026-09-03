// THE FIVE REFUSAL SHAPES REAL CODE ACTUALLY PRODUCES.
//
// tools/check/native-claims.py groups every refusal by its SHAPE - the reason
// with identifiers, numbers and MLIR types replaced - and over the three
// corpora the compiler already measures there are exactly eight of them, in
// 12,916 refusals:
//
//     8600  uses `X`                                    <- always `this`
//     2426  uses its own closure
//     1779  parameter N is T - no caller proves it ...  <- native-numeric.mlir
//       46  unstructured control flow                   <- refusal-unstructured
//       27  global `X` is T, not a number
//       21  a constant that is not a number, a boolean or undefined
//       13  an object literal that escapes - it is returned   <- native-struct
//        4  an array literal that escapes - it is returned    <- native-array
//
// THREE OF THE EIGHT WERE PINNED, and they are the three at the bottom of the
// table. The top two - 11,026 refusals between them, 85% of everything this
// compiler says about real code - were pinned by nothing at all, and neither
// was `global \`X\` is T, not a number` or the constant rule. This file is
// those, plus `uses new.target`, which shares the arm with the two at the top
// and fires nowhere in the corpora only because Babel's _classCallCheck is
// behind a guard the importer does not reach.
//
// THESE ARE STRUCTURAL REFUSALS, decided before any type is looked at, which
// is why they dominate: the three implicit arguments a ctjs.func always has -
// receiver, new.target, callee - have no native carrier, so a function that
// READS one cannot be a plain C++ function whatever its types say.
//
// ONE PROGRAM PER FILE; see the header of shape-field-names.mlir for why.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=THIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/newtarget.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NEWTARGET
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ownclosure.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OWNCLOSURE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/global.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=GLOBAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constant.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CONSTANT

// --- argument 0: the receiver. 8600 refusals, two thirds of the whole census
//
// The message is spelled with the JavaScript name in backticks, not "argument
// 0" or "receiver": what the reader wrote is `this`.
//
// THIS: ctjs.func {{.*}}@usesthis$1
// THIS-SAME: ctnative.not_native = "uses `this`"

// --- argument 1: new.target -------------------------------------------------
//
// NEWTARGET: ctjs.func {{.*}}@constructed$1
// NEWTARGET-SAME: ctnative.not_native = "uses new.target"

// --- argument 2: the function's own closure. 2426 refusals ------------------
//
// A named function expression that calls ITSELF by name reads its own closure
// value, because the name is bound in the function's own scope and nowhere a
// direct call could resolve. (A `function f(){}` DECLARATION is exempt: its
// create_closure's only use is a store_global, the pair lowers to nothing, and
// isDeclarationClosure says so.)
//
// OWNCLOSURE: ctjs.func {{.*}}@me$1
// OWNCLOSURE-SAME: ctnative.not_native = "uses its own closure"

// --- a global that is not a number. 27 refusals -----------------------------
//
// Every global this tier carries is a `static double`, so a global whose type
// inference could not prove numeric has no representation. The message names
// the global AND what it was proved to be, which is what makes the census
// row above a work list rather than a count.
//
// GLOBAL: ctjs.func {{.*}}@reader$1
// GLOBAL-SAME: ctnative.not_native = "global `unstored` is !ctnative.boxed, not a number"

// --- a constant that is not a number, a boolean or undefined. 21 refusals ---
//
// A string literal, here reached through `.length` so that neither the
// key-constant exemption (isKeyOnlyString) nor the vector-length one
// (isVectorKeyString) applies and it is a value in its own right.
//
// CONSTANT: ctjs.func {{.*}}@s$1
// CONSTANT-SAME: ctnative.not_native = "a constant that is not a number, a boolean or undefined"

//--- this.js
function usesthis(x) { return this ? x : x + 1; }
var r = usesthis(3);

//--- newtarget.js
function constructed(n) { return new.target ? 1 : 2; }
var r = constructed(1);

//--- ownclosure.js
var f = function me(n) { return n > 0 ? me(n - 1) : 0; };
var r = f(3);

//--- global.js
function reader() { return unstored + 1; }
var r = reader();

//--- constant.js
function s(n) { var t = "x"; return n + t.length; }
var r = s(1);
