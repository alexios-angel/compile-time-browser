//===- ctbrowser/unittests/unit/css/values/math.hpp - Math function tests -===//
//
// CSS math function tests (calc(), min(), clamp(), the CSS Values 4 §10
// functions and CSS Values 5's progress() and calc-size()), included by
// css/values.cpp, which runs them. ok() and bad() come from support.hpp. Bare
// § numbers are CSS Values 4; names like `calc-unit-analysis` are the WPT
// css/css-values files each case comes from.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support.hpp"

namespace {

void test_what_a_math_function_may_not_be() {
    bad("width", "round()"); // Verbatim from a WPT case,
    bad("width", "min()");   // and its two siblings.
    bad("width", "calc()");
    bad("width", "calc(1px) calc(2px)"); // Two values where one belongs.
    bad("width", "notafunction(1px)");
    bad("display", "calc(1px)"); // A keyword-only property takes no math at all.
    // An unterminated function is closed by EOF (CSS Syntax 3 §5.4.9), so these
    // are values, not errors (`minmax-length-computed` expects 40px from the
    // second). The paren EOF added belongs in the serialisation
    // (`calc-complex-unresolved-serialize`).
    ok("width", "calc(1px", "calc(1px)");
    ok("width", "calc(min(1em, 21px) * 2", "calc(2 * min(1em, 21px))"); // §10.13: numbers first.

    // The arity is part of the grammar (`round-mod-rem-invalid`,
    // `calc-invalid-parsing`): a step is required and a fourth argument is not
    // allowed.
    bad("width", "round(nearest, 1px)");
    bad("width", "round(nearest, 1px, 1px, 1px)");
    bad("width", "clamp(1px, 2px)");
    bad("width", "mod(1px)");
    ok("width", "round(nearest, 10px, 6px)", "calc(12px)");

    // Either of clamp()'s bounds may be `none` (CSS Values 4 §10.3), and an
    // absent bound is an unbounded side rather than a missing argument:
    // `clamp(none, 33px, 30px)` is `min(33px, 30px)` (`clamp-length-serialize`).
    // The middle argument is still required.
    ok("width", "clamp(none, 33px, 30px)", "calc(30px)");
    ok("width", "clamp(33px, 30px, none)", "calc(33px)");
    ok("width", "clamp(none, 30px, none)", "calc(30px)");
    bad("width", "clamp(none, none, none)");
    bad("width", "clamp(1px, none, 2px)");

    // The type algebra is part of the grammar too (CSS Values 4 §10.2). Every
    // one of these is `calc-unit-analysis` verbatim.
    bad("margin-left", "calc(0)");       // A unitless zero in a calc is a number.
    bad("margin-left", "calc(1px + 2)"); // A length plus a number.
    bad("margin-left", "calc(2 + 1px)");
    bad("margin-left", "calc(1px - 2)");
    bad("margin-left", "calc(2 - 1px)");
    bad("margin-left", "calc(2px * 1px)");    // An area, which no property takes.
    bad("margin-left", "calc(20 / 0.75rem)"); // Division by a non-number.
    ok("margin-left", "calc(0px)", "calc(0px)");
    ok("margin-left", "calc(2px * 2)", "calc(4px)");
    ok("margin-left", "calc(2 * 2px)", "calc(4px)");
    // The same rule the other way round: a math function is refused where its
    // resolved type is not a value for the property, in the CSSOM as in the
    // cascade (`width: calc(2 * 3)`).
    bad("width", "calc(2 * 3)");
    bad("rotate", "calc(1s)");
    bad("transition-duration", "calc(1px)");
    bad("border-left-width", "calc(10%)"); // A <length>, not a <length-percentage>.
    ok("text-indent", "calc(10%)", "calc(10%)");
    ok("letter-spacing", "calc(100%)", "calc(100%)"); // CSS Text 4 allows a percentage here.
    ok("rotate", "calc(45deg + 45deg)", "calc(90deg)");
    ok("transition-duration", "calc(1s / 2)", "calc(0.5s)");
    ok("opacity", "calc(2 / 4)", "calc(0.5)");
    ok("z-index", "calc(1 + 1)", "calc(2)");

    // A `<flex>` is a type of its own, not an unresolved length: only grid track
    // sizing resolves `fr`, so `min(1px, 0fr)` mixes types just as `1px + 2`
    // does (`minmax-*-invalid`, `exp-log-invalid`).
    bad("width", "min(0fr)");
    bad("width", "min(1px, 0fr)");
    bad("opacity", "max(1, 0fr)");
    bad("opacity", "exp(0fr)");
    bad("transition-delay", "min(1s, 0fr)");
    bad("margin-left", "calc(1px + 1fr)");
    // Two flexes add up, but their sum is still not a length.
    bad("margin-left", "calc(1fr + 1fr)");

    // A bad calc anywhere in the value is refused, not only when it is the whole
    // value: `transform` has no grammar in this table, and WPT still expects the
    // declaration refused.
    bad("transform", "rotate(calc((0.25turn error)))");
    bad("width", "calc([])");
    bad("width", "calc(7px * up)");
}

/// CSS Values 4 §10.9's numeric constants and the §10.4-§10.8 functions.
/// `infinity` and `NaN` are keywords valid only inside a math function, not
/// number tokens.
void test_the_rest_of_the_math_functions() {
    ok("opacity", "calc(infinity)", "calc(infinity)");
    ok("opacity", "calc(NaN)", "calc(NaN)");
    ok("opacity", "calc(pi)", "calc(3.141593)");
    ok("opacity", "calc(e)", "calc(2.718282)");
    // A division by zero is an infinity, not a syntax error (§10.9), and an
    // infinity keeps its calc() and moves the unit to a multiplier, because
    // `infinitypx` is not a token.
    ok("width", "calc(100px / 0)", "calc(infinity * 1px)");
    ok("opacity", "calc(0 / 0)", "calc(NaN)");
    ok("width", "abs(-10px)", "calc(10px)");
    ok("z-index", "sign(1em - 1px)", "sign(1em - 1px)"); // A length's sign is a number.
    ok("width", "hypot(3px, 4px)", "calc(5px)");
    ok("width", "calc(1px * pow(2, 3))", "calc(8px)");
    ok("opacity", "sqrt(4)", "calc(2)");
    ok("opacity", "log(10, 10)", "calc(1)");
    ok("opacity", "exp(0)", "calc(1)");
    ok("opacity", "cos(0)", "calc(1)");          // An angle in, a number out;
    ok("rotate", "atan2(1, 1)", "calc(45deg)");  // a number in, an angle out.
    ok("rotate", "calc(1turn)", "calc(360deg)"); // Canonical units, CSS Values 4 §6.5.
    ok("transition-duration", "calc(250ms)", "calc(0.25s)");
    ok("width", "mod(10px, 6px)", "calc(4px)");
    ok("width", "rem(10px, 6px)", "calc(4px)");
    ok("width", "round(up, 101px, 10px)", "calc(110px)");
    ok("width", "round(down, 106px, 10px)", "calc(100px)");
    ok("width", "round(to-zero, 105px, 10px)", "calc(100px)");
    // `mod` takes the sign of the divisor and `rem` the sign of the dividend,
    // which is the whole difference between them (§10.6).
    ok("opacity", "mod(-18, 5)", "calc(2)");
    ok("opacity", "rem(-18, 5)", "calc(-3)");
    // progress( [no-clamp]? A, B, C ), CSS Values 5. Three arguments of one type
    // and a <number> out. An empty range is neither an error nor a NaN: the
    // unclamped form keeps the numerator's sign and the clamped one is nought
    // whichever way it points, because there is no range to be anywhere in.
    ok("opacity", "progress(100px, 0px, 100px)", "calc(1)");
    ok("opacity", "progress(1%, (10% - 10%), 100%)", "calc(0.01)"); // A ratio of two of them.
    ok("opacity", "progress(-100px, 0px, 100px)", "calc(0)");       // Clamped into [0, 1].
    ok("opacity", "progress(no-clamp -100px, 0px, 100px)", "calc(-1)");
    ok("opacity", "progress(2rad, 1rad, 1rad)", "calc(0)");
    ok("opacity", "progress(no-clamp 2rad, 1rad, 1rad)", "calc(infinity)");
    ok("opacity", "progress(no-clamp 1rad, 1rad, 1rad)", "calc(0)");
    ok("opacity", "progress(no-clamp 0rad, 1rad, 1rad)", "calc(-infinity)");
    ok("opacity", "progress(10em, 0px, 10em)", "progress(10em, 0px, 10em)"); // No basis yet.
    bad("opacity", "progress(1)");
    bad("opacity", "progress(0, 1,)");
    bad("opacity", "progress(no-clamp, 1, 0 1)");
    bad("opacity", "progress(1 no-clamp, 0, 1)");
    bad("opacity", "progress(5, 0deg, 8deg)");
    // A mixed percentage is a type error here, not an undecidable comparison:
    // three arguments that do not agree on what they measure have no ratio.
    bad("opacity", "progress(5%, 0px, 10px)");
    // An area is a type progress() does not take: three that agree on being
    // <length>^2 still have nothing progress() can take.
    bad("opacity", "progress(10px * 10px, 10px * 10px, 10px * 10px)");
    bad("opacity", "progress(10px, 10px * 10px, 10px * 10px)");
    bad("letter-spacing", "calc(1px * progress(10deg, 0, 10))");
    // A constant is not a value on its own: `infinity` and `NaN` are not
    // <number-token>s, so §10.9 makes them keywords usable only inside a math
    // function. This asks `opacity` rather than `scale` because `scale` is
    // `freeform` in the property table, and a free-form property accepts any
    // value.
    bad("opacity", "infinity");
    bad("width", "sign(1px)"); // A number where the property's value is a length.

    // A function this engine cannot evaluate is not an error: WPT
    // `css/css-values/tree-counting/` expects every one of these to be valid.
    ok("left", "calc(1px * sibling-index())", "calc(1px * sibling-index())");
    // Its type is known before its value is (§10.2), though, and a length is no
    // more a rotation for being unresolved (calc-sibling-function-parsing).
    bad("left", "calc(10 * sibling-index())");
    bad("rotate", "calc(1px * sibling-index())");
    bad("rotate", "calc(1s * sibling-index())");
    bad("opacity", "calc(1ms * sibling-index())");
    bad("animation-duration", "calc(1deg * sibling-index())");
    ok("left", "calc(inherit(--x) + 1px)", "calc(inherit(--x) + 1px)");
    // The blocks EOF closed are written out, or the serialised declaration
    // swallows the one after it (attr-all-types 59-61).
    ok("background-color", "attr(data-foo type(<color>)", "attr(data-foo type(<color>))");
    ok("width", "attr(data-foo type(<length> | <percentage>)",
       "attr(data-foo type(<length> | <percentage>))");
    ok("width", "calc-size(10px, sign(size) * size)", "calc-size(10px, sign(size) * size)");
    // A calc-size() inside another math function is a syntax error
    // (CSS Values 5 §calc-size, calc-size-parsing).
    bad("width", "calc(calc-size(auto, size))");
    bad("width", "min(calc-size(auto, 0px), calc-size(auto, size))");
    // A unit with no basis keeps its term, and its term keeps its place. CSS
    // Values 4 §10.11 does not resolve `1em` or `5%` here, so both survive to
    // the specified value; §10.13 says what order they survive in.
    ok("width", "calc(1em + 10px)", "calc(1em + 10px)");
    ok("width", "calc(100% - 10px)", "calc(100% - 10px)");
    // A unit the specification names and this engine has no basis for is
    // the same case: `1cqw` needs a container and `1lh` a line box.
    ok("width", "calc(1px + 3cqw)", "calc(3cqw + 1px)");
    ok("width", "calc(1px + 1lh)", "calc(1lh + 1px)");
    bad("width", "calc(1px + 1nonsense)"); // A typo is not a unit.
}

/// CSS Values 4 §10.12: a math function's specified value is its simplified
/// form, wherever it sits, including inside properties this table has no
/// grammar for (`transform`, `background-image`, `scale`). The WPT
/// css-values *-serialize files test every function this way.
void test_a_math_function_is_simplified_wherever_it_sits() {
    // Inside a function of a property with no grammar here at all.
    ok("transform", "rotate(acos(1))", "rotate(calc(0deg))");
    ok("transform", "rotate(calc(1deg * NaN))", "rotate(calc(NaN * 1deg))");
    ok("transform", "scale(min(.3, .2, .1))", "scale(calc(0.1))");
    ok("transform", "translate(calc(1px + 2px), calc(2px * 2))", "translate(calc(3px), calc(4px))");
    ok("scale", "calc(sin(30deg) + cos(60deg))", "calc(1)");
    // Beside other values, and beside a quoted string whose parentheses must not
    // end the expression early.
    ok("border", "calc(calc(10px)) solid pink", "calc(10px) solid pink");
    ok("background-image", "image-set(url(\"a)b\") calc(1x * 2))",
       "image-set(url(\"a)b\") calc(2dppx))");

    // A calc() around one other calc() is redundant and loses exactly one layer,
    // which is what §10.12's simplification does with a lone child.
    ok("margin-top", "calc(calc(0px + clamp(1px, 1em, 1vh)))", "calc(0px + clamp(1px, 1em, 1vh))");
    ok("width", "calc(calc(calc(10px)))", "calc(10px)");
    // So is a calc() around a lone comparison. `min()`, `max()` and `clamp()`
    // are nodes of the calculation tree (§10.9), so simplifying
    // `calc(clamp(a, b, c))` leaves a tree whose root is the Clamp node, and
    // §10.13 serialises a Clamp root as `clamp(...)`, with no calc() anywhere.
    // `clamp-length-serialize` runs every one of its values twice, bare and
    // wrapped in one more calc(), and asserts the two serialise the same.
    ok("margin-top", "calc(clamp(1px, 1em, 1vh))", "clamp(1px, 1em, 1vh)");
    ok("margin-top", "calc(clamp(-18px, 3vw, -3vw))", "clamp(-18px, 3vw, -3vw)");
    ok("margin-top", "calc(min(10px, 5%))", "min(10px, 5%)");
    ok("margin-top", "calc(clamp(30px, 100px, 20px))", "calc(30px)");
    // A calc() around any other math function is not redundant. `pow()` is not
    // a node type but a leaf this engine could not evaluate, and a leaf inside a
    // calc() keeps its calc() (`calc-complex-unresolved-serialize`).
    ok("orphans", "calc(pow(2, sign(1em - 18px)))", "calc(pow(2, sign(1em - 18px)))");
    ok("orphans", "calc(pow(2, sibling-index())", "calc(pow(2, sibling-index()))");
    // A comparison that is not alone inside the calc keeps it too: the root is
    // then a sum, not a Clamp.
    ok("margin-top", "calc(0px + clamp(1px, 1em, 1vh))", "calc(0px + clamp(1px, 1em, 1vh))");
    ok("width", "calc(min(1em, 21px) * 2", "calc(2 * min(1em, 21px))"); // §10.13: numbers first.

    // Everything else keeps the author's text: a function with no answer until
    // layout, one whose units have no basis yet, and one this engine cannot
    // evaluate.
    ok("transform", "translate(min(10px, 5%))", "translate(min(10px, 5%))");
    ok("transform", "rotate(calc(1deg * sibling-index()))", "rotate(calc(1deg * sibling-index()))");
    ok("transform", "scale(calc(1 * sibling-index()))", "scale(sibling-index())");
    // calc-sibling-function-parsing: the function is a term of its own, and
    // what multiplies it folds around it.
    ok("rotate", "calc(1turn * sibling-count())", "calc(360deg * sibling-count())");
    ok("rotate", "calc(sibling-index() * 2rad * pi)", "calc(360deg * sibling-index())");
    ok("z-index", "calc(sibling-index())", "sibling-index()");
    ok("animation-duration", "calc(100ms * sibling-count())", "calc(0.1s * sibling-count())");
    ok("left", "calc(10px * sibling-index() + 10%)", "calc(10% + (10px * sibling-index()))");
    // A function over one is not applied to nought (hypot-pow-sqrt-computed),
    // nor is a random() drawn before computed-value time (random-serialize).
    ok("margin-left", "calc(1px * sqrt(sibling-index()))", "calc(1px * sqrt(sibling-index()))");
    ok("width", "calc(2 * random(--foo, 0px, 100px))", "calc(2 * random(--foo, 0px, 100px))");
    // A relative colour's channel keywords are values inside its math, and
    // nowhere else (CSS Color 5 §relative-colors, random-serialize).
    ok("color", "rgb(from red calc(r + 30) g b)", "rgb(from red calc(30 + r) g b)");
    bad("color", "rgb(calc(r + 1) 0 0)");
    ok("width", "calc-size(10px, sign(size) * size)", "calc-size(10px, sign(size) * size)");
    // A calc-size() inside another math function is a syntax error
    // (CSS Values 5 §calc-size, calc-size-parsing).
    bad("width", "calc(calc-size(auto, size))");
    bad("width", "min(calc-size(auto, 0px), calc-size(auto, size))");
    ok("font-family", "\"calc(1px + 1px)\"", "\"calc(1px + 1px)\""); // Inside a string.
}

/// A percentage term simplifies, but a comparison between percentages cannot
/// be decided without the basis.
void test_the_percentage_half_of_simplification() {
    // The value model carries a percentage beside the pixels rather than
    // resolving it, so these need no basis and nothing is guessed.
    ok("left", "calc(50px + calc(40%))", "calc(40% + 50px)");
    ok("width", "calc(100% * 0.5)", "calc(50%)");
    ok("text-indent", "min(1% + 1px)", "calc(1% + 1px)"); // One argument is not a comparison.
    // An absent component is not a zero one: `10%` has no length part, and
    // treating it as 0px would make `1% * infinity` produce a NaN length
    // (IEEE 0 * infinity). A zero the author wrote does produce NaN.
    ok("width", "calc(1% * infinity)", "calc(infinity * 1%)");
    ok("width", "calc(1% * NaN)", "calc(NaN * 1%)");
    ok("width", "calc(1% / 0)", "calc(infinity * 1%)");
    ok("width", "calc(0px * infinity)", "calc(NaN * 1px)");

    // The comparison half: `min(1%, 2%)` looks decidable and is not, because a
    // percentage resolves against a basis that may be negative, and then 2% is
    // the smaller. `minmax-percentage-serialize` asks for both functions back.
    ok("text-indent", "min(1%, 2%)", "min(1%, 2%)");
    ok("text-indent", "max(3%, 4%)", "max(3%, 4%)");
    ok("text-indent", "clamp(1%, 2%, 3%)", "clamp(1%, 2%, 3%)");
    ok("text-indent", "min(10px, 5%)", "min(10px, 5%)"); // The mixed case too.
}

/// A percentage has to be a percentage of something (CSS Values 4 §10.11). The
/// only calculation context this engine supplies is a length (no property
/// resolves a percentage into an angle or a time), so an expression that
/// answers with one of those and mentions a percentage is a syntax error, not a
/// value waiting for layout (`percentage-without-context`).
void test_a_percentage_needs_a_context() {
    bad("transform", "rotate(calc(sign(50%) * 1deg))");
    bad("filter", "hue-rotate(calc(sign(50%) * 1deg))");
    bad("font-style", "oblique calc(sign(50%) * 1deg)");
    bad("color", "hsl(calc(sign(50%) * 1deg) 82% 43%)");
    bad("animation-duration", "calc(sign(50%) * 1s)");
    bad("transition-delay", "calc(sign(50%) * 1s)");
    // Free-form properties, which accept any value otherwise, still type-check
    // the math inside them (percentage-without-context).
    bad("offset-rotate", "calc(sign(50%) * 1deg)");
    bad("offset-path", "ray(calc(sign(50%) * 1deg))");
    ok("offset-rotate", "calc(45deg + 45deg)", "calc(90deg)"); // It is simplified too.

    // A <number> answer is not covered by this rule: there the percentage
    // sits in a length context that the property does supply.
    ok("width", "calc(1px * pow(tan(atan2(50%, 1px)), 1))",
       "calc(1px * pow(tan(atan2(50%, 1px)), 1))");
    ok("width", "calc(50% + 1px)", "calc(50% + 1px)");
}

/// A math function is typed by where it sits, and `rotate()` is the one position
/// this table can check without a grammar for `transform` or `filter`: a
/// rotation by a length, by a number, or by a ratio with a percentage in it is
/// refused (`minmax-angle-invalid`, `sin-cos-tan-invalid`,
/// `acos-asin-atan-atan2-invalid`).
void test_the_angle_functions_take_an_angle() {
    bad("transform", "rotate(min(0px))");
    bad("transform", "rotate(min(0))");
    bad("transform", "rotate(max(0fr))");
    bad("transform", "rotate(tan(45deg ))"); // tan() answers with a <number>.
    bad("transform", "rotate(atan2(90px, 100%))");
    bad("transform", "skew(min(1px), 45deg)");
    bad("filter", "hue-rotate(min(1px))");

    // Only a math function is judged, because of <zero>: CSS Transforms 1 spells
    // it `rotate( [ <angle> | <zero> ] )`, so a literal `0` is a rotation,
    // written as the angle it is (transform-valid).
    ok("transform", "rotate(0)", "rotate(0deg)");
    ok("transform", "rotate(45deg)", "rotate(45deg)");
    ok("transform", "rotate(calc(45deg + 45deg))", "rotate(calc(90deg))");
    ok("transform", "rotate(atan2(1, 1))", "rotate(calc(45deg))");
    ok("filter", "hue-rotate(90deg)", "hue-rotate(90deg)");
    // A function with no answer here is not a function with a wrong type;
    // `1deg + 1cqw` would be one, the container units resolving now.
    ok("transform", "rotate(calc(1deg * sibling-index()))", "rotate(calc(1deg * sibling-index()))");
    // Nothing outside the angle-only functions is touched by the rule.
    ok("transform", "translate(min(10px, 5%))", "translate(min(10px, 5%))");
}

/// A sum that could not be folded is still simplified. Its terms have no single
/// magnitude before there is a font size, a viewport and a containing block, so
/// §10.12's simplified form is one term per unit in §10.13's order: the
/// percentage first, then the units sorted ASCII case-insensitively, `px` in
/// its alphabetical place (`calc-serialization`,
/// `calc-dimension-serialization-order`).
void test_a_sum_that_cannot_fold_still_has_an_order() {
    ok("width", "calc(10px + 1vmin + 10%)", "calc(10% + 10px + 1vmin)");
    ok("width", "calc(10px + 1vmin)", "calc(10px + 1vmin)");
    ok("width", "calc(10px + 1em)", "calc(1em + 10px)");
    ok("width", "calc(1vmin - 10px)", "calc(-10px + 1vmin)");
    ok("width", "calc(-10px + 1em)", "calc(1em - 10px)");
    ok("height", "calc(1ch + 1cap)", "calc(1cap + 1ch)");
    ok("height", "calc(1rcap + 1px)", "calc(1px + 1rcap)"); // `px` sorts; it does not lead.
    ok("height", "calc(1lvw + 1px)", "calc(1lvw + 1px)");
    ok("width", "calc(3 * (1em + 1px))", "calc(3em + 3px)"); // A coefficient scales every term.
    // A unit with no basis still has arithmetic. `fr` converts to
    // nothing and never will, which is a different fact from `1fr + 1fr`.
    ok("grid-template-rows", "calc(1fr + 1fr)", "calc(2fr)");

    // A comparison still cannot be decided: `min(1em, 1px)` has no order
    // before a font size, exactly as `min(10px, 5%)` has none before a
    // containing block. But each side of it is a calculation like any other and
    // simplifies like any other, which is what `minmax-length-percent-serialize`
    // and `calc-infinity-nan-serialize-length` end on.
    ok("width", "min(1em, 1px)", "min(1em, 1px)");
    ok("width", "min(10% + 30px, 5em + 5%)", "min(10% + 30px, 5% + 5em)");
    ok("width", "calc(1 * min(NaN * 2px, NaN * 4em))", "calc(1 * min(NaN * 1px, NaN * 1em))");
    ok("width", "clamp(1rem, 2vw, 3rem)", "clamp(1rem, 2vw, 3rem)");
    ok("width", "min(1em)", "calc(1em)"); // One argument is not a comparison.

    // A sum around such a comparison is a tree (CSS Values 4 §10.12, §10.13):
    // what folds around the min() folds, a product's number comes first, a
    // product inside a sum is parenthesised, and a sum's terms are sorted:
    // numbers, percentages, dimensions by unit, then the rest in order
    // (calc-serialization-002, calc-nesting-002, minmax-*-serialize).
    ok("width", "calc((min(10px, 20%) + max(1rem, 2%)) * 2)",
       "calc(2 * (min(10px, 20%) + max(1rem, 2%)))");
    ok("width", "calc(0px - (1 * (-10px + min(20%, 20px))))",
       "calc(0px - (1 * (-10px + min(20%, 20px))))");
    ok("width", "calc(min(1px, 1in) + max(100px + 1em, 10px + 1in) + 1px)",
       "calc(2px + max(1em + 100px, 106px))");
    ok("width", "calc(2 * (.2 * min(1em, 1px)) + 1px)", "calc(1px + (0.4 * min(1em, 1px)))");
    ok("width", "calc(min(1%, 2%) + max(3%, 4%) + 10%)", "calc(10% + min(1%, 2%) + max(3%, 4%))");
    ok("width", "max((min(10%, 30px) + 10px) * 2 + 10px, 5em + 5%)",
       "max(10px + (2 * (10px + min(10%, 30px))), 5% + 5em)");
    // A leaf's own calc() stays a leaf: nothing is hoisted but numbers,
    // and a product writes its numeric factor first (css-color's
    // `calc(10deg * sign(1em - 10px))`).
    ok("width", "calc(pow(2, sign(1em - 18px)) * 1px)", "calc(1px * pow(2, sign(1em - 18px)))");
    // A signed zero keeps its sign through the tree, and a unit whose
    // canonical spelling would lose digits keeps the author's: the cascade
    // parses this text again (signs-abs-computed, typed_arithmetic).
    ok("scale", "clamp(-1, 1 / sign(-0em / 1px), 1)", "clamp(-1, 1 / sign(-0em / 1px), 1)");
    ok("margin-left", "calc(1px * 10em / -0em)", "calc(1px * 10em / -0em)");
    ok("image-resolution", "calc(100dpi + 20dpi * sign(38px - 2em))",
       "calc(100dpi + (20dpi * sign(-2em + 38px)))");
}

/// The other half of §10.11's calculation context is the property's.
/// `text-indent: min(1px, 0%)` resolves against a containing block and
/// `border-left-width: min(1px, 0%)` has nothing to resolve against, so the two
/// look alike and only one of them is a value (`minmax-length-invalid`,
/// `signs-abs-invalid`).
void test_a_percentage_needs_a_property_that_takes_one() {
    bad("border-left-width", "min(1px, 0%)");
    bad("border-left-width", "max(1px, 0%)");
    bad("font-weight", "sign(10%)");
    bad("outline-width", "calc(10%)");
    ok("text-indent", "min(1px, 0%)", "min(1px, 0%)");
    ok("width", "calc(50% - 10px)", "calc(50% - 10px)");
    // A free-form property answers yes and has to: the grammar is not modelled,
    // so a guess would lose two values that are perfectly ordinary.
    ok("transform", "translate(50%)", "translate(50%)");
    ok("background-position", "calc(50% - 1px)", "calc(50% - 1px)");
    // A percentage outside a math function is not this rule's business.
    ok("color", "hsl(calc(1deg) 82% 43%)", "rgb(200, 23, 20)");

    // `line-height` and `tab-size` are why the two <number>-and-<length> kinds
    // are two: `line-height: 50%` is half the font size and `tab-size: 50%` is
    // nothing at all. CSS Text 4 writes `<number [0,inf]> | <length [0,inf]>`.
    ok("line-height", "50%", "50%");
    ok("line-height", "calc(50% + 1px)", "calc(50% + 1px)");
    bad("tab-size", "50%");
    bad("tab-size", "abs(10%)");
    ok("tab-size", "4", "4");
    ok("tab-size", "10px", "10px");
}

} // namespace
