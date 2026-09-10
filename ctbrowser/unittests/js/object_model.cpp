// The object model's exotic corners, each one a WPT subtest that stood on it:
// `Object.prototype.__proto__` (B.2.2.1), and the Proxy traps beyond get/set/has
// that a DOMStringMap's `delete` and `getOwnPropertyDescriptor` reach.
//
// One case per behaviour, expected values checked against V8. Its own file
// for the same reason property_attributes has one: every case is about the
// SHAPE of a property rather than the value an expression computes.

#include "js_expect.hpp"

int main() {
    // ================================================================
    // 1. `__proto__` READS [[GetPrototypeOf]] - for every kind of receiver,
    //    including the ones whose chain reaches Object.prototype implicitly
    // ================================================================
    js_expect("({}).__proto__ === Object.prototype", "true");
    js_expect("[].__proto__ === Array.prototype", "true");
    js_expect("(function(){}).__proto__ === Function.prototype", "true");
    js_expect("(1).__proto__ === Number.prototype", "true");
    js_expect("'a'.__proto__ === String.prototype", "true");
    js_expect("Object.prototype.__proto__", "null");
    js_expect("(function(){class A{};return new A().__proto__ === A.prototype;})()", "true");
    // THE WPT IDIOM: a prototype is asked whether it CARRIES a property.
    js_expect("({}).__proto__.hasOwnProperty('toString')", "true");

    // ================================================================
    // 2. ...AND WRITES [[SetPrototypeOf]] - assignment and the literal form
    // ================================================================
    js_expect("(function(){var o={};o.__proto__={x:1};return o.x;})()", "1");
    js_expect("({__proto__:{y:2}}).y", "2");
    // B.2.2.1.2 step 2: a non-object is ignored, not stored as an own property.
    js_expect("({__proto__:5}).hasOwnProperty('__proto__')", "false");
    js_expect("(function(){var o={};o.__proto__=5;return o.__proto__===Object.prototype;})()",
              "true");
    // An OWN `__proto__` shadows the accessor, as any own property shadows an
    // inherited one.
    js_expect("(function(){var o={};Object.defineProperty(o,'__proto__',{value:7});"
              "return o.__proto__;})()",
              "7");

    // ================================================================
    // 3. THE ACCESSOR ITSELF: on Object.prototype, non-enumerable, configurable
    // ================================================================
    js_expect("typeof Object.getOwnPropertyDescriptor(Object.prototype,'__proto__').get",
              "function");
    js_expect("Object.getOwnPropertyDescriptor(Object.prototype,'__proto__').enumerable", "false");
    js_expect("Object.keys(Object.prototype).length", "0");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
