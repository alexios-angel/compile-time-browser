// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-string-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/coercion.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=COERCION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/equality.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EQUALITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ordering.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ORDERING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=OPTIONAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/global.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=GLOBAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/field.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=FIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shared-mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SHARED-MIXED

// NATIVE: emitc.include <"string">
// NATIVE: emitc.func @main() -> i32
// NATIVE: emitc.func @placements_{{[0-9]+}}({{.*}}!emitc.opaque<"std::string">{{.*}}) -> !emitc.opaque<"std::string">
// COERCION: ctjs.func private @add$1
// COERCION-SAME: ctnative.not_native = "binary operand is !ctnative.str<utf8>, not a number"
// EQUALITY: ctjs.func private @compare$1
// EQUALITY-SAME: ctnative.not_native = "equality operand is !ctnative.str<utf8>, not a number"
// ORDERING: ctjs.func private @compare$1
// ORDERING-SAME: ctnative.not_native = "compare operand is !ctnative.str<utf8>, not a number"
// OPTIONAL: ctjs.func private @choose$1
// OPTIONAL-SAME: ctnative.not_native = "a value of type !ctnative.opt<!ctnative.str<utf8>> from `scf.if`"
// GLOBAL: ctnative.not_native = "store to global `result` requires a numeric global"
// FIELD: ctnative.not_native = "field `direction` is stored a !ctnative.str<utf8>, not a number or a boolean"
// MIXED: ctjs.func private @choose$1
// MIXED-SAME: ctnative.not_native = "a value of type !ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>> from `scf.if`"
// SHARED-MIXED: ctjs.func private @mixedShared$1
// SHARED-MIXED-SAME: ctnative.not_native = "a shared binding of type !ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>, which has no native carrier
// SHARED-MIXED: ctjs.func private @change$2
// SHARED-MIXED-SAME: ctnative.cell_args = array<i32: 3>
// SHARED-MIXED-SAME: ctnative.not_native = "shared capture 0 is !ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>, which has no native carrier yet"

//--- coercion.js
// A discarded call needs no top-level ternary or global coercion. Those
// can obscure the operation this test means to refuse with a resolver or
// global-printing limitation before the string rule is reached.
function add(value) { return value + 1; }
add("3");

//--- equality.js
function compare(left, right) { return left == right; }
compare("3", 3);

//--- ordering.js
function compare(left, right) { return left < right; }
compare("a", "b");

//--- optional.js
function choose(mode) {
    var value;
    if (mode > 0) { value = "rtl"; }
    return value;
}
choose(-1);

//--- global.js
var result = "rtl";

//--- field.js
function direction() {
    var element = {direction: "rtl"};
    return element.direction === "rtl" ? 1 : 0;
}
var result = direction();

//--- mixed.js
function choose(mode) {
    return mode > 0 ? "x" : 1;
}
choose(0);

//--- shared-mixed.js
// String cells now have a carrier; a binding mixing strings and numbers
// must still exercise the shared-cell carrier refusal.
function mixedShared() {
    var value = "a";
    function change() { value = 1; return value; }
    var before = value;
    change();
    return before === value ? 1 : 0;
}
var result = mixedShared();
