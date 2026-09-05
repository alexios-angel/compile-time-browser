// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-callback-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escape.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ESCAPE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED --implicit-check-not=ctnative.callback}
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/captured.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CAPTURED --implicit-check-not=ctnative.callback}
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/raw.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=RAW --implicit-check-not=ctnative.callback}
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/open.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=OPEN --implicit-check-not=ctnative.callback}
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/identity.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=IDENTITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/lexical.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=LEXICAL --implicit-check-not=ctnative.callback}
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/self.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SELF
// RUN: ctjs-opt --ctnative-lower-to-emitc %t/new-target.mlir | FileCheck %s --check-prefix=NEW-TARGET --implicit-check-not=ctnative.callback}

// NATIVE: emitc.func @main() -> i32
// NATIVE: emitc.func @initialize_{{[0-9]+}}() -> f64
// NATIVE: call @bootstrapFactory_{{[0-9]+}}()
// NATIVE: emitc.func @bootstrapFactory_{{[0-9]+}}() -> f64

// ESCAPE: ctjs.func private @fn$1
// ESCAPE-SAME: ctnative.callback_refusal = "the callback value or its identity escapes the call-only parameter"
// ESCAPE: ctjs.call_direct @fn$2({{.*}}) {ctnative.callback}
// ESCAPE: ctjs.call
// ESCAPE: ctjs.func @fn$2
// ESCAPE-SAME: ctnative.not_native

// MIXED: ctnative.callback_refusal = "callers do not supply one known capture-free callback target"
// CAPTURED: ctnative.callback_refusal = "callers do not supply one known capture-free callback target"
// RAW: ctnative.callback_refusal = "the wrapper reads its raw argument window"
// OPEN: ctnative.callback_refusal = "not every caller of the wrapper is a direct call of its closure"
// IDENTITY: ctnative.callback_refusal = "the callback value or its identity escapes the call-only parameter"
// IDENTITY: ctjs.call_direct @fn$2({{.*}}) {ctnative.callback}
// LEXICAL: ctnative.callback_refusal = "callers do not supply one known capture-free callback target"
// SELF: ctnative.callback_refusal = "the callback value or its identity escapes the call-only parameter"
// SELF: ctjs.call_direct @self$2({{.*}}) {ctnative.callback}
// NEW-TARGET: ctnative.callback_refusal = "the wrapper passes new.target"

//--- escape.js
var result = (function (factory) {
    var value = factory();
    external(factory);
    return value;
})(function () { return 42; });

//--- lexical.js
var result = (function (factory) {
    return factory();
})(() => this.marker);

//--- self.js
var result = (function (factory) {
    return factory();
})(function self() { return self; });

//--- new-target.mlir
module {
  ctjs.func @_script_$0(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %wrapper = ctjs.create_closure %callee[1] this %undefined
    %factory = ctjs.create_closure %callee[2] this %undefined
    %result = ctjs.call_direct @wrapper$1(%undefined, %wrapper, %wrapper, %factory)
    ctjs.return %result
  }
  ctjs.func @wrapper$1(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value, %factory: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    ctjs.pass_new_target
    %result = ctjs.call %factory(%undefined)
    ctjs.return %result
  }
  ctjs.func @factory$2(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.return %new_target
  }
}

//--- mixed.js
function run() {
    var wrapper = function (factory) { return factory(); };
    return wrapper(function () { return 10; }) + wrapper(function () { return 20; });
}
var result = run();

//--- captured.js
function run(seed) {
    var factory = function () { return seed; };
    return (function (callback) { return callback(); })(factory);
}
var result = run(42);

//--- raw.js
var result = (function (factory) {
    return factory() + arguments.length;
})(function () { return 42; });

//--- open.js
function run() {
    var wrapper = function (factory) { return factory(); };
    external(wrapper);
    return wrapper(function () { return 42; });
}
var result = run();

//--- identity.js
var result = (function (factory) {
    var copy = factory;
    return factory() + (copy === factory ? 1 : 2);
})(function () { return 42; });
