//===- internal.hpp - Shared calc/ internals ------------------ -*- C++ -*-===//
//
// Private to lib/Style/css/calc/: not installed and in no file set.
// include/ctbrowser/style/css/calc.hpp declares the whole public surface; this
// header exists only so the implementation can span several files.
//
//===----------------------------------------------------------------------===//
#pragma once

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

/// One operand mid-expression. CSS Values 4 §10.2 gives calc a type algebra over
/// six base types, and this is the whole of it: a type, a value already
/// converted to that type's canonical unit, and a percentage that could not be
/// resolved here. Keeping the type separate from the value is what makes
/// `1px + 2` an error rather than 3px, and `1s + 1deg` an error too.
struct term {
    /// The type, as CSS Values 4 §10.2 defines it: an exponent per base type.
    /// All zero is a <number> and `dims[length] = 1` a <length>. Typed
    /// arithmetic (§10.2's "multiplication and division of types") also gives
    /// `dims[length] = 2` for `2px * 3px` and `dims[length] = -1` for
    /// `20 / 0.75rem`. No property takes those types, so `settle()` refuses
    /// them as a final answer, but they are valid mid-expression:
    /// `110px / 10px * 1px` passes through length^0 on the way to 11px. The WPT
    /// test css-values/calc-typed-arithmetic has many such expressions. Indexed
    /// by numeric_type; the `number` slot is never set.
    std::array<std::int8_t, 7> dims{};
    /// The value in the canonical unit of the type (pixels for a length).
    double value = 0.0;
    /// The unresolved percentage part, meaningful when `has_percent` is set.
    double percent = 0.0;
    bool has_percent = false;
    /// The dimensions that could not be added together, one entry per unit, in
    /// the order they were first written.
    ///
    /// Empty in the ordinary evaluation, where every unit has a basis and every
    /// length is a number of pixels. A specified value has no bases at all, and
    /// there `calc(1em + 1cap)` stays two terms: that is the simplified form
    /// itself (CSS Values 4 §10.12), not a failure to simplify. Sorted only when
    /// printed, because §10.13's order is a serialization rule.
    std::vector<std::pair<std::string, double>> symbols;

    [[nodiscard]] bool is_number() const noexcept {
        return std::ranges::all_of(dims, [](std::int8_t d) { return d == 0; });
    }
    /// Exactly one base type to the first power, or a number: the terms a
    /// property can take, and the only ones `type()` has an answer for.
    [[nodiscard]] bool simple() const noexcept {
        int sum = 0;
        for (const std::int8_t d : dims) { sum += d == 0 ? 0 : (d == 1 ? 1 : 2); }
        return sum <= 1;
    }
    /// The type of a simple term. Asked of a composite one, it names the first
    /// base with a non-zero exponent, which is enough for a diagnostic but not
    /// to compute with: check `simple()` first.
    [[nodiscard]] numeric_type type() const noexcept {
        for (std::size_t i = 1; i < dims.size(); ++i) {
            if (dims[i] != 0) { return static_cast<numeric_type>(i); }
        }
        return numeric_type::number;
    }
    /// Makes this term a <number> or one base type to the first power.
    void set_type(numeric_type family) noexcept {
        dims = {};
        if (family != numeric_type::number) { dims[static_cast<std::size_t>(family)] = 1; }
    }
};

/// Adds `coefficient` to `unit`'s entry in `into.symbols`, appending the entry
/// if it is not there yet. A linear scan, because a sum has a handful of
/// distinct units and the insertion order keeps serialization stable before it
/// is sorted.
inline void add_symbol(term & into, std::string_view unit, double coefficient) {
    for (auto & [name, value] : into.symbols) {
        if (name == unit) {
            value += coefficient;
            return;
        }
    }
    into.symbols.emplace_back(unit, coefficient);
}

/// The end of the math function that starts at `at`, name included.
/// Parentheses are matched with quoted runs skipped, so a `)` inside a string
/// cannot end the expression early.
///
/// An unterminated function runs to the end of the value, because end of input
/// closes every open block (CSS Syntax 3 §5.4.9): `calc(min(1em, 21px) * 2`
/// with no closing paren is still valid, and browsers fold it.
struct function_span {
    std::size_t end = 0; ///< One past the last byte, the `)` included when there is one.
    bool closed = false; ///< Whether a matching `)` was actually found.
};

// --- Helpers shared by more than one file of css/calc/ -----------------------
//
// Each is defined in the file named in its comment.

/// The unit table. Defined in units.cpp.
[[nodiscard]] bool is_known_unit(std::string_view unit) noexcept;
[[nodiscard]] bool context_free_unit(std::string_view unit) noexcept;
[[nodiscard]] std::optional<term> canonical_term(double value, std::string_view unit,
                                                 const length_context & ctx);
[[nodiscard]] std::optional<term> symbolic_term(double value, std::string_view unit);

/// Evaluates one expression with no bases at all (no font size, viewport or
/// containing block), answered as a term. `size_symbol` admits `size` as a
/// length term, for the calculation of a calc-size(). Defined in evaluator.cpp.
[[nodiscard]] std::pair<math_outcome, term> evaluate_symbolic(std::string_view expression,
                                                              bool size_symbol = false);

