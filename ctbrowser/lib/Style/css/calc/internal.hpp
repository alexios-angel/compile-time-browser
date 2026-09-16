#pragma once
// Private to lib/Style/css/calc/. NOT installed and in no file set:
// include/ctbrowser/style/css/calc.hpp declares the whole public surface, and
// this exists only so the implementation can be more than one file.

#include <ctbrowser/style/css/calc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    // THE TYPE, as CSS Values 4 §10.2 defines it: an exponent per base family.
    // All zero is a <number>; `dims[length] = 1` is a <length>; and since
    // typed arithmetic (§10.2's "multiplication and division of types")
    // `dims[length] = 2` is what `2px * 3px` is and `dims[length] = -1` what
    // `20 / 0.75rem` is - types with no property to land in, which is why
    // `settle()` still refuses them, but types all the same: `110px / 10px *
    // 1px` passes through length^0 on the way to being 11px, and
    // `typed_arithmetic` writes thirty-nine such expressions. Indexed by
    // numeric_type; the `number` slot is never set.
    std::array<std::int8_t, 7> dims{};
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

    [[nodiscard]] bool is_number() const noexcept {
        return std::ranges::all_of(dims, [](std::int8_t d) { return d == 0; });
    }
    // Exactly one family to the first power, or a number: the terms a property
    // can take, and the only ones `type()` has an answer for.
    [[nodiscard]] bool simple() const noexcept {
        int sum = 0;
        for (const std::int8_t d : dims) { sum += d == 0 ? 0 : (d == 1 ? 1 : 2); }
        return sum <= 1;
    }
    // The family of a simple term. Asked of a composite one it names the first
    // base with a non-zero exponent, which is enough to print a diagnostic
    // and not enough to compute with - check `simple()` first.
    [[nodiscard]] numeric_type type() const noexcept {
        for (std::size_t i = 1; i < dims.size(); ++i) {
            if (dims[i] != 0) { return static_cast<numeric_type>(i); }
        }
        return numeric_type::number;
    }
    void set_type(numeric_type family) noexcept {
        dims = {};
        if (family != numeric_type::number) { dims[static_cast<std::size_t>(family)] = 1; }
    }
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
// `size_symbol` admits `size` as a length term: the calculation of a calc-size().
[[nodiscard]] std::pair<math_outcome, term> evaluate_symbolic(std::string_view expression,
                                                              bool size_symbol = false);

// A <calc-sum> THAT WILL NOT FOLD, simplified over its tree (CSS Values 4
// §10.12) and written in §10.13's order, without a calc() around it:
// `(min(10px, 20%) + max(1rem, 2%)) * 2` is `2 * (min(10px, 20%) + max(1rem,
// 2%))`. With `ctx` it is a computed value - relative lengths in pixels and
// the nested functions folded; without one a specified value. `nullopt` when
// the text is not one <calc-sum> of arithmetic. Defined in tree.cpp.
[[nodiscard]] std::optional<std::string> simplify_sum_text(std::string_view expression,
                                                           const length_context * ctx = nullptr);

// The inside of a symbolic calc() in §10.13's order, or empty when the sum has
// no canonical spelling. Defined in serialize.cpp.
[[nodiscard]] std::string serialize_symbolic(const term & value);

// Finding a math function in a value's text. Defined in fold.cpp. `name_at` is a
// `(`-terminated name AT `at`, or an empty view, and the boundary test (`is_name`
// on the byte before) is the whole point: `-webkit-calc(` and a custom property
// called `--my-calc` both contain the five bytes of `calc(` and neither is one,
// and `minmax(100px, 1fr)` contains `max(` three bytes in.
[[nodiscard]] std::string_view name_at(std::string_view value, std::size_t at,
                                       std::span<const std::string_view> names) noexcept;
[[nodiscard]] std::string_view math_name_at(std::string_view value, std::size_t at) noexcept;
[[nodiscard]] std::size_t end_of_string_at(std::string_view value, std::size_t at) noexcept;
[[nodiscard]] function_span span_of(std::string_view value, std::size_t at,
                                    std::string_view name) noexcept;
[[nodiscard]] std::string_view body_of(std::string_view value, std::size_t at,
                                       std::string_view name, const function_span & span);
[[nodiscard]] bool has_percentage(std::string_view text);
// Every comma-separated argument of `body`, at bracket depth zero and with
// quoted runs skipped.
[[nodiscard]] std::vector<std::string_view> top_level_arguments(std::string_view body);
// A COMPARISON FUNCTION WITH NO ANSWER, its arguments each rewritten by `one`
// and a `clamp()` with an absent bound reduced to the comparison that is
// left. Defined in fold.cpp; simplify_math and fold_math both render through
// it, one with symbolic terms and one against the bases it has.
[[nodiscard]] std::string rewritten_arguments(
    std::string_view name, std::string_view inner,
    const std::function<std::string(std::string_view)> & one);

// A `random()`'s sharing options, CSS Values 5 §random, read the way
// random-serialize spells them - the `<dashed-ident>`, the UA ident and whether
// the value is element-scoped - with the defaults spelled out against the
// property and the random's ordinal in the value:
//
//   nothing                 element-scoped ua-<property>-<position>
//   property-index-scoped   ua-<property>-<position>, on every element
//   property-scoped         ua-<property>, every position, every element
//   element-scoped alone    this element, whatever the property
//   --name                  the name alone, everywhere
//   --name element-scoped   the name, on this element
//
// The evaluator hashes the three into a base and the serialiser writes them
// back out, so they read the options exactly once. Defined in evaluator.cpp.
struct random_key {
    std::string name;
    std::string ua;
    bool element_scoped = false;
};
[[nodiscard]] random_key random_options(std::string_view options, std::string_view property,
                                        std::size_t index);

// The canonical unit's spelling, or an empty view for a `<number>`. This is what
// a computed value is serialised with. Defined in units.cpp.
[[nodiscard]] std::string_view canonical_unit(numeric_type type) noexcept;

// One dimension to pixels. `nullopt` for a unit this does not model, so a caller
// can leave the value alone rather than guess at it - which is the difference
// between an honest gap and a wrong number. An empty unit is a plain number and
// answers with itself, because that is what a calc term needs. Defined in
// units.cpp.
[[nodiscard]] std::optional<double> unit_to_px(double value, std::string_view unit,
                                               const length_context & ctx);

} // namespace ctbrowser::style::css::detail
