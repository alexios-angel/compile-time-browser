//===- wrapper_objects.cpp - Primitive wrapper tests ------------*- C++ -*-===//
//
// Primitive wrapper objects (`new Number(5)`, `new Boolean(false)`,
// `Object("ab")`), checked against V8's answers.
//
// A wrapper is an ordinary object on its kind's prototype, with the primitive
// in a private-keyed slot (see primitive_slot in value/objects.hpp).
// `Object("ab")` and `new String("ab")` build the same String exotic object,
// so `length` and indices are tested through both.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

int main() {
    // --- What a wrapper is ---------------------------------------------------
    js_expect("typeof new Number(5)", "object");
    js_expect("typeof new Boolean(false)", "object");
    js_expect("typeof Object('ab')", "object");
    js_expect("typeof new String('ab')", "object");
    js_expect("new String('ab').length", "2");
    js_expect("new String('ab')[1]", "b");
    js_expect("new String('ab') + 'c'", "abc");
    js_expect("new String(5).valueOf()", "5");
    js_expect("String(Symbol('d'))", "Symbol(d)");
    js_expect("(function(){ try { new String(Symbol()); return 'no'; } catch (e) { return e "
              "instanceof TypeError; } })()",
              "true");
    js_expect("JSON.stringify([new Number(3), new String('s'), new Boolean(false)])",
              "[3,\"s\",false]");
    js_expect("String.fromCharCode({valueOf: () => 65}, 66.9)", "AB");
    js_expect("typeof Number(5)", "number"); // A call still converts.
    js_expect("typeof Boolean(0)", "boolean");
    js_expect("new Number(5) instanceof Number", "true");
    js_expect("new Boolean(true) instanceof Boolean", "true");
    js_expect("Object('ab') instanceof String", "true");
    js_expect("Object.getPrototypeOf(new Number(1)) === Number.prototype", "true");
    js_expect("Object(5) === Object(5)", "false"); // Two objects.
    js_expect("(function(){var o = {}; return Object(o) === o;})()", "true");

    // --- The slot is invisible ---------------------------------------------
    js_expect("Object.keys(new Number(5)).length", "0");
    js_expect("Object.getOwnPropertyNames(new Boolean(true)).length", "0");
    js_expect("JSON.stringify(Object.keys(Object('ab')))", "[\"0\",\"1\"]");
    js_expect("JSON.stringify(Object.getOwnPropertyNames(Object('ab')))",
              "[\"0\",\"1\",\"length\"]");

    // --- The primitive comes back out --------------------------------------
    js_expect("new Number(5) + 1", "6");
    js_expect("new Number(5) == 5", "true");
    js_expect("new Number(5) === 5", "false");
    js_expect("new Number(5).valueOf()", "5");
    js_expect("new Number(255).toString(16)", "ff");
    js_expect("new Number(1.005).toFixed(2)", "1.00");
    js_expect("new Boolean(false).valueOf()", "false");
    js_expect("new Boolean(false).toString()", "false");
    js_expect("Boolean(new Boolean(false))", "true"); // Every object is truthy.
    js_expect("Object('ab').valueOf()", "ab");
    js_expect("Object('ab') + 'c'", "abc");
    js_expect("Object('ab').toUpperCase()", "AB");
    js_expect("String(Object('ab'))", "ab");

    // --- Object.prototype.toString sees the slot ----------------------------
    js_expect("Object.prototype.toString.call(new Number(1))", "[object Number]");
    js_expect("Object.prototype.toString.call(new Boolean(1))", "[object Boolean]");
    js_expect("Object.prototype.toString.call(Object('x'))", "[object String]");
    // The three prototypes carry a slot of their own (ECMA-262 21.1.3,
    // 22.1.3, 20.3.3).
    js_expect("Object.prototype.toString.call(Number.prototype)", "[object Number]");
    js_expect("Object.prototype.toString.call(String.prototype)", "[object String]");
    js_expect("Object.prototype.toString.call(Boolean.prototype)", "[object Boolean]");

    // --- The String exotic object (ECMA-262 10.4.3) ----------------------------
    js_expect("Object('abc').length", "3");
    js_expect("Object('abc')[1]", "b");
    js_expect("Object('abc')['2']", "c");
    js_expect("Object('abc')[3]", "undefined");
    js_expect("'1' in Object('abc')", "true");
    js_expect("Object('abc').hasOwnProperty('length')", "true");
    js_expect("Object('abc').hasOwnProperty('0')", "true");
    js_expect("JSON.stringify(Object.getOwnPropertyDescriptor(Object('ab'), 'length'))",
              "{\"value\":2,\"writable\":false,\"enumerable\":false,\"configurable\":false}");
    js_expect("JSON.stringify(Object.getOwnPropertyDescriptor(Object('ab'), '0'))",
              "{\"value\":\"a\",\"writable\":false,\"enumerable\":true,\"configurable\":false}");
    js_expect("(function(){var s = Object('ab'); s[0] = 'x'; return s[0];})()", "a");
    js_expect("(function(){var s = Object('ab'); s.length = 9; return s.length;})()", "2");
    // A refused delete is observed through the property staying.
    js_expect("(function(){var s = Object('ab'); delete s[0]; return s.hasOwnProperty(0);})()",
              "true");
    js_expect("(function(){var s = Object('ab'); s.extra = 1; return s.extra;})()", "1");
    js_expect("Object.keys(Object('ab')).concat(Object.keys(Object(5))).length", "2");

    // --- Symbol and BigInt box the same way (Object(x) only; both refuse new).
    js_expect("typeof Object(Symbol('s'))", "object");
    js_expect("Object(Symbol('s')).toString()", "Symbol(s)");
    js_expect("Object(Symbol('s')).valueOf() === Object(Symbol('s')).valueOf()", "false");
    js_expect("(function(){var y = Symbol(); return Object(y).valueOf() === y;})()", "true");
    js_expect("Object(5n) instanceof BigInt", "true");
    js_expect("Object(5n).valueOf()", "5");
    js_expect("Object(255n).toString(16)", "ff");
    js_expect("Object.prototype.toString.call(Object(Symbol()))", "[object Symbol]");
    js_expect("Symbol.prototype.toString.call(1)", "THREW");
    js_expect("BigInt.prototype.valueOf.call(1)", "THREW");

    // --- Brand checks: a method on the wrong receiver throws ----------------
    js_expect("Number.prototype.valueOf.call({})", "THREW");
    js_expect("Number.prototype.toString.call('1')", "THREW");
    js_expect("Boolean.prototype.valueOf.call(1)", "THREW");
    js_expect("Boolean.prototype.toString.call({})", "THREW");
    js_expect("String.prototype.valueOf.call(1)", "THREW");
    js_expect("Number.prototype.valueOf.call(new Number(7))", "7");
    js_expect("Boolean.prototype.valueOf.call(new Boolean(true))", "true");
    js_expect("String.prototype.valueOf.call(Object('s'))", "s");

    // --- A wrapper is a descriptor: 8.10.5 reads through [[Get]] ----------
    js_expect("(function(){var d = new Number(1); d.configurable = true; d.value = 3;"
              " var o = Object.defineProperty({}, 'p', d); return o.p;})()",
              "3");
    js_expect("(function(){var d = new Boolean(false); d.enumerable = true; d.value = 4;"
              " return Object.keys(Object.create({}, {p: d})).join();})()",
              "p");

    // --- Symbol refuses ToNumber and ToString (ECMA-262 7.1.4, 7.1.17) -------
    js_expect("Number(Symbol())", "THREW");
    js_expect("[].copyWithin(0, 0, Symbol())", "THREW");
    js_expect("'abc'.indexOf(Symbol())", "THREW");
    js_expect("'abc'.padStart(5, Symbol())", "THREW");
    js_expect("String.prototype.trim.call(Symbol())", "THREW");
    js_expect("'abc'.at(Symbol())", "THREW");
    js_expect("String(Symbol('d'))", "Symbol(d)"); // The one conversion allowed.

    REPORT("wrapper_objects");
}