/// Makes the given idents <number> terms of their own while this object lives:
/// the channel keywords of a relative colour (CSS Color 5 §4.1), where
/// `calc(r * .5)` is a value whose specified form is `calc(0.5 * r)`. The
/// symbolic evaluator carries one as the symbol `$r`, and serialize_symbolic
/// writes it back without the marker. Defined in evaluator.cpp.
// ponytail: a thread-local list rather than a parameter threaded through
// simplify_math -> evaluate_symbolic -> evaluator; thread it if a second
// caller appears.
struct number_symbols_scope {
    explicit number_symbols_scope(std::span<const std::string_view> names) noexcept;
    ~number_symbols_scope();
    number_symbols_scope(const number_symbols_scope &) = delete;
    number_symbols_scope & operator=(const number_symbols_scope &) = delete;
};
/// Whether `key` (`$` followed by the ident) names an active number symbol.
[[nodiscard]] bool is_number_symbol(std::string_view key) noexcept;

/// One step of the text-level passes (simplify_math, fold_math and the tree
/// reader), which re-read their own nested functions: `min(min(min(...)))`
/// simplifies each argument more than one way, so a few dozen levels are
/// billions of calls. Every such call holds one of these; the count starts
/// over with the outermost, which is one declaration's value. Past
/// max_math_steps calls, or max_math_depth deep, a step is `exhausted` and
/// answers with its input, and the outermost gives back the author's text
/// whole (fold_math: not ok) rather than a value simplified in part.
// ponytail: thread-local like number_symbols_scope, for the same reason.
inline constexpr std::size_t max_math_steps = 16384;
inline constexpr int max_math_depth = 128;
struct math_step {
    math_step() noexcept;
    ~math_step();
    math_step(const math_step &) = delete;
    math_step & operator=(const math_step &) = delete;
    [[nodiscard]] bool exhausted() const noexcept;
    [[nodiscard]] bool outermost() const noexcept;
};

/// Simplifies a <calc-sum> that will not fold to one value over its tree (CSS
/// Values 4 §10.12) and writes it in §10.13's order, without a calc() around
/// it: `(min(10px, 20%) + max(1rem, 2%)) * 2` becomes
/// `2 * (min(10px, 20%) + max(1rem, 2%))`. With `ctx` the result is a computed
/// value (relative lengths in pixels, nested functions folded); without one, a
/// specified value. `nullopt` when the text is not one <calc-sum> of
/// arithmetic. Defined in tree.cpp.
[[nodiscard]] std::optional<std::string> simplify_sum_text(std::string_view expression,
                                                           const length_context * ctx = nullptr);

/// The inside of a symbolic calc() in the order of CSS Values 4 §10.13, or
/// empty when the sum has no canonical spelling. Defined in serialize.cpp.
[[nodiscard]] std::string serialize_symbolic(const term & value);

/// Finding a math function in a value's text. Defined in fold.cpp. `name_at`
/// returns the `(`-terminated name from `names` that starts exactly at `at`, or
/// an empty view. The boundary test (`is_name` on the byte before) matters:
/// `-webkit-calc(` and a custom property called `--my-calc` both contain
/// `calc(` and neither is one, and `minmax(100px, 1fr)` contains `max(`.
[[nodiscard]] std::string_view name_at(std::string_view value, std::size_t at,
                                       std::span<const std::string_view> names) noexcept;
[[nodiscard]] std::string_view math_name_at(std::string_view value, std::size_t at) noexcept;
[[nodiscard]] std::size_t end_of_string_at(std::string_view value, std::size_t at) noexcept;
[[nodiscard]] function_span span_of(std::string_view value, std::size_t at,
                                    std::string_view name) noexcept;
[[nodiscard]] std::string_view body_of(std::string_view value, std::size_t at,
                                       std::string_view name, const function_span & span);
[[nodiscard]] bool has_percentage(std::string_view text);
/// Every comma-separated argument of `body`, at bracket depth zero and with
/// quoted runs skipped.
[[nodiscard]] std::vector<std::string_view> top_level_arguments(std::string_view body);
/// Renders a comparison function that has no answer, each argument rewritten
/// by `one`, and a `clamp()` with an absent bound reduced to the comparison
/// that is left. Defined in fold.cpp; simplify_math (with symbolic terms) and
/// fold_math (against the bases it has) both render through it.
[[nodiscard]] std::string rewritten_arguments(
    std::string_view name, std::string_view inner,
    const std::function<std::string(std::string_view)> & one);

/// A `random()`'s sharing options (CSS Values 5 §random) in the form the WPT
/// test random-serialize spells them: the `<dashed-ident>`, the UA ident and
/// whether the value is element-scoped, with the defaults spelled out against
/// the property and the random()'s ordinal in the value:
///
///   nothing                 element-scoped ua-<property>-<position>
///   property-index-scoped   ua-<property>-<position>, on every element
///   property-scoped         ua-<property>, every position, every element
///   element-scoped alone    this element, whatever the property
///   --name                  the name alone, everywhere
///   --name element-scoped   the name, on this element
///
/// The evaluator hashes the three into a base and the serializer writes them
/// back out, so both read the options exactly once. Defined in evaluator.cpp.
struct random_key {
    std::string name;
    std::string ua;
    bool element_scoped = false;
};
[[nodiscard]] random_key random_options(std::string_view options, std::string_view property,
                                        std::size_t index);

/// The canonical unit's spelling, or an empty view for a `<number>`. A
/// computed value is serialized with it. Defined in units.cpp.
[[nodiscard]] std::string_view canonical_unit(numeric_type type) noexcept;

/// Converts one dimension to pixels. `nullopt` for a unit this does not model,
/// so a caller can leave the value alone rather than guess a wrong number. An
/// empty unit is a plain number and answers with itself, as a calc term needs.
/// Defined in units.cpp.
[[nodiscard]] std::optional<double> unit_to_px(double value, std::string_view unit,
                                               const length_context & ctx);

} // namespace ctbrowser::style::css::detail
