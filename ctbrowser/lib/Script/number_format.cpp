#include <ctbrowser/script/number_format.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <system_error>

#include <ctbrowser/core/algorithms.hpp>

// The header says which specification clause each function is and why C's
// conversions are not it. This says how.

namespace ctbrowser::script {

namespace {

// A double's shortest round-tripping decimal, split the way the specification
// talks about it: `digits` are the significant digits with no point, and `n` is
// the position of the decimal point, so the value is 0.<digits> x 10^n.
//
// `chars_format::scientific` rather than `general` on purpose - general applies
// C's rule for when to go exponential, which is the rule that is wrong here.
// Asked with no precision it produces the SHORTEST form that round-trips, which
// is exactly the digit string 6.1.6.1.20 wants; the exponent is then read back
// out and every notation decision is made below rather than by the library.
struct decimal {
    std::array<char, 32> digits{};
    int count = 0; // significant digits in `digits`
    int n = 0;     // value == 0.<digits> * 10^n
};

// The exponent out of a to_chars scientific result, which is written with a
// SIGN ALWAYS - "1e+20". `std::from_chars` for an integer does not accept a
// leading '+', so reading it directly returns 0 and leaves the position of the
// decimal point silently wrong for every value with a non-zero exponent.
[[nodiscard]] int read_exponent(const char * at, const char * stop) {
    if (at != stop && *at == '+') { ++at; }
    int exponent = 0;
    std::from_chars(at, stop, exponent);
    return exponent;
}

// Split "d.ddde[+-]XX" into its digits and its exponent. Shared by the three
// paths that ask to_chars for a scientific form.
void split_scientific(const char * first, const char * stop, std::string & digits, int & exponent) {
    const char * at = first;
    for (; at != stop && *at != 'e'; ++at) {
        if (*at != '.') { digits += *at; }
    }
    exponent = at == stop ? 0 : read_exponent(at + 1, stop);
}

[[nodiscard]] decimal shortest(double magnitude) {
    std::array<char, 64> buffer{};
    const auto [stop, err] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), magnitude,
                                           std::chars_format::scientific);
    decimal out;
    if (err != std::errc{}) { return out; }
    // "d.dddde[+-]XX" - or "de[+-]XX" when one digit suffices.
    std::string digits;
    int exponent = 0;
    split_scientific(buffer.data(), stop, digits, exponent);
    out.count = static_cast<int>(std::min(digits.size(), out.digits.size()));
    std::copy_n(digits.begin(), out.count, out.digits.begin());
    // to_chars writes d.ddd x 10^exponent; the specification counts from the
    // other side of the leading digit, so n is one more.
    out.n = exponent + 1;
    return out;
}

// "e+21", "e-7" - a sign always, and the fewest digits that say it. C writes at
// least two, which is where "1e+021" came from.
void append_exponent(std::string & text, int exponent) {
    text += 'e';
    text += exponent < 0 ? '-' : '+';
    std::array<char, 16> buffer{};
    const auto [stop, err] =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), std::abs(exponent));
    if (err == std::errc{}) { text.append(buffer.data(), stop); }
}

// The three cases that are not a number at all, shared by every entry point.
// Returns false when `value` is finite and the caller should go on.
[[nodiscard]] bool non_finite(double value, std::string & out) {
    if (std::isnan(value)) {
        out = "NaN";
        return true;
    }
    if (std::isinf(value)) {
        out = value > 0 ? "Infinity" : "-Infinity";
        return true;
    }
    return false;
}

