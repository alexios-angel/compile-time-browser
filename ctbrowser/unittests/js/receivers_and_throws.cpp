// A THROW IS ONE THROW, AND A RECEIVER IS THE ONE THE WRITE BEGAN ON.
//
// Two families of test262 failure with one shape each. A native that asks a
// proxy, a getter or a toJSON something, and is answered by a throw that has
// already LANDED on the page's handler (context::unwinds), must stop rather
// than go on to throw a second time over the handler the first consumed - a
// revoked proxy under `delete`, Object.defineProperty or JSON.stringify, a
// Symbol handed to toPrecision. And a proxy on a prototype chain is asked
// with the ORIGINAL receiver (10.1.9.2, 10.1.8.1), so its traps and a
// setter's `this` see it.

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

    // A proxy on a chain is asked with the object the access began on.
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

    // for-in walks the WHOLE chain for every kind of object (14.7.5.9): a
    // String wrapper's indices, and an enumerable key a page put on a
    // built-in prototype, once, unless something nearer shadows it.
    js_expect("(function(){var out = []; for (var k in new String('ab')) out.push(k);"
              "return out.join(); })()",
              "0,1");
    js_expect("(function(){Object.defineProperty(Function.prototype, 'fp',"
              " {value: 1, enumerable: true, configurable: true});"
              "Object.defineProperty(Array.prototype, 'ap',"
              " {value: 2, enumerable: true, configurable: true});"
              "var out = []; for (var k in (function () {}).bind(null)) out.push(k);"
              "for (var k in [7]) out.push(k); var o = {ap: 0}; for (var k in o) out.push(k);"
              "delete Function.prototype.fp; delete Array.prototype.ap; return out.join(); })()",
              "fp,0,ap,ap");
    // [[Set]] of a fresh name asks the chain: a setter on Array.prototype
    // takes an array's write.
    js_expect("(function(){var got; Object.defineProperty(Array.prototype, 'sp',"
              " {set: function (v) { got = v; }, configurable: true});"
              "var a = []; a.sp = 5; var own = a.hasOwnProperty('sp');"
              "delete Array.prototype.sp; return got + ',' + own; })()",
              "5,false");
    // ...but an object literal's property is DEFINED, whatever a read-only
    // Object.prototype property of the same name says.
    js_expect("(function(){Object.defineProperty(Object.prototype, 'ro',"
              " {value: 1, writable: false, configurable: true});"
              "var o = {ro: 12}; var r = o.ro + ',' + o.hasOwnProperty('ro');"
              "delete Object.prototype.ro; return r; })()",
              "12,true");
    REPORT("receivers_and_throws");
}
