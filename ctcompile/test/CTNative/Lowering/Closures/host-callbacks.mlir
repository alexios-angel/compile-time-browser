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

// EMIT-DAG: emitc.include <"ctcompile/CTNative/Runtime/Events.hpp">
// EMIT-DAG: call_opaque "std::bind_front"
// EMIT-DAG: call_opaque "ctnative::EventTarget.prototype.addEventListener.call"
// EMIT-DAG: emitc.include <"ctcompile/CTNative/Runtime/Timers.hpp">
// EMIT-DAG: call_opaque "ctnative::setTimeout"
// EMIT-DAG: call_opaque "ctnative::Event.prototype.preventDefault.call"
// EMIT-DAG: call_opaque "ctnative::Event.prototype.stopPropagation.call"
// EMIT-DAG: member_call_opaque {{.*}} "type"()
// EMIT-DAG: member_call_opaque {{.*}} "currentTarget"()
// EMIT-DAG: !emitc.opaque<"ctnative::js_event">

// CHECK-DAG: @later${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the listener of EventTarget.prototype.addEventListener captures binding 0, which is written after it is captured - the host keeps the listener, so it takes its captures by value"
// CHECK-DAG: @readsThis${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the listener of EventTarget.prototype.addEventListener reads `this`"
// CHECK-DAG: @takesTwo${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the listener of EventTarget.prototype.addEventListener declares 2 parameters - only a listener that takes the event alone is native"
// CHECK-DAG: ctnative.not_native = "a listener's Event used other than through its reads and its preventDefault and stopPropagation calls - it lives only while the listener runs"
// CHECK-DAG: ctnative.not_native = "an Event read through `clientX` - only type, target, currentTarget and defaultPrevented are native"
// CHECK-DAG: @withOptions${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: EventTarget.prototype.addEventListener with options - only (type, listener) is native"
// CHECK-DAG: @capturesArray${{[0-9]+}}{{.*}}ctnative.not_native = "an array of Strings: an array passed to a call the native runtime does not implement"
// CHECK-DAG: @timerLater${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the handler of setTimeout captures binding 0, which is written after it is captured - the host keeps the handler, so it takes its captures by value"
// CHECK-DAG: @timerThis${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the handler of setTimeout reads `this`"
// CHECK-DAG: @timerArgs${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: setTimeout with 3 arguments - only (handler, timeout) is native"
// CHECK-DAG: @timerParameter${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the handler of setTimeout declares 1 parameters - only a handler that takes none is native"
// CHECK-DAG: @timerHandle${{[0-9]+}}{{.*}}ctnative.not_native = "the result of setTimeout is read"
// CHECK-DAG: @capturesStyle${{[0-9]+}}{{.*}}ctnative.not_native = "the listener of addEventListener captures {{.*}}computed_style{{.*}} - the host keeps a copy, which only a Number, a Boolean, a String or an element shares"

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
setTimeout(function () {
    seen = seen + 't';
}, 0);
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
    }, true);
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
function timerHandle() {
    var id = setTimeout(function () {
        seen = 'h';
    }, 0);
    return id;
}
later();
timerLater();
timerThis();
timerArgs();
timerParameter();
timerHandle();
readsThis();
takesTwo();
storesEvent();
returnsEvent();
readsClientX();
withOptions();
capturesArray();
capturesStyle();
