#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// `calc()`, and the unit table every relative length goes through.
//
// The comparison functions are CSS Values 4 §10.3 and they are the same recursive
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
    // THE LINE HEIGHT `lh` MEASURES, and the root's for `rlh` (CSS Values 4
    // §6.1.1). The same asymmetry as `em`: in `line-height` itself `lh` is the
    // parent's, everywhere else the element's own, and the caller passes the
    // right one. `normal` is 1.25 times the font size here, which is the factor
    // layout uses for it - not the font's metrics, which are injected and
    // pinned by the goldens.
    float line_height = 20.0f;
    float root_line_height = 20.0f;
    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
    // WHERE THE ELEMENT SITS AMONG ITS SIBLINGS, one-based, and how many there
    // are - what `sibling-index()` and `sibling-count()` answer (CSS Values 5
    // §tree-counting). Zero means "no element here", which is every context
    // but the cascade's, and leaves both functions unresolved.
    std::uint32_t sibling_index = 0;
    std::uint32_t sibling_count = 0;
    // WHAT A PERCENTAGE IS A PERCENTAGE OF, when the caller has it: the
    // containing block's width, at USED-value time. The cascade never sets it -
    // a computed value keeps `calc(50% + 12px)` - and getComputedStyle sets it
    // for a margin or a padding, whose resolved value CSSOM says is the used
    // one: `round(10%, 1px)` against a 75px block is `8px`, and no linear sum
    // can say so before the basis exists (round-mod-rem-computed,
    // signs-abs-computed, hypot-pow-sqrt-computed).
    std::optional<float> percent_basis;
    // WHAT `random()` IS RANDOM PER (CSS Values 5 §random-caching): a value
    // with no name is shared by nothing - it differs per element, per property
    // and per position in the value - and the caller says which element and
    // which property this is. `random_index` is the ordinal of this
    // expression's first random() among the value's, counted from zero in
    // source order by the fold as it walks the value; zero everywhere else.
    std::uint64_t element_key = 0;
    std::string_view property;
    std::uint32_t random_index = 0;
};

// WHICH OF CSS'S NUMERIC TYPES a math function came out as. CSS Values 4 §10.2
// gives calc() a type algebra over SIX base types, not two: `rotate: calc(10deg +
// 5deg)` and `transition-delay: calc(1s / 2)` are values.
//
// Each has ONE canonical unit and every member of the family converts to it, so
// a term carries a plain number and this tag rather than the author's unit. That
// is also what a computed value IS - CSS Values 4 §6.1 and §6.4 say an angle
// computes to `deg` and a time to `s`, however it was written.
enum class numeric_type : std::uint8_t {
    number,     // no unit at all
    length,     // canonical px
    angle,      // canonical deg
    time,       // canonical s
    frequency,  // canonical Hz
    resolution, // canonical dppx
    // `<flex>`, canonical fr. It has NO basis and never will have one here - a
    // flex is resolved by grid track sizing and by nothing else - but it is a
    // TYPE, and that is why it is in this list rather than left unresolved:
    // `min(1px, 0fr)` is not "a comparison this engine cannot decide", it is
    // `1px + 2` with different spelling, and `css/css-values` says so in six
    // files at once (`minmax-{length,number,percentage,time}-invalid`,
    // `exp-log-invalid`). Naming the type is what turns those from a value kept
    // verbatim into the syntax error they are.
    flex,
};

// A value that may carry a percentage it could not resolve. `px` alone is the
// ordinary case; `has_percent` is the `calc(100% - 12px)` one.
//
// `is_number` is the OTHER half of calc's type system: CSS Values 3 §8.1 says a
// math function resolves to a `<number>` as readily as to a `<length>`, so
// `opacity: calc(2 / 4)`, `z-index: calc(1 + 1)` and `rgb(calc(0), calc(255),
// calc(0))` are all valid. When `is_number` is set, `px` carries the number and
// the unit is nothing at all.
//
// `px` IS A DOUBLE and the name is a half-truth kept for its callers: it is the
// value in `type`'s canonical unit, which is pixels only when `type` is `length`.
// A double because `pow(20, 4)` and `3e+9px` both lose their last digits at 24
// bits of mantissa, and every intermediate of a trig or exponential function is
// worse.
struct calc_result {
    double px = 0.0;
    double percent = 0.0;
    bool has_percent = false;
    bool is_number = false;
    // The unit family `px` is measured in. `number` exactly when `is_number` is
    // set; the two are kept in step and both are published because callers that
    // predate the type algebra ask the boolean.
    numeric_type type = numeric_type::length;
};

