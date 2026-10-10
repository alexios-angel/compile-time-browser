//===- algorithms.hpp - Shared string helpers ----------------- -*- C++ -*-===//
//
// Small string, encoding and date helpers shared across subsystems: ASCII case
// folding and classes, top-level splitting, hex digits, whitespace trimming,
// UTF-8/WTF-8/UTF-16 conversion, the proleptic Gregorian calendar and base64.
//
// The case and class helpers are ASCII-only on purpose: HTML tag names, HTTP
// field names and CSS keywords fold over A-Z alone, and rendered output is
// byte-compared across Linux and Windows builds, so nothing here may consult
// the host locale (a locale-aware fold would make a render depend on `LC_ALL`).
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser {

// --- Case folding ---------------------------------------------------------

/// Lowercases A-Z only. Not `std::tolower`, which takes an int and is undefined
/// for a negative one, which is what a UTF-8 byte becomes when char is signed.
[[nodiscard]] constexpr char ascii_lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Uppercases a-z only.
[[nodiscard]] constexpr char ascii_upper(char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

/// Whole-string versions of the folds above: A-Z only, bytes above 127
/// untouched, no locale consulted.
void ascii_lower_in_place(std::string & text) noexcept;
[[nodiscard]] std::string ascii_lower_copy(std::string_view text);
void ascii_upper_in_place(std::string & text) noexcept;

/// True when `a` and `b` are equal after ascii_lower on both sides. Not
/// `boost::algorithm::iequals`, whose default overload uses the global locale.
/// Never treats two different UTF-8 sequences as equal.
[[nodiscard]] bool ascii_iequals(std::string_view a, std::string_view b) noexcept;

/// True when ascii_iequals(text, name) holds for any name in `names`, as the CSS
/// front end's keyword tests need. The initializer_list overload exists because
/// a span will not take a braced list.
[[nodiscard]] bool ascii_iequals_any(std::string_view text,
                                     std::span<const std::string_view> names) noexcept;
[[nodiscard]] inline bool ascii_iequals_any(
    std::string_view text, std::initializer_list<std::string_view> names) noexcept {
    return ascii_iequals_any(text, std::span<const std::string_view>{names.begin(), names.size()});
}

/// True when `text` starts with `prefix`, ASCII case-insensitively. CSS function
/// names are ASCII case-insensitive (Bootstrap writes `RGBA(...)`), so this is
/// what a function-name test wants.
[[nodiscard]] bool ascii_istarts_with(std::string_view text, std::string_view prefix) noexcept;

// --- Values ---------------------------------------------------------------

/// Splits `text` at any byte in `separators` that is not inside parentheses or
/// a quoted string, so `1px solid rgba(0, 0, 0, .175)` split on whitespace is
/// three parts, not five.
///
/// The separator set is a parameter because a shorthand's parts are separated
/// by whitespace and a list's by commas.
[[nodiscard]] std::vector<std::string_view> split_top_level(std::string_view text,
                                                            std::string_view separators);

// --- ASCII classes ----------------------------------------------------------

/// 0-9 and A-Za-z only; no locale or UTF-8 byte can match.
[[nodiscard]] constexpr bool is_ascii_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}
[[nodiscard]] constexpr bool is_ascii_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// --- Hex ------------------------------------------------------------------

/// The value of hex digit `c`, or -1 for a non-digit (not 0, which is a valid
/// digit; confusing the two would parse `#gg0000` as black). Not
/// `boost::convert`, which builds a std::stringstream per call and measured 85x
/// slower.
[[nodiscard]] constexpr int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

// --- Whitespace -----------------------------------------------------------

// The whitespace set is a parameter on purpose, because the specifications
// disagree and unifying them would be a bug:
//
//   HTML and CSS   space, tab, LF, FF, CR
//   JavaScript     the above plus vertical tab (and more this engine does not do)
//
// A caller names the set it means; the sets the engine uses are defined here.
inline constexpr std::string_view html_whitespace = " \t\n\r\f";
inline constexpr std::string_view js_whitespace = " \t\n\r\f\v";

/// `text` without leading and trailing bytes from `set`. Not
/// `boost::algorithm::trim`, which trims what the locale calls space (including
/// vertical tab), nor `trim_if`, which returns a std::string and measured 7x
/// slower than this view.
[[nodiscard]] constexpr std::string_view trim(std::string_view text,
                                              std::string_view set) noexcept {
    const std::size_t first = text.find_first_not_of(set);
    if (first == std::string_view::npos) { return {}; }
    return text.substr(first, text.find_last_not_of(set) - first + 1);
}

/// Infra's "strip and collapse": trim, then replace every interior run of
/// bytes from `set` with one space (`two\t\ttabs` -> "two tabs"). Used by
/// `document.title` and CSSOM preludes, which must collapse identically.
[[nodiscard]] inline std::string collapse_whitespace(std::string_view text, std::string_view set) {
    std::string out;
    bool space = false;
    for (const char c : trim(text, set)) {
        if (set.find(c) != std::string_view::npos) {
            space = true;
            continue;
        }
        if (space) { out += ' '; }
        space = false;
        out += c;
    }
    return out;
}

// --- UTF-8 ----------------------------------------------------------------

/// Appends one code point to `out` as UTF-8. A byte encoder and nothing more:
/// the caller decides which values, if any, to replace with U+FFFD. WTF-8
/// callers preserve lone surrogates; other specifications require replacement.
constexpr void append_utf8(std::string & out, char32_t cp) {
    const auto v = static_cast<std::uint32_t>(cp);
    if (v < 0x80) {
        out.push_back(static_cast<char>(v));
    } else if (v < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (v >> 6)));
        out.push_back(static_cast<char>(0x80u | (v & 0x3Fu)));
    } else if (v < 0x10000) {
        out.push_back(static_cast<char>(0xE0u | (v >> 12)));
        out.push_back(static_cast<char>(0x80u | ((v >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (v & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (v >> 18)));
        out.push_back(static_cast<char>(0x80u | ((v >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((v >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (v & 0x3Fu)));
    }
}

/// Infra's isomorphic decode: each byte becomes the code point of the same
/// value, U+0000-U+00FF, returned as UTF-8. This is a "binary string" (atob's
/// result, FileReader's readAsBinaryString, a canvas's PNG on its way to btoa),
/// since scripts read strings by UTF-16 code unit, not by byte.
[[nodiscard]] inline std::string isomorphic_decode(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (const char byte : bytes) { append_utf8(out, static_cast<unsigned char>(byte)); }
    return out;
}

/// WTF-8 concatenation (WTF-8 §4.3): a high surrogate (`ED A0..AF xx`)
/// followed by a low surrogate (`ED B0..BF xx`) is rewritten in place as the
/// one four-byte code point the UTF-16 pair stands for. A text with no ED byte
/// costs one memchr. Uses separate read and write cursors, because erasing
/// bytes per pair would shift the suffix each time and go quadratic.
inline void join_surrogates(std::string & text) {
    std::size_t at = text.find('\xED');
    if (at == std::string::npos) { return; }
    std::size_t write = at;
    while (at + 5 < text.size()) {
        const auto b1 = static_cast<unsigned char>(text[at + 1]);
        const auto b3 = static_cast<unsigned char>(text[at + 3]);
        const auto b4 = static_cast<unsigned char>(text[at + 4]);
        if (text[at] == '\xED' && b1 >= 0xA0 && b1 <= 0xAF && b3 == 0xED && b4 >= 0xB0 &&
            b4 <= 0xBF) {
            const auto b2 = static_cast<unsigned char>(text[at + 2]);
            const auto b5 = static_cast<unsigned char>(text[at + 5]);
            const char32_t high = 0xD000u | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
            const char32_t low = 0xD000u | ((b4 & 0x3Fu) << 6) | (b5 & 0x3Fu);
            const char32_t cp = 0x10000u + ((high - 0xD800u) << 10) + (low - 0xDC00u);
            std::string four;
            append_utf8(four, cp);
            for (const char byte : four) { text[write++] = byte; }
            at += 6;
            continue;
        }
        text[write++] = text[at++];
    }
    while (at < text.size()) { text[write++] = text[at++]; }
    text.resize(write);
}

/// Decodes one code point from `text` at `at` and advances `at` past it. A
/// truncated or malformed sequence yields the lead byte itself and advances by
/// one, so bad input is rejected rather than approximated: a name check sees a
/// value no production admits, and a glyph walk keeps the byte count right.
/// Only the continuation form is checked, never the range: WTF-8 lone
/// surrogates (ED A0 80) must round-trip through CharacterData, so this is
/// deliberately not a validator.
[[nodiscard]] constexpr char32_t decode_utf8(std::string_view text, std::size_t & at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t extra = 0;
    char32_t built = 0;
    if (lead < 0x80u) {
        ++at;
        return static_cast<char32_t>(lead);
    }
    if ((lead & 0xE0u) == 0xC0u) {
        extra = 1;
        built = static_cast<char32_t>(lead & 0x1Fu);
    } else if ((lead & 0xF0u) == 0xE0u) {
        extra = 2;
        built = static_cast<char32_t>(lead & 0x0Fu);
    } else if ((lead & 0xF8u) == 0xF0u) {
        extra = 3;
        built = static_cast<char32_t>(lead & 0x07u);
    } else {
        ++at;
        return static_cast<char32_t>(lead);
    }
    if (at + extra >= text.size()) {
        ++at;
        return static_cast<char32_t>(lead);
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        const auto byte = static_cast<unsigned char>(text[at + i]);
        if ((byte & 0xC0u) != 0x80u) {
            ++at;
            return static_cast<char32_t>(lead);
        }
        built = static_cast<char32_t>((built << 6) | (byte & 0x3Fu));
    }
    at += extra + 1;
    return built;
}

/// Converts the engine's WTF-8 storage to UTF-16 code units. Lone surrogates
/// and embedded NULs are preserved; supplementary code points become two units.
/// Uses decode_utf8's permissive policy: a bad or truncated sequence consumes
/// one byte as its value, and overlong or out-of-range encodings are not
/// rejected. Arbitrary bytes need not round-trip unchanged.
[[nodiscard]] std::u16string wtf8_to_utf16(std::string_view text);

/// Encodes UTF-16 units as WTF-8: adjacent high/low surrogates become one
/// four-byte code point, and lone surrogates use their three-byte encoding.
[[nodiscard]] std::string utf16_to_wtf8(std::u16string_view units);

/// The default case conversion of ECMA-262 22.1.3.28 (toLowerCase) and
/// 22.1.3.30 (toUpperCase) over WTF-8 text: Unicode 17.0.0's full lowercase
/// (`upper` false) or uppercase mapping, with Final_Sigma and without
/// language-sensitive rules. A lone surrogate is copied unchanged. The tables
/// are generated (tools/gen/unicode_case.py), so the answer never depends on
/// the C locale.
[[nodiscard]] std::string unicode_case(std::string_view text, bool upper);

/// The Unicode 17.0.0 default full lowercase of exactly one UTF-16 code unit.
/// U+0130 expands to i + U+0307; lone surrogates are unchanged. The unit is
/// isolated, so the contextual Final_Sigma rule cannot apply; this is not
/// whole-text casing.
[[nodiscard]] std::u16string unicode_lowercase_unit(char16_t unit);

// --- The proleptic Gregorian calendar ---------------------------------------

/// A proleptic Gregorian date; month is 1-12.
struct civil_date {
    std::int64_t year;
    int month;
    int day;
};
/// Days since 1970-01-01 for y/m/d (month 1-12), and back (civil_from_days),
/// using Hinnant's algorithms on 64-bit integers with floor division. Not
/// <chrono> (std::chrono::year stops at +-32767) or Boost.Date_Time
/// (1400-9999): a JS time value reaches year +-273790 and Temporal goes
/// further. Shared by Date, Temporal and the date input types.
[[nodiscard]] constexpr std::int64_t days_from_civil(std::int64_t y, int m, int d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
/// The inverse of days_from_civil.
[[nodiscard]] constexpr civil_date civil_from_days(std::int64_t z) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    return {yoe + era * 400 + (m <= 2 ? 1 : 0), m, static_cast<int>(doy - (153 * mp + 2) / 5 + 1)};
}
static_assert(days_from_civil(1970, 1, 1) == 0 && days_from_civil(2000, 3, 1) == 11017 &&
              days_from_civil(-271821, 4, 19) == -100000001 &&
              civil_from_days(-100000001).year == -271821 && civil_from_days(-1).day == 31);

// --- Base64 ---------------------------------------------------------------

/// Decodes base64 to raw bytes (each char 0-255), which is what `atob` returns
/// and what a data: URL's payload is.
///
/// Lenient: padding is optional, whitespace is skipped, and a character outside
/// the alphabet is ignored rather than fatal. A truncated tail decodes as far as
/// it goes.
[[nodiscard]] std::string base64_decode(std::string_view text);

/// Encodes bytes as base64 (RFC 4648 section 4, or section 5's URL alphabet
/// with `url`), padded to a multiple of four unless `omit_padding`. Used by btoa
/// (after its isomorphic encode), Uint8Array.prototype.toBase64, and data: URLs
/// made from bytes (a canvas's PNG, a Blob).
[[nodiscard]] std::string base64_encode(std::string_view bytes, bool url = false,
                                        bool omit_padding = false);

} // namespace ctbrowser
