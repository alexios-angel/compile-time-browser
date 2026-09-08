#pragma once
// Private to lib/Style/css/calc/. NOT installed and in no file set:
// include/ctbrowser/style/css/calc.hpp declares the whole public surface -
// evaluate_math, fold_math, simplify_math and the unit conversions - and this
// exists only so the implementation can be more than one file: it was 1,810
// lines in one until 2026-09-08. The includes are calc.cpp's, so every file
// here sees exactly what that one saw.

#include <ctbrowser/style/css/calc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/token.hpp>

namespace ctbrowser::style::css::detail {

// One operand mid-expression. CSS Values 4 §10.2 gives calc a type algebra over
// SIX base types and this is the whole of it: a tag saying which family, a value
// already converted to that family's CANONICAL unit, and a percentage that could
// not be resolved here. Keeping the tag separate from "the value happens to be
// zero" is what makes `1px + 2` an error rather than 3px, and what makes
// `1s + 1deg` one too.
struct term {
    numeric_type type = numeric_type::number;
    double value = 0.0;
    double percent = 0.0;
    bool has_percent = false;
    // THE DIMENSIONS THAT COULD NOT BE ADDED TOGETHER, one entry per unit, in
    // the order they were first written.
    //
    // Empty in the ordinary evaluation, where every unit has a basis and every
    // length is a number of pixels. A SPECIFIED value has no bases at all, and
    // there `calc(1em + 1cap)` is two terms for good - which is not a failure to
    // simplify but the simplified form itself, CSS Values 4 §10.12. Sorted only
    // when it is printed, because §10.13's order is a serialisation rule and the
    // arithmetic does not care.
    std::vector<std::pair<std::string, double>> symbols;

    [[nodiscard]] bool is_number() const noexcept { return type == numeric_type::number; }
};

// `unit`'s coefficient in `into`, created at the end if it is not there yet.
// Linear because a sum has a handful of distinct units and the write order is
// what keeps a serialisation stable before it is sorted.
inline void add_symbol(term & into, std::string_view unit, double coefficient) {
    for (auto & [name, value] : into.symbols) {
        if (name == unit) {
            value += coefficient;
            return;
        }
    }
    into.symbols.emplace_back(unit, coefficient);
}

// A `(`-terminated function name AT `at`, or an empty view. The boundary test is
// the whole point: `-webkit-calc(` and a custom property called `--my-calc` both
// contain the five bytes of `calc(` and neither is one, and `minmax(100px, 1fr)`
// contains `max(` three bytes in.
[[nodiscard]] constexpr bool is_name_char(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
           c == '_';
}

// The end of the math function that starts at `at`, name included. Parentheses
// are matched with quoted runs skipped, so a `)` inside a string cannot end the
// expression early.
//
// AN UNTERMINATED FUNCTION RUNS TO THE END OF THE VALUE, which is CSS Syntax 3
// §5.4.9: EOF closes every open block. It is not a corner case here -
// `minmax-length-computed` writes `calc(min(1em, 21px) * 2` with no closing paren
// four times over, and refusing it deleted the declaration where a browser folds
// it to 40px.
struct function_span {
    std::size_t end = 0; // one past the last byte, the `)` included when there is one
    bool closed = false; // whether a matching `)` was actually found
};

// --- helpers shared by more than one file of css/calc/ ------------------------
//
// Everything here was in an anonymous namespace of calc.cpp. It gained external
// linkage when that file was split, and nothing else: the bodies are where they
// were, in the file that owns the concern, and this declares them.

// The unit table. Defined in units.cpp.
[[nodiscard]] bool is_known_unit(std::string_view unit) noexcept;
[[nodiscard]] bool context_free_unit(std::string_view unit) noexcept;
[[nodiscard]] std::optional<term> canonical_term(double value, std::string_view unit,
                                                 const length_context & ctx);
[[nodiscard]] std::optional<term> symbolic_term(double value, std::string_view unit);

// One expression with NO bases at all, answered as a term. Defined in
// evaluator.cpp.
[[nodiscard]] std::pair<math_outcome, term> evaluate_symbolic(std::string_view expression);

// The inside of a symbolic calc() in §10.13's order, or empty when the sum has
// no canonical spelling. Defined in serialize.cpp.
[[nodiscard]] std::string serialize_symbolic(const term & value);

// Finding a math function in a value's text. Defined in fold.cpp.
[[nodiscard]] std::string_view name_at(std::string_view value, std::size_t at,
                                       std::span<const std::string_view> names) noexcept;
[[nodiscard]] std::string_view math_name_at(std::string_view value, std::size_t at) noexcept;
[[nodiscard]] std::size_t end_of_string_at(std::string_view value, std::size_t at) noexcept;
[[nodiscard]] function_span span_of(std::string_view value, std::size_t at,
                                    std::string_view name) noexcept;
[[nodiscard]] std::string_view body_of(std::string_view value, std::size_t at,
                                       std::string_view name, const function_span & span);
[[nodiscard]] bool has_percentage(std::string_view text);

} // namespace ctbrowser::style::css::detail