// What came of one math function. THREE ANSWERS, NOT TWO, and the third is the
// one that makes min()/max()/clamp() safe to add at all:
//
//   resolved     a number came out - substitute it
//   unresolved   the expression is WELL FORMED and has no answer HERE.
//                `min(10px, 5%)` needs a containing block, which is a used-value
//                question; CSS Values 4 §10.11 says its computed value is the
//                function as written. So the text is kept and the declaration is
//                left alone.
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
    // HOW MANY random() FUNCTIONS THE EXPRESSION HOLDS, whatever the outcome,
    // so a caller walking a whole value can number the next one: the
    // automatic sharing key is the function's ordinal in the value
    // (`length_context::random_index`), and a calc() may hold several.
    std::uint32_t randoms = 0;
};

// WHAT KIND OF NUMBER THE PROPERTY WILL TAKE. A `<number>` answer is a valid
// value for `opacity` and a syntax error for `width`, and the evaluator cannot
// tell the two apart on its own - so the cascade, which knows the property,
// says: `width: calc(2 * 3)` is invalid, not `6px`.
enum class math_context : std::uint8_t {
    // A number and a length are both plausible somewhere in this value - a
    // colour channel, a font-feature axis, `opacity`, a transform. The default,
    // because guessing "length" for an unknown property would silently reject
    // values that are fine.
    any,
    // The whole value is a length, a percentage, or a list of them: a bare
    // number cannot appear in it and one that does is a syntax error.
    length,
    // The whole value is an `<integer>`. A math function may still answer with a
    // fraction - `z-index: calc(3 / 2)` is perfectly valid CSS - and CSS Values 4
    // §10.10 says the COMPUTED value rounds it, so this is the context that says
    // to. It is a context rather than a post-pass because rounding has to happen
    // once, at the end of the conversion: `calc(calc(1 / 3) * 3)` is 1, not 0.
    integer
};

// Which of the two a property is. Deliberately a SHORT list of properties whose
// entire value is lengths - a compound value like `box-shadow` or
// `background-position` gets `any`, because the context applies to every math
// function in the value and one of them may legitimately be a number.
[[nodiscard]] math_context math_context_of(std::string_view property) noexcept;

// ONE DIMENSION IN ITS CANONICAL UNIT, whatever family it belongs to: `1in` ->
// `96px`, `10ms` -> `0.01s`, `100grad` -> `90deg`, `96dpi` -> `1dppx`. `nullopt`
// for text that is not exactly one dimension, or one whose unit needs a basis
// this engine has no answer for.
//
// This is `length_text_to_px`'s sibling and NOT a replacement for it: a length
// is the only family the layout tree can use, so the caller that wants a number
// keeps asking for pixels. What this is for is the computed VALUE, which CSS Values 4
// §6.4 and §6.5 say is the canonical unit for every family - `transition-delay:
// 12ms` computes to `0.012s` and `rotate: 100grad` to `90deg` in every browser.
[[nodiscard]] std::optional<std::string> canonical_dimension_text(std::string_view text,
                                                                  const length_context & ctx);

// Evaluate one expression - the inside of a calc(), or a whole `calc(...)`,
// `min(...)`, `max(...)` or `clamp(...)`. The three outcomes are above.
[[nodiscard]] math_answer evaluate_math(std::string_view expression, const length_context & ctx);

