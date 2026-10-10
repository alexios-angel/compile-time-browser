//===- calc.hpp - calc() math functions -------------------------*- C++ -*-===//
//
// `calc()` and the other CSS math functions, and the unit table every relative
// length goes through.
//
// The comparison functions (CSS Values 4 §10.3) use the same recursive descent
// as the rest, plus an argument list and a third outcome for a comparison that
// cannot be decided here (see math_outcome).
//
// A percentage cannot fold: `calc(100% - 12px)` has no answer until a
// containing block exists, which is a used-value question, not a
// computed-value one. So a result carries an optional percentage alongside its
// pixels and serialises back to `calc(50% + 12px)`, the canonical two-term form
// Chrome prints, which layout::parse_length reads against its basis.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ctbrowser::style::css {

/// The bases a relative length resolves against: facts about the element or
/// the window. They travel together because one calc can mix them:
/// `calc(1.375rem + 1.5vw)` (Bootstrap's fluid heading size) needs the root
/// font size and the viewport in the same expression.
struct length_context {
    /// The element's own font size, which is what `em` means everywhere except
    /// in `font-size` itself, where it is the parent's (the caller passes the
    /// parent's for that one property). That asymmetry is CSS's: `font-size:
    /// 2em` doubles the inherited size rather than being circular.
    float font_size = 16.0f;
    float root_font_size = 16.0f;
    /// The line height `lh` measures, and the root's for `rlh` (CSS Values 4
    /// §6.1.1). The same asymmetry as `em`: in `line-height` itself `lh` is the
    /// parent's, everywhere else the element's own; the caller passes the right
    /// one. `normal` is 1.25 times the font size, the factor layout uses, not
    /// the font's metrics.
    float line_height = 20.0f;
    float root_line_height = 20.0f;
    /// The advance of `0` in the element's font and in the root's, which `ch`
    /// and `rch` measure (CSS Values 4 §6.1.1). Only a font backend knows it;
    /// the style engine has it injected (engine::set_text_measure). Zero means
    /// nothing measured, and the unit takes CSS's fallback of half an em.
    float zero_advance = 0.0f;
    float root_zero_advance = 0.0f;
    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
    /// The query containers' content boxes that `cqw` and `cqh` measure (CSS
    /// Conditional 5 §4): the nearest size container on each axis, as the
    /// cascade found it. Negative where there is none, in which case the unit
    /// falls back to the small viewport's size on that axis.
    float container_width = -1.0f;
    float container_height = -1.0f;
    /// True when the element's writing mode is vertical, so the inline axis is
    /// the viewport's height and `vi`/`vb` (and `cqi`/`cqb`) swap (CSS Values 4
    /// §6.1.2). The cascade sets it from the element's own `writing-mode`, not
    /// the root's.
    bool vertical = false;
    /// The element's one-based position among its siblings and their count,
    /// which `sibling-index()` and `sibling-count()` answer (CSS Values 5
    /// §tree-counting). Zero means "no element here" (every context but the
    /// cascade's) and leaves both functions unresolved.
    std::uint32_t sibling_index = 0;
    std::uint32_t sibling_count = 0;
    /// What a percentage is a percentage of, when the caller has it: the
    /// containing block's width, at used-value time. The cascade never sets it
    /// (a computed value keeps `calc(50% + 12px)`); getComputedStyle sets it for
    /// a margin or padding, whose resolved value CSSOM defines as the used one.
    /// For example `round(10%, 1px)` against a 75px block is `8px`, which no
    /// linear sum can express before the basis exists.
    std::optional<float> percent_basis;
    /// The key `random()` is cached by (CSS Values 5 §random-caching). An
    /// unnamed random() differs per element, per property and per position in
    /// the value; the caller says which element and property this is.
    /// `random_index` is the zero-based ordinal, in source order, of this
    /// expression's first random() within the value, set by the fold as it
    /// walks the value; zero everywhere else.
    std::uint64_t element_key = 0;
    std::string_view property;
    std::uint32_t random_index = 0;
    /// True when the element's `interpolate-size` is `allow-keywords` (CSS
    /// Values 5 §interpolate-size): an intrinsic size keyword then interpolates
    /// with a <length-percentage> as `calc-size(<keyword>, size)`. Only
    /// animation sets it.
    bool interpolate_keywords = false;
};

