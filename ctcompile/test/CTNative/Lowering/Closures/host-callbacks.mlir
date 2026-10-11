// Listeners an ordinary script hands to addEventListener and handlers it hands
// to setTimeout, which closure lifting specialises for their one call and the
// runtime's listener table (Runtime/Events.hpp) or timer table
// (Runtime/Timers.hpp) owns. EMIT checks each registration with and without
// captures, and that nothing is refused; each CHECK line is one refusal with
// its reason. The differential runs are CTNative/Browser/page/events.test and
// timers.test.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/refusals.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DYNAMIC

// A window listener leaves the globals typed (EMIT stores them and registers
// one); a window that also flows into a property write may write any global,
// so the globals are boxed (TypeInference::isInertWindowLoad).
// DYNAMIC: ctnative.not_native = "global `count` is !ctnative.boxed

// EMIT-DAG: emitc.include <"ctcompile/CTNative/Runtime/Events.hpp">
// EMIT-DAG: call_opaque "std::bind_front"
// EMIT-DAG: call_opaque "ctnative::EventTarget.prototype.addEventListener.call"
// EMIT-DAG: emitc.include <"ctcompile/CTNative/Runtime/Timers.hpp">
// EMIT-DAG: call_opaque "ctnative::setTimeout"
// EMIT-DAG: call_opaque "ctnative::setInterval"
// EMIT-DAG: call_opaque "ctnative::clearTimeout"
// EMIT-DAG: call_opaque "ctnative::clearInterval"
// EMIT-DAG: member_call_opaque {{.*}} "is_set"()
// EMIT-DAG: call_opaque "ctnative::Event.prototype.preventDefault.call"
// EMIT-DAG: call_opaque "ctnative::Event.prototype.stopPropagation.call"
// EMIT-DAG: member_call_opaque {{.*}} "type"()
// EMIT-DAG: member_call_opaque {{.*}} "currentTarget"()
// EMIT-DAG: !emitc.opaque<"ctnative::js_event">
// EMIT-DAG: literal "ctnative::document"
// EMIT-DAG: literal "ctnative::window"
// EMIT-DAG: call_opaque "ctnative::Event"
// EMIT-DAG: call_opaque "ctnative::event_init"
// EMIT-DAG: call_opaque "ctnative::EventTarget.prototype.dispatchEvent.call"
// EMIT-DAG: call_opaque "ctnative::new_callable_identity"
// EMIT-DAG: call_opaque "ctnative::identified_listener"
// EMIT-DAG: call_opaque "ctnative::EventTarget.prototype.removeEventListener.call"

// CHECK-DAG: @later${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the listener of EventTarget.prototype.addEventListener captures binding 0, which is written after it is captured - the host keeps the listener, so it takes its captures by value"
// CHECK-DAG: @readsThis${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the listener of EventTarget.prototype.addEventListener reads `this`"
// CHECK-DAG: @takesTwo${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the listener of EventTarget.prototype.addEventListener declares 2 parameters - only a listener that takes the event alone is native"
// CHECK-DAG: ctnative.not_native = "an Event read through `clientX` - only type, target, currentTarget and defaultPrevented are native"
// CHECK-DAG: ctnative.not_native = "addEventListener with options of {{.*}} - only a Boolean capture flag is native"
// CHECK-DAG: @composedInit${{[0-9]+}}{{.*}}ctnative.not_native = "new Event with an init that is not an object literal of `bubbles` and `cancelable` - only that EventInit is native"
// CHECK-DAG: @initEscapes${{[0-9]+}}{{.*}}ctnative.not_native = "new Event with an init that is not an object literal of `bubbles` and `cancelable` - only that EventInit is native"
// CHECK-DAG: @returnsMade${{[0-9]+}}{{.*}}ctnative.not_native = "an Event used other than through its reads, its preventDefault and stopPropagation calls and dispatchEvent - it does not leave its function"
// CHECK-DAG: ctnative.not_native = "an Event used other than through its reads, its preventDefault and stopPropagation calls and dispatchEvent - it does not leave its function"
// CHECK-DAG: ctnative.not_native = "currentTarget of a listener on the document or the window - only an element's listener reads it natively"
// CHECK-DAG: @capturesArray${{[0-9]+}}{{.*}}ctnative.not_native = "an array of Strings: an array passed to a call the native runtime does not implement"
// CHECK-DAG: @timerLater${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the handler of setTimeout captures binding 0, which is written after it is captured - the host keeps the handler, so it takes its captures by value"
// CHECK-DAG: @timerThis${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the handler of setTimeout reads `this`"
// CHECK-DAG: @timerArgs${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: setTimeout with 3 arguments - only (handler, timeout) is native"
// CHECK-DAG: @timerParameter${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the handler of setTimeout declares 1 parameters - only a handler that takes none is native"
// CHECK-DAG: @clearNumber${{[0-9]+}}{{.*}}ctnative.not_native = "clearing a timer with !ctnative.num<{{.*}}>, not a handle setTimeout or setInterval made"
// CHECK-DAG: @timerArithmetic${{[0-9]+}}{{.*}}ctnative.not_native = "binary operand is !ctnative.timer, not a number"
// CHECK-DAG: @timerGlobal${{[0-9]+}}{{.*}}ctnative.not_native = "store to global `keptHandle` is !ctnative.timer; native global observations require a supported scalar type"
// CHECK-DAG: @capturesStyle${{[0-9]+}}{{.*}}ctnative.not_native = "the listener of addEventListener captures {{.*}}computed_style{{.*}} - the host keeps a copy, which only a Number, a Boolean, a String, an element or a timer's handle shares"

