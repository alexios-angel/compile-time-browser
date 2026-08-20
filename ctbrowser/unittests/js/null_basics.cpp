// `null`, against V8.
//
// The value that means "deliberately empty", as against `undefined`'s "never
// set". Differentially tested against node (V8) and it came out CLEAN - zero
// differences - which is worth a file of its own precisely because nothing here
// is going to announce itself if it breaks.
//
// The character of null is that it coerces to ZERO where undefined coerces to
// NaN, and that its `typeof` is a forty-year-old bug that every page depends
// on. unittests/js/undefined_basics.cpp is the other half.

#include "js_expect.hpp"

int main() {
    // --- typeof, and the oldest bug in the language --------------------------
    // `typeof null` is "object" and will never be anything else: fixing it was
    // proposed and withdrawn because too much code branches on it. It is why
    // `x !== null && typeof x === "object"` is the idiom for "a real object".
    js_expect("typeof null", "object");
    js_expect("null === null", "true");
    js_expect("typeof JSON.parse(\"null\")", "object");

    // --- against undefined ---------------------------------------------------
    js_expect("null == undefined", "true");
    js_expect("null === undefined", "false");

    // --- conversions: ZERO, not NaN ------------------------------------------
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

    // --- comparisons, where it is deliberately inconsistent ------------------
    // `null == 0` is FALSE but `null >= 0` is TRUE: loose equality has a
    // special case for null, and relational comparison does not - it goes
    // through ToNumber and gets 0. This asymmetry is specified, not a bug, and
    // it is the classic interview question.
    js_expect("null == 0", "false");
    js_expect("null == false", "false");
    js_expect("null == \"\"", "false");
    js_expect("null >= 0", "true");
    js_expect("null > 0", "false");
    js_expect("null < 1", "true");

    // --- the operators that single it out ------------------------------------
    js_expect("null ?? \"d\"", "d");
    js_expect("null || \"d\"", "d");
    js_expect("null && \"d\"", "null");
    js_expect("null?.x", "undefined"); // optional chaining short-circuits

    // --- in arrays and JSON --------------------------------------------------
    // Unlike undefined, null SURVIVES serialisation - it is a real JSON value.
    js_expect("[null].length", "1");
    js_expect("String([null])", ""); // but still joins as empty
    js_expect("JSON.stringify(null)", "null");
    js_expect("JSON.stringify([null])", "[null]");
    js_expect("JSON.stringify({a:null})", "{\"a\":null}");
    js_expect("({a:null}).a", "null");

    REPORT("null_basics");
}
