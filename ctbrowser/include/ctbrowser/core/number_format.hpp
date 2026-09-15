#pragma once
#include <string>
#include <string_view>

// NUMBERS AS JAVASCRIPT WRITES THEM, and reads them - ECMA-262, not C. The
// C conversions (`%g`, `std::to_string`, `strtod`) differ from the
// specification on the exponential threshold, on tie rounding in `toFixed`,
// and on the locale: they read `LC_NUMERIC`, and this repository byte-compares
// rendered output across Linux and Windows. Built on the locale-independent
// `std::to_chars`/`std::from_chars`, which both toolchains provide in full, so
// Boost.Charconv would add a compiled dependency for nothing.
//
// `unittests/js/number_format.cpp` pins every one of these against V8.

namespace ctbrowser {

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

// The StringToNumber/StringToBigInt whitespace set, returned as a borrowed view.
[[nodiscard]] std::string_view trim_js_space(std::string_view text);

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

} // namespace ctbrowser
