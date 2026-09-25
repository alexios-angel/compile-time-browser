// A THROW IS ONE THROW, AND A RECEIVER IS THE ONE THE WRITE BEGAN ON.
//
// Two families of test262 failure with one shape each. A native that asks a
// proxy, a getter or a toJSON something, and is answered by a throw that has
// already LANDED on the page's handler (context::unwinds), must stop rather
// than go on to throw a second time over the handler the first consumed - a
// revoked proxy under `delete`, Object.defineProperty or JSON.stringify, a
// Symbol handed to toPrecision. And a proxy with no `set` trap, or one on a
// prototype chain, hands the write on with the ORIGINAL receiver (10.5.9 step
// 6, 10.1.9.2), so its own traps and a setter's `this` see it.

#include "js_expect.hpp"

int main() {
    // Each of these threw twice and the second throw escaped the `try`.
    js_expect(
        "(function(){var h = Proxy.revocable({}, {}); h.revoke();"
        "try { Object.defineProperty(h.proxy, 'x', {value: 1}); } catch (e) { return e.name; }"
        "})()",
        "TypeError");
    js_expect("(function(){'use strict'; var p = new Proxy({}, {deleteProperty: {}});"
              "try { delete p.x; } catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function(){var h = Proxy.revocable([], {}); h.revoke();"
              "try { JSON.stringify([[h.proxy]]); } catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function(){try { (1).toPrecision(Symbol()); } catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function(){var f = Object.defineProperty(function () {}, 'name',"
              " {get: function () { throw new RangeError('n'); }});"
              "try { f.bind(); return 'bound'; } catch (e) { return e.name; } })()",
              "RangeError");

    // A proxy with no `set` trap: the target's [[Set]] with the PROXY as the
    // receiver, so the proxy's own getOwnPropertyDescriptor and
    // defineProperty traps take the write.
    js_expect("(function(){var seen = []; var p = new Proxy({a: 1}, {"
              " getOwnPropertyDescriptor: function (t, k) { seen.push('g' + k);"
              "   return Reflect.getOwnPropertyDescriptor(t, k); },"
              " defineProperty: function (t, k, d) { seen.push('d' + k);"
              "   return Reflect.defineProperty(t, k, d); }});"
              "p.a = 2; p.b = 3; return seen.join() + '|' + p.a + p.b; })()",
              "ga,da,gb,db|23");
    // ...and a proxy on a chain is asked with the object the access began on.
    js_expect("(function(){var who; var proto = new Proxy({}, {set: function (t, k, v, r) {"
              " who = r; return true; }}); var o = Object.create(proto); o.x = 1;"
              "return (who === o) + ',' + o.hasOwnProperty('x'); })()",
              "true,false");
    js_expect("(function(){var o = Object.create(new Proxy({get g() { return this; }}, {}));"
              "return o.g === o; })()",
              "true");

    // Function.prototype is a function of length 0 and name "", which a
    // function reads once its own are deleted.
    js_expect("(function(){var f = function g(a, b) {}; delete f.length; delete f.name;"
              "return f.length + '|' + f.name + '|' + f.hasOwnProperty('length'); })()",
              "0||false");
    js_expect("Object.getOwnPropertyNames(Function.prototype).slice(0, 2).join()", "length,name");
    // Array.prototype is an Array: its `length` is 0.
    js_expect("Array.prototype.length + '|' + ('length' in Object.create(Array.prototype))",
              "0|true");
    // [[ErrorData]] makes an Error, not the prototype an object was made on.
    js_expect("Object.prototype.toString.call(EvalError.prototype)", "[object Object]");
    // A String wrapper's own integer keys come before its `length`.
    js_expect("(function(){var s = new String('ab'); s[5] = 'x'; s.k = 1;"
              "return Object.getOwnPropertyNames(s).join(); })()",
              "0,1,5,length,k");
    REPORT("receivers_and_throws");
}
