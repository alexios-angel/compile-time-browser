#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// THE SMALL THINGS EVERY SUBSYSTEM WAS WRITING FOR ITSELF.
//
// Not a grab bag: everything here had at least three copies in the tree, and
// each one is listed with where it came from so the next person can tell
// whether their case really is the same case. A shared helper that quietly
// differs from what a caller needed is worse than the duplication it replaced.
//
// ASCII-ONLY, DELIBERATELY, and that is the recurring theme rather than an
// implementation detail. HTML tag names, HTTP field names and CSS keywords are
// all defined to fold over A-Z alone, and this repository byte-compares its
// rendered output across Linux and the Windows cross-build - so anything that
// consulted the host's locale would make a render depend on `LC_ALL`.

namespace ctbrowser {

// --- case folding ---------------------------------------------------------

// Was: `tokenizer::lower`, and open-coded `std::tolower` in raster/ttf.cpp,
// script/builtins.cpp and two places in shell/bindings.
//
// NOT `std::tolower`, which takes an int and is undefined for a negative one -
// which is exactly what a UTF-8 byte becomes when char is signed. Every one of
// those call sites had to remember the cast to unsigned char; this one does not
// have the problem to remember.
[[nodiscard]] constexpr char ascii_lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr char ascii_upper(char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

void ascii_lower_in_place(std::string & text) noexcept;

[[nodiscard]] std::string ascii_lower_copy(std::string_view text);

// Was: `tokenizer::ascii_iequals` for HTML tag names, plus one in each of the
// two HTTP transports for header names.
//
// `boost::algorithm::iequals` implements it, but NOT its default overload -
// that one takes `std::locale()`, the global locale, which is the host
// dependence above. The definition passes `std::locale::classic()`, and having
// one function here is what stops the next caller reaching for the convenient
// overload by accident. Verified: with the classic locale it folds A-Z, leaves
// bytes above 127 alone, and so never merges two different UTF-8 sequences.
//
// Defined in algorithms.cpp so no consumer of the engine has to parse
// boost/algorithm/string/predicate.hpp - the rule core/cpu_time.hpp follows for
// <windows.h>.
[[nodiscard]] bool ascii_iequals(std::string_view a, std::string_view b) noexcept;

// --- hex ------------------------------------------------------------------

// Was: paint/values.hpp for `#rrggbb`, dom/tokenizer.cpp for `&#x41;`, and
// script/compile.cpp for `\x41` - three copies of the same six lines.
//
// -1 rather than 0 for a non-digit, because `0` is a perfectly good hex digit
// and a caller that cannot tell them apart parses `#gg0000` as black.
[[nodiscard]] constexpr int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

// --- whitespace -----------------------------------------------------------

// THE SET IS A PARAMETER ON PURPOSE, because the specifications disagree and
// unifying them would be a bug rather than a tidy-up. Eight call sites in this
// tree trim whitespace and they use four different sets - which looked like
// carelessness and is mostly correct:
//
//   HTML and CSS   space, tab, LF, FF, CR
//   JavaScript     the above plus vertical tab (and more this engine does not do)
//   GLSL lines     space, tab, CR - a newline ENDS the thing being trimmed
//
// So a caller names the set it means, and the three the engine uses are here to
// be named rather than retyped.
inline constexpr std::string_view html_whitespace = " \t\n\r\f";
inline constexpr std::string_view js_whitespace = " \t\n\r\f\v";
inline constexpr std::string_view glsl_line_whitespace = " \t\r";

[[nodiscard]] constexpr std::string_view trim(std::string_view text,
                                              std::string_view set) noexcept {
    const std::size_t first = text.find_first_not_of(set);
    if (first == std::string_view::npos) { return {}; }
    return text.substr(first, text.find_last_not_of(set) - first + 1);
}

[[nodiscard]] constexpr bool all_whitespace(std::string_view text,
                                            std::string_view set) noexcept {
    return text.find_first_not_of(set) == std::string_view::npos;
}

} // namespace ctbrowser
