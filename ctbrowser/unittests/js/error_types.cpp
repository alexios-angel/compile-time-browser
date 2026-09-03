// SIX ERROR CONSTRUCTORS, SIX PROTOTYPES, and the one place they were all
// collapsed into Error.
//
// `Error`, `TypeError`, `RangeError`, `ReferenceError`, `SyntaxError`,
// `EvalError` and `URIError` each had a constructor and a prototype already -
// what did not was an error the ENGINE raised. `context::make_error` put every
// one of them on Error.prototype, so an engine-raised TypeError had
// `name === "TypeError"` and `constructor === Error`, and
// `assert.throws(TypeError, ...)` could not match it. test262 counted 336 tests
// failing with exactly that shape (docs/test262.md, 2026-09-02) and the runner
// carries a documented leniency - it matches on `thrown.name` OR
// `thrown.constructor.name` - because of it.
//
// THIS FILE IS THE REGRESSION NET, not test262: that suite is opt-in, needs a
// 273 MB corpus and takes five minutes. Expected values are node's.

#include "js_expect.hpp"

// An expression that throws, reduced to the answer we want to assert about the
// thrown value. Written out rather than macro'd so a failure prints the source.
static std::string caught(std::string_view thrower, std::string_view question) {
    return "(function(){try{" + std::string{thrower} + ";return 'no throw';}catch(e){return " +
           std::string{question} + ";}})()";
}

