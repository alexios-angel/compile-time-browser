// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/concat.js | ctjs-opt --pass-pipeline="builtin.module(ctjs-resolve-globals,ctjs-lift-to-scf,ctnative-lower-to-emitc,emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))" -o %t/concat.mlir
// RUN: %compilation_unit --module %t/concat.mlir --js %t/concat.js --work %t/concat --name optional_concat
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../Fixtures/Scalars/string.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/coercion.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --check-prefix=COERCION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/equality.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --check-prefix=EQUALITY --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ordering.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --check-prefix=ORDERING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --check-prefix=OPTIONAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/global.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --check-prefix=GLOBAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/field.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --check-prefix=FIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --check-prefix=MIXED --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shared-mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --check-prefix=SHARED-MIXED

// NATIVE: emitc.include "ctcompile/CTNative/Runtime/ctnative.hpp"
// NATIVE: emitc.func @main() -> i32
// NATIVE: emitc.func @placements_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::js_string">{{.*}}) -> !emitc.opaque<"ctnative::js_string">
// NATIVE: add {{.*}} : (!emitc.opaque<"ctnative::js_string">, !emitc.opaque<"ctnative::js_string">) -> !emitc.opaque<"ctnative::js_string">
// COERCION: emitc.func @add_1({{.*}}!emitc.opaque<"ctnative::js_string">{{.*}}) -> !emitc.opaque<"ctnative::js_string">
// COERCION: add {{.*}} : (!emitc.opaque<"ctnative::js_string">, !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_string">
// COERCION-NOT: ctnative.not_native
// EQUALITY: emitc.func @compare_1
// EQUALITY: call_opaque "ctnative::primitive_equal"
// ORDERING: ctjs.func private @compare$1
// ORDERING-SAME: ctnative.not_native = "compare operand is !ctnative.str<utf8>, not a number"
// OPTIONAL: emitc.func @choose_1({{.*}}) -> !emitc.opaque<"ctnative::nullable_string">
// OPTIONAL: call_opaque "ctnative::to_nullable_string"
// OPTIONAL-NOT: ctnative.not_native
// GLOBAL: emitc.global static @g_result : !emitc.opaque<"ctnative::nullable_string">
// GLOBAL: call_opaque "ctnative::global_string"
// GLOBAL: call_opaque "ctnative::print_string"
// GLOBAL-NOT: ctnative.not_native
// FIELD: emitc.field @direction : !emitc.opaque<"ctnative::js_string">
// FIELD: emitc.func @main()
// FIELD-NOT: ctnative.not_native
// FIELD-NOT: ctjs.func
// MIXED: emitc.func @choose_1({{.*}}) -> !emitc.opaque<"ctnative::number_string">
// The union now has a scalar carrier; shared-cell assignments still require
// a proved widening rule. Preserve the original before/call/after source.
// SHARED-MIXED: ctjs.func private @mixedShared$1
// SHARED-MIXED-SAME: ctnative.not_native = "an assignment of !ctnative.str<utf8> to a shared binding of type !ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>"
// SHARED-MIXED: ctjs.func private @change$2
// SHARED-MIXED-SAME: ctnative.cell_args = array<i32: 3>
// SHARED-MIXED-SAME: ctnative.not_native = "an assignment of !ctnative.num<i32> to a shared binding of type !ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>"

//--- coercion.js
// Keep a runtime String/Number addition even when its result is discarded.
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

//--- concat.js
function concat(mode, high, low) {
    let value;
    if (mode > 0) { value = high; }
    return value + low;
}
var joined = concat(1, "\ud83d", "\ude00");
var absent = concat(0, "\ud83d", "\ude00");