// THE TYPE OF A WELL-FORMED MATH FUNCTION THAT HAS NO ANSWER YET. `calc(1px *
// sibling-index())` is unresolved everywhere but in the cascade, and is a
// <length> all the same: CSS Values 4 §10.2 types the expression before anything
// is measured, which is what lets `rotate` refuse it on sight. The answer is a
// `calc_result` whose magnitudes are left at zero - only `type`, `is_number` and
// `has_percent` mean anything - and `nullopt` for an expression whose type
// cannot be settled without a basis, such as `min(10px, 5%)`.
[[nodiscard]] std::optional<calc_result> math_type_of(std::string_view expression);

// calc-size( <calc-size-basis>, <calc-sum> ), CSS Values 5 §calc-size, AS A
// SPECIFIED VALUE: the basis canonical - a keyword, a nested calc-size() or a
// <length-percentage> - and the calculation simplified with `size` as a term
// of its own, so `size * 2` is `2 * size`. `keywords` are the property's own
// size keywords (`auto` for width, `none` for max-width); the intrinsic ones
// and `any` are always a basis, and `size` may not be used over `any`.
// `nullopt` when `value` is not one valid calc-size() and nothing else
// (calc-size-parsing).
[[nodiscard]] std::optional<std::string> calc_size_text(std::string_view value,
                                                        std::string_view keywords);

// EVERY random() IN A SPECIFIED VALUE, ITS KEY SPELLED OUT (CSS Values 5
// §random-caching, as random-serialize reads it): the sharing words become
// the `<dashed-ident>`, `element-scoped` and UA-ident triple `random_base`
// keys on - `random(0px, 100px)` in `width` is `random(element-scoped
// ua-width-1, 0px, 100px)`, `property-scoped` is `ua-width` - and the
// bounds take their canonical units. `fixed` keeps its number.
[[nodiscard]] std::string canonical_random(std::string_view value, std::string_view property);

// A folded value, and whether every calc() in it actually evaluated.
//
// The flag is not a nicety. `margin-top: calc(-1 * var(--bs-gutter-y))` with a
// gutter of `0` multiplies a number by a number and gets a NUMBER, which is not a
// length - so the declaration is invalid and `margin-top` takes its initial 0,
// which is what Chrome reports. Leaving the text in place would hand layout
// `calc(-1 * 0)`, which it reads as `auto`.
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
// `clamp()` either fold or keep their text with `ok` intact: `min(10px, 5%)` is a
// perfectly valid declaration whose computed value IS the function as written.
[[nodiscard]] folded_value fold_math(std::string_view value, const length_context & ctx,
                                     math_context accepts = math_context::any);

// A FOLDED VALUE CLAMPED TO ZERO FROM BELOW. CSS Values 4 §10.10: a math
// function's result outside the property's range is clamped at computed-value
// time, so `tab-size: calc(2 * -4)` is 0 where a literal `-8` is a syntax
// error. The property table says which properties are non-negative; this only
// knows what a folded value looks like. A lone negative number, dimension or
// percentage becomes the zero of its unit; anything else is handed back as it
// came.
[[nodiscard]] std::string non_negative(std::string_view folded);

// Worth a look at all? A substring test for the math function names, so a
// `--custom: calc-ish-name` costs one wasted parse and nothing else.
[[nodiscard]] bool may_have_math(std::string_view value) noexcept;

