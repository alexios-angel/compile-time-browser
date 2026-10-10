#include "constructors.hpp"

#include "../expect.hpp"

// OrdinaryCallBindThis through call/apply/bind: a sloppy function's `this`
// is the global object for null/undefined and a wrapper for a primitive; a
// strict one takes the value as given. And `new bound()` constructs the
// target with the bound arguments in front (10.4.1.2).
void test_call_bind_this() {
    for (const char * expression :
         {"new globalThis(1)", "new globalThis(...[1])", "new ({})(...[1])", "new (0)(...[1])"}) {
        expect_result(std::string{"try { "} + expression +
                          "; return 'constructed'; } catch (e) { return e.name; }",
                      "TypeError");
    }
    // A construct trap cannot give a non-constructor [[Construct]]. Arguments
    // are still evaluated before the TypeError, without even reading the trap.
    for (const char * target : {"{}", "() => {}", "Math.abs", "function* () {}"}) {
        expect_result(std::string{"let log = ''; const p = new Proxy("} + target +
                          ", {get construct() { log += 'lookup'; return function() {"
                          " log += 'trap'; return {}; }; }});"
                          "try { new p((log += 'arg', 1)); return 'constructed'; }"
                          "catch (e) { return e.name + ',' + log; }",
                      "TypeError,arg");
    }
    // A suspended constructor expression or argument must not shift the
    // contiguous argument registers consumed by Construct.
    expect_result(R"(
        function C(a, b) { this.a = a; this.b = b; this.target = new.target; }
        function* make() { return new (yield 'ctor')(7, 2); }
        const it = make(), first = it.next().value;
        const o = it.next(C).value;
        return [first, o.a, o.b, o.target === C].join();
    )",
                  "ctor,7,2,true");
    expect_result(R"(
        function C(a, b) { this.a = a; this.b = b; this.target = new.target; }
        function* make() { return new C(yield 'a', yield 'b'); }
        const it = make(), first = it.next().value, second = it.next(7).value;
        const o = it.next(2).value;
        return [first, second, o.a, o.b, o.target === C].join();
    )",
                  "a,b,7,2,true");
    // Every Call path rejects a class before its body starts, including
    // wrappers and callbacks that enter through context::invoke.
    for (const char * target : {"C", "C.bind(null)", "C.bind(null).bind(null)", "new Proxy(C, {})",
                                "new Proxy(C.bind(null), {})", "new Proxy(C, {}).bind(null)"}) {
        expect_result(std::string{"let ran = 0; class C { constructor() { ran++; } } const f = "} +
                          target +
                          "; const out = []; for (const call of [() => f(),"
                          " () => f(...[]), () => f.call(null), () => f.apply(null, []),"
                          " () => Reflect.apply(f, null, []), () => [0].map(f)]) {"
                          " try { call(); out.push('no throw'); }"
                          " catch (e) { out.push(e.name); } } return out.join() + ',' + ran;",
                      "TypeError,TypeError,TypeError,TypeError,TypeError,TypeError,0");
    }
    // IsCallable(class) is true: CreateListFromArrayLike happens before Call.
    expect_result("class C {} const log = []; const args = {get length() {"
                  " log.push('length'); return 1; }, get 0() { log.push('arg'); return 0; }};"
                  "for (const apply of [() => C.apply(null, args),"
                  " () => Reflect.apply(C, null, args)]) {"
                  " try { apply(); } catch (e) { log.push(e.name); } } return log.join();",
                  "length,arg,TypeError,length,arg,TypeError");
    expect_result("class C {} const args = { get length() { throw 'argument'; } };"
                  "const out = []; for (const apply of [() => C.apply(null, args),"
                  " () => Reflect.apply(C, null, args)]) {"
                  " try { apply(); } catch (e) { out.push(e); } } return out.join();",
                  "argument,argument");
    // A proxy apply trap can implement Call without invoking its class target.
    expect_result("class C {} const f = new Proxy(C, { apply(t, self, args) { return args[0]; }});"
                  "return f(7) + ',' + f.bind(null, 8)();",
                  "7,8");
    expect_result("class A { constructor(v) { this.v = v; } }"
                  "class B extends A { constructor(v) { (() => super(...[v]))(); } }"
                  "const C = B.bind(null, 9); return new C().v + ',' + new B(8).v;",
                  "9,8");
    // 10.4.1.3: a bound function's [[Prototype]] is the target's, and its
    // length follows 20.2.3.2 step 7 - an infinite target length stays
    // infinite, a fractional one truncates, less the bound arguments.
    expect_result("function f() {} class A {} class B extends A {}"
                  " return [Object.getPrototypeOf(f.bind()) === Function.prototype,"
                  " Object.getPrototypeOf(B.bind()) === A].join();",
                  "true,true");
    expect_result("const l = v => Object.defineProperty(function (a, b) {}, 'length', {value: v})"
                  ".bind(null, 1).length; return [l(Infinity), l(-Infinity), l(2.5), l(0)].join();",
                  "Infinity,0,1,0");
    expect_result("function f() { return this === globalThis; } return f.call() + ',' + "
                  "f.apply(null) + ',' + f.bind(undefined)();",
                  "true,true,true");
    expect_result("function f() { return typeof this; } return f.call(1) + ',' + f.call('s');",
                  "object,object");
    expect_result("'use strict'; function f() { return this; } return f.call(1) + ',' + "
                  "f.call(undefined);",
                  "1,undefined");
    expect_result("function P(a, b) { this.sum = a + b; } const B = P.bind({}, 1);"
                  "const p = new B(2); return p.sum + ',' + (p instanceof P);",
                  "3,true");
    expect_result("const B = Math.abs.bind(null); try { new B(); } catch (e) { return "
                  "e.constructor.name; }",
                  "TypeError");
    expect_result("return Object.getOwnPropertyNames((function () {}).bind()).join();",
                  "length,name");
}

