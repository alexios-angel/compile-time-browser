#pragma once
#include <cstdint>
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

// --- base64 ---------------------------------------------------------------

// Bytes, not text: the result is a "binary string" of 0-255, which is what
// `atob` is defined to return and what a data: URL's payload actually is.
//
// LENIENT, matching the WHATWG `atob` in every browser: padding is optional,
// whitespace is skipped, and a character outside the alphabet is ignored rather
// than fatal. A truncated tail decodes as far as it goes.
[[nodiscard]] std::string base64_decode(std::string_view text);

} // namespace ctbrowser