// IS `magnitude * 10^places` EXACTLY A HALF-INTEGER?
//
// This is the whole of the disagreement between `toFixed` and correctly-rounded
// conversion. The specification picks the LARGER n when two are equally close,
// so a tie rounds away from zero; `std::to_chars` is correctly rounded, which
// means a tie goes to even. They differ on `(0.5).toFixed(0)` ("1" against
// "0"), `(8.5).toFixed(0)` ("9" against "8"), `(0.25).toFixed(1)` ("0.3"
// against "0.2") and every other exactly-halfway value.
//
// It is decidable exactly, and cheaply. Write the magnitude as m * 2^e with m
// ODD - every finite double has exactly one such form. Then
//
//     magnitude * 10^places == m * 5^places * 2^(e + places)
//
// and 5^places is odd, so the product is an integer when e + places >= 0 and a
// half-integer when e + places == -1, and has a smaller fractional part
// otherwise. So the tie is precisely `e + places == -1`, with no arithmetic on
// the scaled value and therefore no rounding error of its own.
[[nodiscard]] bool halfway_exactly(double magnitude, int places) {
    if (magnitude == 0 || !std::isfinite(magnitude)) { return false; }
    int exponent = 0;
    const double fraction = std::frexp(magnitude, &exponent); // magnitude = fraction * 2^exponent
    auto mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
    if (mantissa == 0) { return false; }
    int e = exponent - 53;
    while ((mantissa & 1U) == 0) { // reduce until the mantissa is odd
        mantissa >>= 1U;
        ++e;
    }
    return e + places == -1;
}

// A decimal digit string, rounded up one place. "1249" -> "125", and "999" ->
// "100" with a carry the caller has to account for by bumping the exponent.
// Returns whether it overflowed into a new leading digit.
[[nodiscard]] bool round_up_in_place(std::string & digits) {
    for (std::size_t at = digits.size(); at-- > 0;) {
        if (digits[at] != '9') {
            ++digits[at];
            return false;
        }
        digits[at] = '0';
    }
    digits.insert(digits.begin(), '1');
    return true;
}

// The magnitude to `places` decimals, as a plain digit string with a point.
// Correctly rounded by to_chars, then nudged when the value sits exactly
// halfway - see halfway_exactly.
[[nodiscard]] std::string fixed_digits(double magnitude, int places) {
    std::array<char, 1200> buffer{}; // 1e308 with 100 decimals still fits
    char * const end = buffer.data() + buffer.size();

    if (halfway_exactly(magnitude, places)) {
        // EXACTLY HALFWAY, so the specification's answer is the larger one and
        // to_chars cannot give it: correct rounding takes a tie to EVEN, which
        // is the right answer only half the time and cannot be recovered from
        // the rounded digits (after rounding to even the last digit is always
        // even, whichever way it went).
        //
        // At places+1 digits the value is exact - that is what halfway_exactly
        // established - and ends in '5'. So drop that digit and carry, which is
        // "away from zero" by construction.
        const auto [stop, err] =
            std::to_chars(buffer.data(), end, magnitude, std::chars_format::fixed, places + 1);
        if (err == std::errc{}) {
            std::string body{buffer.data(), stop};
            const std::size_t point = body.find('.');
            if (point != std::string::npos) { body.erase(point, 1); }
            body.pop_back(); // the exact trailing '5'
            if (body.empty()) { body = "0"; }
            (void)round_up_in_place(body);
            if (places > 0) {
                // Enough leading zeros that the point has digits on both sides:
                // 0.25 at one place reaches here as "03".
                while (body.size() <= static_cast<std::size_t>(places)) {
                    body.insert(body.begin(), '0');
                }
                body.insert(body.size() - static_cast<std::size_t>(places), 1, '.');
            }
            return body;
        }
    }

    const auto [stop, err] =
        std::to_chars(buffer.data(), end, magnitude, std::chars_format::fixed, places);
    if (err != std::errc{}) { return "0"; }
    return std::string{buffer.data(), stop};
}

} // namespace

