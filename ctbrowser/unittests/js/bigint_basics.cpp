//===- ctbrowser/unittests/js/bigint_basics.cpp - BigInt against V8 -------===//
//
// The `bigint` type, with expected values taken from node (V8): arithmetic,
// comparison, conversion and every error the specification names.
//
// The point of the type is exactness past 2^53, where a double starts rounding
// (an id, a nanosecond timestamp, a 64-bit hash). So the assertions that
// matter most are the refusals: a BigInt never silently becomes a Number,
// because that would lose exactly what it was reached for.
//
// Represented by `boost::multiprecision::cpp_int` in `bigint_object`. Bare
// section numbers are ECMA-262.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

#include <ctbrowser/script/bigint.hpp>

int main() {
    // --- literals, in every radix --------------------------------------------
    js_expect("1n", "1");
    js_expect("0n", "0");
    js_expect("typeof 1n", "bigint");
    js_expect("0xFFn", "255");
    js_expect("0b101n", "5");
    js_expect("0o17n", "15");
    js_expect("1_000n", "1000");
    // The reason the type exists: this integer is not representable as a
    // double, and the Number beside it rounds to something else.
    js_expect("9007199254740993n", "9007199254740993");
    js_expect("9007199254740993", "9007199254740992"); // the Number, rounded
    js_expect("9007199254740993n + 1n", "9007199254740994");

    // --- arithmetic ------------------------------------------------------------
    js_expect("1n + 2n", "3");
    js_expect("10n - 3n", "7");
    js_expect("6n * 7n", "42");
    js_expect("7n / 2n", "3"); // truncates toward zero; there are no fractions
    js_expect("-7n / 2n", "-3");
    js_expect("7n % 3n", "1");
    js_expect("-7n % 3n", "-1"); // the sign follows the dividend, as for Number
    js_expect("2n ** 10n", "1024");
    js_expect("2n ** 64n", "18446744073709551616"); // past what a double can hold
    js_expect("2n ** 100n", "1267650600228229401496703205376");
    js_expect("-5n", "-5");
    js_expect("-(-5n)", "5");

    // --- bitwise, on an unbounded two's-complement value ----------------------
    js_expect("~5n", "-6");
    js_expect("5n & 3n", "1");
    js_expect("5n | 3n", "7");
    js_expect("5n ^ 3n", "6");
    js_expect("1n << 10n", "1024");
    js_expect("1024n >> 3n", "128");

    // --- comparison does cross to Number, and exactly -------------------------
    // Arithmetic between the two types is refused, but comparison is not, and
    // the comparison is exact rather than going through a double, or the two
    // values below would compare equal.
    js_expect("1n == 1", "true");
    js_expect("1n === 1", "false"); // === never crosses types
    js_expect("1n != 1", "false");
    js_expect("0n == 0", "true");
    js_expect("0n === 0", "false");
    js_expect("1n < 2", "true");
    js_expect("1n < 1.5", "true");
    js_expect("2n > 1.5", "true");
    js_expect("1n <= 1", "true");
    js_expect("1n >= 2", "false");
    js_expect("2n > 1n", "true");
    js_expect("1n < NaN", "false"); // as every comparison against NaN is
    js_expect("9007199254740993n == 9007199254740992", "false"); // NOT rounded together
    js_expect("1n == \"1\"", "true");                            // a string converts to BigInt here
    js_expect("1n === 1n", "true");                              // by VALUE, not by allocation
    js_expect("2n === 1n", "false");

    // Loose equality converts Booleans to Number and objects to primitives.
    js_expect("[0n == false, false == 0n, 1n == true, true == 1n,"
              " 2n != true, true != 2n, 0n == null, 0n == undefined].join(',')",
              "true,true,true,true,true,true,false,false");
    js_expect("[Object(1n) == 1n, 1n == Object(1n), Object(1n) != 2n,"
              " 2n != Object(1n), Object(1n) == Object(1n)].join(',')",
              "true,true,true,true,false");
    js_expect(R"JS((function() {
        var calls = '';
        var object = {valueOf() { calls += 'v'; return {}; },
                      toString() { calls += 's'; return '1'; }};
        return [object == 1n, 1n != object, calls].join(',');
    })())JS",
              "true,false,vsvs");
    js_expect(R"JS((function() {
        var marker = {};
        var object = {valueOf() { throw marker; }};
        try { return 1n == object; } catch (e) { return e === marker; }
    })())JS",
              "true");
    js_expect(R"JS((function() {
        var object = {valueOf() { return {}; }, toString() { return {}; }};
        try { return object != 1n; } catch (e) { return e.name; }
    })())JS",
              "TypeError");

    // --- conversion ------------------------------------------------------------
    js_expect("String(1n)", "1"); // the digits, with no trailing `n`
    js_expect("`${5n}`", "5");
    js_expect("1n + \"a\"", "1a"); // concatenation is allowed; addition is not
    js_expect("\"a\" + 1n", "a1");
    js_expect("[1n, 2n].join(\",\")", "1,2");
    js_expect("!!0n", "false"); // 0n is the only falsy bigint
    js_expect("!!1n", "true");
    js_expect("(255n).toString(16)", "ff");
    js_expect("(255n).toString(2)", "11111111");
    js_expect("BigInt(5)", "5");
    js_expect("BigInt(\"42\")", "42");
    js_expect("BigInt(\"0x10\")", "16");
    js_expect("BigInt('  +0010  ')", "10");
    js_expect("BigInt('-0010')", "-10");
    js_expect("BigInt('0o10')", "8");
    js_expect("BigInt('0b10')", "2");
    js_expect("BigInt(true)", "1");
    js_expect("typeof BigInt(5)", "bigint");
    js_expect("typeof BigInt", "function");
    js_expect("Number(1n)", "1"); // the EXPLICIT conversion, which is permitted

    // Constructor coercion uses the number hint exactly once, before ToBigInt.
    js_expect("BigInt(Object(42))", "42");
    js_expect("BigInt(Object(42n))", "42");
    js_expect("BigInt.call({}, 42)", "42");
    js_expect(R"JS((function() {
        var calls = 0, hint;
        var result = BigInt({[Symbol.toPrimitive](h) { ++calls; hint = h; return '42'; }});
        return [result, calls, hint].join(',');
    })())JS",
              "42,1,number");
    js_expect(R"JS((function() {
        var calls = '';
        var result = BigInt({valueOf() { calls += 'v'; return {}; },
                             toString() { calls += 's'; return '42'; }});
        return result + ':' + calls;
    })())JS",
              "42:vs");
    js_expect(R"JS((function() {
        var marker = {};
        try { BigInt({valueOf() { throw marker; }, toString() { throw 1; }}); }
        catch (e) { return e === marker; }
    })())JS",
              "true");
    js_expect(R"JS((function() {
        var calls = 0;
        try { BigInt({valueOf() { ++calls; return Infinity; }}); }
        catch (e) { return e.name + ':' + calls; }
    })())JS",
              "RangeError:1");
    js_expect("(255n).toString(undefined)", "255");
    js_expect("(255n).toString(16.9)", "ff");
    js_expect("(255n).toString({valueOf() { return 16; }})", "ff");
    js_expect(R"JS((function() {
        var marker = {};
        try { (1n).toString({valueOf() { throw marker; }}); }
        catch (e) { return e === marker; }
    })())JS",
              "true");
    js_expect(R"JS((function() {
        var calls = 0;
        try { BigInt.prototype.toString.call(1, {valueOf() { ++calls; return 16; }}); }
        catch (e) { return e.name + ':' + calls; }
    })())JS",
              "TypeError:0");

    // --- the refusals, which are the safety property --------------------------
    // Mixing a BigInt with a Number in arithmetic is a TypeError. An engine
    // that coerced instead would round at exactly the point the type was
    // reached for, so the throw is the feature.
    const auto throws = [](const char * expression, const char * error) {
        js_expect(std::string{"(function(){try{return "} + expression +
                      "}catch(e){return \"THROWS \"+e.name}})()",
                  std::string{"THROWS "} + error);
    };
    throws("1n+1", "TypeError");
    throws("1+1n", "TypeError");
    throws("1n-2", "TypeError");
    throws("1n*2", "TypeError");
    throws("1n/2", "TypeError");
    // Unary plus is ToNumber, so it is refused too - `+1n` does not convert.
    throws("+1n", "TypeError");
    // Anything that reaches ToNumber implicitly refuses the same way.
    throws("Math.abs(1n)", "TypeError");
    // There is no lossless JSON spelling for a BigInt, so it throws rather than
    // picking between a string and a rounded number.
    throws("JSON.stringify(1n)", "TypeError");
    // `>>>` needs a width to fill from and a BigInt has none.
    throws("1n>>>1n", "TypeError");
    // No BigInt infinity, so division by zero has no value to give.
    throws("1n/0n", "RangeError");
    throws("1n%0n", "RangeError");
    // No fractions, so a negative exponent has none either.
    throws("2n**-1n", "RangeError");
    // Denial-of-service guard: a result past 2^30 bits is refused from the
    // operands' widths before any of it is computed; bases 0, 1 and -1 stay
    // small at any power.
    throws("(2n**100000n)**4000000000n", "RangeError");
    throws("3n**4294967295n", "RangeError");
    throws("3n**700000000n", "RangeError"); // 1.1 billion bits: past the cap, not just near it
    js_expect("1n ** 4294967295n", "1");
    js_expect("(-1n) ** 4294967295n", "-1");
    throws("(1n << 268435456n) << 268435456n << 268435456n << 268435456n << 268435456n",
           "RangeError");
    throws("(1n << 536870912n) * (1n << 536870912n)", "RangeError");
    // A size refusal says so; only a negative exponent says it is negative.
    const auto message = [](const char * expression, const char * text) {
        js_expect(std::string{"(function(){try{return "} + expression +
                      "}catch(e){return e.name+\": \"+e.message}})()",
                  std::string{"RangeError: "} + text);
    };
    message("2n**-1n", "Exponent must be non-negative");
    message("3n**700000000n", "Maximum BigInt size exceeded");
    message("1n << 1073741824n", "Maximum BigInt size exceeded");
    message("1n >> -1073741824n", "Maximum BigInt size exceeded");
    message("(1n << 536870912n) * (1n << 536870912n)", "Maximum BigInt size exceeded");
    {
        using namespace ctbrowser::script;
        const bigint wide = bigint{1} << 536870912;
        check(!bigint_mul(wide, wide), "a product past 2^30 bits is refused");
        check(bigint_mul(bigint{6}, bigint{7}) == bigint{42}, "and a small one is not");
    }
    // BigInt() refuses what it cannot represent rather than truncating.
    throws("BigInt(1.5)", "RangeError");
    throws("BigInt(NaN)", "RangeError");
    throws("BigInt(\"zz\")", "SyntaxError");
    // StringIntegerLiteral permits signs only on decimal digits, and no separators.
    for (const char * text :
         {"-0x1", "+0X1", "-0o1", "+0O1", "-0b1", "+0B1", "1_0", "0xF_F", "_1", "1_"}) {
        const std::string quoted = "'" + std::string{text} + "'";
        throws(("BigInt(" + quoted + ")").c_str(), "SyntaxError");
        throws(("BigInt.asIntN(8, " + quoted + ")").c_str(), "SyntaxError");
        js_expect("1n == " + quoted, "false");
    }
    throws("BigInt()", "TypeError");
    throws("BigInt(null)", "TypeError");
    throws("BigInt(Symbol())", "TypeError");
    throws("BigInt({valueOf() { return null; }})", "TypeError");
    throws("BigInt({valueOf() { return 1.5; }})", "RangeError");
    throws("BigInt({valueOf() { return 'zz'; }})", "SyntaxError");
    throws("BigInt({[Symbol.toPrimitive]() { return {}; }})", "TypeError");
    for (const char * radix : {"0", "1", "37", "NaN", "Infinity", "-Infinity", "null"}) {
        const std::string expression = "(1n).toString(" + std::string{radix} + ")";
        throws(expression.c_str(), "RangeError");
    }
    throws("(1n).toString(Symbol())", "TypeError");
    throws("(1n).toString(2n)", "TypeError");
    throws("(1n).toString({valueOf() { return Symbol(); }})", "TypeError");
    throws("(1n).toString({valueOf() { return 2n; }})", "TypeError");

    // 21.2.2.1-2 asIntN / asUintN: the value modulo 2^bits, two's complement
    // for the signed form; ToBigInt refuses a Number where BigInt() converts.
    js_expect("BigInt.asUintN(8, 257n)", "1");
    js_expect("BigInt.asUintN(8, -1n)", "255");
    js_expect("BigInt.asIntN(8, 255n)", "-1");
    js_expect("BigInt.asIntN(8, 128n)", "-128");
    js_expect("BigInt.asIntN(8, 127n)", "127");
    js_expect("BigInt.asIntN(64, 2n ** 63n)", "-9223372036854775808");
    js_expect("BigInt.asUintN(64, -1n)", "18446744073709551615");
    js_expect("BigInt.asUintN(0, 5n)", "0");
    js_expect("BigInt.asUintN(200, 1n)", "1");
    js_expect("BigInt.asIntN(3, 4n)", "-4");
    js_expect("BigInt.asIntN(8, \"255\")", "-1");
    js_expect("BigInt.asIntN(8, true)", "1");
    js_expect("BigInt.asIntN.length + BigInt.asUintN.length", "4");
    throws("BigInt.asIntN(8, 1)", "TypeError");
    throws("BigInt.asIntN(-1, 1n)", "RangeError");

    // Against a string the comparison is StringToBigInt's: exact, and false
    // every way round for a string that is no integer.
    js_expect("'9007199254740993' > 9007199254740992n", "true");
    js_expect("9007199254740992n < '9007199254740993'", "true");
    js_expect("(1n > '0.') + ',' + (1n <= '0.') + ',' + ('0.' < 1n)", "false,false,false");

    REPORT("bigint_basics");
}
