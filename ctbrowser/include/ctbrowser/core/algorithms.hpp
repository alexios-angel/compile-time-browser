#pragma once
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// THE SMALL THINGS EVERY SUBSYSTEM WAS WRITING FOR ITSELF. Not a grab bag:
// everything here had at least three copies in the tree.
//
// ASCII-ONLY, DELIBERATELY, and that is the recurring theme rather than an
// implementation detail. HTML tag names, HTTP field names and CSS keywords are
// all defined to fold over A-Z alone, and this repository byte-compares its
// rendered output across Linux and the Windows cross-build - so anything that
// consulted the host's locale would make a render depend on `LC_ALL`.

namespace ctbrowser {

// --- case folding ---------------------------------------------------------

// NOT `std::tolower`, which takes an int and is undefined for a negative one -
// which is exactly what a UTF-8 byte becomes when char is signed.
[[nodiscard]] constexpr char ascii_lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr char ascii_upper(char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

// Whole strings, through the single-character folds above: A-Z only, bytes
// above 127 untouched, no locale consulted.
void ascii_lower_in_place(std::string & text) noexcept;
[[nodiscard]] std::string ascii_lower_copy(std::string_view text);
void ascii_upper_in_place(std::string & text) noexcept;

// Equal after ascii_lower on both sides, and nothing else: NOT
// `boost::algorithm::iequals`, whose default overload takes the global locale -
// the host dependence above - and never merges two different UTF-8 sequences.
[[nodiscard]] bool ascii_iequals(std::string_view a, std::string_view b) noexcept;

// ascii_iequals against ANY of a keyword list - the shape every "is this one
// of the reserved words" test in the CSS front end has. The initializer_list
// overload exists because a span will not take a braced list.
[[nodiscard]] bool ascii_iequals_any(std::string_view text,
                                     std::span<const std::string_view> names) noexcept;
[[nodiscard]] inline bool ascii_iequals_any(
    std::string_view text, std::initializer_list<std::string_view> names) noexcept {
    return ascii_iequals_any(text, std::span<const std::string_view>{names.begin(), names.size()});
}

// The prefix form. CSS FUNCTION NAMES ARE ASCII CASE-INSENSITIVE - Bootstrap
// writes `RGBA(...)` in capitals - so this is what a function-name test wants.
[[nodiscard]] bool ascii_istarts_with(std::string_view text, std::string_view prefix) noexcept;

// --- values ---------------------------------------------------------------

// Split at separators that are NOT inside parentheses or a quoted string, so
// `border: 1px solid rgba(0, 0, 0, .175)` is three parts and not five.
//
// The SEPARATOR SET is a parameter for the same reason the whitespace set is
// elsewhere in this file: a shorthand's parts are separated by whitespace and a
// LIST's by commas, and they are genuinely different questions asked of the same
// string.
[[nodiscard]] std::vector<std::string_view> split_top_level(std::string_view text,
                                                            std::string_view separators);

// --- hex ------------------------------------------------------------------

// -1 rather than 0 for a non-digit, because `0` is a perfectly good hex digit
// and a caller that cannot tell them apart parses `#gg0000` as black. NOT
// `boost::convert`: a std::stringstream per call, measured 85x slower.
[[nodiscard]] constexpr int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

// --- whitespace -----------------------------------------------------------

// THE SET IS A PARAMETER ON PURPOSE, because the specifications disagree and
// unifying them would be a bug rather than a tidy-up:
//
//   HTML and CSS   space, tab, LF, FF, CR
//   JavaScript     the above plus vertical tab (and more this engine does not do)
//
// So a caller names the set it means, and the sets the engine uses are here to
// be named rather than retyped.
inline constexpr std::string_view html_whitespace = " \t\n\r\f";
inline constexpr std::string_view js_whitespace = " \t\n\r\f\v";

// NOT `boost::algorithm::trim`: it trims what the locale calls space, which
// INCLUDES vertical tab, and `trim_if` returns a std::string - measured 7x slower
// than this view.
[[nodiscard]] constexpr std::string_view trim(std::string_view text,
                                              std::string_view set) noexcept {
    const std::size_t first = text.find_first_not_of(set);
    if (first == std::string_view::npos) { return {}; }
    return text.substr(first, text.find_last_not_of(set) - first + 1);
}

// Infra's "strip and collapse": trim, then every interior run of the set
// becomes ONE space. `document.title` and a CSSOM prelude both read this way
// (`two\t\ttabs` comes back "two tabs"), and having them collapse differently
// was the two-decoder bug base64 had.
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

// --- utf-8 ----------------------------------------------------------------

// One code point, appended as UTF-8. A byte encoder and nothing more: the
// caller has already turned surrogates, NUL and out-of-range values into
// U+FFFD, because each spec says which of those to replace.
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

// WTF-8 CONCATENATION (WTF-8 §4.3): a high surrogate ending one piece and a
// low surrogate starting the next - `ED A0..AF xx` then `ED B0..BF xx` -
// are one code point once they meet, as the UTF-16 they stand for would be.
// Rewrites in place; a text with no ED byte is untouched at the cost of one
// memchr.
inline void join_surrogates(std::string & text) {
    std::size_t at = text.find('\xED');
    while (at != std::string::npos && at + 5 < text.size()) {
        const auto b1 = static_cast<unsigned char>(text[at + 1]);
        const auto b3 = static_cast<unsigned char>(text[at + 3]);
        const auto b4 = static_cast<unsigned char>(text[at + 4]);
        if (b1 >= 0xA0 && b1 <= 0xAF && b3 == 0xED && b4 >= 0xB0 && b4 <= 0xBF) {
            const auto b2 = static_cast<unsigned char>(text[at + 2]);
            const auto b5 = static_cast<unsigned char>(text[at + 5]);
            const char32_t high = 0xD000u | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
            const char32_t low = 0xD000u | ((b4 & 0x3Fu) << 6) | (b5 & 0x3Fu);
            const char32_t cp = 0x10000u + ((high - 0xD800u) << 10) + (low - 0xDC00u);
            std::string four;
            append_utf8(four, cp);
            text.replace(at, 6, four);
            at = text.find('\xED', at + 4);
            continue;
        }
        at = text.find('\xED', at + 1);
    }
}

// One code point out of UTF-8, advancing `at` past it. A truncated or
// malformed sequence yields the lead byte itself and advances by one, so bad
// input is REJECTED rather than approximated: a name check sees a value no
// production admits, a glyph walk still keeps the byte count honest. Only
// the continuation FORM is checked, never the range - WTF-8 lone surrogates
// (ED A0 80) must round-trip through CharacterData, so this is deliberately
// not a validator.
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

// --- base64 ---------------------------------------------------------------

// Bytes, not text: the result is a "binary string" of 0-255, which is what
// `atob` is defined to return and what a data: URL's payload actually is.
//
// LENIENT, matching the WHATWG `atob` in every browser: padding is optional,
// whitespace is skipped, and a character outside the alphabet is ignored rather
// than fatal. A truncated tail decodes as far as it goes.
[[nodiscard]] std::string base64_decode(std::string_view text);

} // namespace ctbrowser
