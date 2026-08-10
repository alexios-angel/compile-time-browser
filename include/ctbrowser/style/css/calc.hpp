#pragma once
#include <optional>
#include <string>
#include <string_view>

// `calc()`, and the unit table every relative length goes through.
//
// WHY THIS IS A RUNG OF ITS OWN. Bootstrap has 134 calc() expressions and until
// now none of them evaluated: the value reached layout as the text `calc(var(x) *
// .5)`, parse_length gave up on the leading `c`, and the property was silently
// nothing. `.container { padding: calc(var(--bs-gutter-x) * .5) }` is the one
// that mattered most - the parity report attributed a +12px shift on 27 of 40
// elements of the smallest fixture to it, because a missing horizontal padding
// moves every descendant and then everything is measured against the wrong basis.
//
// WHAT THE SHAPES ACTUALLY ARE. Counted over bootstrap.css rather than guessed:
// 34 are `-1 * var(x)` or `-.5 * var(x)`, 14 are `Nrem + Nvw`, 16 involve `em`,
// 7 are `var(x) - var(y)`, and exactly 2 carry a percentage. So the work is in
// multiplication by a number and in addition of two absolute lengths - which is
// to say, in having real bases for `rem`, `em` and `vw` rather than in the
// expression grammar. There is no min(), max() or clamp() anywhere in the file.
//
// THE PERCENTAGE CASE CANNOT FOLD, and pretending otherwise would be the wrong
// kind of simple: `calc(100% - 12px)` has no answer until a containing block
// exists, which is a used-value question and not a computed-value one. So a
// result carries an optional percentage alongside its pixels and serialises back
// to `calc(50% + 12px)` - the same canonical two-term form Chrome prints, which
// layout::parse_length knows how to read against its basis.

namespace ctbrowser::style::css {

// The bases a relative length resolves against. Everything here is a fact about
// the element or the window, and the reason they travel together is that a calc
// can mix them: `calc(1.375rem + 1.5vw)` is Bootstrap's fluid heading size and
// needs the root font size and the viewport in the same expression.
struct length_context {
    // The element's OWN font size, which is what `em` means everywhere except in
    // `font-size` itself - there it is the parent's, and the caller passes the
    // parent's when resolving that one property. That asymmetry is CSS's, not a
    // convenience: `font-size: 2em` doubles the inherited size rather than being
    // circular.
    float font_size = 16.0f;
    float root_font_size = 16.0f;
    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
};

// A length that may carry a percentage it could not resolve. `px` alone is the
// ordinary case; `has_percent` is the `calc(100% - 12px)` one.
struct calc_result {
    float px = 0.0f;
    float percent = 0.0f;
    bool has_percent = false;
};

// One dimension to pixels. `nullopt` for a unit this does not model, so a caller
// can leave the value alone rather than guess at it - which is the difference
// between an honest gap and a wrong number. An empty unit is a plain number and
// answers with itself, because that is what a calc term needs.
[[nodiscard]] std::optional<float> unit_to_px(float value, std::string_view unit,
                                              const length_context & ctx);

// Evaluate one expression - the inside of a calc(), or a whole `calc(...)`.
// `nullopt` when it is not valid arithmetic: mismatched types (`1px + 2`),
// multiplication of two lengths, division by a non-number, an unmodelled unit,
// or a missing operator. Chrome treats all of those as an invalid declaration
// and so does the caller.
[[nodiscard]] std::optional<calc_result> evaluate_calc(std::string_view expression,
                                                       const length_context & ctx);

// Every calc() in a declaration value, replaced by its answer. A value with no
// calc comes back unchanged, and so does one whose calc is invalid - leaving the
// text in place means the property is dropped later by whoever cannot read it,
// which is the same outcome as before this existed and never a wrong number.
[[nodiscard]] std::string fold_calc(std::string_view value, const length_context & ctx);

// Worth a look at all? A substring test, so a `--custom: calc-ish-name` costs
// one wasted parse and nothing else.
[[nodiscard]] bool may_have_calc(std::string_view value) noexcept;

// ONE already-folded length in text form to pixels: `12px`, `1.5rem`, `2em`, or a
// bare number. `nullopt` for a percentage, a keyword, a calc that did not fold, or
// anything else without a single answer - which is what lets a caller tell "this
// is 24px" from "this is not a length at all" without a second parse.
[[nodiscard]] std::optional<float> length_text_to_px(std::string_view text,
                                                     const length_context & ctx);

// Like length_text_to_px, but a BARE NUMBER IS NOT A LENGTH. That distinction is
// the difference between folding `padding: 1rem` to `16px` and destroying
// `line-height: 1.5` by calling it `1.5px`, so the two callers get two functions
// rather than a flag nobody remembers to pass.
[[nodiscard]] std::optional<float> dimension_text_to_px(std::string_view text,
                                                        const length_context & ctx);

// A folded result as CSS text: `12px`, `50%`, or `calc(50% + 12px)`.
[[nodiscard]] std::string serialize_calc(const calc_result & value);

} // namespace ctbrowser::style::css
