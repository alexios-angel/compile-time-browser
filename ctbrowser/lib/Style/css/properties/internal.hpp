#pragma once
// The property table, the value grammar, and the canonical serialisation.
//
// See include/ctbrowser/style/css/properties.hpp for why this is conservative
// and where the three consumers are. The short version: a property this file
// does not model is accepted verbatim, so nothing that works today can start
// failing, and only a property with a real `value_kind` can refuse anything.
//
// Private to lib/Style/css/properties/. NOT installed and in no file set:
// include/ctbrowser/style/css/properties.hpp declares the whole public
// surface, and this exists only so the implementation can be more than one
// file: it was 1,155 lines in one until 2026-09-08. The includes are
// properties.cpp's, so every file here sees exactly what that one saw.

#include <ctbrowser/style/css/properties.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/token.hpp>

#include <algorithm>
#include <array>
#include <boost/container/small_vector.hpp>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::style::css::detail {

using k = value_kind;

struct scan {
    std::vector<std::size_t> significant; // indices of the non-whitespace tokens
    bool malformed = false;               // a bad string/url, or unbalanced brackets
    bool important = false;               // a `!` delim; `!important` is not a value
    bool substituted = false;             // holds a var()/env()
    // A function whose NAME this engine does not implement. Only `CSS.supports`
    // reads it: `el.style` still stores such a value, because CSSOM says a page
    // may set a property this engine has never heard of and read it back.
    bool unknown_function = false;
};

// DOES A PERCENTAGE MEAN ANYTHING FOR THIS PROPERTY? It is the property that
// supplies §10.11's calculation context, so this is the one question a math
// function cannot answer for itself: `text-indent: min(1px, 0%)` resolves
// against a containing block and `border-left-width: min(1px, 0%)` has nothing
// to resolve against and is a syntax error, however alike the two look.
//
// A `freeform` property answers YES, and has to: the grammar is not modelled, so
// `transform: translate(50%)` and `background-position: calc(50% - 1px)` would
// both be lost to a guess.
[[nodiscard]] constexpr bool takes_percentage_of(value_kind kind) noexcept {
    switch (kind) {
    case k::length_percentage:
    case k::percentage:
    case k::number_percentage:
    case k::number_length_percentage:
    case k::position:
    case k::freeform:
    case k::keyword_only: return true;
    case k::length:
    case k::number:
    case k::integer:
    case k::number_length:
    case k::angle:
    case k::time: return false;
    }
    return true;
}

// --- helpers shared by more than one file of css/properties/ ------------------
//
// Everything here was in an anonymous namespace of properties.cpp. It gained
// external linkage when that file was split, and nothing else: the bodies are
// where they were, in grammar.cpp, and this declares them.

[[nodiscard]] bool in_list(std::span<const std::string_view> list, std::string_view name);
[[nodiscard]] bool has_keyword(std::string_view set, std::string_view word);
[[nodiscard]] scan scan_tokens(const token_stream & ts);
[[nodiscard]] bool substitution_grammar_ok(const token_stream & ts);
[[nodiscard]] bool whole_value_is_math(const token_stream & ts, const scan & found);
[[nodiscard]] bool math_type_fits(const property_syntax & p, const math_answer & answer);
[[nodiscard]] bool match_position(const token_stream & ts, const scan & found, std::string & out);
[[nodiscard]] bool match_typed(const token_stream & ts, const css_token & t,
                               const property_syntax & p, std::string & out);

} // namespace ctbrowser::style::css::detail
