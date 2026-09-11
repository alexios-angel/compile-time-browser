// Field storage follows every actual write, independently of current read types.
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/saved.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SAVED --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nullable-literal.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NULLABLE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/missing.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MISSING --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compatible-literal.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=COMPATIBLE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compatible-readers.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ABSENT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nullable.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=UNPROVED-GLOBAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compatible-functions.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=UNPROVED-GLOBAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed-stores.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CENSUS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/distinct-objects.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=READ-UNION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/incompatible-functions.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=FUNCTIONS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reversed-functions.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CENSUS

// SAVED-DAG: nullable_string field_76616c7565;
// SAVED-DAG: inline nullable_string object_get_field_76616c7565
// SAVED-DAG: nullable_string field)
// SAVED-DAG: call_opaque "ctnative::object_get_field_76616c7565"
// SAVED-DAG: call_opaque "ctnative::object_set_field_76616c7565"
// SAVED-DAG: emitc.func @saved_1({{.*}}) -> !emitc.opaque<"std::string">
// NULLABLE-DAG: nullable_string field_76616c7565;
// NULLABLE-DAG: nullable_scalar field_636f756e74;
// NULLABLE-DAG: emitc.func @nullable_1({{.*}}) -> !emitc.opaque<"ctnative::nullable_string">
// NULLABLE-DAG: call_opaque "ctnative::object_get_field_76616c7565"
// NULLABLE-DAG: call_opaque "ctnative::object_set_field_76616c7565"
// MISSING-DAG: nullable_string field_76616c7565;
// MISSING-DAG: emitc.func @missing_1({{.*}}) -> !emitc.opaque<"ctnative::nullable_string">
// COMPATIBLE-DAG: nullable_string field_76616c7565;
// COMPATIBLE-DAG: emitc.func @text_1({{.*}}) -> !emitc.opaque<"std::string">
// COMPATIBLE-DAG: emitc.func @empty_2(
// ABSENT: nullable_string field_76616c7565;
// ABSENT-LABEL: emitc.func @nullField_2(
// ABSENT: call_opaque "ctnative::object_absent_field"
// ABSENT-LABEL: emitc.func @undefinedField_3(
// ABSENT: call_opaque "ctnative::object_absent_field"
// CENSUS: ctnative.not_native = "owning field `value` has incompatible types across its complete store census"
// FUNCTIONS: ctjs.func private @numeric$1
// FUNCTIONS-SAME: ctnative.not_native = "owning field `value` has incompatible types across its complete store census"
// FUNCTIONS: ctjs.func private @text$2
// FUNCTIONS-SAME: ctnative.not_native = "owning field `value` has incompatible types across its complete store census"
// UNPROVED-GLOBAL: ctnative.not_native = "a value of type !ctnative.boxed from `ctjs.call_direct`"
// UNPROVED-GLOBAL: ctjs.load_global "undefined"
// Both objects enter the same Map field group, so this source's final read
// already contains both types. The separate function cases isolate the later
// emitted-member census with independently narrow Number and String results.
// READ-UNION: ctnative.not_native = "a value of type !ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>> from `ctjs.call_direct`"

//--- saved.js
function saved() {
    const map = new Map();
    const item = {value: ""};
    map.set("x", item);
    const before = item.value;
    item.value = "changed";
    map.delete("x");
    return before;
}
saved();

//--- nullable.js
function nullable(flag) {
    const map = new Map();
    const item = {value: "initial", count: 1};
    map.set("x", item);
    const before = item.value;
    if (flag) { item.value = null; }
    else { item.value = undefined; }
    item.count = 2;
    return before;
}
nullable(true);
nullable(false);

//--- missing.js
function missing(flag) {
    const map = new Map();
    const item = {};
    map.set("x", item);
    if (flag) { item.value = "present"; }
    return item.value;
}
missing(true);
missing(false);

//--- compatible-functions.js
function text() {
    const map = new Map();
    const item = {value: "owned"};
    map.set("x", item);
    return item.value;
}
function empty() {
    const map = new Map();
    const item = {value: null};
    map.set("x", item);
    item.value = undefined;
    return map.size;
}
text();
empty();

//--- mixed-stores.js
function mixed() {
    const map = new Map();
    const item = {value: "first"};
    map.set("x", item);
    item.value = 1;
    item.value = "last";
    return map.size;
}
mixed();

//--- distinct-objects.js
// The return sees only the String object's group; the emitted member sees both.
function distinct() {
    const map = new Map();
    const number = {value: 1};
    const text = {value: "last"};
    map.set("x", number);
    map.set("y", text);
    return text.value;
}
distinct();

//--- incompatible-functions.js
function numeric() {
    const map = new Map();
    const item = {value: 1};
    map.set("x", item);
    return item.value;
}
function text() {
    const map = new Map();
    const item = {value: "owned"};
    map.set("x", item);
    return item.value;
}
numeric();
text();

//--- reversed-functions.js
// Carrier selection cannot depend on function declaration or visitation order.
function text() {
    const map = new Map();
    const item = {value: "owned"};
    map.set("x", item);
    return item.value;
}
function numeric() {
    const map = new Map();
    const item = {value: 1};
    map.set("x", item);
    return item.value;
}
text();
numeric();

//--- nullable-literal.js
// Literal repair of nullable.js; bare undefined remains an unproved host name.
function nullable(flag) {
    const map = new Map();
    const item = {value: "initial", count: 1};
    map.set("x", item);
    const before = item.value;
    if (flag) { item.value = null; }
    else { item.value = void 0; }
    item.count = 2;
    return before;
}
nullable(true);
nullable(false);

//--- compatible-literal.js
// A field that is absent in one function shares the String member in another.
function text() {
    const map = new Map();
    const item = {value: "owned"};
    map.set("x", item);
    return item.value;
}
function empty() {
    const map = new Map();
    const item = {value: null};
    map.set("x", item);
    item.value = void 0;
    return map.size;
}
text();
empty();

//--- compatible-readers.js
// Separate absent-only reads narrow the shared owning String member's tag.
function text() {
    const map = new Map();
    const item = {value: "owned"};
    map.set("x", item);
    return item.value;
}
function nullField() {
    const map = new Map();
    const item = {value: null};
    map.set("x", item);
    return item.value;
}
function undefinedField() {
    const map = new Map();
    const item = {value: void 0};
    map.set("x", item);
    return item.value;
}
text();
nullField();
undefinedField();
