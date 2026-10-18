// A callback handed down through a function to the code that waits for a
// transition, Bootstrap's _queueCallback shape: the per-call-site copies
// (ClosureLifting/CallSiteCopies.cpp) make the callback in the copy of the
// last link, and the relay (ClosureLifting/ExecuteCalls.cpp) makes it where it
// is called, so the listener that captured it calls the callback's function
// itself and execute's call of it is a direct call. The differential run on the
// interpreter, with clang and with GCC, is CTNative/Browser/page/transitions.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/listener.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LISTENER --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/first.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FIRST --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/execute.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EXECUTE --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STORED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/written.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=WRITTEN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/args.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARGS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/function.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FUNCTION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shared.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SHARED --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shared-escaped.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SHAREDESCAPED

// The listener calls the callback. Each call of `queue` gets its own copy of
// `queue`, `whenEnds` and the listener, which takes the label the callback
// captures and calls the callback's function with it.
// LISTENER: ctnative.copies = {fn$[[HANDLER:[0-9]+]] = "fn$3", queue${{[0-9]+}} = "queue$4", whenEnds${{[0-9]+}} = "whenEnds$2"}
// LISTENER: emitc.func @whenEnds_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_element_t">, %arg1: !emitc.opaque<"ctnative::js_string">)
// LISTENER: call_opaque "std::bind_front"(%{{[0-9]+}}, %arg0, %{{[0-9]+}}, %arg1)
// LISTENER: emitc.func @fn_[[HANDLER]](%arg0: !emitc.opaque<"ctnative::js_element_t">, %arg1: !emitc.opaque<"ctnative::callable_identity">, %arg2: !emitc.opaque<"ctnative::js_string">)
// LISTENER: call @fn_{{[0-9]+}}(%arg2) : (!emitc.opaque<"ctnative::js_string">)

// The listener calls the callback before it removes itself, so the box's
// capture comes before the listener's own, and dropping it moves the slot
// that carries the listener's identity (Bindings.cpp, removeCaptureSlots).
// FIRST: emitc.func @fn_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_element_t">, %arg1: !emitc.opaque<"ctnative::callable_identity">, %arg2: !emitc.opaque<"ctnative::js_string">)
// FIRST: call @fn_{{[0-9]+}}(%arg2)
// FIRST: removeEventListener.call"(%arg0, %{{[0-9]+}}, %arg1)

// Bootstrap's shape: the last link runs the callback through execute at once,
// or from the listener. Both calls become direct calls; the listener's copy
// calls a copy of the callback's function, since each function has one
// closure that makes it. The execute helper is left with calls only in the
// originals no call reaches any more, so it goes with them.
// EXECUTE: ctnative.copies = {afterTransition${{[0-9]+}} = "afterTransition$2", afterTransition${{[0-9]+}} = "afterTransition$2", fn${{[0-9]+}} = "fn$3", fn${{[0-9]+}} = "fn$6", fn${{[0-9]+}} = "fn$3", fn${{[0-9]+}} = "fn$7", queue${{[0-9]+}} = "queue$4", queue${{[0-9]+}} = "queue$4"}
// EXECUTE-SAME: ctnative.unreachable_refusals = [{function = "afterTransition$2"{{.*}}{function = "fn$8", reason = "removed: unreachable
// EXECUTE: emitc.func @afterTransition_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_element_t">, %arg1: !emitc.opaque<"ctnative::js_boolean_t">, %arg2: !emitc.opaque<"ctnative::js_string">)
// EXECUTE: emitc.call @fn_{{[0-9]+}}(%arg2)
// EXECUTE: call_opaque "std::bind_front"(%{{[0-9]+}}, %arg0, %{{[0-9]+}}, %arg2)
// EXECUTE: emitc.func @fn_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_element_t">, %arg1: !emitc.opaque<"ctnative::callable_identity">, %arg2: !emitc.opaque<"ctnative::js_string">)
// EXECUTE: call @fn_{{[0-9]+}}(%arg2) : (!emitc.opaque<"ctnative::js_string">)