// --- Number::toString ------------------------------------------------------
//
// 6.1.6.1.20 steps 5-10, which are a rule about the decimal exponent n and
// nothing else: plain digits while -6 < n <= 21, exponential outside it. That
// is why 1e20 prints as twenty-one digits and 1e21 does not, and why 0.000001
// is written out while 1e-7 is not.
std::string number_to_string(double value) {
    std::string out;
    if (non_finite(value, out)) { return out; }
    if (value == 0) { return "0"; } // -0 prints as "0"; Object.is tells them apart

    // THE INTEGER FAST PATH, and it is not a shortcut past the specification -
    // it is the same answer arrived at cheaply. For an integral value below
    // 1e15 the shortest digits are the integer's own and k <= n <= 21 holds, so
    // the rule below reduces to "print the digits". Pages print far more small
    // integers than anything else - a score, a frame count, an array index -
    // and this is measurably the common case.
    if (value == static_cast<double>(static_cast<std::int64_t>(value)) && std::fabs(value) < 1e15) {
        std::array<char, 24> whole{};
        const auto [stop, err] = std::to_chars(whole.data(), whole.data() + whole.size(),
                                               static_cast<std::int64_t>(value));
        if (err == std::errc{}) { return std::string{whole.data(), stop}; }
    }

    const bool negative = value < 0;
    const decimal d = shortest(std::fabs(value));
    const std::string_view s{d.digits.data(), static_cast<std::size_t>(d.count)};
    const int k = d.count;
    const int n = d.n;

    std::string text;
    if (k <= n && n <= 21) {
        text.append(s);
        text.append(static_cast<std::size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        text.append(s.substr(0, static_cast<std::size_t>(n)));
        text += '.';
        text.append(s.substr(static_cast<std::size_t>(n)));
    } else if (-6 < n && n <= 0) {
        text += "0.";
        text.append(static_cast<std::size_t>(-n), '0');
        text.append(s);
    } else if (k == 1) {
        text.append(s);
        append_exponent(text, n - 1);
    } else {
        text += s.front();
        text += '.';
        text.append(s.substr(1));
        append_exponent(text, n - 1);
    }
    return negative ? "-" + text : text;
}

// --- toFixed ---------------------------------------------------------------
std::string number_to_fixed(double value, int digits) {
    std::string out;
    if (non_finite(value, out)) { return out; }
    // Past 1e21 the specification hands back ToString, exponent and all, rather
    // than printing a thousand digits.
    if (std::fabs(value) >= 1e21) { return number_to_string(value); }

    const bool negative = value < 0;
    std::string text = fixed_digits(std::fabs(value), digits);
    // "-0.00" is not a thing: the sign goes on only if something is left of zero.
    if (negative && text.find_first_of("123456789") != std::string::npos) { text.insert(0, "-"); }
    return text;
}

// --- toExponential ---------------------------------------------------------
std::string number_to_exponential(double value, int places) {
    std::string out;
    if (non_finite(value, out)) { return out; }

    const bool negative = value < 0;
    const double magnitude = std::fabs(value);

    std::string digits;
    int n = 0;
    if (places < 0) {
        // No argument: as many digits as uniquely specify the value, which is
        // the shortest round trip again.
        const decimal d = shortest(magnitude);
        digits.assign(d.digits.data(), static_cast<std::size_t>(d.count));
        n = d.n;
        if (magnitude == 0) {
            digits = "0";
            n = 1;
        }
    } else {
        std::array<char, 64> buffer{};
        const auto [stop, err] = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                               magnitude, std::chars_format::scientific, places);
        if (err != std::errc{}) { return "0"; }
        int exponent = 0;
        split_scientific(buffer.data(), stop, digits, exponent);
        n = exponent + 1;
    }

    std::string text;
    text += digits.front();
    if (digits.size() > 1) {
        text += '.';
        text.append(digits, 1);
    }
    append_exponent(text, magnitude == 0 ? 0 : n - 1);
    return negative ? "-" + text : text;
}

