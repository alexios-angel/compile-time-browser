// Objects and classes: fields, accessors, descriptors, prototypes,
// `new.target`, and the class-expression scoping that broke p5.js. Carved out
// of js/vm_basics.cpp on 2026-09-08 - vm_operators.cpp names the family, and
// vm_expect.hpp is the assertion every file in it shares.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "vm_expect.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

using namespace ctbrowser::script;

namespace {

// AN INSTANCE FIELD BELONGS TO THE INSTANCE.
//
// Field initialisers used to be evaluated ONCE, at class-definition time, and
// stored on the prototype - so every instance of `class A { items = [] }`
// shared one array. Push to one and it appeared in all of them, with nothing
// wrong at any stage. It is the nastiest shape in this batch, because the
// symptom shows up arbitrarily far from the cause.
void test_class_fields_are_per_instance() {
    expect_result("class A { n = 1; } const x = new A(); const y = new A(); x.n = 99; return y.n;",
                  "1");
    // the case that actually bites: a mutable field
    expect_result("class A { items = []; } const x = new A(); const y = new A(); "
                  "x.items.push(1); return y.items.length;",
                  "0");
    // a field with no initialiser is still a field
    expect_result("class A { x; } const a = new A(); return typeof a.x;", "undefined");
    // an initialiser is an expression, evaluated per instance
    expect_result("let made = 0; class A { id = ++made; } new A(); new A(); return new A().id;",
                  "3");
    // it may capture an enclosing local
    expect_result(
        "function build(v) { class A { val = v; } return new A().val; } return build(42);", "42");
    // fields arrive before the constructor body, which may then use them
    expect_result("class A { n = 2; constructor() { this.n = this.n * 5; } } return new A().n;",
                  "10");
    // INHERITED fields are initialised too, base first
    expect_result("class B { b = 'base'; } class D extends B { d = 'derived'; } "
                  "const o = new D(); return o.b + '/' + o.d;",
                  "base/derived");
    // and a STATIC field is the opposite case - one value on the class, which
    // is what it should always have been
    expect_result("class A { static count = 0; } A.count = 5; return A.count;", "5");
    // methods still live on the prototype and are shared
    expect_result("class A { n = 1; get2() { return 2; } } "
                  "return new A().get2() + new A().get2();",
                  "4");
}

// A PRIVATE NAME IS A DISTINCT NAME. The `#` used to be skipped as an unknown
// byte, so `this.#count` became `this.count`: a private field silently aliased
// a public one, and every later stage agreed with the wrong reading. p5.js
// declares 174 of them. Real brand-check privacy is not modelled - what is
// fixed is that the two names are no longer the same name.
void test_private_names_are_distinct() {
    expect_result("class C { #n = 1; n = 2; read() { return this.#n + ',' + this.n; } } "
                  "return new C().read();",
                  "1,2");
    expect_result("class C { #v = 7; get() { return this.#v; } } return new C().get();", "7");
    expect_result(
        "class C { static #hidden = 3; static read() { return C.#hidden; } } return C.read();",
        "3");
    // a private method
    expect_result("class C { #twice(x) { return x * 2; } run() { return this.#twice(4); } } "
                  "return new C().run();",
                  "8");
    // and the public field of the same name is untouched from outside
    expect_result("class C { #n = 1; n = 2; } const c = new C(); c.n = 9; return c.n;", "9");
}

// ACCESSORS. A property that runs code when it is read, which is a different
// thing from a property that HOLDS a function - installing a getter as a data
// property made `obj.v` be the function rather than call it, and a setter of
// the same name overwrote the getter outright.
//
// They live in a table BESIDE the data properties rather than widening every
// property into a descriptor, so the dozen places that iterate props - the DOM
// bindings among them - were untouched, and an object with no accessors pays
// one bool.
void test_accessors() {
    expect_result("const o = { get v() { return 42; } }; return o.v;", "42");
    expect_result("const o = { n: 0, set v(x) { this.n = x * 2; } }; o.v = 21; return o.n;", "42");
    expect_result("const o = { n: 1, get v() { return this.n; }, set v(x) { this.n = x; } }; "
                  "o.v = 9; return o.v;",
                  "9");
    // a getter is CALLED, not returned
    expect_result("const o = { get v() { return 1; } }; return typeof o.v;", "number");
    // on a class, and on its prototype so every instance sees it
    expect_result("class C { constructor() { this.n = 3; } get double() { return this.n * 2; } } "
                  "return new C().double;",
                  "6");
    expect_result("class C { constructor() { this.n = 0; } set v(x) { this.n = x + 1; } } "
                  "const c = new C(); c.v = 4; return c.n;",
                  "5");
    // a static accessor lives on the constructor
    expect_result("class C { static get name2() { return 'C'; } } return C.name2;", "C");
    // an accessor INHERITED through extends still reads the instance
    expect_result("class B { get kind() { return 'b:' + this.n; } } "
                  "class D extends B { constructor() { super(); this.n = 7; } } "
                  "return new D().kind;",
                  "b:7");
    // a write with no setter is discarded rather than shadowing the getter
    expect_result("const o = { get v() { return 1; } }; o.v = 99; return o.v;", "1");
    // an own DATA property wins over an inherited accessor
    expect_result("class B { get v() { return 'proto'; } } class D extends B {} "
                  "const d = new D(); return d.v;",
                  "proto");
    // and Object.keys sees an accessor, because it is a property
    expect_result("const o = { a: 1, get b() { return 2; } }; return Object.keys(o).join(',');",
                  "a,b");
}

void test_object_descriptors() {
    expect_result("const o = {}; Object.defineProperty(o, 'x', { value: 5 }); return o.x;", "5");
    expect_result(
        "const o = { n: 2 }; Object.defineProperty(o, 'x', { get() { return this.n * 3; } }); "
        "return o.x;",
        "6");
    expect_result("const o = {}; Object.defineProperty(o, 'x', { get() { return 1; } }); "
                  "const d = Object.getOwnPropertyDescriptor(o, 'x'); return typeof d.get;",
                  "function");
    // create + getPrototypeOf round-trip
    expect_result("const base = { greet() { return 'hi'; } }; const o = Object.create(base); "
                  "return o.greet();",
                  "hi");
    expect_result("const base = {}; const o = Object.create(base); "
                  "return Object.getPrototypeOf(o) === base;",
                  "true");
    expect_result("const o = {}; Object.setPrototypeOf(o, { v: 8 }); return o.v;", "8");
    expect_result("const o = { a: 1, get b() { return 2; } }; "
                  "return Object.getOwnPropertyNames(o).join(',');",
                  "a,b");
    expect_result("return Object.fromEntries([['a', 1], ['b', 2]]).b;", "2");
}

void test_symbol() {
    expect_result("return typeof Symbol('x');", "symbol");
    expect_result("return typeof Symbol.iterator;", "symbol");
    expect_result("return Symbol('tag').description;", "tag");
    expect_result("return Symbol('a') === Symbol('a');", "false");
    expect_result("return Symbol.iterator === Symbol.iterator;", "true");
    // usable as a property key, which is the whole point
    expect_result("const s = Symbol('k'); const o = {}; o[s] = 5; return o[s];", "5");
    expect_result("const s = Symbol('k'); const o = {}; o[s] = 5; return o.k;", "undefined");
    expect_result("const o = {}; o[Symbol.iterator] = 1; return o[Symbol.iterator];", "1");
    expect_result("return Symbol('x').toString();", "Symbol(x)");
}

// `new.target` - the meta-property, and a PARSE ERROR here until 2026-08-02.
//
// Every transpiler emits it: Babel's `_classCallCheck` is built on the
// undefined test below, and Babylon.js uses it in its decorator metadata, which
// is the first thing in that 11.6 MB bundle this engine's parser stopped on.
void test_new_target() {
    // Called as a constructor: the function itself.
    expect_result("function F() { return typeof new.target; } return new F() && 'made';", "made");
    expect_result("function F() { this.t = new.target === F; } return String(new F().t);", "true");
    // Called ordinarily: undefined. THIS is the test real code performs - a
    // guard asks whether it is undefined, not which constructor it is.
    expect_result("function F() { return new.target === undefined; } return String(F());", "true");
    expect_result("function F() { return typeof new.target; } return F();", "undefined");
    // The transpiler guard itself, which is the whole reason this exists.
    expect_result(
        "function F() { if (new.target === undefined) { return 'needs new'; } return 'ok'; }"
        "return F() + ',' + (new F() === undefined ? '?' : 'constructed');",
        "needs new,constructed");
    // At the top level there is no constructor at all.
    expect_result("return String(new.target);", "undefined");
    // And it is not a general property of `new`: `new.other` is an error, not
    // a silent undefined, or a typo becomes a value.
    bool ok = true;
    (void)run_vm("return new.other;", &ok);
    if (ok) {
        std::printf("FAIL `new.other` was accepted\n");
        ++ctbrowser_test_failures;
    }
}

// A DERIVED CLASS WITH NO CONSTRUCTOR still runs its parent's. An empty one was
// synthesised instead, so the parent never ran and the instance had none of its
// state - and the failure surfaced at the first method that needed it.
void test_implicit_super() {
    expect_result(
        "class B { constructor(v) { this.v = v; } } class D extends B {} return new D(7).v;", "7");
    expect_result(
        "class B { constructor() { this.n = 1; } } class M extends B {} class D extends M {} "
        "return new D().n;",
        "1");
    expect_result("class B { constructor(a, b) { this.s = a + b; } } class D extends B {} "
                  "return new D(2, 3).s;",
                  "5");
}

// A CLASS EXPRESSION is as ordinary as a function expression. `class` had no
// case in primary(), so `const X = class {...}` read a global named `class` and
// the body's members leaked out as top-level statements - silently.
void test_class_expressions() {
    expect_result("const X = class { constructor() { this.v = 1; } }; return new X().v;", "1");
    expect_result("const X = class Named { m() { return 'ok'; } }; return new X().m();", "ok");
    expect_result("const make = () => class { get v() { return 5; } }; return new (make())().v;",
                  "5");
}

// A NAMED CLASS EXPRESSION BINDS ITS OWN NAME, and its methods see it.
// `let X = class Inner { static m() { return Inner; } }` - `Inner` inside those
// methods is the class, not any outer binding. It is how p5.js declares itself
// (`let p5$2 = class p5 {...}`), and without it every static method that named
// the class read an undefined global.
void test_named_class_expression_binds_itself() {
    expect_result("let X = class Inner { static who() { return typeof Inner; } }; return X.who();",
                  "function");
    expect_result("let X = class Inner { static tag = 'i'; static get() { return Inner.tag; } }; "
                  "return X.get();",
                  "i");
    expect_result("let X = class Inner { constructor() { this.self = Inner; } }; "
                  "return new X().self === X;",
                  "true");
    // AND NOWHERE ELSE. This used to read `inner|function`, recorded as a known
    // approximation with a TODO and the note that "nothing in p5.js depends on
    // the difference, and a leaked binding is visible rather than wrong".
    //
    // Both halves of that were wrong. p5's bundle is `let p5$2 = class p5 {...}`,
    // so the module scope acquired a `p5` holding undefined; every function
    // compiled after that point captured it instead of the global, and `new
    // p5.TableRow()` three thousand lines later read undefined.TableRow. The
    // reported error named `TableRow`. It cost an afternoon to find, which is the
    // argument against leaving a known approximation in a compiler: the
    // difference between visible and wrong is whoever happens to look.
    //
    // The name now lives in a scope of its own, opened before the methods are
    // compiled - so they capture it - and closed after, so nothing else sees it.
    expect_result("let Outer = class Inner { static who() { return 'inner'; } }; "
                  "return Outer.who() + '|' + typeof Inner;",
                  "inner|undefined");
    // The enclosing binding it used to overwrite is left alone, which is the
    // failure that actually bit.
    expect_result("var Shared = { tag: 'outer' }; "
                  "var alias = class Shared { static who() { return 'inner'; } }; "
                  "return Shared.tag + '|' + alias.who();",
                  "outer|inner");
}

// A CLASS DECLARATION IS A HOISTED BINDING, and the reason is register
// allocation rather than semantics: a local first declared while an EXPRESSION
// is being compiled sits above the statement's register mark, so the statement
// releases it and the next statement's temporaries reuse the slot. `class S {}`
// followed by two `new S()` therefore worked once and found an object the
// second time.
void test_class_declaration_survives_the_statement() {
    expect_result("class S { constructor(x) { this.id = x; } } "
                  "const a = new S('a'); const b = new S('b'); return a.id + b.id;",
                  "ab");
    expect_result("function f() { class S { constructor(x) { this.id = x; } } "
                  "const a = new S('a'); const b = new S('b'); const c = new S('c'); "
                  "return a.id + b.id + c.id; } return f();",
                  "abc");
    // with a static field, which is what put a fresh object in the register
    expect_result(
        "function f() { class S { constructor(x) { this.id = x; } static r = new Map(); } "
        "const a = new S('a'); const b = new S('b'); return a.id + b.id; } return f();",
        "ab");
    // and a class used before its declaration in the same scope still binds
    expect_result("function f() { function make() { return new S(1); } class S { constructor(v) "
                  "{ this.v = v; } } return make().v; } return f();",
                  "1");
}

void test_proxy() {
    expect_result("const p = new Proxy({ v: 1 }, {}); return p.v;", "1");
    expect_result(
        "const p = new Proxy({}, { get(t, k) { return 'got:' + k; } }); return p.anything;",
        "got:anything");
    expect_result("const p = new Proxy({ a: 1 }, { has(t, k) { return k === 'z'; } }); "
                  "return ('z' in p) + '|' + ('a' in p);",
                  "true|false");
    // the trap p5.js needs at its top level
    expect_result("class B { constructor(v) { this.v = v; } } "
                  "const P = new Proxy(B, { construct(t, args) { return new t(args[0] * 2); } }); "
                  "return new P(4).v;",
                  "8");
    // an absent trap falls through to the target rather than being skipped
    expect_result("class B { constructor() { this.v = 'base'; } } "
                  "const P = new Proxy(B, {}); return new P().v;",
                  "base");
    expect_result("return typeof Reflect.get({ a: 5 }, 'a');", "number");
    expect_result("return Reflect.get({ a: 5 }, 'a');", "5");
}

// `Object.defineProperty` with a descriptor that has no value, get or set
// describes ATTRIBUTES ONLY and must leave the existing value alone. Writing
// undefined instead is how `Object.defineProperty(C, "prototype",
// {writable: false})` - which every Babel class emits - wiped the prototype it
// had just filled in.
void test_define_property_attributes_only() {
    expect_result(
        "const o = { v: 1 }; Object.defineProperty(o, 'v', { writable: false }); return o.v;", "1");
    expect_result("function F() {} F.prototype.m = function () { return 1; }; "
                  "Object.defineProperty(F, 'prototype', { writable: false }); "
                  "return typeof F.prototype.m;",
                  "function");
    // a FUNCTION is an object: Babel defines onto the constructor as well
    expect_result("function F() {} Object.defineProperty(F, 'tag', { value: 'x' }); return F.tag;",
                  "x");
}

void test_object_prototype() {
    expect_result("const o = { a: 1 }; return o.hasOwnProperty('a') + '|' + o.hasOwnProperty('b');",
                  "true|false");
    // an INHERITED property is not an own one, which is the whole question
    expect_result("const base = { a: 1 }; const o = Object.create(base); "
                  "return o.a + '|' + o.hasOwnProperty('a');",
                  "1|false");
    expect_result("const o = { get v() { return 1; } }; return o.hasOwnProperty('v');", "true");
    expect_result("const base = {}; const o = Object.create(base); return base.isPrototypeOf(o);",
                  "true");
    expect_result("return ({}).toString();", "[object Object]");
    expect_result("return [1, 2].hasOwnProperty('length');", "true");
}

// A NAMED CLASS EXPRESSION BINDS ITS NAME INSIDE ITSELF, AND NOWHERE ELSE.
//
// `let x = class C {}` used to declare `C` in the ENCLOSING scope, so everything
// compiled after it captured that binding - which holds undefined, because the
// class value goes to `x`. Exactly like a named function expression, whose name
// is visible only in its own body.
//
// COMPILE ORDER decided whether it bit, which is what made it look arbitrary: a
// hoisted function declaration is compiled before the leak exists and reads the
// outer binding correctly; a class method written later does not.
//
// It broke p5.js. The bundle has `let p5$2 = class p5 { ... }`, so the module
// scope acquired a `p5` holding undefined and `new p5.TableRow()` - three
// thousand lines later, inside p5.Table's addRow - read undefined.TableRow. The
// error named `TableRow`, which is not where the problem was.
void test_a_named_class_expression_does_not_leak() {
    // The reader is a CLASS METHOD, compiled after the expression - the case that
    // failed. A function declaration is hoisted and would pass either way.
    expect_result(
        "var Shared = { tag: 'outer' };\n"
        "var alias = class Shared { static who() { return 'inner'; } };\n"
        "class Reader { read() { return Shared === undefined ? 'LEAKED' : Shared.tag; } }\n"
        "return new Reader().read();",
        "outer");
    // A function expression compiled after it, same question.
    expect_result(
        "var Shared = { tag: 'outer' };\n"
        "var alias = class Shared {};\n"
        "var read = function () { return Shared === undefined ? 'LEAKED' : Shared.tag; };\n"
        "return read();",
        "outer");
    // The class can still see its OWN name from inside, which is what the
    // enclosing binding was there for.
    expect_result("var alias = class Inner { static me() { return typeof Inner; } };\n"
                  "return alias.me();",
                  "function");
    expect_result("var made = class Node { constructor() { this.kind = Node.name || 'Node'; } };\n"
                  "return typeof new made().kind;",
                  "string");
    // A named function expression, for the same reason and the same shape.
    expect_result("var Shared = 'outer';\n"
                  "var fn = function Shared() { return 1; };\n"
                  "class Reader { read() { return typeof Shared; } }\n"
                  "return new Reader().read();",
                  "string");
    // And a class DECLARATION still binds its name in the enclosing scope -
    // that is the whole difference, and breaking it would be the mirror bug.
    expect_result("class Kept { static tag() { return 'declared'; } }\n"
                  "class User { read() { return Kept.tag(); } }\n"
                  "return new User().read();",
                  "declared");
    // A declaration inside a function, too.
    expect_result("function outer() {\n"
                  "  class Kept { static tag() { return 'nested'; } }\n"
                  "  class User { read() { return Kept.tag(); } }\n"
                  "  return new User().read();\n"
                  "}\n"
                  "return outer();",
                  "nested");
}

void test_objects_and_arrays() {
    expect_result("let o = { a: 1, b: 2 }; return o.a + o.b;", "3");
    expect_result("let o = { name: 'ctbrowser' }; return o.name;", "ctbrowser");
    expect_result("let o = {}; o.x = 5; return o.x;", "5");
    expect_result("let o = { a: 1 }; return o.missing;", "undefined");
    expect_result("let a = [1, 2, 3]; return a[0] + a[2];", "4");
    expect_result("let a = [1, 2, 3]; return a.length;", "3");
    expect_result("let a = []; a[0] = 'x'; return a[0];", "x");
    expect_result("let a = [1,2,3]; a[1] = 9; return a[1];", "9");
    expect_result("return 'hello'.length;", "5");
    expect_result("let o = { a: 1 }; return o['a'];", "1");
    expect_result(
        "let a = [1,2,3]; let s = 0; for (let i = 0; i < a.length; i++) { s = s + a[i]; } "
        "return s;",
        "6");
    // nested structure
    expect_result("let o = { inner: { deep: 42 } }; return o.inner.deep;", "42");
}

void test_object_statics() {
    expect_result("return Object.keys({a: 1, b: 2}).join(',');", "a,b");
    expect_result("return Object.values({a: 1, b: 2}).join(',');", "1,2");
    expect_result("var t = Object.assign({}, {a: 1}, {b: 2}); return t.a + t.b;", "3");
}

void test_own_properties_beat_the_prototype() {
    // A page that puts its own `join` on an object must get its own, not the
    // array method. Prototype lookup has to come SECOND.
    expect_result("var o = {join: function () { return 'mine'; }}; return o.join();", "mine");
}

void test_computed_and_named_lookup_agree() {
    // `a["length"]` and `a.length` went down different paths, so a page that
    // computed a property name silently saw undefined.
    expect_result("var a = [1,2,3]; return a['length'];", "3");
    expect_result("var s = 'abc'; return s['length'];", "3");
    expect_result("var a = [1,2]; var m = 'push'; a[m](3); return a.length;", "3");
}

void test_new_and_classes() {
    expect_result("class P { constructor(n) { this.n = n; } }"
                  "var p = new P(7); return p.n;",
                  "7");
    expect_result("class P { constructor(n) { this.n = n; } double() { return this.n * 2; } }"
                  "return new P(4).double();",
                  "8");
    // A method lives on the prototype, so two instances share one function and
    // both find it.
    expect_result("class P { hi() { return 'hi'; } }"
                  "var a = new P(); var b = new P(); return a.hi() + b.hi();",
                  "hihi");
    expect_result("class P { static make() { return 'static'; } } return P.make();", "static");
    // `extends` chains the prototypes, so an inherited method is reachable.
    expect_result("class A { who() { return 'A'; } }"
                  "class B extends A { } return new B().who();",
                  "A");
    expect_result("class A { who() { return 'A'; } }"
                  "class B extends A { who() { return 'B'; } } return new B().who();",
                  "B");

    // `super(...)` runs the parent constructor against the SAME object, so the
    // fields it sets are on the instance.
    expect_result("class A { constructor(n) { this.n = n; } }"
                  "class B extends A { constructor() { super(3); this.m = 4; } }"
                  "var b = new B(); return b.n + b.m;",
                  "7");
    // `super.m()` calls the parent's version, and `this` inside it is still the
    // instance.
    expect_result("class A { label() { return 'A' + this.n; } }"
                  "class B extends A { constructor() { this.n = 1; }"
                  "  label() { return super.label() + 'B'; } } return new B().label();",
                  "A1B");
    // Three deep. This is what resolving super against `this` gets wrong: C's
    // method would find itself again and recurse until the stack gave out.
    expect_result("class A { who() { return 'A'; } }"
                  "class B extends A { who() { return super.who() + 'B'; } }"
                  "class C extends B { who() { return super.who() + 'C'; } }"
                  "return new C().who();",
                  "ABC");
    expect_result("class A { }; return typeof new A().constructor;", "function");
}

// A SYMBOL-KEYED PROPERTY ON A CLASS. Babylon's decorator metadata does exactly
// this - `new.target[Symbol.metadata] = {}` and then reads it straight back -
// and throws a named error if the read comes back empty.
void test_symbol_keys_on_functions() {
    expect_result("const s = Symbol('m'); const o = {}; o[s] = 3; return o[s];", "3");
    expect_result("const s = Symbol('m'); function F() {} F[s] = 2; return F[s];", "2");
    expect_result("const s = Symbol('m'); class C {} C[s] = 7; return C[s];", "7");
    expect_result("const s = Symbol('m'); class C {} return String(!C[s]);", "true");
    expect_result("const s = Symbol('m'); class C {} C[s] = {a: 1}; return C[s].a;", "1");
    // Through `new.target`, which is how the class gets at itself.
    expect_result("const s = Symbol('m');"
                  "class E { constructor() { const t = new.target; t[s] = t[s] || {v: 5}; } }"
                  "new E(); return E[s].v;",
                  "5");
    // NEW.TARGET THROUGH A super() CHAIN. Babylon reads it in a derived
    // constructor, after super(...), to hang decorator metadata off the class
    // actually being constructed.
    expect_result("var seen = 'unset';"
                  "class A { constructor() { seen = new.target === undefined ? 'undefined'"
                  " : new.target.name; } }"
                  "class B extends A { constructor() { super(); } }"
                  "new B(); return seen;",
                  "B");
    expect_result("var seen = 'unset';"
                  "class A { constructor() { seen = String(new.target && new.target.name); } }"
                  "class B extends A {}"
                  "new B(); return seen;",
                  "B");
    // And in the derived constructor itself, after super() has run.
    expect_result("var seen = 'unset';"
                  "class A { constructor() {} }"
                  "class B extends A { constructor() { super(); seen = String(new.target"
                  " && new.target.name); } }"
                  "new B(); return seen;",
                  "B");
    // A subclass must not see the parent's own metadata as its own.
    expect_result("const s = Symbol('m'); class A {} A[s] = 1; class B extends A {}"
                  "return String(Object.prototype.hasOwnProperty.call(B, s));",
                  "false");
}

void test_object_literal_keys() {
    expect_result("var k = 'x'; var o = {[k]: 5}; return o.x;", "5");
    expect_result("var o = {'a b': 1}; return o['a b'];", "1");
    expect_result("var o = {1: 'one'}; return o[1];", "one");
    // Spread copies own properties in sequence, so a later key still wins.
    expect_result("var a = {x: 1, y: 2}; var b = {...a, y: 3}; return b.x + b.y;", "4");
    expect_result("var a = {x: 1}; var b = {...a}; b.x = 9; return a.x;", "1"); // a copy
    expect_result("var a = {x: 1}; var b = {y: 2, ...a}; var keys = '';"
                  "for (const k in b) { keys += k; } return keys;",
                  "yx");
}

} // namespace

int main() {
    test_class_fields_are_per_instance();
    test_private_names_are_distinct();
    test_accessors();
    test_object_descriptors();
    test_symbol();
    test_new_target();
    test_implicit_super();
    test_class_expressions();
    test_named_class_expression_binds_itself();
    test_class_declaration_survives_the_statement();
    test_proxy();
    test_define_property_attributes_only();
    test_object_prototype();
    test_a_named_class_expression_does_not_leak();
    test_objects_and_arrays();
    test_object_statics();
    test_own_properties_beat_the_prototype();
    test_computed_and_named_lookup_agree();
    test_new_and_classes();
    test_object_literal_keys();
    test_symbol_keys_on_functions();
    REPORT("vm_objects");
}
