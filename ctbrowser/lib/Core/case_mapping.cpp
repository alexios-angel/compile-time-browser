//===- case_mapping.cpp - Unicode case mapping ------------------*- C++ -*-===//
//
// The default case conversion that String.prototype.toLowerCase and
// toUpperCase apply (ECMA-262 22.1.3.28 / 22.1.3.30): Unicode's full case
// mappings, not only ASCII's.
//
// The table is generated from the pinned Unicode release (case_mapping.inc)
// and compiled in, rather than read from the C locale as `std::toupper` does,
// so every machine answers "Straße".toUpperCase() the same way - "STRASSE",
// as every browser does - and the goldens stay byte-identical.
//
// Three layers, as Unicode section 3.13 "Default Case Conversion" puts them:
// the unconditional SpecialCasing.txt entries (ß to "SS", İ to "i̇", the Greek
// iota-subscript forms) replace a code point with up to three; the one
// language-independent conditional entry, Final_Sigma, is judged here from the
// Cased and Case_Ignorable ranges the generated file also carries (the same
// ranges the regular-expression tables give \p{Cased} and \p{Case_Ignorable});
// everything else is UnicodeData's simple mapping. Language-sensitive rules
// (lt, tr, az) belong to toLocale*Case with a locale, which this engine does
// not have, so the locale forms return the default mapping as ECMA-262
// 22.1.3.26 allows.
//
// The text is WTF-8. A lone surrogate decodes to its own code point, which no
// table maps, and is written back as it was.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/algorithms.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

namespace ctbrowser {

namespace {

/// A run of code points first..last, every stride-th of which maps to itself
/// plus delta (case_mapping.inc's simple mappings).
struct case_run {
    char32_t first;
    char32_t last;
    std::uint16_t stride;
    std::int32_t delta;
};
/// A SpecialCasing.txt entry: \c cp maps to up to three code points.
struct case_special {
    char32_t cp;
    char32_t to[3]; ///< Zero-padded.
};
/// Code points lo..hi inclusive that have a property (Cased, Case_Ignorable).
struct case_range {
    char32_t lo;
    char32_t hi;
};

#include "case_mapping.inc"

/// The simple mapping of \p cp in \p runs, or \p cp when it has none.
template <std::size_t N> [[nodiscard]] char32_t simple_map(const case_run (&runs)[N], char32_t cp) {
    const auto * run = std::lower_bound(std::begin(runs), std::end(runs), cp,
                                        [](const case_run & r, char32_t c) { return r.last < c; });
    if (run == std::end(runs) || cp < run->first || (cp - run->first) % run->stride != 0) {
        return cp;
    }
    return static_cast<char32_t>(static_cast<std::int64_t>(cp) + run->delta);
}

/// The special-casing entry for \p cp, or null.
template <std::size_t N>
[[nodiscard]] const case_special * special_map(const case_special (&table)[N], char32_t cp) {
    const auto * at = std::lower_bound(std::begin(table), std::end(table), cp,
                                       [](const case_special & e, char32_t c) { return e.cp < c; });
    return at != std::end(table) && at->cp == cp ? at : nullptr;
}

/// Whether \p cp is in one of the sorted, disjoint \p ranges.
template <std::size_t N>
[[nodiscard]] bool has_property(const case_range (&ranges)[N], char32_t cp) {
    const auto * it = std::lower_bound(std::begin(ranges), std::end(ranges), cp,
                                       [](const case_range & r, char32_t c) { return r.hi < c; });
    return it != std::end(ranges) && it->lo <= cp;
}

/// The code point that ends at byte \p end of \p s; \p start receives where it
/// starts. A byte that is not the end of a valid sequence decodes alone.
[[nodiscard]] char32_t decode_before(std::string_view s, std::size_t end, std::size_t & start) {
    start = end - 1;
    while (start > 0 && end - start < 4 &&
           (static_cast<unsigned char>(s[start]) & 0xC0u) == 0x80u) {
        --start;
    }
    std::size_t at = start;
    const char32_t cp = decode_utf8(s, at);
    if (at != end) { // Not one sequence after all: take the last byte alone.
        start = end - 1;
        return static_cast<unsigned char>(s[start]);
    }
    return cp;
}

/// Final_Sigma (Unicode table 3-17): the capital sigma at [at, after) is
/// preceded by a cased letter and then zero or more case-ignorable ones, and
/// is not followed by zero or more case-ignorable ones and then a cased one.
[[nodiscard]] bool final_sigma(std::string_view s, std::size_t at, std::size_t after) {
    const auto & cased = cased_ranges;
    const auto & ignorable = case_ignorable_ranges;
    bool preceded = false;
    for (std::size_t end = at; end > 0;) {
        std::size_t start = 0;
        const char32_t cp = decode_before(s, end, start);
        end = start;
        if (has_property(ignorable, cp)) { continue; }
        preceded = has_property(cased, cp);
        break;
    }
    if (!preceded) { return false; }
    for (std::size_t i = after; i < s.size();) {
        const char32_t cp = decode_utf8(s, i);
        if (has_property(ignorable, cp)) { continue; }
        return !has_property(cased, cp);
    }
    return true;
}

} // namespace

std::string unicode_case(std::string_view s, bool upper) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        // ASCII is most of every page's text, and its mapping is one compare.
        const auto byte = static_cast<unsigned char>(s[i]);
        if (byte < 0x80u) {
            const char c = s[i++];
            out.push_back(upper ? ascii_upper(c) : ascii_lower(c));
            continue;
        }
        const std::size_t from = i;
        const char32_t cp = decode_utf8(s, i);
        if (!upper && cp == U'Σ') {
            append_utf8(out, final_sigma(s, from, i) ? U'ς' : U'σ');
            continue;
        }
        if (const case_special * full =
                upper ? special_map(upper_special, cp) : special_map(lower_special, cp)) {
            for (const char32_t each : full->to) {
                if (each != 0) { append_utf8(out, each); }
            }
            continue;
        }
        const char32_t mapped = upper ? simple_map(upper_runs, cp) : simple_map(lower_runs, cp);
        if (mapped == cp) {
            // Unmapped, so copy it as it was, including a lone surrogate or stray byte.
            out.append(s.substr(from, i - from));
        } else {
            append_utf8(out, mapped);
        }
    }
    return out;
}

} // namespace ctbrowser