// EVERY MATH FUNCTION IN A SPECIFIED VALUE, REPLACED BY ITS SIMPLIFIED FORM.
// CSS Values 4 §10.12, and the words that matter are "every" and "specified".
//
// EVERY: a math function is simplified WHEREVER IT SITS, not only when it is the
// whole value. `transform: rotate(acos(1))` is `rotate(calc(0deg))` and
// `background-image: image-set(url("") calc(1x * NaN))` is `image-set(url("")
// calc(NaN * 1dppx))` in every browser. The corpus tests these functions through
// `transform`, `background-image` and `scale`, which are properties whose grammar
// this engine does not model at all - so the simplification has to be independent
// of it.
//
// SPECIFIED: the answer keeps a `calc()` around it, because that is what
// distinguishes `width: calc(96px)` from `width: 96px` after the fact.
// `serialize_calc` writes the COMPUTED form, where a bare `96px` is the whole
// of it. And a `calc()` whose entire body is one other math function loses that
// redundant layer: `calc(clamp(1px, 1em, 1vh))` is `clamp(1px, 1em, 1vh)`.
//
// A FUNCTION THAT CANNOT BE SIMPLIFIED HERE KEEPS THE AUTHOR'S BYTES. There are
// no bases at specified-value time, so anything mentioning `em`, `vw`, `lh`,
// `cqw`, `fr` or a percentage is left exactly as written - §10.11 says
// `calc(10px + 1em)` keeps both terms - as is a function this file cannot
// evaluate and one whose comparison has no answer until layout.
[[nodiscard]] std::string simplify_math(std::string_view value);

// IS EVERY MATH FUNCTION IN THIS VALUE WELL FORMED? Not "does it fold" - a
// `min(10px, 5%)` has no answer until layout and is perfectly well formed - but
// "would a browser drop the declaration on sight". `round(nearest, 1px)` is
// missing its step, `calc(7px * up)` multiplies by a keyword and
// `rotate(calc((0.25turn error)))` has two values where one belongs; all three
// are in `css/css-values` as `test_invalid_value`.
//
// It looks INSIDE other functions, which the fold deliberately does not have to:
// the malformed calc above is an argument of `rotate()`, and `transform` is a
// property whose grammar this engine does not model at all.
//
// A math function is checked only if this file IMPLEMENTS it. `calc-size()` and
// anything else named in the specification and not here is left alone, because
// "I cannot parse it" and "it is invalid" are different answers and only the
// second one may delete a declaration.
[[nodiscard]] bool math_syntax_ok(std::string_view value);

// IS THERE A PERCENTAGE INSIDE A MATH FUNCTION HERE? Not a percentage anywhere -
// `hsl(calc(1deg) 82% 43%)` writes two that are channels and not lengths - but
// one that a calculation would have to resolve.
//
// The question belongs to the CALLER, because the answer does: §10.11's
// calculation context is the property's, so `text-indent: min(1px, 0%)`
// resolves against a containing block and `border-left-width: min(1px, 0%)` has
// nothing to resolve against and is a syntax error. This file knows where the
// math functions are and the property table knows which properties take a
// percentage; neither can answer alone.
[[nodiscard]] bool math_uses_percentage(std::string_view value);

// ONE already-folded length in text form to pixels: `12px`, `1.5rem`, `2em`, or a
// bare number. `nullopt` for a percentage, a keyword, a calc that did not fold, or
// anything else without a single answer - which is what lets a caller tell "this
// is 24px" from "this is not a length at all" without a second parse.
[[nodiscard]] std::optional<float> length_text_to_px(std::string_view text,
                                                     const length_context & ctx);

// `random()`'S BASE for a set of sharing options against a context, CSS
// Values 5 §random-caching: a number in [0, 1) that is the same every time the
// same key asks. The key is the options' - a `--name`, `element-scoped`,
// `property-index-scoped` - over the context's element, property and
// position. Public because `random-item()` shares it.
[[nodiscard]] double random_base(std::string_view options, const length_context & ctx);

// A folded result as CSS text: `12px`, `50%`, `calc(50% + 12px)`, `90deg`,
// `0.5s` - or, for a number answer, the bare number with no unit at all: `0.5`,
// `6`, `-8`.
//
// AN INFINITY OR A NaN KEEPS ITS calc(). CSS Values 4 §10.12 says so and it is
// not a formality: `infinity` and `NaN` are not <number-token>s, so `opacity:
// infinity` is a syntax error while `opacity: calc(infinity)` is a value. The
// dimensioned form is the specification's own spelling too - `calc(NaN * 1px)`,
// because `NaNpx` is not a token either.
[[nodiscard]] std::string serialize_calc(const calc_result & value);

} // namespace ctbrowser::style::css
