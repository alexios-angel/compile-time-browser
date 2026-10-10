//===- ctbrowser/unittests/js/symbol_basics.cpp - Symbol tests ------------===//
//
// Symbols, checked against node (V8). A symbol is a property key that cannot
// collide and that a page cannot stumble across by enumerating.
//
// This engine stores a symbol key as a string led by a byte no JS string can
// hold (core/symbol.hpp), in the ordinary property table. These cases pin that
// the representation never leaks: string-only enumerations skip it,
// JSON.stringify drops it, String(sym) describes it, and implicit conversion
// throws.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

int main() {
    // --- the type -------------------------------------------------------------
    js_expect("typeof Symbol", "function");
    js_expect("typeof Symbol()", "symbol");
    js_expect("typeof Symbol(\"x\")", "symbol");
    js_expect("Symbol(\"d\").description", "d");

    // --- uniqueness, which is the entire point --------------------------------
    js_expect("Symbol(\"a\") === Symbol(\"a\")", "false");
    js_expect("(function(){var s=Symbol();return s === s})()", "true");
    js_expect("Boolean(Symbol())", "true"); // Truthy, like every non-falsy primitive.

    // A Symbol is a primitive even though the VM stores it on the heap.
    js_expect(R"JS((function() {
        var symbol = Symbol('x'), calls = '';
        var object = {[Symbol.toPrimitive](hint) { calls += hint + ','; return symbol; }};
        return [object == symbol, symbol == object, object != symbol,
                symbol != object, symbol == Symbol('x'), symbol == 'Symbol(x)',
                symbol == Object(symbol), calls].join(';');
    })())JS",
              "true;true;false;false;false;false;true;default,default,default,default,");

    // --- the well-known symbols -----------------------------------------------
    js_expect("typeof Symbol.iterator", "symbol");
    js_expect("typeof Symbol.asyncIterator", "symbol");
    js_expect("typeof Symbol.toStringTag", "symbol");
    js_expect("typeof Symbol.hasInstance", "symbol");
    js_expect("Symbol.iterator === Symbol.iterator", "true");

    // --- as a property key ----------------------------------------------------
    js_expect("(function(){var s=Symbol(\"p\");var o={};o[s]=1;return o[s]})()", "1");
    js_expect("(function(){var s=Symbol(\"p\");var o={};o[s]=1;return o.p})()", "undefined");
    js_expect("(function(){var a=Symbol(\"k\"),b=Symbol(\"k\");var o={};o[a]=1;o[b]=2;"
              "return o[a]+\",\"+o[b]})()",
              "1,2"); // Same description, different keys.

    // --- string-only enumerations skip symbol keys ----------------------------
    // Object.keys, Object.values, for-in, getOwnPropertyNames and JSON.stringify
    // skip them; Object.assign and Reflect.ownKeys see them, as specified, which
    // is why the filter is a second method rather than a change to the
    // unfiltered walk.
    js_expect("(function(){var s=Symbol(\"p\");var o={};o[s]=1;return Object.keys(o).length})()",
              "0");
    js_expect("(function(){var s=Symbol(\"p\");var o={a:1};o[s]=2;return JSON.stringify(o)})()",
              "{\"a\":1}");
    js_expect("(function(){var s=Symbol(\"p\");var o={a:1};o[s]=2;var r=[];"
              "for(var k in o)r.push(k);return r.join(\",\")})()",
              "a");
    js_expect("(function(){var s=Symbol(\"p\");var o={a:1};o[s]=2;"
              "return Object.getOwnPropertyNames(o).join(\",\")})()",
              "a");
    // String(sym) describes rather than coerces: the one conversion the
    // specification allows on a symbol.
    js_expect("String(Symbol(\"x\"))", "Symbol(x)");
    js_expect("String(Symbol())", "Symbol()");

    // --- the registry interns -------------------------------------------------
    js_expect("Symbol.for(\"k\") === Symbol.for(\"k\")", "true");
    js_expect("Symbol.for(\"a\") === Symbol.for(\"b\")", "false");
    js_expect("typeof Symbol.keyFor", "function");
    js_expect("Symbol.keyFor(Symbol.for(\"k\"))", "k");
    // A missing key is ToString(undefined).
    js_expect("Symbol.for() === Symbol.for('undefined')", "true");
    js_expect("Symbol.keyFor(Symbol(\"k\"))", "undefined"); // Never registered.
    js_expect("typeof Symbol.prototype", "object");

    // --- a proxy trap is handed the symbol, not its internal spelling ---------
    // (js_expect takes an expression, hence the IIFEs.)
    js_expect("(function () { var seen = []; var p = new Proxy({}, { get: function (t, k) {"
              " seen.push(typeof k); return 1; }, has: function (t, k) { seen.push(typeof k);"
              " return true; } }); p[Symbol.iterator]; p.x; Symbol.iterator in p;"
              " return seen.join(); })()",
              "symbol,string,symbol");
    js_expect("(function () { var got; new Proxy({}, { get: function (t, k) { got = k; } })"
              "[Symbol.iterator]; return got === Symbol.iterator; })()",
              "true");

    // --- a symbol's key and a slot's key cannot be spelled -------------------
    // Both are string keys led by a byte no JS string holds (core/symbol.hpp).
    // A printable prefix would let a page forge them: a computed key such as
    // `@@iterator` could pose as Symbol.iterator, and `@#ErrorData` could make
    // any object an Error to Object.prototype.toString.
    js_expect("(function () { var o = {}; o['@@iterator'] = 1; o['@@sym:1:x'] = 2;"
              " return [typeof o[Symbol.iterator], Symbol.iterator in o,"
              " Object.getOwnPropertySymbols(o).length].join(); })()",
              "undefined,false,0");
    js_expect("Object.prototype.toString.call({'@#ErrorData': 1})", "[object Object]");
    js_expect("(function () { var o = {}; o[String.fromCharCode(0xFF) + '@iterator'] = 1;"
              " return typeof o[Symbol.iterator]; })()",
              "undefined");
    // The byte itself in the source decodes to U+FFFD, so it cannot forge one.
    js_expect("(function () { var s = '\xFF@iterator'; var o = {}; o[s] = 1;"
              " return s.charCodeAt(0).toString(16) + ' ' + typeof o[Symbol.iterator]; })()",
              "fffd undefined");
    // ...while every path that hands a key back keeps it the key it was.
    js_expect("(function () { var s = Symbol('k'); var o = {}; o[s] = 7;"
              " return [Reflect.get(o, s), Reflect.ownKeys(o)[0] === s,"
              " Object.getOwnPropertySymbols(o)[0] === s, Reflect.set(o, s, 8), o[s]].join();"
              " })()",
              "7,true,true,true,8");
    js_expect("(function () { var seen; var p = new Proxy({}, { getOwnPropertyDescriptor:"
              " function (t, k) { seen = k; } }); Object.prototype.hasOwnProperty.call(p,"
              " Symbol.iterator); return seen === Symbol.iterator; })()",
              "true");
    js_expect("(function () { class A { #x = 1; get #y() { return 2; } sum() {"
              " return this.#x + this.#y; } static has(o) { return #x in o; } }"
              " return [new A().sum(), A.has(new A()), A.has({})].join(); })()",
              "3,true,false");

    // --- a symbol refuses implicit conversion --------------------------------
    // `"" + sym` and `sym + 1` throw TypeError (7.1.17, 7.1.4), and that is a
    // feature: it is what stops a symbol silently reaching page output. So do
    // a template substitution, `-`, unary `+` and a relational comparison;
    // String(sym) is the one conversion that describes it.
    js_expect("(function(){try{return \"\"+Symbol(\"x\")}catch(e){return \"THROWS \"+e.name}})()",
              "THROWS TypeError");
    js_expect("(function(){try{return Symbol(\"x\")+1}catch(e){return \"THROWS \"+e.name}})()",
              "THROWS TypeError");
    js_expect("(function () { var s = Symbol('x'), out = [];"
              " [function () { return `${s}`; }, function () { return s - 1; },"
              " function () { return +s; }, function () { return s < 1; },"
              " function () { return 3n > s; }].forEach(function (f) {"
              " try { out.push(f()); } catch (e) { out.push(e.name); } });"
              " return out.join(); })()",
              "TypeError,TypeError,TypeError,TypeError,TypeError");

    // An absent description is undefined, an empty one is "" (20.4.3.2; the
    // prototype's getter tells them apart by the key).
    js_expect("Symbol().description", "undefined");
    js_expect("typeof Symbol('').description + ',' + Symbol('x').description", "string,x");
    js_expect("Symbol('x').hasOwnProperty('description')", "false");

    REPORT("symbol_basics");
}