/// Which of CSS's numeric types a math function produced. CSS Values 4 §10.2
/// gives calc() a type algebra over several base types, not just length and
/// number: `rotate: calc(10deg + 5deg)` and `transition-delay: calc(1s / 2)`
/// are values.
///
/// Each type has one canonical unit that every unit of the family converts to,
/// so a term carries a plain number and this tag rather than the author's
/// unit. That is also the computed value: CSS Values 4 §6.1 and §6.4 say an
/// angle computes to `deg` and a time to `s`, however it was written.
enum class numeric_type : std::uint8_t {
    number,     // no unit at all
    length,     // canonical px
    angle,      // canonical deg
    time,       // canonical s
    frequency,  // canonical Hz
    resolution, // canonical dppx
    // `<flex>`, canonical fr. It never resolves here (only grid track sizing
    // does), but naming the type is what makes `min(1px, 0fr)` a type error
    // rather than an unresolved comparison, as css/css-values expects.
    flex,
};

/// A math result that may carry a percentage it could not resolve. `px` alone
/// is the ordinary case; `has_percent` is the `calc(100% - 12px)` one.
///
/// `is_number` marks a unitless result: CSS Values 3 §8.1 lets a math function
/// resolve to a `<number>` as readily as to a `<length>`, so `opacity: calc(2 /
/// 4)`, `z-index: calc(1 + 1)` and `rgb(calc(0), calc(255), calc(0))` are all
/// valid. Then `px` carries the number.
///
/// Despite its name, `px` is the value in `type`'s canonical unit, which is
/// pixels only when `type` is `length`. It is a double because `pow(20, 4)` and
/// `3e+9px` lose their last digits in a float's 24-bit mantissa, and trig and
/// exponential intermediates lose more.
struct calc_result {
    double px = 0.0;
    double percent = 0.0;
    bool has_percent = false;
    bool is_number = false;
    /// The unit family `px` is measured in. It is `number` exactly when
    /// `is_number` is set; both exist because some callers read only the
    /// boolean.
    numeric_type type = numeric_type::length;
};

/// The outcome of evaluating one math function. The `unresolved` case is what
/// makes min()/max()/clamp() safe to support:
///
///   resolved     a number came out; substitute it
///   unresolved   the expression is well formed but has no answer here.
///                `min(10px, 5%)` needs a containing block, a used-value
///                question; CSS Values 4 §10.11 says its computed value is the
///                function as written. The text is kept and the declaration is
///                left alone.
///   invalid      not valid arithmetic: `1px + 2`, `2px * 3px`, an unmodelled
///                unit, a missing operator. The declaration is invalid and the
///                cascade drops it.
enum class math_outcome : std::uint8_t {
    resolved,
    unresolved,
    invalid
};

/// The result of evaluate_math().
struct math_answer {
    math_outcome outcome = math_outcome::invalid;
    calc_result value;
    /// How many random() calls the expression holds, whatever the outcome, so a
    /// caller walking a whole value can number the next one: the automatic
    /// sharing key is the call's ordinal in the value
    /// (`length_context::random_index`), and one calc() may hold several.
    std::uint32_t randoms = 0;
};

/// What kind of number the property accepts. A `<number>` answer is valid for
/// `opacity` and a syntax error for `width`, and the evaluator cannot tell
/// which on its own, so the cascade, which knows the property, says: `width:
/// calc(2 * 3)` is invalid, not `6px`.
enum class math_context : std::uint8_t {
    // A number and a length are both plausible somewhere in this value (a
    // colour channel, a font-feature axis, `opacity`, a transform). This is the
    // default, because guessing "length" for an unknown property would silently
    // reject valid values.
    any,
    // The whole value is a length, a percentage, or a list of them: a bare
    // number cannot appear in it and one that does is a syntax error.
    length,
    // The whole value is an `<integer>`. A math function may still answer with
    // a fraction (`z-index: calc(3 / 2)` is valid CSS), and CSS Values 4 §10.10
    // says the computed value rounds it. It is a context rather than a
    // post-pass because rounding must happen once, at the end of the
    // conversion: `calc(calc(1 / 3) * 3)` is 1, not 0.
    integer
};

