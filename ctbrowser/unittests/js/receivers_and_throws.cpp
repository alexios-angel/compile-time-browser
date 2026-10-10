//===- ctbrowser/unittests/js/receivers_and_throws.cpp - Receivers --------===//
//
// A throw is one throw, and a receiver is the one the write began on.
//
// Two families of test262 failure with one shape each. A native that asks a
// proxy, a getter or a toJSON something, and is answered by a throw that has
// already landed on the page's handler (context::unwinds), must stop rather
// than go on to throw a second time over the handler the first consumed: a
// revoked proxy under `delete`, Object.defineProperty or JSON.stringify, a
// Symbol handed to toPrecision. And a proxy on a prototype chain is asked
// with the original receiver (ECMA-262 10.1.9.2, 10.1.8.1), so its traps and
// a setter's `this` see it.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

int main() {
    // Each of these throws once, so the `try` catches it.
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

    // for-in walks the whole chain for every kind of object (ECMA-262 14.7.5.9): a
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
    // ...but an object literal's property is defined, whatever a read-only
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
    // A function's own keys: integer keys ascending, then `length`, `name`,
    // `prototype` where they were made (a redefinition does not move one),
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
    // whatever it was handed (ECMA-262 20.1.1.1 step 1); a plain call still passes an
    // object through.
    js_expect("(function(){class O extends Object {} var o = new O({a: 1});"
              "var r = Reflect.construct(Object, [{b: 2}], O); var p = {c: 3};"
              "return o.a + ',' + r.b + ',' + (Object.getPrototypeOf(o) === O.prototype) +"
              " (Object.getPrototypeOf(r) === O.prototype) + (Object(p) === p) +"
              " (new Object(p) === p); })()",
              "undefined,undefined,truetruetruetrue");
    // A String receiver refuses a write, and an empty one has none to refuse.
    js_expect("(function(){var s = [].sort.call(''); var r = [].reverse.call('a');"
              "var t; try { [].sort.call('ba'); t = 'sorted'; } catch (e) { t = e.name; }"
              "return (s instanceof String) + ',' + (r instanceof String) + ',' + t; })()",
              "true,true,TypeError");
    // An array whose length runs past its vector is not the fast path's.
    js_expect("(function(){var x = []; x.length = 4294967295; var n = x.push(); var e;"
              "try { x.push('x'); } catch (err) { e = err.name; }"
              "return n + ',' + e + ',' + x[4294967295] + ',' + x.length; })()",
              "4294967295,RangeError,x,4294967295");
    // A `has` trap is asked with a property key, a string, for an index.
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
    // An operator's left operand stops it only on its own throw: a
    // `return()` IteratorClose calls while the TypeError that closed the
    // iterator is in flight still adds (test262
    // built-ins/Array/from/iter-set-elem-prop-err.js).
    js_expect("(function(){ var n = 0; var it = {[Symbol.iterator]() { return {"
              " next() { return {done: false, value: 1}; },"
              " return() { n += 1; return {}; } }; }};"
              " try { Array.from.call(function () { Object.defineProperty(this, '0',"
              " {writable: true, configurable: false}); }, it); } catch (e) {}"
              " return n; })()",
              "1");
    // `null[k]` throws the TypeError before k's ToPropertyKey (ECMA-262
    // GetValue/PutValue step 3.a), so a key whose toString throws is never
    // called and no second throw is left pending.
    js_expect("(function(){ var base = null, k = {toString() { throw 1; }};"
              " try { base[k] ^= 1; } catch (e) { return e instanceof TypeError; } })()",
              "true");
    js_expect("(function(){ var base, k = {toString() { throw 1; }};"
              " try { base[k] = 1; } catch (e) { return e instanceof TypeError; } })()",
              "true");
    js_expect("(function(){ var k = {toString() { throw 'key'; }};"
              " try { return ({})[k]; } catch (e) { return e; } })()",
              "key");
    // Object.fromEntries closes the iterator on an entry's throw and keeps
    // the throw.
    js_expect("(function(){ var closed = 0; var it = { [Symbol.iterator]() { return {"
              " next() { return {done: false, value: { get 0() { throw 'key'; } }}; },"
              " return() { closed++; return {}; } }; } };"
              " try { Object.fromEntries(it); } catch (e) { return e + closed; }"
              " return 'none'; })()",
              "key1");
    // A lexical for-of head's names are uninitialised while its operand runs.
    js_expect("(function(){ let x = 1; try { for (let x of [x]) {} } catch (e) {"
              " return e.constructor.name; } return 'none'; })()",
              "ReferenceError");
    js_expect("(function(){ let x = [1]; for (let y of x) { return y; } })()", "1");
    // `break`/`continue` to an outer loop closes the for-of it leaves.
    js_expect("(function(){ var closed = 0; var it = { [Symbol.iterator]() { return {"
              " next() { return {done: false, value: 1}; },"
              " return() { closed++; return {}; } }; } };"
              " L: do { for (var x of it) { continue L; } } while (false);"
              " M: for (;;) { for (var y of it) { break M; } }"
              " for (var z of it) { break; }"
              " return closed; })()",
              "3");
    // A strict write through a `with` object that lost the binding meanwhile
    // (ECMA-262 9.1.1.2.5 SetMutableBinding); sloppy code writes it back.
    js_expect("(function(){ var scope = { get x() { delete this.x; return 2; } };"
              " with (scope) { return (function(){ 'use strict'; try { x += 1; }"
              " catch (e) { return e.constructor.name; } return 'none'; })(); } })()",
              "ReferenceError");
    js_expect("(function(){ var scope = { get x() { delete this.x; return 2; } };"
              " with (scope) { x += 1; } return scope.x; })()",
              "3");
    // A spread's iterator throws once: a throw from @@iterator, next() or a
    // result's `value` getter is the only one, and the spread must not throw a
    // TypeError of its own over it. A present but non-callable @@iterator is
    // GetIterator's TypeError.
    js_expect("(function () { function Boom() {} var out = [];"
              " var bad = [function () { throw new Boom(); }, function () { return {next:"
              " function () { throw new Boom(); } }; }, function () { return {next: function ()"
              " { return Object.defineProperty({done: false}, 'value', {get: function () {"
              " throw new Boom(); }}); } }; }];"
              " bad.forEach(function (m) { var it = {}; it[Symbol.iterator] = m;"
              " try { (function () {})(...it); out.push('none'); } catch (e) {"
              " out.push(e instanceof Boom); } try { [...it]; out.push('none'); } catch (e) {"
              " out.push(e instanceof Boom); } });"
              " var nul = Object.defineProperty({}, Symbol.iterator, {get: function () {"
              " return null; }}); try { [...nul]; out.push('none'); } catch (e) {"
              " out.push(e.name); } return out.join(); })()",
              "true,true,true,true,true,true,TypeError");
    // `.return()` at a `yield*` reads the inner `return` once, and a throw
    // from calling it, or from its result's `done`, is thrown at the yield,
    // where the generator's own catch sees it (ECMA-262 14.4.14 step 7.c).
    js_expect("(function () { var gets = 0, thrown = {}; var inner = {next: function () {"
              " return {done: false}; }, get return() { gets += 1; return null; }};"
              " inner[Symbol.iterator] = function () { return inner; };"
              " function* g() { yield* inner; } var it = g(); it.next(); var r = it.return(2);"
              " var caught; var bad = {next: function () { return {done: false}; }, return:"
              " function () { throw thrown; }}; bad[Symbol.iterator] = function () {"
              " return bad; }; function* h() { try { yield* bad; } catch (e) { caught = e; } }"
              " var jt = h(); jt.next(); jt.return(); return [gets, r.value, r.done,"
              " caught === thrown].join(); })()",
              "1,2,true,true");
    // `super.m()` where the home object's [[Prototype]] is the implicit
    // Object.prototype (a class with no `extends`, a plain literal) calls
    // Object.prototype's m (GetSuperBase), and an explicit null is nothing.
    js_expect("(function(){ class A { m() { return super.toString(); } } return new A().m(); })()",
              "[object Object]");
    js_expect("({ m() { return super.hasOwnProperty.call(this, 'm'); } }).m()", "true");
    js_expect("(function(){ var o = { m() { return super.toString(); } };"
              " Object.setPrototypeOf(o, null); try { o.m(); } catch (e) { return e.name; } })()",
              "TypeError");
    // A numeric operator converts its left operand first, and a throw there
    // (a valueOf, a Symbol's TypeError) is the operator's: the right one is
    // never converted.
    js_expect("(function(){ var x = {valueOf: function () { throw 'x'; }};"
              " var y = {valueOf: function () { throw 'y'; }};"
              " try { x & y; } catch (e) { return e + (function () { try { x << y; }"
              " catch (f) { return f; } })(); } })()",
              "xx");
    js_expect(
        "(function(){ var t = ''; try { ({valueOf: function () { t += 'l'; return Symbol(); }})"
        " / ({valueOf: function () { t += 'r'; return 1; }}); } catch (e) { return e.name + t; } "
        "})()",
        "TypeErrorl");
    // A function's [[Prototype]] set to null is null, not the unset link that
    // means Function.prototype, so a static method's `super.x = v` throws.
    js_expect("(function(){ function F() {} Object.setPrototypeOf(F, null);"
              " return Object.getPrototypeOf(F) === null; })()",
              "true");
    js_expect("(function(){ class C { static m() { super.x = 1; } }"
              " Object.setPrototypeOf(C, null); try { C.m(); } catch (e) { return e.name; } })()",
              "TypeError");
    REPORT("receivers_and_throws");
}
