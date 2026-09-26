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
    // THE `new` OPERATOR ITSELF, not only Reflect.construct: the interpreter's
    // op::construct ran an arrow, a generator and an async function as
    // constructors while the other two tiers refused them.
    js_expect("(function () { var out = []; [() => 1, function* () {}, async function () {},"
              " async function* () {}].forEach(function (f) { try { new f(); out.push('no'); }"
              " catch (e) { out.push(e.constructor === TypeError); } }); return out.join(); })()",
              "true,true,true,true");
    // A PROXY OVER A FUNCTION IS CALLED through its `apply` trap, or its
    // target when there is none - directly, through .call, and through a
    // proxy over a proxy over a bound function (10.5.12).
    js_expect("(function () { var seen; var p = new Proxy(function (a) { return a; }, {"
              " apply: function (t, self, args) { seen = this; return t(args[0]) + 1; } });"
              " var sum = function (a, b) { return this.k + a"
              " + b; }.bind({k: 10}, 1); var q = new Proxy(new Proxy(sum, {}), {apply: null});"
              " return [p(1), p.call(null, 2), typeof seen, q(2)].join(); })()",
              "2,3,object,13");
    REPORT("is_constructor");
}
