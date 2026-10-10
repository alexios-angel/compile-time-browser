//===- ctbrowser/unittests/js/null_basics.cpp - null value tests ----------===//
//
// `null`, the value that means "deliberately empty", as against `undefined`'s
// "never set". Expected values are node's (V8). A breakage here would not
// announce itself anywhere else, which is why null has a file of its own.
//
// The character of null is that it coerces to zero where undefined coerces to
// NaN, and that its `typeof` is a decades-old mistake that every page depends
// on. unittests/js/undefined_basics.cpp is the other half.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

int main() {
    // --- typeof, and the oldest bug in the language. -------------------------
    // `typeof null` is "object" and will never be anything else: fixing it was
    // proposed and withdrawn because too much code branches on it. It is why
    // `x !== null && typeof x === "object"` is the idiom for "a real object".
    js_expect("typeof null", "object");
    js_expect("null === null", "true");
    js_expect("typeof JSON.parse(\"null\")", "object");

    // --- Against undefined. --------------------------------------------------
    js_expect("null == undefined", "true");
    js_expect("null === undefined", "false");

    // --- Conversions: zero, not NaN. -----------------------------------------
    // This is the whole practical difference from undefined. `null + 1` is 1
    // and `undefined + 1` is NaN, so a missing field that defaults to null
    // silently sums where one that defaults to undefined poisons the total.
    js_expect("String(null)", "null");
    js_expect("Number(null)", "0");
    js_expect("Boolean(null)", "false");
    js_expect("!null", "true");
    js_expect("null + 1", "1");
    js_expect("null * 2", "0");
    js_expect("null + \"\"", "null");

    // --- Comparisons, where it is deliberately inconsistent. -----------------
    // `null == 0` is false but `null >= 0` is true: loose equality has a
    // special case for null, and relational comparison does not; it goes
    // through ToNumber and gets 0. This asymmetry is specified, not a bug.
    js_expect("null == 0", "false");
    js_expect("null == false", "false");
    js_expect("null == \"\"", "false");
    js_expect("null >= 0", "true");
    js_expect("null > 0", "false");
    js_expect("null < 1", "true");

    // --- The operators that single it out. -----------------------------------
    js_expect("null ?? \"d\"", "d");
    js_expect("null || \"d\"", "d");
    js_expect("null && \"d\"", "null");
    js_expect("null?.x", "undefined"); // Optional chaining short-circuits.

    // --- In arrays and JSON. -------------------------------------------------
    // Unlike undefined, null survives serialisation: it is a real JSON value.
    js_expect("[null].length", "1");
    js_expect("String([null])", ""); // But it still joins as empty.
    js_expect("JSON.stringify(null)", "null");
    js_expect("JSON.stringify([null])", "[null]");
    js_expect("JSON.stringify({a:null})", "{\"a\":null}");
    js_expect("({a:null}).a", "null");

    REPORT("null_basics");
}