// --- toPrecision -----------------------------------------------------------
//
// 21.1.3.5: `digits` counts SIGNIFICANT digits, and the choice between notations
// is made on e - exponential when e < -6 or e >= digits, positional otherwise.
// Trailing zeros are KEPT here, unlike everywhere else: `(1.5).toPrecision(3)`
// is "1.50" precisely because the caller asked for three digits.
std::string number_to_precision(double value, int digits) {
    std::string out;
    if (non_finite(value, out)) { return out; }

    const bool negative = value < 0;
    const double magnitude = std::fabs(value);
    if (magnitude == 0) {
        std::string zero = "0";
        if (digits > 1) {
            zero += '.';
            zero.append(static_cast<std::size_t>(digits - 1), '0');
        }
        return negative ? "-" + zero : zero;
    }

    // The exponent of the value rounded to `digits` significant digits - taken
    // from to_chars rather than from log10, which is off by one near a power of
    // ten and would put the decimal point in the wrong place there.
    std::array<char, 64> buffer{};
    const auto [stop, err] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), magnitude,
                                           std::chars_format::scientific, digits - 1);
    if (err != std::errc{}) { return number_to_string(value); }
    std::string significant;
    int e = 0;
    split_scientific(buffer.data(), stop, significant, e);

    std::string text;
    if (e < -6 || e >= digits) {
        text += significant.front();
        if (significant.size() > 1) {
            text += '.';
            text.append(significant, 1);
        }
        append_exponent(text, e);
    } else if (e >= 0) {
        text.append(significant, 0, static_cast<std::size_t>(e + 1));
        if (static_cast<int>(significant.size()) > e + 1) {
            text += '.';
            text.append(significant, static_cast<std::size_t>(e + 1));
        }
    } else {
        text += "0.";
        text.append(static_cast<std::size_t>(-e - 1), '0');
        text.append(significant);
    }
    return negative ? "-" + text : text;
}

// --- what "out of range" meant ---------------------------------------------
//
// Decided from the literal's ORDER OF MAGNITUDE, because from_chars will not
// say. The estimate need only get the SIGN right: from_chars accepts everything
// between 5e-324 and 1.8e308, so anything reaching here is past 10^308 or below
// 10^-323 and the margin is enormous. Off-by-one in the exponent cannot flip it.
double out_of_range_value(std::string_view text) {
    std::size_t at = 0;
    // Digits before the point, ignoring leading zeros.
    while (at < text.size() && text[at] == '0') { ++at; }
    int order = 0;
    std::size_t integer_digits = 0;
    while (at + integer_digits < text.size() && text[at + integer_digits] >= '0' &&
           text[at + integer_digits] <= '9') {
        ++integer_digits;
    }
    at += integer_digits;
    if (integer_digits > 0) {
        order = static_cast<int>(integer_digits) - 1;
    } else if (at < text.size() && text[at] == '.') {
        // 0.000...ddd - count the zeros the first significant digit sits behind.
        std::size_t zeros = 0;
        while (at + 1 + zeros < text.size() && text[at + 1 + zeros] == '0') { ++zeros; }
        order = -static_cast<int>(zeros) - 1;
    }
    // Skip the fraction, then take the explicit exponent if there is one.
    while (at < text.size() && (text[at] == '.' || (text[at] >= '0' && text[at] <= '9'))) { ++at; }
    if (at < text.size() && ascii_lower(text[at]) == 'e') {
        int exponent = 0;
        const char * first = text.data() + at + 1;
        const char * last = text.data() + text.size();
        if (first != last && *first == '+') { ++first; }
        std::from_chars(first, last, exponent);
        order += exponent;
    }
    // Magnitude only; the caller owns the sign, so an underflow is +0 and it is
    // the caller that turns it into -0.
    return order > 0 ? std::numeric_limits<double>::infinity() : 0.0;
}