// A DERIVED CLASS'S CONSTRUCTOR binds `this` only when `super()` returns
// (10.2.1.3): `this` before it, a return without it, and a second `super()`
// are ReferenceErrors; a returned primitive is the TypeError; a returned
// object replaces the instance. An arrow inside the constructor shares the
// binding. Tracked by a hidden local the compiler keeps - see
// frame::derived_flag - so a plain function or a base class pays nothing.
void test_derived_constructors() {
    expect_result("class A {} class B extends A { constructor() { super(); this.x = 1; } }"
                  "return new B().x;",
                  "1");
    expect_result("class A {} class B extends A { constructor() { this.x = 1; super(); } }"
                  "try { new B(); } catch (e) { return e.name; } return 'no throw';",
                  "ReferenceError");
    expect_result("class A {} class B extends A { constructor() {} }"
                  "try { new B(); } catch (e) { return e.name; } return 'no throw';",
                  "ReferenceError");
    expect_result("class A {} class B extends A { constructor() { super(); super(); } }"
                  "try { new B(); } catch (e) { return e.name; } return 'no throw';",
                  "ReferenceError");
    expect_result("class A {} class B extends A { constructor() { return 1; } }"
                  "try { new B(); } catch (e) { return e.name; } return 'no throw';",
                  "TypeError");
    expect_result("class B extends Object { constructor() { return null; } }"
                  "const results = []; for (const make of [() => new B(), () => new B(...[]),"
                  " () => Reflect.construct(B, []), () => new (B.bind(null))()]) {"
                  " try { make(); results.push('no throw'); } catch (e) { results.push(e.name); }"
                  " } return results.join();",
                  "TypeError,TypeError,TypeError,TypeError");
    expect_result("let ran = 0; class B extends Object { constructor() { super();"
                  " try { return null; } finally { ran++; } } }"
                  "try { new B(); } catch (e) { return e.name + ',' + ran; } return 'no throw';",
                  "TypeError,1");
    expect_result("class B extends Object { constructor() { try { return null; }"
                  " finally { return {x: 7}; } } } return new B().x;",
                  "7");
    // [[Construct]] validates the completed body, beyond its catch clauses.
    expect_result("const results = []; for (const value of [null, 1, undefined]) {"
                  " class B extends Object { constructor() { try { return value; }"
                  " catch (e) { results.push('caught'); return {}; } } }"
                  " try { new B(); results.push('no throw'); } catch (e) { results.push(e.name); }"
                  " } return results.join();",
                  "TypeError,TypeError,ReferenceError");
    expect_result("const log = []; function* items() { try { yield 0; }"
                  " finally { log.push('close'); } }"
                  "class B extends Object { constructor() { try { for (const x of items()) {"
                  " try { return null; } finally { log.push('finally'); } } }"
                  " catch (e) { log.push('caught'); return {}; } } }"
                  "try { new B(); } catch (e) { log.push(e.name); } return log.join();",
                  "finally,close,TypeError");
    // Errors evaluating the return expression or closing its iterator still
    // belong to the body and can be handled there.
    expect_result("function fail() { throw 'expression'; }"
                  "function* items() { try { yield 0; } finally { throw 'close'; } }"
                  "class A extends Object { constructor() { try { return fail(); }"
                  " catch (e) { return {kind: e}; } } }"
                  "class B extends Object { constructor() { try { for (const x of items()) {"
                  " return null; } } catch (e) { return {kind: e}; } } }"
                  "return new A().kind + ',' + new B().kind;",
                  "expression,close");
    expect_result("class A { constructor() { this.x = 3; return null; } }"
                  "class B extends A { constructor() { super(); return undefined; } }"
                  "return new A().x + ',' + new B().x;",
                  "3,3");
    expect_result("class A {} class B extends A { constructor() { return {y: 2}; } }"
                  "return new B().y;",
                  "2");
    expect_result("class A {} class B extends A { constructor() { const f = () => super(); f();"
                  " this.x = 3; } } return new B().x;",
                  "3");
    // Arrows share the binding, including captures made before super() and
    // calls made after the constructor has returned a different object.
    for (const char * call : {"super()", "(() => super())()", "(() => () => super(...[]))()()"}) {
        expect_result(std::string{"const out = []; class A { constructor(v) { return {v: 7}; } }"
                                  "class B extends A { field = 9; constructor() {"
                                  " const read = () => this; const result = "} +
                          call +
                          "; out.push(result === this, read() === this, this.v, this.field);"
                          " this.read = read; } } const b = new B();"
                          "out.push(b.read() === b); return out.join();",
                      "true,true,7,9,true");
    }
    expect_result("const pending = []; class A { constructor(v) { return {v}; } }"
                  "class B extends A { constructor() { const read = () => this;"
                  " pending.push({read, finish: v => { const result = super(v);"
                  " return result === this && read() === this; }}); return {}; } }"
                  "new B(); new B(); const out = [];"
                  "try { pending[0].read(); } catch (e) { out.push(e.name); }"
                  "out.push(pending[0].finish(1), pending[1].finish(2),"
                  " pending[0].read().v, pending[1].read().v);"
                  "try { pending[0].finish(3); } catch (e) { out.push(e.name); }"
                  "return out.join();",
                  "ReferenceError,true,true,1,2,ReferenceError");
    expect_result("class B extends Array { constructor() { const read = () => this;"
                  " (() => super(...[1, 2]))(); this.same = read() === this; } }"
                  "const b = new B(); return [Array.isArray(b), b.length, b.same].join();",
                  "true,2,true");
    expect_after_turn("var result = ''; let finish, read;"
                      "class A { constructor() { return {v: 7}; } }"
                      "class B extends A { constructor() { read = () => this;"
                      " finish = async () => { await 0; (() => super())(); await 0;"
                      " result = (read() === this) + ',' + this.v; }; return {}; } }"
                      "new B(); finish();",
                      "true,7");
    // Bare super() forwards the enclosing constructor's new.target even
    // through escaped/nested arrows and bound or proxy superclass calls.
    expect_result("let finish; class A { constructor() { return {target: new.target}; } }"
                  "class B extends A { constructor() { finish = () => () => super();"
                  " return {}; } } new B(); return finish()().target === B;",
                  "true");
    expect_result("class A { constructor() { return {target: new.target}; } }"
                  "const P = A.bind(null); P.prototype = Object.create(A.prototype);"
                  "P.prototype.constructor = P;"
                  "class B extends P { constructor() { return (() => super(...[]))(); } }"
                  "return new B().target === B;",
                  "true");
    expect_result("class A { constructor() { return {target: new.target}; } }"
                  "const P = new Proxy(A, {}); A.prototype.constructor = P;"
                  "class B extends P { constructor() { return (() => super())(); } }"
                  "return new B().target === B;",
                  "true");
    // A native superclass must not lend its new.target to later Call paths.
    expect_result("class A {} class B extends Array { constructor() { (() => super())(); A(); } }"
                  "try { new B(); } catch (e) { return e.name; } return 'no throw';",
                  "TypeError");
    expect_result("let ran = 0; class A { constructor() { ran++; throw 'body'; } }"
                  "class B extends Map { constructor() { super({[Symbol.iterator]: A}); } }"
                  "try { new B(); } catch (e) { return e.name + ',' + ran; } return 'no throw';",
                  "TypeError,0");
    expect_result(
        "class A {} class B extends A { constructor() { const g = () => this; try { g(); }"
        " catch (e) { super(); return; } } } new B(); return 'caught';",
        "caught");
    // `super.x = v` lands on `this`, and a frozen prototype refuses it.
    expect_result("class C { m() { super.x = 8; return this.x; } } return new C().m();", "8");
    expect_result("class C { m() { super.x = 8; return C.prototype.hasOwnProperty('x'); } }"
                  "return new C().m();",
                  "false");
    // `super[k]` reads through the parent prototype.
    expect_result("class A { get k() { return 'a'; } } class B extends A { get k() { return 'b'; }"
                  " m() { return super['k'] + this.k; } } return new B().m();",
                  "ab");
}

