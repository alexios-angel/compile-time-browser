// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/source.js 2>/dev/null | ctjs-opt | FileCheck %s
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/refused.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=REFUSED

// Source reference mode survives register forwarding. The first three hard
// reads below can have the same LoadGlobal-to-TypeOf graph as a direct typeof;
// neither a sole TypeOf user nor bytecode adjacency grants a soft lookup.

//--- source.js
function direct() { return typeof missingDirect; }
function parenthesized() { return typeof (missingParens); }
function comma() { return typeof (0, missingComma); }
function alias() { var saved = missingAlias; return typeof saved; }
function conditional(flag) { return typeof (flag ? missingBranch : 0); }
function receiver() { return typeof missingReceiver.value; }
function property(object) { return typeof object.value; }
function local(value) { return typeof value; }
function tdz(value = typeof value) { return value; }
function withLookup(object) { with (object) { return typeof missingWith; } }
function withComma(object) { with (object) { return typeof (0, missingWithComma); } }

// CHECK-NOT: ctjs.skipped
// CHECK-LABEL: ctjs.func @direct$
// CHECK: %[[DIRECT:[^ ]+]] = ctjs.load_global "missingDirect" {typeof_lookup = true}
// CHECK: ctjs.unary typeof %[[DIRECT]]
// CHECK-LABEL: ctjs.func @parenthesized$
// CHECK: %[[PARENS:[^ ]+]] = ctjs.load_global "missingParens" {typeof_lookup = true}
// CHECK: ctjs.unary typeof %[[PARENS]]
// CHECK-LABEL: ctjs.func @comma$
// CHECK: %[[COMMA:[^ ]+]] = ctjs.load_global "missingComma"{{$}}
// CHECK: ctjs.unary typeof %[[COMMA]]
// CHECK-LABEL: ctjs.func @alias$
// CHECK: %[[ALIAS:[^ ]+]] = ctjs.load_global "missingAlias"{{$}}
// CHECK: ctjs.unary typeof %[[ALIAS]]
// CHECK-LABEL: ctjs.func @conditional$
// CHECK: ctjs.load_global "missingBranch"{{$}}
// CHECK: ctjs.unary typeof
// CHECK-LABEL: ctjs.func @receiver$
// CHECK: %[[RECEIVER:[^ ]+]] = ctjs.load_global "missingReceiver"{{$}}
// CHECK: %[[FIELD:[^ ]+]] = ctjs.get_property %[[RECEIVER]][
// CHECK: ctjs.unary typeof %[[FIELD]]
// CHECK-LABEL: ctjs.func @property$
// CHECK-NOT: typeof_lookup
// CHECK: %[[PROPERTY:[^ ]+]] = ctjs.get_property %arg3[
// CHECK: ctjs.unary typeof %[[PROPERTY]]
// CHECK-LABEL: ctjs.func @local$
// CHECK-NOT: typeof_lookup
// CHECK-NOT: ctjs.load_global
// CHECK: ctjs.unary typeof %arg3
// CHECK-LABEL: ctjs.func @tdz$
// CHECK-NOT: typeof_lookup
// CHECK: ctjs.load_global "ReferenceError"{{$}}
// CHECK: ctjs.throw
// CHECK-NOT: typeof_lookup
// CHECK-LABEL: ctjs.func @withLookup$
// CHECK: ctjs.load_global "missingWith" {typeof_lookup = true}
// CHECK-NOT: ctjs.unary typeof
// CHECK: ctjs.get_property
// CHECK: ctjs.unary typeof
// CHECK-NOT: ctjs.unary typeof
// CHECK-LABEL: ctjs.func @withComma$
// CHECK-NOT: typeof_lookup
// CHECK: ctjs.load_global "missingWithComma"{{$}}
// CHECK-NOT: typeof_lookup
// CHECK: ctjs.unary typeof
// CHECK-NOT: typeof_lookup
// CHECK-NOT: ctjs.skipped

//--- refused.js
function* refused() { typeof Function; yield 1; }

// A skipped body still contributes its complete global-effects summary when
// the source used the dedicated TypeOf lookup opcode.
// REFUSED: ctjs.skipped
// REFUSED-SAME: function = 1 : i32
// REFUSED-SAME: opaque = "a refused body reads `Function`, and the program it compiles can store any global"
// REFUSED-SAME: opcode = "yield_value"
// REFUSED-NOT: ctjs.func @refused$