// Negatives: each keeps the callback a value passed as an argument. The
// listener also stores it; the last link writes its binding; the listener
// hands execute an argument list; the program names `Function`, so no call
// of execute may become a plain call.
// STORED: @parts${{[0-9]+}}({{.*}}ctnative.not_native = "a closure used as a value: it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"
// WRITTEN: @parts${{[0-9]+}}({{.*}}ctnative.not_native = "a closure used as a value: it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"
// ARGS: @parts${{[0-9]+}}({{.*}}ctnative.not_native = "a closure used as a value: it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"
// FUNCTION: @parts${{[0-9]+}}({{.*}}ctnative.not_native = "a closure used as a value: it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"

// The original of the last link, which no call reaches once both calls have
// copies, also calls `trigger` from its timer. The copier makes such an
// original private and admission skips it (CallSiteCopies.cpp, LowerToEmitC's
// closeNativeSet), so `trigger` is typed by its live callers alone and the
// whole program lowers; the originals are removed as unreachable. In
// shared-escaped.js a function handed to the global object still calls the
// original, which keeps a caller no one proves, so `trigger` keeps its
// refusal; the copies are made there too.
// SHARED: ctnative.copies = {afterTransition${{[0-9]+}} = "afterTransition$[[AT:[0-9]+]]"
// SHARED-SAME: ctnative.unreachable_refusals = [{function = "afterTransition$[[AT]]", reason = "removed: unreachable
// SHARED: emitc.func @trigger_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_element_t">)
// SHAREDESCAPED: ctnative.copies = {afterTransition${{[0-9]+}} = "afterTransition${{[0-9]+}}"
// SHAREDESCAPED: @trigger${{[0-9]+}}({{.*}}ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it

//--- listener.js
var log = '';
(function () {
    function whenEnds(callback, element) {
        const handler = () => {
            element.removeEventListener('transitionend', handler);
            callback();
        };
        element.addEventListener('transitionend', handler);
    }
    function queue(callback, element) {
        whenEnds(callback, element);
    }
    function parts(part, label) {
        queue(() => {
            log = log + label;
        }, part);
    }
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        parts(kids[k], 'k');
    }
})();

//--- first.js
var log = '';
(function () {
    function whenEnds(callback, element) {
        const handler = () => {
            callback();
            element.removeEventListener('transitionend', handler);
        };
        element.addEventListener('transitionend', handler);
    }
    function queue(callback, element) {
        whenEnds(callback, element);
    }
    function parts(part, label) {
        queue(() => {
            log = log + label;
        }, part);
    }
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        parts(kids[k], 'k');
    }
})();

//--- execute.js
var log = '';
(function () {
    const execute = (possibleCallback, args = [], defaultValue = possibleCallback) =>
        typeof possibleCallback === 'function' ? possibleCallback.call(...args) : defaultValue;
    function afterTransition(callback, element, waits) {
        if (!waits) {
            execute(callback);
            return;
        }
        const handler = () => {
            element.removeEventListener('transitionend', handler);
            execute(callback);
        };
        element.addEventListener('transitionend', handler);
    }
    function queue(callback, element, waits) {
        afterTransition(callback, element, waits);
    }
    function parts(part, label) {
        queue(() => {
            log = log + 'a' + label;
        }, part, true);
        queue(() => {
            log = log + 'b' + label;
        }, part, false);
    }
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        parts(kids[k], 'k');
    }
})();

//--- stored.js
var log = '';
var saved;
(function () {
    function whenEnds(callback, element) {
        const handler = () => {
            element.removeEventListener('transitionend', handler);
            saved = callback;
            callback();
        };
        element.addEventListener('transitionend', handler);
    }
    function queue(callback, element) {
        whenEnds(callback, element);
    }
    function parts(part, label) {
        queue(() => {
            log = log + label;
        }, part);
    }
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        parts(kids[k], 'k');
    }
})();

