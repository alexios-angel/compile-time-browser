#pragma once
#include <string>
#include <string_view>

// NUMBERS AS JAVASCRIPT WRITES THEM, and reads them.
//
// Five functions, all of them ECMA-262 rather than C. The distinction is not
// pedantic - before this file the engine used `std::to_string(double)`, `%.*f`,
// `%.*e`, `%.*g` and `std::stod`, and every one of them is a different function
// from the one the specification names:
//
//   * `std::to_string(double)` is `%f` to six decimals, so `String(1/3)` was
//     "0.333333", `String(1e-7)` was "0" - as was every smaller number - and
//     `String(1.7976931348623157e308)` was 309 literal digits. The specification
//     asks for the SHORTEST digit string that reads back as the same double,
//     which is a different thing from "enough digits" and cannot be reached by
//     raising a precision.
//   * `%g` switches to exponential on PRECISION; JavaScript switches on the
//     decimal exponent, printing 1e20 as "100000000000000000000" and 1e-7 as
//     "1e-7". C also writes at least two exponent digits where JavaScript writes
//     the fewest that suffice - "1e+21", never "1e+021".
//   * `toFixed` rounds a tie AWAY FROM ZERO; correctly-rounded conversion, which
//     is what `std::to_chars` does, rounds a tie to even. They disagree on
//     `(0.5).toFixed(0)`, `(8.5).toFixed(0)`, `(0.25).toFixed(1)` and every
//     other value whose scaled form lands exactly halfway.
//
// AND ALL FIVE OF THE OLD ONES CONSULT THE LOCALE. `std::to_string`, `strtod`
// and the printf conversions read `LC_NUMERIC` for the decimal separator, and
// this repository byte-compares rendered output across Linux and the Windows
// cross-build. `std::to_chars`/`std::from_chars` are the locale-independent
// pair - which is the argument docs/build.md already makes for preferring
// `from_chars` to `strtod`, applied to the other direction as well.
//
// NOT Boost.Charconv, and it was considered: every floating-point `to_chars`
// overload including the format+precision forms is present in both toolchains
// this repository builds with - libstdc++ since GCC 11, and llvm-mingw's libc++
// exports all nine - so it would have cost a compiled dependency, a sixth
// cross-build script and a Boost floor raise for nothing the standard library
// does not already do.
//
// `tests/js/number_format.cpp` pins every one of these against V8, and the
// round-trip property against the engine itself.

namespace ctbrowser::script {

// ECMA-262 6.1.6.1.20, Number::toString(x, 10). The shortest round-tripping
// digits, then the specification's own rule for where the decimal point goes.
[[nodiscard]] std::string number_to_string(double value);

// 21.1.3.3. `digits` is 0..100 and the caller has already clamped it.
[[nodiscard]] std::string number_to_fixed(double value, int digits);

// 21.1.3.2. `places` < 0 means the argument was absent, which is not the same
// as zero: it asks for as many digits as uniquely specify the value.
[[nodiscard]] std::string number_to_exponential(double value, int places);

// 21.1.3.5. `digits` is 1..100.
[[nodiscard]] std::string number_to_precision(double value, int digits);

// ToNumber applied to a string (7.1.4.1). The WHOLE string must be a numeric
// literal - trailing garbage is NaN, not a prefix, which is what separates this
// from `parseFloat`. Understands leading/trailing whitespace, an empty string as
// zero, `Infinity`, and the 0x/0o/0b radix prefixes.
[[nodiscard]] double string_to_number(std::string_view text);

// WHAT A LITERAL `std::from_chars` REFUSED AS OUT OF RANGE ACTUALLY MEANS.
//
// It reports `result_out_of_range` and does NOT write the value, and the two
// directions need opposite answers: too big is +Infinity, too small is +0.
// `text` is the unsigned decimal literal; the caller applies the sign. Shared by
// the lexer and by ToNumber, which had the same hole and would otherwise grow
// two different answers to one question.
[[nodiscard]] double out_of_range_value(std::string_view text);

// `parseFloat` (19.2.4), which is the PREFIX form and deliberately not the one
// above: it reads as much as looks like a number and ignores the rest, so
// `parseFloat("3.14xyz")` is 3.14 where `Number("3.14xyz")` is NaN. NaN when
// nothing numeric starts the string.
[[nodiscard]] double string_to_number_prefix(std::string_view text);

} // namespace ctbrowser::script