/// The math_context of `property`. Deliberately a short list of properties
/// whose entire value is lengths (or integers): a compound value like
/// `box-shadow` or `background-position` gets `any`, because the context
/// applies to every math function in the value and one may legitimately be a
/// number.
[[nodiscard]] math_context math_context_of(std::string_view property) noexcept;

/// One dimension rewritten in its family's canonical unit: `1in` -> `96px`,
/// `10ms` -> `0.01s`, `100grad` -> `90deg`, `96dpi` -> `1dppx`. `nullopt` for
/// text that is not exactly one dimension, or whose unit needs a basis this
/// engine lacks.
///
/// This is for computed values, which CSS Values 4 §6.4 and §6.5 define in the
/// canonical unit (`transition-delay: 12ms` computes to `0.012s`). Callers that
/// want a number for layout use length_text_to_px() instead.
[[nodiscard]] std::optional<std::string> canonical_dimension_text(std::string_view text,
                                                                  const length_context & ctx);

/// Evaluates one expression: the inside of a calc(), or a whole `calc(...)`,
/// `min(...)`, `max(...)` or `clamp(...)`. See math_outcome for the outcomes.
[[nodiscard]] math_answer evaluate_math(std::string_view expression, const length_context & ctx);

/// The type of a well-formed math function that has no value yet. `calc(1px *
/// sibling-index())` is unresolved everywhere but in the cascade, yet is a
/// <length>: CSS Values 4 §10.2 types the expression before anything is
/// measured, which lets `rotate` reject it immediately. Returns a calc_result
/// whose magnitudes are zero (only `type`, `is_number` and `has_percent` mean
/// anything), or `nullopt` for an expression whose type cannot be settled
/// without a basis, such as `min(10px, 5%)`.
[[nodiscard]] std::optional<calc_result> math_type_of(std::string_view expression);

/// `calc-size(<calc-size-basis>, <calc-sum>)` (CSS Values 5 §calc-size) as a
/// specified value: the basis in canonical form (a keyword, a nested
/// calc-size() or a <length-percentage>), and the calculation simplified with
/// `size` as a term of its own, so `size * 2` becomes `2 * size`. `keywords`
/// are the property's own size keywords (`auto` for width, `none` for
/// max-width); the intrinsic keywords and `any` are always a valid basis, and
/// `size` may not be used with `any`. `nullopt` when `value` is not exactly one
/// valid calc-size().
[[nodiscard]] std::optional<std::string> calc_size_text(std::string_view value,
                                                        std::string_view keywords);

/// Rewrites every random() in a specified value with its caching key spelled
/// out (CSS Values 5 §random-caching): the sharing options become the
/// `<dashed-ident>`, `element-scoped` and UA-ident triple random_base() keys
/// on, and the bounds take their canonical units. For example
/// `random(0px, 100px)` in `width` becomes `random(element-scoped ua-width-1,
/// 0px, 100px)`, and `property-scoped` becomes `ua-width`. `fixed` keeps its
/// number.
[[nodiscard]] std::string canonical_random(std::string_view value, std::string_view property);

/// A folded value, and whether every calc() in it evaluated.
///
/// The flag matters: `margin-top: calc(-1 * var(--bs-gutter-y))` with a gutter
/// of `0` multiplies two numbers and gets a number, not a length, so the
/// declaration is invalid and `margin-top` takes its initial 0, as in Chrome.
/// Leaving the text in place would hand layout `calc(-1 * 0)`, which it reads
/// as `auto`.
struct folded_value {
    std::string text;
    bool ok = true;
};

