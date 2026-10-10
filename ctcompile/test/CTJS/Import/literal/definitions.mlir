// AN OBJECT LITERAL'S MEMBERS ARE DEFINED, NOT ASSIGNED.
//
// `{k: v}` is CreateDataPropertyOrThrow (13.2.5.5): a setter or read-only `k`
// up the chain does not intercept it, and `["__proto__"]: v` is an ordinary own
// key. The browser compiler emits op::define_data for it and the importer makes
// it the ctjs.set_property every native proof already reads, marked
// `ctjs.define_own`; the boxed tier lowers the mark to ct_aot_define_data, not
// to the assignment ct_aot_set_index.
//
// AN OPCODE, NOT A CALL. b67016d91 first routed these through the global
// __ctbrowser_define_own. A call of a global a page can replace is an escape,
// so every literal-bearing native proof refused, and where the global object
// escapes (the HostContract controls that call `globalThis.foreign()`) no
// closed-world proof can say the global was not replaced. Class fields and
// `__home` still call the helper, as they did before b67016d91.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/keys.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=KEYS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/computed.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=COMPUTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/class.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=CLASS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/native.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boxed.js 2>/dev/null | ctjs-opt --ctjs-lower-to-emitc | mlir-translate --mlir-to-cpp --declare-variables-at-top | FileCheck %s --check-prefix=BOXED --implicit-check-not=ct_aot_set_index --implicit-check-not=__ctbrowser_define_own

// An identifier, a quoted and a numeric key: three marked stores, no helper.
// KEYS-NOT: __ctbrowser_define_own
// KEYS: %[[A:[0-9]+]] = ctjs.constant #ctjs.string<"a">
// KEYS: ctjs.set_property %[[O:[0-9]+]][%[[A]]], %{{[0-9]+}} {ctjs.define_own}
// KEYS: %[[B:[0-9]+]] = ctjs.constant #ctjs.string<"b c">
// KEYS: ctjs.set_property %[[O]][%[[B]]], %{{[0-9]+}} {ctjs.define_own}
// KEYS: ctjs.set_property %[[O]][%{{[0-9]+}}], %{{[0-9]+}} {ctjs.define_own}
// KEYS-NOT: __ctbrowser_define_own

// A computed key is converted once, before the value, and then defined.
// COMPUTED: %[[KEY:[0-9]+]] = ctjs.call %{{[0-9]+}}(%{{[0-9]+}}, %arg3)
// COMPUTED: ctjs.set_property %{{[0-9]+}}[%[[KEY]]], %{{[0-9]+}} {ctjs.define_own}

// `__proto__: v` sets the prototype; `["__proto__"]: v` defines an own key.
// PROTO: ctjs.set_proto
// PROTO: %[[KEY:[0-9]+]] = ctjs.constant #ctjs.string<"__proto__">
// PROTO: ctjs.set_property %{{[0-9]+}}[%[[KEY]]], %{{[0-9]+}} {ctjs.define_own}

// A class field is defined on `this` by the helper, unmarked, as before.
// CLASS-NOT: ctjs.define_own
// CLASS: ctjs.load_global "__ctbrowser_define_own"
// CLASS-NOT: ctjs.define_own

// Codex's probe of the regression: a literal read back lowers natively again.
// NATIVE: emitc.func @literal_1

// BOXED: ct_aot_define_data(

//--- keys.js
var o = {a: 1, "b c": 2, 3: 4};

//--- computed.js
function f(k) { return {[k]: 1}; }
var r = f("x");

//--- proto.js
var o = {__proto__: null, ["__proto__"]: 1};

//--- class.js
class C { x = 1; }
var c = new C();

//--- native.js
function literal() { var value = {text: "owned"}; return value.text; }
var a = literal();

//--- boxed.js
function make(v) { return {a: v}; }
