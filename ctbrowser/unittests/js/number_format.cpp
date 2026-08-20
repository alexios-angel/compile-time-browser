// Numbers turning into text, and text turning back into numbers.
//
// EVERY EXPECTATION HERE CAME OUT OF V8 (node v26, the engine Chrome ships) and
// not out of this engine. That distinction is the whole point of the file: it
// was written against `Number::toString` as the specification defines it while
// `context::to_string` was still `std::to_string(double)`, which is `%f` to six
// decimals - so `String(1/3)` was "0.333333" and most of this file failed on the
// day it was committed. Expectations copied from the code under test would have
// pinned the bug instead.
//
// THE ROUND TRIP IS THE ASSERTION THAT MATTERS. ECMA-262 6.1.6.1.20 does not
// ask for "enough digits"; it asks for the SHORTEST digit string that reads back
// as the same double. `round_trips` below checks exactly that, which is a
// property rather than a table and so cannot be satisfied by tuning a precision
// until the listed cases pass.
//
// It is also a determinism test. `std::to_string`, `strtod` and `%f`/`%e`/`%g`
// all consult LC_NUMERIC, and this repository byte-compares rendered output
// across Linux and the Windows cross-build; a decimal comma would move a
// golden for a reason no diff would explain. `std::to_chars`/`from_chars` are
// the locale-independent pair, which is the same argument docs/build.md already
// makes for preferring `from_chars` to `strtod`.

#include "js_expect.hpp"

#include <string>
#include <string_view>

