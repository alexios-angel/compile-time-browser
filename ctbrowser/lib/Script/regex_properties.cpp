//===- regex_properties.cpp - Unicode properties for \p{...} ----*- C++ -*-===//
//
// The Unicode Character Database behind regular expression `\p{...}`,
// simple case folding, and group-name validation.
//
// 22.2.2.9's UnicodeMatchProperty and UnicodeMatchPropertyValue, over the
// tables tools/gen/unicode_properties.py writes into regex_properties.inc.
// The .inc is generated data (its header names the Unicode release). This
// file is its only reader, so the matcher's header carries a few declarations
// instead of 700 KB of ranges for every translation unit that includes it.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/regex.hpp>

#include <algorithm>
#include <cstring>

namespace ctbrowser::script::rx {

namespace {

struct rx_property {
    const char * name;
    std::uint32_t at, count;
};
struct rx_string_property {
    const char * name;
    std::uint32_t at, count, seq_at, seq_count;
};

#include "regex_properties.inc"

/// Linear search of a property index by exact name; nullptr when absent.
template <typename Entry, std::size_t N>
[[nodiscard]] const Entry * find(const Entry (&index)[N], std::string_view name) {
    for (const Entry & e : index) {
        if (name == e.name) { return &e; }
    }
    return nullptr;
}

[[nodiscard]] std::span<const rx_range> ranges_of(const rx_property & e) {
    return {rx_ranges + e.at, e.count};
}

} // namespace

std::optional<rx_property_set> rx_property_lookup(std::string_view name, std::string_view value,
                                                  bool strings) {
    // `\p{Name=Value}`: only the three non-binary properties (table 68), and
    // no loose matching: the spelling must be one the tables carry.
    if (!value.empty()) {
        const rx_property * e = nullptr;
        if (name == "General_Category" || name == "gc") {
            e = find(rx_general_category, value);
        } else if (name == "Script" || name == "sc") {
            e = find(rx_script, value);
        } else if (name == "Script_Extensions" || name == "scx") {
            e = find(rx_script_extensions, value);
        }
        if (e == nullptr) { return std::nullopt; }
        return rx_property_set{ranges_of(*e), {}};
    }
    // `\p{LoneName}`: a General_Category value, then a binary property of
    // table 67, then (under the `v` flag only) a property of strings (table
    // 69).
    if (const rx_property * e = find(rx_general_category, name); e != nullptr) {
        return rx_property_set{ranges_of(*e), {}};
    }
    if (const rx_property * e = find(rx_binary, name); e != nullptr) {
        return rx_property_set{ranges_of(*e), {}};
    }
    if (strings) {
        if (const rx_string_property * e = find(rx_strings, name); e != nullptr) {
            // The sequences are {length, code points...} runs; the span ends
            // where the next property's begins, so it is measured here.
            std::size_t end = e->seq_at;
            for (std::uint32_t n = 0; n < e->seq_count; ++n) { end += 1 + rx_sequences[end]; }
            return rx_property_set{{rx_ranges + e->at, e->count},
                                   {rx_sequences + e->seq_at, end - e->seq_at}};
        }
    }
    return std::nullopt;
}

char32_t rx_fold_simple(char32_t cp) {
    const auto * it =
        std::lower_bound(std::begin(rx_folds), std::end(rx_folds), cp,
                         [](const char32_t (&row)[2], char32_t c) { return row[0] < c; });
    return it != std::end(rx_folds) && (*it)[0] == cp ? (*it)[1] : cp;
}

rx_ranges_t rx_fold_closure(const rx_ranges_t & ranges) {
    const auto in = [&](char32_t probe) {
        const auto it = std::lower_bound(ranges.begin(), ranges.end(), probe,
                                         [](const std::pair<std::uint32_t, std::uint32_t> & r,
                                            char32_t v) { return r.second < v; });
        return it != ranges.end() && it->first <= probe;
    };
    // Every fold pair with either side in the set brings the whole
    // equivalence class along: the fold, and everything that folds to it.
    rx_class cc;
    cc.ranges = ranges;
    for (const auto & row : rx_folds) {
        if (!in(row[0]) && !in(row[1])) { continue; }
        cc.ranges.push_back({row[1], row[1]});
        char32_t others[8];
        for (std::size_t n = rx_unfold(row[1], others); n-- > 0;) {
            cc.ranges.push_back({others[n], others[n]});
        }
    }
    rx_class_normalize(cc);
    return std::move(cc.ranges);
}

std::optional<std::string> rx_group_name(std::string_view raw) {
    // RegExpIdentifierStart / RegExpIdentifierPart are code points: a
    // `\uLEAD\uTRAIL` escape pair, a `\u{...}` escape, or a source character,
    // whose WTF-8 may itself be a lead and a trail surrogate stored apart. A
    // lone surrogate survives decoding and then fails the ID tests below.
    std::vector<char32_t> points;
    const auto hex4 = [&raw](std::size_t at) -> std::optional<char32_t> {
        if (at + 4 > raw.size()) { return std::nullopt; }
        char32_t v = 0;
        for (std::size_t k = at; k < at + 4; ++k) {
            const int d = hex_value(raw[k]);
            if (d < 0) { return std::nullopt; }
            v = v * 16 + static_cast<char32_t>(d);
        }
        return v;
    };
    const auto lead = [](char32_t c) { return c >= 0xD800 && c <= 0xDBFF; };
    const auto trail = [](char32_t c) { return c >= 0xDC00 && c <= 0xDFFF; };
    for (std::size_t i = 0; i < raw.size();) {
        if (raw[i] != '\\') {
            char32_t cp = decode_utf8(raw, i);
            if (lead(cp) && i < raw.size()) {
                std::size_t next = i;
                if (const char32_t low = decode_utf8(raw, next); trail(low)) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    i = next;
                }
            }
            points.push_back(cp);
            continue;
        }
        if (i + 1 >= raw.size() || raw[i + 1] != 'u') { return std::nullopt; }
        if (i + 2 < raw.size() && raw[i + 2] == '{') {
            const std::size_t close = raw.find('}', i + 3);
            if (close == std::string_view::npos || close == i + 3) { return std::nullopt; }
            char32_t v = 0;
            for (std::size_t k = i + 3; k < close; ++k) {
                const int d = hex_value(raw[k]);
                if (d < 0 || v > 0x10FFFF) { return std::nullopt; }
                v = v * 16 + static_cast<char32_t>(d);
            }
            if (v > 0x10FFFF) { return std::nullopt; }
            points.push_back(v);
            i = close + 1;
            continue;
        }
        const std::optional<char32_t> unit = hex4(i + 2);
        if (!unit) { return std::nullopt; }
        i += 6;
        char32_t cp = *unit;
        if (lead(cp) && i + 1 < raw.size() && raw[i] == '\\' && raw[i + 1] == 'u') {
            if (const std::optional<char32_t> low = hex4(i + 2); low && trail(*low)) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (*low - 0xDC00);
                i += 6;
            }
        }
        points.push_back(cp);
    }
    if (points.empty()) { return std::nullopt; }
    static const std::span<const rx_range> id_start = ranges_of(*find(rx_binary, "ID_Start"));
    static const std::span<const rx_range> id_continue = ranges_of(*find(rx_binary, "ID_Continue"));
    const auto in = [](std::span<const rx_range> set, char32_t cp) {
        const auto it = std::lower_bound(set.begin(), set.end(), cp,
                                         [](const rx_range & r, char32_t v) { return r.hi < v; });
        return it != set.end() && it->lo <= cp;
    };
    std::string out;
    for (std::size_t k = 0; k < points.size(); ++k) {
        const char32_t cp = points[k];
        const bool ok =
            cp == '$' || cp == '_' ||
            (k == 0 ? in(id_start, cp) : cp == 0x200C || cp == 0x200D || in(id_continue, cp));
        if (!ok) { return std::nullopt; }
        append_utf8(out, cp);
    }
    return out;
}

std::size_t rx_unfold(char32_t folded, char32_t (&out)[8]) {
    std::size_t n = 0;
    for (auto * it =
             std::lower_bound(std::begin(rx_unfolds), std::end(rx_unfolds), folded,
                              [](const char32_t (&row)[2], char32_t c) { return row[0] < c; });
         it != std::end(rx_unfolds) && (*it)[0] == folded && n < 8; ++it) {
        out[n++] = (*it)[1];
    }
    return n;
}

} // namespace ctbrowser::script::rx
