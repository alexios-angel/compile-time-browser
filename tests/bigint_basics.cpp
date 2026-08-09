// `bigint` - WHICH THIS ENGINE DOES NOT HAVE.
//
// Every assertion in this file records an ABSENCE. `1n` does not lex, `BigInt`
// is not a global, and there is no seventh primitive type. That is a legitimate
// thing for an engine to be, and an illegitimate thing for it to be QUIETLY:
// this file is what makes the gap countable, and it is the acceptance list for
// adding the type.
//
// The failure mode matters more than the absence. `1n` is a PARSE ERROR, so a
// bundle containing one anywhere fails to compile as a whole - not just the
// expression, the whole script - and the page goes blank. That is why bigint
// shows up in this suite at all: p5, Phaser and Babylon do not use it today,
// but any dependency that does would take the page down rather than degrade.
//
// V8's answer is in the comment on every line. When BigInt is implemented, this
// file should fail everywhere at once and be rewritten as a conformance suite.

#include "js_expect.hpp"

int main() {
    // --- the literal does not lex, so the whole script is refused -------------
    // "THREW" here is a COMPILE failure, not a runtime one. Nothing after it in
    // the same script would run either.
    js_expect("1n", "THREW");                // V8: 1
    js_expect("typeof 1n", "THREW");         // V8: "bigint"
    js_expect("9007199254740993n", "THREW"); // V8: 9007199254740993, exactly
    js_expect("1n + 1n", "THREW");           // V8: 2
    js_expect("2n ** 64n", "THREW");         // V8: 18446744073709551616
    js_expect("1n === 1n", "THREW");         // V8: true
    js_expect("1n == 1", "THREW");           // V8: true  - loose equality crosses types
    js_expect("1n === 1", "THREW");          // V8: false - strict does not
    js_expect("1n < 2", "THREW");            // V8: true  - relational does cross
    js_expect("2n > 1n", "THREW");           // V8: true

    // --- the constructor and its family are absent ---------------------------
    js_expect("typeof BigInt", "undefined");        // V8: "function"
    js_expect("BigInt(5)", "THREW");                // V8: 5
    js_expect("BigInt(\"5\")", "THREW");            // V8: 5
    js_expect("typeof BigInt(5)", "THREW");         // V8: "bigint"
    js_expect("String(1n)", "THREW");               // V8: "1"
    js_expect("Number(1n)", "THREW");               // V8: 1
    js_expect("typeof BigInt64Array", "undefined"); // V8: "function"

    // --- MIXING IS A TypeError, and that is the part most likely to surprise --
    // A bigint and a number cannot be added. An implementation that quietly
    // coerced instead would lose precision in exactly the case bigint exists to
    // protect, so the throw is the feature.
    js_expect("(function(){try{return 1n+1}catch(e){return \"THROWS \"+e.name}})()",
              "1"); // V8: THROWS TypeError - and note this engine answers 1,
                    // because `1n` fails to parse and the expression degrades

    // --- what the type is FOR, expressed in numbers this engine cannot hold ---
    // The reason to want it: a double loses integers above 2^53, so an id, a
    // nanosecond timestamp or a 64-bit hash silently rounds.
    js_expect("2**53 === 2**53+1", "true");            // the collision bigint avoids
    js_expect("9007199254740993", "9007199254740992"); // the literal cannot be held
    js_expect("typeof 9007199254740993", "number");

    REPORT("bigint_basics");
}
