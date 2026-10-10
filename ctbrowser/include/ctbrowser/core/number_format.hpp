//===- number_format.hpp - JS number formatting and parsing --- -*- C++ -*-===//
//
// Numbers as JavaScript writes and reads them (ECMA-262, not C). The C
// conversions (`%g`, `std::to_string`, `strtod`) differ from the specification
// on the exponential threshold and on tie rounding in `toFixed`, and they read
// `LC_NUMERIC`, while rendered output is byte-compared across Linux and
// Windows. Built on the locale-independent `std::to_chars`/`std::from_chars`,
// which both toolchains provide in full, so Boost.Charconv is not needed.
//
// `unittests/js/number/format.cpp` checks every one of these against V8.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <string>
#include <string_view>

namespace ctbrowser {

/// ECMA-262 6.1.6.1.20, Number::toString(x, 10): the shortest round-tripping
/// digits, then the specification's rule for where the decimal point goes.
[[nodiscard]] std::string number_to_string(double value);

/// Number.prototype.toFixed (ECMA-262 21.1.3.3). `digits` is 0..100; the
/// caller has already clamped it.
[[nodiscard]] std::string number_to_fixed(double value, int digits);

/// Number.prototype.toExponential (ECMA-262 21.1.3.2). `places` < 0 means the
/// argument was absent, which is not the same as zero: it asks for as many
/// digits as uniquely specify the value.
[[nodiscard]] std::string number_to_exponential(double value, int places);

/// Number.prototype.toPrecision (ECMA-262 21.1.3.5). `digits` is 1..100.
[[nodiscard]] std::string number_to_precision(double value, int digits);

/// `text` with JS whitespace (the StringToNumber/StringToBigInt set, also
/// TrimString's, ECMA-262 22.1.3.32.1) trimmed, as a view into `text`. `start`
/// and `end` pick the ends, for trimStart, trimEnd and parseInt. A malformed or
/// overlong byte is never a space, and U+180E stopped being one in Unicode 6.3
/// (test262 trim/u180e.js).
[[nodiscard]] std::string_view trim_js_space(std::string_view text, bool start = true,
                                             bool end = true);

/// ToNumber applied to a string (ECMA-262 7.1.4.1). The whole string must be a
/// numeric literal: trailing garbage gives NaN, unlike `parseFloat`. Accepts
/// leading/trailing whitespace, an empty string as zero, `Infinity`, and the
/// 0x/0o/0b radix prefixes.
[[nodiscard]] double string_to_number(std::string_view text);

/// The value of a decimal literal that `std::from_chars` refused as out of
/// range. from_chars reports `result_out_of_range` without writing a value, and
/// the two directions need opposite answers: too big is +Infinity, too small is
/// +0. `text` is the unsigned literal; the caller applies the sign. Shared by
/// the lexer and ToNumber so they agree.
[[nodiscard]] double out_of_range_value(std::string_view text);

/// `parseFloat` (ECMA-262 19.2.4), the prefix form: it reads as much as looks
/// like a number and ignores the rest, so `parseFloat("3.14xyz")` is 3.14 where
/// `Number("3.14xyz")` is NaN. Returns NaN when nothing numeric starts the
/// string.
[[nodiscard]] double string_to_number_prefix(std::string_view text);

} // namespace ctbrowser