/// Replaces every math function in a declaration value by its answer. A value
/// with none comes back unchanged and `ok`. One whose calc() did not evaluate
/// comes back with its text unchanged and `ok` false, so a caller with no
/// better answer can still use the text and one that has one can drop the
/// declaration.
///
/// A comparison function never makes a declaration invalid: `min()`, `max()`
/// and `clamp()` either fold or keep their text with `ok` intact, since
/// `min(10px, 5%)` is a valid declaration whose computed value is the function
/// as written.
[[nodiscard]] folded_value fold_math(std::string_view value, const length_context & ctx,
                                     math_context accepts = math_context::any);

/// A folded value clamped to zero from below. CSS Values 4 §10.10: a math
/// function's result outside the property's range is clamped at computed-value
/// time, so `tab-size: calc(2 * -4)` is 0 where a literal `-8` is a syntax
/// error. The property table says which properties are non-negative. A lone
/// negative number, dimension or percentage becomes the zero of its unit;
/// anything else is returned unchanged.
[[nodiscard]] std::string non_negative(std::string_view folded);

/// A cheap pre-check: a substring test for the math function names. A false
/// positive such as `--custom: calc-ish-name` costs one wasted parse.
[[nodiscard]] bool may_have_math(std::string_view value) noexcept;

/// Replaces every math function in a specified value by its simplified form
/// (CSS Values 4 §10.12).
///
/// Every function is simplified wherever it sits, not only when it is the
/// whole value: `transform: rotate(acos(1))` is `rotate(calc(0deg))` and
/// `image-set(url("") calc(1x * NaN))` is `image-set(url("") calc(NaN *
/// 1dppx))` in every browser. Tests reach these functions through
/// `transform`, `background-image` and `scale`, whose grammar this engine does
/// not model, so the simplification must not depend on it.
///
/// The result is a specified value, so it keeps a `calc()` around it: that is
/// what distinguishes `width: calc(96px)` from `width: 96px`. (serialize_calc()
/// writes the computed form, a bare `96px`.) A `calc()` whose whole body is
/// one other math function loses that redundant layer: `calc(clamp(1px, 1em,
/// 1vh))` is `clamp(1px, 1em, 1vh)`.
///
/// A function that cannot be simplified here keeps the author's bytes. There
/// are no bases at specified-value time, so anything mentioning `em`, `vw`,
/// `lh`, `cqw`, `fr` or a percentage is left as written (§10.11 says
/// `calc(10px + 1em)` keeps both terms), as is a function this file cannot
/// evaluate or a comparison with no answer until layout.
[[nodiscard]] std::string simplify_math(std::string_view value);

/// Whether every math function in this value is well formed, meaning a
/// browser would not drop the declaration on sight. That differs from "does
/// it fold": `min(10px, 5%)` has no answer until layout but is well formed.
/// Malformed examples: `round(nearest, 1px)` (missing its step), `calc(7px *
/// up)` (multiplies by a keyword) and `rotate(calc((0.25turn error)))` (two
/// values where one belongs).
///
/// It looks inside other functions, which the fold does not need to: the last
/// example is an argument of `rotate()`, and this engine does not model
/// `transform`'s grammar.
///
/// Only math functions this file implements are checked. Others, such as
/// `calc-size()`, are left alone, because "cannot parse it" and "it is
/// invalid" are different answers and only the second may delete a
/// declaration.
[[nodiscard]] bool math_syntax_ok(std::string_view value);

/// Whether a percentage appears inside a math function in `value`. A
/// percentage elsewhere does not count: `hsl(calc(1deg) 82% 43%)` has two that
/// are colour channels, not something a calculation must resolve.
///
/// The caller decides what this means, because §10.11's calculation context is
/// the property's: `text-indent: min(1px, 0%)` resolves against a containing
/// block, while `border-left-width: min(1px, 0%)` has nothing to resolve
/// against and is a syntax error. This file knows where the math functions
/// are and the property table knows which properties take a percentage.
[[nodiscard]] bool math_uses_percentage(std::string_view value);

