// `undefined`, against V8.
//
// The value that means "no value", and the one a page meets most often without
// asking for it: a missing argument, an absent property, an unwritten array
// slot, a function that returns nothing. Differentially tested against node
// (V8); 3 differences, pinned at the bottom.
//
// Its whole character is that it is DISTINCT FROM null while being loosely
// equal to it, and that it poisons arithmetic rather than defaulting to zero -
// which is exactly what `null` does not do. tests/null_basics.cpp is the other
// half of that comparison and the two are meant to be read together.

#include "js_expect.hpp"

int main() {
    // --- identity and typeof -------------------------------------------------
    js_expect("typeof undefined", "undefined");
    js_expect("undefined === void 0", "true"); // `void 0` is how minifiers spell it
    js_expect("typeof void 0", "undefined");
    js_expect("undefined === undefined", "true");

    // --- against null: loosely equal, strictly not ---------------------------
    js_expect("undefined == null", "true");
    js_expect("undefined === null", "false");
    js_expect("typeof null", "object"); // and their typeofs differ too

    // --- conversions ---------------------------------------------------------
    // ToNumber is NaN, NOT 0. That is the difference from null that turns a
    // missing property into NaN arithmetic rather than a quiet zero.
    js_expect("String(undefined)", "undefined");
    js_expect("Number(undefined)", "NaN");
    js_expect("Boolean(undefined)", "false");
    js_expect("!undefined", "true");
    js_expect("undefined + 1", "NaN");
    js_expect("undefined + \"\"", "undefined"); // string concatenation, not addition

    // --- where it arrives without being written -----------------------------
    js_expect("({}).missing", "undefined");
    js_expect("[1][5]", "undefined");
    js_expect("(function(){})()", "undefined");          // no return
    js_expect("(function(a){return a})()", "undefined"); // missing argument
    js_expect("(function(){var x;return typeof x})()", "undefined");
    js_expect("typeof undefinedGlobalName", "undefined"); // typeof never throws

    // --- the operators that single it out ------------------------------------
    js_expect("undefined ?? \"d\"", "d");
    js_expect("undefined || \"d\"", "d");
    js_expect("undefined && \"d\"", "undefined");
    js_expect("undefined?.x", "undefined");

    // --- comparisons ---------------------------------------------------------
    // Loosely equal to null and to NOTHING else - not to 0, "" or false, which
    // is where it parts company with null's numeric coercion.
    js_expect("undefined == 0", "false");
    js_expect("undefined == false", "false");
    js_expect("undefined == \"\"", "false");
    // Relational comparison goes through ToNumber, so NaN, so all false.
    js_expect("undefined < 1", "false");
    js_expect("undefined > 1", "false");

    // --- in arrays and JSON --------------------------------------------------
    js_expect("[undefined].length", "1");
    js_expect("String([undefined])", "");               // joins as empty
    js_expect("JSON.stringify([undefined])", "[null]"); // but serialises as null
    js_expect("JSON.stringify({a:undefined})", "{}");   // and a property vanishes

    // AT THE TOP LEVEL an unserialisable value yields undefined, not the string
    // "null" - the difference matters to a caller doing `if (j === undefined)`.
    // Inside an array the same value becomes null, which is why the caller and
    // not the writer decides.
    js_expect("JSON.stringify(undefined)", "undefined");
    js_expect("JSON.stringify(function(){})", "undefined");

    // --- KNOWN WRONG, pinned so a fix trips over them -------------------------
    // ARRAY HOLES. `[,1]` has no element at 0 - `in` says false and Object.keys
    // skips it - where this engine materialises every slot as undefined.
    js_expect("0 in [,1]", "true");             // V8: false
    js_expect("Object.keys([,1]).length", "0"); // V8: 1

    REPORT("undefined_basics");
}
