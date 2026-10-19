// The most common refusal shapes in real code.
//
// tools/check/native-claims.py groups refusals by shape (the reason with
// identifiers, numbers and MLIR types replaced). Over the three corpora it
// measures, the 12,916 refusals had eight shapes when this file was written:
//
//     8600  uses `X`                                    <- always `this`
//     2426  uses its own closure
//     1779  parameter N is T - no caller proves it ...  <- Scalars/numeric.mlir
//       46  unstructured control flow                   <- refusal/unstructured.mlir
//       27  global `X` is T, not a number
//       21  a constant that is not a number, a boolean or undefined
//       13  an object literal that escapes - returned   <- Objects/struct.mlir
//        4  an array literal that escapes - returned    <- Objects/array.mlir
//
// Rerun native-claims.py for current counts. An arrow names the test that
// pins that message.
//
// This file pins `this`, the function's own closure and a non-numeric global
// as refusals, plus `uses new.target`, which shares the arm with `this` and
// fires nowhere in the corpora only because Babel's _classCallCheck sits
// behind a guard the importer does not reach. It also checks that a String
// length constant, once refused by the constant rule, now lowers and runs.
//
// The first three are structural refusals, decided before any type is looked
// at, which is why they dominate: the three implicit arguments every ctjs.func
// has (receiver, new.target, callee) have no native carrier, so a function
// that reads one cannot be a plain C++ function whatever its types say.
//
// One program per split-file input; see the header of
// Objects/shape-field-names.mlir for why.
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=THIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/liftedthis.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=LIFTEDTHIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/newtarget.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=NEWTARGET
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ownclosure.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=OWNCLOSURE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/global.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=GLOBAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constant.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/constant.mlir
// RUN: FileCheck %s --check-prefix=CONSTANT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func < %t/constant.mlir
// RUN: ctjs-opt %t/constant.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/constant.cpp
// RUN: %gxx -O2 %t/constant.cpp -o %t/constant-gcc
// RUN: %t/constant-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/constant.cpp -o %t/constant-clang
// RUN: %t/constant-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/constant.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/constant.js | FileCheck %s --check-prefix=OBSERVATION

// --- argument 0: the receiver, two thirds of all refusals measured ---------
//
// The message is spelled with the JavaScript name in backticks, not "argument
// 0" or "receiver": what the reader wrote is `this`.
//
// THIS: ctjs.func {{.*}}@usesthis$1
// THIS-SAME: ctnative.not_native = "uses `this`"
//
// When the receiver lift judged the function and refused it, the reason the
// lift recorded on its closure is the actionable one, so it follows the use.
// Here the method's object has a computed-key write, so its shape is open (the
// same program as OPENSHAPE in Objects/receiver/refusals.mlir, whose CHECK
// pins the enclosing function's message).
//
// LIFTEDTHIS: ctjs.func {{.*}}@fn$2
// LIFTEDTHIS-SAME: ctnative.not_native = "uses `this` - a closure used as a value: it is a method field of an object whose shape is not closed, so `f` cannot become a free function taking that object"

// --- argument 1: new.target -------------------------------------------------
//
// NEWTARGET: ctjs.func {{.*}}@constructed$1
// NEWTARGET-SAME: ctnative.not_native = "uses new.target"

// --- argument 2: the function's own closure ---------------------------------
//
// A named function expression that calls itself by name reads its own closure
// value, because the name is bound in the function's own scope and nowhere a
// direct call could resolve. (A `function f(){}` declaration is exempt: its
// create_closure's only use is a store_global, the pair lowers to nothing, and
// isDeclarationClosure says so.)
//
// OWNCLOSURE: ctjs.func {{.*}}@me$1
// OWNCLOSURE-SAME: ctnative.not_native = "uses its own closure"

// --- a global that is not a number ------------------------------------------
//
// A global that is read but never stored has no type inference can prove, so
// it stays !ctnative.boxed and has no native representation. The message names
// the global and what it was proved to be, which makes each refusal a to-do
// item rather than a count.
//
// GLOBAL: ctjs.func {{.*}}@reader$1
// GLOBAL-SAME: ctnative.not_native = "global `unstored` is !ctnative.boxed, not a number"

// A String's length has a definite native type, so this lowers and runs.
// CONSTANT: emitc.func @main()
// CONSTANT: emitc.func @s_1
// CONSTANT: call_opaque "ctnative::string_length"
// OBSERVATION: r=2

//--- this.js
function usesthis(x) { return this ? x : x + 1; }
var r = usesthis(3);

//--- liftedthis.js
function opened(k) {
    var o = { x: 1, f: function () { return this.x; } };
    o[k] = 5;
    return o.f();
}
var a = opened(2);

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