//--- emit.js
var seen = '';
document.documentElement.addEventListener('click', function () {
    seen = seen + 'r';
});
function listen(name) {
    document.documentElement.addEventListener('click', function () {
        seen = seen + name;
    });
}
listen('b');
document.documentElement.addEventListener('click', function (event) {
    event.preventDefault();
    event.stopPropagation();
    seen = event.type + event.currentTarget.id + (event.defaultPrevented ? '!' : '');
});
document.addEventListener('click', function (event) {
    seen = event.target.id;
}, true);
window.addEventListener('click', function () {
    seen = 'w';
});
function fire() {
    var root = document.documentElement;
    var heard = root.dispatchEvent(new Event('note', {cancelable: true}));
    var made = new Event('note');
    root.dispatchEvent(made);
    seen = seen + (made.defaultPrevented ? '!' : '') + (heard ? 'h' : '');
}
fire();
function toggle() {
    var root = document.documentElement;
    var listener = function (event) {
        seen = seen + event.type;
    };
    root.addEventListener('click', listener, true);
    root.removeEventListener('click', listener, true);
}
toggle();
setTimeout(function () {
    seen = seen + 't';
}, 0);
function keep() {
    var once = setTimeout(function () {
        seen = seen + 'o';
    }, 5);
    var every = setInterval(function () {
        seen = seen + 'v';
    }, 5);
    if (every) {
        clearTimeout(once);
    }
    clearInterval(every);
}
keep();
function wait(name) {
    setTimeout(function () {
        seen = seen + name;
    }, 5);
}
wait('w');

//--- refusals.js
var seen = '';
function later() {
    var label = 'a';
    document.documentElement.addEventListener('click', function () {
        seen = label;
    });
    label = 'b';
}
function readsThis() {
    document.documentElement.addEventListener('click', function () {
        seen = this.id;
    });
}
function takesTwo() {
    document.documentElement.addEventListener('click', function (event, more) {
        seen = 'e';
    });
}
var kept;
function storesEvent() {
    document.documentElement.addEventListener('click', function (event) {
        kept = event;
    });
}
function returnsEvent() {
    document.documentElement.addEventListener('click', function (event) {
        return event;
    });
}
function readsClientX() {
    document.documentElement.addEventListener('click', function (event) {
        seen = 'x' + event.clientX;
    });
}
function withOptions() {
    document.documentElement.addEventListener('click', function () {
        seen = 'o';
    }, 'capture');
}
function composedInit() {
    document.documentElement.dispatchEvent(new Event('x', {composed: true}));
}
function returnsMade() {
    return new Event('x');
}
var keptInit;
function initEscapes() {
    var init = {bubbles: true};
    keptInit = init;
    document.documentElement.dispatchEvent(new Event('x', init));
}
function documentCurrentTarget() {
    document.addEventListener('click', function (event) {
        seen = event.currentTarget.id;
    });
}
function capturesArray() {
    var names = ['a', 'b'];
    document.documentElement.addEventListener('click', function () {
        seen = names[0];
    });
}
function capturesStyle() {
    var style = getComputedStyle(document.documentElement);
    document.documentElement.addEventListener('click', function () {
        seen = style.getPropertyValue('display');
    });
}
function timerLater() {
    var label = 'a';
    setTimeout(function () {
        seen = label;
    }, 0);
    label = 'b';
}
function timerThis() {
    setTimeout(function () {
        seen = this.name;
    }, 0);
}
function timerArgs() {
    setTimeout(function () {
        seen = 'x';
    }, 0, 'extra');
}
function timerParameter() {
    setTimeout(function (late) {
        seen = 'p';
    }, 0);
}
function timerArithmetic() {
    var id = setTimeout(function () {
        seen = 'h';
    }, 0);
    return id + 1;
}
var keptHandle;
function timerGlobal() {
    keptHandle = setInterval(function () {
        seen = 'k';
    }, 5);
}
function clearNumber() {
    clearTimeout(5);
}
later();
timerLater();
timerThis();
timerArgs();
timerParameter();
timerArithmetic();
timerGlobal();
clearNumber();
readsThis();
takesTwo();
storesEvent();
returnsEvent();
readsClientX();
withOptions();
documentCurrentTarget();
composedInit();
returnsMade();
initEscapes();
capturesArray();
capturesStyle();

//--- dynamic.js
var count = 0;
window.addEventListener('click', function () {
    count = count + 1;
});
window.foo = 1;