// A BUILT-IN THAT MAKES ITS OWN OBJECT CAN BE EXTENDED. `class A extends
// Array` reaches Array through super() with the instance [[Construct]] made;
// Array answers an array of its own, which takes the instance's prototype
// (array_object::prototype) and becomes `this`. Until 2026-09-17 the native
// parent's answer was dropped and `new A()` was a plain object that was never
// an array: `length` undefined, `Array.isArray` false, every method a
// TypeError. The typed arrays are the same shape, and Object's `super()`
// keeps the instance.
void test_builtin_subclasses() {
    expect_result("class A extends Array {} const a = new A(3);"
                  "return a.length + ',' + Array.isArray(a) + ',' + (a instanceof A) + ',' +"
                  "(a instanceof Array) + ',' + (Object.getPrototypeOf(a) === A.prototype);",
                  "3,true,true,true,true");
    expect_result("class A extends Array { sum() { return this.reduce((s, x) => s + x, 0); } }"
                  "const a = A.from([1, 2, 3]); const m = a.map(x => x * 2);"
                  "return a.sum() + ',' + (m instanceof A) + ',' + m.sum();",
                  "6,true,12");
    expect_result("class A extends Array { constructor(...xs) { super(...xs); this.tag = 't'; } }"
                  "const a = new A(1, 2); return a.tag + a.length + a[1];",
                  "t22");
    expect_result("class U extends Uint8Array {} const u = new U(2); u[0] = 300;"
                  "return u.length + ',' + u[0] + ',' + (u instanceof U) + ',' +"
                  "(u instanceof Uint8Array) + ',' + u.byteLength + ',' + u.subarray(1).length;",
                  "2,44,true,true,2,1");
    expect_result(
        "class U extends Uint8Array {} const rab = new ArrayBuffer(4, {maxByteLength: 8});"
        "const u = new U(rab, 0, 2); return u.length + ',' + (u.buffer === rab) + ',' +"
        "Array.prototype.at.call(u, 0);",
        "2,true,0");
    expect_result("class O extends Object { constructor() { super(); this.x = 1; } }"
                  "return new O().x + ',' + (new O() instanceof O);",
                  "1,true");
    expect_result("class F extends Function {} const f = new F('return 7');"
                  "return f() + ',' + (f instanceof F) + ',' + (f instanceof Function);",
                  "7,true,true");
    // Object.setPrototypeOf on an array, and `in` through the chain.
    expect_result(
        "const a = [1]; Object.setPrototypeOf(a, { extra: 7 });"
        "return a.extra + ',' + ('extra' in a) + ',' + a.length + ',' + (a instanceof Array)"
        " + ',' + typeof a.push;",
        "7,true,1,false,undefined");
    // A typed array has no own `length`: a page may define one.
    expect_result("const t = new Uint8Array(2); Object.defineProperty(t, 'length', {value: 9});"
                  "return t.length + ',' + Object.getOwnPropertyNames(new Uint8Array(1)).join();",
                  "9,0");
}