namespace {

// `Number(String(x)) === x`, asked of the engine in its own terms. A literal is
// not enough on its own - the LEXER has to read the constant correctly too, and
// this closes over both halves at once.
void round_trips(std::string_view literal) {
    // Through js_run, like every other assertion in this file. It costs one
    // repetition of the literal - this used to bind `let x` and compare against
    // it - and buys the guarantee that this suite renders a result exactly as
    // its neighbours do, which is the whole reason js_expect.hpp exists. Lexing
    // the constant twice tests the lexer at least as hard as lexing it once.
    const std::string l{literal};
    const std::string got = ctbrowser_test::js_run("Number(String(" + l + ")) === " + l +
                                                   " ? 'yes' : ('no: ' + String(" + l + "))");
    if (got != "yes") {
        std::printf("FAIL     %s does not round-trip => %s\n", std::string{literal}.c_str(),
                    got.c_str());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    // --- Number::toString, the default path ---------------------------------
    // Integral values print with no decimal point, and -0 prints as "0" (the
    // sign survives in the double and is deliberately not shown; Object.is is
    // what distinguishes them).
    js_expect("0", "0");
    js_expect("-0", "0");
    js_expect("1", "1");
    js_expect("-1", "-1");
    js_expect("100", "100");
    js_expect("9007199254740992", "9007199254740992");

    // Fractions, and THE case that `%f` to six decimals gets wrong.
    js_expect("0.5", "0.5");
    js_expect("-2.5", "-2.5");
    js_expect("1/3", "0.3333333333333333");
    js_expect("0.1 + 0.2", "0.30000000000000004");
    js_expect("4.35", "4.35");
    js_expect("1.005", "1.005");

    // WHERE THE NOTATION SWITCHES, which is where `%g` and JavaScript part
    // company. The specification's rule is on the DECIMAL EXPONENT n: plain
    // digits while -6 < n <= 21, exponential outside it. `%g` switches on
    // precision instead, so it would print 1e20 as "1e+20" and 0.000001 as
    // "1e-06" - both wrong here, and the second wrong twice over because
    // JavaScript writes no leading zero in an exponent.
    js_expect("1e20", "100000000000000000000");
    js_expect("1e21", "1e+21");
    js_expect("1.5e21", "1.5e+21");
    js_expect("0.000001", "0.000001");
    js_expect("0.0000001", "1e-7");
    js_expect("1.5e-7", "1.5e-7");
    js_expect("123456789012345678901234", "1.2345678901234569e+23");
    js_expect("1234567890123456789", "1234567890123456800");

    // The extremes, where "enough digits" and "the shortest that round-trips"
    // are furthest apart.
    js_expect("5e-324", "5e-324");
    js_expect("1e-323", "1e-323");
    js_expect("1.7976931348623157e308", "1.7976931348623157e+308");

    // The three that are not numbers.
    js_expect("0/0", "NaN");
    js_expect("1/0", "Infinity");
    js_expect("-1/0", "-Infinity");

    // --- the property, not the table ----------------------------------------
    for (const char * literal : {"0",
                                 "1",
                                 "-1",
                                 "0.5",
                                 "1/3",
                                 "0.1+0.2",
                                 "1e20",
                                 "1e21",
                                 "1e-7",
                                 "0.000001",
                                 "5e-324",
                                 "1.7976931348623157e308",
                                 "9007199254740992",
                                 "4.35",
                                 "1.005",
                                 "123456.789",
                                 "2.2250738585072014e-308",
                                 "0.30000000000000004",
                                 "-2.5e-13",
                                 "3.141592653589793"}) {
        round_trips(literal);
    }

    // --- toString(radix) ----------------------------------------------------
    // Not a to_chars question - the radix path is this engine's own long
    // division - but it shares `to_string` for radix 10 and for the non-finite
    // values, so it is pinned beside it.
    js_expect("(255).toString(16)", "ff");
    js_expect("(0.5).toString(2)", "0.1");
    js_expect("(255).toString(10)", "255");
    js_expect("(1/0).toString(16)", "Infinity");

    // --- toFixed ------------------------------------------------------------
    // 1.005 is the classic: the double nearest 1.005 is slightly BELOW it, so
    // the correctly-rounded answer is "1.00" and not "1.01". Same for 1.45.
    js_expect("(1.005).toFixed(2)", "1.00");
    js_expect("(1.45).toFixed(1)", "1.4");
    js_expect("(0).toFixed(2)", "0.00");
    js_expect("(-1.5).toFixed(0)", "-2");
    js_expect("(2.5).toFixed(0)", "3");
    // Past 1e21 toFixed hands back ToString, exponent and all.
    js_expect("(1e21).toFixed(2)", "1e+21");

    // --- toExponential ------------------------------------------------------
    // One exponent digit, not C's minimum of two: "7.71e+1", never "7.71e+01".
    js_expect("(77.1234).toExponential(2)", "7.71e+1");
    js_expect("(0).toExponential(1)", "0.0e+0");
    js_expect("(5).toExponential()", "5e+0");
    js_expect("(1234.5).toExponential(3)", "1.234e+3");

    // --- toPrecision --------------------------------------------------------
    js_expect("(1234.5).toPrecision(2)", "1.2e+3");
    js_expect("(0.000123).toPrecision(2)", "0.00012");
    js_expect("(123456).toPrecision(3)", "1.23e+5");
    js_expect("(1.5).toPrecision(3)", "1.50");

    // --- the way back: ToNumber on a string ---------------------------------
    // Trailing garbage is NaN rather than a prefix - that is ToNumber, and it is
    // what separates it from parseFloat below. An empty or all-whitespace string
    // is 0, which surprises people and is nonetheless the specification.
    js_expect("Number(' 12 ')", "12");
    js_expect("Number('abc')", "NaN");
    js_expect("Number('')", "0");
    js_expect("Number('   ')", "0");
    js_expect("Number('12abc')", "NaN");
    js_expect("Number('1e3')", "1000");
    js_expect("Number('0x1f')", "31");
    js_expect("Number('Infinity')", "Infinity");
    js_expect("Number('-Infinity')", "-Infinity");
    js_expect("Number('.5')", "0.5");
    js_expect("Number('5.')", "5");
    js_expect("parseFloat('3.14xyz')", "3.14");
    js_expect("parseFloat('abc')", "NaN");

    // A DECIMAL POINT IS A POINT, in every locale. `strtod` and `%f` consult
    // LC_NUMERIC and would answer differently under, say, de_DE - which is how
    // a byte-compared golden starts depending on the machine that rendered it.
    // Nothing in this file may go through either.
    js_expect("Number('1.5') + 1", "2.5");

    REPORT("number_format");
}