//--- written.js
var log = '';
(function () {
    function whenEnds(callback, element) {
        const handler = () => {
            element.removeEventListener('transitionend', handler);
            callback();
        };
        element.addEventListener('transitionend', handler);
        callback = null;
    }
    function queue(callback, element) {
        whenEnds(callback, element);
    }
    function parts(part, label) {
        queue(() => {
            log = log + label;
        }, part);
    }
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        parts(kids[k], 'k');
    }
})();

//--- args.js
var log = '';
(function () {
    const execute = (possibleCallback, args = [], defaultValue = possibleCallback) =>
        typeof possibleCallback === 'function' ? possibleCallback.call(...args) : defaultValue;
    function afterTransition(callback, element, waits) {
        if (!waits) {
            execute(callback);
            return;
        }
        const handler = () => {
            element.removeEventListener('transitionend', handler);
            execute(callback, [element]);
        };
        element.addEventListener('transitionend', handler);
    }
    function queue(callback, element, waits) {
        afterTransition(callback, element, waits);
    }
    function parts(part, label) {
        queue(() => {
            log = log + 'a' + label;
        }, part, true);
        queue(() => {
            log = log + 'b' + label;
        }, part, false);
    }
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        parts(kids[k], 'k');
    }
})();

//--- function.js
var log = '';
var named = Function;
(function () {
    const execute = (possibleCallback, args = [], defaultValue = possibleCallback) =>
        typeof possibleCallback === 'function' ? possibleCallback.call(...args) : defaultValue;
    function afterTransition(callback, element, waits) {
        if (!waits) {
            execute(callback);
            return;
        }
        const handler = () => {
            element.removeEventListener('transitionend', handler);
            execute(callback);
        };
        element.addEventListener('transitionend', handler);
    }
    function queue(callback, element, waits) {
        afterTransition(callback, element, waits);
    }
    function parts(part, label) {
        queue(() => {
            log = log + 'a' + label;
        }, part, true);
        queue(() => {
            log = log + 'b' + label;
        }, part, false);
    }
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        parts(kids[k], 'k');
    }
})();

//--- shared.js
var log = '';
(function () {
    const execute = (possibleCallback, args = [], defaultValue = possibleCallback) =>
        typeof possibleCallback === 'function' ? possibleCallback.call(...args) : defaultValue;
    function trigger(element) {
        element.dispatchEvent(new Event('transitionend'));
    }
    function afterTransition(callback, element, waits) {
        if (!waits) {
            execute(callback);
            return;
        }
        const handler = () => {
            element.removeEventListener('transitionend', handler);
            execute(callback);
        };
        element.addEventListener('transitionend', handler);
        setTimeout(() => {
            trigger(element);
        }, 5);
    }
    function queue(callback, element, waits) {
        afterTransition(callback, element, waits);
    }
    function parts(part, label) {
        queue(() => {
            log = log + 'a' + label;
        }, part, true);
        queue(() => {
            log = log + 'b' + label;
        }, part, false);
    }
    trigger(document.documentElement);
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        parts(kids[k], 'k');
    }
})();

//--- shared-escaped.js
var log = '';
(function () {
    const execute = (possibleCallback, args = [], defaultValue = possibleCallback) =>
        typeof possibleCallback === 'function' ? possibleCallback.call(...args) : defaultValue;
    function trigger(element) {
        element.dispatchEvent(new Event('transitionend'));
    }
    function afterTransition(callback, element, waits) {
        if (!waits) {
            execute(callback);
            return;
        }
        const handler = () => {
            element.removeEventListener('transitionend', handler);
            execute(callback);
        };
        element.addEventListener('transitionend', handler);
        setTimeout(() => {
            trigger(element);
        }, 5);
    }
    function queue(callback, element, waits) {
        afterTransition(callback, element, waits);
    }
    function later(callback, element) {
        afterTransition(callback, element, true);
    }
    globalThis.later = later;
    function parts(part, label) {
        queue(() => {
            log = log + 'a' + label;
        }, part, true);
        queue(() => {
            log = log + 'b' + label;
        }, part, false);
    }
    trigger(document.documentElement);
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        parts(kids[k], 'k');
    }
})();
