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
    // Spreading a proxy asks its traps: ownKeys, then each key's descriptor,
    // then a [[Get]] of the enumerable ones.
    js_expect("(function(){var log = []; var p = new Proxy({a: 1, b: 2}, {"
              " ownKeys: function (t) { log.push('keys'); return ['b', 'a']; },"
              " getOwnPropertyDescriptor: function (t, k) { log.push('d' + k);"
              "   return Reflect.getOwnPropertyDescriptor(t, k); },"
              " get: function (t, k) { log.push('g' + k); return t[k]; }});"
              "var o = {...p}; return log.join() + '|' + Object.keys(o).join() + o.a + o.b; })()",
              "keys,db,gb,da,ga|b,a12");
    // A private accessor with no getter is a TypeError to read (PrivateGet).
    js_expect("(function(){class C { set #f(v) {} read() { return this.#f; } }"
              "try { new C().read(); return 'read'; } catch (e) { return e.name; } })()",
              "TypeError");
    // A FUNCTION'S OWN KEYS: integer keys ascending, then `length`, `name`,
    // `prototype` where they were made - a redefinition does not move one -
    // then strings, then symbols; the compiler's `__home` is not one of them.
    js_expect("(function(){var f = () => {}; f.a = 1;"
              "Object.defineProperty(f, 'length', {enumerable: true});"
              "return Object.keys(f).join(); })()",
              "length,a");
    js_expect("(function(){var s = Symbol(); class A { static b() { return super.x; }"
              " static 2() {} static 1() {} static length() {} static [s]() {} }"
              "var k = []; for (var x in A) k.push(x);"
              "return Object.getOwnPropertyNames(A).join() + '|' +"
              " Object.getOwnPropertySymbols(A).length + '|' + k.join(); })()",
              "1,2,length,name,prototype,b|1|");
    js_expect("(function(){function f(a, b) {} Object.defineProperty(f, 'length', {value: 5});"
              "delete f.length; return f.hasOwnProperty('length') + ',' + f.length; })()",
              "false,0");
    js_expect("(function(){var d = Object.getOwnPropertyDescriptor(Function, 'prototype');"
              "return '' + d.writable + d.enumerable + d.configurable; })()",
              "falsefalsefalse");
    // `Object` under a subclass's NewTarget makes the subclass's instance,
    // whatever it was handed (20.1.1.1 step 1); a plain call still passes an
    // object through.
    js_expect("(function(){class O extends Object {} var o = new O({a: 1});"
              "var r = Reflect.construct(Object, [{b: 2}], O); var p = {c: 3};"
              "return o.a + ',' + r.b + ',' + (Object.getPrototypeOf(o) === O.prototype) +"
              " (Object.getPrototypeOf(r) === O.prototype) + (Object(p) === p) +"
              " (new Object(p) === p); })()",
              "undefined,undefined,truetruetruetrue");
    // A String receiver refuses a WRITE, and an empty one has none to refuse.
    js_expect("(function(){var s = [].sort.call(''); var r = [].reverse.call('a');"
              "var t; try { [].sort.call('ba'); t = 'sorted'; } catch (e) { t = e.name; }"
              "return (s instanceof String) + ',' + (r instanceof String) + ',' + t; })()",
              "true,true,TypeError");
    // An array whose length runs past its vector is not the fast path's.
    js_expect("(function(){var x = []; x.length = 4294967295; var n = x.push(); var e;"
              "try { x.push('x'); } catch (err) { e = err.name; }"
              "return n + ',' + e + ',' + x[4294967295] + ',' + x.length; })()",
              "4294967295,RangeError,x,4294967295");
    // A `has` trap is asked with a property KEY, a string, for an index.
    js_expect("(function(){var seen = []; var p = new Proxy([[1], 2], {has: function (t, k) {"
              " seen.push(typeof k + k); return k in t; }});"
              "Array.prototype.flat.call(p); return seen.join(); })()",
              "string0,string1");
    // defineProperties reads each descriptor through the proxy's [[Get]].
    js_expect("(function(){var log = []; var logger = new Proxy({}, {"
              " get: function (t, k) { log.push(k); }});"
              "Object.defineProperties({}, new Proxy({a: {value: 0}, b: {value: 1}}, logger));"
              "return log.join(); })()",
              "ownKeys,getOwnPropertyDescriptor,get,getOwnPropertyDescriptor,get");
    // An operator's left operand stops it only on its OWN throw: a
    // `return()` IteratorClose calls while the TypeError that closed the
    // iterator is in flight still adds (Array/from/iter-set-elem-prop-err.js).
    js_expect("(function(){ var n = 0; var it = {[Symbol.iterator]() { return {"
              " next() { return {done: false, value: 1}; },"
              " return() { n += 1; return {}; } }; }};"
              " try { Array.from.call(function () { Object.defineProperty(this, '0',"
              " {writable: true, configurable: false}); }, it); } catch (e) {}"
              " return n; })()",
              "1");
    REPORT("receivers_and_throws");
}
