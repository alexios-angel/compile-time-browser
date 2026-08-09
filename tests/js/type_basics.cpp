// JavaScript's type system, against V8.
//
// `typeof`, the four abstract conversions (ToBoolean, ToNumber, ToString,
// ToPrimitive), the `+` operator's double life, and the `==` coercion table -
// differentially tested against node (V8, the engine Chrome ships) over ~370
// expressions. As with tests/math_basics.cpp and tests/string_basics.cpp the
// expectations came out of V8, not out of this engine.
//
// The sweep found 19 differences, and ELEVEN OF THEM WERE ONE DEFECT: ToNumber
// of an OBJECT did not go through ToPrimitive, so `Number([])` was NaN rather
// than 0 and every `==` between an object and a primitive answered false.
// Fixed - the built-ins use `to_number_value` now - and pinned below. Three
// differences remain and are labelled at the bottom.
//
// The coercion tables here are the part of the language that pages rely on
// without meaning to: `if (x)`, `x + ""`, `x == null` and `+x` are in every
// bundle, and each is a different conversion with different rules.

#include "js_expect.hpp"

int main() {
    // --- typeof --------------------------------------------------------------
    // `typeof null` is "object" and always will be - it is the oldest bug in
    // the language and pages branch on it.
    js_expect("typeof undefined", "undefined");
    js_expect("typeof null", "object");
    js_expect("typeof true", "boolean");
    js_expect("typeof 0", "number");
    js_expect("typeof NaN", "number");
    js_expect("typeof Infinity", "number");
    js_expect("typeof \"\"", "string");
    js_expect("typeof []", "object");
    js_expect("typeof ({})", "object");
    js_expect("typeof (function(){})", "function");
    js_expect("typeof Symbol(\"s\")", "symbol");
    // typeof is the ONE operator that does not throw on an undeclared name -
    // which is why `typeof x === "undefined"` is the guard every bundle uses.
    js_expect("typeof undeclaredThing", "undefined");
    js_expect("typeof typeof 1", "string");
    js_expect("typeof void 0", "undefined");

    // --- ToBoolean -----------------------------------------------------------
    // Exactly seven falsy values, and everything else is true - including "0",
    // "false", [] and {}, which is what surprises people.
    for (const char * falsy : {"undefined", "null", "false", "0", "-0", "NaN", "\"\""}) {
        js_expect(std::string{"Boolean("} + falsy + ")", "false");
        js_expect(std::string{"!!"} + falsy, "false");
    }
    for (const char * truthy :
         {"true", "1", "-1", "\"0\"", "\"false\"", "[]", "({})", "Infinity"}) {
        js_expect(std::string{"Boolean("} + truthy + ")", "true");
    }

    // --- ToNumber ------------------------------------------------------------
    js_expect("Number(undefined)", "NaN");
    js_expect("Number(null)", "0"); // null is 0, undefined is NaN - they differ here
    js_expect("Number(true)", "1");
    js_expect("Number(false)", "0");
    js_expect("Number(\"\")", "0");
    js_expect("Number(\" \")", "0");
    js_expect("Number(\"1\")", "1");
    js_expect("Number(\"1a\")", "NaN");
    js_expect("+\"2\"", "2");
    js_expect("+true", "1");
    js_expect("+null", "0");
    js_expect("+undefined", "NaN");

    // --- ToString ------------------------------------------------------------
    js_expect("String(undefined)", "undefined");
    js_expect("String(null)", "null");
    js_expect("String(true)", "true");
    js_expect("String(0)", "0");
    js_expect("String(-0)", "0"); // the sign is not shown
    js_expect("String(NaN)", "NaN");
    js_expect("String([])", "");
    js_expect("String([1])", "1");
    js_expect("String([1,2])", "1,2");
    js_expect("String([1,[2,[3]]])", "1,2,3"); // join is recursive
    js_expect("String([null])", "");           // null and undefined join as empty
    js_expect("String([undefined])", "");
    js_expect("String({})", "[object Object]");

    // --- the + operator, which is two operators -----------------------------
    // If either side is a string after ToPrimitive it CONCATENATES; otherwise
    // it adds. That single rule explains every surprising line below.
    js_expect("1 + 1", "2");
    js_expect("1 + \"1\"", "11");
    js_expect("\"1\" + 1", "11");
    js_expect("1 + null", "1");
    js_expect("1 + undefined", "NaN");
    js_expect("1 + true", "2");
    js_expect("true + true", "2");
    js_expect("\"\" + null", "null");
    js_expect("\"\" + undefined", "undefined");
    js_expect("[] + []", "");
    js_expect("[1] + [2]", "12"); // both join to strings, so this concatenates
    // The other arithmetic operators have no string mode, so they coerce.
    js_expect("\"3\" - 1", "2");
    js_expect("\"3\" * \"2\"", "6");
    js_expect("\"6\" / \"2\"", "3");
    js_expect("1 + +\"2\"", "3"); // the unary + a minifier writes to force a number

    // --- == against === ------------------------------------------------------
    // `==` coerces and `===` does not. The rows below are the ones bundles
    // actually depend on.
    js_expect("null == undefined", "true");
    js_expect("null === undefined", "false");
    js_expect("null == 0", "false"); // null is ONLY loosely equal to undefined
    js_expect("null == false", "false");
    js_expect("undefined == 0", "false");
    js_expect("0 == \"\"", "true");
    js_expect("0 == \"0\"", "true");
    js_expect("\"\" == \"0\"", "false"); // two strings never coerce
    js_expect("0 == false", "true");
    js_expect("1 == true", "true");
    js_expect("2 == true", "false");
    js_expect("\"1\" == 1", "true");
    js_expect("NaN == NaN", "false");
    js_expect("NaN === NaN", "false");
    js_expect("0 === -0", "true"); // === cannot tell the zeros apart
    js_expect("1 === 1", "true");
    js_expect("\"a\" === \"a\"", "true");

    // --- numbers -------------------------------------------------------------
    js_expect("1/0", "Infinity");
    js_expect("-1/0", "-Infinity");
    js_expect("0/0", "NaN");
    js_expect("NaN !== NaN", "true");
    js_expect("0.1 + 0.2 === 0.3", "false"); // binary floating point, as everywhere
    js_expect("Number.isInteger(1)", "true");
    js_expect("Number.isInteger(1.5)", "false");
    js_expect("Number.isNaN(NaN)", "true");
    js_expect("Number.isNaN(\"NaN\")", "false"); // isNaN COERCES, Number.isNaN does not
    js_expect("isNaN(\"NaN\")", "true");
    js_expect("Number.MAX_SAFE_INTEGER", "9007199254740991");
    js_expect("Number.EPSILON > 0", "true");
    js_expect("parseInt(\"08\")", "8"); // not octal, despite the leading zero
    js_expect("parseInt(\"10\", 2)", "2");
    js_expect("parseFloat(\".5\")", "0.5");

    // --- null, undefined and the nullish operators ---------------------------
    js_expect("null ?? \"d\"", "d");
    js_expect("undefined ?? \"d\"", "d");
    js_expect("0 ?? \"d\"", "0");   // ?? only catches null and undefined...
    js_expect("\"\" ?? \"d\"", ""); // ...unlike ||, which catches every falsy value
    js_expect("0 || \"d\"", "d");
    js_expect("\"\" || \"d\"", "d");
    js_expect("1 && 2", "2");
    js_expect("0 && 2", "0");
    js_expect("void 0 === undefined", "true");

    // --- objects, prototypes, identity ---------------------------------------
    js_expect("[] instanceof Array", "true");
    js_expect("({}) instanceof Object", "true");
    js_expect("(function(){}) instanceof Function", "true");
    js_expect("\"a\" instanceof String", "false"); // a primitive is not an instance
    js_expect("Array.isArray([])", "true");
    js_expect("Array.isArray({})", "false");
    js_expect("[].constructor === Array", "true");
    js_expect("(1).constructor === Number", "true");
    js_expect("typeof \"\".constructor", "function");
    js_expect("({a:1}).a", "1");
    js_expect("({a:1}).b", "undefined");
    js_expect("\"a\" in {a:1}", "true");
    js_expect("\"b\" in {a:1}", "false");
    js_expect("(function(){var o={a:1};delete o.a;return o.a})()", "undefined");

    // --- ToPrimitive on objects that define it -------------------------------
    // valueOf is tried first for a numeric hint, toString for a string one.
    js_expect("({valueOf:function(){return 42}}) + 1", "43");
    js_expect("({toString:function(){return \"x\"}}) + 1", "x1");
    js_expect("String({toString:function(){return \"T\"}})", "T");
    js_expect("(new Date(0)) instanceof Date", "true");

    // --- ToNumber of an object, which goes through ToPrimitive ---------------
    // These were the sweep's biggest cluster: `context::to_number` is static
    // and cannot re-enter the VM to call `valueOf`, so it answered NaN for
    // every object - which made `Number([])` NaN and every `==` between an
    // object and a primitive false. The built-ins use `to_number_value` now.
    js_expect("Number([])", "0");
    js_expect("Number([1])", "1");
    js_expect("Number({valueOf:function(){return 7}})", "7");
    js_expect("Math.abs([])", "0");
    js_expect("Math.abs([-2.5])", "2.5");
    js_expect("Math.max([1],[2])", "2");
    // And `==` retries after ToPrimitive - 7.2.15 steps 10 and 11.
    js_expect("0 == []", "true");
    js_expect("\"\" == []", "true");
    js_expect("false == []", "true");
    js_expect("1 == [1]", "true");
    js_expect("\"1\" == [1]", "true");
    js_expect("\"abc\" == [\"abc\"]", "true");
    // Two OBJECTS still compare by identity, and a string is on the heap here
    // too - which is why "both on the heap" was the wrong test for that.
    js_expect("[] == []", "false");
    js_expect("({}) == ({})", "false");
    js_expect("null == []", "false");
    js_expect("0 == {}", "false");
    js_expect("NaN == [NaN]", "false");
    // A leading 0x is hexadecimal when no radix is given, 19.2.5 step 8.
    js_expect("parseInt(\"0x10\")", "16");
    js_expect("parseInt(\"0xFF\")", "255");
    js_expect("parseInt(\"-0x10\")", "-16");
    js_expect("parseInt(\"0x10\", 10)", "0"); // an explicit radix wins

    // --- Object.is: === plus the two questions it cannot answer ---------------
    js_expect("typeof Object.is", "function");
    js_expect("Object.is(0,-0)", "false");   // === says true
    js_expect("Object.is(NaN,NaN)", "true"); // === says false
    // And String(sym) describes rather than exposing the internal key.
    js_expect("String(Symbol(\"s\"))", "Symbol(s)");

    // --- KNOWN WRONG, pinned so a fix trips over it ---------------------------
    js_expect("String((function(){}))", "function"); // V8: the source text

    REPORT("type_basics");
}
