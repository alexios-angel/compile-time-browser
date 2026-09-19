#include "constructors.hpp"

#include "../vm_expect.hpp"

// OrdinaryCallBindThis through call/apply/bind: a sloppy function's `this`
// is the global object for null/undefined and a wrapper for a primitive; a
// strict one takes the value as given. And `new bound()` constructs the
// target with the bound arguments in front (10.4.1.2).
void test_call_bind_this() {
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
    expect_result("class A {} class B extends A { constructor() { return {y: 2}; } }"
                  "return new B().y;",
                  "2");
    expect_result("class A {} class B extends A { constructor() { const f = () => super(); f();"
                  " this.x = 3; } } return new B().x;",
                  "3");
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
