// IsConstructor (7.2.4), as `new` and Reflect.construct see it: a built-in
// METHOD, a getter, an arrow, a generator and an async function have no
// [[Construct]], so `new Math.abs()` is a TypeError. test262's isConstructor.js
// asks `Reflect.construct(function(){}, [], f)` and expects the newTarget check
// to refuse - ~170 `not-a-constructor.js` files across the built-ins.
//
// The default is the other way for an embedder's native (`new Image()`, `new
// DOMParser()`): those set nothing and must keep constructing.

#include "js_expect.hpp"

int main() {
    // what throws, spelled the way the harness spells it
    js_expect("(function(){ try { new Math.abs(); return 'no'; } catch (e) { return e.constructor "
              "=== TypeError; } })()",
              "true");
    js_expect("(function(){ try { Reflect.construct(function(){}, [], Math.abs); return 'no'; } "
              "catch (e) { return e.constructor === TypeError; } })()",
              "true");
    js_expect("(function(){ try { Reflect.construct(Math.abs, []); return 'no'; } catch (e) { "
              "return e.constructor === TypeError; } })()",
              "true");
    js_expect("(function(){ try { Reflect.construct(() => 1, []); return 'no'; } catch (e) { "
              "return e.constructor === TypeError; } })()",
              "true");
    js_expect("(function(){ try { new Array.prototype.at(); return 'no'; } catch (e) { return "
              "e.constructor === TypeError; } })()",
              "true");
    js_expect(
        "(function(){ var d = Object.getOwnPropertyDescriptor(Map.prototype, 'size'); try "
        "{ new d.get(); return 'no'; } catch (e) { return e.constructor === TypeError; } })()",
        "true");
    // what still constructs
    js_expect("typeof Reflect.construct(Object, [])", "object");
    js_expect("Reflect.construct(Array, [3]).length", "3");
    js_expect("new Number(5) + 1", "6");
    js_expect("(function(){ function F() { this.x = 1; } return Reflect.construct(F, [], "
              "function(){}).x; })()",
              "1");
    js_expect("(function(){ class C {} return Reflect.construct(C, []) instanceof C; })()", "true");
    js_expect("new (new Proxy(Array, {}))(2).length", "2");
    // GetPrototypeFromConstructor: the instance sits on newTarget's prototype.
    js_expect("(function(){ function N() {} N.prototype = {mark: 1}; return "
              "Reflect.construct(Error, ['m'], N).mark + ',' + Reflect.construct(Error, ['m'], "
              "N).message; })()",
              "1,m");
    REPORT("is_constructor");
}