// --- ToNumber on a string --------------------------------------------------
//
// from_chars, never strtod: strtod reads LC_NUMERIC for the decimal separator,
// and it reports failure by returning zero with errno, which is the same answer
// it gives for the string "0". from_chars says where it stopped, which is the
// check this needs - ToNumber is all-or-NaN, so anything left over is a
// failure rather than a prefix.
double string_to_number(std::string_view text) {
    const std::string_view body = trim(text, js_whitespace);
    if (body.empty()) { return 0; } // "" and "   " are both 0, per StringToNumber

    // THE RADIX PREFIXES TAKE NO SIGN. `Number("0x10")` is 16 and
    // `Number("-0x10")` is NaN, so this is checked before the sign is stripped.
    if (body.size() > 2 && body.front() == '0') {
        int radix = 0;
        switch (ascii_lower(body[1])) {
        case 'x': radix = 16; break;
        case 'o': radix = 8; break;
        case 'b': radix = 2; break;
        default: break;
        }
        if (radix != 0) {
            std::uint64_t whole = 0;
            const char * const first = body.data() + 2;
            const char * const last = body.data() + body.size();
            const auto [stopped, failed] = std::from_chars(first, last, whole, radix);
            if (failed != std::errc{} || stopped != last) { return std::nan(""); }
            return static_cast<double>(whole);
        }
    }

    // The sign is stripped HERE rather than left to from_chars, which accepts
    // '-' and rejects '+' - where JavaScript takes both.
    std::string_view rest = body;
    double sign = 1;
    if (rest.front() == '+' || rest.front() == '-') {
        sign = rest.front() == '-' ? -1 : 1;
        rest.remove_prefix(1);
    }
    if (rest.empty()) { return std::nan(""); }

    if (rest == "Infinity") { return sign * std::numeric_limits<double>::infinity(); }
    // from_chars ALSO accepts "inf", "infinity" and "nan", case-insensitively,
    // and JavaScript accepts none of them - `Number("inf")` is NaN and only the
    // exact spelling "Infinity" above is a number. A numeric literal never
    // starts with a letter, so refusing one here is the whole guard.
    if (ascii_lower(rest.front()) >= 'a' && ascii_lower(rest.front()) <= 'z') {
        return std::nan("");
    }

    // A TRAILING POINT IS LEGAL: `Number("5.")` is 5 and `Number("5.e3")` is
    // 5000, both of which from_chars stops short of. Dropping the point is
    // sound because nothing else in the grammar can follow the digits it ends.
    std::string without_point;
    const std::size_t point = rest.find('.');
    if (point != std::string_view::npos &&
        (point + 1 == rest.size() || ascii_lower(rest[point + 1]) == 'e')) {
        without_point.assign(rest.substr(0, point));
        without_point.append(rest.substr(point + 1));
        rest = without_point;
        if (rest.empty()) { return std::nan(""); }
    }

    double parsed = 0;
    const char * const first = rest.data();
    const char * const last = rest.data() + rest.size();
    const auto [stopped, failed] = std::from_chars(first, last, parsed);
    // Too big is Infinity and too small is zero, both of them SIGNED - and
    // neither is NaN. `Number("1e400")` is Infinity, which is what a page
    // comparing against Infinity expects to see.
    if (failed == std::errc::result_out_of_range) { return sign * out_of_range_value(rest); }
    // NOT a prefix: ToNumber requires the WHOLE string. "12abc" is NaN where
    // parseFloat("12abc") is 12, and conflating the two is what lets a page do
    // arithmetic on something that was never a number.
    if (failed != std::errc{} || stopped != last) { return std::nan(""); }
    return sign * parsed;
}

double string_to_number_prefix(std::string_view text) {
    std::string_view rest = text;
    while (!rest.empty() && js_whitespace.find(rest.front()) != std::string_view::npos) {
        rest.remove_prefix(1);
    }
    if (rest.empty()) { return std::nan(""); } // parseFloat("") is NaN, unlike Number("")

    double sign = 1;
    if (rest.front() == '+' || rest.front() == '-') {
        sign = rest.front() == '-' ? -1 : 1;
        rest.remove_prefix(1);
    }
    if (rest.empty()) { return std::nan(""); }

    if (rest.starts_with("Infinity")) { return sign * std::numeric_limits<double>::infinity(); }
    // Same guard as string_to_number: from_chars would take "inf" and "nan",
    // and parseFloat takes neither. NO radix prefixes either - parseFloat("0x10")
    // is 0, because it reads the leading "0" and stops at the 'x'.
    if (ascii_lower(rest.front()) >= 'a' && ascii_lower(rest.front()) <= 'z') {
        return std::nan("");
    }

    double parsed = 0;
    const char * const first = rest.data();
    const char * const last = rest.data() + rest.size();
    const auto [stopped, failed] = std::from_chars(first, last, parsed);
    if (failed == std::errc::result_out_of_range) { return sign * out_of_range_value(rest); }
    // A PREFIX, so `stopped` is not required to reach `last` - only to have
    // moved at all.
    if (failed != std::errc{} || stopped == first) { return std::nan(""); }
    return sign * parsed;
}

} // namespace ctbrowser::script
