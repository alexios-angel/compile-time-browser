// Built-in statics and arrays of Numbers. EMIT checks that a map to Numbers
// lowers to a std::vector<ctnative::js_num>, parseInt to
// ctnative::Number.parseInt and Math.min to ctnative::Math.min, with nothing
// refused. NUMBER is the refusal of parseInt of a value that is not a
// String, and SHADOW checks that a parseInt read off a `Number` the program
// binds itself, which is not the realm's, is not lowered to the runtime's.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native
// The realm's Number is recognised by the row's host_binding field, never by
// its reason text: REWORDED rewrites every reason and still lowers both rows;
// UNMARKED drops the field and lowers neither. Each `not grep` proves its edit
// landed.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals > %t/resolved.mlir
// RUN: FileCheck %s --check-prefix=MARKED < %t/resolved.mlir
// RUN: sed 's/reason = "[^"]*"/reason = "reworded"/g' %t/resolved.mlir > %t/reworded.mlir
// RUN: not grep -q "a host binding" %t/reworded.mlir
// RUN: ctjs-opt --ctjs-lift-to-scf --ctnative-lower-to-emitc %t/reworded.mlir | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native
// RUN: sed 's/host_binding, //g' %t/resolved.mlir > %t/unmarked.mlir
// RUN: not grep -q host_binding %t/unmarked.mlir
// RUN: ctjs-opt --ctjs-lift-to-scf --ctnative-lower-to-emitc %t/unmarked.mlir | FileCheck %s --check-prefix=UNMARKED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/number.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NUMBER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shadow.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SHADOW

// EMIT-DAG: call_opaque "ctnative::Number.parseInt"
// EMIT-DAG: call_opaque "ctnative::Math.min"
// EMIT-DAG: call_opaque "ctnative::Array.prototype.map.call"{{.*}}std::vector<ctnative::js_num>
// MARKED-DAG: {host_binding, name = "Math"
// MARKED-DAG: {host_binding, name = "Number"
// UNMARKED: module
// UNMARKED-NOT: ctnative::Number.parseInt
// UNMARKED-NOT: ctnative::Math.min
// NUMBER: @fromNumber${{[0-9]+}}{{.*}}ctnative.not_native = "Number.parseInt's argument 0 is {{.*}}, not a String"
// SHADOW: module
// SHADOW-NOT: ctnative::Number.parseInt

//--- emit.js
function sum(text) {
    var parts = text.split(',').map(function (t) {
        return Number.parseInt(t, 10);
    });
    return Math.min(parts.length, 5) + Number.parseInt(text);
}
var total = sum('1,2');

//--- number.js
function fromNumber(n) {
    return Number.parseInt(n, 10);
}
var r = fromNumber(7);

//--- shadow.js
var Number = {
    parseInt: function (s) {
        return 1;
    }
};
function shadowed(text) {
    return Number.parseInt(text, 10);
}
var r = shadowed('5');
