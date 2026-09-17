// ctbrowser.script - the Unicode Character Database behind `\p{...}`.
//
// 22.2.2.9's UnicodeMatchProperty and UnicodeMatchPropertyValue, over the
// tables tools/gen/unicode_properties.py writes into regex_properties.inc.
// The .inc is DATA (its header says which Unicode release); this file is the
// only reader, so the matcher's header carries three declarations and no
// 700 KB of ranges for every TU that includes it.

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
    // no loose matching - the spelling must be one the tables carry.
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
    // table 67, then - under `v` only - a property of strings (table 69).
    if (const rx_property * e = find(rx_general_category, name); e != nullptr) {
        return rx_property_set{ranges_of(*e), {}};
    }
    if (const rx_property * e = find(rx_binary, name); e != nullptr) {
        return rx_property_set{ranges_of(*e), {}};
    }
    if (strings) {
        if (const rx_string_property * e = find(rx_strings, name); e != nullptr) {
            // The sequences are {length, code points...} runs; the span ends
            // where the next property's begin, so it is measured out here.
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
