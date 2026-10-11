// Listeners an ordinary script hands to addEventListener, which closure
// lifting specialises for their one call and the runtime's listener table
// (Runtime/Events.hpp) owns. EMIT checks the registration with and without
// captures, and that nothing is refused; each other prefix is one refusal
// with its reason. The differential run is CTNative/Browser/page/events.test.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/refusals.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s

// EMIT-DAG: emitc.include <"ctcompile/CTNative/Runtime/Events.hpp">
// EMIT-DAG: call_opaque "std::bind_front"
// EMIT-DAG: call_opaque "ctnative::EventTarget.prototype.addEventListener.call"

// CHECK-DAG: @later${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the listener of EventTarget.prototype.addEventListener captures binding 0, which is written after it is captured - the host keeps the listener, so it takes its captures by value"
// CHECK-DAG: @readsThis${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the listener of EventTarget.prototype.addEventListener reads `this`"
// CHECK-DAG: @takesEvent${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the listener of EventTarget.prototype.addEventListener declares 1 parameters - only a listener that takes no event is native"
// CHECK-DAG: @withOptions${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: EventTarget.prototype.addEventListener with options - only (type, listener) is native"
// CHECK-DAG: @capturesArray${{[0-9]+}}{{.*}}ctnative.not_native = "an array of Strings: an array passed to a call the native runtime does not implement"
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
function takesEvent() {
    document.documentElement.addEventListener('click', function (event) {
        seen = 'e';
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
later();
readsThis();
takesEvent();
withOptions();
capturesArray();
capturesStyle();