/// Converts one already-folded length in text form to pixels: `12px`,
/// `1.5rem`, `2em`, or a bare number. `nullopt` for a percentage, a keyword, a
/// calc that did not fold, or anything else without a single answer, so a
/// caller can tell "this is 24px" from "this is not a length" without a second
/// parse.
[[nodiscard]] std::optional<float> length_text_to_px(std::string_view text,
                                                     const length_context & ctx);

/// The base value of `random()` for a set of sharing options in a context (CSS
/// Values 5 §random-caching): a number in [0, 1) that is the same every time
/// the same key asks. The key combines the options (a `--name`,
/// `element-scoped`, `property-index-scoped`) with the context's element,
/// property and position. Public because `random-item()` uses it too.
[[nodiscard]] double random_base(std::string_view options, const length_context & ctx);

/// A folded result as CSS text: `12px`, `50%`, `calc(50% + 12px)`, `90deg`,
/// `0.5s`, or for a number the bare number: `0.5`, `6`, `-8`.
///
/// An infinity or NaN keeps its calc() (CSS Values 4 §10.12), because
/// `infinity` and `NaN` are not <number-token>s: `opacity: infinity` is a
/// syntax error while `opacity: calc(infinity)` is a value. A dimension is
/// spelled `calc(NaN * 1px)`, since `NaNpx` is not a token either.
[[nodiscard]] std::string serialize_calc(const calc_result & value);
/// A bare <number> through the same rules: `0.5`, `12`, `calc(infinity)`.
[[nodiscard]] std::string serialize_number(double value);

/// A CRTP parser that reads a <calc-sum> as a linear function: CSS Values 4
/// §10.1's `+`/`-` between products and `*`/`/` between factors, kept linear
/// (a product has at most one side that is not a number, and nothing divides
/// by a length). calc-size()'s calculation is read this way by both style's
/// interpolation and layout, each with its own term type.
///
/// The deriving `Reader` supplies `factor()` (recursing into sum() for
/// parentheses) and, over its `Term` (which has a `number` flag for a bare
/// <number>), the static functions `number_of(t)`, `scale(t, by)` and
/// `add(t, sign, rhs)`.
template <class Reader, class Term> struct linear_sum {
    std::string_view text;
    std::string_view space; // The characters skip_space() skips.
    std::size_t at = 0;

    void skip_space() {
        while (at < text.size() && space.find(text[at]) != std::string_view::npos) { ++at; }
    }
    [[nodiscard]] std::optional<Term> sum() {
        using scalar = decltype(Reader::number_of(std::declval<const Term &>()));
        std::optional<Term> left = product();
        while (left) {
            skip_space();
            if (at >= text.size() || (text[at] != '+' && text[at] != '-')) { break; }
            const scalar sign = text[at] == '-' ? scalar{-1} : scalar{1};
            ++at;
            const std::optional<Term> right = product();
            if (!right || left->number != right->number) { return std::nullopt; }
            Reader::add(*left, sign, *right);
        }
        return left;
    }
    [[nodiscard]] std::optional<Term> product() {
        using scalar = decltype(Reader::number_of(std::declval<const Term &>()));
        Reader & self = static_cast<Reader &>(*this);
        std::optional<Term> left = self.factor();
        while (left) {
            skip_space();
            if (at >= text.size() || (text[at] != '*' && text[at] != '/')) { break; }
            const bool divide = text[at] == '/';
            ++at;
            std::optional<Term> right = self.factor();
            if (!right) { return std::nullopt; }
            if (divide) {
                if (!right->number || Reader::number_of(*right) == 0) { return std::nullopt; }
                Reader::scale(*left, scalar{1} / Reader::number_of(*right));
            } else if (right->number) {
                Reader::scale(*left, Reader::number_of(*right));
            } else if (left->number) {
                const scalar by = Reader::number_of(*left);
                left = std::move(right);
                Reader::scale(*left, by);
            } else {
                return std::nullopt;
            }
        }
        return left;
    }
};

} // namespace ctbrowser::style::css
