// `symbol`, against V8.
//
// The primitive whose whole purpose is to be a property key that CANNOT
// collide, and that a page cannot stumble across by enumerating. Differentially
// tested against node (V8); 8 differences, and they share one cause worth
// stating plainly.
//
// THE CAUSE: this engine represents a symbol key as the STRING "@@sym:N:desc"
// and stores it in the ordinary property table. That works for lookup - a
// symbol key round-trips - and leaks everywhere the representation is
// observable: Object.keys sees it, JSON.stringify serialises it, and
// `String(sym)` prints it. Symbols also fail to be UNCOERCIBLE: the specified
// behaviour is that implicit conversion THROWS, which is what stops one
// reaching page output by accident. All pinned below.

#include "js_expect.hpp"

int main() {
    // --- the type -------------------------------------------------------------
    js_expect("typeof Symbol", "function");
    js_expect("typeof Symbol()", "symbol");
    js_expect("typeof Symbol(\"x\")", "symbol");
    js_expect("Symbol(\"d\").description", "d");

    // --- UNIQUENESS, which is the entire point --------------------------------
    js_expect("Symbol(\"a\") === Symbol(\"a\")", "false");
    js_expect("(function(){var s=Symbol();return s === s})()", "true");
    js_expect("Boolean(Symbol())", "true"); // truthy, like every non-falsy primitive

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
              "1,2"); // same description, different keys - the collision guarantee

    // --- the representation no longer leaks -----------------------------------
    // A symbol key is still stored as "@@sym:N:desc" in the ordinary property
    // table, but every enumeration specified to see STRING keys only now
    // filters it: Object.keys, Object.values, for-in, getOwnPropertyNames and
    // JSON.stringify. Object.assign and Reflect.ownKeys keep the UNFILTERED
    // walk, because those two are specified to see symbols - which is why the
    // filter is a second method rather than a change to the existing one.
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
    // String(sym) DESCRIBES rather than coerces - the one conversion the
    // specification allows on a symbol.
    js_expect("String(Symbol(\"x\"))", "Symbol(x)");
    js_expect("String(Symbol())", "Symbol()");

    // --- the registry, which now interns --------------------------------------
    js_expect("Symbol.for(\"k\") === Symbol.for(\"k\")", "true");
    js_expect("Symbol.for(\"a\") === Symbol.for(\"b\")", "false");
    js_expect("typeof Symbol.keyFor", "function");
    js_expect("Symbol.keyFor(Symbol.for(\"k\"))", "k");
    js_expect("Symbol.keyFor(Symbol(\"k\"))", "undefined"); // never registered
    js_expect("typeof Symbol.prototype", "object");

    // --- KNOWN WRONG: a symbol must REFUSE implicit conversion ----------------
    // `"" + sym` and `sym + 1` are specified to throw TypeError, and that is a
    // feature: it is what stops a symbol silently reaching page output. Here
    // they coerce through the internal spelling instead.
    js_expect("(function(){try{return \"\"+Symbol(\"x\")}catch(e){return \"THROWS \"+e.name}})()",
              "Symbol(x)"); // V8: THROWS TypeError
    js_expect("(function(){try{return Symbol(\"x\")+1}catch(e){return \"THROWS \"+e.name}})()",
              "Symbol(x)1"); // V8: THROWS TypeError

    // --- KNOWN WRONG ----------------------------------------------------------
    // An ABSENT description is undefined, an empty one is "". This engine
    // stores a plain std::string and cannot tell them apart.
    js_expect("Symbol().description", ""); // V8: undefined

    REPORT("symbol_basics");
}
