#pragma once
#include <cstdint>
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
// THEY ARE HERE ANYWAY, since 2026-09-03, and Bootstrap is the reason the
// omission lasted: a corpus of one says nothing about the rest of the web, and
// `width: clamp(1rem, 2vw, 3rem)` is ordinary CSS that this front end silently
// handed to layout as text it cannot read - which is a zero, not a gap. The
// comparison functions are CSS Values 4 §10.3 and they are the same recursive
// descent as the rest, with one addition: an argument list, and a third outcome
// for the case a comparison genuinely cannot be decided here (see math_outcome).
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
//
// `is_number` is the OTHER half of calc's type system, and leaving it out was a
// defect rather than a simplification: CSS Values 3 §8.1 says a math function
// resolves to a `<number>` as readily as to a `<length>`, so `opacity: calc(2 /
// 4)`, `z-index: calc(1 + 1)`, `tab-size: calc(2 * 3)` and `rgb(calc(0),
// calc(255), calc(0))` are all valid - and every one of them was DROPPED by this
// engine, because the evaluator answered "not a length" and the cascade read
// that as "not a value". When `is_number` is set, `px` carries the number and
// the unit is nothing at all.
struct calc_result {
    float px = 0.0f;
    float percent = 0.0f;
    bool has_percent = false;
    bool is_number = false;
};

// What came of one math function. THREE ANSWERS, NOT TWO, and the third is the
// one that makes min()/max()/clamp() safe to add at all:
//
//   resolved     a number came out - substitute it
//   unresolved   the expression is WELL FORMED and has no answer HERE.
//                `min(10px, 5%)` needs a containing block, which is a used-value
//                question; CSS Values 4 §10.11 says its computed value is the
//                function as written. So the text is kept and the declaration is
//                left alone - which is also exactly what this engine did before
//                it could parse the function at all, so nothing can regress.
//   invalid      not arithmetic: `1px + 2`, `2px * 3px`, an unmodelled unit, a
//                missing operator. The declaration is invalid and the cascade
//                drops it.
enum class math_outcome : std::uint8_t {
    resolved,
    unresolved,
    invalid
};

struct math_answer {
    math_outcome outcome = math_outcome::invalid;
    calc_result value;
};

// WHAT KIND OF NUMBER THE PROPERTY WILL TAKE. A `<number>` answer is a valid
// value for `opacity` and a syntax error for `width`, and the evaluator cannot
// tell the two apart on its own - so the cascade, which knows the property,
// says. Without this, teaching calc() to answer with a number would have made
// `width: calc(2 * 3)` mean `6px`, which is neither what CSS says (invalid) nor
// what this engine did before (invalid).
enum class math_context : std::uint8_t {
    // A number and a length are both plausible somewhere in this value - a
    // colour channel, a font-feature axis, `opacity`, a transform. The default,
    // because guessing "length" for an unknown property would silently reject
    // values that are fine.
    any,
    // The whole value is a length, a percentage, or a list of them: a bare
    // number cannot appear in it and one that does is a syntax error.
    length
};

// Which of the two a property is. Deliberately a SHORT list of properties whose
// entire value is lengths - a compound value like `box-shadow` or
// `background-position` gets `any`, because the context applies to every math
// function in the value and one of them may legitimately be a number.
[[nodiscard]] math_context math_context_of(std::string_view property) noexcept;

// One dimension to pixels. `nullopt` for a unit this does not model, so a caller
// can leave the value alone rather than guess at it - which is the difference
// between an honest gap and a wrong number. An empty unit is a plain number and
// answers with itself, because that is what a calc term needs.
[[nodiscard]] std::optional<float> unit_to_px(float value, std::string_view unit,
                                              const length_context & ctx);

// Evaluate one expression - the inside of a calc(), or a whole `calc(...)`,
// `min(...)`, `max(...)` or `clamp(...)`. The three outcomes are above.
[[nodiscard]] math_answer evaluate_math(std::string_view expression, const length_context & ctx);

// The length-only view of evaluate_math, kept because most callers want exactly
// that: `nullopt` for a number, for an unresolved comparison, and for anything
// invalid. NOT the primitive - a caller that has to tell "not a length" from
// "not a value" must ask evaluate_math, and confusing the two is the defect
// `is_number` exists to fix.
[[nodiscard]] std::optional<calc_result> evaluate_calc(std::string_view expression,
                                                       const length_context & ctx);

// A folded value, and whether every calc() in it actually evaluated.
//
// The flag is not a nicety. `.row { margin-top: calc(-1 * var(--bs-gutter-y)) }`
// with a gutter of `0` multiplies a number by a number and gets a NUMBER, which
// is not a length - so the declaration is invalid and `margin-top` takes its
// initial 0, which is what Chrome reports. Leaving the text in place instead put
// the string `calc(-1 * 0)` in front of layout, whose parse_length cannot read it
// and answers `auto`; that was 24 differences on one fixture, and `auto` is a
// worse answer than nothing because nothing at least means "initial value".
struct folded_value {
    std::string text;
    bool ok = true;
};

// Every math function in a declaration value, replaced by its answer. A value
// with none comes back unchanged and `ok`. One whose calc() did not evaluate
// comes back with its text UNCHANGED and `ok` false, so a caller that has no
// better answer can still use the text and one that does can drop the
// declaration.
//
// A COMPARISON FUNCTION NEVER MAKES A DECLARATION INVALID. `min()`, `max()` and
// `clamp()` either fold or keep their text with `ok` intact, because before this
// engine could read them at all they were kept verbatim - so "leave it alone" is
// the one answer that cannot be a regression, and `min(10px, 5%)` is a perfectly
// valid declaration whose computed value IS the function as written.
[[nodiscard]] folded_value fold_math(std::string_view value, const length_context & ctx,
                                     math_context accepts = math_context::any);

// Worth a look at all? A substring test for the four function names, so a
// `--custom: calc-ish-name` costs one wasted parse and nothing else.
[[nodiscard]] bool may_have_math(std::string_view value) noexcept;

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

// A folded result as CSS text: `12px`, `50%`, `calc(50% + 12px)` - or, for a
// number answer, the bare number with no unit at all: `0.5`, `6`, `-8`.
[[nodiscard]] std::string serialize_calc(const calc_result & value);

} // namespace ctbrowser::style::css
