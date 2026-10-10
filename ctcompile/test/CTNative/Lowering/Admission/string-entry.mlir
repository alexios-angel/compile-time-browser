// A string entry is a function the embedder calls from outside the script,
// after it has run, with every declared parameter a String
// (--ctnative-lower-to-emitc string-entries). Its parameters are Strings with
// no caller in the program, it stays public and it keeps its JavaScript name.
// Without the option nothing proves the parameters, so the function is
// refused as before. A name that is not one global bound once to a function,
// or that C++ cannot spell, is a hard error rather than a silent no-op.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/entry.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=string-entries=greet | FileCheck %s --check-prefix=ENTRY --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/entry.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NO-OPTION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/entry.js 2>/dev/null | not ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=string-entries=missing 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/value.js 2>/dev/null | not ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=string-entries=count 2>&1 | FileCheck %s --check-prefix=VALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/entry.js 2>/dev/null | not ctjs-opt --ctjs-lift-to-scf --ctnative-lower-to-emitc=string-entries=greet 2>&1 | FileCheck %s --check-prefix=UNRESOLVED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/main.js 2>/dev/null | not ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=string-entries=main 2>&1 | FileCheck %s --check-prefix=NAME
// RUN: python3 %S/string-entry.py --translate ctjs-translate --opt ctjs-opt --work %t/run

// ENTRY: emitc.func @greet({{[^,]*}}!emitc.opaque<"ctnative::js_string">, {{[^,]*}}!emitc.opaque<"ctnative::js_string">) -> !emitc.opaque<"ctnative::js_string">
// ENTRY-NOT: @greet_1

// NO-OPTION: ctjs.func private @greet$1
// NO-OPTION-SAME: ctnative.not_native = "parameter 0 is <unvisited> - no caller proves it

// MISSING: error: string entry `missing` is not a global bound once to a function in this program
// VALUE: error: string entry `count` is not a global bound once to a function in this program
// UNRESOLVED: error: string-entries needs --ctjs-resolve-globals to have run first
// NAME: error: string entry `main` cannot be a C++ function name

//--- entry.js
function greet(first, second) { return first + "," + second; }

//--- value.js
var count = 1;

//--- main.js
function main(text) { return text; }
