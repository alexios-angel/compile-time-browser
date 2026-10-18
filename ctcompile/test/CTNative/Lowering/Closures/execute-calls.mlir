// A callback handed to Bootstrap's execute helper (bootstrap.bundle.js `_`)
// with nothing else is called directly (ClosureLifting/ExecuteCalls.cpp), so
// the local-binding rule binds it: DIRECT lowers with no refusal and prints
// what the interpreter prints. A call that passes arguments, a program that
// names `Function`, and a helper without the `typeof` test keep the callback
// a value passed as an argument. The differential with a listener is
// CTNative/Browser/page/transitions.test.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/direct.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/direct.mlir
// RUN: FileCheck %s --check-prefix=DIRECT --implicit-check-not=ctnative.not_native < %t/direct.mlir
// RUN: ctjs-opt %t/direct.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/direct.cpp
// RUN: %gxx -O2 %t/direct.cpp -o %t/direct-gcc
// RUN: %t/direct-gcc | FileCheck %s --check-prefix=DIRECTOUT
// RUN: %clangxx -O2 %t/direct.cpp -o %t/direct-clang
// RUN: %t/direct-clang | FileCheck %s --check-prefix=DIRECTOUT
// RUN: %native_reference %t/direct.js | FileCheck %s --check-prefix=DIRECTOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arguments.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARGUMENTS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/function.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FUNCTION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/apply.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=APPLY

// `once` calls its callback, which captures `label`, directly.
// DIRECT: emitc.func @once_{{[0-9]+}}(
// DIRECT: call @fn_{{[0-9]+}}(%arg0)
// DIRECTOUT: log="ab"

// ARGUMENTS: @withArguments${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"
// FUNCTION: @namesFunction${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"
// APPLY: @applies${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"

//--- direct.js
var log = '';
(function () {
    const execute = (possibleCallback, args = [], defaultValue = possibleCallback) =>
        typeof possibleCallback === 'function' ? possibleCallback.call(...args) : defaultValue;
    function once(label) {
        const callback = () => {
            log = log + label;
        };
        execute(callback);
    }
    once('a');
    once('b');
})();

//--- arguments.js
var log = '';
(function () {
    const execute = (possibleCallback, args = [], defaultValue = possibleCallback) =>
        typeof possibleCallback === 'function' ? possibleCallback.call(...args) : defaultValue;
    function withArguments(label) {
        const callback = () => {
            log = log + label;
        };
        execute(callback, [label]);
    }
    withArguments('a');
})();

//--- function.js
var log = '';
var named = Function;
(function () {
    const execute = (possibleCallback, args = [], defaultValue = possibleCallback) =>
        typeof possibleCallback === 'function' ? possibleCallback.call(...args) : defaultValue;
    function namesFunction(label) {
        const callback = () => {
            log = log + label;
        };
        execute(callback);
    }
    namesFunction('a');
})();

//--- apply.js
var log = '';
(function () {
    const apply = (possibleCallback, args = []) => possibleCallback.call(...args);
    function applies(label) {
        const callback = () => {
            log = log + label;
        };
        apply(callback);
    }
    applies('a');
})();