int main() {
    // ================================================================
    // 1. THE CONSTRUCTORS THEMSELVES
    // ================================================================
    js_expect("Error.name", "Error");
    js_expect("TypeError.name", "TypeError");
    js_expect("RangeError.name", "RangeError");
    js_expect("ReferenceError.name", "ReferenceError");
    js_expect("SyntaxError.name", "SyntaxError");
    js_expect("EvalError.name", "EvalError");
    js_expect("URIError.name", "URIError");
    js_expect("Error.length+','+TypeError.length", "1,1");
    // A NativeError constructor's [[Prototype]] is %Error% (20.5.6.2), not
    // Function.prototype - which is how `TypeError.captureStackTrace` and any
    // other static on Error is inherited by all six.
    js_expect("Object.getPrototypeOf(TypeError)===Error", "true");

    // ================================================================
    // 2. THE PROTOTYPE CHAIN
    // ================================================================
    js_expect("Object.getPrototypeOf(TypeError.prototype)===Error.prototype", "true");
    js_expect("Object.getPrototypeOf(RangeError.prototype)===Error.prototype", "true");
    js_expect("TypeError.prototype.constructor===TypeError", "true");
    js_expect("Error.prototype.constructor===Error", "true");
    js_expect("TypeError.prototype.name", "TypeError");
    js_expect("Error.prototype.name", "Error");
    // NativeError.prototype.message is "" (20.5.6.3.2), and it is an OWN
    // property of each prototype rather than inherited from Error's.
    js_expect("TypeError.prototype.message", "");
    js_expect("TypeError.prototype.hasOwnProperty('message')", "true");
    js_expect("TypeError.prototype.hasOwnProperty('name')", "true");
    // Clause 17 again: none of the three is enumerable.
    js_expect("Object.keys(TypeError.prototype).length", "0");
    js_expect("Object.keys(Error.prototype).length", "0");

    // ================================================================
    // 3. A CONSTRUCTED ERROR
    // ================================================================
    js_expect("(new TypeError('m')).constructor===TypeError", "true");
    js_expect("(new TypeError('m')) instanceof TypeError", "true");
    js_expect("(new TypeError('m')) instanceof Error", "true");
    js_expect("(new RangeError('m')) instanceof TypeError", "false");
    js_expect("(new TypeError('m')).name", "TypeError");
    js_expect("(new TypeError('m')).message", "m");
    js_expect("(new TypeError('m')).toString()", "TypeError: m");
    js_expect("(new TypeError()).message", "");
    js_expect("(new TypeError()).toString()", "TypeError");
    // `name` comes from the PROTOTYPE - an instance has no own one (20.5.6.5).
    js_expect("(new TypeError('m')).hasOwnProperty('name')", "false");
    js_expect("(new TypeError('m')).hasOwnProperty('message')", "true");
    // ...and `message` is not enumerable, so an error does not serialise its
    // own text as data (20.5.8.1: { true, false, true }).
    js_expect("Object.keys(new TypeError('m')).length", "0");
    js_expect("JSON.stringify(new TypeError('m'))", "{}");
    // Called WITHOUT new, which the spec makes identical (20.5.6.1).
    js_expect("TypeError('m').constructor===TypeError", "true");
    js_expect("TypeError('m').name", "TypeError");

    // ================================================================
    // 4. AN ERROR THE ENGINE RAISED - the 336-test gap
    // ================================================================
    //
    // Every one of these went through context::make_error, which put all of
    // them on Error.prototype. `e.name` was right (it was written as an own
    // property); `e.constructor` was Error for all seven kinds.
    // `(void 0)()` rather than `null.x`: reading a property of null is
    // undefined here rather than a TypeError, which is a DIFFERENT gap (the
    // engine has no null/undefined guard on member access) and not one this
    // file is about.
    js_expect(caught("(void 0)()", "e.constructor===TypeError"), "true");
    js_expect(caught("(void 0)()", "e instanceof TypeError"), "true");
    js_expect(caught("(void 0)()", "e instanceof Error"), "true");
    js_expect(caught("(void 0)()", "e.name"), "TypeError");
    js_expect(caught("new 5", "e.constructor===TypeError"), "true");
    js_expect(caught("(5).toString(1)", "e.constructor===RangeError"), "true");
    js_expect(caught("(5).toString(1)", "e instanceof RangeError"), "true");
    js_expect(caught("(5).toString(1)", "e.constructor===TypeError"), "false");
    js_expect(caught("[].length=-1", "e.constructor===RangeError"), "true");
    js_expect(caught("new Array(-1)", "e.constructor===RangeError"), "true");
    js_expect(caught("'x'.repeat(-1)", "e.constructor===RangeError"), "true");
    js_expect(caught("new Function('var a = ;')", "e.constructor===SyntaxError"), "true");
    js_expect(caught("BigInt('nope')", "e.constructor===SyntaxError"), "true");
    js_expect(caught("BigInt(1.5)", "e.constructor===RangeError"), "true");
    js_expect(caught("1n + 1", "e.constructor===TypeError"), "true");
    js_expect(caught("1n >>> 1n", "e.constructor===TypeError"), "true");
    js_expect(caught("Object.defineProperty(1,'x',{})", "e.constructor===TypeError"), "true");
    // NOT ASSERTED HERE: the 512-frame ceiling in `invoke` ends the run
    // outright rather than throwing a catchable RangeError - `try { f() }
    // catch (e)` around an infinite recursion never sees one - so there is no
    // constructor to compare. The CONVERSION ceiling does throw catchably and
    // unittests/js/crash_guards.cpp covers that one.
    //
    // ...and an engine-raised error still carries a stack and reads back as one.
    js_expect(caught("(void 0)()", "typeof e.stack"), "string");
    js_expect(caught("(void 0)()", "e.toString().indexOf('TypeError: ')===0"), "true");

    // ================================================================
    // 5. A KIND THE ENGINE HAS NO CONSTRUCTOR FOR falls back to Error
    // ================================================================
    //
    // structuredClone raises a DOMException named "DataCloneError", which is a
    // web name rather than an ECMAScript one and has no constructor here. The
    // fallback must be Error rather than a crash or a nameless object.
    js_expect(caught("structuredClone(function(){})", "e.constructor===Error"), "true");
    js_expect(caught("structuredClone(function(){})", "e.name"), "DataCloneError");

    // ================================================================
    // 6. A PAGE'S OWN SUBCLASS still works, which is what `extends` needs
    // ================================================================
    js_expect("(function(){class E extends Error{};var e=new E('m');"
              "return (e instanceof E)+','+(e instanceof Error);})()",
              "true,true");
    js_expect("(function(){class E extends TypeError{};var e=new E('m');"
              "return (e instanceof TypeError)+','+(e instanceof Error);})()",
              "true,true");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
